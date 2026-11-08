#pragma once

// The Config Patterns page's "Crop exactly to the chart" switch (ledger A200,
// replacing A183 (6)'s "No crop or zoom"; operator: "so i like the exact
// crop. What if we try the switch for that and the other way it flows
// through typical path"). While it is on and the pattern generator shows an
// aspect chart, VP's main window keeps the active Screen (viewport) profile,
// sets the Zoom profile aside, pauses automatic crop and crops the source to
// exactly the chart's ratio with VP's own fixed crop, so each ratio shows as
// a perfectly cropped film of that ratio would through the operator's own
// Screen, and nothing moves. The generator draws each frame on the fixed
// crop's grid (vp-patgen `AspectFrame`) and says each chart's ratio
// ("aspect_ratio <id>"), so VP's crop lands on the frame lines. Switch off:
// the typical path, unchanged.
//
// Never selected in the runtime: its manual and automatic selections and its
// persisted state are not touched, so nothing is pinned and the restore is
// exact. Only the copy of the snapshot handed to the renderer changes: its
// configuration is a copy holding PATGEN's own Zoom profile with the crop
// keys below, and its Zoom group selects that profile (A229, operator: "it
// should be using code that will work on any config with or without a zoom
// crop profile"). The operator's Screen profile and every other group apply
// exactly as configured; no profile of theirs is written, borrowed or needed
// (ConfigFile::SetValueInMemory; the file is never written). Not saved.
//
// Config sends it as RunAction "pattern.exact_crop on" / "pattern.exact_crop
// off"; VP answers it itself and does not pass it to the generator.

#include <cstdint>
#include <string>
#include <vector>

namespace PatgenNoCrop
{
	// A profile name no configuration can hold: profile names are stored
	// lower-case (ConfigFile::NormalizeName), and this one is not.
	constexpr const char* SetAsideName = "PATGEN set aside";
	constexpr const char* Command = "exact_crop";
	// The generator's answer to an aspect chart's ratio (A200).
	constexpr const char* RatioCommand = "aspect_ratio";
	constexpr const char* AspectPrefix = "aspect_";

	// The native-raster charts, shown 1:1 automatically (ledger A201,
	// operator: "Yes, 1:1 automatically"): their 1-pixel lines mean nothing
	// once resampled. While one is up, the Screen and the Zoom are both set
	// aside, so no resize, no crop (the renderer's crop keys default off, and
	// the rig keeps them in Zoom profiles only) and no zoom; no switch. One
	// focus chart since A219 (12); gamma_check is gone (A219 (8)).
	inline bool IsOneToOneChart(const std::string& id)
	{
		return id == "resolution" || id == "focus";
	}
	template <typename Selections>
	void SetAside(Selections& selections)
	{
		selections["viewport"] = { SetAsideName };
		selections["zoom"] = { SetAsideName };
	}

	// The keys of PATGEN's own Zoom profile (A200, A229): automatic crop and
	// both fill crops off, the fixed crop at the chart's ratio. The renderer
	// reads crop keys from every profile section it applies, Zoom after
	// Screen, and takes fixed_crop_aspect from a Zoom profile only
	// (RendererProfileConfig.h); written into a Screen section it voids the
	// whole profile model (A223 (1)(a), build 16).
	struct CropKey
	{
		const char* key;
		const char* value;    // nullptr: the chart's ratio
	};
	inline const std::vector<CropKey>& CropKeys()
	{
		static const std::vector<CropKey> keys = {
			{ "automatic_crop", "false" },
			{ "crop_narrower_content_to_fill_screen", "false" },
			{ "crop_wider_content_to_fill_screen", "false" },
			{ "fixed_crop_aspect", nullptr } };
		return keys;
	}

	// PATGEN's own Zoom profile: a named variant of the Zoom group, under the
	// target model `vprenderer.zoom.<name>`, otherwise `profiles.zoom.<name>`
	// (as LibplaceboVideoRenderer.cpp `ApplyAutomaticProfiles` resolves a
	// section; the caller says which model, RendererProfileConfig::
	// IsTargetModel). Never the root: the root is the operator's, and a root
	// beside their own `.base` variant is refused by his validator.
	constexpr const char* ZoomProfileName = "patgen_exact_crop";
	inline std::string ZoomRoot(bool targetModel)
	{
		return targetModel ? "vprenderer.zoom" : "profiles.zoom";
	}

	// Adds PATGEN's Zoom profile to the renderer's copy and selects it in the
	// Zoom group (selections: group -> profile names, as RendererProfileConfig
	// ::Selections); returns its section. The name is one no section of the
	// copy holds. The section goes FIRST in the copy's order: a Zoom group
	// with no root section takes its first named variant as the baseline the
	// renderer applies before the selected one (the rig's is `Crop`, with
	// automatic crop on), so the baseline is PATGEN's too and no key of the
	// operator's Zoom profiles reaches the chart. Every other group, the
	// Screen's included, stays as selected.
	template <typename Config, typename Selections>
	std::string CropExactly(Config& copy, bool targetModel, Selections& selections, const std::string& ratio)
	{
		const std::string root = ZoomRoot(targetModel);
		std::string name = ZoomProfileName;
		for (int suffix = 2; copy.HasSection(root + "." + name); ++suffix)
			name = std::string(ZoomProfileName) + "_" + std::to_string(suffix);
		const std::string section = root + "." + name;
		for (const CropKey& crop : CropKeys())
			copy.SetValueInMemory(section, crop.key, crop.value ? crop.value : ratio, true);
		selections["zoom"] = { name };
		return section;
	}

	// "exact_crop on" or "exact_crop off" (the command after "pattern.");
	// false for anything else.
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

	// For "chart <id>" (the command after "pattern."): true, with the id
	// ("off" for chart off). "chart_clear" (Config's Patterns page built
	// anew, A219 (2)) is "off": whatever was up loses its crop. False for any
	// other command.
	inline bool ParseChart(const std::string& command, std::string& id)
	{
		if (command == "chart_clear")
		{
			id = "off";
			return true;
		}
		const std::string prefix = "chart ";
		if (command.rfind(prefix, 0) != 0) return false;
		id = command.substr(prefix.size());
		return true;
	}

	inline bool IsAspectChart(const std::string& id)
	{
		return id.rfind(AspectPrefix, 0) == 0;
	}

	// The generator command asking an aspect chart's ratio.
	inline std::string RatioQuery(const std::string& id)
	{
		return std::string(RatioCommand) + " " + id;
	}

	// The generator's answer, numerator << 16 | denominator, as the
	// "<numerator>:<denominator>" VP's fixed crop reads; false for a failure
	// (negative) or a pair that is not a ratio of 1 to 4, VP's limits.
	inline bool RatioText(int32_t answer, std::string& ratio)
	{
		if (answer <= 0) return false;
		const int numerator = answer >> 16, denominator = answer & 0xFFFF;
		if (numerator <= 0 || denominator <= 0 || numerator < denominator ||
			numerator > 4 * denominator)
			return false;
		ratio = std::to_string(numerator) + ":" + std::to_string(denominator);
		return true;
	}
}
