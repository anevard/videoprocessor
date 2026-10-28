/*
 * Copyright(C) 2026 Andrew Nevard
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#include <pch.h>
#include <VideoFrame.h>
#include <VideoState.h>

#include <atomic>
#include <cstdlib>
#include <fstream>
#include <string>

#include "CPluginPassThroughVideoFrameFormatter.h"


namespace
{
	// Explicit per-process diagnostic opt-in, for the bit-exact check: save one
	// formatted frame without changing capture, settings or presentation.
	// Frames before this one are skipped so playback has settled.
	constexpr uint64_t CAPTURE_AFTER_FRAMES = 240;

	const std::string& CapturePath()
	{
		static const std::string path = [] {
			char* value = nullptr; size_t size = 0;
			_dupenv_s(&value, &size, "VP_PLUGIN_FRAME_CAPTURE_PATH");
			const std::string result = value ? value : "";
			std::free(value);
			return result;
		}();
		return path;
	}

	// One capture per process, across formatter instances.
	std::atomic<bool> g_captureClaimed{ false };
}


void CPluginPassThroughVideoFrameFormatter::OnVideoState(VideoStateComPtr& videoState)
{
	if (!videoState)
		throw std::runtime_error("Null video state is not allowed");

	if (videoState->videoFrameEncoding != VideoFrameEncoding::P010 &&
		videoState->videoFrameEncoding != VideoFrameEncoding::NV12)
		throw std::runtime_error("Can only handle source-plugin P010 or NV12 input");

	const uint32_t width = videoState->displayMode->FrameWidth();
	const uint32_t height = videoState->displayMode->FrameHeight();
	if (width == 0 || (width & 1) != 0 || height == 0 || (height & 1) != 0)
		throw std::runtime_error("4:2:0 input requires a positive, even width and height");

	m_encoding = videoState->videoFrameEncoding;
	m_width = width;
	m_height = height;
}


bool CPluginPassThroughVideoFrameFormatter::FormatVideoFrame(
	const VideoFrame& inFrame,
	BYTE* outBuffer)
{
	if (m_encoding == VideoFrameEncoding::UNKNOWN)
		throw std::runtime_error("encoding not known, call OnVideoState() first");

	const auto startTime = GetWallClockTime();

	// The frame's cues ride on its source buffer.
	m_cues = {};
	if (IUnknown* sourceBuffer = inFrame.GetSourceBuffer())
	{
		ISourceCueCarrier* carrier = nullptr;
		if (SUCCEEDED(sourceBuffer->QueryInterface(__uuidof(ISourceCueCarrier),
			reinterpret_cast<void**>(&carrier))))
		{
			m_cues = carrier->GetSourceCues();
			carrier->Release();
		}
	}

	// Y rows plus half as many CbCr rows, each width samples wide.
	const size_t samples = static_cast<size_t>(m_width) * m_height * 3 / 2;

	if (m_encoding == VideoFrameEncoding::P010)
	{
		memcpy(outBuffer, inFrame.GetData(), samples * sizeof(uint16_t));
	}
	else
	{
		// NV12: each 8-bit code goes into the high byte, low byte zero.
		const uint8_t* src = static_cast<const uint8_t*>(inFrame.GetData());
		uint16_t* dst = reinterpret_cast<uint16_t*>(outBuffer);
		for (size_t i = 0; i < samples; i++)
			dst[i] = static_cast<uint16_t>(src[i]) << 8;
	}

	const auto endTime = GetWallClockTime();
	const uint64_t conversionTimeUs = (endTime - startTime) / 10;
	m_performanceWindow.AddSample(static_cast<double>(conversionTimeUs));

	++m_framesFormatted;
	if (m_framesFormatted > CAPTURE_AFTER_FRAMES && !CapturePath().empty() &&
		!g_captureClaimed.exchange(true))
		CaptureFrame(inFrame, outBuffer);

	return true;
}


// Writes the formatter's output exactly as the renderer receives it, plus a
// sidecar. Diagnostic only: the write blocks this one frame.
void CPluginPassThroughVideoFrameFormatter::CaptureFrame(
	const VideoFrame& inFrame,
	const BYTE* outBuffer) const
{
	const std::string& path = CapturePath();
	const VideoFrameFormatterOutputContract contract = GetOutputContract();
	const LONG bytes = GetOutFrameSize();

	std::ofstream pixels(path, std::ios::binary);
	pixels.write(reinterpret_cast<const char*>(outBuffer), bytes);

	std::ofstream metadata(path + ".txt");
	metadata
		<< "encoding=" << (m_encoding == VideoFrameEncoding::P010 ? "P010" : "NV12") << '\n'
		<< "width=" << m_width << '\n'
		<< "height=" << m_height << '\n'
		<< "layout=Y plane then interleaved CbCr at half height, 16-bit little-endian words, no row padding\n"
		<< "bytes=" << bytes << '\n'
		<< "color_depth=" << static_cast<unsigned>(contract.colorDepth) << '\n'
		<< "bit_shift=" << static_cast<unsigned>(contract.bitShift) << '\n'
		<< "sample_range=" << (contract.sampleRange == VideoFrameSampleRange::LIMITED ? "limited" : "other") << '\n'
		<< "source_counter=" << inFrame.GetCounter() << '\n'
		<< "frames_formatted=" << m_framesFormatted << '\n'
		<< "written=" << (pixels.good() ? "ok" : "failed") << '\n';
}


LONG CPluginPassThroughVideoFrameFormatter::GetOutFrameSize() const
{
	assert(m_width > 0 && m_height > 0);
	return static_cast<LONG>(static_cast<size_t>(m_width) * m_height * 3 / 2 * sizeof(uint16_t));
}


VideoFrameFormatterOutputContract
CPluginPassThroughVideoFrameFormatter::GetOutputContract() const
{
	switch (m_encoding)
	{
	case VideoFrameEncoding::P010:
		return { VideoFrameSampleRange::LIMITED, 10, 6 };
	case VideoFrameEncoding::NV12:
		return { VideoFrameSampleRange::LIMITED, 8, 8 };
	default:
		return {};
	}
}
