#pragma once

// The Config Patterns page's [pattern] keys, in two kinds (ledger A183 (1),
// operator: "Save them silently").
//
// LIVE: a chart setting. The page sends it to the pattern generator at once
// and saves it to the configuration file the moment it changes, without
// Apply; it never lights Apply and never offers the generator's restart.
//
// START-UP: every other [pattern] key (cs_host, cs_port, mode, gamut,
// log_messages, chart, and VP's own sdr_gamma_route and sdr_gamma_check_ms).
// The generator reads it only when its input starts, so it is applied with
// Apply, which offers that restart.

#include <cctype>
#include <string>

namespace PatternLiveKeys
{
    inline bool SameName(const std::string& left, const char* right)
    {
        size_t index = 0;
        for (; index < left.size() && right[index]; ++index)
            if (std::tolower(static_cast<unsigned char>(left[index])) !=
                std::tolower(static_cast<unsigned char>(right[index])))
                return false;
        return index == left.size() && !right[index];
    }

    inline bool IsPatternSection(const std::string& section)
    {
        return SameName(section, "pattern");
    }

    // True for a live chart setting of [pattern].
    inline bool IsLive(const std::string& key)
    {
        static const char* const live[] = {
            "flash", "text",
            "hdr10", "hdr10_peak", "hdr10_min", "hdr10_maxcll", "hdr10_maxfall",
            "nits_from", "nits_to", "sdr_from", "sdr_to",
            "saturation", "nits_gamut", "ramp_peak", "dtm_hold", "dtm_size",
        };
        for (const char* name : live)
            if (SameName(key, name)) return true;
        return false;
    }

    // True when a changed section and key needs the generator's restart: a
    // [pattern] key that is not live.
    inline bool NeedsRestart(const std::string& section, const std::string& key)
    {
        return IsPatternSection(section) && !IsLive(key);
    }
}
