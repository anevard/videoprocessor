#pragma once
#include "LevelsScopeData.h"
#include <QDateTime>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <QWidget>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <tuple>
#include <vector>

// The Levels Scope chart: brightness over time on a PQ nits axis, the picked
// sections side by side with the gaps between them removed. Two views:
// Input scope (what VP measured and what the stream declared) and In vs out
// (the master's range against where the profile put it on screen). A QPainter
// port of the web page's canvas, every operator ruling kept (GUI-PLAN item K).
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

        // The sections to draw, in time order; the screen's peak and black.
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
            update();
        }
        void setScreen(double peakNits, double blackNits) { screenPeak_ = peakNits; screenBlack_ = blackNits; update(); }
        void setNames(std::function<QString(const LevelsScopeData::Section&)> name) { name_ = std::move(name); update(); }
        void setView(View v) { view_ = v; update(); }
        void setRange(Range r) { range_ = r; update(); }
        void setShown(const std::string& key, bool on) { shown_[key] = on; update(); }
        bool shown(const std::string& key) const { auto f = shown_.find(key); return f == shown_.end() || f->second; }
        // 0 shows everything.
        void setWindow(double seconds)
        {
            const double mid = start_ + windowLength() / 2.0;
            window_ = seconds;
            start_ = mid - windowLength() / 2.0;
            clampStart();
            update();
        }
        double window() const { return window_; }
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
            update();
        }
        // Called with a hover description, or empty when the pointer leaves.
        std::function<void(const QString&)> onHover;
        // Called when zoom or drag moves the view.
        std::function<void()> onMoved;

    protected:
        void paintEvent(QPaintEvent*) override
        {
            QPainter p(this);
            p.fillRect(rect(), QColor(QStringLiteral("#101b27")));
            p.setRenderHint(QPainter::Antialiasing);
            QFont font(QStringLiteral("Consolas"));
            font.setPixelSize(12);
            p.setFont(font);
            const QFontMetrics fm(font);
            const double w = width(), h = height();
            if (segs_.empty())
            {
                p.setPen(Muted());
                p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Open logs, then pick one or more sections above."));
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
                for (double t = std::ceil((g.s->a + 1) / step) * step; t < g.s->b; t += step)
                {
                    const double x = X(g.off + t - g.s->a);
                    if (x - xa < 30 || xb - x < 20) continue;
                    const QString clock = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(t)).toString(QStringLiteral("HH:mm"));
                    p.drawText(QRectF(x - 30, h - PadB + 4, 60, 16), Qt::AlignHCenter | Qt::AlignTop, clock);
                }
            }
            if (view_ == View::InOut) drawInOut(p, X, Y, W, xEnd, inY);
            else drawInput(p, X, Y, W);
            if (hover_ >= 0.0)
            {
                p.setPen(QPen(QColor(232, 234, 236, 128), 1.0));
                p.drawLine(QPointF(X(hover_), PadT), QPointF(X(hover_), h - PadB));
            }
            p.restore();
            drawMarginLabels(p, Y, inY, xEnd, h);
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
            update();
            if (onMoved) onMoved();
            e->accept();
        }

    private:
        static constexpr double PadL = 64, PadR = 84, PadT = 26, PadB = 30;
        struct Seg { const LevelsScopeData::Section* s; double off, len; };
        struct Axis { double lo, hi; std::vector<double> ticks; };

        static QColor Grid() { return QColor(QStringLiteral("#2d475f")); }
        static QColor Muted() { return QColor(QStringLiteral("#7d8a97")); }
        static QColor Fg() { return QColor(QStringLiteral("#e8eaec")); }
        static QColor Fg2() { return QColor(QStringLiteral("#b4b9bf")); }

        static Axis AxisFor(Range r)
        {
            if (r == Range::Shadows) return { 0.00005, 30, { 0.0001, 0.0003, 0.001, 0.003, 0.01, 0.03, 0.1, 0.3, 1, 3, 10, 30 } };
            if (r == Range::Highlights) return { 5, 4000, { 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 4000 } };
            return { 0.0001, 4000, { 0.0001, 0.001, 0.01, 0.1, 1, 10, 100, 1000, 4000 } };
        }

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
            const double t = g.s->a + (v - g.off);
            const LevelsScopeData::Point* best = nullptr;
            for (const auto& point : g.s->points)
                if (!best || std::abs(point.t - t) < std::abs(best->t - t)) best = &point;
            return best;
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
            {
                const auto Xp = [&](const LevelsScopeData::Point& pt) { return X(g.off + pt.t - g.s->a); };
                for (const Trace& tr : InputTraces())
                {
                    if (!shown(tr.key)) continue;
                    if (tr.dots)
                    {
                        const double r = W < 1500 ? 2.6 : 1.6;
                        QColor c = tr.color;
                        c.setAlphaF(0.75f);
                        p.setPen(Qt::NoPen);
                        p.setBrush(c);
                        for (const auto& pt : g.s->points)
                        {
                            const double v = value(pt.s, tr.key);
                            if (v > 0) p.drawEllipse(QPointF(Xp(pt), Y(v)), r, r);
                        }
                        p.setBrush(Qt::NoBrush);
                        continue;
                    }
                    QPen pen(tr.color, tr.width);
                    if (tr.dashed) pen.setDashPattern(tr.step ? QVector<qreal>{ 6 / tr.width, 5 / tr.width } : QVector<qreal>{ 7 / tr.width, 4 / tr.width });
                    p.setPen(pen);
                    QPainterPath path;
                    bool open = false;
                    qint64 prevT = 0;
                    double prevY = 0;
                    for (const auto& pt : g.s->points)
                    {
                        const double v = value(pt.s, tr.key);
                        if (!(v > 0)) { open = false; continue; }
                        const double x = Xp(pt), y = Y(v);
                        if (open && pt.t - prevT <= 30)
                        {
                            if (tr.step) path.lineTo(x, prevY);
                            path.lineTo(x, y);
                        }
                        else path.moveTo(x, y);
                        open = true;
                        prevT = pt.t;
                        prevY = y;
                    }
                    p.drawPath(path);
                }
            }
        }

        template<class FX, class FY, class FIn>
        void drawInOut(QPainter& p, const FX& X, const FY& Y, double W, double xEnd, const FIn& inY)
        {
            const double bw = (std::max)(1.0, 5.0 / W * (xEnd - PadL));
            const auto& traces = InOutTraces();
            const auto avgLine = [&](const std::vector<std::tuple<qint64, double, double>>& pts, const QColor& color) {
                for (const auto& [c, width] : { std::pair<QColor, double>{ QColor(QStringLiteral("#101b27")), 4.5 }, { color, 2.0 } })
                {
                    QPen pen(c, width);
                    pen.setJoinStyle(Qt::RoundJoin);
                    p.setPen(pen);
                    QPainterPath path;
                    bool open = false;
                    qint64 prev = 0;
                    for (const auto& [t, x, y] : pts)
                    {
                        if (open && t - prev <= 30) path.lineTo(x, y); else path.moveTo(x, y);
                        open = true;
                        prev = t;
                    }
                    p.drawPath(path);
                }
            };
            for (const Seg& g : segs_)
            {
                std::vector<std::tuple<qint64, double, double>> inAvg, outAvg;
                for (const auto& pt : g.s->points)
                {
                    if (!pt.computed || !pt.s.rangeKnown || !(pt.s.masteringBlackNits > 0)) continue;
                    const double x = X(g.off + pt.t - g.s->a);
                    if (x < PadL - bw || x > xEnd + bw) continue;
                    const double x0 = x - bw / 2;
                    if (shown("inband"))
                        p.fillRect(QRectF(x0, Y(pt.inPeak), bw, Y(pt.s.masteringBlackNits) - Y(pt.inPeak)), traces[0].color);
                    if (shown("outband"))
                        p.fillRect(QRectF(x0, Y(pt.outPeak), bw, Y(screenBlack_) - Y(pt.outPeak)), traces[1].color);
                    if (pt.inAverage > 0)
                    {
                        inAvg.emplace_back(pt.t, x, Y(pt.inAverage));
                        outAvg.emplace_back(pt.t, x, Y(pt.outAverage));
                    }
                }
                if (shown("inband")) avgLine(inAvg, QColor(255, 255, 255));
                if (shown("outband")) avgLine(outAvg, QColor(0x5c, 0xf2, 0x9a));
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
            const double r = (std::min)(2.4, (std::max)(1.2, bw / 2));
            p.setPen(Qt::NoPen);
            for (const Seg& g : segs_)
                for (const auto& pt : g.s->points)
                {
                    if (!pt.computed || !pt.s.rangeKnown || !(pt.s.masteringBlackNits > 0)) continue;
                    const double x = X(g.off + pt.t - g.s->a);
                    if (x < PadL - bw || x > xEnd + bw) continue;
                    for (int i = 0; i < 3; ++i)
                    {
                        if (!shown(traces[2 + i].key)) continue;
                        p.setBrush(traces[2 + i].color);
                        p.drawEllipse(QPointF(x, Y(pt.outProbe[i])), r, r);
                    }
                }
            p.setBrush(Qt::NoBrush);
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
        bool dragging_ = false;
        double dragX_ = 0.0, dragStart_ = 0.0;
    };
}
