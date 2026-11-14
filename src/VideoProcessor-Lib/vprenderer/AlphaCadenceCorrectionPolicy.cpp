#include <pch.h>

#include "AlphaCadenceCorrectionPolicy.h"

#include <algorithm>
#include <cmath>

void AlphaCadenceCorrectionPolicy::Reset(uint64_t generation)
{
	m_generation = generation;
	m_phaseFrames = 0.0;
	m_stableSamples = 0;
	m_rateFilterSamples = 0;
	m_plannedFrames = 0;
	m_cooldownFrames = 0;
	m_lastSceneEventId = 0;
	m_verificationPending = false;
	m_verificationAction = AlphaCadenceAction::None;
	m_verificationDebt = 0;
	m_verificationPresentId = 0;
	m_lastVerificationValid = false;
	m_lastVerificationSucceeded = false;
	m_filteredPhasePerFrame = 0.0;
	m_predictionDirection = AlphaCadenceAction::None;
	m_surplusFrames = 0;
	m_dueFrames = 0;
	m_dxgiBaseValid = false;
	m_dxgiBasePresentCount = 0;
	m_dxgiBasePresentRefreshCount = 0;
	m_repeatedRefreshes = 0;
	m_framesFlushed = 0;
	m_prematureDrops = 0;
	m_dxgiRebases = 0;
	m_referenceShiftFrames = 0;
	m_wrapPending = false;
	m_prematureCheckFrames = 0;
	m_recoveryHold = false;
	m_lastDisplayRateHz = 0.0;
	m_slowerFloorKnown = false;
	m_slowerFloorPhaseFrames = 0.0;
	m_recoveryEvaluations = 0;
}

void AlphaCadenceCorrectionPolicy::OnBacklogRecovery(uint32_t framesFlushed)
{
	m_phaseFrames -= static_cast<double>(framesFlushed);
	m_framesFlushed += framesFlushed;
	m_verificationPending = false;
	m_verificationAction = AlphaCadenceAction::None;
	m_verificationDebt = 0;
	m_verificationPresentId = 0;
	m_surplusFrames = 0;
	m_plannedFrames = 0;
	m_dueFrames = 0;
	// Pass the stabilize gate again before acting: by then DXGI has reported
	// the stall's repeated refreshes, so the flush and the refreshes land
	// together before the wrap.
	m_stableSamples = 0;
	m_wrapPending = true;
	// The telemetry is reset with the backlog and reports no display rate
	// until it measures one again; the stall's repeated refreshes arrive in
	// that time and are counted against the last rate measured.
	m_recoveryHold = true;
	// The stall's repeated refreshes cannot be told from a premature drop's:
	// they are all counted.
	m_prematureCheckFrames = 0;
}

void AlphaCadenceCorrectionPolicy::OnOutputChanged()
{
	m_recoveryHold = false;
	m_lastDisplayRateHz = 0.0;
	m_stableSamples = 0;
	m_rateFilterSamples = 0;
	m_filteredPhasePerFrame = 0.0;
	m_predictionDirection = AlphaCadenceAction::None;
	// The floor is a level of the old output's chain.
	m_slowerFloorKnown = false;
}

// The forecast drains a slower source's phase by its filtered rate. A rate off
// by e ppm puts the next run-out e / |ppm| frames from where the forecast has
// it, and arrival and render timing move it by a few seconds of drift; either
// way it is the display running out of frames by itself, not an event. An
// event's frame that close above the floor drains off by itself as soon.
double AlphaCadenceCorrectionPolicy::SlowerFloorMarginFrames(
	double displayRateHz) const
{
	const double drainPerFrame = std::abs(m_filteredPhasePerFrame);
	const double drainPpm =
		std::max(drainPerFrame * 1000000.0, MINIMUM_PREDICTION_PPM);
	return SLOWER_FLOOR_RATE_ERROR_PPM / drainPpm +
		drainPerFrame * displayRateHz * SLOWER_FLOOR_TIMING_SECONDS;
}

// Each refresh that showed the previous frame again is one more frame in the
// chain. Runs on every evaluation, before any early return.
void AlphaCadenceCorrectionPolicy::CountRepeatedRefreshes(
	const AlphaCadenceCorrectionInput& input, double limitRateHz)
{
	const bool checkingDrop = m_prematureCheckFrames > 0;
	if (checkingDrop)
		--m_prematureCheckFrames;
	if (!input.dxgiSampleValid)
		return;
	if (!m_dxgiBaseValid)
	{
		m_dxgiBaseValid = true;
		m_dxgiBasePresentCount = input.dxgiPresentCount;
		m_dxgiBasePresentRefreshCount = input.dxgiPresentRefreshCount;
		return;
	}

	// Unsigned 32-bit differences, safe across a counter wrap.
	const uint32_t presents =
		input.dxgiPresentCount - m_dxgiBasePresentCount;
	const uint32_t refreshes =
		input.dxgiPresentRefreshCount - m_dxgiBasePresentRefreshCount;
	m_dxgiBasePresentCount = input.dxgiPresentCount;
	m_dxgiBasePresentRefreshCount = input.dxgiPresentRefreshCount;
	if (presents > 0x80000000u || refreshes > 0x80000000u ||
		refreshes < presents ||
		static_cast<double>(refreshes - presents) >
			std::ceil(4.0 * limitRateHz))
	{
		++m_dxgiRebases;
		return;
	}

	uint32_t repeated = refreshes - presents;
	if (repeated == 0)
		return;
	m_repeatedRefreshes += repeated;
	if (checkingDrop)
	{
		// A drop with no whole surplus frame behind it makes the display
		// repeat one refresh: the two cancel.
		--repeated;
		++m_prematureDrops;
		m_prematureCheckFrames = 0;
	}
	// A slower source at its floor: the display ran out of frames by itself.
	// That is where the floor is, so it is anchored here, whatever error the
	// forecast has gathered since the last time.
	if (repeated > 0 && m_slowerFloorKnown && !m_recoveryHold &&
		m_predictionDirection == AlphaCadenceAction::Repeat &&
		m_phaseFrames - m_slowerFloorPhaseFrames <=
			SlowerFloorMarginFrames(limitRateHz))
	{
		m_slowerFloorPhaseFrames = m_phaseFrames;
	}
	m_phaseFrames += static_cast<double>(repeated);
}

void AlphaCadenceCorrectionPolicy::CancelPendingAction()
{
	if (!m_verificationPending)
		return;
	// A drop that did not happen leaves no repeated refresh to cancel.
	if (m_verificationAction == AlphaCadenceAction::Drop)
		m_prematureCheckFrames = 0;
	m_phaseFrames +=
		m_verificationAction == AlphaCadenceAction::Drop ? 1.0 : -1.0;
	m_verificationPending = false;
	m_verificationAction = AlphaCadenceAction::None;
	m_verificationDebt = 0;
	m_verificationPresentId = 0;
}

AlphaCadenceCorrectionDecision AlphaCadenceCorrectionPolicy::Evaluate(
	const AlphaCadenceCorrectionInput& input)
{
	if (input.generation != m_generation)
		Reset(input.generation);
	const bool displayRateInRange =
		input.displayRateHz >= 10.0 && input.displayRateHz <= 500.0;
	if (displayRateInRange)
		m_lastDisplayRateHz = input.displayRateHz;
	// With no display rate known, a repeated refresh re-bases the ledger, as at
	// start-up; inside a recovery the last rate measured stands in.
	const double limitRateHz = displayRateInRange
		? input.displayRateHz
		: (m_recoveryHold ? m_lastDisplayRateHz : 0.0);
	CountRepeatedRefreshes(input, limitRateHz);

	AlphaCadenceCorrectionDecision decision;
	decision.diagnostic.policyGeneration = input.generation;
	decision.diagnostic.detectorGeneration = input.detectorGeneration;
	decision.diagnostic.presentationGeneration =
		input.presentationGeneration;
	decision.diagnostic.sourceSequence = input.sourceSequence;
	decision.diagnostic.presentationEvidence = input.presentationEvidence;
	decision.diagnostic.captureRateHz = input.captureRateHz;
	decision.diagnostic.displayRateHz = input.displayRateHz;
	decision.diagnostic.queueDepth = input.queueDepth;
	decision.diagnostic.desiredQueueDepth =
		std::max<size_t>(1, input.desiredQueueDepth);
	decision.diagnostic.oldestQueuedAgeMs = input.oldestQueuedAgeMs;
	decision.diagnostic.presentationDebt = input.presentationDebt;
	decision.diagnostic.lastPresentId = input.lastPresentId;
	decision.diagnostic.safeSceneBoundary = input.safeSceneBoundary;
	decision.diagnostic.sceneEventId = input.sceneEventId;
	decision.diagnostic.repeatedRefreshes = m_repeatedRefreshes;
	decision.diagnostic.framesFlushed = m_framesFlushed;
	decision.diagnostic.prematureDrops = m_prematureDrops;
	decision.diagnostic.dxgiRebases = m_dxgiRebases;
	decision.diagnostic.referenceShiftFrames = m_referenceShiftFrames;
	const bool ratesInRange =
		input.captureRateHz >= 10.0 && input.captureRateHz <= 240.0 &&
		displayRateInRange;
	if (!input.enabled ||
		input.presentationEvidence != AlphaPresentationEvidence::Stable ||
		!ratesInRange)
	{
		m_stableSamples = 0;
		if (m_wrapPending)
			++m_recoveryEvaluations;
		if (!m_recoveryHold)
		{
			m_rateFilterSamples = 0;
			m_filteredPhasePerFrame = 0.0;
			m_predictionDirection = AlphaCadenceAction::None;
		}
		m_plannedFrames = 0;
		m_verificationPending = false;
		m_lastVerificationValid = false;
		decision.phaseFrames = m_phaseFrames;
		decision.timingStatus = !input.enabled
			? AlphaCadenceTimingStatus::Disabled
			: (input.presentationEvidence != AlphaPresentationEvidence::Stable
				? AlphaCadenceTimingStatus::WaitingForDxgi
				: AlphaCadenceTimingStatus::Measuring);
		decision.blockReason = !input.enabled
			? AlphaCadenceBlockReason::Disabled
			: (input.presentationEvidence != AlphaPresentationEvidence::Stable
				? AlphaCadenceBlockReason::PresentationEvidenceUnavailable
				: AlphaCadenceBlockReason::InvalidRates);
		return decision;
	}

	const double rawPhasePerFrame =
		input.captureRateHz / input.displayRateHz - 1.0;
	decision.diagnostic.rawMismatchPpm =
		rawPhasePerFrame * 1000000.0;
	if (m_rateFilterSamples < MINIMUM_PREDICTION_SAMPLES)
	{
		++m_rateFilterSamples;
		m_filteredPhasePerFrame +=
			(rawPhasePerFrame - m_filteredPhasePerFrame) /
			static_cast<double>(m_rateFilterSamples);
	}
	else
	{
		m_filteredPhasePerFrame +=
			(rawPhasePerFrame - m_filteredPhasePerFrame) * RATE_FILTER_ALPHA;
	}
	const double filteredMismatchPpm =
		std::abs(m_filteredPhasePerFrame) * 1000000.0;
	decision.rateFilterSamples = m_rateFilterSamples;
	decision.filteredMismatchPpm = filteredMismatchPpm;
	decision.ratesCompatible =
		filteredMismatchPpm <= MAXIMUM_RATE_MISMATCH_PPM;
	if (!decision.ratesCompatible)
	{
		m_stableSamples = 0;
		if (m_wrapPending)
			++m_recoveryEvaluations;
		m_plannedFrames = 0;
		m_verificationPending = false;
		m_lastVerificationValid = false;
		decision.phaseFrames = m_phaseFrames;
		decision.timingStatus = AlphaCadenceTimingStatus::Incompatible;
		decision.blockReason =
			AlphaCadenceBlockReason::RateMismatchTooLarge;
		return decision;
	}

	if (m_verificationPending &&
		input.lastPresentId != 0 &&
		input.lastPresentId != m_verificationPresentId)
	{
		decision.verificationAction = m_verificationAction;
		m_lastVerificationValid = true;
		m_lastVerificationSucceeded =
			m_verificationAction == AlphaCadenceAction::Drop
				? (m_verificationDebt > 0 &&
					input.presentationDebt + 1 == m_verificationDebt)
				: input.presentationDebt == m_verificationDebt + 1;
		m_verificationPending = false;
		m_cooldownFrames = COOLDOWN_FRAMES;
		decision.verificationCompleted = true;
	}
	decision.lastVerificationValid = m_lastVerificationValid;
	decision.lastVerificationSucceeded = m_lastVerificationSucceeded;
	decision.verificationPending = m_verificationPending;

	if (m_stableSamples < MINIMUM_STABLE_SAMPLES)
	{
		++m_stableSamples;
		if (m_wrapPending)
			++m_recoveryEvaluations;
		decision.phaseFrames = m_phaseFrames;
		decision.timingStatus = AlphaCadenceTimingStatus::Measuring;
		decision.blockReason =
			AlphaCadenceBlockReason::StabilizingRates;
		return decision;
	}

	if (m_wrapPending)
	{
		// A flush that took more than the chain gained leaves the phase below
		// the reference. For a source that is not slower, move the reference
		// down by whole frames and keep the sub-frame drift, so the chain
		// holds the lower delay and no repeat is requested against the
		// source. A slower source keeps its phase: frames a stall left above
		// its floor are dropped, and a chain below its floor runs dry and is
		// counted back up.
		m_wrapPending = false;
		m_recoveryHold = false;
		// The chain drifted through the recovery while the phase was held:
		// add that drift, at the rate the hold kept.
		m_phaseFrames += static_cast<double>(m_recoveryEvaluations) *
			m_filteredPhasePerFrame;
		m_recoveryEvaluations = 0;
		// An output change inside the recovery measured the rates again: the
		// direction then comes from the filter the stabilize gate has rebuilt.
		const double wrapFilteredPpm = m_filteredPhasePerFrame * 1000000.0;
		const bool wrapRepeat =
			m_predictionDirection == AlphaCadenceAction::Repeat ||
			(m_predictionDirection == AlphaCadenceAction::None &&
				wrapFilteredPpm <= -MINIMUM_PREDICTION_PPM);
		if (!wrapRepeat && m_phaseFrames < 0.0)
		{
			const double frames = std::ceil(-m_phaseFrames);
			m_phaseFrames += frames;
			m_referenceShiftFrames += static_cast<int64_t>(frames);
		}
		decision.diagnostic.referenceShiftFrames = m_referenceShiftFrames;
	}

	m_phaseFrames += m_filteredPhasePerFrame;
	decision.diagnostic.phaseFrames = m_phaseFrames;
	const double signedFilteredPpm = m_filteredPhasePerFrame * 1000000.0;
	decision.diagnostic.filteredMismatchPpm =
		signedFilteredPpm;
	if (m_predictionDirection == AlphaCadenceAction::None)
	{
		if (signedFilteredPpm >= MINIMUM_PREDICTION_PPM)
			m_predictionDirection = AlphaCadenceAction::Drop;
		else if (signedFilteredPpm <= -MINIMUM_PREDICTION_PPM)
			m_predictionDirection = AlphaCadenceAction::Repeat;
	}
	else if (m_predictionDirection == AlphaCadenceAction::Drop &&
		signedFilteredPpm <= -DIRECTION_REVERSAL_PPM)
	{
		m_predictionDirection = AlphaCadenceAction::Repeat;
	}
	else if (m_predictionDirection == AlphaCadenceAction::Repeat &&
		signedFilteredPpm >= DIRECTION_REVERSAL_PPM)
	{
		m_predictionDirection = AlphaCadenceAction::Drop;
	}

	// A slower source drains its chain until the display runs out of frames
	// and shows one twice by itself: the lowest phase reached is its floor.
	// It follows the drift down; a recovery's flush is never taken for it,
	// as the hold has ended by now and the chain has counted back up.
	if (m_predictionDirection == AlphaCadenceAction::Repeat)
	{
		if (!m_slowerFloorKnown || m_phaseFrames < m_slowerFloorPhaseFrames)
		{
			m_slowerFloorPhaseFrames = m_phaseFrames;
			m_slowerFloorKnown = true;
		}
	}
	else
		m_slowerFloorKnown = false;
	const bool slowerFloor = m_slowerFloorKnown;
	if (slowerFloor)
	{
		decision.diagnostic.aboveFloorFrames =
			m_phaseFrames - m_slowerFloorPhaseFrames;
	}

	const size_t desired = std::max<size_t>(1, input.desiredQueueDepth);
	if (m_rateFilterSamples >= MINIMUM_PREDICTION_SAMPLES &&
		m_predictionDirection != AlphaCadenceAction::Repeat &&
		input.queueDepth >= desired)
	{
		if (m_surplusFrames < SURPLUS_CONFIRM_FRAMES)
			++m_surplusFrames;
	}
	else
		m_surplusFrames = 0;
	if (m_surplusFrames >= SURPLUS_CONFIRM_FRAMES && !m_verificationPending &&
		m_phaseFrames < ACTION_PHASE_FRAMES)
	{
		// The queue has held a surplus source for SURPLUS_CONFIRM_FRAMES
		// evaluations: the forecast is behind the queue (a reset while a
		// surplus was queued, or the sub-frame phase at start). Bring the
		// phase to the action point; the scene and time-limit gates decide.
		m_phaseFrames = ACTION_PHASE_FRAMES;
		m_plannedFrames = std::max(m_plannedFrames, m_surplusFrames);
		decision.diagnostic.snapSource = AlphaCadenceSnapSource::Depth;
	}

	const bool predictionDirectionAgrees =
		(m_predictionDirection == AlphaCadenceAction::Drop &&
			signedFilteredPpm >= MINIMUM_PREDICTION_PPM) ||
		(m_predictionDirection == AlphaCadenceAction::Repeat &&
			signedFilteredPpm <= -MINIMUM_PREDICTION_PPM);
	if (m_rateFilterSamples >= MINIMUM_PREDICTION_SAMPLES &&
		predictionDirectionAgrees && !m_verificationPending)
	{
		decision.predictedAction = m_predictionDirection;
		const double phasePerFrame = m_filteredPhasePerFrame;
		const double correctionPhase =
			m_predictionDirection == AlphaCadenceAction::Drop
				? ACTION_PHASE_FRAMES : -ACTION_PHASE_FRAMES;
		const double correctionFrames =
			(correctionPhase - m_phaseFrames) / phasePerFrame;
		decision.secondsUntilCorrection =
			correctionFrames / input.captureRateHz;
		const double planPhase =
			m_predictionDirection == AlphaCadenceAction::Drop
				? PLAN_PHASE_FRAMES : -PLAN_PHASE_FRAMES;
		const double planFrames =
			(planPhase - m_phaseFrames) / phasePerFrame;
		decision.secondsUntilPlan = planFrames / input.captureRateHz;
		decision.predictionValid =
			std::isfinite(decision.secondsUntilCorrection) &&
			std::isfinite(decision.secondsUntilPlan);
	}
	decision.timingStatus = m_verificationPending
		? AlphaCadenceTimingStatus::Verifying
		: (m_rateFilterSamples < MINIMUM_PREDICTION_SAMPLES
			? AlphaCadenceTimingStatus::Measuring
			: (decision.predictionValid
				? AlphaCadenceTimingStatus::Forecasting
				: AlphaCadenceTimingStatus::Matched));

	// The phase a drop is measured on. A slower source drops only frames an
	// event left a whole frame above its floor, never ones the drift brought;
	// any other source drops what the forecast and the ledger bring due.
	const double dropPhaseFrames = slowerFloor
		? m_phaseFrames - m_slowerFloorPhaseFrames -
			SlowerFloorMarginFrames(input.displayRateHz)
		: m_phaseFrames;
	const AlphaCadenceAction requested =
		dropPhaseFrames >= ACTION_PHASE_FRAMES
			? AlphaCadenceAction::Drop
			: (m_phaseFrames <= -ACTION_PHASE_FRAMES
				? AlphaCadenceAction::Repeat
				: AlphaCadenceAction::None);
	if (requested == AlphaCadenceAction::Drop)
		++m_dueFrames;
	else
		m_dueFrames = 0;
	if (slowerFloor && requested == AlphaCadenceAction::Drop)
	{
		// Frames an event left above a slower source's floor: the next
		// action is a drop, due now.
		decision.predictedAction = AlphaCadenceAction::Drop;
		decision.secondsUntilCorrection = 0.0;
		decision.secondsUntilPlan = 0.0;
	}
	decision.due = requested != AlphaCadenceAction::None;
	decision.planned = dropPhaseFrames >= PLAN_PHASE_FRAMES ||
		m_phaseFrames <= -PLAN_PHASE_FRAMES;
	decision.diagnostic.cooldownFrames = m_cooldownFrames;

	if (m_cooldownFrames > 0)
	{
		--m_cooldownFrames;
		decision.phaseFrames = m_phaseFrames;
		decision.blockReason = AlphaCadenceBlockReason::Cooldown;
		return decision;
	}
	if (m_verificationPending)
	{
		decision.phaseFrames = m_phaseFrames;
		decision.blockReason =
			AlphaCadenceBlockReason::VerificationPending;
		return decision;
	}

	if (decision.planned)
		++m_plannedFrames;
	else
		m_plannedFrames = 0;

	if (requested == AlphaCadenceAction::None)
	{
		decision.phaseFrames = m_phaseFrames;
		decision.blockReason = decision.predictionValid
			? AlphaCadenceBlockReason::BeforeDeadline
			: AlphaCadenceBlockReason::NoActionableMismatch;
		return decision;
	}

	const uint32_t fallbackFrames = static_cast<uint32_t>(std::max(
		1.0, std::ceil(MAXIMUM_QUEUE_AGE_MS * input.displayRateHz / 1000.0)));
	decision.diagnostic.fallbackFrames = fallbackFrames;
	decision.diagnostic.plannedFrames = m_plannedFrames;
	decision.diagnostic.sceneEventFresh =
		input.sceneEventId != 0 && input.sceneEventId != m_lastSceneEventId;
	const bool sceneAuthorized =
		input.safeSceneBoundary && input.sceneEventId != 0 &&
		input.sceneEventId != m_lastSceneEventId;
	decision.diagnostic.sceneAuthorized = sceneAuthorized;
	decision.diagnostic.fallbackMature =
		m_plannedFrames >= fallbackFrames;
	// A due drop waits for a fresh scene for at most DROP_DUE_LIMIT_MS,
	// converted to display frames as fallbackFrames is.
	const uint32_t dueLimitFrames = static_cast<uint32_t>(std::max(
		1.0, std::ceil(DROP_DUE_LIMIT_MS * input.displayRateHz / 1000.0)));
	const bool deadlineAuthorized =
		m_plannedFrames >= fallbackFrames &&
		(requested == AlphaCadenceAction::Repeat ||
			m_dueFrames >= dueLimitFrames);
	decision.diagnostic.fallbackEligible = deadlineAuthorized;

	// No queue-depth gate on a drop: at 1:1 the post-dequeue depth is the last
	// place a surplus source shows, after the driver's presentation queue, the
	// loop block and the head's age. A due phase is a whole surplus source
	// somewhere in the chain, and the frames in flight cover the skipped one.
	if (requested == AlphaCadenceAction::Repeat &&
		input.queueDepth >= desired)
	{
		decision.phaseFrames = m_phaseFrames;
		decision.blockReason =
			AlphaCadenceBlockReason::RepeatQueueNotBelowDesired;
		return decision;
	}
	if (requested == AlphaCadenceAction::Drop &&
		input.presentationDebt == 0)
	{
		decision.phaseFrames = m_phaseFrames;
		decision.blockReason =
			AlphaCadenceBlockReason::DropPresentationDebtMissing;
		return decision;
	}
	if (requested == AlphaCadenceAction::Repeat &&
		input.presentationDebt != 0)
	{
		decision.phaseFrames = m_phaseFrames;
		decision.blockReason =
			AlphaCadenceBlockReason::RepeatPresentationDebtPresent;
		return decision;
	}

	if (!sceneAuthorized && !deadlineAuthorized)
	{
		decision.phaseFrames = m_phaseFrames;
		if (m_plannedFrames < fallbackFrames)
			decision.blockReason =
				AlphaCadenceBlockReason::FallbackNotMature;
		else
			decision.blockReason =
				AlphaCadenceBlockReason::WaitingForFreshScene;
		return decision;
	}

	decision.action = requested;
	decision.deadlineFallback = !sceneAuthorized;
	decision.sceneEventId = sceneAuthorized ? input.sceneEventId : 0;
	m_lastSceneEventId = input.sceneEventId;
	m_phaseFrames += requested == AlphaCadenceAction::Drop ? -1.0 : 1.0;
	m_plannedFrames = 0;
	m_dueFrames = 0;
	if (requested == AlphaCadenceAction::Drop)
		m_prematureCheckFrames = PREMATURE_DROP_WINDOW;
	m_verificationPending = true;
	m_verificationAction = requested;
	m_verificationDebt = input.presentationDebt;
	m_verificationPresentId = input.lastPresentId;
	decision.verificationPending = true;
	decision.phaseFrames = m_phaseFrames;
	return decision;
}
