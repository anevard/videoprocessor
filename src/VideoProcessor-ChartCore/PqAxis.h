#pragma once
#include <ToneCurveProvider.h>
#include <QString>
#include <algorithm>
#include <cmath>
#include <vector>

// A brightness axis in PQ, through libplacebo's own PQ: where a brightness
// sits on it, the brightness at a position, and its gridlines and labels.
namespace PqAxis
{
    // The axis position of a brightness: libplacebo's PQ, as the Config
    // charts place theirs. Not a number when libplacebo is not loaded.
    inline double Pq(double nits)
    {
        const auto toPq = ToneCurveProvider::Registered().nitsToPq;
        return toPq ? toPq((std::max)(nits, 0.0)) : std::nan("");
    }

    // Nits from an axis position, the inverse of Pq().
    inline double PqNits(double pq)
    {
        const auto toNits = ToneCurveProvider::Registered().pqToNits;
        return toNits ? toNits((std::max)(pq, 0.0)) : std::nan("");
    }

    // Gridlines for the brightness shown: decades when it spans many,
    // then 1-3, 1-2-5, and every digit when zoomed in close.
    // A view reaching 0 nit gets a 0 line and counts decades from 0.0001,
    // the old floor, so the spacing above it is as before.
    inline std::vector<double> Ticks(double lo, double hi)
    {
        const double decades = std::log10(hi / (std::max)(lo, 0.0001));
        static const std::vector<double> d1{ 1 }, d13{ 1, 3 }, d125{ 1, 2, 5 }, d19{ 1, 2, 3, 4, 5, 6, 7, 8, 9 };
        const std::vector<double>& m = decades > 4 ? d1 : decades > 2 ? d13 : decades > 0.7 ? d125 : d19;
        std::vector<double> ticks;
        if (lo < 1e-7) ticks.push_back(0.0);   // the floor, read back through PQ
        for (int e = -5; e <= 4; ++e)
            for (double k : m)
            {
                const double n = k * std::pow(10.0, e);
                if (n >= lo * 0.999 && n <= hi * 1.001) ticks.push_back(n);
            }
        if (decades > 4 && hi >= 3999 && (ticks.empty() || ticks.back() < 4000)) ticks.push_back(4000);
        return ticks;
    }
    inline QString TickLabel(double n)
    {
        if (n <= 0) return QStringLiteral("0");
        if (n >= 1) return QString::number(n, 'g', 6);
        return QString::number(n, 'f', static_cast<int>(std::ceil(-std::log10(n) - 1e-9)));
    }
}
