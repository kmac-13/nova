#pragma once
#ifndef KMAC_FLARE_RECORD_H
#define KMAC_FLARE_RECORD_H

#include <kmac/nova/platform/array.h>

#include <cstdint>
#include <cstddef>

namespace kmac {
namespace flare {

/**
 * @brief Parsed forensic record (Reader output).
 *
 * This structure is used by Reader to return parsed TLV data.
 * It is NOT used by EmergencySink (which works directly with nova::Record).
 *
 * Design:
 * - fixed-size arrays (no heap allocation)
 * - null-terminated strings for convenience
 * - truncation if original data exceeds buffer size
 *
 * Limitations:
 * - file name: max 256 chars
 * - function name: max 256 chars
 * - message: max 4096 bytes
 * - stack trace: max 32 frames (raw return addresses)
 */
struct Record
{
	// string data (fixed buffers, no allocation)
	static constexpr std::size_t MAX_FILENAME_LEN = 256;
	static constexpr std::size_t MAX_FUNCTION_LEN = 256;
	static constexpr std::size_t MAX_MESSAGE_LEN = 4096;

	// maximum number of stack frames captured per record
	static constexpr std::size_t MAX_STACK_FRAMES = 32;

	// record metadata
	std::uint64_t sequenceNumber = 0;  // monotonic sequence for ordering
	std::uint8_t status = 0;           // RecordStatus enum value

	// timestamp (nanoseconds)
	std::uint64_t timestampNs = 0;

	// tag hash ID
	std::uint64_t tagId = 0;

	// source location
	std::uint32_t line = 0;

	// process/thread info
	std::uint32_t processId = 0;
	std::uint32_t threadId = 0;

	// message flags
	bool messageTruncated = false;  // true if message was truncated

	kmac::nova::platform::Array< char, MAX_FILENAME_LEN > file { };
	kmac::nova::platform::Array< char, MAX_FUNCTION_LEN > function { };
	kmac::nova::platform::Array< char, MAX_MESSAGE_LEN > message { };

	// actual lengths (may be less than buffer size)
	std::size_t fileLen = 0;
	std::size_t functionLen = 0;
	std::size_t messageLen = 0;

	// stack trace (raw return addresses, innermost frame first)
	// only populated when EmergencySink is constructed with captureStackTrace=true
	kmac::nova::platform::Array< std::uint64_t, MAX_STACK_FRAMES > stackFrames { };
	std::size_t stackFrameCount = 0;  // number of valid entries in stackFrames

	// runtime load base address of the main executable, written on every record
	// use to convert frame addresses to static addresses for addr2line / llvm-symbolizer:
	//   static_addr = frame_addr - loadBaseAddress
	// will be 0 on unsupported platforms or non-PIE binaries
	std::uint64_t loadBaseAddress = 0;

	// fault context - only populated when captured via signal handler
	// (captureRegisters=true on EmergencySinkBase, triggered by signal delivery)
	std::uint64_t faultAddress = 0;  // si_addr from siginfo_t; 0 if not a fault record
	std::uint64_t aslrOffset = 0;    // ASLR slide for the main executable
	bool hasFaultAddress = false;    // false when faultAddress is not meaningful

	// CPU registers at the point of fault, architecture layout identified by registerLayout.
	// Maximum size covers the largest supported layout (ARM64: 34 x uint64_t = 272 bytes).
	static constexpr std::size_t MAX_REGISTERS = 34;
	kmac::nova::platform::Array< std::uint64_t, MAX_REGISTERS > registers { };
	std::size_t registerCount = 0;   // number of valid entries in registers
	std::uint8_t registerLayout = 0; // RegisterLayoutId value

	/**
	 * @brief Reset record to empty state.
	 */
	void clear() noexcept;

	/**
	 * @brief Get human-readable status string.
	 */
	const char* statusString() const noexcept;
};

} // namespace flare
} // namespace kmac

#endif // KMAC_FLARE_RECORD_H
