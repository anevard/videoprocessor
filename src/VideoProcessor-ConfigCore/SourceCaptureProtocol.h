#pragma once

#include "ConfigurationRpcProtocol.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Source capture over the configuration RPC: the second PC asks VP to copy the
// next admitted source frame, then pulls its sidecar and pixels in pieces. VP
// keeps the capture in memory only, until it is released or replaced.
//
// Every request body starts with ConfigurationCompatibilityVersion, as for the
// other operations. Operation numbers sit well above the configuration
// operations so that a new one there cannot collide with these.
namespace SourceCaptureProtocol
{
	enum class Operation : uint16_t
	{
		Arm = 0x0100,      // -> captureId (u64)
		Pull = 0x0101,     // captureId (u64), offset (u32), length (u32) -> see PullState
		Release = 0x0102,  // captureId (u64) -> empty
		// Review: a still sent once, then shown in place of the live frame.
		ReviewBegin = 0x0103,  // ReviewStill fields -> stillId (u64)
		ReviewPiece = 0x0104,  // stillId (u64), offset (u32), bytes (string) -> empty
		ReviewShow = 0x0105,   // stillId (u64) -> empty
		ReviewLive = 0x0106,   // nothing -> empty
	};
	// The frame codec refuses an operation outside its known range.
	static_assert(static_cast<uint16_t>(Operation::Arm) == ConfigurationRpcProtocol::FirstSourceCaptureOperation &&
		static_cast<uint16_t>(Operation::ReviewLive) == ConfigurationRpcProtocol::LastSourceCaptureOperation,
		"source capture operations must match ConfigurationRpcProtocol's known range");

	// Leaves room under MaximumPayloadBytes for the pull response's own fields.
	constexpr uint32_t MaximumPieceBytes = 3 * 1024 * 1024;

	// Pull response: state (u8), then
	//   Waiting: nothing more; pull again shortly.
	//   Ready:   frame bytes (u32), sidecar JSON (string), piece (string);
	//            a length of 0 asks for the sidecar only, and only that
	//            request carries it (empty otherwise).
	//   Failed:  reason (string); the capture is gone, arm again.
	enum class PullState : uint8_t
	{
		Waiting = 0,
		Ready = 1,
		Failed = 2,
	};

	inline bool IsOperation(uint16_t operation)
	{
		return operation >= ConfigurationRpcProtocol::FirstSourceCaptureOperation &&
			operation <= ConfigurationRpcProtocol::LastSourceCaptureOperation;
	}

	// A still's signal, as the sidecar records it: VP's own ToString names for
	// encoding, EOTF and colour space, so neither side needs the other's enums.
	struct ReviewStill
	{
		uint32_t byteSize = 0;
		uint32_t width = 0;
		uint32_t height = 0;
		std::string encoding;
		std::string eotf;
		std::string colorspace;
		bool invertedVertical = false;
		bool dolbyVisionTunnel = false;  // decode as a tunnel, whatever the live profile
		bool hasHdr10 = false;
		// Mastering min, max, MaxCLL, MaxFALL, primaries RGB (x, y), white (x, y).
		double hdr10[12] = {};
	};

	inline void Write64(std::vector<uint8_t>& output, uint64_t value)
	{
		ConfigurationRpcProtocol::Write32(output, static_cast<uint32_t>(value >> 32));
		ConfigurationRpcProtocol::Write32(output, static_cast<uint32_t>(value));
	}

	inline bool Read32At(const std::vector<uint8_t>& input, size_t& cursor,
		uint32_t& value)
	{
		if (cursor > input.size() || input.size() - cursor < 4)
			return false;
		value = ConfigurationRpcProtocol::Read32(input.data() + cursor);
		cursor += 4;
		return true;
	}

	inline bool Read64At(const std::vector<uint8_t>& input, size_t& cursor,
		uint64_t& value)
	{
		uint32_t high = 0, low = 0;
		if (!Read32At(input, cursor, high) || !Read32At(input, cursor, low))
			return false;
		value = (static_cast<uint64_t>(high) << 32) | low;
		return true;
	}

	// Request fields after the compatibility version, which
	// ConfigurationRpcClient::Request writes itself.
	inline std::vector<uint8_t> PullFields(uint64_t captureId,
		uint32_t offset, uint32_t length)
	{
		std::vector<uint8_t> body;
		Write64(body, captureId);
		ConfigurationRpcProtocol::Write32(body, offset);
		ConfigurationRpcProtocol::Write32(body, length);
		return body;
	}

	inline std::vector<uint8_t> ReleaseFields(uint64_t captureId)
	{
		std::vector<uint8_t> body;
		Write64(body, captureId);
		return body;
	}

	inline std::vector<uint8_t> ReviewBeginFields(const ReviewStill& still)
	{
		std::vector<uint8_t> body;
		ConfigurationRpcProtocol::Write32(body, still.byteSize);
		ConfigurationRpcProtocol::Write32(body, still.width);
		ConfigurationRpcProtocol::Write32(body, still.height);
		ConfigurationRpcProtocol::WriteString(body, still.encoding);
		ConfigurationRpcProtocol::WriteString(body, still.eotf);
		ConfigurationRpcProtocol::WriteString(body, still.colorspace);
		body.push_back(still.invertedVertical ? 1 : 0);
		body.push_back(still.dolbyVisionTunnel ? 1 : 0);
		body.push_back(still.hasHdr10 ? 1 : 0);
		for (const double value : still.hdr10)
		{
			uint64_t bits = 0;
			static_assert(sizeof(bits) == sizeof(value), "double must be 64-bit");
			std::memcpy(&bits, &value, sizeof(bits));
			Write64(body, bits);
		}
		return body;
	}

	inline bool ReadReviewBegin(const std::vector<uint8_t>& input, size_t& cursor,
		ReviewStill& still)
	{
		const auto flag = [&](bool& value)
		{
			if (cursor >= input.size() || input[cursor] > 1) return false;
			value = input[cursor++] != 0;
			return true;
		};
		if (!Read32At(input, cursor, still.byteSize) ||
			!Read32At(input, cursor, still.width) ||
			!Read32At(input, cursor, still.height) ||
			!ConfigurationRpcProtocol::ReadString(input, cursor, still.encoding) ||
			!ConfigurationRpcProtocol::ReadString(input, cursor, still.eotf) ||
			!ConfigurationRpcProtocol::ReadString(input, cursor, still.colorspace) ||
			!flag(still.invertedVertical) || !flag(still.dolbyVisionTunnel) ||
			!flag(still.hasHdr10))
			return false;
		for (double& value : still.hdr10)
		{
			uint64_t bits = 0;
			if (!Read64At(input, cursor, bits)) return false;
			std::memcpy(&value, &bits, sizeof(value));
		}
		return true;
	}

	inline std::vector<uint8_t> ReviewPieceFields(uint64_t stillId, uint32_t offset,
		const uint8_t* bytes, uint32_t length)
	{
		std::vector<uint8_t> body;
		body.reserve(20 + length);
		Write64(body, stillId);
		ConfigurationRpcProtocol::Write32(body, offset);
		ConfigurationRpcProtocol::Write32(body, length);
		body.insert(body.end(), bytes, bytes + length);
		return body;
	}
}
