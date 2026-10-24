#pragma once

#include <SourceCaptureProtocol.h>

#include <ACaptureDevice.h>
#include <ColorSpace.h>
#include <DisplayMode.h>
#include <EOTF.h>
#include <HDRData.h>
#include <VideoFrame.h>
#include <VideoFrameEncoding.h>
#include <VideoState.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

// Review (dtm-tuning REVIEW-BUILD.md): a still sent once by the second PC and
// shown in place of every live frame, on the live card's timing, until "Back
// to live". The rules that keep it from flickering:
//  - the swap decision and every signal-state publication happen under the
//    dialog's capture-state lock, so no frame passes under the other's state;
//  - the still's bytes never change after upload, and each swapped frame holds
//    them through its own lease, as a card frame holds its buffer;
//  - live state changes are held during review, not published; leaving review
//    publishes the newest one;
//  - a new capture run (a capture restart) gets the still's state again, so
//    review survives restarts.
class SourceReview
{
public:
	struct Still
	{
		uint64_t id = 0;
		std::vector<uint8_t> pixels;
		VideoStateComPtr deviceState;  // raw, as a card would report it
		bool dolbyVisionTunnel = false;
	};
	using StillPtr = std::shared_ptr<const Still>;

	enum class Publication
	{
		Publish,       // publish the given state as usual
		Hold,          // a live state during review: keep it, publish nothing
		PublishStill,  // a new capture run during review: publish the still's state
	};

	// ---- UI thread: the upload ----

	bool Begin(const SourceCaptureProtocol::ReviewStill& fields, uint64_t& stillId,
		std::string& error)
	{
		upload_ = {};
		VideoFrameEncoding encoding{};
		EOTF eotf{};
		ColorSpace colorspace{};
		if (!Parse(fields.encoding, VideoFrameEncoding::UNKNOWN, VideoFrameEncoding::DNxHR, encoding) ||
			!Parse(fields.eotf, EOTF::UNKNOWN, EOTF::HLG, eotf) ||
			!Parse(fields.colorspace, ColorSpace::UNKNOWN, ColorSpace::BT_2020, colorspace))
		{
			error = "VideoProcessor does not know this still's signal.";
			return false;
		}
		if (fields.width == 0 || fields.height == 0 || fields.byteSize == 0)
		{
			error = "The still has no picture size.";
			return false;
		}
		auto still = std::make_shared<Still>();
		still->deviceState = new VideoState();
		VideoState& state = *still->deviceState;
		state.valid = true;
		state.videoFrameEncoding = encoding;
		state.eotf = eotf;
		state.colorspace = colorspace;
		state.invertedVertical = fields.invertedVertical;
		if (fields.hasHdr10)
		{
			auto hdr = std::make_shared<HDRData>();
			const double* v = fields.hdr10;
			hdr->masteringDisplayMinLuminance = v[0];
			hdr->masteringDisplayMaxLuminance = v[1];
			hdr->maxCll = v[2];
			hdr->maxFall = v[3];
			hdr->displayPrimaryRedX = v[4];
			hdr->displayPrimaryRedY = v[5];
			hdr->displayPrimaryGreenX = v[6];
			hdr->displayPrimaryGreenY = v[7];
			hdr->displayPrimaryBlueX = v[8];
			hdr->displayPrimaryBlueY = v[9];
			hdr->whitePointX = v[10];
			hdr->whitePointY = v[11];
			state.hdrData = hdr;
		}
		still->dolbyVisionTunnel = fields.dolbyVisionTunnel;
		try { still->pixels.resize(fields.byteSize); }
		catch (const std::bad_alloc&)
		{
			error = "VideoProcessor could not reserve memory for the still.";
			return false;
		}
		still->id = ++nextStillId_;
		upload_.still = std::move(still);
		upload_.width = fields.width;
		upload_.height = fields.height;
		stillId = upload_.still->id;
		return true;
	}

	// Pieces arrive in order; a gap or a repeat fails the upload.
	bool Piece(uint64_t stillId, uint32_t offset, const uint8_t* bytes, uint32_t length,
		std::string& error)
	{
		if (!upload_.still || upload_.still->id != stillId)
		{
			error = "That still is no longer being received; send it again.";
			return false;
		}
		std::vector<uint8_t>& pixels = upload_.still->pixels;
		if (offset != upload_.received || length > pixels.size() - offset)
		{
			upload_ = {};
			error = "The still arrived out of order; send it again.";
			return false;
		}
		std::memcpy(pixels.data() + offset, bytes, length);
		upload_.received += length;
		return true;
	}

	// The finished still, under the live rate: a still's own rate would switch
	// the projector (REVIEW-REPEAT "Hook-site checks").
	StillPtr Finish(uint64_t stillId, const DisplayMode& live, std::string& error)
	{
		if (!upload_.still || upload_.still->id != stillId ||
			upload_.received != upload_.still->pixels.size())
		{
			error = "The still has not fully arrived; send it again.";
			return nullptr;
		}
		VideoState& state = *upload_.still->deviceState;
		state.displayMode = std::make_shared<DisplayMode>(upload_.width, upload_.height,
			false, live.TimeScale(), live.FrameDuration());
		if (state.BytesPerFrame() != upload_.still->pixels.size())
		{
			upload_ = {};
			error = "The still's size does not match its signal.";
			return nullptr;
		}
		StillPtr still = std::move(upload_.still);
		upload_ = {};
		return still;
	}

	// UI thread: -1 outside review, else the still's own tunnel decision.
	int TunnelOverride() const { return tunnelOverride_; }
	void SetTunnelOverride(int value) { tunnelOverride_ = value; }

	// ---- under the dialog's capture-state lock ----

	bool InReview() const { return static_cast<bool>(active_); }
	StillPtr Active() const { return active_; }

	// The next publication of the still's state starts (or switches) review.
	void PrepareEntry(StillPtr still, const VideoStateComPtr& liveNow)
	{
		if (!active_)
		{
			liveAtEntry_ = liveNow;
			heldLive_.Release();
		}
		entering_ = std::move(still);
		exiting_ = false;
	}

	// The state to publish to leave review; its publication ends review. Null
	// with review still on means there is no live state to return to: live
	// frames must never run under the still's state.
	VideoStateComPtr PrepareExit()
	{
		entering_.reset();
		if (!active_) return nullptr;
		exitState_ = heldLive_ ? heldLive_ : liveAtEntry_;
		exiting_ = static_cast<bool>(exitState_);
		return exitState_;
	}

	// A publication that never ran (stale capture run): drop what was pending.
	void CancelPending()
	{
		entering_.reset();
		if (exiting_) Clear();
	}

	Publication OnStatePublication(const VideoStateComPtr& state, CaptureRunToken epoch,
		VideoStateComPtr& publish)
	{
		if (entering_ && state.p == entering_->deviceState.p)
		{
			active_ = std::move(entering_);
			activeEpoch_ = epoch;
			return Publication::Publish;
		}
		if (exiting_ && state.p == exitState_.p)
		{
			Clear();
			return Publication::Publish;
		}
		if (!active_)
			return Publication::Publish;
		if (state && state->valid)
			heldLive_ = state;
		if (epoch != activeEpoch_)
		{
			activeEpoch_ = epoch;
			publish = active_->deviceState;
			return Publication::PublishStill;
		}
		return Publication::Hold;
	}

	// ---- card thread, outside the lock ----

	// The still with the live frame's counter and timing. `lease` holds the
	// still for as long as the renderer keeps the frame; the caller releases
	// its own reference after handing the frame on.
	static VideoFrame MakeFrame(const StillPtr& still, const VideoFrame& live, IUnknown*& lease)
	{
		lease = new Lease(still);
		VideoFrame frame(still->pixels.data(), live.GetCounter(), live.GetTimingTimestamp(),
			lease, live.GetCaptureTimingTimestamp());
		frame.SetCaptureArrivalTick(live.GetCaptureArrivalTick());
		frame.SetSourceDiscontinuity(live.IsSourceDiscontinuity());
		return frame;
	}

private:
	// One per swapped frame, as a card frame's buffer: the renderer's release
	// is the last, as VideoFrame::SourceBufferRelease expects.
	class Lease final : public IUnknown
	{
	public:
		explicit Lease(StillPtr still) : still_(std::move(still)) {}
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
		{
			if (!object) return E_POINTER;
			if (iid == IID_IUnknown)
			{
				*object = static_cast<IUnknown*>(this);
				AddRef();
				return S_OK;
			}
			*object = nullptr;
			return E_NOINTERFACE;
		}
		ULONG STDMETHODCALLTYPE AddRef() override { return ++count_; }
		ULONG STDMETHODCALLTYPE Release() override
		{
			const ULONG count = --count_;
			if (count == 0) delete this;
			return count;
		}

	private:
		std::atomic<ULONG> count_{ 1 };
		StillPtr still_;
	};

	struct Upload
	{
		std::shared_ptr<Still> still;
		uint32_t width = 0;
		uint32_t height = 0;
		size_t received = 0;
	};

	template <typename Enum>
	static bool Parse(const std::string& name, Enum first, Enum last, Enum& value)
	{
		for (int i = static_cast<int>(first); i <= static_cast<int>(last); ++i)
		{
			const TCHAR* text = ToString(static_cast<Enum>(i));
			if (text && name == std::string(CStringA(text).GetString()))
			{
				value = static_cast<Enum>(i);
				return true;
			}
		}
		return false;
	}

	void Clear()
	{
		active_.reset();
		entering_.reset();
		exiting_ = false;
		exitState_.Release();
		heldLive_.Release();
		liveAtEntry_.Release();
	}

	Upload upload_;
	uint64_t nextStillId_ = 0;
	int tunnelOverride_ = -1;

	StillPtr active_;
	StillPtr entering_;
	bool exiting_ = false;
	VideoStateComPtr exitState_;
	VideoStateComPtr heldLive_;
	VideoStateComPtr liveAtEntry_;
	CaptureRunToken activeEpoch_ = 0;
};
