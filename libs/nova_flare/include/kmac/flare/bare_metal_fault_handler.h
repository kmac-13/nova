#pragma once
#ifndef KMAC_FLARE_BARE_METAL_FAULT_HANDLER_H
#define KMAC_FLARE_BARE_METAL_FAULT_HANDLER_H

// BareMetalFaultHandler is only available in bare-metal builds.
#if defined( NOVA_BARE_METAL )

// ============================================================================
// Vector table installation macros
//
// By default (neither macro defined) BareMetalFaultHandler provides only the
// static handleFault() method.  The user wires it manually:
//
//   extern "C" void HardFault_Handler( void )
//   {
//       kmac::flare::BareMetalFaultHandler::handleFault();
//   }
//
// This is the safest option: it is explicit, auditable, and has no link
// conflicts with vendor Cortex Microcontroller Software Interface Standard
// (CMSIS) startup files or RTOS port layers.
//
// FLARE_FAULT_HANDLER_STRONG
//   Flare emits a strong extern "C" HardFault_Handler symbol.  The linker
//   picks it up automatically.  Use this when starting a project from scratch
//   with no pre-existing HardFault_Handler.  Will produce a multiple-definition
//   link error if any other translation unit also defines HardFault_Handler.
//
// FLARE_FAULT_HANDLER_WEAK
//   Flare emits a weak extern "C" HardFault_Handler symbol.  When the user's
//   startup file already provides a weak HardFault_Handler (as CMSIS startup
//   files do), two weak definitions exist and the linker picks whichever
//   object file appears first in the link order - Flare may silently not run.
//   Only use this option when you can verify the link order or confirm no
//   other weak definition is present.
//
// Defining both is a configuration error.
// ============================================================================

#if defined( FLARE_FAULT_HANDLER_STRONG ) && defined( FLARE_FAULT_HANDLER_WEAK )
#error "Define at most one of FLARE_FAULT_HANDLER_STRONG | FLARE_FAULT_HANDLER_WEAK"
#endif

#include "emergency_sink.h"
#include "fault_context.h"

namespace kmac {
namespace flare {

/**
 * @brief Cortex-M hardware fault handler for Flare crash capture.
 *
 * BareMetalFaultHandler reads the hardware exception frame pushed by the
 * Cortex-M processor onto the stack before branching to the fault vector,
 * optionally reads the faulting address from the System Control Block (SCB)
 * fault status registers (MMFAR / BFAR), populates a FaultContext, and calls
 * EmergencySinkBase::processWithFaultContext() on the registered sink.
 *
 * Wiring (option C - manual, recommended):
 * @code
 *   // in your startup file or application init
 *   extern "C" void HardFault_Handler( void )
 *   {
 *       kmac::flare::BareMetalFaultHandler::handleFault();
 *   }
 * @endcode
 *
 * Alternatively define FLARE_FAULT_HANDLER_STRONG or FLARE_FAULT_HANDLER_WEAK
 * before including this header (or via CMake) to have Flare emit the symbol
 * automatically.  See the macro documentation above for trade-offs.
 *
 * Register capture:
 *   The Cortex-M processor automatically stacks r0-r3, r12, LR, PC, and xPSR
 *   before entering the fault handler, giving us 8 registers without any
 *   assembly.  Callee-saved r4-r11 are not included unless the vector entry
 *   stub saves them in an assembly prologue before branching to handleFault().
 *   The captured registers are written using the CortexM layout ID (4) so
 *   flare_reader.py can label each field correctly.
 *
 * Fault address:
 *   MMFAR is read when the MMARVALID bit in CFSR is set (MemManage fault with
 *   a valid address).  BFAR is read when the BFARVALID bit is set (BusFault
 *   with a valid address).  When neither is valid, hasFaultAddress is false
 *   and the PC from the exception frame serves as the nearest useful address.
 *
 * ASLR offset:
 *   Always 0 on bare-metal; the binary is loaded at the address specified in
 *   the linker script and the processor executes from that fixed location.
 *   Stack frame addresses can be passed directly to addr2line without
 *   relocation.
 *
 * Usage:
 * @code
 *   // place in .noinit so contents survive a warm reset
 *   __attribute__( ( section( ".noinit" ) ) )
 *   static std::uint8_t crashBuf[ 4096 ];
 *
 *   static kmac::flare::RamWriter ramWriter( crashBuf, sizeof( crashBuf ) );
 *   static kmac::flare::EmergencySink< 512 > emergencySink( &ramWriter );
 *
 *   // call once at startup, before any fault can occur
 *   kmac::flare::BareMetalFaultHandler::install( &emergencySink );
 * @endcode
 *
 * @note All static methods are async-signal-safe and allocate nothing on the heap.
 * @note This class is non-instantiable; all methods are static.
 * @note Only available when NOVA_BARE_METAL is defined.
 */
class BareMetalFaultHandler
{
private:
	// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
	static EmergencySinkBase* _sink;

public:
	BareMetalFaultHandler() = delete;

	/**
	 * @brief Register the sink that fault records will be written to.
	 *
	 * Call once at startup, before any fault can occur and before the
	 * vector table entry is live.  The sink must remain valid for the
	 * lifetime of the application.
	 *
	 * @param sink EmergencySink instance to receive fault records
	 */
	static void install( EmergencySinkBase* sink ) noexcept;

	/**
	 * @brief Entry point for the hardware fault handler.
	 *
	 * Call this from the vector table entry point (HardFault_Handler,
	 * MemManage_Handler, BusFault_Handler, or UsageFault_Handler) after
	 * the processor has stacked the exception frame.
	 *
	 * Reads the active SP to locate the exception frame, extracts the
	 * stacked registers, checks SCB fault status registers for a valid
	 * fault address, populates a FaultContext, and calls
	 * processWithFaultContext() on the registered sink.
	 *
	 * If no sink has been installed, this is a no-op (safe to call early).
	 *
	 * @note Does not return; calls __builtin_trap() after writing the record
	 *       so the debugger can halt and the system can be reset.
	 */
	static void handleFault() noexcept;

private:
	/**
	 * @brief Read the active SP and extract the Cortex-M exception frame.
	 *
	 * The Cortex-M hardware pushes the following frame before branching to
	 * the fault vector (offsets from the pre-fault SP):
	 *
	 *   offset 0x00  r0
	 *   offset 0x04  r1
	 *   offset 0x08  r2
	 *   offset 0x0C  r3
	 *   offset 0x10  r12
	 *   offset 0x14  lr  (return address / EXC_RETURN)
	 *   offset 0x18  pc  (address of the faulting instruction)
	 *   offset 0x1C  xpsr
	 *
	 * On entry to the fault vector the processor has already switched to
	 * MSP (if using PSP for tasks).  We read MSP to get the stacked frame
	 * address.  If the fault was from a task using PSP, the frame is on
	 * PSP and LR will be EXC_RETURN 0xFFFFFFFD; we detect that and use PSP
	 * instead.
	 *
	 * @param ctx output FaultContext to populate with stacked registers
	 */
	static void extractExceptionFrame( FaultContext& ctx ) noexcept;

	/**
	 * @brief Check SCB fault status registers and extract a valid fault address.
	 *
	 * Reads CFSR (Configurable Fault Status Register) to determine whether
	 * MMFAR or BFAR contains a valid fault address, and copies the address
	 * into ctx if so.
	 *
	 * MMFAR is valid when SCB_CFSR_MMARVALID is set (MemManage fault with
	 * a valid data / instruction address).  BFAR is valid when
	 * SCB_CFSR_BFARVALID is set (BusFault with a valid address).
	 *
	 * Reading MMFAR or BFAR when the VALID bit is clear produces an
	 * unpredictable value; this function only reads when the bit is set.
	 *
	 * @param ctx output FaultContext to populate with fault address
	 */
	static void extractFaultAddress( FaultContext& ctx ) noexcept;
};

} // namespace flare
} // namespace kmac

#endif // NOVA_BARE_METAL

#endif // KMAC_FLARE_BARE_METAL_FAULT_HANDLER_H
