#pragma once

// The Config Patterns page's Chart signal (ledger A183 (3), operator: one
// live selector, five values, separate from the ColourSpace patches' Gamut).
// Every chart declares exactly the chosen signal, and the page lists only the
// charts drawn for it. The lists mirror the pattern generator's own
// (vp-patgen src/charts.cpp ChartDrawnFor), as the operator chose them at
// patgen-16: the grey-only and colour clipping charts, the SDR nits chart and
// greyscale charts under the three SDR signals; each colour ramp under its
// own signal; the PQ nits chart, PQ grey ramp and DTM response under both PQ
// signals; the sharpness and aspect charts under all five (A199).

#include <cstring>
#include <string>

namespace PatternChartSignal
{
    struct Signal
    {
        const char* value;   // the [pattern] chart_signal value and command word
        const char* label;   // as the page shows it
        bool pq;
    };

    inline const Signal* Signals(size_t& count)
    {
        static const Signal table[] = {
            { "bt709_sdr", "BT.709 SDR", false },
            { "bt2020_sdr", "BT.2020 SDR", false },
            { "p3_bt2020_sdr", "P3 in BT.2020 SDR", false },
            { "bt2020_pq", "BT.2020 PQ", true },
            { "p3_bt2020_pq", "P3 in BT.2020 PQ", true },
        };
        count = sizeof(table) / sizeof(table[0]);
        return table;
    }

    // The default, as the generator's: BT.709 SDR.
    constexpr const char* DefaultValue = "bt709_sdr";

    inline const Signal* Find(const std::string& value)
    {
        size_t count = 0;
        const Signal* table = Signals(count);
        for (size_t index = 0; index < count; ++index)
            if (value == table[index].value) return &table[index];
        return nullptr;
    }

    // True when the chart (its id) is drawn for the signal (its value). An
    // unknown chart or signal is never drawn.
    inline bool Drawn(const std::string& chart, const std::string& signal)
    {
        const Signal* found = Find(signal);
        if (!found || chart.empty()) return false;
        // Each colour ramp under its own signal only.
        if (chart == "ramp_709") return signal == "bt709_sdr";
        if (chart == "ramp_2020_sdr") return signal == "bt2020_sdr";
        if (chart == "ramp_p3_sdr") return signal == "p3_bt2020_sdr";
        if (chart == "ramp_2020_pq") return signal == "bt2020_pq";
        if (chart == "ramp_p3_pq") return signal == "p3_bt2020_pq";
        // The PQ charts under both PQ signals.
        if (chart == "nits_pq" || chart == "grey_ramp_pq" || chart == "dtm_response") return found->pq;
        // Sharpness and aspect under every signal (ledger A199): under PQ
        // their white is VP's target nits, capped at 203 cd/m2. One focus
        // chart, its width and colour its own settings (A219 (12)).
        // The saturation chart under every signal too (A246).
        static const char* const every[] = {
            "sat_sweep", "resolution", "focus",
            "aspect_1.33", "aspect_1.78", "aspect_1.85", "aspect_1.896", "aspect_2.00", "aspect_2.39",
        };
        for (const char* id : every)
            if (chart == id) return true;
        // Every other chart under the three SDR signals ("Range, SDR" does
        // the colour clipping chart's job, and gamma_check is gone: A219 (3),
        // (8)).
        static const char* const sdr[] = {
            "clip_black", "clip_black_grey", "clip_white", "clip_white_grey", "nits_sdr",
            "grey_steps", "grey_ramp", "gradient_10bit",
        };
        for (const char* id : sdr)
            if (chart == id) return !found->pq;
        return false;
    }

    // The chart that follows a showing chart to a new signal (ledger A198,
    // operator), as the generator does it itself (vp-patgen src/charts.cpp
    // ChartForSignal): a colour ramp becomes that signal's ramp, and the twins
    // swap (nits_pq <-> nits_sdr, grey_ramp_pq <-> grey_ramp), each keeping
    // its own range settings. Any other chart is returned unchanged.
    inline std::string Follow(const std::string& chart, const std::string& signal)
    {
        const Signal* found = Find(signal);
        if (!found) return chart;
        if (chart == "ramp_709" || chart == "ramp_2020_sdr" || chart == "ramp_p3_sdr" ||
            chart == "ramp_2020_pq" || chart == "ramp_p3_pq")
        {
            if (signal == "bt709_sdr") return "ramp_709";
            if (signal == "bt2020_sdr") return "ramp_2020_sdr";
            if (signal == "p3_bt2020_sdr") return "ramp_p3_sdr";
            if (signal == "bt2020_pq") return "ramp_2020_pq";
            return "ramp_p3_pq";
        }
        if (chart == "nits_pq" || chart == "nits_sdr") return found->pq ? "nits_pq" : "nits_sdr";
        if (chart == "grey_ramp_pq" || chart == "grey_ramp") return found->pq ? "grey_ramp_pq" : "grey_ramp";
        return chart;
    }

    // The ColourSpace patches' Gamut ([pattern] gamut) is PQ (A230 (1)):
    // "BT.2020, PQ" or "BT.2020 PQ, P3 mastering".
    inline bool PatchGamutIsPq(const std::string& gamut)
    {
        return gamut == "bt2020_pq" || gamut == "p3_bt2020_pq";
    }

    // The HDR10 block card, one for the charts and the patches, shows while
    // the Chart signal or the patches' Gamut is PQ (A230 (1)).
    inline bool Hdr10CardShown(const std::string& chartSignal, const std::string& gamut)
    {
        const Signal* found = Find(chartSignal);
        return (found && found->pq) || PatchGamutIsPq(gamut);
    }
}
