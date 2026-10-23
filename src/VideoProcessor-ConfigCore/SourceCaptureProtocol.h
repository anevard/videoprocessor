#pragma once

#include "ConfigurationRpcProtocol.h"

#include <cstdint>
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
	};

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
		return operation == static_cast<uint16_t>(Operation::Arm) ||
			operation == static_cast<uint16_t>(Operation::Pull) ||
			operation == static_cast<uint16_t>(Operation::Release);
	}

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
}
