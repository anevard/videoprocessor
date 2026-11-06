#pragma once

// The pattern generator's SDR gamma follows VP. VP's main window tells
// PATGEN the gamma VP DECODES its SDR picture with, as one command,
// "gamma <word>": 2.0 to 2.6 is a pure power PATGEN draws its SDR charts
// with; srgb, bt1886, 1.8, 2.8 or unknown PATGEN refuses with its refusal
// screen, so a chart is never drawn with a guessed gamma. The word is sent
// again when it changes, and once more in each new run: a run begins when
// the generator's input is capturing and ends when its capture device goes
// (CaptureRemove), so every capture Restart starts a new one.
//
// [pattern] keys, read at each start of the pattern generator's input:
//   sdr_gamma_route = renderer | profile   (default renderer)
//     renderer: the decode gamma the renderer itself reports in its output
//       mode line, "SDR Gamma <mode>: <action> (<declared>-><decoded>; ...)".
//     profile: worked out again from the active Colour profile's keys with
//       the renderer's own policy functions. It cannot see what the
//       renderer settles on at run time: a LUT that fails to load is taken
//       as loaded, and passthrough with output_gamma auto and no LUT is
//       unknown. Rendering-profile copies of the keys are not read.
//   sdr_gamma_check_ms = 100 to 5000       (default 1000)
//     how often VP looks; a change reaches PATGEN within one interval.

#include "ConfigFile.h"
#include "RendererProfileConfig.h"
#include "vprenderer/LibplaceboOutputPolicy.h"

#include <windows.h>

#include <cstdlib>
#include <string>

namespace PatgenSdrGamma
{
	constexpr UINT_PTR TimerId = 0x7a41;
	constexpr const char* Section = "pattern";
	constexpr const char* ColorRoot = "vprenderer.color";
	constexpr unsigned DefaultCheckMs = 1000;
	constexpr unsigned MinimumCheckMs = 100;
	constexpr unsigned MaximumCheckMs = 5000;

	enum class Route { Renderer, Profile };

	inline const char* RouteName(Route route)
	{
		return route == Route::Profile ? "profile" : "renderer";
	}

	// The command word for one of the renderer's SDR transfers.
	inline std::string Word(LibplaceboOutput::SdrTransfer transfer)
	{
		using T = LibplaceboOutput::SdrTransfer;
		switch (transfer)
		{
		case T::GAMMA18: return "1.8";
		case T::GAMMA20: return "2.0";
		case T::GAMMA22: return "2.2";
		case T::GAMMA24: return "2.4";
		case T::GAMMA26: return "2.6";
		case T::GAMMA28: return "2.8";
		case T::SRGB: return "srgb";
		case T::BT1886: return "bt1886";
		default: return "unknown";
		}
	}

	// Route renderer: the transfer after the arrow, named as the renderer's
	// own ToString names it. False: the line holds no SDR gamma decision
	// yet (nothing rendered since the renderer was set up).
	inline bool FromRendererLine(const std::string& line, std::string& word)
	{
		const size_t segment = line.find("| SDR Gamma ");
		if (segment == std::string::npos)
			return false;
		word = "unknown";
		const size_t arrow = line.find("->", segment);
		if (arrow == std::string::npos)
			return true;
		const size_t end = line.find(';', arrow);
		if (end == std::string::npos)
			return true;
		const std::string name = line.substr(arrow + 2, end - arrow - 2);
		using T = LibplaceboOutput::SdrTransfer;
		for (T transfer : { T::BT1886, T::SRGB, T::GAMMA18, T::GAMMA20, T::GAMMA22,
				T::GAMMA24, T::GAMMA26, T::GAMMA28 })
			if (name == LibplaceboOutput::ToString(transfer))
			{
				word = Word(transfer);
				break;
			}
		return true;
	}

	// One Colour key: the active profile's section, then the root Colour
	// section, then the renderer's own default.
	inline std::string ColorValue(const ConfigFile& config, const std::string& section,
		const char* key, const char* fallback)
	{
		std::string value;
		if (!section.empty() && config.TryGetString(section, key, value) && !value.empty())
			return value;
		if (config.TryGetString(ColorRoot, key, value) && !value.empty())
			return value;
		return fallback;
	}

	inline bool ColorLutEnabled(const ConfigFile& config, const std::string& section)
	{
		bool enabled = false;
		if (!section.empty() && config.TryGetBool(section, "calibration_lut_enabled", enabled))
			return enabled;
		if (config.TryGetBool(ColorRoot, "calibration_lut_enabled", enabled))
			return enabled;
		return false;
	}

	// Route profile. colorSection: the active Colour profile's section
	// (ActiveProfileStatus::SectionFor), empty for none.
	inline std::string FromProfile(const ConfigFile& config, const std::string& colorSection)
	{
		using namespace LibplaceboOutput;
		const auto read = [&](const char* key, const char* fallback)
		{
			return RendererProfileConfig::NormalizeCalibrationChoice(key,
				ColorValue(config, colorSection, key, fallback));
		};
		// The renderer's defaults (LoadRendererSettings).
		const SdrAdjustGamma requested = ParseSdrAdjustGamma(read("sdr_adjust_gamma", "passthrough"));
		const GammaRequest input = ParseGamma(read("sdr_input_transfer", "2.4"));
		const GammaRequest display = ParseGamma(read("output_gamma", "auto"));
		const GammaRequest lutInput = ParseGamma(read("sdr_lut_input_gamma", "passthrough"));
		const GammaRequest hdrTarget = ParseGamma(read("hdr_tone_map_target_gamma", ""));
		const bool lut = ColorLutEnabled(config, colorSection);

		const SdrTransfer declared = ResolveCalibrationTargetTransfer(input, SdrTransfer::UNKNOWN);
		if (declared == SdrTransfer::UNKNOWN)
			return "unknown";
		// Codes preserved into the display's own transfer, which only the
		// renderer knows while output_gamma is auto.
		if (!lut && requested == SdrAdjustGamma::PRESERVE_CODES && display == GammaRequest::AUTO)
			return "unknown";
		const CalibrationTransferDecision decision = ResolveCalibrationTransfers(true, true, lut,
			requested, display, hdrTarget, declared, SdrTransfer::UNKNOWN, lutInput);
		return Word(decision.sdr.effectiveSource);
	}

	inline Route ReadRoute(const ConfigFile& config, bool& valid)
	{
		std::string value;
		valid = true;
		if (!config.TryGetString(Section, "sdr_gamma_route", value) || value.empty())
			return Route::Renderer;
		const std::string name = ConfigFile::NormalizeName(value);
		if (name == "profile")
			return Route::Profile;
		valid = name == "renderer";
		return Route::Renderer;
	}

	inline unsigned ReadCheckMs(const ConfigFile& config, bool& valid)
	{
		std::string value;
		valid = true;
		if (!config.TryGetString(Section, "sdr_gamma_check_ms", value) || value.empty())
			return DefaultCheckMs;
		char* end = nullptr;
		const unsigned long ms = std::strtoul(value.c_str(), &end, 10);
		valid = end != value.c_str() && *end == '\0' && ms >= MinimumCheckMs && ms <= MaximumCheckMs;
		return valid ? static_cast<unsigned>(ms) : DefaultCheckMs;
	}
}
