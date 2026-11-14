#pragma once

#include <vprenderer/AlphaPresentationTelemetry.h>

#include <cstddef>
#include <cstdint>

enum class AlphaCadenceAction
{
	Drop = -1,
	None = 0,
	Repeat = 1
};

enum class AlphaCadenceTimingStatus
{
	Disabled,
	WaitingForDxgi,
	Measuring,
	Matched,
	Forecasting,
	Verifying,
	Incompatible
};

enum class AlphaCadenceBlockReason
{
	None,
	Disabled,
	PresentationEvidenceUnavailable,
	InvalidRates,
	RateMismatchTooLarge,
	StabilizingRates,
	NoActionableMismatch,
	BeforeDeadline,
	Cooldown,
	VerificationPending,
	DropQueueNotAboveDesired,
	RepeatQueueNotBelowDesired,
	DropPresentationDebtMissing,
	RepeatPresentationDebtPresent,
	WaitingForFreshScene,
	FallbackNotMature,
	DropFallbackQueueTooYoung
};

// What brought the phase to the action point in this evaluation, if anything
// did besides the forecast.
enum class AlphaCadenceSnapSource
{
	None,
	Depth
};

struct AlphaCadenceCorrectionInput
{
	bool enabled = false;
	uint64_t generation = 0;
	uint64_t detectorGeneration = 0;
	uint64_t presentationGeneration = 0;
	AlphaPresentationEvidence presentationEvidence =
		AlphaPresentationEvidence::Unavailable;
	double captureRateHz = 0.0;
	double displayRateHz = 0.0;
	size_t queueDepth = 0;
	size_t desiredQueueDepth = 1;
	double oldestQueuedAgeMs = 0.0;
	uint64_t presentationDebt = 0;
	uint32_t lastPresentId = 0;
	// One DXGI_FRAME_STATISTICS sample: PresentRefreshCount - PresentCount
	// rises by one for each refresh that showed the previous frame again.
	uint32_t dxgiPresentCount = 0;
	uint32_t dxgiPresentRefreshCount = 0;
	bool dxgiSampleValid = false; // a successful, non-disjoint sample exists
	bool safeSceneBoundary = false;
	uint64_t sceneEventId = 0;
	uint64_t sourceSequence = 0;
};

struct AlphaCadenceDiagnosticSnapshot
{
	uint64_t policyGeneration = 0;
	uint64_t detectorGeneration = 0;
	uint64_t presentationGeneration = 0;
	uint64_t sourceSequence = 0;
	AlphaPresentationEvidence presentationEvidence =
		AlphaPresentationEvidence::Unavailable;
	double captureRateHz = 0.0;
	double displayRateHz = 0.0;
	double rawMismatchPpm = 0.0;
	double filteredMismatchPpm = 0.0;
	double phaseFrames = 0.0;
	size_t queueDepth = 0;
	size_t desiredQueueDepth = 1;
	double oldestQueuedAgeMs = 0.0;
	uint64_t presentationDebt = 0;
	uint32_t lastPresentId = 0;
	bool safeSceneBoundary = false;
	uint64_t sceneEventId = 0;
	bool sceneEventFresh = false;
	bool sceneAuthorized = false;
	bool fallbackMature = false;
	bool fallbackEligible = false;
	uint32_t plannedFrames = 0;
	uint32_t fallbackFrames = 0;
	uint32_t cooldownFrames = 0;
	AlphaCadenceSnapSource snapSource = AlphaCadenceSnapSource::None;
	// The ledger of whole frames, this generation: refreshes that showed the
	// previous frame again, frames the backlog recovery flushed, drops a
	// repeated refresh showed to be premature, re-based DXGI samples, and how
	// far the wrap has moved the reference.
	uint64_t repeatedRefreshes = 0;
	uint64_t framesFlushed = 0;
	uint64_t prematureDrops = 0;
	uint64_t dxgiRebases = 0;
	int64_t referenceShiftFrames = 0;
	// A source slower than the display: the phase above the lowest point the
	// chain has reached, where the display last ran out of frames by itself.
	// -1 when there is no such floor (a faster or matched source).
	double aboveFloorFrames = -1.0;
};

struct AlphaCadenceCorrectionDecision
{
	AlphaCadenceAction action = AlphaCadenceAction::None;
	AlphaCadenceAction predictedAction = AlphaCadenceAction::None;
	AlphaCadenceTimingStatus timingStatus =
		AlphaCadenceTimingStatus::WaitingForDxgi;
	bool ratesCompatible = false;
	bool predictionValid = false;
	bool planned = false;
	bool deadlineFallback = false;
	bool verificationPending = false;
	bool verificationCompleted = false;
	bool lastVerificationValid = false;
	bool lastVerificationSucceeded = false;
	bool due = false;
	double phaseFrames = 0.0;
	double secondsUntilCorrection = 0.0;
	double secondsUntilPlan = 0.0;
	uint32_t rateFilterSamples = 0;
	double filteredMismatchPpm = 0.0;
	uint64_t sceneEventId = 0;
	AlphaCadenceBlockReason blockReason =
		AlphaCadenceBlockReason::None;
	AlphaCadenceAction verificationAction =
		AlphaCadenceAction::None;
	uint64_t actionId = 0;
	AlphaCadenceDiagnosticSnapshot diagnostic;
};

class AlphaCadenceCorrectionPolicy
{
public:
	AlphaCadenceCorrectionDecision Evaluate(
		const AlphaCadenceCorrectionInput& input);
	void Reset(uint64_t generation = 0);
	// The backlog recovery flushed framesFlushed queued frames: take them off
	// the phase and wait out the stabilize gate before acting again; the drift
	// of that wait is added when it ends. The rates, the direction, the
	// cooldown and the ledger are kept.
	void OnBacklogRecovery(uint32_t framesFlushed);
	void CancelPendingAction();
	// The output was rebuilt: the display rate may differ, so the rates and the
	// direction are measured again. The phase and the ledger are kept.
	void OnOutputChanged();

private:
	static constexpr double MAXIMUM_RATE_MISMATCH_PPM = 200.0;
	static constexpr double PLAN_PHASE_FRAMES = 0.75;
	static constexpr double ACTION_PHASE_FRAMES = 1.0;
	static constexpr double MAXIMUM_QUEUE_AGE_MS = 250.0;
	static constexpr uint32_t MINIMUM_STABLE_SAMPLES = 120;
	static constexpr uint32_t MINIMUM_PREDICTION_SAMPLES = 600;
	static constexpr uint32_t COOLDOWN_FRAMES = 120;
	static constexpr double RATE_FILTER_ALPHA = 1.0 / 600.0;
	static constexpr double MINIMUM_PREDICTION_PPM = 1.0;
	static constexpr double DIRECTION_REVERSAL_PPM = 2.0;
	static constexpr uint32_t SURPLUS_CONFIRM_FRAMES = 120;
	// How long a due drop waits for a scene cut before it is taken anyway.
	static constexpr double DROP_DUE_LIMIT_MS = 180000.0;
	// How many evaluations after a drop a repeated refresh shows that drop to
	// have been premature: presentation depths up to 8 plus DXGI's reporting
	// lag.
	static constexpr uint32_t PREMATURE_DROP_WINDOW = 10;
	// How far above a slower source's floor the display may run out of frames
	// by itself: the forecast's rate error, at most this, gathered over one
	// run-out cycle (a whole frame of drift)...
	static constexpr double SLOWER_FLOOR_RATE_ERROR_PPM = 0.25;
	// ...and the arrival and render timing, which moves the run-out by up to
	// this much drift.
	static constexpr double SLOWER_FLOOR_TIMING_SECONDS = 10.0;

	void CountRepeatedRefreshes(
		const AlphaCadenceCorrectionInput& input, double limitRateHz);
	// A repeated refresh this close above the floor is the display running
	// out of frames by itself; a slower source drops only while the chain
	// stands a whole frame and this much above its floor.
	double SlowerFloorMarginFrames(double displayRateHz) const;

	uint64_t m_generation = 0;
	double m_phaseFrames = 0.0;
	uint32_t m_stableSamples = 0;
	uint32_t m_rateFilterSamples = 0;
	uint32_t m_plannedFrames = 0;
	uint32_t m_cooldownFrames = 0;
	uint64_t m_lastSceneEventId = 0;
	bool m_verificationPending = false;
	AlphaCadenceAction m_verificationAction = AlphaCadenceAction::None;
	uint64_t m_verificationDebt = 0;
	uint32_t m_verificationPresentId = 0;
	bool m_lastVerificationValid = false;
	bool m_lastVerificationSucceeded = false;
	double m_filteredPhasePerFrame = 0.0;
	AlphaCadenceAction m_predictionDirection = AlphaCadenceAction::None;
	uint32_t m_surplusFrames = 0;
	uint32_t m_dueFrames = 0;
	// The DXGI pair the next sample is counted from.
	bool m_dxgiBaseValid = false;
	uint32_t m_dxgiBasePresentCount = 0;
	uint32_t m_dxgiBasePresentRefreshCount = 0;
	uint64_t m_repeatedRefreshes = 0;
	uint64_t m_framesFlushed = 0;
	uint64_t m_prematureDrops = 0;
	uint64_t m_dxgiRebases = 0;
	int64_t m_referenceShiftFrames = 0;
	bool m_wrapPending = false;
	// Evaluations left in which a repeated refresh cancels the last drop.
	uint32_t m_prematureCheckFrames = 0;
	// From a backlog recovery to its wrap: the telemetry measures the display
	// again, so the rates and the direction are kept and the stall's repeated
	// refreshes are counted against the last display rate measured.
	bool m_recoveryHold = false;
	double m_lastDisplayRateHz = 0.0;
	// Evaluations from a backlog recovery to its wrap that did not advance
	// the phase: the chain drifted meanwhile, so the wrap adds their drift.
	uint32_t m_recoveryEvaluations = 0;
	// A slower source: the lowest phase reached outside a recovery, which is
	// where the display last ran out of frames by itself once it has done so.
	bool m_slowerFloorKnown = false;
	double m_slowerFloorPhaseFrames = 0.0;
};
