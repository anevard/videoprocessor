#include "pch.h"
#include "CppUnitTest.h"

#include <vprenderer/AlphaCadenceCorrectionPolicy.h>
#include <vprenderer/AlphaQueuePolicy.h>

#include "AlphaCadenceDriftSceneEvents.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <limits>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// A model of the matched-refresh render loop driving the real
// AlphaCadenceCorrectionPolicy and AlphaQueuePolicy functions for a whole
// film while the capture clock runs a few ppm off the display clock. The loop
// runs in the renderer's order: wait until the queue holds the target
// (CanDequeue); pop; evaluate the policy on the post-pop depth, the new
// head's age and the display's DXGI pair; a drop skips the frame and returns
// to the wait; otherwise render for a fixed time and present. The present
// stage holds the frame on screen and up to presentSlots presents waiting
// for a refresh; a present into a full stage blocks until a refresh takes
// one. After each rendered frame the backlog check runs (ShouldRecoverBacklog:
// flush to the target, then OnBacklogRecovery with the frames flushed, as the
// renderer does). A scenario may stall one render, or miss one vsync on the
// display side: the display keeps the previous frame and the waiting present
// stays queued.
//
// The DXGI pair is the PresentCount and PresentRefreshCount of the frame on
// screen. Each present carries the next present id; a refresh that takes a
// frame from the stage sets the pair to that id and the refresh's index. A
// missed vsync or an empty refresh leaves the pair as it was, as DXGI does,
// so the refresh count gains one on the present count for each refresh that
// showed the previous frame again. The pair is valid from the first scan-out.
// The presentation telemetry's warm-up is modelled: at the start and after
// each backstop the policy gets no display rate and no evidence until 8
// presents and 0.25 s have passed. A scenario may give the policy the
// capture rate with a fixed error (rateErrorPpm), as a measured rate may
// carry; the chain keeps the true rate.
//
// The ground truth is G: the time from a frame's nominal arrival (its arrival
// without jitter) to the refresh that shows it. It rises by one display period
// less one source period per refresh, and falls by one source period at a
// drop. A source slower than the display drains the chain until the display
// runs out of frames and shows one twice by itself, where G is at its floor;
// for it the corrector may drop only while G stands a whole source period
// above the lowest G so far, never a frame the drift brought.
//
// Not modelled: presentation debt (held at 2, as logged at 1:1), the real
// scene detector (cuts come from a logged film or a fixed interval), GPU
// timing variance, present_every_vsync, motion blending, lost capture frames,
// an output change.

namespace Tests
{
	namespace
	{
		enum class CutPattern
		{
			Film20261109,
			Film20261112,
			Every24Seconds,
			Every120Seconds,
			None
		};

		// Defaults: the 2026-11-12 film, its rates, its render time and its
		// start (render plus present 9.9 ms: 5.2 of render, 4.7 of block).
		struct DriftScenario
		{
			double displayHz = 23.975217;
			double ppm = 14.25;
			size_t desired = 4;
			double minutes = 0.0; // 0 = the length of the logged film
			CutPattern cuts = CutPattern::Film20261112;
			double renderMs = 5.2;
			double startBlockMs = 4.7;
			double stallAtMinutes = -1.0;
			double stallMs = 0.0; // the render time of the one stalled frame
			// The first refresh from this time with a present waiting does
			// not take it (a missed vsync).
			double missedVsyncAtMinutes = -1.0;
			// Arrival jitter, uniform in [0, jitterMs).
			double jitterMs = 3.0;
			// How many presents may wait for a refresh before a present
			// blocks.
			size_t presentSlots = 1;
			// End the run at the first refresh from measureFrom that goes
			// without a new frame (a slower source's display running dry).
			bool stopAtFirstEmptyRefresh = false;
			// The policy's capture rate is the true one times 1 + this / 1e6;
			// the chain keeps the true rate.
			double rateErrorPpm = 0.0;
		};

		struct DriftResult
		{
			uint64_t presents = 0;
			uint64_t dropsAtCut = 0;
			uint64_t dropsByLimit = 0;
			// Drops the limit took although a cut came while they were due.
			uint64_t limitDropsPastACut = 0;
			uint64_t repeats = 0;
			uint64_t staleAgeRecoveries = 0;
			uint64_t renderStallRecoveries = 0;
			uint64_t backstopFramesDropped = 0;
			uint64_t emptyRefreshes = 0;
			uint64_t emptyRefreshesAfterDrop = 0;
			// The first refresh from measureFrom that went without a new frame.
			double firstEmptyRefreshSeconds = -1.0;
			// R4 for a slower source: drops taken while G stood less than one
			// source period above the lowest G so far.
			uint64_t slowerDropsWithinAPeriod = 0;
			size_t maxPostPopDepth = 0;
			double firstDueSeconds = -1.0;
			uint64_t firstDueSequence = 0;
			double firstDropSeconds = -1.0;
			uint64_t firstDropSequence = 0;
			bool firstDropAtCut = false;
			double lastDropSeconds = -1.0;
			double longestDueWaitSeconds = 0.0;
			double maxBlockMs = 0.0;
			double maxCutGapSeconds = 0.0;
			double sourcePeriodMs = 0.0;
			// G (ms) from measureFrom: its range, and R2's longest stretch
			// above the lowest G so far plus one source period, the drift of
			// the cut wait W and 1 ms, against its bound B.
			double minGMs = 1.0e9;
			double maxGMs = -1.0e9;
			double waitBoundSeconds = 0.0;
			double excessBoundSeconds = 0.0;
			double longestExcessSeconds = 0.0;
			double longestExcessFromSeconds = -1.0;
			// The same with the stretch restarted at each corrector drop, as
			// R2 counts it: the corrector takes one frame per cut, so each
			// frame above the floor has its own cut wait.
			double longestExcessBetweenDropsSeconds = 0.0;
			// The policy's ledger at the last evaluation, and the evaluations
			// at which it disagreed with the model's own counts (R5).
			uint64_t ledgerRepeatedRefreshes = 0;
			uint64_t ledgerFramesFlushed = 0;
			uint64_t ledgerPrematureDrops = 0;
			uint64_t ledgerRebases = 0;
			int64_t ledgerReferenceShift = 0;
			uint64_t ledgerMismatches = 0;
			double firstLedgerMismatchSeconds = -1.0;
		};

		// The longest a due drop may wait: one cut interval, at most the
		// policy's three-minute limit.
		constexpr double DROP_DUE_LIMIT_SECONDS = 180.0;
		// A failing run's trace keeps this many evaluations from the start
		// of its window and as many from the end.
		constexpr size_t TRACE_HALF_LINES = 200;
		// The failures one test lists before it only counts them.
		constexpr size_t MAX_LISTED_FAILURES = 60;

		// One run's evaluations from `from` to 10 s after the first drop at or
		// after it (7.7).
		struct DriftTrace
		{
			double from = 0.0;
			double to = std::numeric_limits<double>::infinity();
			std::vector<std::wstring> head;
			std::deque<std::wstring> tail;
			uint64_t omitted = 0;

			void Add(const wchar_t* line)
			{
				if (head.size() < TRACE_HALF_LINES)
				{
					head.emplace_back(line);
					return;
				}
				tail.emplace_back(line);
				if (tail.size() > TRACE_HALF_LINES)
				{
					tail.pop_front();
					++omitted;
				}
			}
		};

		struct CutList
		{
			const uint64_t* sequences;
			size_t count;
		};

		CutList LoggedCuts(CutPattern cuts)
		{
			using namespace AlphaCadenceDriftSceneEvents;
			if (cuts == CutPattern::Film20261109)
				return { FILM_2026_11_09,
					sizeof(FILM_2026_11_09) / sizeof(FILM_2026_11_09[0]) };
			if (cuts == CutPattern::Film20261112)
				return { FILM_2026_11_12,
					sizeof(FILM_2026_11_12) / sizeof(FILM_2026_11_12[0]) };
			return { nullptr, 0 };
		}

		const wchar_t* CutName(CutPattern cuts)
		{
			switch (cuts)
			{
			case CutPattern::Film20261109: return L"film 2026-11-09";
			case CutPattern::Film20261112: return L"film 2026-11-12";
			case CutPattern::Every24Seconds: return L"every 24 s";
			case CutPattern::Every120Seconds: return L"every 120 s";
			default: return L"none";
			}
		}

		std::wstring Describe(const DriftScenario& scenario)
		{
			wchar_t line[320];
			swprintf_s(line,
				L"display %.6f Hz, %+.2f ppm, target %zu, render %.1f ms, "
				L"start block %.1f ms, jitter %.0f ms, slots %zu, cuts %s",
				scenario.displayHz, scenario.ppm, scenario.desired,
				scenario.renderMs, scenario.startBlockMs, scenario.jitterMs,
				scenario.presentSlots, CutName(scenario.cuts));
			std::wstring text = line;
			if (scenario.stallAtMinutes >= 0.0)
			{
				swprintf_s(line, L", stall %.1f ms at %.5f min",
					scenario.stallMs, scenario.stallAtMinutes);
				text += line;
			}
			if (scenario.missedVsyncAtMinutes >= 0.0)
			{
				swprintf_s(line, L", missed vsync at %.5f min",
					scenario.missedVsyncAtMinutes);
				text += line;
			}
			if (scenario.rateErrorPpm != 0.0)
			{
				swprintf_s(line, L", rate error %+.2f ppm",
					scenario.rateErrorPpm);
				text += line;
			}
			return text;
		}

		const wchar_t* ActionName(AlphaCadenceAction action)
		{
			switch (action)
			{
			case AlphaCadenceAction::Drop: return L"drop";
			case AlphaCadenceAction::Repeat: return L"repeat";
			default: return L"none";
			}
		}

		const wchar_t* BlockReasonName(AlphaCadenceBlockReason reason)
		{
			switch (reason)
			{
			case AlphaCadenceBlockReason::None: return L"none";
			case AlphaCadenceBlockReason::Disabled: return L"disabled";
			case AlphaCadenceBlockReason::PresentationEvidenceUnavailable:
				return L"evidence";
			case AlphaCadenceBlockReason::InvalidRates: return L"rates";
			case AlphaCadenceBlockReason::RateMismatchTooLarge:
				return L"mismatch";
			case AlphaCadenceBlockReason::StabilizingRates: return L"stabilizing";
			case AlphaCadenceBlockReason::NoActionableMismatch:
				return L"no_mismatch";
			case AlphaCadenceBlockReason::BeforeDeadline: return L"before_due";
			case AlphaCadenceBlockReason::Cooldown: return L"cooldown";
			case AlphaCadenceBlockReason::VerificationPending:
				return L"verifying";
			case AlphaCadenceBlockReason::DropQueueNotAboveDesired:
				return L"drop_queue";
			case AlphaCadenceBlockReason::RepeatQueueNotBelowDesired:
				return L"repeat_queue";
			case AlphaCadenceBlockReason::DropPresentationDebtMissing:
				return L"drop_debt";
			case AlphaCadenceBlockReason::RepeatPresentationDebtPresent:
				return L"repeat_debt";
			case AlphaCadenceBlockReason::WaitingForFreshScene:
				return L"waiting_scene";
			case AlphaCadenceBlockReason::FallbackNotMature:
				return L"fallback_young";
			case AlphaCadenceBlockReason::DropFallbackQueueTooYoung:
				return L"drop_fallback_young";
			default: return L"unknown";
			}
		}

		// A due decision asks for a drop when the policy forecasts one, or,
		// with no forecast, when the phase stands ahead of the reference.
		bool DropDue(const AlphaCadenceCorrectionDecision& decision)
		{
			return decision.due &&
				(decision.predictedAction == AlphaCadenceAction::Drop ||
					(decision.predictedAction == AlphaCadenceAction::None &&
						decision.phaseFrames > 0.0));
		}

		DriftResult RunDrift(const DriftScenario& scenario,
			DriftTrace* trace = nullptr)
		{
			using namespace AlphaQueuePolicy;
			const CutList logged = LoggedCuts(scenario.cuts);

			const double displayPeriod = 1.0 / scenario.displayHz;
			const double captureHz =
				scenario.displayHz * (1.0 + scenario.ppm / 1000000.0);
			const double sourcePeriod = 1.0 / captureHz;
			const size_t desired = NormalizeDesiredDepth(scenario.desired);
			const size_t presentSlots =
				std::max<size_t>(1, scenario.presentSlots);
			const double renderSeconds = scenario.renderMs / 1000.0;
			const double seconds = scenario.minutes > 0.0 || logged.count == 0
				? scenario.minutes * 60.0
				: static_cast<double>(logged.sequences[logged.count - 1]) /
					captureHz;
			const uint64_t cutEvery =
				scenario.cuts == CutPattern::Every24Seconds
					? static_cast<uint64_t>(std::llround(24.0 * captureHz))
					: (scenario.cuts == CutPattern::Every120Seconds
						? static_cast<uint64_t>(std::llround(120.0 * captureHz))
						: 0);

			DriftResult result;
			result.sourcePeriodMs = sourcePeriod * 1000.0;
			if (logged.count != 0)
			{
				uint64_t gap = logged.sequences[0];
				for (size_t index = 1; index < logged.count; ++index)
					gap = std::max(gap,
						logged.sequences[index] - logged.sequences[index - 1]);
				result.maxCutGapSeconds = static_cast<double>(gap) / captureHz;
			}
			else if (cutEvery != 0)
				result.maxCutGapSeconds = static_cast<double>(cutEvery) / captureHz;
			else
				result.maxCutGapSeconds = seconds;
			// R2: W is the longest cut gap capped at the limit (the limit with
			// no cuts); B is W, 240 refreshes (the cut wait and the stabilize
			// gate after a backstop) and 1.5 s. A stretch above the bound
			// restarts at each corrector drop: one frame per cut.
			result.waitBoundSeconds = logged.count == 0 && cutEvery == 0
				? DROP_DUE_LIMIT_SECONDS
				: std::min(result.maxCutGapSeconds, DROP_DUE_LIMIT_SECONDS);
			result.excessBoundSeconds = result.waitBoundSeconds +
				240.0 / scenario.displayHz + 1.5;
			// The drift of W counts where it adds delay, a faster source's;
			// a slower source's chain only drains while it waits.
			const double excessAboveFloorMs = result.sourcePeriodMs +
				std::max(0.0, scenario.ppm) / 1000000.0 *
					result.waitBoundSeconds * 1000.0 +
				1.0;

			std::mt19937 random(20261113u);
			std::uniform_real_distribution<double> unit(0.0, 1.0);
			const double jitterSeconds = scenario.jitterMs / 1000.0;
			auto arrivalJitter = [&]()
			{
				return unit(random) * jitterSeconds;
			};

			struct Frame
			{
				uint64_t sequence;
				double arrival;
				uint32_t presentId;
			};
			std::deque<Frame> queue;
			uint64_t nextSequence = 1;
			double nextArrival = arrivalJitter();
			auto admit = [&](double now)
			{
				while (nextArrival <= now)
				{
					queue.push_back({ nextSequence, nextArrival, 0 });
					++nextSequence;
					nextArrival = static_cast<double>(nextSequence - 1) *
						sourcePeriod + arrivalJitter();
				}
			};

			// Start: the queue prefilled to the target, the present stage full
			// (a frame on screen and one queued), the first present blocking
			// for the start block; any start surplus beyond the most a present
			// can block (one refresh less the render) is in the head's age.
			double now = 0.0;
			while (queue.size() < desired)
			{
				now = nextArrival;
				admit(now);
			}
			const double blockRoom = std::max(0.0, displayPeriod - renderSeconds);
			const double startBlock =
				std::min(scenario.startBlockMs / 1000.0, blockRoom);
			const double startAge = scenario.startBlockMs / 1000.0 - startBlock;
			now += startAge;
			std::deque<Frame> presentQueue;
			presentQueue.push_back({ 0, 0.0, 0 });
			double nextRefresh = now + renderSeconds + startBlock;
			double previousLoopBlockMs = (renderSeconds + startBlock) * 1000.0;

			// G is measured from when the corrector can first act.
			const double measureFrom =
				std::max(0.25, 8.0 / scenario.displayHz) +
				(600.0 + 120.0) / scenario.displayHz;
			bool presented = false;
			double lastDropAt = -1.0;
			bool missedVsyncDone = scenario.missedVsyncAtMinutes < 0.0;
			// The DXGI pair of the frame on screen, and the empty refreshes
			// counted when it was shown (the ones the pair reflects).
			uint32_t refreshIndex = 0;
			uint32_t shownPresentId = 0;
			uint32_t shownRefresh = 0;
			bool pairValid = false;
			uint64_t emptyAtShow = 0;
			uint64_t shownSequence = 0;
			double lastGMs = 0.0;
			double excessSince = -1.0;
			double excessSinceDrop = -1.0;
			auto refreshUntil = [&](double until)
			{
				while (nextRefresh <= until)
				{
					if (!missedVsyncDone && !presentQueue.empty() &&
						nextRefresh >= scenario.missedVsyncAtMinutes * 60.0)
					{
						// The display keeps the previous frame; the waiting
						// present stays queued for the next refresh.
						missedVsyncDone = true;
						++result.emptyRefreshes;
					}
					else if (!presentQueue.empty())
					{
						const Frame shown = presentQueue.front();
						presentQueue.pop_front();
						shownPresentId = shown.presentId;
						shownRefresh = refreshIndex;
						pairValid = true;
						emptyAtShow = result.emptyRefreshes;
						shownSequence = shown.sequence;
						if (shown.sequence != 0)
						{
							lastGMs = (nextRefresh - static_cast<double>(
								shown.sequence - 1) * sourcePeriod) * 1000.0;
						}
						if (shown.sequence != 0 && nextRefresh >= measureFrom)
						{
							result.minGMs = std::min(result.minGMs, lastGMs);
							result.maxGMs = std::max(result.maxGMs, lastGMs);
							if (lastGMs > result.minGMs + excessAboveFloorMs)
							{
								if (excessSince < 0.0)
									excessSince = nextRefresh;
								if (nextRefresh - excessSince >
									result.longestExcessSeconds)
								{
									result.longestExcessSeconds =
										nextRefresh - excessSince;
									result.longestExcessFromSeconds =
										excessSince;
								}
								if (excessSinceDrop < 0.0)
									excessSinceDrop = nextRefresh;
								result.longestExcessBetweenDropsSeconds = std::max(
									result.longestExcessBetweenDropsSeconds,
									nextRefresh - excessSinceDrop);
							}
							else
							{
								excessSince = -1.0;
								excessSinceDrop = -1.0;
							}
						}
					}
					else if (presented)
					{
						++result.emptyRefreshes;
						if (result.firstEmptyRefreshSeconds < 0.0 &&
							nextRefresh >= measureFrom)
							result.firstEmptyRefreshSeconds = nextRefresh;
						if (lastDropAt >= 0.0 &&
							nextRefresh - lastDropAt <= 2.0 * displayPeriod)
							++result.emptyRefreshesAfterDrop;
					}
					++refreshIndex;
					nextRefresh += displayPeriod;
				}
			};

			// The policy epoch never changes in a run: backlog recovery calls
			// OnBacklogRecovery with the frames it flushed, as the renderer
			// does.
			AlphaCadenceCorrectionPolicy policy;
			const uint64_t policyGeneration = 0x9e3779b97f4a7c15ULL;
			// The telemetry's warm-up (AlphaPresentationTelemetry: 8 samples
			// and 0.25 s): at the start and after each backstop's reset it
			// reports no display rate and no evidence until both have passed.
			double warmUpFrom = 0.0;
			uint32_t warmUpFromPresentId = 0;
			uint32_t presentId = 0;
			uint64_t sceneEventId = 0;
			size_t loggedIndex = 0;
			bool stallDone = scenario.stallAtMinutes < 0.0;
			double dueSince = -1.0;
			uint64_t cutsWhileDue = 0;
			// R5: the empty refreshes the pair reflected when the policy last
			// re-based, before the display rate was first known.
			bool ledgerHasPair = false;
			bool ledgerBaseTaken = false;
			uint64_t emptyAtLedgerBase = 0;

			while (now < seconds && !(scenario.stopAtFirstEmptyRefresh &&
				result.firstEmptyRefreshSeconds >= 0.0))
			{
				admit(now);
				while (!CanDequeue(queue.size(), desired, false))
				{
					now = std::max(now, nextArrival);
					admit(now);
				}
				const Frame frame = queue.front();
				queue.pop_front();
				const size_t postPopDepth = queue.size();

				bool cut = false;
				if (logged.count != 0)
				{
					while (loggedIndex < logged.count &&
						logged.sequences[loggedIndex] < frame.sequence)
						++loggedIndex;
					cut = loggedIndex < logged.count &&
						logged.sequences[loggedIndex] == frame.sequence;
				}
				else if (cutEvery != 0)
					cut = frame.sequence % cutEvery == 0;

				const bool stable =
					presentId - warmUpFromPresentId >= 8 &&
					now - warmUpFrom >= 0.25;
				AlphaCadenceCorrectionInput input;
				input.enabled = true;
				input.generation = policyGeneration;
				input.presentationEvidence = stable
					? AlphaPresentationEvidence::Stable
					: AlphaPresentationEvidence::Unavailable;
				input.captureRateHz =
					captureHz * (1.0 + scenario.rateErrorPpm / 1000000.0);
				input.displayRateHz = stable ? scenario.displayHz : 0.0;
				input.queueDepth = postPopDepth;
				input.desiredQueueDepth = desired;
				input.oldestQueuedAgeMs = queue.empty() ? 0.0 :
					(now - queue.front().arrival) * 1000.0;
				input.presentationDebt = stable ? 2 : 0;
				input.lastPresentId = presentId;
				input.dxgiPresentCount = shownPresentId;
				input.dxgiPresentRefreshCount = shownRefresh;
				input.dxgiSampleValid = pairValid;
				input.safeSceneBoundary = cut;
				input.sceneEventId = cut ? ++sceneEventId : 0;
				input.sourceSequence = frame.sequence;
				const auto decision = policy.Evaluate(input);

				// R5: the ledger counts what the model counted, from the pair
				// it held when the display rate was first known: its first pair
				// and every repeated refresh before that only re-base it.
				if (pairValid && !ledgerBaseTaken)
				{
					if (ledgerHasPair && input.displayRateHz != 0.0)
						ledgerBaseTaken = true;
					else
						emptyAtLedgerBase = emptyAtShow;
					ledgerHasPair = true;
				}
				const AlphaCadenceDiagnosticSnapshot& ledger =
					decision.diagnostic;
				const uint64_t expectedRepeated =
					ledgerBaseTaken ? emptyAtShow - emptyAtLedgerBase : 0;
				if (ledger.repeatedRefreshes != expectedRepeated ||
					ledger.framesFlushed != result.backstopFramesDropped)
				{
					if (result.ledgerMismatches == 0)
						result.firstLedgerMismatchSeconds = now;
					++result.ledgerMismatches;
				}
				result.ledgerRepeatedRefreshes = ledger.repeatedRefreshes;
				result.ledgerFramesFlushed = ledger.framesFlushed;
				result.ledgerPrematureDrops = ledger.prematureDrops;
				result.ledgerRebases = ledger.dxgiRebases;
				result.ledgerReferenceShift = ledger.referenceShiftFrames;

				if (trace != nullptr && now >= trace->from && now <= trace->to)
				{
					wchar_t line[400];
					swprintf_s(line,
						L"%10.3f %9llu %9.3f %9llu %5zu %+11.6f %+9.6f %5llu %5llu "
						L"%5llu %+5lld %-7s %-7s %s",
						now, static_cast<unsigned long long>(frame.sequence),
						lastGMs, static_cast<unsigned long long>(shownSequence),
						postPopDepth, decision.phaseFrames,
						ledger.aboveFloorFrames,
						static_cast<unsigned long long>(ledger.repeatedRefreshes),
						static_cast<unsigned long long>(ledger.framesFlushed),
						static_cast<unsigned long long>(ledger.prematureDrops),
						static_cast<long long>(ledger.referenceShiftFrames),
						!decision.due ? L"none"
							: (DropDue(decision) ? L"drop" : L"repeat"),
						ActionName(decision.action),
						BlockReasonName(decision.blockReason));
					trace->Add(line);
					if (decision.action == AlphaCadenceAction::Drop &&
						trace->to == std::numeric_limits<double>::infinity())
						trace->to = now + 10.0;
				}

				if (DropDue(decision))
				{
					if (dueSince < 0.0)
					{
						dueSince = now;
						cutsWhileDue = 0;
					}
					if (result.firstDueSeconds < 0.0)
					{
						result.firstDueSeconds = now;
						result.firstDueSequence = frame.sequence;
					}
				}
				else if (!decision.due)
					dueSince = -1.0;
				if (cut && dueSince >= 0.0 &&
					decision.action != AlphaCadenceAction::Drop)
					++cutsWhileDue;

				if (decision.action == AlphaCadenceAction::Drop)
				{
					// R4, a slower source: a drop only while the chain stands
					// a whole source period above its floor, the lowest G so
					// far; never one the drift brought.
					if (scenario.ppm < 0.0 &&
						!(result.minGMs < 1.0e9 &&
							lastGMs >= result.minGMs + result.sourcePeriodMs))
						++result.slowerDropsWithinAPeriod;
					if (excessSinceDrop >= 0.0)
						excessSinceDrop = now;
					if (decision.deadlineFallback)
					{
						++result.dropsByLimit;
						if (cutsWhileDue != 0)
							++result.limitDropsPastACut;
					}
					else
						++result.dropsAtCut;
					if (result.firstDropSeconds < 0.0)
					{
						result.firstDropSeconds = now;
						result.firstDropSequence = frame.sequence;
						result.firstDropAtCut = !decision.deadlineFallback;
					}
					result.lastDropSeconds = now;
					if (dueSince >= 0.0)
						result.longestDueWaitSeconds =
							std::max(result.longestDueWaitSeconds, now - dueSince);
					dueSince = -1.0;
					lastDropAt = now;
					continue;
				}
				if (decision.action == AlphaCadenceAction::Repeat)
					++result.repeats;
				result.maxPostPopDepth =
					std::max(result.maxPostPopDepth, postPopDepth);

				// A repeat renders and presents the same frame twice.
				const int renders =
					decision.action == AlphaCadenceAction::Repeat ? 2 : 1;
				for (int render = 0; render < renders; ++render)
				{
					double renderTime = renderSeconds;
					if (!stallDone && now >= scenario.stallAtMinutes * 60.0)
					{
						stallDone = true;
						renderTime = scenario.stallMs / 1000.0;
					}
					const double cycleStart = now;
					now += renderTime;
					refreshUntil(now);
					// A present into a full stage blocks until a refresh
					// takes a waiting one.
					while (presentQueue.size() >= presentSlots)
					{
						now = nextRefresh;
						refreshUntil(now);
					}
					++presentId;
					presentQueue.push_back(
						{ frame.sequence, frame.arrival, presentId });
					++result.presents;
					presented = true;
					previousLoopBlockMs = (now - cycleStart) * 1000.0;
					result.maxBlockMs = std::max(result.maxBlockMs,
						(now - cycleStart - renderTime) * 1000.0);
				}

				admit(now);
				const double oldestMs = queue.empty() ? 0.0 :
					(now - queue.front().arrival) * 1000.0;
				if (ShouldRecoverBacklog(queue.size(), desired, oldestMs,
					previousLoopBlockMs, captureHz))
				{
					if (previousLoopBlockMs >= RenderStallThresholdMs(captureHz))
						++result.renderStallRecoveries;
					else
						++result.staleAgeRecoveries;
					uint32_t flushed = 0;
					while (queue.size() > desired)
					{
						queue.pop_front();
						++flushed;
					}
					result.backstopFramesDropped += flushed;
					policy.OnBacklogRecovery(flushed);
					warmUpFrom = now;
					warmUpFromPresentId = presentId;
					dueSince = -1.0;
				}
			}
			if (!scenario.stopAtFirstEmptyRefresh)
				refreshUntil(seconds);
			return result;
		}

		void Report(const DriftScenario& scenario, const DriftResult& result)
		{
			wchar_t line[2048];
			swprintf_s(line,
				L"%s: presents %llu; first due "
				L"%.1f min (source %llu); drops %llu at cuts, %llu by the limit "
				L"(%llu past a cut), "
				L"first %.1f min (source %llu, %s); repeats %llu; longest due "
				L"wait %.1f s; backstop %llu stale-age, %llu render-stall, %llu "
				L"frames; empty refreshes %llu (%llu after a drop); max post-pop "
				L"depth %zu; max present block %.1f ms; G %.1f to %.1f ms, "
				L"longest above its floor %.1f s from %.1f s, %.1f s between "
				L"drops (B %.1f s); ledger "
				L"rep_refresh %llu flushed %llu premature %llu ref_shift %+lld "
				L"rebase %llu, %llu evaluations off the model from %.1f s\n",
				Describe(scenario).c_str(),
				static_cast<unsigned long long>(result.presents),
				result.firstDueSeconds / 60.0,
				static_cast<unsigned long long>(result.firstDueSequence),
				static_cast<unsigned long long>(result.dropsAtCut),
				static_cast<unsigned long long>(result.dropsByLimit),
				static_cast<unsigned long long>(result.limitDropsPastACut),
				result.firstDropSeconds / 60.0,
				static_cast<unsigned long long>(result.firstDropSequence),
				result.firstDropSeconds < 0.0 ? L"none"
					: (result.firstDropAtCut ? L"at a cut" : L"by the limit"),
				static_cast<unsigned long long>(result.repeats),
				result.longestDueWaitSeconds,
				static_cast<unsigned long long>(result.staleAgeRecoveries),
				static_cast<unsigned long long>(result.renderStallRecoveries),
				static_cast<unsigned long long>(result.backstopFramesDropped),
				static_cast<unsigned long long>(result.emptyRefreshes),
				static_cast<unsigned long long>(result.emptyRefreshesAfterDrop),
				result.maxPostPopDepth, result.maxBlockMs,
				result.minGMs, result.maxGMs, result.longestExcessSeconds,
				result.longestExcessFromSeconds,
				result.longestExcessBetweenDropsSeconds,
				result.excessBoundSeconds,
				static_cast<unsigned long long>(result.ledgerRepeatedRefreshes),
				static_cast<unsigned long long>(result.ledgerFramesFlushed),
				static_cast<unsigned long long>(result.ledgerPrematureDrops),
				static_cast<long long>(result.ledgerReferenceShift),
				static_cast<unsigned long long>(result.ledgerRebases),
				static_cast<unsigned long long>(result.ledgerMismatches),
				result.firstLedgerMismatchSeconds);
			Logger::WriteMessage(line);
		}

		// 7.7: re-run a failing scenario (the same seed gives the same run)
		// and print its evaluations from 2 s before its event, or 60 s before
		// its first due, to 10 s after the next drop.
		void PrintTrace(const DriftScenario& scenario, const DriftResult& result)
		{
			const double eventSeconds = scenario.stallAtMinutes >= 0.0
				? scenario.stallAtMinutes * 60.0
				: scenario.missedVsyncAtMinutes * 60.0;
			DriftTrace trace;
			if (eventSeconds >= 0.0)
				trace.from = eventSeconds - 2.0;
			else if (result.firstDueSeconds >= 0.0)
				trace.from = result.firstDueSeconds - 60.0;
			else if (result.longestExcessFromSeconds >= 0.0)
				trace.from = result.longestExcessFromSeconds - 2.0;
			RunDrift(scenario, &trace);

			std::wstring text = L"TRACE " + Describe(scenario) +
				L"\n      time    source    G (ms)     shown depth       phase "
				L"   floor+   rep flush prem. shift request action  reason\n";
			for (const auto& line : trace.head)
				text += line + L"\n";
			if (trace.omitted != 0)
				text += L"  ... " + std::to_wstring(trace.omitted) +
					L" evaluations left out ...\n";
			for (const auto& line : trace.tail)
				text += line + L"\n";
			Logger::WriteMessage(text.c_str());
		}

		// Every scenario is run and reported before any assertion, so a
		// failing build still prints its whole table.
		struct Expectations
		{
			std::wstring failures;
			size_t count = 0;

			void Expect(bool ok, const DriftScenario& scenario,
				const wchar_t* what)
			{
				if (ok)
					return;
				if (++count > MAX_LISTED_FAILURES)
					return;
				failures += L"[" + Describe(scenario) + L"] " + what + L"\n";
			}

			void AssertNone() const
			{
				if (count == 0)
					return;
				std::wstring text = failures;
				if (count > MAX_LISTED_FAILURES)
					text += L"... and " +
						std::to_wstring(count - MAX_LISTED_FAILURES) +
						L" more\n";
				Assert::Fail(text.c_str());
			}
		};

		double DueWaitBoundSeconds(const DriftResult& result)
		{
			return std::min(result.maxCutGapSeconds, DROP_DUE_LIMIT_SECONDS);
		}

		// R1-R6 (the frozen spec's 7.5), one bit each, so a test can count
		// which of them a run failed.
		constexpr int REQUIREMENT_COUNT = 6;
		unsigned FailedRequirements(const DriftScenario& scenario,
			const DriftResult& result)
		{
			const bool met[REQUIREMENT_COUNT] =
			{
				result.emptyRefreshesAfterDrop == 0,
				result.longestExcessBetweenDropsSeconds <=
					result.excessBoundSeconds,
				result.longestDueWaitSeconds <= DueWaitBoundSeconds(result) + 1.0 &&
					result.limitDropsPastACut == 0,
				scenario.ppm > 0.0 ? result.repeats == 0
					: (scenario.ppm < 0.0
						? result.slowerDropsWithinAPeriod == 0 : true),
				result.ledgerMismatches == 0,
				result.ledgerPrematureDrops == 0
			};
			unsigned failed = 0;
			for (int rule = 0; rule < REQUIREMENT_COUNT; ++rule)
			{
				if (!met[rule])
					failed |= 1u << rule;
			}
			return failed;
		}

		// R1-R6, checked on every run.
		void ExpectRequirements(Expectations& expectations,
			const DriftScenario& scenario, const DriftResult& result)
		{
			const unsigned failed = FailedRequirements(scenario, result);
			expectations.Expect((failed & 1u) == 0, scenario,
				L"R1: a refresh went without a new frame within two display "
				L"periods of a corrector drop");
			expectations.Expect((failed & 2u) == 0, scenario,
				L"R2: G stayed above the lowest G so far plus one source "
				L"period, the drift of W and 1 ms for longer than B without "
				L"a corrector drop");
			expectations.Expect((failed & 4u) == 0, scenario,
				L"R3: a due drop passed a cut, or waited longer than one cut "
				L"interval or the limit");
			expectations.Expect((failed & 8u) == 0, scenario,
				scenario.ppm > 0.0
					? L"R4: the corrector repeated for a faster source"
					: L"R4: the corrector dropped for a slower source while G "
					  L"stood less than one source period above the lowest G "
					  L"so far");
			expectations.Expect((failed & 16u) == 0, scenario,
				L"R5: the ledger's repeated refreshes or flushed frames "
				L"differed from the model's");
			expectations.Expect((failed & 32u) == 0, scenario,
				L"R6: a drop was counted premature");
		}

		// R1-R6, and the trace of the run if any of them failed.
		void CheckRequirements(Expectations& expectations,
			const DriftScenario& scenario, const DriftResult& result)
		{
			const size_t before = expectations.count;
			ExpectRequirements(expectations, scenario, result);
			if (expectations.count != before)
				PrintTrace(scenario, result);
		}

		// The corrector dropped, and the backstop never fired on age.
		void ExpectDropped(Expectations& expectations,
			const DriftScenario& scenario, const DriftResult& result)
		{
			expectations.Expect(result.dropsAtCut + result.dropsByLimit >= 1,
				scenario, L"the corrector never dropped");
			expectations.Expect(result.staleAgeRecoveries == 0, scenario,
				L"the backstop fired on queue age");
		}

		// A source faster than the display, nothing else: R1-R6, the
		// corrector drops, the backstop never fires, and no refresh goes
		// without a new frame.
		void ExpectCorrected(Expectations& expectations,
			const DriftScenario& scenario, const DriftResult& result)
		{
			ExpectDropped(expectations, scenario, result);
			expectations.Expect(result.renderStallRecoveries == 0, scenario,
				L"the backstop fired on a render stall");
			expectations.Expect(result.emptyRefreshes == 0, scenario,
				L"a refresh went without a new frame");
			CheckRequirements(expectations, scenario, result);
		}

		// A 24.000 Hz display at a drift the forecast hardly sees, cuts every
		// 24 s, 100 minutes.
		DriftScenario LowDrift24HzScenario(double ppm)
		{
			DriftScenario scenario;
			scenario.displayHz = 24.0;
			scenario.ppm = ppm;
			scenario.minutes = 100.0;
			scenario.cuts = CutPattern::Every24Seconds;
			return scenario;
		}

		// One extra frame entered the chain at eventMinutes. At 24 Hz the
		// backstop's stall threshold is two periods, so whether the backstop
		// or the corrector removes the frame depends on the loop's pace at
		// that moment. Either way it is removed exactly once, at once by the
		// backstop or by one drop at the next cut, and nothing else acts.
		void ExpectExtraFrameRemovedOnce(Expectations& expectations,
			const DriftScenario& scenario, const DriftResult& result,
			double eventMinutes)
		{
			const uint64_t drops = result.dropsAtCut + result.dropsByLimit;
			const double eventSeconds = eventMinutes * 60.0;
			const bool byBackstop = result.renderStallRecoveries == 1 &&
				result.backstopFramesDropped == 1 && drops == 0;
			const bool byCorrector = result.renderStallRecoveries == 0 &&
				drops == 1 && result.firstDropAtCut &&
				result.firstDropSeconds > eventSeconds &&
				result.firstDropSeconds <= eventSeconds +
					result.maxCutGapSeconds + 120.0 / scenario.displayHz + 1.0;
			expectations.Expect(byBackstop || byCorrector, scenario,
				L"the extra frame was not removed exactly once (by the "
				L"backstop, or by one drop at the next cut)");
			expectations.Expect(result.emptyRefreshes == 1, scenario,
				L"not exactly one refresh went without a new frame");
			expectations.Expect(result.staleAgeRecoveries == 0, scenario,
				L"the backstop fired on queue age");
			expectations.Expect(result.emptyRefreshesAfterDrop == 0, scenario,
				L"a refresh went without a new frame right after a drop");
		}

		// 7.6, the sweep. Every axis at every value; cuts every 24 s.
		const double SWEEP_RATES[] =
		{
			24000.0 / 1001.0, 24.0, 25.0, 50.0, 60000.0 / 1001.0, 60.0
		};
		const double SWEEP_DRIFTS[] = { -14.25, -0.5, 0.0, 0.5, 2.0, 14.25, 150.0 };
		constexpr int SWEEP_DRIFT_COUNT =
			sizeof(SWEEP_DRIFTS) / sizeof(SWEEP_DRIFTS[0]);
		const wchar_t* const SWEEP_EVENTS[] =
		{
			L"none", L"missed vsync", L"stall 2 periods + 1 ms",
			L"stall 5 periods", L"stall 12 periods"
		};
		constexpr int SWEEP_EVENT_COUNT =
			sizeof(SWEEP_EVENTS) / sizeof(SWEEP_EVENTS[0]);
		constexpr int SWEEP_EVENT_PHASES = 8;
		// The failing runs a sweep reports and traces in full.
		constexpr size_t SWEEP_TRACED_RUNS = 5;

		struct SweepRun
		{
			DriftScenario scenario;
			int event;
			int drift;
		};

		std::vector<SweepRun> SweepRuns()
		{
			const double jitters[] = { 0.0, 3.0 };
			const size_t slots[] = { 1, 2 };
			std::vector<SweepRun> runs;
			for (const double rate : SWEEP_RATES)
			{
				const double periodMs = 1000.0 / rate;
				for (int drift = 0; drift < SWEEP_DRIFT_COUNT; ++drift)
				{
					const double ppm = SWEEP_DRIFTS[drift];
					for (int start = 0; start < 3; ++start)
					{
						for (int event = 0; event < SWEEP_EVENT_COUNT; ++event)
						{
							const int phases = event == 0 ? 1 : SWEEP_EVENT_PHASES;
							for (int phase = 0; phase < phases; ++phase)
							{
								for (const double jitterMs : jitters)
								{
									for (const size_t presentSlots : slots)
									{
										SweepRun run{ {}, event, drift };
										DriftScenario& s = run.scenario;
										s.displayHz = rate;
										s.ppm = ppm;
										s.cuts = CutPattern::Every24Seconds;
										// None, half a period, a full period
										// less the render.
										s.startBlockMs = start == 0 ? 0.0
											: (start == 1 ? periodMs / 2.0
												: periodMs - s.renderMs);
										s.jitterMs = jitterMs;
										s.presentSlots = presentSlots;
										if (event == 0)
										{
											s.minutes =
												std::abs(ppm) >= 2.0 ? 120.0 : 30.0;
										}
										else
										{
											s.minutes = 30.0;
											// 10 minutes plus one of 8 phases
											// across one display period.
											const double at = (600.0 +
												phase * periodMs / 1000.0 /
													SWEEP_EVENT_PHASES) / 60.0;
											if (event == 1)
												s.missedVsyncAtMinutes = at;
											else
											{
												s.stallAtMinutes = at;
												s.stallMs = event == 2
													? 2.0 * periodMs + 1.0
													: (event == 3 ? 5.0 : 12.0) *
														periodMs;
											}
										}
										runs.push_back(run);
									}
								}
							}
						}
					}
				}
			}
			return runs;
		}

		// Every run is independent (its own policy and seed), so the runs
		// share out over threads; the results are the same in any order.
		unsigned RunAll(const std::vector<SweepRun>& runs,
			std::vector<DriftResult>& results)
		{
			std::atomic<size_t> next{ 0 };
			auto worker = [&]()
			{
				for (;;)
				{
					const size_t index = next.fetch_add(1);
					if (index >= runs.size())
						return;
					results[index] = RunDrift(runs[index].scenario);
				}
			};
			const unsigned count =
				std::max(1u, std::thread::hardware_concurrency());
			std::vector<std::thread> threads;
			for (unsigned thread = 0; thread < count; ++thread)
				threads.emplace_back(worker);
			for (auto& thread : threads)
				thread.join();
			return count;
		}

		// The drifts of CommonRatesAndDriftsAtTargetFour, at every sweep rate.
		const double COMMON_DRIFTS[] = { 14.25, 50.0, 150.0, -14.25, -150.0 };

		DriftScenario CommonScenario(double rate, double ppm)
		{
			DriftScenario scenario;
			scenario.displayHz = rate;
			scenario.ppm = ppm;
			scenario.minutes = 100.0;
			scenario.cuts = CutPattern::Every24Seconds;
			return scenario;
		}

		// The runs of SlowerSourceEventAfterTheDisplayRanDry. A slower source at
		// every sweep rate, -14.25 and -150 ppm, jitter 0 and 3 ms, 1 and 2
		// present slots, cuts every 24 s and none, is run to the first refresh
		// from measureFrom that goes without a new frame; from each, one event
		// (a missed vsync, or a stall of two periods + 1 ms, 5 or 12 periods)
		// at 8 points across the next frame of drift, run to 10 minutes past
		// the next time the display runs dry. SweepRun::drift indexes -14.25
		// and -150.
		std::vector<SweepRun> RanDryRuns(double rateErrorPpm,
			Expectations& expectations)
		{
			const double drifts[] = { -14.25, -150.0 };
			const double jitters[] = { 0.0, 3.0 };
			const size_t slots[] = { 1, 2 };
			const CutPattern cutPatterns[] =
			{
				CutPattern::Every24Seconds, CutPattern::None
			};
			std::vector<SweepRun> ranDryRuns;
			for (const double rate : SWEEP_RATES)
			{
				for (int drift = 0; drift < 2; ++drift)
				{
					for (const double jitterMs : jitters)
					{
						for (const size_t presentSlots : slots)
						{
							for (const CutPattern cuts : cutPatterns)
							{
								SweepRun run{ {}, 0, drift };
								DriftScenario& s = run.scenario;
								s.displayHz = rate;
								s.ppm = drifts[drift];
								s.cuts = cuts;
								s.jitterMs = jitterMs;
								s.presentSlots = presentSlots;
								s.minutes = 300.0;
								s.stopAtFirstEmptyRefresh = true;
								s.rateErrorPpm = rateErrorPpm;
								ranDryRuns.push_back(run);
							}
						}
					}
				}
			}
			std::vector<DriftResult> ranDry(ranDryRuns.size());
			RunAll(ranDryRuns, ranDry);

			std::vector<SweepRun> runs;
			for (size_t index = 0; index < ranDryRuns.size(); ++index)
			{
				const DriftScenario& base = ranDryRuns[index].scenario;
				const double ranDrySeconds = ranDry[index].firstEmptyRefreshSeconds;
				expectations.Expect(ranDrySeconds >= 0.0, base,
					L"the display never ran out of frames by itself");
				if (ranDrySeconds < 0.0)
					continue;
				// The time one whole frame of drift takes.
				const double captureHz =
					base.displayHz * (1.0 + base.ppm / 1000000.0);
				const double cycleSeconds =
					1000000.0 / (std::abs(base.ppm) * captureHz);
				const double periodMs = 1000.0 / base.displayHz;
				for (int event = 1; event < SWEEP_EVENT_COUNT; ++event)
				{
					for (int phase = 0; phase < SWEEP_EVENT_PHASES; ++phase)
					{
						SweepRun run{ base, event, ranDryRuns[index].drift };
						DriftScenario& s = run.scenario;
						s.stopAtFirstEmptyRefresh = false;
						s.minutes =
							(ranDrySeconds + cycleSeconds + 600.0) / 60.0;
						const double at = (ranDrySeconds +
							(phase + 0.5) / SWEEP_EVENT_PHASES * cycleSeconds) /
							60.0;
						if (event == 1)
							s.missedVsyncAtMinutes = at;
						else
						{
							s.stallAtMinutes = at;
							s.stallMs = event == 2 ? 2.0 * periodMs + 1.0
								: (event == 3 ? 5.0 : 12.0) * periodMs;
						}
						runs.push_back(run);
					}
				}
			}
			return runs;
		}
	}

	TEST_CLASS(AlphaCadenceDriftSimulationTests)
	{
	public:
		// The 2026-11-12 film as logged (+14.25 ppm, its cuts, 5.2 ms of
		// render and 4.7 ms of present block at the start): the forecast falls
		// due while the post-pop depth still rests at target - 1, and the
		// corrector must drop at the first cut after that.
		TEST_METHOD(LoggedFilmDropsAtTheFirstCutAfterDue)
		{
			DriftScenario scenario;
			const auto result = RunDrift(scenario);
			Report(scenario, result);

			const CutList cuts = LoggedCuts(scenario.cuts);
			uint64_t firstCutAfterDue = 0;
			for (size_t index = 0; index < cuts.count; ++index)
			{
				if (cuts.sequences[index] >= result.firstDueSequence)
				{
					firstCutAfterDue = cuts.sequences[index];
					break;
				}
			}
			Expectations expectations;
			expectations.Expect(result.firstDueSeconds >= 0.0, scenario,
				L"the forecast never fell due");
			// A whole frame of drift takes 1e6 / ppm source frames; nothing
			// may bring the drop due before that.
			expectations.Expect(static_cast<double>(result.firstDueSequence) >=
				1000000.0 / scenario.ppm, scenario,
				L"the drop fell due before a whole frame of drift");
			expectations.Expect(firstCutAfterDue != 0 &&
				result.firstDropAtCut &&
				result.firstDropSequence == firstCutAfterDue, scenario,
				L"the first drop was not at the first cut after due");
			ExpectCorrected(expectations, scenario, result);
			expectations.AssertNone();
		}

		// The same film started with none, half and nearly all of one
		// source period already in the present block.
		TEST_METHOD(LoggedFilmFromAnyStartBlock)
		{
			const double startBlocks[] = { 0.0, 20.0, 40.0 };
			Expectations expectations;
			for (const double startBlock : startBlocks)
			{
				DriftScenario scenario;
				scenario.startBlockMs = startBlock;
				const auto result = RunDrift(scenario);
				Report(scenario, result);
				ExpectCorrected(expectations, scenario, result);
			}
			expectations.AssertNone();
		}

		TEST_METHOD(LoggedFilmAtEveryTarget)
		{
			Expectations expectations;
			for (size_t desired = 1; desired <= 5; ++desired)
			{
				DriftScenario scenario;
				scenario.cuts = CutPattern::Film20261109;
				scenario.desired = desired;
				const auto result = RunDrift(scenario);
				Report(scenario, result);
				ExpectCorrected(expectations, scenario, result);
			}
			expectations.AssertNone();
		}

		TEST_METHOD(CommonRatesAndDriftsAtTargetFour)
		{
			Expectations expectations;
			for (const double rate : SWEEP_RATES)
			{
				for (const double ppm : COMMON_DRIFTS)
				{
					const DriftScenario scenario = CommonScenario(rate, ppm);
					const auto result = RunDrift(scenario);
					Report(scenario, result);
					if (ppm > 0.0)
					{
						ExpectCorrected(expectations, scenario, result);
						continue;
					}
					// A slower source drains at the dequeue wait: the display
					// shows a frame twice and the ledger counts it. With no
					// event the corrector never drops, as nothing but the
					// drift moved the chain, and the backstop never fires.
					expectations.Expect(result.staleAgeRecoveries +
						result.renderStallRecoveries == 0, scenario,
						L"the backstop fired on a slower source");
					expectations.Expect(
						result.dropsAtCut + result.dropsByLimit == 0, scenario,
						L"the corrector dropped for a slower source with no "
						L"event");
					expectations.Expect(result.emptyRefreshes >= 1, scenario,
						L"the display never ran out of frames by itself");
					CheckRequirements(expectations, scenario, result);
				}
			}
			expectations.AssertNone();
		}

		// Cut spacing sets how long a due drop waits; with no cut at all the
		// three-minute limit takes it, before the backstop.
		TEST_METHOD(CutSpacingSetsHowLongADueDropWaits)
		{
			const CutPattern patterns[] =
			{
				CutPattern::Every24Seconds, CutPattern::Every120Seconds,
				CutPattern::None
			};
			Expectations expectations;
			for (const CutPattern cuts : patterns)
			{
				DriftScenario scenario;
				scenario.cuts = cuts;
				scenario.minutes = 150.0;
				const auto result = RunDrift(scenario);
				Report(scenario, result);
				ExpectCorrected(expectations, scenario, result);
				if (cuts == CutPattern::None)
					expectations.Expect(result.dropsAtCut == 0 &&
						result.dropsByLimit >= 1, scenario,
						L"with no cut the limit did not take the drop");
			}
			expectations.AssertNone();
		}

		// A render as long as the backstop's stall threshold: it fires once,
		// flushes to the target and tells the policy what it flushed; the
		// corrector drops again after it.
		TEST_METHOD(RenderStallRecoversOnceThenCorrectionResumes)
		{
			Expectations expectations;
			for (int run = 0; run < 2; ++run)
			{
				DriftScenario scenario;
				if (run == 0)
					scenario.cuts = CutPattern::Film20261109;
				else
				{
					scenario.displayHz = 60000.0 / 1001.0;
					scenario.cuts = CutPattern::Every24Seconds;
					scenario.minutes = 100.0;
				}
				scenario.stallAtMinutes = 20.0;
				scenario.stallMs = AlphaQueuePolicy::RenderStallThresholdMs(
					scenario.displayHz * (1.0 + scenario.ppm / 1000000.0)) + 1.0;
				const auto result = RunDrift(scenario);
				Report(scenario, result);
				ExpectDropped(expectations, scenario, result);
				expectations.Expect(result.renderStallRecoveries == 1, scenario,
					L"the backstop did not fire exactly once on the stall");
				expectations.Expect(
					result.lastDropSeconds > scenario.stallAtMinutes * 60.0,
					scenario, L"the corrector never dropped after the stall");
				CheckRequirements(expectations, scenario, result);
			}
			expectations.AssertNone();
		}

		// At 59.94 Hz a render of two refreshes and 1 ms stays under the
		// backstop's 50 ms stall threshold: the display shows one frame twice,
		// and the chain holds a frame the forecast never counted. The ledger
		// counts the repeated refresh, which brings the drop due, and it is
		// taken at the next cut.
		TEST_METHOD(MissedRefreshSurplusIsDroppedAtTheNextCut)
		{
			DriftScenario scenario;
			scenario.displayHz = 60000.0 / 1001.0;
			scenario.cuts = CutPattern::Every24Seconds;
			scenario.minutes = 30.0;
			scenario.stallAtMinutes = 10.0;
			scenario.stallMs = 2000.0 / scenario.displayHz + 1.0;
			const auto result = RunDrift(scenario);
			Report(scenario, result);

			Expectations expectations;
			ExpectDropped(expectations, scenario, result);
			expectations.Expect(result.emptyRefreshes == 1, scenario,
				L"the stall did not skip exactly one refresh");
			expectations.Expect(result.staleAgeRecoveries +
				result.renderStallRecoveries == 0, scenario,
				L"the backstop fired");
			const double stallSeconds = scenario.stallAtMinutes * 60.0;
			expectations.Expect(result.firstDropSeconds > stallSeconds &&
				result.firstDropSeconds <= stallSeconds + 24.0 +
					120.0 / scenario.displayHz + 1.0, scenario,
				L"the surplus was not dropped at the next cut");
			CheckRequirements(expectations, scenario, result);
			expectations.AssertNone();
		}

		// At +2 ppm, and at +0.5 ppm below the forecast's minimum, drift is a
		// non-event on a 24 Hz display; one render of two periods and 1 ms at
		// 10 minutes skips a refresh, and that one extra frame is removed
		// exactly once.
		TEST_METHOD(LowDriftRenderStallAt24HzIsRemovedOnce)
		{
			const double drifts[] = { 2.0, 0.5 };
			Expectations expectations;
			for (const double ppm : drifts)
			{
				DriftScenario scenario = LowDrift24HzScenario(ppm);
				scenario.stallAtMinutes = 10.0;
				scenario.stallMs = 2000.0 / scenario.displayHz + 1.0;
				const auto result = RunDrift(scenario);
				Report(scenario, result);
				ExpectExtraFrameRemovedOnce(expectations, scenario, result,
					scenario.stallAtMinutes);
				CheckRequirements(expectations, scenario, result);
			}
			expectations.AssertNone();
		}

		// The same with the extra frame from the display side: one missed
		// vsync at 10 minutes, the frame rendered on time and shown a refresh
		// late.
		TEST_METHOD(LowDriftMissedVsyncAt24HzIsRemovedOnce)
		{
			const double drifts[] = { 2.0, 0.5 };
			Expectations expectations;
			for (const double ppm : drifts)
			{
				DriftScenario scenario = LowDrift24HzScenario(ppm);
				scenario.missedVsyncAtMinutes = 10.0;
				const auto result = RunDrift(scenario);
				Report(scenario, result);
				ExpectExtraFrameRemovedOnce(expectations, scenario, result,
					scenario.missedVsyncAtMinutes);
				CheckRequirements(expectations, scenario, result);
			}
			expectations.AssertNone();
		}

		// A slower source once its display has run out of frames by itself,
		// so the lowest G so far is the chain's floor; then one event (a
		// missed vsync, or a stall of two periods + 1 ms, 5 or 12 periods) at
		// 8 points across the next frame of drift, at every common rate,
		// -14.25 and -150 ppm, jitter 0 and 3 ms, 1 and 2 present slots,
		// cuts every 24 s and none, run to 10 minutes past the next time the
		// display runs dry. Each run meets R1-R6: what the event left a whole
		// frame above the floor is dropped at the cuts or by the limit, and
		// nothing the drift brought.
		TEST_METHOD(SlowerSourceEventAfterTheDisplayRanDry)
		{
			Expectations expectations;
			const std::vector<SweepRun> runs = RanDryRuns(0.0, expectations);
			std::vector<DriftResult> results(runs.size());
			RunAll(runs, results);

			size_t failedRuns = 0;
			size_t tracedRuns = 0;
			size_t failed[SWEEP_EVENT_COUNT][2] = {};
			for (size_t index = 0; index < runs.size(); ++index)
			{
				const SweepRun& run = runs[index];
				const size_t before = expectations.count;
				ExpectRequirements(expectations, run.scenario, results[index]);
				if (expectations.count == before)
					continue;
				if (++failed[run.event][run.drift] == 1 &&
					tracedRuns < SWEEP_TRACED_RUNS)
				{
					++tracedRuns;
					Report(run.scenario, results[index]);
					PrintTrace(run.scenario, results[index]);
				}
				++failedRuns;
			}
			wchar_t line[256];
			swprintf_s(line, L"%zu runs after the display ran dry; %zu failed "
				L"(-14.25 ppm / -150 ppm): missed vsync %zu / %zu, stall 2 "
				L"periods + 1 ms %zu / %zu, 5 periods %zu / %zu, 12 periods "
				L"%zu / %zu\n", runs.size(), failedRuns,
				failed[1][0], failed[1][1], failed[2][0], failed[2][1],
				failed[3][0], failed[3][1], failed[4][0], failed[4][1]);
			Logger::WriteMessage(line);
			expectations.AssertNone();
		}

		// 7.6: every display rate, drift, start block, event, event phase,
		// jitter and present-slot count, each run checked against R1-R6.
		BEGIN_TEST_METHOD_ATTRIBUTE(SweepMeetsEveryRequirement)
			TEST_METHOD_ATTRIBUTE(L"TestCategory", L"CadenceSweep")
		END_TEST_METHOD_ATTRIBUTE()
		TEST_METHOD(SweepMeetsEveryRequirement)
		{
			const std::vector<SweepRun> runs = SweepRuns();
			std::vector<DriftResult> results(runs.size());
			const auto started = std::chrono::steady_clock::now();
			const unsigned threads = RunAll(runs, results);
			const double elapsedSeconds = std::chrono::duration<double>(
				std::chrono::steady_clock::now() - started).count();

			Expectations expectations;
			size_t failedRuns = 0;
			size_t tracedRuns = 0;
			size_t failed[SWEEP_EVENT_COUNT][SWEEP_DRIFT_COUNT] = {};
			for (size_t index = 0; index < runs.size(); ++index)
			{
				const SweepRun& run = runs[index];
				const size_t before = expectations.count;
				ExpectRequirements(expectations, run.scenario, results[index]);
				if (expectations.count == before)
					continue;
				// The first failing run of each event and drift is traced.
				if (++failed[run.event][run.drift] == 1 &&
					tracedRuns < SWEEP_TRACED_RUNS)
				{
					++tracedRuns;
					Report(run.scenario, results[index]);
					PrintTrace(run.scenario, results[index]);
				}
				++failedRuns;
			}

			wchar_t line[256];
			swprintf_s(line,
				L"sweep: %zu runs on %u threads in %.1f s; %zu failed. Failed "
				L"runs by event and ppm:\n", runs.size(), threads,
				elapsedSeconds, failedRuns);
			std::wstring summary = line;
			summary += L"  event                       ";
			for (const double ppm : SWEEP_DRIFTS)
			{
				swprintf_s(line, L" %+7.2f", ppm);
				summary += line;
			}
			summary += L"\n";
			for (int event = 0; event < SWEEP_EVENT_COUNT; ++event)
			{
				swprintf_s(line, L"  %-28s", SWEEP_EVENTS[event]);
				summary += line;
				for (int drift = 0; drift < SWEEP_DRIFT_COUNT; ++drift)
				{
					swprintf_s(line, L" %7zu", failed[event][drift]);
					summary += line;
				}
				summary += L"\n";
			}
			Logger::WriteMessage(summary.c_str());
			expectations.AssertNone();
		}

		// A measured capture rate carries an error. The policy is given the
		// capture rate with a fixed error of +-0.1, +-0.2 and +-0.5 ppm while
		// the chain keeps the true rate, so the forecast moves the phase a
		// little faster or slower than the chain drifts: every run of the
		// sweep, of SlowerSourceEventAfterTheDisplayRanDry and of
		// CommonRatesAndDriftsAtTargetFour, at each error. The table counts,
		// per error, set and drift, the runs that failed each requirement and
		// the drops the ledger counted premature. R1-R6 must hold up to the
		// error a slower source's floor is made for (the policy's
		// SLOWER_FLOOR_RATE_ERROR_PPM, 0.25); +-0.5 is reported, not held.
		BEGIN_TEST_METHOD_ATTRIBUTE(RateErrorMeetsEveryRequirement)
			TEST_METHOD_ATTRIBUTE(L"TestCategory", L"CadenceSweep")
		END_TEST_METHOD_ATTRIBUTE()
		TEST_METHOD(RateErrorMeetsEveryRequirement)
		{
			const double errors[] = { -0.5, -0.2, -0.1, 0.1, 0.2, 0.5 };
			constexpr double HELD_RATE_ERROR_PPM = 0.25;
			constexpr int SET_COUNT = 3;
			const wchar_t* const sets[SET_COUNT] =
			{
				L"sweep", L"ran dry", L"common"
			};
			struct Row
			{
				int set = 0;
				double ppm = 0.0;
				size_t runs = 0;
				size_t failed = 0;
				size_t rules[REQUIREMENT_COUNT] = {};
				uint64_t premature = 0;
			};
			Expectations expectations;
			const std::vector<SweepRun> sweep = SweepRuns();
			size_t tracedRuns = 0;
			size_t totalRuns = 0;
			const auto started = std::chrono::steady_clock::now();
			std::wstring summary;
			wchar_t line[256];
			for (const double error : errors)
			{
				std::vector<SweepRun> runs;
				std::vector<int> setOf;
				for (SweepRun run : sweep)
				{
					run.scenario.rateErrorPpm = error;
					runs.push_back(run);
					setOf.push_back(0);
				}
				for (const SweepRun& run : RanDryRuns(error, expectations))
				{
					runs.push_back(run);
					setOf.push_back(1);
				}
				for (const double rate : SWEEP_RATES)
				{
					for (const double ppm : COMMON_DRIFTS)
					{
						SweepRun run{ CommonScenario(rate, ppm), 0, 0 };
						run.scenario.rateErrorPpm = error;
						runs.push_back(run);
						setOf.push_back(2);
					}
				}
				std::vector<DriftResult> results(runs.size());
				RunAll(runs, results);
				totalRuns += runs.size();
				const bool held = std::abs(error) <= HELD_RATE_ERROR_PPM;

				std::vector<Row> rows;
				size_t failedRuns = 0;
				bool reportedSet[SET_COUNT] = {};
				for (size_t index = 0; index < runs.size(); ++index)
				{
					const DriftScenario& scenario = runs[index].scenario;
					const DriftResult& result = results[index];
					const int set = setOf[index];
					auto row = std::find_if(rows.begin(), rows.end(),
						[&](const Row& candidate)
						{
							return candidate.set == set &&
								candidate.ppm == scenario.ppm;
						});
					if (row == rows.end())
					{
						Row added;
						added.set = set;
						added.ppm = scenario.ppm;
						rows.push_back(added);
						row = rows.end() - 1;
					}
					++row->runs;
					row->premature += result.ledgerPrematureDrops;
					const unsigned failed = FailedRequirements(scenario, result);
					if (failed == 0)
						continue;
					++row->failed;
					++failedRuns;
					for (int rule = 0; rule < REQUIREMENT_COUNT; ++rule)
					{
						if ((failed & (1u << rule)) != 0)
							++row->rules[rule];
					}
					if (held)
						ExpectRequirements(expectations, scenario, result);
					// The first failing run of each set at each error is
					// reported; the first few are traced.
					if (!reportedSet[set])
					{
						reportedSet[set] = true;
						Report(scenario, result);
						if (tracedRuns < SWEEP_TRACED_RUNS)
						{
							++tracedRuns;
							PrintTrace(scenario, result);
						}
					}
				}
				swprintf_s(line,
					L"rate error %+.2f ppm (%s): %zu runs, %zu failed\n"
					L"  set         ppm    runs  failed    R1    R2    R3    R4"
					L"    R5    R6  premature\n",
					error, held ? L"held" : L"reported", runs.size(),
					failedRuns);
				summary += line;
				for (const Row& row : rows)
				{
					swprintf_s(line,
						L"  %-7s %+8.2f %7zu %7zu %5zu %5zu %5zu %5zu %5zu %5zu "
						L"%10llu\n",
						sets[row.set], row.ppm, row.runs, row.failed,
						row.rules[0], row.rules[1], row.rules[2], row.rules[3],
						row.rules[4], row.rules[5],
						static_cast<unsigned long long>(row.premature));
					summary += line;
				}
			}
			const double elapsedSeconds = std::chrono::duration<double>(
				std::chrono::steady_clock::now() - started).count();
			swprintf_s(line, L"rate error: %zu runs in %.1f s\n", totalRuns,
				elapsedSeconds);
			Logger::WriteMessage((line + summary).c_str());
			expectations.AssertNone();
		}
	};
}
