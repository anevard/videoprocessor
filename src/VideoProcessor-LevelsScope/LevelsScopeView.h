#pragma once
#include "LevelsScopeData.h"
#include <QDateTime>
#include <QElapsedTimer>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QWheelEvent>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <vector>

// The Levels Scope chart: brightness over time on a PQ nits axis, the picked
// sections side by side with the gaps between them removed. Two views:
// Input scope (what VP measured and what the stream declared) and In vs out
// (the master's range against where the profile put it on screen). A QPainter
// port of the web page's canvas, every operator ruling kept (GUI-PLAN item K).
//
// Drawn like a hardware scope: samples are binned into pixel columns (the
// column's lowest, highest and mean value), so a redraw costs the chart's
// width, not the log's length, and samples off screen are skipped. The data
// layer is cached; hovering redraws only the crosshair.
// No Q_OBJECT: callbacks are plain functions, as on his Config page.
namespace LevelsScopeView
{
    enum class View { Input, InOut };
    enum class Range { Full, Shadows, Highlights };

    struct Trace
    {
        std::string key;
        QString name;
        QColor color;
        bool dots = false, step = false, dashed = false;
        double width = 1.6;
    };

    inline const std::vector<Trace>& InputTraces()
    {
        static const std::vector<Trace> traces = {
            { "detpk", QStringLiteral("Measured peak"), QColor(0x39, 0x87, 0xe5), true },
            { "l1pk", QStringLiteral("L1 peak"), QColor(0xd9, 0x59, 0x26) },
            { "l1avg", QStringLiteral("L1 average"), QColor(0x90, 0x85, 0xe9), false, false, true },
            { "detlin", QStringLiteral("Measured avg (linear)"), QColor(0xc9, 0x85, 0x00) },
            { "detpq", QStringLiteral("Measured avg (PQ)"), QColor(0x19, 0x9e, 0x70) },
            { "mblack", QStringLiteral("Mastering black (metadata)"), QColor(0xf4, 0xf5, 0xf6), false, true, false, 2.4 },
            { "mpeak", QStringLiteral("Mastering peak (metadata)"), QColor(0x8f, 0x96, 0x9d), false, true, true, 1.6 },
        };
        return traces;
    }

    inline const std::vector<Trace>& InOutTraces()
    {
        static const std::vector<Trace> traces = {
            { "inband", QStringLiteral("Master: black to peak, avg line"), QColor(255, 255, 255, 71) },
            { "outband", QStringLiteral("On screen: where they landed"), QColor(70, 215, 120, 128) },
            { "p1", QStringLiteral("0.1 nit lands at"), QColor(0xed, 0xa1, 0x00) },
            { "p01", QStringLiteral("0.01 nit lands at"), QColor(0xe6, 0x67, 0x67) },
            { "p001", QStringLiteral("0.001 nit lands at"), QColor(0x4c, 0x9b, 0xf0) },
        };
        return traces;
    }

    // SMPTE ST 2084 for the axis only (drawing position, not tone mapping).
    inline double Pq(double nits)
    {
        const double m1 = 2610.0 / 16384, m2 = 2523.0 / 4096 * 128, c1 = 3424.0 / 4096,
            c2 = 2413.0 / 4096 * 32, c3 = 2392.0 / 4096 * 32;
        const double y = std::pow((std::max)(nits, 0.0) / 10000.0, m1);
        return std::pow((c1 + c2 * y) / (1 + c3 * y), m2);
    }

    // Whole from 100, one decimal from 1, three from 0.01, else four.
    inline QString Nits(double v)
    {
        if (!(v >= 0.0)) return QStringLiteral("-");
        return QString::number(v, 'f', v >= 100 ? 0 : v >= 1 ? 1 : v >= 0.01 ? 3 : 4);
    }

    class Chart : public QWidget
    {
    public:
        explicit Chart(QWidget* parent = nullptr) : QWidget(parent)
        {
            setMouseTracking(true);
            setMinimumHeight(360);
            setMaximumHeight(760);
            for (const auto* list : { &InputTraces(), &InOutTraces() })
                for (const Trace& t : *list) shown_[t.key] = true;
        }

        // The sections to draw, in time order.
        void setSections(std::vector<const LevelsScopeData::Section*> sections)
        {
            segs_.clear();
            double off = 0.0;
            for (const auto* s : sections)
            {
                segs_.push_back({ s, off, static_cast<double>(s->b - s->a) });
                off += static_cast<double>(s->b - s->a);
            }
            total_ = (std::max)(off, 1.0);
            clampStart();
            changed();
        }
        // The data under the sections changed (a new profile placed them).
        void refresh() { changed(); }
        void setScreen(double peakNits, double blackNits) { screenPeak_ = peakNits; screenBlack_ = blackNits; changed(); }
        void setNames(std::function<QString(const LevelsScopeData::Section&)> name) { name_ = std::move(name); changed(); }
        void setView(View v) { view_ = v; changed(); }
        void setRange(Range r) { range_ = r; changed(); }
        void setShown(const std::string& key, bool on) { shown_[key] = on; changed(); }
        bool shown(const std::string& key) const { auto f = shown_.find(key); return f == shown_.end() || f->second; }
        // 0 shows everything.
        void setWindow(double seconds)
        {
            const double mid = start_ + windowLength() / 2.0;
            window_ = seconds;
            start_ = mid - windowLength() / 2.0;
            clampStart();
            changed();
        }
        // Scroll position, 0 to 1000; -1 when everything is shown.
        int position() const
        {
            const double span = total_ - windowLength();
            return span > 0 ? static_cast<int>(std::lround(start_ / span * 1000.0)) : -1;
        }
        void setPosition(int value)
        {
            start_ = (total_ - windowLength()) * value / 1000.0;
            clampStart();
            changed();
        }
        // How long the last data layer took to draw, in milliseconds.
        double lastDrawMs() const { return lastDrawMs_; }
        // Called with a hover description, or empty when the pointer leaves.
        std::function<void(const QString&)> onHover;
        // Called when zoom or drag moves the view.
        std::function<void()> onMoved;

    protected:
        void paintEvent(QPaintEvent*) override
        {
            const qreal dpr = devicePixelRatioF();
            if (dirty_ || layer_.size() != size() * dpr)
            {
                QElapsedTimer timer;
                timer.start();
                layer_ = QPixmap(size() * dpr);
                layer_.setDevicePixelRatio(dpr);
                QPainter p(&layer_);
                drawLayer(p);
                dirty_ = false;
                lastDrawMs_ = timer.nsecsElapsed() / 1e6;
            }
            QPainter p(this);
            p.drawPixmap(0, 0, layer_);
            if (hover_ >= 0.0 && !segs_.empty())
            {
                const double x = PadL + (hover_ - start_) / windowLength() * plotWidth();
                p.setPen(QPen(QColor(232, 234, 236, 128), 1.0));
                p.drawLine(QPointF(x, PadT), QPointF(x, height() - PadB));
            }
        }

        void mousePressEvent(QMouseEvent* e) override
        {
            dragging_ = true;
            dragX_ = e->position().x();
            dragStart_ = start_;
            setCursor(Qt::ClosedHandCursor);
        }
        void mouseReleaseEvent(QMouseEvent*) override { dragging_ = false; unsetCursor(); }
        void mouseMoveEvent(QMouseEvent* e) override
        {
            if (dragging_)
            {
                start_ = dragStart_ - (e->position().x() - dragX_) / plotWidth() * windowLength();
                clampStart();
                dirty_ = true;
                if (onMoved) onMoved();
            }
            hover_ = valueAt(e->position().x());
            update();
            if (onHover) onHover(describe(hover_));
        }
        void leaveEvent(QEvent*) override
        {
            hover_ = -1.0;
            update();
            if (onHover) onHover(QString());
        }
        void wheelEvent(QWheelEvent* e) override
        {
            if (segs_.empty()) return;
            const double at = valueAt(e->position().x()), W = windowLength();
            const double next = (std::min)((std::max)(W * (e->angleDelta().y() < 0 ? 1.25 : 0.8), 120.0), total_);
            window_ = next >= total_ ? 0.0 : next;
            start_ = at - (at - start_) * next / W;
            clampStart();
            changed();
            if (onMoved) onMoved();
            e->accept();
        }

    private:
        static constexpr double PadL = 64, PadR = 84, PadT = 26, PadB = 30;
        struct Seg { const LevelsScopeData::Section* s; double off, len; };
        struct Axis { double lo, hi; std::vector<double> ticks; };
        // One pixel column of one quantity.
        struct Column { double x = 0, lo = 0, hi = 0, sum = 0, last = 0; int n = 0; qint64 t0 = 0, t1 = 0; };

        static QColor Grid() { return QColor(QStringLiteral("#2d475f")); }
        static QColor Muted() { return QColor(QStringLiteral("#93a8bb")); }
        static QColor Fg() { return QColor(QStringLiteral("#e8eaec")); }
        static QColor Fg2() { return QColor(QStringLiteral("#b4b9bf")); }
        static QColor Card() { return QColor(QStringLiteral("#101b27")); }

        static Axis AxisFor(Range r)
        {
            if (r == Range::Shadows) return { 0.00005, 30, { 0.0001, 0.0003, 0.001, 0.003, 0.01, 0.03, 0.1, 0.3, 1, 3, 10, 30 } };
            if (r == Range::Highlights) return { 5, 4000, { 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 4000 } };
            return { 0.0001, 4000, { 0.0001, 0.001, 0.01, 0.1, 1, 10, 100, 1000, 4000 } };
        }

        void changed() { dirty_ = true; update(); }
        double windowLength() const { return window_ > 0 ? (std::min)(window_, total_) : total_; }
        double plotWidth() const { return (std::max)(1.0, width() - PadL - PadR); }
        double valueAt(double x) const { return start_ + (x - PadL) / plotWidth() * windowLength(); }
        void clampStart() { start_ = (std::min)((std::max)(start_, 0.0), (std::max)(total_ - windowLength(), 0.0)); }

        const Seg* segAt(double v) const
        {
            for (const Seg& g : segs_) if (v >= g.off && v <= g.off + g.len) return &g;
            return nullptr;
        }
        static const LevelsScopeData::Point* nearest(const Seg& g, double v)
        {
            const auto& pts = g.s->points;
            if (pts.empty()) return nullptr;
            const double t = g.s->a + (v - g.off);
            auto it = std::lower_bound(pts.begin(), pts.end(), t,
                [](const LevelsScopeData::Point& p, double value) { return p.t < value; });
            if (it == pts.end()) return &pts.back();
            if (it != pts.begin() && std::abs(std::prev(it)->t - t) < std::abs(it->t - t)) --it;
            return &*it;
        }

        // The samples of `g` on screen, binned into pixel columns of `value`
        // (values <= 0 are left out, as missing).
        template<class FX, class FV>
        std::vector<Column> bin(const Seg& g, const FX& X, double margin, FV value) const
        {
            std::vector<Column> columns;
            const auto& pts = g.s->points;
            const double t0 = g.s->a + (start_ - g.off) - margin, t1 = g.s->a + (start_ + windowLength() - g.off) + margin;
            auto it = std::lower_bound(pts.begin(), pts.end(), t0,
                [](const LevelsScopeData::Point& p, double v) { return p.t < v; });
            for (; it != pts.end() && it->t <= t1; ++it)
            {
                const double v = value(*it);
                if (!(v > 0)) continue;
                const double x = std::floor(X(g.off + it->t - g.s->a)) + 0.5;
                if (columns.empty() || columns.back().x != x || it->t - columns.back().t1 > 30)
                {
                    Column c;
                    c.x = x; c.lo = c.hi = v; c.t0 = it->t;
                    columns.push_back(c);
                }
                Column& c = columns.back();
                c.lo = (std::min)(c.lo, v);
                c.hi = (std::max)(c.hi, v);
                c.sum += v;
                c.last = v;
                ++c.n;
                c.t1 = it->t;
            }
            return columns;
        }

        void drawLayer(QPainter& p)
        {
            p.fillRect(rect(), Card());
            p.setRenderHint(QPainter::Antialiasing);
            QFont font(QStringLiteral("Consolas"));
            font.setPixelSize(12);
            p.setFont(font);
            const QFontMetrics fm(font);
            const double w = width(), h = height();
            if (segs_.empty())
            {
                p.setPen(Muted());
                p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Open logs, then tick one or more sections above."));
                return;
            }
            const double W = windowLength(), v0 = start_, xEnd = w - PadR;
            const Axis axis = AxisFor(range_);
            const double ylo = Pq(axis.lo), yhi = Pq(axis.hi);
            const auto X = [&](double v) { return PadL + (v - v0) / W * (xEnd - PadL); };
            const auto Y = [&](double n) { return PadT + (1.0 - (Pq(n) - ylo) / (yhi - ylo)) * (h - PadT - PadB); };
            const auto inY = [&](double n) { return n >= axis.lo && n <= axis.hi; };

            // The screen band and the graticule; a tick label only where it clears the last one.
            const double bandTop = Y((std::min)(screenPeak_, axis.hi)), bandBot = Y((std::max)(screenBlack_, axis.lo));
            if (bandBot > bandTop) p.fillRect(QRectF(PadL, bandTop, xEnd - PadL, bandBot - bandTop), QColor(120, 170, 255, 18));
            double lastLabelY = 1e9;
            for (double n : axis.ticks)
            {
                p.setPen(QPen(Grid(), 1.0));
                p.drawLine(QPointF(PadL, Y(n)), QPointF(xEnd, Y(n)));
                if (lastLabelY - Y(n) < 15) continue;
                lastLabelY = Y(n);
                p.setPen(Muted());
                p.drawText(QRectF(0, Y(n) - 8, PadL - 8, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(n));
            }
            for (double n : { screenPeak_, screenBlack_ })
            {
                if (!inY(n)) continue;
                p.setPen(QPen(Fg2(), 1.0, Qt::DotLine));
                p.drawLine(QPointF(PadL, Y(n)), QPointF(xEnd, Y(n)));
            }

            p.save();
            p.setClipRect(QRectF(PadL, 0, xEnd - PadL, h));
            // Sections: joins, names and clock ticks.
            const double step = W > 10800 ? 1800 : W > 3600 ? 600 : W > 1200 ? 300 : W > 400 ? 60 : 30;
            for (const Seg& g : segs_)
            {
                const double xa = X(g.off), xb = X(g.off + g.len);
                if (xb < PadL || xa > xEnd) continue;
                p.setPen(QPen(Fg2(), 1.0));
                p.drawLine(QPointF(xa, 4), QPointF(xa, h - PadB));
                const double room = (std::min)(xb, xEnd) - (std::max)(xa, PadL) - 10;
                const QString shortName = name_ ? name_(*g.s) : g.s->kind;
                const QDateTime start = QDateTime::fromSecsSinceEpoch(g.s->a);
                QString label = shortName + QStringLiteral(" · ") + start.toString(QStringLiteral("d MMM HH:mm"));
                if (fm.horizontalAdvance(label) > room) label = fm.elidedText(shortName, Qt::ElideRight, static_cast<int>(room));
                if (room > 14)
                {
                    p.setPen(Fg());
                    p.drawText(QPointF((std::max)(xa, PadL) + 5, 16), label);
                }
                p.setPen(Muted());
                const double first = (std::max)(std::ceil((g.s->a + 1) / step), std::floor((g.s->a + v0 - g.off) / step)) * step;
                for (double t = first; t < g.s->b && X(g.off + t - g.s->a) <= xEnd + 40; t += step)
                {
                    const double x = X(g.off + t - g.s->a);
                    if (x - xa < 30 || xb - x < 20) continue;
                    const QString clock = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(t)).toString(QStringLiteral("HH:mm"));
                    p.drawText(QRectF(x - 30, h - PadB + 4, 60, 16), Qt::AlignHCenter | Qt::AlignTop, clock);
                }
            }
            if (view_ == View::InOut) drawInOut(p, X, Y, W, xEnd, inY);
            else drawInput(p, X, Y, W);
            p.restore();
            drawMarginLabels(p, Y, inY, xEnd, h);
        }

        template<class FX, class FY>
        void drawInput(QPainter& p, const FX& X, const FY& Y, double W)
        {
            const auto value = [](const LevelsScopeLog::Sample& s, const std::string& key) {
                if (key == "detpk") return s.measured ? s.detectedPeakNits : 0.0;
                if (key == "l1pk") return s.authored ? s.authoredPeakNits : 0.0;
                if (key == "l1avg") return s.authored ? s.authoredAverageNits : 0.0;
                if (key == "detlin") return s.measured ? s.detectedLinearAverageNits : 0.0;
                if (key == "detpq") return s.measured ? s.detectedPqAverageNits : 0.0;
                if (key == "mblack") return s.rangeKnown ? s.masteringBlackNits : 0.0;
                return s.rangeKnown ? s.masteringPeakNits : 0.0;
            };
            for (const Seg& g : segs_)
                for (const Trace& tr : InputTraces())
                {
                    if (!shown(tr.key)) continue;
                    const auto columns = bin(g, X, 30, [&](const LevelsScopeData::Point& pt) { return value(pt.s, tr.key); });
                    if (tr.dots)
                    {
                        // The column's highest and, when apart, its lowest.
                        const double r = W < 1500 ? 2.6 : 1.6;
                        QColor c = tr.color;
                        c.setAlphaF(0.75f);
                        p.setPen(Qt::NoPen);
                        p.setBrush(c);
                        for (const Column& col : columns)
                        {
                            p.drawEllipse(QPointF(col.x, Y(col.hi)), r, r);
                            if (Y(col.lo) - Y(col.hi) > 2 * r) p.drawEllipse(QPointF(col.x, Y(col.lo)), r, r);
                        }
                        p.setBrush(Qt::NoBrush);
                        continue;
                    }
                    QPen pen(tr.color, tr.width);
                    if (tr.dashed) pen.setDashPattern(tr.step ? QVector<qreal>{ 6 / tr.width, 5 / tr.width } : QVector<qreal>{ 7 / tr.width, 4 / tr.width });
                    p.setPen(pen);
                    // A line through the column means (steps through the last
                    // value), and the column's spread where it is wider than a pixel.
                    QPainterPath path;
                    const Column* prev = nullptr;
                    for (const Column& col : columns)
                    {
                        const double y = Y(tr.step ? col.last : col.sum / col.n);
                        if (prev && col.t0 - prev->t1 <= 30)
                        {
                            if (tr.step) path.lineTo(col.x, Y(prev->last));
                            path.lineTo(col.x, y);
                        }
                        else path.moveTo(col.x, y);
                        prev = &col;
                    }
                    p.drawPath(path);
                    if (!tr.step)
                    {
                        p.setPen(QPen(tr.color, 1.0));
                        for (const Column& col : columns)
                            if (Y(col.lo) - Y(col.hi) > 1.5) p.drawLine(QPointF(col.x, Y(col.hi)), QPointF(col.x, Y(col.lo)));
                    }
                }
        }

        template<class FX, class FY, class FIn>
        void drawInOut(QPainter& p, const FX& X, const FY& Y, double W, double xEnd, const FIn& inY)
        {
            const double bw = (std::max)(1.0, 5.0 / W * (xEnd - PadL));
            const auto& traces = InOutTraces();
            const auto placed = [](const LevelsScopeData::Point& pt) {
                return pt.computed && pt.s.rangeKnown && pt.s.masteringBlackNits > 0;
            };
            const auto avgLine = [&](const std::vector<Column>& columns, const QColor& color) {
                for (const auto& [c, width] : { std::pair<QColor, double>{ Card(), 4.5 }, { color, 2.0 } })
                {
                    QPen pen(c, width);
                    pen.setJoinStyle(Qt::RoundJoin);
                    p.setPen(pen);
                    QPainterPath path;
                    const Column* prev = nullptr;
                    for (const Column& col : columns)
                    {
                        const double y = Y(col.sum / col.n);
                        if (prev && col.t0 - prev->t1 <= 30) path.lineTo(col.x, y); else path.moveTo(col.x, y);
                        prev = &col;
                    }
                    p.drawPath(path);
                }
            };
            for (const Seg& g : segs_)
            {
                const auto peaks = bin(g, X, 30, [&](const auto& pt) { return placed(pt) ? pt.inPeak : 0.0; });
                const auto blacks = bin(g, X, 30, [&](const auto& pt) { return placed(pt) ? pt.s.masteringBlackNits : 0.0; });
                const auto outPeaks = bin(g, X, 30, [&](const auto& pt) { return placed(pt) ? pt.outPeak : 0.0; });
                p.setPen(Qt::NoPen);
                if (shown("inband"))
                    for (size_t i = 0; i < peaks.size() && i < blacks.size(); ++i)
                        p.fillRect(QRectF(peaks[i].x - bw / 2, Y(peaks[i].hi), bw, Y(blacks[i].lo) - Y(peaks[i].hi)), traces[0].color);
                if (shown("outband"))
                    for (const Column& col : outPeaks)
                        p.fillRect(QRectF(col.x - bw / 2, Y(col.hi), bw, Y(screenBlack_) - Y(col.hi)), traces[1].color);
                if (shown("inband"))
                    avgLine(bin(g, X, 30, [&](const auto& pt) { return placed(pt) ? pt.inAverage : 0.0; }), QColor(255, 255, 255));
                if (shown("outband"))
                    avgLine(bin(g, X, 30, [&](const auto& pt) { return placed(pt) && pt.inAverage > 0 ? pt.outAverage : 0.0; }),
                        QColor(0x5c, 0xf2, 0x9a));
            }
            // The probes' origin lines, over the bands so the white band cannot wash them out.
            for (int i = 0; i < 3; ++i)
            {
                const Trace& tr = traces[2 + i];
                if (!shown(tr.key) || !inY(LevelsScopeData::Probes[i])) continue;
                QPen pen(tr.color, 1.5);
                pen.setDashPattern({ 4, 3.3 });
                p.setPen(pen);
                p.drawLine(QPointF(PadL, Y(LevelsScopeData::Probes[i])), QPointF(xEnd, Y(LevelsScopeData::Probes[i])));
            }
            // Where each probe landed: a dot at the column's mean, its spread as a stroke.
            const double r = (std::min)(2.4, (std::max)(1.2, bw / 2));
            for (int i = 0; i < 3; ++i)
            {
                const Trace& tr = traces[2 + i];
                if (!shown(tr.key)) continue;
                for (const Seg& g : segs_)
                {
                    const auto columns = bin(g, X, 30, [&](const auto& pt) { return placed(pt) ? pt.outProbe[i] : 0.0; });
                    p.setPen(QPen(tr.color, 1.0));
                    for (const Column& col : columns)
                        if (Y(col.lo) - Y(col.hi) > 1.5) p.drawLine(QPointF(col.x, Y(col.hi)), QPointF(col.x, Y(col.lo)));
                    p.setPen(Qt::NoPen);
                    p.setBrush(tr.color);
                    for (const Column& col : columns) p.drawEllipse(QPointF(col.x, Y(col.sum / col.n)), r, r);
                    p.setBrush(Qt::NoBrush);
                }
            }
        }

        // Right-margin labels, pushed apart so none overlaps; no mastering
        // values there (they would only be true for the last section shown).
        template<class FY, class FIn>
        void drawMarginLabels(QPainter& p, const FY& Y, const FIn& inY, double xEnd, double h)
        {
            struct Label { double y, ly; QString a, b; QColor c; };
            std::vector<Label> labels;
            if (inY(screenPeak_)) labels.push_back({ Y(screenPeak_), 0, QStringLiteral("screen"), Nits(screenPeak_), Fg2() });
            if (inY(screenBlack_)) labels.push_back({ Y(screenBlack_), 0, QStringLiteral("screen"), Nits(screenBlack_), Fg2() });
            if (view_ == View::Input)
            {
                const double vr = (std::min)(start_ + windowLength(), total_) - 1e-6;
                const Seg* g = segAt(vr);
                if (!g && !segs_.empty()) g = &segs_.back();
                const LevelsScopeData::Point* q = g ? nearest(*g, vr) : nullptr;
                if (q && q->s.rangeKnown)
                    for (const Trace& tr : InputTraces())
                    {
                        if (!tr.step || !shown(tr.key)) continue;
                        const double v = tr.key == "mblack" ? q->s.masteringBlackNits : q->s.masteringPeakNits;
                        if (v > 0 && inY(v))
                            labels.push_back({ Y(v), 0, QStringLiteral("mastering"),
                                tr.key == "mblack" ? QStringLiteral("black") : QStringLiteral("peak"), tr.color });
                    }
            }
            std::sort(labels.begin(), labels.end(), [](const Label& a, const Label& b) { return a.y < b.y; });
            for (size_t i = 0; i < labels.size(); ++i)
                labels[i].ly = i == 0 ? labels[i].y : (std::max)(labels[i].y, labels[i - 1].ly + 28);
            if (!labels.empty())
            {
                const double over = labels.back().ly - (h - 8);
                if (over > 0) for (Label& l : labels) l.ly -= over;
            }
            for (const Label& l : labels)
            {
                QColor line = l.c;
                line.setAlphaF(0.6f);
                p.setPen(QPen(line, 1.0));
                p.drawLine(QPointF(xEnd, l.y), QPointF(xEnd + 6, l.ly - 4));
                p.setPen(l.c);
                p.drawText(QPointF(xEnd + 9, l.ly - 6), l.a);
                p.drawText(QPointF(xEnd + 9, l.ly + 8), l.b);
            }
        }

        QString describe(double v) const
        {
            const Seg* g = segAt(v);
            if (!g) return QString();
            const LevelsScopeData::Point* pt = nearest(*g, v);
            if (!pt) return QString();
            const QString when = QDateTime::fromSecsSinceEpoch(pt->t).toString(QStringLiteral("HH:mm:ss · d MMM"));
            const QString name = name_ ? name_(*g->s) : g->s->kind;
            const auto& s = pt->s;
            QStringList parts{ QStringLiteral("<b>%1</b> %2").arg(when, name.toHtmlEscaped()) };
            if (view_ == View::InOut)
            {
                if (!pt->computed) return parts.first() + QStringLiteral(" · black frame");
                const bool measured = s.measured && s.detectedPeakNits > 0;
                parts << QStringLiteral("%1 <b>%2 → %3</b>").arg(measured ? QStringLiteral("peak") : QStringLiteral("L1 peak"),
                    Nits(pt->inPeak), Nits(pt->outPeak));
                if (pt->inAverage > 0)
                    parts << QStringLiteral("%1 <b>%2 → %3</b>").arg(measured ? QStringLiteral("avg (PQ)") : QStringLiteral("L1 avg"),
                        Nits(pt->inAverage), Nits(pt->outAverage));
                for (int i = 0; i < 3; ++i)
                    parts << QStringLiteral("<b>%1 → %2</b>").arg(Nits(LevelsScopeData::Probes[i]), Nits(pt->outProbe[i]));
                parts << QStringLiteral("knee <b>%1</b>, exposure <b>%2</b>").arg(Nits(pt->kneeNits), QString::number(pt->exposure, 'f', 3));
                return parts.join(QStringLiteral(" &nbsp; "));
            }
            parts << QStringLiteral("peak <b>%1</b>").arg(s.measured ? Nits(s.detectedPeakNits) : QStringLiteral("-"));
            if (s.authored)
                parts << QStringLiteral("L1 peak <b>%1</b> &nbsp; L1 avg <b>%2</b>").arg(Nits(s.authoredPeakNits), Nits(s.authoredAverageNits));
            if (s.measured)
                parts << QStringLiteral("avg lin <b>%1</b> &nbsp; avg PQ <b>%2</b>").arg(Nits(s.detectedLinearAverageNits), Nits(s.detectedPqAverageNits));
            if (s.rangeKnown)
                parts << QStringLiteral("mastering <b>%1 – %2</b>").arg(Nits(s.masteringBlackNits), Nits(s.masteringPeakNits));
            return parts.join(QStringLiteral(" &nbsp; "));
        }

        std::vector<Seg> segs_;
        double total_ = 1.0, window_ = 0.0, start_ = 0.0, hover_ = -1.0;
        double screenPeak_ = 203.0, screenBlack_ = 0.0;
        View view_ = View::InOut;
        Range range_ = Range::Full;
        std::map<std::string, bool> shown_;
        std::function<QString(const LevelsScopeData::Section&)> name_;
        bool dragging_ = false, dirty_ = true;
        double dragX_ = 0.0, dragStart_ = 0.0, lastDrawMs_ = 0.0;
        QPixmap layer_;
    };
}
