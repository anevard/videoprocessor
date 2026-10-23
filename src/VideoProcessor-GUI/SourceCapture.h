#pragma once

#include <SourceCaptureProtocol.h>

#include <IRenderer.h>
#include <VideoFrame.h>
#include <VideoState.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

// One source capture held in memory for the second PC (VIEWING-PLAN
// CAPTURE-FORMAT and TRANSPORT). The UI thread arms and serves pulls; the
// capture card thread copies the next admitted frame. The card thread takes
// the lock only while armed, so an idle or pulled capture never touches it.
class SourceCapture
{
public:
	// Everything the UI thread knows when the capture is requested.
	struct ArmContext
	{
		uint64_t publishedSequence = 0;   // ingress capture sequence at arm
		VideoStateComPtr deviceState;     // as the card reports it
		VideoStateComPtr vpState;         // what VP made of it
		std::string buildVersion;
		std::string configFnv1a64;
		std::map<std::string, std::string> profiles;
	};

	// Renderer access for the report, supplied by the caller so this header
	// stays independent of the plugin proxy.
	struct ReportAccess
	{
		virtual ~ReportAccess() = default;
		virtual bool Arm(uint64_t deviceCounter) = 0;
		virtual bool Get(RendererSourceCaptureReport& report) = 0;
	};

	static constexpr uint64_t ReportWaitMilliseconds = 2000;

	bool IsArmed() const { return armed_.load(std::memory_order_acquire); }

	// UI thread. Replaces any earlier capture.
	bool Arm(ArmContext context, uint64_t& captureId, std::string& error)
	{
		if (!context.deviceState || !context.deviceState->valid ||
			!context.vpState || !context.vpState->valid ||
			!context.deviceState->displayMode)
		{
			error = "No valid source signal to capture.";
			return false;
		}
		const uint32_t bytes = context.deviceState->BytesPerFrame();
		if (bytes == 0)
		{
			error = "The source encoding has no known frame size.";
			return false;
		}
		std::lock_guard<std::mutex> lock(mutex_);
		armed_.store(false, std::memory_order_release);
		Clear();
		try { pixels_.resize(bytes); }
		catch (const std::bad_alloc&)
		{
			error = "VideoProcessor could not reserve memory for the capture.";
			return false;
		}
		context_ = std::move(context);
		captureId_ = ++nextCaptureId_;
		phase_ = Phase::Armed;
		captureId = captureId_;
		armed_.store(true, std::memory_order_release);
		return true;
	}

	// Card thread, inside the ingress lease and before renderer->OnVideoFrame,
	// so the renderer report is armed before the frame can render.
	void OnAdmittedFrame(const VideoFrame& frame, const VideoStateComPtr& deviceState,
		uint64_t publishedSequence, ReportAccess* report)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (phase_ != Phase::Armed)
			return;
		armed_.store(false, std::memory_order_release);
		if (publishedSequence != context_.publishedSequence)
			return Fail("The source signal changed during the capture; capture again.");
		if (!deviceState || !deviceState->valid || !deviceState->displayMode ||
			deviceState->BytesPerFrame() != pixels_.size() || !frame.GetData())
			return Fail("The source frame did not match the signal state; capture again.");
		std::memcpy(pixels_.data(), frame.GetData(), pixels_.size());
		deviceCounter_ = frame.GetCounter();
		context_.deviceState = deviceState;
		SYSTEMTIME local{};
		GetLocalTime(&local);
		char stamp[32]{};
		sprintf_s(stamp, "%04u-%02u-%02uT%02u:%02u:%02u.%03u",
			local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute,
			local.wSecond, local.wMilliseconds);
		captureTime_ = stamp;
		copiedTick_ = GetTickCount64();
		reportArmed_ = report && report->Arm(deviceCounter_);
		phase_ = Phase::Copied;
	}

	// UI thread. Builds the pull response payload.
	std::vector<uint8_t> Pull(uint64_t captureId, uint32_t offset,
		uint32_t length, ReportAccess* report)
	{
		using SourceCaptureProtocol::PullState;
		std::vector<uint8_t> out;
		std::lock_guard<std::mutex> lock(mutex_);
		if (captureId != captureId_ || phase_ == Phase::Idle)
		{
			out.push_back(static_cast<uint8_t>(PullState::Failed));
			ConfigurationRpcProtocol::WriteString(out,
				"That capture is no longer held; capture again.");
			return out;
		}
		if (phase_ == Phase::Failed)
		{
			out.push_back(static_cast<uint8_t>(PullState::Failed));
			ConfigurationRpcProtocol::WriteString(out, failure_);
			return out;
		}
		if (phase_ == Phase::Armed)
		{
			// No frame admitted yet (none arriving, or the signal is settling).
			out.push_back(static_cast<uint8_t>(PullState::Waiting));
			return out;
		}
		if (phase_ == Phase::Copied)
		{
			RendererSourceCaptureReport filled;
			const bool haveReport = reportArmed_ && report && report->Get(filled) &&
				filled.deviceCounter == deviceCounter_;
			const bool late = !reportArmed_ ||
				GetTickCount64() - copiedTick_ >= ReportWaitMilliseconds;
			if (!haveReport && !late)
			{
				out.push_back(static_cast<uint8_t>(PullState::Waiting));
				return out;
			}
			sidecar_ = BuildSidecar(haveReport ? &filled : nullptr,
				!reportArmed_ ? "the renderer does not provide capture reports" :
				"the captured frame was not rendered in time");
			phase_ = Phase::Ready;
		}
		const uint32_t total = static_cast<uint32_t>(pixels_.size());
		if (offset > total || length > SourceCaptureProtocol::MaximumPieceBytes)
		{
			out.push_back(static_cast<uint8_t>(PullState::Failed));
			ConfigurationRpcProtocol::WriteString(out, "Invalid capture piece request.");
			return out;
		}
		const uint32_t piece = (std::min)(length, total - offset);
		out.reserve(16 + sidecar_.size() + piece);
		out.push_back(static_cast<uint8_t>(PullState::Ready));
		ConfigurationRpcProtocol::Write32(out, total);
		ConfigurationRpcProtocol::WriteString(out, length == 0 ? sidecar_ : std::string());
		ConfigurationRpcProtocol::Write32(out, piece);
		out.insert(out.end(), pixels_.begin() + offset, pixels_.begin() + offset + piece);
		return out;
	}

	// UI thread.
	void Release(uint64_t captureId)
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (captureId != captureId_)
			return;
		armed_.store(false, std::memory_order_release);
		Clear();
	}

private:
	enum class Phase { Idle, Armed, Copied, Ready, Failed };

	void Clear()
	{
		std::vector<uint8_t>().swap(pixels_);
		context_ = {};
		phase_ = Phase::Idle;
		failure_.clear();
		sidecar_.clear();
		captureTime_.clear();
		deviceCounter_ = 0;
		copiedTick_ = 0;
		reportArmed_ = false;
	}

	void Fail(const char* reason)
	{
		std::vector<uint8_t>().swap(pixels_);
		failure_ = reason;
		phase_ = Phase::Failed;
	}

	static std::string Narrow(const TCHAR* text)
	{
		return text ? std::string(CStringA(text).GetString()) : std::string();
	}

	static std::string Json(const std::string& value)
	{
		std::string out = "\"";
		for (const unsigned char c : value)
		{
			if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
			else if (c < 0x20)
			{
				char escaped[8]{};
				sprintf_s(escaped, "\\u%04x", c);
				out += escaped;
			}
			else out += static_cast<char>(c);
		}
		return out + "\"";
	}

	static std::string Number(double value)
	{
		if (!std::isfinite(value)) return "null";
		std::ostringstream out;
		out.precision(10);
		out << value;
		return out.str();
	}

	static std::string Bool(bool value) { return value ? "true" : "false"; }

	static void Hdr10(std::ostringstream& out, const VideoState& state)
	{
		if (!state.hdrData) { out << "null"; return; }
		const HDRData& hdr = *state.hdrData;
		out << "{\"mastering_min_nits\": " << Number(hdr.masteringDisplayMinLuminance)
			<< ", \"mastering_max_nits\": " << Number(hdr.masteringDisplayMaxLuminance)
			<< ", \"max_cll\": " << Number(hdr.maxCll)
			<< ", \"max_fall\": " << Number(hdr.maxFall)
			<< ", \"primaries\": [" << Number(hdr.displayPrimaryRedX) << ", "
			<< Number(hdr.displayPrimaryRedY) << ", " << Number(hdr.displayPrimaryGreenX) << ", "
			<< Number(hdr.displayPrimaryGreenY) << ", " << Number(hdr.displayPrimaryBlueX) << ", "
			<< Number(hdr.displayPrimaryBlueY) << "]"
			<< ", \"white_point\": [" << Number(hdr.whitePointX) << ", "
			<< Number(hdr.whitePointY) << "]}";
	}

	static void State(std::ostringstream& out, const VideoState& state)
	{
		out << "{\"encoding\": " << Json(Narrow(ToString(state.videoFrameEncoding)))
			<< ", \"eotf\": " << Json(Narrow(ToString(state.eotf)))
			<< ", \"colorspace\": " << Json(Narrow(ToString(state.colorspace)))
			<< ", \"inverted_vertical\": " << Bool(state.invertedVertical)
			<< ", \"dolby_vision_tunnel\": " << Bool(state.dolbyVisionTunnel)
			<< ", \"hdr10\": ";
		Hdr10(out, state);
		out << "}";
	}

	std::string BuildSidecar(const RendererSourceCaptureReport* report,
		const char* missingReason) const
	{
		const VideoState& device = *context_.deviceState;
		const DisplayMode& mode = *device.displayMode;
		std::ostringstream out;
		out << "{\n  \"schema\": \"vp-source-capture-v1\",\n"
			<< "  \"identity\": {\"capture_id\": " << captureId_
			<< ", \"source_sequence\": " << deviceCounter_
			<< ", \"capture_time\": " << Json(captureTime_)
			<< ", \"build_version\": " << Json(context_.buildVersion)
			<< ", \"config_fnv1a64\": " << Json(context_.configFnv1a64) << "},\n"
			<< "  \"frame\": {\"width\": " << mode.FrameWidth()
			<< ", \"height\": " << mode.FrameHeight()
			<< ", \"row_bytes\": " << device.BytesPerRow()
			<< ", \"byte_size\": " << pixels_.size()
			<< ", \"frame_rate\": " << Number(mode.RefreshRateHz())
			<< ", \"time_scale\": " << mode.TimeScale()
			<< ", \"frame_duration\": " << mode.FrameDuration() << "},\n"
			<< "  \"device_state\": ";
		State(out, device);
		out << ",\n  \"vp_state\": ";
		State(out, *context_.vpState);
		out << ",\n  \"renderer\": ";
		if (!report)
			out << "{\"available\": false, \"reason\": " << Json(missingReason) << "}";
		else
		{
			const RendererHdrStatus& hdr = report->hdr;
			out << "{\"available\": true"
				<< ", \"renderer_sequence\": " << hdr.sourceSequence
				<< ", \"source_levels\": " << Json(report->sourceLevels)
				<< ", \"source_transfer\": " << Json(report->sourceTransfer)
				<< ", \"source_primaries\": " << Json(report->sourcePrimaries)
				<< ", \"source_min_nits\": " << Number(report->sourceMinNits)
				<< ", \"source_max_nits\": " << Number(report->sourceMaxNits)
				<< ", \"scene_peak_pq\": " << Number(report->scenePeakPq)
				<< ", \"scene_average_pq\": " << Number(report->sceneAveragePq)
				<< ",\n    \"dolby_vision\": " << Bool(hdr.dolbyVision)
				<< ", \"l1_present\": " << Bool(report->dvSceneAvailable)
				<< ", \"dv_levels\": [";
			bool first = true;
			if (report->dvAvailable)
			{
				out << "{\"level\": \"source_range\", \"from_metadata\": "
					<< Bool(report->dvSourceRangeFromMetadata)
					<< ", \"min\": " << Number(report->dvSourceMinNits)
					<< ", \"max\": " << Number(report->dvSourceMaxNits) << "}";
				first = false;
			}
			if (report->dvSceneAvailable)
				out << (first ? "" : ", ") << "{\"level\": \"L1\", \"min\": "
					<< Number(report->dvSceneMinNits) << ", \"max\": "
					<< Number(report->dvSceneMaxNits) << ", \"avg\": "
					<< Number(report->dvSceneAvgNits) << "}";
			out << "],\n    \"detection\": {\"enabled\": " << Bool(hdr.peakDetectionEnabled)
				<< ", \"valid\": " << Bool(hdr.detectedValid)
				<< ", \"peak_nits\": " << Number(hdr.detectedPeakNits)
				<< ", \"avg_pq_nits\": " << Number(hdr.detectedAvgNits)
				<< ", \"avg_linear_nits\": " << Number(hdr.detectedLinearAvgNits)
				<< ", \"scene_max_nits\": " << Number(hdr.detectedSceneMaxNits) << "}"
				<< ",\n    \"scene\": {\"valid\": " << Bool(hdr.sceneValid)
				<< ", \"peak_nits\": " << Number(hdr.scenePeakNits)
				<< ", \"avg_nits\": " << Number(hdr.sceneAvgNits)
				<< ", \"min_nits\": " << Number(hdr.sceneMinNits) << "}"
				<< ",\n    \"picture_bounds\": ";
			if (report->boundsAvailable)
				out << "{\"left\": " << report->boundsLeft << ", \"top\": " << report->boundsTop
					<< ", \"right\": " << report->boundsRight << ", \"bottom\": "
					<< report->boundsBottom << "}";
			else
				out << "null";
			out << "}";
		}
		out << ",\n  \"profiles\": {";
		bool first = true;
		for (const auto& profile : context_.profiles)
		{
			out << (first ? "" : ", ") << Json(profile.first) << ": " << Json(profile.second);
			first = false;
		}
		out << "}\n}\n";
		return out.str();
	}

	std::atomic<bool> armed_{ false };
	std::mutex mutex_;
	Phase phase_ = Phase::Idle;
	uint64_t nextCaptureId_ = 0;
	uint64_t captureId_ = 0;
	ArmContext context_;
	std::vector<uint8_t> pixels_;
	uint64_t deviceCounter_ = 0;
	uint64_t copiedTick_ = 0;
	bool reportArmed_ = false;
	std::string captureTime_;
	std::string failure_;
	std::string sidecar_;
};
