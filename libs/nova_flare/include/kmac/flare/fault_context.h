#pragma once
#ifndef KMAC_FLARE_FAULT_CONTEXT_H
#define KMAC_FLARE_FAULT_CONTEXT_H

#include <kmac/nova/platform/array.h>

#include <cstddef>
#include <cstdint>

namespace kmac {
namespace flare {

/**
 * @brief Fault context captured from a hardware fault or signal handler.
 *
 * Passed to EmergencySinkBase::processWithFaultContext() to write crash
 * records that include the faulting address, ASLR offset, and CPU registers.
 *
 * On POSIX targets this struct is populated by SignalHandler from siginfo_t
 * and ucontext_t.  On bare-metal targets it is populated by
 * BareMetalFaultHandler from the hardware exception frame and SCB registers.
 *
 * All fields are safe to populate from within a fault handler context
 * (no heap allocation, no OS calls).
 */
struct FaultContext
{
	std::uint64_t faultAddress = 0;    // faulting memory address; 0 if unavailable
	bool hasFaultAddress = false;      // true when faultAddress is meaningful

	std::uint64_t aslrOffset = 0;      // ASLR slide for the main executable
								// always 0 on bare-metal (fixed load address)

	// CPU registers at the point of fault, layout is architecture-specific (see
	// RegisterLayoutId in tlv.h); maximum size covers the largest supported
	// layout (ARM64: 34 registers)
	static constexpr std::size_t MAX_REGISTERS = 34;
	kmac::nova::platform::Array< std::uint64_t, MAX_REGISTERS > registers {};
	std::size_t registerCount = 0;
	std::uint8_t layoutId = 0;         // RegisterLayoutId value
};

} // namespace flare
} // namespace kmac

#endif // KMAC_FLARE_FAULT_CONTEXT_H
