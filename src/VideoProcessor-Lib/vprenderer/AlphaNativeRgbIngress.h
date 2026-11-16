#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <VideoConversionOverride.h>
#include <VideoFrameEncoding.h>

// A regular packed RGB layout is safe to hand to libplacebo's generic image
// uploader. Keeping this table separate makes byte order and bit placement
// independently testable from the D3D11 renderer.
struct AlphaNativeRgbLayout
{
	uint64_t masks[4]{};
	bool swapped = false;
	bool limitedRange = false;
	int bitDepth = 0;
	const char* label = nullptr;
};

inline bool GetAlphaNativeRgbLayout(VideoFrameEncoding encoding,
	AlphaNativeRgbLayout& layout)
{
	layout = {};
	switch (encoding)
	{
	case VideoFrameEncoding::BGRA_8BIT:
		layout.masks[0] = 0x00FF0000; // R in [B G R A]
		layout.masks[1] = 0x0000FF00;
		layout.masks[2] = 0x000000FF;
		layout.masks[3] = 0xFF000000;
		layout.bitDepth = 8;
		layout.label = "Native BGRA -> RGB";
		return true;
	case VideoFrameEncoding::ARGB_8BIT:
		layout.masks[0] = 0x0000FF00; // R in [A R G B]
		layout.masks[1] = 0x00FF0000;
		layout.masks[2] = 0xFF000000;
		layout.masks[3] = 0x000000FF;
		layout.bitDepth = 8;
		layout.label = "Native ARGB -> RGB";
		return true;
	case VideoFrameEncoding::R210:
		layout.masks[0] = 0x3FF00000; // [00 R10 G10 B10], big endian
		layout.masks[1] = 0x000FFC00;
		layout.masks[2] = 0x000003FF;
		layout.swapped = true;
		layout.limitedRange = true;
		layout.bitDepth = 10;
		layout.label = "Native r210 -> RGB";
		return true;
	case VideoFrameEncoding::R10b:
		layout.masks[0] = 0xFFC00000; // [R10 G10 B10 00], big endian
		layout.masks[1] = 0x003FF000;
		layout.masks[2] = 0x00000FFC;
		layout.swapped = true;
		layout.limitedRange = true;
		layout.bitDepth = 10;
		layout.label = "Native R10b -> RGB";
		return true;
	case VideoFrameEncoding::R10l:
		layout.masks[0] = 0xFFC00000; // [R10 G10 B10 00], little endian
		layout.masks[1] = 0x003FF000;
		layout.masks[2] = 0x00000FFC;
		layout.limitedRange = true;
		layout.bitDepth = 10;
		layout.label = "Native R10l -> RGB";
		return true;
	default:
		return false;
	}
}

inline bool AlphaCanUseNativeRgbUpload(VideoFrameEncoding encoding,
	bool dolbyVisionTunnel = false)
{
	AlphaNativeRgbLayout layout;
	// libplacebo's generic limited-RGB representation uses conventional
	// 10-bit 64-940 levels. DeckLink r210 has a distinct 64-960 interval, so
	// normalize it through the range-aware P010 formatter instead.
	return !dolbyVisionTunnel && encoding != VideoFrameEncoding::R210 &&
		GetAlphaNativeRgbLayout(encoding, layout);
}

inline bool AlphaUsesP210Ingress(VideoFrameEncoding encoding,
	VideoConversionOverride conversionOverride) noexcept
{
	return conversionOverride ==
			VideoConversionOverride::VIDEOCONVERSION_NONE &&
		(encoding == VideoFrameEncoding::UYVY ||
			encoding == VideoFrameEncoding::HDYC ||
			encoding == VideoFrameEncoding::V210 ||
			encoding == VideoFrameEncoding::P210);
}

// libplacebo's generic uploader matches a packed layout against the GPU's
// texture formats bit for bit, padding included. D3D11's one packed 10-bit
// format, R10G10B10A2, holds red from bit 0 and its pad on top; R10l and R10b
// hold red at the top and two pad bits at the bottom, so no format matches
// them. Such a frame is repacked on the CPU into the R10G10B10A2 word before
// upload; every other layout uploads as it is.
inline bool AlphaNativeRgbNeedsRepack(const AlphaNativeRgbLayout& layout) noexcept
{
	return layout.masks[0] == 0xFFC00000 && layout.masks[1] == 0x003FF000 &&
		layout.masks[2] == 0x00000FFC && layout.masks[3] == 0;
}

// The layout libplacebo is given for a frame AlphaRepackR10ToRgb10a2 has
// repacked: red, green and blue in bits 0-9, 10-19 and 20-29, native byte order.
inline AlphaNativeRgbLayout AlphaNativeRgbRepackedLayout(
	const AlphaNativeRgbLayout& layout) noexcept
{
	AlphaNativeRgbLayout repacked = layout;
	repacked.masks[0] = 0x000003FF;
	repacked.masks[1] = 0x000FFC00;
	repacked.masks[2] = 0x3FF00000;
	repacked.masks[3] = 0;
	repacked.swapped = false;
	return repacked;
}

// Repacks R10l (little-endian) or R10b (big-endian, `swapped`) pixels, each
// word R10:G10:B10:00 from the top, into R10G10B10A2 words: red in bits 0-9,
// green 10-19, blue 20-29, the top two bits zero. Component codes are copied
// exactly. `target` receives `width` words per row; the source is never
// written.
inline void AlphaRepackR10ToRgb10a2(const uint8_t* source, size_t sourceStride,
	uint32_t* target, uint32_t width, uint32_t height, bool swapped) noexcept
{
	for (uint32_t row = 0; row < height; ++row)
	{
		const uint8_t* input = source + static_cast<size_t>(row) * sourceStride;
		uint32_t* output = target + static_cast<size_t>(row) * width;
		if (swapped)
		{
			for (uint32_t pixel = 0; pixel < width; ++pixel)
			{
				uint32_t word;
				std::memcpy(&word, input + static_cast<size_t>(pixel) * 4, sizeof(word));
				word = (word >> 24) | ((word >> 8) & 0x0000FF00u) |
					((word << 8) & 0x00FF0000u) | (word << 24);
				output[pixel] = (word >> 22) | ((word >> 2) & 0x000FFC00u) |
					((word << 18) & 0x3FF00000u);
			}
		}
		else
		{
			for (uint32_t pixel = 0; pixel < width; ++pixel)
			{
				uint32_t word;
				std::memcpy(&word, input + static_cast<size_t>(pixel) * 4, sizeof(word));
				output[pixel] = (word >> 22) | ((word >> 2) & 0x000FFC00u) |
					((word << 18) & 0x3FF00000u);
			}
		}
	}
}
