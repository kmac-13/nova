#pragma once
#ifndef KMAC_FLARE_SIGNAL_HANDLER_H
#define KMAC_FLARE_SIGNAL_HANDLER_H

// SignalHandler is a POSIX-only facility.  Guard the entire header so
// non-POSIX translation units can include nova_flare headers without error.
#if defined( __linux__ ) || defined( __APPLE__ ) || defined( __FreeBSD__ ) || defined( __ANDROID__ )
#define FLARE_HAVE_FAULT_CONTEXT 1 /* NOLINT(cppcoreguidelines-macro-usage) */

#include "emergency_sink.h"
#include "fault_context.h"

#include <kmac/nova/record.h>
#include <kmac/nova/platform/array.h>

#include <cstddef>
#include <cstdint>
#include <signal.h>

namespace kmac {
namespace flare {

/**
 * @brief Non-templated base for SignalHandler.
 *
 * SignalHandlerBase holds all state and implements everything that does not
 * depend on the alternate stack size: signal handler installation, fault
 * context capture, and crash record writing.  The only thing left to the
 * derived template is the alternate stack buffer and the public install()
 * entry point that passes it to SignalHandlerBase::install().
 *
 * This class is not intended to be used directly.  Use SignalHandler<N>.
 */
class SignalHandlerBase
{
public:
	// number of signals we handle
	static constexpr std::size_t NUM_SIGNALS = 5;

private:
	// signal numbers we install handlers for
	static const kmac::nova::platform::Array< int, NUM_SIGNALS > HANDLED_SIGNALS;

	// the sink that crash records are routed to
	// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
	static EmergencySinkBase* _sink;

	// saved previous signal actions so we can restore + re-raise
	// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
	static kmac::nova::platform::Array< struct sigaction, NUM_SIGNALS > _previousActions;

	// saved previous terminate handler
	// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
	static void ( *_previousTerminateHandler )();

public:
	/**
	 * @brief Uninstall crash signal handlers, restoring previous handlers.
	 *
	 * Restores the signal dispositions saved at install() time and
	 * removes the std::set_terminate handler.  Frees the alternate stack.
	 *
	 * @note Safe to call even if install() was never called (no-op).
	 */
	static void uninstall() noexcept;

protected:
	/**
	 * @brief Install crash signal handlers using the given sink.
	 *
	 * Configures an alternate signal stack and installs handlers for
	 * SIGSEGV, SIGBUS, SIGFPE, SIGILL, and SIGABRT.  Also installs a
	 * std::set_terminate handler.
	 *
	 * Previous signal dispositions are saved and restored on re-raise.
	 *
	 * @param altStackBuffer buffer for alt stack
	 * @param altStackSize size of alt stack buffer
	 * @param sink sink to receive crash records (must outlive the process)
	 *
	 * @note Must be called from a normal (non-signal) context.
	 * @note Calling install() more than once replaces the registered sink.
	 */
	static void install( char* altStackBuffer, std::size_t altStackSize, EmergencySinkBase* sink ) noexcept;

private:
	/**
	 * @brief SA_SIGINFO-compatible signal handler.
	 *
	 * Captures fault context from siginfo_t and ucontext_t, writes a
	 * crash record via _sink, then re-raises to restore default behaviour.
	 */
	static void signalHandler( int signum, siginfo_t* info, void* uctx ) noexcept;

	/**
	 * @brief std::terminate replacement that logs before aborting.
	 */
	static void terminateHandler() noexcept;

	/**
	 * @brief Populate FaultContext from siginfo_t and ucontext_t.
	 *
	 * Architecture-specific register extraction happens here.
	 * The ASLR offset stored in _sink is copied into ctx.aslrOffset.
	 *
	 * @param info siginfo_t pointer from the signal handler (may be null)
	 * @param uctx ucontext_t pointer from the signal handler (may be null)
	 * @param ctx output FaultContext
	 */
	static void buildFaultContext( const siginfo_t* info, const void* uctx, FaultContext& ctx ) noexcept;
};

/**
 * @brief Install Flare signal handlers for crash-capturing signals.
 *
 * SignalHandler configures the alternate signal stack (sigaltstack) and
 * installs signal handlers via sigaction with SA_ONSTACK | SA_SIGINFO for
 * SIGSEGV, SIGBUS, SIGFPE, SIGILL, and SIGABRT.  It also installs a
 * std::set_terminate handler for C++ exception faults.
 *
 * On signal delivery the handler:
 *   1. assembles a FaultContext from siginfo_t (fault address) and
 *      ucontext_t (CPU registers)
 *   2. writes a crash record via the bound EmergencySinkBase so the fault
 *      context is included in the TLV output
 *   3. restores the previous (default) signal handler and re-raises to
 *      produce a core dump and correct exit status
 *
 * Usage:
 * @code
 *   // construct sink and writer at startup, before install()
 *   static kmac::flare::FdWriter writer( fd );
 *   static kmac::flare::EmergencySink<> sink( &writer, true, true );
 *
 *   kmac::flare::SignalHandler<>::install( &sink );
 * @endcode
 *
 * Thread safety:
 *   install() and uninstall() must be called from a single thread.
 *   The signal handler itself is re-entrant-safe for a single signal
 *   at a time (signals are masked for the duration of handler execution).
 *
 * @tparam StackSize size of the alternate signal stack in bytes.
 *   Must be at least 2048.  The default (8192) is sufficient for the handler
 *   body and a few frames of call depth.  Increase if your signal handler or
 *   any function it calls has large stack frames.
 *
 * @note Designed for Linux, Android, macOS, and FreeBSD.
 *   Not available on Windows or bare-metal targets.
 *
 * @note The alternate signal stack is mandatory for SIGSEGV caused by stack
 *   overflow: the faulting stack cannot be used to run the handler.
 *
 * @note SIGABRT may arrive without a meaningful si_addr (e.g. from abort()).
 *   hasFaultAddress will be false in the FaultContext for those records.
 *
 * @note Bare-metal crash capture (deferred - requires separate planning):
 *   On bare-metal targets there are no POSIX signals, no sigaltstack, and no
 *   /proc filesystem, so SignalHandler cannot be ported directly.  Crash
 *   capture requires platform-specific fault handler hooks instead:
 *
 *   Fault entry points:
 *   - ARM Cortex-M: HardFault_Handler, MemManage_Handler, BusFault_Handler,
 *     UsageFault_Handler in the vector table.  The processor pushes a
 *     standard exception frame (r0-r3, r12, LR, PC, xPSR) onto the stack
 *     automatically before branching to the handler, so register capture
 *     does not require inline assembly beyond reading the pre-fault SP.
 *   - ARM Cortex-A / Cortex-R: undefined instruction, data abort, prefetch
 *     abort, and FIQ/IRQ vectors.  The banked registers and SPSR must be
 *     saved manually in the vector stubs before branching to C handlers.
 *   - RISC-V: trap handler pointed to by mtvec/stvec; mcause/scause
 *     identifies the fault type; mtval/stval holds the fault address.
 *
 *   Stack overflow detection:
 *   - Cortex-M MPU: configure a no-access region at the bottom of each
 *     task stack; a MemManage fault fires before the stack corrupts the
 *     heap.  The handler must run from a separate stack (MSP vs PSP on
 *     Cortex-M, or a dedicated IRQ stack on A/R profiles).
 *   - FreeRTOS: configCHECK_FOR_STACK_OVERFLOW hook provides a software
 *     check at each context switch, but does not replace MPU protection.
 *
 *   Fault address:
 *   - Cortex-M: MMFAR (MemManage Fault Address Register) and BFAR (Bus
 *     Fault Address Register) in the SCB hold the faulting address when
 *     the corresponding VALID bit is set in MMFSR/BFSR.
 *   - Cortex-A: the abort-mode LR and the CP15 DFAR/IFAR registers.
 *   - RISC-V: mtval/stval directly holds the fault address.
 *
 *   Load base / ASLR:
 *   - Bare-metal binaries are typically non-PIE and loaded at a fixed
 *     address from the linker script.  The ASLR offset is always 0, so
 *     stack frame addresses are already static and can be passed directly
 *     to addr2line without relocation.
 *
 *   Register capture:
 *   - Cortex-M: read the exception frame from the stacked SP to recover
 *     r0-r3, r12, LR, PC, xPSR.  Callee-saved registers (r4-r11) are not
 *     stacked automatically and must be saved in the handler prologue
 *     before any C code runs (i.e. in the vector stub assembly).
 *   - Cortex-A/R and RISC-V: all registers must be saved in the trap
 *     entry stub; no automatic hardware stacking.
 *
 *   Writer:
 *   - FdWriter is unavailable (no file descriptors).  Use RamWriter to
 *     write into a dedicated crash buffer, or UartWriter to stream directly
 *     over a serial port.  The crash buffer should be placed in a
 *     non-initialised RAM section so its contents survive a warm reset and
 *     can be read by the bootloader or a subsequent run of the application.
 *
 *   Suggested implementation approach:
 *   - Provide a BareMetalFaultHandler class (analogous to SignalHandler)
 *     that installs itself into the relevant fault vectors at construction
 *     and provides a static fault entry point suitable for use as a
 *     C-linkage vector table entry.
 *   - FaultContext population can reuse the same struct and TLV encoding
 *     path as the hosted implementation; only the fault entry and register
 *     extraction are platform-specific.
 */
template < std::size_t StackSize = 8 * 1024 >
class SignalHandler : public SignalHandlerBase
{
private:
	// can't use MINSIGSTKSZ directly in a static_assert because glibc 2.34+
	// made it a runtime sysconf() call, not a constant; 2048 bytes is a safe,
	// portable floor (larger than the historical MINSIGSTKSZ on all supported
	// architectures, which is typically 2048 on x86-64, 5120 on ARM64)
	static_assert( StackSize >= 2048, "StackSize must be at least 2048 bytes" );

	// alternate stack storage (static so it outlives signal delivery)
	// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
	static kmac::nova::platform::Array< char, StackSize > _altStack;

public:
	static void install( EmergencySinkBase* sink ) noexcept;
};

// static member definitions

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
template< std::size_t StackSize >
kmac::nova::platform::Array< char, StackSize > SignalHandler< StackSize >::_altStack {};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

template < std::size_t StackSize >
void SignalHandler< StackSize >::install( EmergencySinkBase* sink ) noexcept
{
	install( _altStack.data(), _altStack.size(), sink );
}

} // namespace flare
} // namespace kmac

#endif // POSIX platforms

#endif // KMAC_FLARE_SIGNAL_HANDLER_H
