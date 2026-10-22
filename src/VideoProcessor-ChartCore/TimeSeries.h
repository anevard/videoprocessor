#pragma once
#include <QColor>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QString>
#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

// Drawing a time series like a hardware scope: samples binned into pixel
// columns (each column's lowest, highest and mean value), so a redraw costs
// the chart's width, not the series' length; columns drawn as rectangle
// fills; dots drawn once and stamped. A point type needs only `t`, its time,
// with the points sorted by it. `gap` is the longest time between two
// samples that are still joined.
namespace TimeSeries
{
    // One pixel column of one quantity.
    struct Column { double x = 0, lo = 0, hi = 0, sum = 0, last = 0; int n = 0; double t0 = 0, t1 = 0; };

    // The points from t0 to t1, binned into pixel columns of `value` at
    // `toX(t)` (values <= 0 are left out, as missing; with `zeroIsValue`, only
    // negative ones). A column also ends where the samples are over `gap` apart.
    template<class P, class FX, class FV>
    std::vector<Column> Bin(const std::vector<P>& pts, double t0, double t1, const FX& toX, FV value, double gap,
        bool zeroIsValue = false)
    {
        std::vector<Column> columns;
        auto it = std::lower_bound(pts.begin(), pts.end(), t0,
            [](const P& p, double v) { return p.t < v; });
        for (; it != pts.end() && it->t <= t1; ++it)
        {
            const double v = value(*it);
            if (!(v > 0) && !(zeroIsValue && v == 0)) continue;
            const double x = std::floor(toX(it->t)) + 0.5;
            if (columns.empty() || columns.back().x != x || it->t - columns.back().t1 > gap)
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

    // A trace drawn as a hardware scope draws it: no anti-aliasing, each
    // pixel column filled from its lowest to its highest value (`spread`)
    // and joined to the column before. Anti-aliased strokes cost seconds
    // a frame on a large high-DPI window; rectangle fills cost almost
    // nothing. Dashes are gaps in the column pattern; columns further than
    // a pixel apart (zoomed in) are joined by a plain line.
    template<class FY>
    void DrawColumns(QPainter& p, const std::vector<Column>& columns, const FY& Y, const QColor& color,
        double thick, bool step, bool dashed, bool spread, double gap)
    {
        p.save();
        p.setRenderHint(QPainter::Antialiasing, false);
        QPen pen(color, thick);
        pen.setCapStyle(Qt::FlatCap);
        if (dashed) pen.setDashPattern({ 7 / thick, 4 / thick });
        p.setPen(pen);
        const double t = (std::max)(1.0, std::round(thick));
        const Column* prev = nullptr;
        double prevY = 0.0;
        for (const Column& col : columns)
        {
            const double y = Y(step ? col.last : col.sum / col.n);
            const bool joined = prev && col.t0 - prev->t1 <= gap;
            const bool adjacent = joined && col.x - prev->x <= 1.5;
            // Dense columns dash by position: 7 pixels on, 4 off.
            if (!dashed || !adjacent || std::fmod(col.x, 11.0) < 7.0)
            {
                double top = y, bottom = y;
                if (spread)
                {
                    top = (std::min)(top, Y(col.hi));
                    bottom = (std::max)(bottom, Y(col.lo));
                }
                if (adjacent)
                {
                    top = (std::min)(top, prevY);
                    bottom = (std::max)(bottom, prevY);
                }
                else if (joined && step)
                {
                    p.fillRect(QRectF(prev->x, prevY - t / 2, col.x - prev->x, t), color);
                    top = (std::min)(top, prevY);
                    bottom = (std::max)(bottom, prevY);
                }
                else if (joined)
                    p.drawLine(QPointF(prev->x, prevY), QPointF(col.x, y));
                p.fillRect(QRectF(col.x - t / 2, top - t / 2, t, bottom - top + t), color);
            }
            prev = &col;
            prevY = y;
        }
        p.restore();
    }

    // Dots drawn once per colour, size and pixel ratio and then stamped:
    // anti-aliased circles drawn one by one cost far more than the rest of
    // a chart.
    class DotCache
    {
    public:
        const QPixmap& Dot(const QColor& color, double r, qreal dpr)
        {
            const QString key = QStringLiteral("%1/%2/%3").arg(color.rgba()).arg(r).arg(dpr);
            auto found = dots_.find(key);
            if (found != dots_.end()) return found->second;
            const int side = static_cast<int>(std::ceil(2 * r + 2));
            QPixmap dot(QSize(side, side) * dpr);
            dot.setDevicePixelRatio(dpr);
            dot.fill(Qt::transparent);
            QPainter p(&dot);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(color);
            p.drawEllipse(QPointF(side / 2.0, side / 2.0), r, r);
            return dots_.emplace(key, dot).first->second;
        }

    private:
        std::map<QString, QPixmap> dots_;
    };

    inline void Stamp(QPainter& p, const QPixmap& dot, double x, double y)
    {
        const double half = dot.width() / dot.devicePixelRatio() / 2.0;
        p.drawPixmap(QPointF(x - half, y - half), dot);
    }

    // The point nearest time `t`; null when there are none.
    template<class P>
    const P* Nearest(const std::vector<P>& pts, double t)
    {
        if (pts.empty()) return nullptr;
        auto it = std::lower_bound(pts.begin(), pts.end(), t,
            [](const P& p, double value) { return p.t < value; });
        if (it == pts.end()) return &pts.back();
        if (it != pts.begin() && std::abs(std::prev(it)->t - t) < std::abs(it->t - t)) --it;
        return &*it;
    }
}
