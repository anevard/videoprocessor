#pragma once

// The Config Patterns page's "Low latency" box (ledger A356 (5); operator,
// 2026-11-17, questions 1-4). While it is on, VP holds no frames back for the
// pattern generator: his own [queue.Low_Latency] values (queue_size 1,
// lead_frames 1, startup_preroll_frames 0, target_frames 1; the repository's
// VideoProcessor.cfg) with its reset_queue_too_large_percent 200 (at a queue
// of one, a lower high water trips on every queued frame), [general]
// presentation_queue_depth 1, and every feature that carries a picture from
// one frame into the next set aside: peak detection smoothing
// (smoothing_period 0), frame mixing (smooth_motion off) and deinterlacing
// (deinterlace off; its default, auto, never touches the generator's
// progressive frames). A patch then reaches VP's present within 2 frames and
// the screen within 1 refresh more, on all four routes (A356 (2)).
//
// Modelled on the 1:1 set-aside (PatgenNoCrop.h): nothing is selected,
// pinned, written or saved. Only the copy of the profile snapshot handed to
// the renderer changes: its queue, and a configuration copy holding the keys
// above (ConfigFile::SetValueInMemory; the file is never written). The depth
// and the motion keys apply only by a renderer restart, his own (operator,
// question 1: one when the box goes on, one when it goes off); the new
// renderer is built from the copy, so it finds nothing more to restart.
//
// On only by the user's click while the generator is the input. It stays on
// while the generator stays the input: a restart of that same input (Config's
// "Restart now", VP's capture Restart, a configuration that restarts capture)
// keeps it, so its order against a Signal change does not matter and the next
// renderer is built at low latency at once (operator, 2026-11-17, replacing
// question 4's "off whenever the generator's input starts"). Off when VP
// starts (never saved) and when another input replaces the generator; never
// on a renderer restart, the mode's own included (operator: "make sure it
// doesn't get into a loop"); the box never causes an input restart. VP
// decides where the input stops (CaptureRemove), a step no renderer restart
// takes, and again where a new input is set up, so no other input inherits it.
//
// Config sends RunAction "pattern.low_latency on" / "pattern.low_latency off"
// and reads the box with "pattern.low_latency_state" (OK for on, refused with
// Off for off, as PatgenRawOutput.h); VP answers all three itself and passes
// none of them to the generator.

#include <cstddef>
#include <string>
#include <vector>

namespace PatgenLowLatency
{
	constexpr const char* Command = "low_latency";
	constexpr const char* StateCommand = "low_latency_state";
	constexpr const char* Off = "Low latency is off.";
	// "low_latency on" refused: the renderer kept the copy back
	// (SourcePluginKeptBack.h), so the box went back off.
	constexpr const char* KeptBack = "Low latency stays off: VP's renderer kept its current settings; vp.log says why.";

	// The queue profile names the renderer's copy carries, so vp.log's
	// "Queue profile applied" line says whose values these are. Profile names
	// are stored lower-case (ConfigFile::NormalizeName); these are not, so no
	// configuration's queue profile can be mistaken for them.
	constexpr const char* QueueProfile = "PATGEN low latency";
	constexpr const char* RestoreProfile = "PATGEN low latency off";

	// "low_latency on" or "low_latency off" (the command after "pattern.");
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

	// What VP's events do to the box (operator, 2026-11-17, and NO LOOP): a
	// restart of the same input and a renderer restart leave it as it is; the
	// input stopping for good (another input chosen, VP closing) and another
	// input being set up switch it off.
	enum class Event
	{
		InputRestarted,
		InputStopped,
		OtherInputStarted,
		RendererRestarted,
	};
	inline bool After(bool on, Event event)
	{
		return on && (event == Event::InputRestarted || event == Event::RendererRestarted);
	}

	// The event of an input stopping (CaptureRemove): a restart of the same
	// input when VP wants the capture restarted and the device to come back
	// is the one stopping; else the input stops for good.
	inline Event StopEvent(bool wantToRestartCapture, bool sameDeviceComesBack)
	{
		return wantToRestartCapture && sameDeviceComesBack ? Event::InputRestarted : Event::InputStopped;
	}

	// While the box is on: his [queue.Low_Latency] values over the user's
	// queue (RendererProfileConfig::ResolvedQueue). The active-picture
	// lookahead and the reset delay stay as the user's profile has them.
	constexpr std::size_t QueueSize = 1;
	constexpr std::size_t LeadFrames = 1;
	constexpr std::size_t StartupPrerollFrames = 0;
	constexpr std::size_t TargetFrames = 1;
	constexpr int HighWaterPercent = 200;
	template <typename Queue>
	void SetQueue(Queue& queue)
	{
		queue.profile = QueueProfile;
		queue.hasQueueSize = true;
		queue.queueSize = QueueSize;
		queue.hasLeadFrames = true;
		queue.leadFrames = LeadFrames;
		queue.hasStartupPrerollFrames = true;
		queue.startupPrerollFrames = StartupPrerollFrames;
		queue.hasTargetFrames = true;
		queue.targetFrames = TargetFrames;
		queue.hasResetQueueTooLargePercent = true;
		queue.resetQueueTooLargePercent = HighWaterPercent;
	}

	// Once the box is off, when the user's own snapshot names no queue
	// profile: VP applies a queue only when a profile is named, so the mode's
	// values would stay. A profile that names no value gives back VP's own
	// values, those it started with (VideoProcessorDlg.cpp, the queue block's
	// fallbacks).
	template <typename Queue>
	void RestoreQueue(Queue& queue)
	{
		queue = Queue();
		queue.profile = RestoreProfile;
	}

	// The presentation depth (PresentationConfig.h: [general], 1 to 8,
	// default 2): DXGI's maximum frame latency and the swapchain's buffers.
	constexpr const char* DepthSection = "general";
	constexpr const char* DepthKey = "presentation_queue_depth";
	constexpr const char* Depth = "1";

	// The keys that carry a picture from one frame into the next, set to
	// their off value wherever a section of the copy holds them, whatever
	// group it belongs to: those are every place the renderer reads them from.
	struct Key
	{
		const char* key;
		const char* value;
	};
	inline const std::vector<Key>& FrameStateKeys()
	{
		static const std::vector<Key> keys = {
			{ "smoothing_period", "0" },
			{ "smooth_motion", "off" },
			{ "deinterlace", "off" } };
		return keys;
	}
	constexpr const char* SmoothingKey = "smoothing_period";

	// A section the renderer takes peak smoothing from in the target model
	// (VP-0079, `vprenderer.*`): the display group, the `vprenderer` root and
	// its named variants (the same test as RendererProfileConfig::
	// SettingAlias's "rendering"), and the input group. With no smoothing key
	// in any of them the renderer keeps libplacebo's default of 20 frames, so
	// each gets the key too: the root is the renderer's baseline, a group with
	// no root applies its first variant before the selected one, and a
	// variant without the key keeps what came before. Never a section the
	// copy does not already hold: a new root would become a group's baseline
	// in place of the operator's first variant. isChildNamespace is
	// RendererProfileConfig::IsRendererChildNamespace.
	template <typename IsChildNamespace>
	bool IsToneSection(const std::string& section, IsChildNamespace isChildNamespace)
	{
		const std::string root = "vprenderer";
		const std::string input = "vprenderer.input";
		if (section == root || section == input) return true;
		if (section.rfind(input + ".", 0) == 0)
			return section.find('.', input.size() + 1) == std::string::npos;
		if (section.rfind(root + ".", 0) != 0) return false;
		const std::string name = section.substr(root.size() + 1);
		return name.find('.') == std::string::npos && !isChildNamespace(name);
	}

	// What SetAside changed in the copy, for vp.log.
	struct Report
	{
		std::vector<std::string> smoothing;   // sections with smoothing_period 0
		std::vector<std::string> other;       // "<section> <key>" for the other frame-state keys
	};

	// The renderer's configuration copy while the box is on (Config: a
	// ConfigFile copy; never the file VP loaded). targetModel is
	// RendererProfileConfig::IsTargetModel of the operator's file.
	template <typename Config, typename IsChildNamespace>
	Report SetAside(Config& copy, bool targetModel, IsChildNamespace isChildNamespace)
	{
		Report report;
		copy.SetValueInMemory(DepthSection, DepthKey, Depth);
		for (const std::string& section : copy.GetSectionNames())
		{
			for (const Key& entry : FrameStateKeys())
			{
				std::string value;
				const bool smoothing = std::string(entry.key) == SmoothingKey;
				const bool held = copy.TryGetString(section, entry.key, value);
				if (!held && !(smoothing && targetModel && IsToneSection(section, isChildNamespace)))
					continue;
				copy.SetValueInMemory(section, entry.key, entry.value);
				if (smoothing) report.smoothing.push_back(section);
				else report.other.push_back(section + " " + entry.key);
			}
		}
		return report;
	}

	// One vp.log line's worth.
	inline std::string Describe(const Report& report)
	{
		std::string text = "queue_size " + std::to_string(QueueSize) + ", target_frames " +
			std::to_string(TargetFrames) + ", lead_frames " + std::to_string(LeadFrames) +
			", startup_preroll_frames " + std::to_string(StartupPrerollFrames) +
			", reset_queue_too_large_percent " + std::to_string(HighWaterPercent) +
			"; [general] presentation_queue_depth " + Depth + "; smoothing_period 0 in ";
		if (report.smoothing.empty())
			text += "no section (none of the copy's sections takes it; the renderer's default applies)";
		for (std::size_t i = 0; i < report.smoothing.size(); ++i)
			text += (i ? ", [" : "[") + report.smoothing[i] + "]";
		text += "; set to off:";
		if (report.other.empty())
			text += " nothing else (smooth_motion and deinterlace as their defaults, off and auto)";
		for (std::size_t i = 0; i < report.other.size(); ++i)
			text += (i ? ", [" : " [") + report.other[i].substr(0, report.other[i].find(' ')) + "] " +
				report.other[i].substr(report.other[i].find(' ') + 1);
		return text;
	}
}
