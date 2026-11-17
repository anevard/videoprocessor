#pragma once

// A source plugin's start timing (Config's Patterns page "Timing"; ledger
// A356 (B)(b); operator, 2026-11-17: a pick list, written for any rig, built
// from the user's own Output display profiles, "Same as the display" first).
//
// VP tells a plugin at each start the raster and rate to draw at, as
// idle_format: today the fullscreen display's desktop mode, so VP's scaler
// stays idle (SourcePluginBackend::DesktopModeFormat). A "timing" key in the
// plugin's own section names one of VP's allowed display modes instead; VP
// sends that mode, and its own mode choice (RankDisplayOutputModesForInput)
// then takes the display there, restoring the desktop mode when the renderer
// retires, as for any input. The pick is honoured only when an Output display
// profile on the advanced policy lists it, the modes VP itself would switch
// to; anything else starts at the display's mode and says why. "timing" is
// VP's key, never sent to the plugin.

#include <ConfigFile.h>
#include <DisplayRefreshRatePolicy.h>

#include <cstdio>
#include <string>
#include <vector>

namespace SourcePluginStartTiming
{
	constexpr const char* Key = "timing";
	// The value Config writes for "Same as the display" (an absent key means
	// the same).
	constexpr const char* Display = "display";

	// The allowed display modes of every Output display profile on the
	// advanced policy, in file order, each once: the modes VP switches to.
	inline std::vector<DisplayOutputMode> AllowedModes(const ConfigFile& config)
	{
		std::vector<DisplayOutputMode> modes;
		for (const std::string& section : config.GetSectionNames())
		{
			const std::string name = ConfigFile::NormalizeName(section);
			if (name != "output_display" && name.rfind("output_display.", 0) != 0)
				continue;
			std::string policy, list, error;
			if (!config.TryGetString(section, "mode_policy", policy) ||
				ConfigFile::NormalizeName(policy) != "advanced" ||
				!config.TryGetString(section, "allowed_modes", list))
				continue;
			std::vector<DisplayOutputMode> parsed;
			if (!ParseDisplayOutputModeList(list, parsed, error))
				continue;
			for (const DisplayOutputMode& mode : parsed)
			{
				bool listed = false;
				for (const DisplayOutputMode& known : modes)
					listed = listed || DisplayOutputModesExactlyEqual(known, mode);
				if (!listed)
					modes.push_back(mode);
			}
		}
		return modes;
	}

	// The idle_format a plugin starts at. desktop is DesktopModeFormat's
	// ("<w>x<h>@<num>/<den> sdr|pq", or empty). An empty pick or Display:
	// desktop, note empty. A pick that is a listed mode: that mode, with the
	// desktop's sdr or pq (sdr when the desktop is unknown). Anything else:
	// desktop, and note says why the pick was not used.
	inline std::string Choose(const std::string& pick, const std::vector<DisplayOutputMode>& allowed,
		const std::string& desktop, std::string& note)
	{
		note.clear();
		if (pick.empty() || pick == Display)
			return desktop;
		DisplayOutputMode mode{};
		std::string error;
		if (!ParseDisplayOutputMode(pick, mode, error))
		{
			note = "timing '" + pick + "' is not a display mode (" + error + ")";
			return desktop;
		}
		bool listed = false;
		for (const DisplayOutputMode& known : allowed)
			listed = listed || DisplayOutputModesExactlyEqual(known, mode);
		if (!listed)
		{
			note = "timing " + FormatDisplayOutputMode(mode) +
				" is not in any Output display profile's Allowed display modes";
			return desktop;
		}
		const size_t space = desktop.rfind(' ');
		const std::string transfer = space == std::string::npos ? "sdr" : desktop.substr(space + 1);
		char format[64];
		sprintf_s(format, "%ux%u@%u/%u %s", mode.width, mode.height,
			mode.refreshRate.numerator, mode.refreshRate.denominator, transfer.c_str());
		return format;
	}
}
