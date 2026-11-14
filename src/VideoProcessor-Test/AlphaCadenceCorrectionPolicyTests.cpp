#include "pch.h"
#include "CppUnitTest.h"

#include <vprenderer/AlphaCadenceCorrectionPolicy.h>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace Tests
{
	TEST_CLASS(AlphaCadenceCorrectionPolicyTests)
	{
	public:
		static AlphaCadenceCorrectionInput Input(double captureRate)
		{
			AlphaCadenceCorrectionInput input;
			input.enabled = true;
			input.generation = 1;
			input.presentationEvidence = AlphaPresentationEvidence::Stable;
			input.captureRateHz = captureRate;
			input.displayRateHz = 60.0;
			input.desiredQueueDepth = 4;
			input.queueDepth = 5;
			input.presentationDebt = 1;
			return input;
		}

		static AlphaCadenceCorrectionDecision Advance(
			AlphaCadenceCorrectionPolicy& policy,
			AlphaCadenceCorrectionInput input,
			uint32_t frames)
		{
			AlphaCadenceCorrectionDecision decision;
			for (uint32_t frame = 0; frame < frames; ++frame)
				decision = policy.Evaluate(input);
			return decision;
		}

		static AlphaCadenceCorrectionDecision AdvanceUntilDue(
			AlphaCadenceCorrectionPolicy& policy,
			AlphaCadenceCorrectionInput input,
			uint32_t maximumFrames = 20000)
		{
			for (uint32_t frame = 0; frame < maximumFrames; ++frame)
			{
				const auto decision = policy.Evaluate(input);
				if (decision.due)
					return decision;
			}
			Assert::Fail(L"Cadence correction did not become due");
			return {};
		}

		static int Reason(AlphaCadenceBlockReason reason)
		{
			return static_cast<int>(reason);
		}

		TEST_METHOD(InvalidPresentationEvidenceAlwaysFailsClosed)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = Input(60.012);
			input.presentationEvidence = AlphaPresentationEvidence::Disjoint;
			input.safeSceneBoundary = true;
			input.sceneEventId = 1;

			const auto decision = Advance(policy, input, 10000);
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::None),
				static_cast<int>(decision.action));
			Assert::IsFalse(decision.planned);
			Assert::AreEqual(
				Reason(AlphaCadenceBlockReason::PresentationEvidenceUnavailable),
				Reason(decision.blockReason));
		}

		TEST_METHOD(IncompatibleRatesCannotPlanOrAct)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = Input(61.0);
			input.safeSceneBoundary = true;
			input.sceneEventId = 1;

			const auto decision = Advance(policy, input, 10000);
			Assert::IsFalse(decision.ratesCompatible);
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::None),
				static_cast<int>(decision.action));
		}

		TEST_METHOD(Capture23976To60HzMonitorNeverBecomesPpmOrQueueCorrection)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = Input(24000.0 / 1001.0);
			input.safeSceneBoundary = true;
			input.sceneEventId = 1;
			input.queueDepth = 8;
			input.desiredQueueDepth = 8;

			const auto decision = Advance(policy, input, 10000);
			Assert::IsFalse(decision.ratesCompatible);
			Assert::IsFalse(decision.predictionValid);
			Assert::IsFalse(decision.planned);
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::None),
				static_cast<int>(decision.action));
			Assert::AreEqual(Reason(AlphaCadenceBlockReason::RateMismatchTooLarge),
				Reason(decision.blockReason));
		}

		TEST_METHOD(PublishesLongRangePredictionBeforePlanningWindow)
		{
			AlphaCadenceCorrectionPolicy policy;
			const auto decision = Advance(policy, Input(60.012), 601);

			Assert::IsTrue(decision.predictionValid);
			Assert::IsFalse(decision.planned);
			Assert::AreEqual(
				static_cast<int>(AlphaCadenceTimingStatus::Forecasting),
				static_cast<int>(decision.timingStatus));
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::Drop),
				static_cast<int>(decision.predictedAction));
			Assert::IsTrue(decision.secondsUntilCorrection > 70.0);
			Assert::IsTrue(decision.secondsUntilCorrection < 80.0);
			Assert::IsTrue(decision.secondsUntilPlan > 50.0);
			Assert::IsTrue(decision.secondsUntilPlan < 60.0);
			Assert::IsTrue(decision.secondsUntilPlan <
				decision.secondsUntilCorrection);
		}

		TEST_METHOD(ReportsMatchedWhenStableRatesNeedNoCorrection)
		{
			AlphaCadenceCorrectionPolicy policy;
			const auto decision = Advance(policy, Input(60.0), 601);

			Assert::IsTrue(decision.ratesCompatible);
			Assert::IsFalse(decision.predictionValid);
			Assert::AreEqual(
				static_cast<int>(AlphaCadenceTimingStatus::Matched),
				static_cast<int>(decision.timingStatus));
		}

		TEST_METHOD(NearZeroNoiseCannotReversePredictionDirection)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = Input(60.0 * (1.0 + 5e-6));
			auto decision = Advance(policy, input, 601);
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::Drop),
				static_cast<int>(decision.predictedAction));

			for (uint32_t frame = 0; frame < 1200; ++frame)
			{
				const double ppm = frame % 2 == 0 ? -20.0 : 20.0;
				input.captureRateHz = 60.0 * (1.0 + ppm / 1000000.0);
				decision = policy.Evaluate(input);
				Assert::IsFalse(
					decision.predictedAction == AlphaCadenceAction::Repeat);
			}
		}

		TEST_METHOD(DropRequiresPhaseQueueDebtAndSceneAgreement)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = Input(60.012);
			Advance(policy, input, 5120);
			input.safeSceneBoundary = true;
			input.sceneEventId = 7;
			const auto decision = policy.Evaluate(input);

			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::Drop),
				static_cast<int>(decision.action));
			Assert::AreEqual(static_cast<uint64_t>(7), decision.sceneEventId);
			Assert::IsTrue(decision.verificationPending);
		}

		TEST_METHOD(RepeatRequiresLowQueueAndNoPresentationDebt)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = Input(59.988);
			input.queueDepth = 3;
			input.presentationDebt = 0;
			Advance(policy, input, 5120);
			input.safeSceneBoundary = true;
			input.sceneEventId = 9;
			const auto decision = policy.Evaluate(input);

			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::Repeat),
				static_cast<int>(decision.action));
		}

		TEST_METHOD(RestingPostPopDepthDoesNotBlockDueDrop)
		{
			// Depth 3 under a target of 4 is the loop's resting post-pop depth;
			// at 1:1 a surplus source shows there last, so it cannot veto a due
			// drop.
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = Input(60.012);
			input.queueDepth = 3;
			Advance(policy, input, 5200);
			input.safeSceneBoundary = true;
			input.sceneEventId = 3;

			const auto decision = policy.Evaluate(input);
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::Drop),
				static_cast<int>(decision.action));
			Assert::AreEqual(static_cast<uint64_t>(3), decision.sceneEventId);
		}

		TEST_METHOD(DueForecastPublishesSignedOverdueTime)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = Input(59.988);
			input.queueDepth = input.desiredQueueDepth;
			input.presentationDebt = 0;

			const auto due = AdvanceUntilDue(policy, input);
			Assert::IsTrue(due.predictionValid);
			Assert::IsTrue(due.due);
			Assert::IsTrue(due.secondsUntilCorrection <= 0.0);
			Assert::AreEqual(
				Reason(AlphaCadenceBlockReason::RepeatQueueNotBelowDesired),
				Reason(due.blockReason));

			const auto overdue = Advance(policy, input, 60);
			Assert::IsTrue(overdue.due);
			Assert::IsTrue(overdue.secondsUntilCorrection < 0.0);
		}

		TEST_METHOD(DueRepeatDistinguishesQueueAndDebtBlockers)
		{
			AlphaCadenceCorrectionPolicy queuePolicy;
			AlphaCadenceCorrectionInput queueInput = Input(59.988);
			queueInput.queueDepth = queueInput.desiredQueueDepth;
			queueInput.presentationDebt = 0;
			const auto queueBlocked =
				AdvanceUntilDue(queuePolicy, queueInput);
			Assert::AreEqual(
				Reason(AlphaCadenceBlockReason::RepeatQueueNotBelowDesired),
				Reason(queueBlocked.blockReason));

			AlphaCadenceCorrectionPolicy debtPolicy;
			AlphaCadenceCorrectionInput debtInput = Input(59.988);
			debtInput.queueDepth = debtInput.desiredQueueDepth - 1;
			debtInput.presentationDebt = 1;
			const auto debtBlocked = AdvanceUntilDue(debtPolicy, debtInput);
			Assert::AreEqual(
				Reason(AlphaCadenceBlockReason::RepeatPresentationDebtPresent),
				Reason(debtBlocked.blockReason));
			Assert::AreEqual(
				debtInput.queueDepth,
				debtBlocked.diagnostic.queueDepth);
			Assert::AreEqual(
				debtInput.desiredQueueDepth,
				debtBlocked.diagnostic.desiredQueueDepth);
			Assert::AreEqual(
				debtInput.presentationDebt,
				debtBlocked.diagnostic.presentationDebt);
			Assert::IsTrue(
				debtBlocked.diagnostic.fallbackMature);
			Assert::IsTrue(
				debtBlocked.diagnostic.fallbackEligible);
		}

		TEST_METHOD(DueDropDistinguishesDebtAndTimeLimitBlockers)
		{
			AlphaCadenceCorrectionPolicy debtPolicy;
			AlphaCadenceCorrectionInput debtInput = Input(60.012);
			debtInput.presentationDebt = 0;
			const auto debtBlocked = AdvanceUntilDue(debtPolicy, debtInput);
			Assert::AreEqual(
				Reason(AlphaCadenceBlockReason::DropPresentationDebtMissing),
				Reason(debtBlocked.blockReason));

			// Due with no fresh scene: the drop waits, then is taken anyway
			// once it has been due for three minutes of display frames.
			const uint32_t limitFrames = 10800; // 180 s at 60 Hz
			AlphaCadenceCorrectionPolicy limitPolicy;
			AlphaCadenceCorrectionInput limitInput = Input(60.012);
			const auto waiting = AdvanceUntilDue(limitPolicy, limitInput);
			Assert::AreEqual(
				Reason(AlphaCadenceBlockReason::WaitingForFreshScene),
				Reason(waiting.blockReason));
			const auto beforeLimit =
				Advance(limitPolicy, limitInput, limitFrames - 2);
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::None),
				static_cast<int>(beforeLimit.action));
			Assert::AreEqual(
				Reason(AlphaCadenceBlockReason::WaitingForFreshScene),
				Reason(beforeLimit.blockReason));
			const auto atLimit = limitPolicy.Evaluate(limitInput);
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::Drop),
				static_cast<int>(atLimit.action));
			Assert::IsTrue(atLimit.deadlineFallback);
		}

		TEST_METHOD(DueRepeatUsesBoundedNoSceneFallback)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = Input(59.988);
			input.queueDepth = input.desiredQueueDepth - 1;
			input.presentationDebt = 0;

			const auto decision = AdvanceUntilDue(policy, input);
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::Repeat),
				static_cast<int>(decision.action));
			Assert::IsTrue(decision.deadlineFallback);
			Assert::AreEqual(Reason(AlphaCadenceBlockReason::None),
				Reason(decision.blockReason));
		}

		TEST_METHOD(GenerationReplacementClearsPendingAction)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = Input(60.012);
			Advance(policy, input, 5120);
			input.safeSceneBoundary = true;
			input.sceneEventId = 1;
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::Drop),
				static_cast<int>(policy.Evaluate(input).action));

			input.generation = 2;
			const auto decision = policy.Evaluate(input);
			Assert::IsFalse(decision.verificationPending);
			Assert::AreEqual(0.0, decision.phaseFrames);
		}

		TEST_METHOD(VerificationRequiresDebtToChangeByExactlyOne)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = Input(60.012);
			Advance(policy, input, 5120);
			input.safeSceneBoundary = true;
			input.sceneEventId = 1;
			input.lastPresentId = 10;
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::Drop),
				static_cast<int>(policy.Evaluate(input).action));

			input.safeSceneBoundary = false;
			input.lastPresentId = 11;
			input.presentationDebt = 1;
			const auto ambiguous = policy.Evaluate(input);
			Assert::IsTrue(ambiguous.verificationCompleted);
			Assert::AreEqual(
				static_cast<int>(AlphaCadenceAction::Drop),
				static_cast<int>(ambiguous.verificationAction));
			Assert::IsFalse(ambiguous.lastVerificationSucceeded);
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::None),
				static_cast<int>(ambiguous.action));
		}

		TEST_METHOD(OneSurplusSourceDropsAtFreshScene)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = Input(60.012);
			input.queueDepth = input.desiredQueueDepth;
			input.presentationDebt = 1;
			const auto due = AdvanceUntilDue(policy, input);
			Assert::AreNotEqual(
				Reason(AlphaCadenceBlockReason::DropQueueNotAboveDesired),
				Reason(due.blockReason));

			input.safeSceneBoundary = true;
			input.sceneEventId = 5;
			const auto decision = policy.Evaluate(input);
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::Drop),
				static_cast<int>(decision.action));
			Assert::AreEqual(static_cast<uint64_t>(5), decision.sceneEventId);
		}

		TEST_METHOD(HeldSurplusBringsSlowForecastDue)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = Input(60.0 * (1.0 + 14.6e-6));
			input.queueDepth = input.desiredQueueDepth;

			const auto due = AdvanceUntilDue(policy, input, 720);
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::Drop),
				static_cast<int>(due.predictedAction));
			Assert::IsTrue(due.phaseFrames >= 1.0);
		}

		TEST_METHOD(RestingQueueAtZeroPpmNeverBecomesDue)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = Input(60.0);
			input.queueDepth = input.desiredQueueDepth - 1;
			input.safeSceneBoundary = true;

			for (uint32_t frame = 0; frame < 10000; ++frame)
			{
				input.sceneEventId = frame + 1;
				const auto decision = Step(policy, input);
				Assert::IsFalse(decision.due);
				Assert::AreEqual(static_cast<int>(AlphaCadenceAction::None),
					static_cast<int>(decision.action));
			}
		}

		TEST_METHOD(SurplusLeftAfterBacklogRecoveryBecomesDueAgain)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = Input(60.0 * (1.0 + 14.6e-6));
			input.queueDepth = input.desiredQueueDepth;
			AdvanceUntilDue(policy, input, 720);
			input.safeSceneBoundary = true;
			input.sceneEventId = 1;
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::Drop),
				static_cast<int>(policy.Evaluate(input).action));

			// The recovery flushed nothing and the surplus is still queued: the
			// rates are kept, so the held depth brings the drop due again 120
			// evaluations past the stabilize gate.
			policy.OnBacklogRecovery(0);
			input.safeSceneBoundary = false;
			input.sceneEventId = 0;
			const auto due = AdvanceUntilDue(policy, input, 120 + 120);
			Assert::AreEqual(
				Reason(AlphaCadenceBlockReason::WaitingForFreshScene),
				Reason(due.blockReason));

			input.safeSceneBoundary = true;
			input.sceneEventId = 2;
			Assert::AreEqual(static_cast<int>(AlphaCadenceAction::Drop),
				static_cast<int>(policy.Evaluate(input).action));
		}

		// A source at rest: the post-pop depth at target - 1 and the head two
		// source periods old, so only the forecast and the ledger move the phase.
		static AlphaCadenceCorrectionInput RestingInput(double ppm)
		{
			AlphaCadenceCorrectionInput input =
				Input(60.0 * (1.0 + ppm / 1000000.0));
			input.queueDepth = input.desiredQueueDepth - 1;
			input.oldestQueuedAgeMs = 2000.0 / input.captureRateHz;
			return input;
		}

		// The phase the policy adds per evaluation, computed as it does.
		static double Drift(const AlphaCadenceCorrectionInput& input)
		{
			return input.captureRateHz / input.displayRateHz - 1.0;
		}

		static int Action(AlphaCadenceAction action)
		{
			return static_cast<int>(action);
		}

		// One evaluation with a DXGI sample that moved by `presents` and
		// `refreshes` (unsigned, so a negative step wraps as the counters do).
		static AlphaCadenceCorrectionDecision Sample(
			AlphaCadenceCorrectionPolicy& policy,
			AlphaCadenceCorrectionInput& input,
			uint32_t presents,
			uint32_t refreshes)
		{
			input.dxgiSampleValid = true;
			input.dxgiPresentCount += presents;
			input.dxgiPresentRefreshCount += refreshes;
			return policy.Evaluate(input);
		}

		// One present shown at one refresh, after `repeated` refreshes that
		// showed the previous frame again.
		static AlphaCadenceCorrectionDecision Step(
			AlphaCadenceCorrectionPolicy& policy,
			AlphaCadenceCorrectionInput& input,
			uint32_t repeated = 0)
		{
			return Sample(policy, input, 1, 1 + repeated);
		}

		static AlphaCadenceCorrectionDecision Steps(
			AlphaCadenceCorrectionPolicy& policy,
			AlphaCadenceCorrectionInput& input,
			uint32_t frames)
		{
			AlphaCadenceCorrectionDecision decision;
			for (uint32_t frame = 0; frame < frames; ++frame)
				decision = Step(policy, input);
			return decision;
		}

		// At 0 ppm: one repeated refresh makes the drop due, and a fresh scene
		// takes it. The phase is then 0.
		static void DropAtScene(
			AlphaCadenceCorrectionPolicy& policy,
			AlphaCadenceCorrectionInput& input)
		{
			Steps(policy, input, 120 + 10);
			Assert::IsTrue(Step(policy, input, 1).due);
			input.safeSceneBoundary = true;
			input.sceneEventId = 1;
			const auto drop = Step(policy, input);
			Assert::AreEqual(Action(AlphaCadenceAction::Drop),
				Action(drop.action));
			Assert::AreEqual(0.0, drop.phaseFrames);
			input.safeSceneBoundary = false;
			input.sceneEventId = 0;
		}

		// A refresh that showed the previous frame again is one more frame in
		// the chain: the phase rises by one at once, and the drop is taken at
		// the next fresh scene.
		static void ExpectRepeatedRefreshBringsDropDue(double ppm,
			uint32_t framesToPhaseOneTenth)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = RestingInput(ppm);
			auto decision = Steps(policy, input, 120 + framesToPhaseOneTenth);
			Assert::IsFalse(decision.due);
			const double before = decision.phaseFrames;
			Assert::IsTrue(before > 0.099 && before < 0.101);

			decision = Step(policy, input, 1);
			Assert::IsTrue(decision.due);
			Assert::AreEqual(before + 1.0 + Drift(input),
				decision.phaseFrames, 1e-9);
			Assert::AreEqual(static_cast<uint64_t>(1),
				decision.diagnostic.repeatedRefreshes);

			input.safeSceneBoundary = true;
			input.sceneEventId = 1;
			decision = Step(policy, input);
			Assert::AreEqual(Action(AlphaCadenceAction::Drop),
				Action(decision.action));
			Assert::AreEqual(static_cast<uint64_t>(1), decision.sceneEventId);
		}

		TEST_METHOD(RepeatedRefreshBringsDropDue)
		{
			ExpectRepeatedRefreshBringsDropDue(14.25, 7018);
		}

		TEST_METHOD(RepeatedRefreshBringsDropDueBelowMinimumPpm)
		{
			// Below MINIMUM_PREDICTION_PPM the forecast predicts nothing; the
			// ledger still counts the whole frame.
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = RestingInput(0.5);
			const auto decision = Steps(policy, input, 120 + 1000);
			Assert::AreEqual(Action(AlphaCadenceAction::None),
				Action(decision.predictedAction));
			ExpectRepeatedRefreshBringsDropDue(0.5, 200000);
		}

		TEST_METHOD(FlushAndRepeatedRefreshesNetZero)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = RestingInput(14.25);
			const double drift = Drift(input);
			const double before = Steps(policy, input, 120 + 1000).phaseFrames;

			Step(policy, input, 3);
			policy.OnBacklogRecovery(3);
			auto decision = Steps(policy, input, 120);
			Assert::AreEqual(
				Reason(AlphaCadenceBlockReason::StabilizingRates),
				Reason(decision.blockReason));
			// The phase also carries the drift of the 120 gate evaluations.
			decision = Step(policy, input);
			Assert::AreEqual(before + 122.0 * drift, decision.phaseFrames, 1e-9);
			Assert::AreEqual(static_cast<uint64_t>(3),
				decision.diagnostic.repeatedRefreshes);
			Assert::AreEqual(static_cast<uint64_t>(3),
				decision.diagnostic.framesFlushed);
			Assert::AreEqual(static_cast<int64_t>(0),
				decision.diagnostic.referenceShiftFrames);
		}

		TEST_METHOD(OverFlushMovesReferenceDown)
		{
			// The flush took two frames the chain did not gain: the reference
			// moves down by two, the fraction is kept, and nothing asks for a
			// repeat against the faster source.
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = RestingInput(14.25);
			const double drift = Drift(input);
			const double before = Steps(policy, input, 120 + 42106).phaseFrames;
			Assert::IsTrue(before > 0.599 && before < 0.601);

			policy.OnBacklogRecovery(2);
			for (uint32_t frame = 0; frame < 120 + 1 + 2000; ++frame)
			{
				const auto decision = Step(policy, input);
				Assert::AreNotEqual(Action(AlphaCadenceAction::Repeat),
					Action(decision.action));
				Assert::AreNotEqual(Action(AlphaCadenceAction::Repeat),
					Action(decision.predictedAction));
				Assert::IsFalse(decision.due);
				if (frame == 120)
				{
					Assert::AreEqual(before + 121.0 * drift, decision.phaseFrames,
						1e-9);
					Assert::AreEqual(static_cast<int64_t>(2),
						decision.diagnostic.referenceShiftFrames);
					Assert::AreEqual(static_cast<uint64_t>(2),
						decision.diagnostic.framesFlushed);
				}
			}
		}

		TEST_METHOD(BacklogRecoveryKeepsRates)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = RestingInput(14.25);
			const double drift = Drift(input);
			const auto settled = Steps(policy, input, 120 + 1000);
			Assert::AreEqual(static_cast<uint32_t>(600),
				settled.rateFilterSamples);
			Assert::AreEqual(Action(AlphaCadenceAction::Drop),
				Action(settled.predictedAction));

			// The backstop resets the telemetry: no display rate and no
			// evidence until it has measured the display again.
			policy.OnBacklogRecovery(1);
			Unmeasured(policy, input, 12);
			for (uint32_t frame = 0; frame < 120; ++frame)
			{
				const auto decision = Step(policy, input);
				Assert::AreEqual(
					Reason(AlphaCadenceBlockReason::StabilizingRates),
					Reason(decision.blockReason));
				Assert::AreEqual(static_cast<uint32_t>(600),
					decision.rateFilterSamples);
				Assert::AreEqual(settled.filteredMismatchPpm,
					decision.filteredMismatchPpm, 1e-9);
			}
			const auto decision = Step(policy, input);
			Assert::AreEqual(
				static_cast<int>(AlphaCadenceTimingStatus::Forecasting),
				static_cast<int>(decision.timingStatus));
			Assert::AreEqual(Action(AlphaCadenceAction::Drop),
				Action(decision.predictedAction));
			// The 12 unmeasured and 120 gate evaluations' drift is added at
			// the wrap, with the one-frame shift.
			Assert::AreEqual(settled.phaseFrames + 133.0 * drift,
				decision.phaseFrames, 1e-9);
			Assert::AreEqual(static_cast<int64_t>(1),
				decision.diagnostic.referenceShiftFrames);
		}

		// A rate mismatch too large inside a recovery: the corrector cannot
		// act, but the chain drifts on, so those evaluations' drift is added at
		// the wrap with the others.
		TEST_METHOD(RateMismatchInsideRecoveryDriftsAtTheWrap)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = RestingInput(14.25);
			const double before = Steps(policy, input, 120 + 1000).phaseFrames;

			policy.OnBacklogRecovery(1);
			uint32_t evaluations = 12;
			Unmeasured(policy, input, evaluations);
			// 61 Hz against 60 is 16667 ppm: the filter passes 200 ppm within
			// a few evaluations, and comes back under it some time after the
			// rate does.
			const double trueRate = input.captureRateHz;
			input.captureRateHz = 61.0;
			uint32_t mismatched = 0;
			AlphaCadenceCorrectionDecision decision;
			for (uint32_t frame = 0; frame < 20; ++frame)
			{
				decision = Step(policy, input);
				++evaluations;
				if (decision.blockReason ==
					AlphaCadenceBlockReason::RateMismatchTooLarge)
					++mismatched;
			}
			Assert::IsTrue(mismatched > 0);
			input.captureRateHz = trueRate;
			for (;;)
			{
				decision = Step(policy, input);
				if (decision.blockReason !=
						AlphaCadenceBlockReason::RateMismatchTooLarge &&
					decision.blockReason !=
						AlphaCadenceBlockReason::StabilizingRates)
					break;
				++evaluations;
				Assert::IsTrue(evaluations < 2000);
			}
			// Every evaluation from the backstop to the wrap drifted, at the
			// rate the filter holds at the wrap, and the wrap advances once
			// more itself.
			const double drift =
				decision.diagnostic.filteredMismatchPpm / 1000000.0;
			Assert::AreEqual(before - 1.0 +
					static_cast<double>(evaluations + 1) * drift +
					static_cast<double>(
						decision.diagnostic.referenceShiftFrames),
				decision.phaseFrames, 1e-9);
		}

		// `frames` evaluations as the telemetry reports after a reset: evidence
		// Unavailable and no display rate, one present shown per refresh, the
		// first one after `repeated` refreshes that showed the previous frame
		// again. The DXGI counters carry on in `input`.
		static AlphaCadenceCorrectionDecision Unmeasured(
			AlphaCadenceCorrectionPolicy& policy,
			AlphaCadenceCorrectionInput& input,
			uint32_t frames,
			uint32_t repeated = 0)
		{
			AlphaCadenceCorrectionInput unmeasured = input;
			unmeasured.presentationEvidence =
				AlphaPresentationEvidence::Unavailable;
			unmeasured.displayRateHz = 0.0;
			AlphaCadenceCorrectionDecision decision;
			for (uint32_t frame = 0; frame < frames; ++frame)
			{
				decision = Step(policy, unmeasured, frame == 0 ? repeated : 0);
				Assert::AreEqual(
					Reason(AlphaCadenceBlockReason::PresentationEvidenceUnavailable),
					Reason(decision.blockReason));
			}
			input.dxgiSampleValid = unmeasured.dxgiSampleValid;
			input.dxgiPresentCount = unmeasured.dxgiPresentCount;
			input.dxgiPresentRefreshCount = unmeasured.dxgiPresentRefreshCount;
			return decision;
		}

		static AlphaCadenceCorrectionInput RestingInputAt23976(double ppm)
		{
			AlphaCadenceCorrectionInput input = RestingInput(ppm);
			input.displayRateHz = 24000.0 / 1001.0;
			input.captureRateHz = input.displayRateHz * (1.0 + ppm / 1000000.0);
			input.oldestQueuedAgeMs = 2000.0 / input.captureRateHz;
			return input;
		}

		TEST_METHOD(RepeatedRefreshesInsideRecoveryCount)
		{
			// The stall's repeated refreshes reach the policy after the
			// backstop, while the telemetry reports no display rate: they are
			// counted against the last rate measured.
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = RestingInputAt23976(14.25);
			const double drift = Drift(input);
			const double before = Steps(policy, input, 120 + 1000).phaseFrames;

			policy.OnBacklogRecovery(1);
			auto decision = Unmeasured(policy, input, 12, 2);
			Assert::AreEqual(static_cast<uint64_t>(2),
				decision.diagnostic.repeatedRefreshes);
			Assert::AreEqual(static_cast<uint64_t>(0),
				decision.diagnostic.dxgiRebases);
			Steps(policy, input, 120);
			decision = Step(policy, input);
			Assert::AreEqual(before - 1.0 + 2.0 + 133.0 * drift,
				decision.phaseFrames, 1e-9);
			Assert::AreEqual(static_cast<int64_t>(0),
				decision.diagnostic.referenceShiftFrames);
		}

		TEST_METHOD(StepAboveSavedLimitReBasesInsideRecovery)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = RestingInput(14.25);
			const double before = Steps(policy, input, 120 + 1000).phaseFrames;

			policy.OnBacklogRecovery(1);
			// Four seconds at 60 Hz is 240 refreshes: 241 is above it.
			const auto decision = Unmeasured(policy, input, 1, 241);
			Assert::AreEqual(static_cast<uint64_t>(1),
				decision.diagnostic.dxgiRebases);
			Assert::AreEqual(static_cast<uint64_t>(0),
				decision.diagnostic.repeatedRefreshes);
			Assert::AreEqual(before - 1.0, decision.phaseFrames, 1e-9);
		}

		TEST_METHOD(RepeatedRefreshBeforeAnyRateReBases)
		{
			// At start-up no display rate has been measured: a repeated
			// refresh re-bases the ledger instead of counting.
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = RestingInput(0.0);
			Unmeasured(policy, input, 1);
			const auto decision = Unmeasured(policy, input, 1, 1);
			Assert::AreEqual(static_cast<uint64_t>(1),
				decision.diagnostic.dxgiRebases);
			Assert::AreEqual(static_cast<uint64_t>(0),
				decision.diagnostic.repeatedRefreshes);
			Assert::AreEqual(0.0, decision.phaseFrames);
		}

		TEST_METHOD(OutputChangeEndsRecoveryHold)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = RestingInput(14.25);
			Steps(policy, input, 120 + 1000);

			policy.OnBacklogRecovery(1);
			policy.OnOutputChanged();
			auto decision = Unmeasured(policy, input, 1, 1);
			Assert::AreEqual(static_cast<uint64_t>(1),
				decision.diagnostic.dxgiRebases);
			Assert::AreEqual(static_cast<uint64_t>(0),
				decision.diagnostic.repeatedRefreshes);

			// The rates are measured again from the first sample.
			decision = Step(policy, input);
			Assert::AreEqual(static_cast<uint32_t>(1),
				decision.rateFilterSamples);
			Steps(policy, input, 119);
			decision = Step(policy, input);
			Assert::AreEqual(
				static_cast<int>(AlphaCadenceTimingStatus::Measuring),
				static_cast<int>(decision.timingStatus));
			Assert::AreEqual(Action(AlphaCadenceAction::None),
				Action(decision.predictedAction));
		}

		TEST_METHOD(OutputChangeInsideRecoveryWrapsBySourceDirection)
		{
			// An output change inside a recovery clears the direction; the wrap
			// then takes it from the rebuilt filter, so a slower source keeps
			// its negative phase.
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = RestingInput(-14.25);
			const double drift = Drift(input);
			const auto settled = Steps(policy, input, 120 + 1000);
			Assert::AreEqual(Action(AlphaCadenceAction::Repeat),
				Action(settled.predictedAction));

			policy.OnBacklogRecovery(1);
			policy.OnOutputChanged();
			Unmeasured(policy, input, 12);
			Steps(policy, input, 120);
			const auto decision = Step(policy, input);
			Assert::AreEqual(settled.phaseFrames - 1.0 + 133.0 * drift,
				decision.phaseFrames, 1e-9);
			Assert::AreEqual(static_cast<int64_t>(0),
				decision.diagnostic.referenceShiftFrames);
		}

		TEST_METHOD(CancelledDropEndsPrematureCheck)
		{
			// A drop that did not happen leaves no repeated refresh to cancel:
			// the next one is one more frame in the chain.
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = RestingInput(0.0);
			DropAtScene(policy, input);
			policy.CancelPendingAction();
			const auto decision = Step(policy, input, 1);
			Assert::AreEqual(2.0, decision.phaseFrames);
			Assert::AreEqual(static_cast<uint64_t>(0),
				decision.diagnostic.prematureDrops);
		}

		TEST_METHOD(BacklogRecoveryEndsPrematureCheck)
		{
			// A stall's repeated refreshes cannot be told from a premature
			// drop's: after a backlog recovery every one is counted.
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = RestingInput(0.0);
			DropAtScene(policy, input);
			policy.OnBacklogRecovery(1);
			const auto decision = Step(policy, input, 1);
			Assert::AreEqual(0.0, decision.phaseFrames);
			Assert::AreEqual(static_cast<uint64_t>(0),
				decision.diagnostic.prematureDrops);
			Assert::AreEqual(static_cast<uint64_t>(2),
				decision.diagnostic.repeatedRefreshes);
		}

		TEST_METHOD(MissingSampleKeepsCount)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = RestingInput(0.0);
			Steps(policy, input, 120 + 10);
			for (uint32_t frame = 0; frame < 50; ++frame)
			{
				input.dxgiPresentCount += 1;
				input.dxgiPresentRefreshCount += 1;
				input.dxgiSampleValid = false;
				Assert::AreEqual(0.0, policy.Evaluate(input).phaseFrames);
			}
			const auto decision = Step(policy, input, 2);
			Assert::AreEqual(2.0, decision.phaseFrames);
			Assert::AreEqual(static_cast<uint64_t>(2),
				decision.diagnostic.repeatedRefreshes);
		}

		TEST_METHOD(RegressionReBases)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = RestingInput(0.0);
			Steps(policy, input, 120 + 10);

			// The present count goes back.
			auto decision = Sample(policy, input,
				static_cast<uint32_t>(-5), 1);
			Assert::AreEqual(0.0, decision.phaseFrames);
			Assert::AreEqual(static_cast<uint64_t>(1),
				decision.diagnostic.dxgiRebases);
			Assert::AreEqual(0.0, Step(policy, input).phaseFrames);

			// Fewer refreshes than presents.
			decision = Sample(policy, input, 3, 1);
			Assert::AreEqual(0.0, decision.phaseFrames);
			Assert::AreEqual(static_cast<uint64_t>(2),
				decision.diagnostic.dxgiRebases);

			// A step above four seconds of refreshes.
			decision = Sample(policy, input, 1, 1 + 241);
			Assert::AreEqual(0.0, decision.phaseFrames);
			Assert::AreEqual(static_cast<uint64_t>(3),
				decision.diagnostic.dxgiRebases);
			Assert::AreEqual(static_cast<uint64_t>(0),
				decision.diagnostic.repeatedRefreshes);

			// Counting resumes from the re-based pair.
			decision = Step(policy, input, 1);
			Assert::AreEqual(1.0, decision.phaseFrames);
			Assert::AreEqual(static_cast<uint64_t>(1),
				decision.diagnostic.repeatedRefreshes);
		}

		TEST_METHOD(CounterWrapCounts)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = RestingInput(0.0);
			input.dxgiPresentCount = 0xFFFFFF00u;
			input.dxgiPresentRefreshCount = 0xFFFFFF08u;
			Steps(policy, input, 120 + 10);

			const auto decision = Sample(policy, input, 0x80, 0x81);
			Assert::IsTrue(input.dxgiPresentCount < 0x100u);
			Assert::IsTrue(input.dxgiPresentRefreshCount < 0x100u);
			Assert::AreEqual(1.0, decision.phaseFrames);
			Assert::AreEqual(static_cast<uint64_t>(1),
				decision.diagnostic.repeatedRefreshes);
			Assert::AreEqual(static_cast<uint64_t>(0),
				decision.diagnostic.dxgiRebases);
		}

		TEST_METHOD(RepeatedRefreshAfterDropIsPremature)
		{
			// Within PREMATURE_DROP_WINDOW evaluations of a drop, one repeated
			// refresh cancels it.
			AlphaCadenceCorrectionPolicy inside;
			AlphaCadenceCorrectionInput insideInput = RestingInput(0.0);
			DropAtScene(inside, insideInput);
			Steps(inside, insideInput, 9);
			auto decision = Step(inside, insideInput, 1);
			Assert::AreEqual(0.0, decision.phaseFrames);
			Assert::AreEqual(static_cast<uint64_t>(1),
				decision.diagnostic.prematureDrops);
			Assert::AreEqual(static_cast<uint64_t>(2),
				decision.diagnostic.repeatedRefreshes);

			// After the window it is one more frame in the chain.
			AlphaCadenceCorrectionPolicy after;
			AlphaCadenceCorrectionInput afterInput = RestingInput(0.0);
			DropAtScene(after, afterInput);
			Steps(after, afterInput, 10);
			decision = Step(after, afterInput, 1);
			Assert::AreEqual(1.0, decision.phaseFrames);
			Assert::AreEqual(static_cast<uint64_t>(0),
				decision.diagnostic.prematureDrops);
			Assert::AreEqual(static_cast<uint64_t>(2),
				decision.diagnostic.repeatedRefreshes);
		}

		TEST_METHOD(GenerationChangeClearsLedger)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = RestingInput(0.0);
			Steps(policy, input, 120 + 10);
			Step(policy, input, 1);
			policy.OnBacklogRecovery(2);
			Steps(policy, input, 120 + 1);
			auto decision = Sample(policy, input, 3, 1);
			Assert::AreEqual(static_cast<uint64_t>(1),
				decision.diagnostic.repeatedRefreshes);
			Assert::AreEqual(static_cast<uint64_t>(2),
				decision.diagnostic.framesFlushed);
			Assert::AreEqual(static_cast<int64_t>(1),
				decision.diagnostic.referenceShiftFrames);
			Assert::AreEqual(static_cast<uint64_t>(1),
				decision.diagnostic.dxgiRebases);

			// The new generation's first sample is its baseline, so the two
			// repeated refreshes it carries are not counted.
			input.generation = 2;
			decision = Step(policy, input, 2);
			Assert::AreEqual(0.0, decision.phaseFrames);
			Assert::AreEqual(static_cast<uint64_t>(0),
				decision.diagnostic.repeatedRefreshes);
			Assert::AreEqual(static_cast<uint64_t>(0),
				decision.diagnostic.framesFlushed);
			Assert::AreEqual(static_cast<uint64_t>(0),
				decision.diagnostic.prematureDrops);
			Assert::AreEqual(static_cast<uint64_t>(0),
				decision.diagnostic.dxgiRebases);
			Assert::AreEqual(static_cast<int64_t>(0),
				decision.diagnostic.referenceShiftFrames);

			decision = Step(policy, input, 1);
			Assert::AreEqual(1.0, decision.phaseFrames);
			Assert::AreEqual(static_cast<uint64_t>(1),
				decision.diagnostic.repeatedRefreshes);
		}

		static uint64_t DueEvaluationsUntilLimitDrop(double displayHz)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input =
				Input(displayHz * (1.0 + 100.0 / 1000000.0));
			input.displayRateHz = displayHz;
			input.queueDepth = input.desiredQueueDepth - 1;
			auto decision = AdvanceUntilDue(policy, input);
			uint64_t dueEvaluations = 1;
			while (decision.action != AlphaCadenceAction::Drop)
			{
				Assert::AreEqual(
					Reason(AlphaCadenceBlockReason::WaitingForFreshScene),
					Reason(decision.blockReason));
				Assert::IsTrue(dueEvaluations < 20000);
				decision = policy.Evaluate(input);
				++dueEvaluations;
			}
			Assert::IsTrue(decision.deadlineFallback);
			return dueEvaluations;
		}

		TEST_METHOD(DueDropTimeLimitIsThreeMinutesOfDisplayFrames)
		{
			// ceil(180 s x the display rate), as the planning fallback converts.
			Assert::AreEqual(static_cast<uint64_t>(4316),
				DueEvaluationsUntilLimitDrop(24000.0 / 1001.0));
			Assert::AreEqual(static_cast<uint64_t>(10790),
				DueEvaluationsUntilLimitDrop(60000.0 / 1001.0));
		}

		// One drop at a fresh scene, the scene cleared again after it.
		static AlphaCadenceCorrectionDecision StepAtScene(
			AlphaCadenceCorrectionPolicy& policy,
			AlphaCadenceCorrectionInput& input,
			uint64_t sceneEventId)
		{
			input.safeSceneBoundary = true;
			input.sceneEventId = sceneEventId;
			const auto decision = Step(policy, input);
			input.safeSceneBoundary = false;
			input.sceneEventId = 0;
			return decision;
		}

		TEST_METHOD(SlowerSourceDropsOnlyWhatAnEventLeftAboveItsFloor)
		{
			// The display running out of frames by itself puts a slower
			// source's chain a whole frame less the drift above its floor: no
			// drop. One more repeated refresh is an event's frame: a drop.
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = RestingInput(-14.25);
			const double drift = Drift(input);
			auto decision = Steps(policy, input, 120 + 1000);
			Assert::AreEqual(Action(AlphaCadenceAction::Repeat),
				Action(decision.predictedAction));
			Assert::AreEqual(0.0, decision.diagnostic.aboveFloorFrames);

			decision = Step(policy, input, 1);
			Assert::IsFalse(decision.due);
			Assert::AreEqual(1.0 + drift,
				decision.diagnostic.aboveFloorFrames, 1e-9);

			decision = Step(policy, input, 1);
			Assert::IsTrue(decision.due);
			Assert::AreEqual(Action(AlphaCadenceAction::Drop),
				Action(decision.predictedAction));
			Assert::AreEqual(2.0 + 2.0 * drift,
				decision.diagnostic.aboveFloorFrames, 1e-9);

			decision = StepAtScene(policy, input, 1);
			Assert::AreEqual(Action(AlphaCadenceAction::Drop),
				Action(decision.action));
			decision = Steps(policy, input, 2000);
			Assert::IsFalse(decision.due);
			Assert::AreEqual(1.0 + 2003.0 * drift,
				decision.diagnostic.aboveFloorFrames, 1e-9);
		}

		TEST_METHOD(SlowerSourceStallSurplusSurvivesTheWrap)
		{
			// A stall of five periods: the backstop flushes three frames and
			// the five repeated refreshes reach the policy after it. The two
			// frames left stay in the phase, above a floor the flush did not
			// move, and are due.
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = RestingInputAt23976(-14.25);
			const double drift = Drift(input);
			const auto settled = Steps(policy, input, 120 + 1000);

			policy.OnBacklogRecovery(3);
			Unmeasured(policy, input, 12, 5);
			Steps(policy, input, 120);
			const auto decision = Step(policy, input);
			Assert::AreEqual(settled.phaseFrames + 2.0 + 133.0 * drift,
				decision.phaseFrames, 1e-9);
			Assert::AreEqual(static_cast<int64_t>(0),
				decision.diagnostic.referenceShiftFrames);
			Assert::AreEqual(2.0 + 133.0 * drift,
				decision.diagnostic.aboveFloorFrames, 1e-9);
			Assert::IsTrue(decision.due);
			Assert::AreEqual(Action(AlphaCadenceAction::Drop),
				Action(decision.predictedAction));
		}

		// The display runs out of frames by itself while the phase stands just
		// under `above` the floor: that is where the floor is, so it moves
		// there, and no drop falls due.
		static void ExpectRunOutAnchorsTheFloor(double ppm, double above)
		{
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = RestingInput(ppm);
			const double drift = Drift(input);
			Steps(policy, input, 120 + 1000);
			Step(policy, input, 1);
			Assert::IsTrue(Step(policy, input, 1).due);
			Assert::AreEqual(Action(AlphaCadenceAction::Drop),
				Action(StepAtScene(policy, input, 1).action));

			auto decision = Step(policy, input);
			while (decision.diagnostic.aboveFloorFrames >= above)
				decision = Step(policy, input);
			Assert::IsTrue(decision.diagnostic.aboveFloorFrames > 0.0);

			decision = Step(policy, input, 1);
			Assert::IsFalse(decision.due);
			Assert::AreEqual(1.0 + drift,
				decision.diagnostic.aboveFloorFrames, 1e-9);
		}

		TEST_METHOD(SlowerSourceRunOutAnchorsTheFloor)
		{
			// At -150 ppm on 60 Hz, arrival and render timing: 10 s of drift is
			// 0.09 frames.
			ExpectRunOutAnchorsTheFloor(-150.0, 0.05);
			// At -14.25 ppm, a forecast 0.2 ppm slow leaves the next run-out
			// 0.014 frames above the floor.
			ExpectRunOutAnchorsTheFloor(-14.25, 0.015);
		}

		TEST_METHOD(SlowerSourceDropsByTheLimitWithNoCut)
		{
			// The chain has drained past the run's start reference before the
			// display runs dry, so an event's frame is due while the phase is
			// below the planning point: the limit still takes it. At -50 ppm
			// the drift drains half a frame in the three minutes, so the frame
			// is still due when the limit comes.
			AlphaCadenceCorrectionPolicy policy;
			AlphaCadenceCorrectionInput input = RestingInput(-50.0);
			Steps(policy, input, 120 + 30000);
			Step(policy, input, 1);
			auto decision = Step(policy, input, 1);
			Assert::IsTrue(decision.due);
			Assert::IsTrue(decision.phaseFrames < 0.75);

			uint64_t dueEvaluations = 1;
			while (decision.action != AlphaCadenceAction::Drop)
			{
				Assert::AreEqual(
					Reason(AlphaCadenceBlockReason::WaitingForFreshScene),
					Reason(decision.blockReason));
				Assert::IsTrue(dueEvaluations < 20000);
				decision = Step(policy, input);
				++dueEvaluations;
			}
			Assert::IsTrue(decision.deadlineFallback);
			Assert::AreEqual(static_cast<uint64_t>(10800), dueEvaluations);
		}
	};
}
