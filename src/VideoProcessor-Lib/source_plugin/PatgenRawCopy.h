#pragma once

// A source plugin's raw output (VP_SOURCE_STATE_RAW_OUTPUT, the pattern
// generator's "Raw output: no VP processing", ledger A255 and A356): the
// R10L codes reach the output 1:1 with no GPU pass of VP's own. The CPU
// writes each pixel's exact output codes as one word of the backbuffer's own
// layout; the renderer uploads those words into a texture of the
// backbuffer's own format and copies that texture to the backbuffer, a
// same-format, same-size copy that D3D11 does without a shader. Nothing is
// compiled on a live frame.
//
// A source word is R10l: little-endian, red in bits 22-31, green in 12-21,
// blue in 2-11, bits 0-1 unused. A 10-bit output stores each 10-bit code.
// An 8-bit output stores the 8-bit code the generator sends as code << 2;
// any other word lands on its nearest 8-bit code. Alpha is opaque, as in
// every frame VP renders.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace PatgenRawCopy
{
	// Where red, green, blue and the opaque alpha sit in one output word.
	struct WordLayout
	{
		int bits = 0;               // red, green and blue: 10 or 8
		uint32_t shift[3] = {};     // red, green, blue
		uint32_t alpha = 0;         // the opaque alpha, in place
	};

	// The layout of a texel of `channels` channels in `texelSize` bytes,
	// given libplacebo's pl_fmt fields for it: `hostBits` per channel in
	// memory order, the first from bit 0 of a little-endian word, and
	// `sampleOrder`, the component each channel holds (0 red, 1 green,
	// 2 blue, 3 alpha). False unless the texel is red, green, blue and alpha
	// filling 32 bits, red, green and blue all 10 bits or all 8.
	inline bool LayoutFor(int channels, size_t texelSize, const int hostBits[4],
		const int sampleOrder[4], WordLayout& layout)
	{
		layout = {};
		if (channels != 4 || texelSize != 4)
			return false;
		int depth[4] = {};
		uint32_t shift[4] = {};
		bool seen[4] = {};
		uint32_t offset = 0;
		for (int channel = 0; channel < 4; ++channel)
		{
			const int component = sampleOrder[channel];
			if (component < 0 || component > 3 || seen[component] ||
				hostBits[channel] <= 0 || hostBits[channel] > 16)
				return false;
			seen[component] = true;
			depth[component] = hostBits[channel];
			shift[component] = offset;
			offset += static_cast<uint32_t>(hostBits[channel]);
		}
		if (offset != 32 || depth[0] != depth[1] || depth[1] != depth[2] ||
			(depth[0] != 10 && depth[0] != 8))
			return false;
		layout.bits = depth[0];
		for (int component = 0; component < 3; ++component)
			layout.shift[component] = shift[component];
		layout.alpha = ((1u << depth[3]) - 1u) << shift[3];
		return true;
	}

	// The nearest 8-bit code to a 10-bit one: code << 2 gives code back.
	inline uint32_t Code8(uint32_t code10)
	{
		const uint32_t code = (code10 + 2u) >> 2;
		return code > 255u ? 255u : code;
	}

	// Writes `width` x `height` output words, `width` to a row, rows packed,
	// from `height` rows of R10l words `sourceStride` bytes apart. The source
	// is never written.
	inline void WriteWords(const uint8_t* source, size_t sourceStride,
		uint32_t width, uint32_t height, const WordLayout& layout, uint32_t* target)
	{
		const uint32_t red = layout.shift[0];
		const uint32_t green = layout.shift[1];
		const uint32_t blue = layout.shift[2];
		const uint32_t alpha = layout.alpha;
		for (uint32_t row = 0; row < height; ++row)
		{
			const uint8_t* input = source + static_cast<size_t>(row) * sourceStride;
			uint32_t* output = target + static_cast<size_t>(row) * width;
			if (layout.bits == 10)
			{
				for (uint32_t pixel = 0; pixel < width; ++pixel)
				{
					uint32_t word;
					std::memcpy(&word, input + static_cast<size_t>(pixel) * 4, sizeof(word));
					output[pixel] = ((word >> 22) << red) |
						(((word >> 12) & 0x3FFu) << green) |
						(((word >> 2) & 0x3FFu) << blue) | alpha;
				}
			}
			else
			{
				for (uint32_t pixel = 0; pixel < width; ++pixel)
				{
					uint32_t word;
					std::memcpy(&word, input + static_cast<size_t>(pixel) * 4, sizeof(word));
					output[pixel] = (Code8(word >> 22) << red) |
						(Code8((word >> 12) & 0x3FFu) << green) |
						(Code8((word >> 2) & 0x3FFu) << blue) | alpha;
				}
			}
		}
	}
}
