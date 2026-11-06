#pragma once

// The pattern generator's sharpness and aspect charts draw their PQ white at
// VP's target nits (ledger A199, operator: "VP sends it"). VP's main window
// tells PATGEN, as "target_nits <cd/m2>", the sdr_target_nits of the active
// Rendering profile, on the PATGEN SDR gamma timer (PatgenSdrGamma.h), and
// again only when it changes. The value is resolved in the renderer's own
// order: the active rule's section over the base display section, else the
// renderer's built-in 203 (PL_COLOR_SDR_WHITE); a value the renderer would
// refuse is skipped the same way. PATGEN caps the white it draws at 203.

#include "ConfigFile.h"
#include "HdrTargetLuminance.h"
#include "RendererConfigView.h"

#include <cstdio>
#include <string>

namespace PatgenTargetNits
{
	// The renderer's built-in sdr_target_nits (LibplaceboVideoRenderer.cpp
	// sdrTargetNits = PL_COLOR_SDR_WHITE).
	constexpr double BuiltIn = 203.0;

	// The target nits for the active Rendering profile's section (empty: no
	// active rule), as the renderer resolves it.
	inline double Resolve(const ConfigFile& config, const std::string& renderingSection)
	{
		double nits = BuiltIn;
		std::string raw;
		double parsed = 0.0;
		if (RendererConfigView(config).TryGetDisplayString("sdr_target_nits", raw) &&
			HdrTargetLuminance::ParseWhite(ConfigFile::Trim(raw), parsed))
			nits = parsed;
		if (!renderingSection.empty() && config.TryGetString(renderingSection, "sdr_target_nits", raw) &&
			HdrTargetLuminance::ParseWhite(ConfigFile::Trim(raw), parsed))
			nits = parsed;
		return nits;
	}

	// The command word: the value as %g.
	inline std::string Word(double nits)
	{
		char text[32];
		std::snprintf(text, sizeof text, "%g", nits);
		return text;
	}
}
