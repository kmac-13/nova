#pragma once
#ifndef KMAC_FLARE_TLV_H
#define KMAC_FLARE_TLV_H

#include <cstdint>

namespace kmac::flare
{

// Type Length Value
enum class TlvType : uint16_t
{
	Invalid             = 0,

	// record framing
	RecordBegin         = 1,
	RecordSize          = 2,
	RecordStatus        = 3,  // Complete/Partial/Truncated status
	SequenceNumber      = 4,  // monotonic sequence for ordering

	// metadata
	TimestampNs         = 10,
	TagId               = 11,
	FileName            = 12,
	LineNumber		= 13,
	FunctionName        = 14,
	ProcessId           = 15,  // process ID
	ThreadId            = 16,  // thread ID

	// payload
	MessageBytes        = 20,
	MessageTruncated    = 21,  // indicates message was truncated due to buffer limits

	// crash context (30-39 reserved for crash diagnostics)
	// LoadBaseAddress: runtime load base of the main executable (uint64_t).
	// Required to convert stack frame addresses to static addresses for
	// symbolization when the binary is position-independent (PIE/ASLR).
	// Captured once at EmergencySink construction, not per-record.
	// static_addr = frame_addr - load_base_address
	LoadBaseAddress     = 30,

	// StackFrames: tightly-packed array of uint64_t return addresses,
	// little-endian, innermost frame first, frame count = tlv_length / 8.
	StackFrames         = 31,

	// record terminator
	RecordEnd           = 0xFFFF
};

/**
 * @brief Record status indicator.
 *
 * Helps readers distinguish between complete records, known truncations,
 * and potentially torn writes.
 */
enum class RecordStatus : uint8_t
{
	Unknown = 0,        // default - status unknown (likely torn write if no end marker)
	InProgress = 1,     // write in progress (should never see this unless torn)
	Complete = 2,       // write completed successfully with end marker
	Truncated = 3       // known truncation (message too large for buffer)
};

static constexpr uint64_t FLARE_MAGIC = 0x4B4D41435F464C52ULL; // "KMAC_FLR"

static constexpr uint32_t MAX_RECORD_SIZE = 64 * 1024;

} // namespace kmac::flare

#endif // KMAC_FLARE_TLV_H
