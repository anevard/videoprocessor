#pragma once

#include <cstddef>
#include <cstdint>

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
// texture formats bit for bit, padding included. D3D11 has one packed 10-bit
// format, R10G10B10A2: three components from bit 0 and the pad on top. The
// DeckLink 10-bit RGB words put their two pad bits at the bottom, so no format
// ever matches them and every upload fails. Such a word is shifted down by its
// low pad before upload and the masks move with it; the components then sit
// where the format has them. A layout with no low pad (BGRA, ARGB) is
// uploaded as it is.
inline int AlphaNativeRgbLowPadBits(const AlphaNativeRgbLayout& layout) noexcept
{
	uint64_t used = layout.masks[0] | layout.masks[1] | layout.masks[2] |
		layout.masks[3];
	if (!used)
		return 0;
	int pad = 0;
	while (!(used & 1u))
	{
		used >>= 1;
		++pad;
	}
	return pad;
}

// The layout libplacebo is given for a frame that AlphaRepackNativeRgbWords
// has shifted: masks moved down by the low pad, byte order already native.
inline AlphaNativeRgbLayout AlphaNativeRgbUploadLayout(
	const AlphaNativeRgbLayout& layout) noexcept
{
	AlphaNativeRgbLayout upload = layout;
	const int pad = AlphaNativeRgbLowPadBits(layout);
	if (pad == 0)
		return upload;
	for (uint64_t& mask : upload.masks)
		mask >>= pad;
	upload.swapped = false;
	return upload;
}

// Repacks `count` 32-bit pixel words for upload: big-endian words are
// byte-swapped first, then every word is shifted down by `pad` bits. The
// pad bits leave at the bottom; the top `pad` bits read zero.
inline void AlphaRepackNativeRgbWords(const uint8_t* source, uint32_t* target,
	size_t count, bool swapped, int pad) noexcept
{
	for (size_t i = 0; i < count; ++i, source += 4)
	{
		const uint32_t word = swapped ?
			(static_cast<uint32_t>(source[0]) << 24) |
				(static_cast<uint32_t>(source[1]) << 16) |
				(static_cast<uint32_t>(source[2]) << 8) |
				static_cast<uint32_t>(source[3]) :
			static_cast<uint32_t>(source[0]) |
				(static_cast<uint32_t>(source[1]) << 8) |
				(static_cast<uint32_t>(source[2]) << 16) |
				(static_cast<uint32_t>(source[3]) << 24);
		target[i] = word >> pad;
	}
}
