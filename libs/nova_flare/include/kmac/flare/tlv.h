#pragma once
#ifndef KMAC_FLARE_TLV_H
#define KMAC_FLARE_TLV_H

#include <cstdint>

namespace kmac {
namespace flare {

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
	//
	// TLV ordering within a crash record follows the dependency chain a
	// reader needs to interpret addresses:
	//   FaultAddress    - what faulted (si_addr; present only in signal records)
	//   LoadBaseAddress - where the binary is loaded at runtime
	//   AslrOffset      - ASLR slide (equals LoadBaseAddress for PIE; stored
	//                     explicitly so readers need no binary at read time)
	//   StackFrames     - runtime return addresses (subtract AslrOffset for
	//                     static/file-relative addresses for addr2line)
	//   RegisterLayout  - identifies the register array layout that follows
	//   CpuRegisters    - register snapshot at the point of fault

	// FaultAddress: memory address that triggered the fault (uint64_t).
	// Sourced from siginfo_t::si_addr.  Present only when a signal handler
	// captures the siginfo_t context (e.g. SIGSEGV, SIGBUS, SIGFPE, SIGILL).
	// Absent for SIGABRT records where si_addr is not meaningful.
	FaultAddress        = 30,

	// LoadBaseAddress: runtime load base of the main executable (uint64_t).
	// Required to convert stack frame addresses to static addresses for
	// symbolization when the binary is position-independent (PIE/ASLR).
	// Captured once at EmergencySink construction, not per-record.
	// static_addr = frame_addr - load_base_address
	LoadBaseAddress     = 31,

	// AslrOffset: the Address Space Layout Randomization (ASLR) slide applied
	// to the main executable (uint64_t).  For Position Independent Executable
	// (PIE) binaries on Linux/Android/macOS this equals LoadBaseAddress.
	// Stored explicitly so flare_reader.py can relocate addresses without
	// needing the binary present at read time.
	AslrOffset          = 32,

	// StackFrames: tightly-packed array of uint64_t return addresses,
	// little-endian, innermost frame first, frame count = tlv_length / 8.
	StackFrames         = 33,

	// RegisterLayout: identifies the architecture and register ordering for
	// the CpuRegisters TLV that follows (uint8_t).  See RegisterLayoutId enum.
	// Written immediately before CpuRegisters so readers can decode the array
	// without inspecting the binary or knowing the target architecture.
	RegisterLayout      = 34,

	// CpuRegisters: tightly-packed array of uint64_t register values,
	// little-endian, register count = tlv_length / 8.
	// Architecture-specific field order is documented in RegisterLayoutId.
	// Supported layouts:
	//   x86-64: rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp, r8-r15, rip, rflags
	//           (18 registers, 144 bytes)
	//   ARM64:  x0-x30, sp, pc, pstate
	//           (34 registers, 272 bytes)
	//   ARM32:  r0-r15, cpsr
	//           (17 registers, 136 bytes)
	CpuRegisters        = 35,

	// record terminator
	RecordEnd           = 0xFFFF
};

/**
 * @brief Identifies the architecture-specific register layout in a CpuRegisters TLV.
 *
 * Written as the value of a RegisterLayout TLV immediately preceding the
 * CpuRegisters TLV so readers can decode registers without inspecting the binary.
 */
enum class RegisterLayoutId : uint8_t
{
	Unknown = 0,
	X86_64  = 1,  ///< see TlvType::CpuRegisters for field order
	Arm64   = 2,  ///< see TlvType::CpuRegisters for field order
	Arm32   = 3,  ///< see TlvType::CpuRegisters for field order
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

} // namespace flare
} // namespace kmac

#endif // KMAC_FLARE_TLV_H
