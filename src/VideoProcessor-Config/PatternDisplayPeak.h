#pragma once

// The Config Patterns page's display peak, for its "Up to display peak" and
// "Above display peak" nits buttons. It is VP's own HDR tone-map target
// white: the SAVED sdr_target_nits of the Rendering profile VP reports
// active, else of [vprenderer], else the built-in default. That is the order
// the renderer itself uses: its base value from [vprenderer], the active
// rule's value over it. Never guessed: with no active Rendering profile from
// VP's live status the result is empty and the page greys the two buttons
// (ledger Decisions 2026-11-04 patgen-14 and patgen-15).

#include <ProfileGroups.h>

#include <cctype>
#include <map>
#include <string>
#include <vector>

namespace PatternDisplayPeak
{
    // Section -> key -> value, as Config keeps the configuration at its last save.
    using Snapshot = std::map<std::string, std::map<std::string, std::string>>;

    inline bool SameName(const std::string& left, const std::string& right)
    {
        if (left.size() != right.size()) return false;
        for (size_t index = 0; index < left.size(); ++index)
            if (std::tolower(static_cast<unsigned char>(left[index])) !=
                std::tolower(static_cast<unsigned char>(right[index])))
                return false;
        return true;
    }

    inline std::string Trimmed(const std::string& text)
    {
        const size_t first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return {};
        return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
    }

    // The Rendering profile among VP's active sections; empty for none. VP
    // publishes it as [vprenderer] (Base) or [vprenderer.<name>]; every other
    // group under vprenderer is its own section in VP's group table
    // (ProfileGroups.h: vprenderer.motion, .output, ...) or one level deeper.
    inline std::string ActiveRenderingSection(const std::vector<std::string>& active)
    {
        const std::string root = "vprenderer";
        for (const std::string& section : active)
        {
            if (SameName(section, root)) return section;
            if (section.size() <= root.size() + 1 || !SameName(section.substr(0, root.size() + 1), root + "."))
                continue;
            if (section.find('.', root.size() + 1) != std::string::npos) continue;
            bool otherGroup = false;
            for (const auto& group : ProfileGroups::Definitions())
                otherGroup = otherGroup || SameName(section, group.section);
            if (!otherGroup) return section;
        }
        return {};
    }

    // One key's saved value in one section, trimmed; empty when either is absent.
    inline std::string SavedValue(const Snapshot& saved, const std::string& section,
        const std::string& key)
    {
        for (const auto& [name, settings] : saved)
        {
            if (!SameName(name, section)) continue;
            for (const auto& [setting, value] : settings)
                if (SameName(setting, key)) return Trimmed(value);
            return {};
        }
        return {};
    }

    // The display peak in cd/m2 as typed in the configuration; empty while VP
    // names no active Rendering profile.
    inline std::string TargetNits(const Snapshot& saved, const std::string& activeRendering,
        const std::string& builtIn)
    {
        if (activeRendering.empty()) return {};
        std::string nits = SavedValue(saved, activeRendering, "sdr_target_nits");
        if (nits.empty()) nits = SavedValue(saved, "vprenderer", "sdr_target_nits");
        return nits.empty() ? builtIn : nits;
    }
}
