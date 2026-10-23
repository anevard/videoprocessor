#pragma once
#include "PqAxis.h"
#include <algorithm>
#include <cmath>

// What a time-series chart shows: a time window over the whole length of its
// time line, and a brightness window on a PQ axis, with the wheel-zoom and
// drag maths. No widget: the chart turns the pointer into fractions of its
// plot, calls these and repaints. The chart passes its own limits in. Times
// are seconds along the chart's time line; brightness is in PQ.
namespace ChartViewport
{
    struct Limits
    {
        // The whole brightness range, in nits.
        double loNits, hiNits;
        // The narrowest brightness zoom, in PQ, and the narrowest time window, in seconds.
        double minBrightnessSpan, minWindow;
    };

    class Viewport
    {
    public:
        explicit Viewport(const Limits& limits)
            : limits_(limits), yLo_(PqAxis::Pq(limits.loNits)), yHi_(PqAxis::Pq(limits.hiNits)) {}

        // The length of the whole time line; the window stays inside it.
        void setTotal(double seconds)
        {
            total_ = seconds;
            clampStart();
        }
        double total() const { return total_; }
        double start() const { return start_; }
        double windowLength() const { return window_ > 0 ? (std::min)(window_, total_) : total_; }
        // The time at `fraction` of the way across the plot.
        double valueAt(double fraction) const { return start_ + fraction * windowLength(); }
        // The brightness shown, in PQ.
        double lo() const { return yLo_; }
        double hi() const { return yHi_; }

        // Show everything: the whole time line and the whole brightness range.
        void showAll()
        {
            window_ = 0.0;
            start_ = 0.0;
            yLo_ = PqAxis::Pq(limits_.loNits);
            yHi_ = PqAxis::Pq(limits_.hiNits);
        }
        // Show this brightness range, in nits.
        void setBrightness(double loNits, double hiNits)
        {
            yLo_ = PqAxis::Pq(loNits);
            yHi_ = PqAxis::Pq(hiNits);
            clampBrightness();
        }
        // Brightness scroll position, 0 (brightest at the top) to 1000; -1
        // when the whole brightness range is shown.
        int brightnessPosition() const
        {
            const double free = (PqAxis::Pq(limits_.hiNits) - PqAxis::Pq(limits_.loNits)) - (yHi_ - yLo_);
            return free > 1e-9 ? static_cast<int>(std::lround((PqAxis::Pq(limits_.hiNits) - yHi_) / free * 1000.0)) : -1;
        }
        void setBrightnessPosition(int value)
        {
            const double height = yHi_ - yLo_, free = (PqAxis::Pq(limits_.hiNits) - PqAxis::Pq(limits_.loNits)) - height;
            yHi_ = PqAxis::Pq(limits_.hiNits) - free * value / 1000.0;
            yLo_ = yHi_ - height;
            clampBrightness();
        }
        // 0 shows everything.
        void setWindow(double seconds)
        {
            const double mid = start_ + windowLength() / 2.0;
            window_ = seconds;
            start_ = mid - windowLength() / 2.0;
            clampStart();
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
        }

        // Drag moves the view both ways: `startDrag` when the button goes
        // down, then `drag` by the distance moved, as fractions of the plot's
        // width and height (down is positive).
        void startDrag()
        {
            dragStart_ = start_;
            dragLo_ = yLo_;
        }
        void drag(double dxFraction, double dyFraction)
        {
            start_ = dragStart_ - dxFraction * windowLength();
            clampStart();
            const double height = yHi_ - yLo_;
            yLo_ = dragLo_ + dyFraction * height;
            yHi_ = yLo_ + height;
            clampBrightness();
        }

        // Wheel zoom about the pointer, one step per call. Time: `fraction` of
        // the way across the plot. Brightness: `fromBottom` of the way up it.
        void zoomTime(bool out, double fraction)
        {
            const double at = valueAt(fraction), W = windowLength();
            const double next = (std::min)((std::max)(W * (out ? ZoomOut : ZoomIn), limits_.minWindow), total_);
            window_ = next >= total_ ? 0.0 : next;
            start_ = at - (at - start_) * next / W;
            clampStart();
        }
        void zoomBrightness(bool in, double fromBottom)
        {
            const double height = yHi_ - yLo_;
            const double at = yLo_ + fromBottom * height;
            const double next = (std::min)((std::max)(height * (in ? ZoomIn : ZoomOut), limits_.minBrightnessSpan),
                PqAxis::Pq(limits_.hiNits) - PqAxis::Pq(limits_.loNits));
            yLo_ = at - (at - yLo_) * next / height;
            yHi_ = yLo_ + next;
            clampBrightness();
        }

    private:
        // One wheel step narrows the window to this share of itself, or widens it.
        static constexpr double ZoomIn = 0.8, ZoomOut = 1.25;

        void clampBrightness()
        {
            const double full = PqAxis::Pq(limits_.hiNits) - PqAxis::Pq(limits_.loNits);
            const double height = (std::min)((std::max)(yHi_ - yLo_, limits_.minBrightnessSpan), full);
            yLo_ = (std::min)((std::max)(yLo_, PqAxis::Pq(limits_.loNits)), PqAxis::Pq(limits_.hiNits) - height);
            yHi_ = yLo_ + height;
        }
        void clampStart() { start_ = (std::min)((std::max)(start_, 0.0), (std::max)(total_ - windowLength(), 0.0)); }

        Limits limits_;
        double total_ = 1.0, window_ = 0.0, start_ = 0.0;
        double dragStart_ = 0.0, dragLo_ = 0.0;
        double yLo_, yHi_;
    };
}
