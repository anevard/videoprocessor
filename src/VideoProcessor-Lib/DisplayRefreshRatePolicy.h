#pragma once

#include <cstdint>
#include <vector>

enum class DisplayRefreshRateDecision
{
	Accepted,
	Warming,
	Quarantined,
	Unavailable
};

enum class DisplayRefreshRateReason
{
	Accepted,
	NoSamples,
	InsufficientSamples,
	Stabilizing,
	NonFiniteCandidate,
	StaleCandidate,
	InvalidRawCadence,
	InvalidIntervalRange,
	RawCadenceExceedsCandidate,
	HarmonicMismatch,
	NominalMismatch,
	UnexplainedCompensation
};

struct DisplayRefreshRateInput
{
	double candidateRateHz = 0.0;
	double rawWaitRateHz = 0.0;
	double nominalRateHz = 0.0;
	double minimumWaitIntervalMs = 0.0;
	double maximumWaitIntervalMs = 0.0;
	uint64_t compensatedIntervals = 0;
	uint64_t rawWaitIntervals = 0;
	// Short current-rate evidence used to begin the one deterministic
	// reset/prefill sequence. It deliberately precedes long phase confidence.
	double startupObservationSeconds = 0.0;
	// Duration of the current, post-transition observation window. This is
	// distinct from the longer phase-correction stability requirement.
	double readinessObservationSeconds = 0.0;
	bool fresh = false;
	bool stable = false;
};

struct DisplayRefreshRateResult
{
	DisplayRefreshRateDecision decision =
		DisplayRefreshRateDecision::Unavailable;
	DisplayRefreshRateReason reason =
		DisplayRefreshRateReason::NoSamples;
	double selectedRateHz = 0.0;
	double startupRateHz = 0.0;
	bool startupValidated = false;
	// The candidate passed all freshness, raw-cadence, interval-range, harmonic,
	// and nominal-family checks. It may still be in the longer stabilization
	// period required for phase-sensitive correction. Output readiness can use
	// this bounded, already-validated observation without treating it as HDMI
	// lock proof.
	double readinessRateHz = 0.0;
	bool readinessValidated = false;
	bool shouldRecalculate = false;
};

struct DisplayRefreshRational
{
	uint32_t numerator = 0;
	uint32_t denominator = 0;
};

enum class DisplayRefreshModeSelectionPath
{
	None,
	ExactOrClose,
	ClosestInRange
};

struct DisplayRefreshModeSelection
{
	DisplayRefreshModeSelectionPath path =
		DisplayRefreshModeSelectionPath::None;
	DisplayRefreshRational selected{};
	double requestedRateHz = 0.0;
	double selectedRateHz = 0.0;
	double differenceHz = 0.0;
	bool doubledRate = false;
};

enum class RefreshRateSwitchMode
{
	Never,
	FullscreenOnly,
	Always
};

double DisplayRefreshRateHz(const DisplayRefreshRational& rate);

// Select a display mode in two intentional passes. The first pass accepts an
// exactly equivalent rational or tight driver rounding. Only if that fails is
// the closest cadence-family candidate accepted within the bounded fallback
// window; this must not turn a 24 Hz source into an unrelated 50/60 Hz mode.
DisplayRefreshModeSelection SelectDisplayRefreshMode(
	const DisplayRefreshRational& requested,
	const std::vector<DisplayRefreshRational>& candidates);

// Rank native-rate candidates before doubled-rate fallbacks for progressive
// 25/29.97/30 input (the existing >24.1 and <31 Hz family). Interlaced input
// keeps field-rate output only. Each group retains the normal rational/tolerance
// ranking; returning the complete list lets switch failures try the next mode.
std::vector<DisplayRefreshModeSelection> RankDisplayRefreshModesForInput(
	const DisplayRefreshRational& input, bool interlaced,
	const std::vector<DisplayRefreshRational>& candidates);

// The single raster both high-rate decisions below are written against. A link
// that cannot carry a high rate at the desktop raster is expected to carry it
// here. The selector and the ceiling must read the same pair: if they disagreed
// about what this raster is, a successful drop would leave the ceiling armed and
// the high rate would never be applied at all.
constexpr uint32_t HIGH_RATE_TARGET_WIDTH = 1920;
constexpr uint32_t HIGH_RATE_TARGET_HEIGHT = 1080;

struct DisplayResolutionSelection
{
	uint32_t width = 0;
	uint32_t height = 0;
	bool change = false;
};

// Choose the desktop raster for a rate that is already resolved: preferredRateHz
// is the rate the caller is about to request, with any interlaced field-rate
// doubling already folded in. A limitHz of zero is off and never changes
// anything.
//
// This deliberately takes no original raster and can therefore only ever
// propose the target raster or no change; it cannot express a climb back to a
// larger desktop while a high rate is live. Restoring the original mode belongs
// to the caller's own retirement path.
DisplayResolutionSelection SelectDisplayResolutionForRate(
	double preferredRateHz, double limitHz,
	uint32_t desktopWidth, uint32_t desktopHeight);

struct DisplayRateCeilingResult
{
	std::vector<DisplayRefreshModeSelection> retained;
	size_t dropped = 0;
	bool armed = false;
};

// Refuse rates above the limit while the desktop is at any raster other than
// the target. The ceiling is a function of where the desktop actually ended up,
// never of whether a mode set failed and never of what the display claims it can
// do; an EDID may advertise a mode the link cannot lock.
//
// An emptied result is reported, not resolved: what to do about it needs the
// display calls the caller owns. Retained entries keep their incoming rank
// order.
DisplayRateCeilingResult ApplyDisplayRateCeiling(
	const std::vector<DisplayRefreshModeSelection>& ranked, double limitHz,
	uint32_t desktopWidth, uint32_t desktopHeight);

bool DisplayRefreshRatesExactlyEqual(
	const DisplayRefreshRational& first,
	const DisplayRefreshRational& second);

// Windows and GPU drivers can report a refresh rate that differs from the
// requested rational by a few millihertz after a display-topology rebuild.
// This comparison is deliberately for restore acknowledgement only; mode
// selection continues to require exact rational equality.
bool DisplayRefreshRatesEquivalentForRestore(
	const DisplayRefreshRational& first,
	const DisplayRefreshRational& second);

// Refresh switching is display-global. Only a top-level presentation surface
// may own that transition; an embedded preview must not change desktop timing
// while a fullscreen renderer is being retired or replaced.
bool ShouldSwitchRefreshRateForPresentationTarget(bool isChildWindow,
	RefreshRateSwitchMode mode);

// Compatibility overload for existing callers/tests: the former checked
// setting maps to the new Fullscreen Only policy.
bool ShouldSwitchRefreshRateForPresentationTarget(bool isChildWindow);

class DisplayRefreshRestoreVerifier
{
public:
	explicit DisplayRefreshRestoreVerifier(
		DisplayRefreshRational expected,
		unsigned int requiredConsecutiveMatches = 2)
		: m_expected(expected),
		  m_requiredConsecutiveMatches(
			requiredConsecutiveMatches == 0 ? 1 : requiredConsecutiveMatches)
	{
	}

	bool Observe(bool querySucceeded, DisplayRefreshRational observed);
	unsigned int ConsecutiveMatches() const
	{
		return m_consecutiveMatches;
	}

private:
	DisplayRefreshRational m_expected;
	unsigned int m_requiredConsecutiveMatches = 2;
	unsigned int m_consecutiveMatches = 0;
};

DisplayRefreshRateResult EvaluateDisplayRefreshRate(
	const DisplayRefreshRateInput& input);
const char* ToString(DisplayRefreshRateDecision decision);
const char* ToString(DisplayRefreshRateReason reason);
