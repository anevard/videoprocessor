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
// Set aside, never selected: the runtime's manual and automatic selections
// and its persisted state are not touched, so nothing is pinned and the
// restore is exact. Only the copy of the snapshot handed to the renderer
// changes: the Zoom group names a profile no configuration can have (the
// renderer skips an unknown profile), and its configuration is a copy whose
// active Screen section carries the crop keys below (ConfigFile::
// SetValueInMemory; the file is never written). Not saved.
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

	// Selections (group -> profile names, as RendererProfileConfig::Selections)
	// with the Zoom group set aside; the Screen stays (A200).
	template <typename Selections>
	void SetAsideZoom(Selections& selections)
	{
		selections["zoom"] = { SetAsideName };
	}

	// The native-raster charts, shown 1:1 automatically (ledger A201,
	// operator: "Yes, 1:1 automatically"): their 1-pixel lines and the
	// gamma_check line field mean nothing once resampled, and gamma_check's
	// black bands must not be detected as bars. While one is up, the Screen
	// and the Zoom are both set aside, so no resize, no crop (the renderer's
	// crop keys default off, and the rig keeps them in Zoom profiles only)
	// and no zoom; no switch.
	inline bool IsOneToOneChart(const std::string& id)
	{
		return id == "gamma_check" || id == "resolution" || id == "focus_white" || id == "focus_green";
	}
	template <typename Selections>
	void SetAside(Selections& selections)
	{
		selections["viewport"] = { SetAsideName };
		selections["zoom"] = { SetAsideName };
	}

	// The keys written into the active Screen section of the renderer's copy
	// (A200): automatic crop and both fill crops off, the fixed crop at the
	// chart's ratio. The renderer reads crop keys from every profile section
	// it applies; with Zoom set aside, the Screen's are the ones that count.
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
	template <typename Config>
	void CropExactly(Config& copy, const std::string& screenSection, const std::string& ratio)
	{
		for (const CropKey& crop : CropKeys())
			copy.SetValueInMemory(screenSection, crop.key, crop.value ? crop.value : ratio);
	}

	// The section holding a Screen profile's keys, as the renderer resolves
	// it (LibplaceboVideoRenderer.cpp `ApplyAutomaticProfiles`): under the
	// target model `vprenderer.viewport.<name>`, or the root itself for an
	// internal Base profile; otherwise `profiles.viewport.<name>`. The caller
	// says which model (RendererProfileConfig::IsTargetModel) and passes
	// whether the root section exists.
	inline std::string ScreenSection(const std::string& normalizedProfile, bool targetModel, bool rootExists)
	{
		if (!targetModel)
			return "profiles.viewport." + normalizedProfile;
		const std::string root = "vprenderer.viewport";
		return normalizedProfile == "base" && rootExists ? root : root + "." + normalizedProfile;
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
	// ("off" for chart off). False for any other command.
	inline bool ParseChart(const std::string& command, std::string& id)
	{
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
