#pragma once

// The Config Patterns page's "No crop or zoom" switch (ledger A183 (6),
// operator: "A switch on the page", then "Aspect card, aspect only"). While
// it is on and the pattern generator shows an aspect chart, VP's main window
// sets the viewport (Screen) and zoom profiles aside, so the chart is seen on
// the whole output with no viewport, crop or zoom; when either ends, the
// selections VP's profile runtime holds are applied again, unchanged.
//
// Set aside, never selected: the runtime's manual and automatic selections
// and its persisted state are not touched, so nothing is pinned and the
// restore is exact. Only the copy of the snapshot handed to the renderer
// names, for these two groups, a profile no configuration can have; the
// renderer skips an unknown profile, so neither group applies any setting.
// Not saved to the configuration. VP's automatic crop itself is unchanged
// (ledger Decision 2026-10-29 coord-21).
//
// Config sends it as RunAction "pattern.nocrop on" / "pattern.nocrop off";
// VP answers it itself and does not pass it to the generator.

#include <string>
#include <vector>

namespace PatgenNoCrop
{
	// A profile name no configuration can hold: profile names are stored
	// lower-case (ConfigFile::NormalizeName), and this one is not.
	constexpr const char* SetAsideName = "PATGEN no crop or zoom";
	constexpr const char* Command = "nocrop";

	// The groups set aside: the viewport (Screen) and zoom profiles.
	inline const std::vector<std::string>& Groups()
	{
		static const std::vector<std::string> groups = { "viewport", "zoom" };
		return groups;
	}

	// Selections (group -> profile names, as RendererProfileConfig::Selections)
	// with the viewport and zoom groups set aside.
	template <typename Selections>
	void SetAside(Selections& selections)
	{
		for (const std::string& group : Groups())
			selections[group] = { SetAsideName };
	}

	// "nocrop on" or "nocrop off" (the command after "pattern."); false for
	// anything else.
	inline bool ParseCommand(const std::string& command, bool& on)
	{
		const std::string prefix = std::string(Command) + " ";
		if (command.rfind(prefix, 0) != 0) return false;
		const std::string value = command.substr(prefix.size());
		if (value == "on") on = true;
		else if (value == "off") on = false;
		else return false;
		return true;
	}

	// For "chart <id>" (the command after "pattern."): true, with whether it
	// is an aspect chart ("chart off" is not). False for any other command.
	inline bool ParseChart(const std::string& command, bool& aspect)
	{
		const std::string prefix = "chart ";
		if (command.rfind(prefix, 0) != 0) return false;
		aspect = command.compare(prefix.size(), 7, "aspect_") == 0;
		return true;
	}
}
