#include "kmac/flare/bare_metal_fault_handler.h"

#if defined( NOVA_BARE_METAL )

#include "kmac/flare/tlv.h"

#include <kmac/nova/record.h>

#include <cstring>
#include <cstdint>

// ============================================================================
// Cortex-M SCB register addresses
//
// These are fixed addresses defined by the ARMv7-M and ARMv6-M architecture
// reference manuals.  They are valid for all Cortex-M0/M0+/M3/M4/M7/M33
// devices regardless of vendor.
//
// Using volatile uint32_t* reads prevents the compiler from optimising away
// hardware register accesses.  reinterpret_cast is the only way to form a
// typed pointer to a fixed hardware address; NOLINT annotations document this.
// ============================================================================

// Configurable Fault Status Register - contains MMFSR, BFSR, UFSR sub-fields
// MMARVALID bit: [7] in MMFSR (bit 7 of CFSR byte 0)
// BFARVALID bit: [15] in BFSR (bit 7 of CFSR byte 1)
static constexpr std::uint32_t SCB_CFSR_ADDR = 0xE000ED28UL;
static constexpr std::uint32_t SCB_CFSR_MMARVALID = ( 1UL << 7  );   // MMFSR bit 7
static constexpr std::uint32_t SCB_CFSR_BFARVALID = ( 1UL << 15 );   // BFSR bit 15 (CFSR bit 15)

// MemManage Fault Address Register
static constexpr std::uint32_t SCB_MMFAR_ADDR = 0xE000ED34UL;

// Bus Fault Address Register
static constexpr std::uint32_t SCB_BFAR_ADDR  = 0xE000ED38UL;

// EXC_RETURN value when the fault occurred from a task using PSP
// LR is set to 0xFFFFFFFD by the processor on exception entry from Thread mode / PSP
static constexpr std::uint32_t EXC_RETURN_THREAD_PSP = 0xFFFFFFFDUL;

namespace kmac {
namespace flare {

// ============================================================================
// static member definition
// ============================================================================

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
EmergencySinkBase* BareMetalFaultHandler::_sink = nullptr;

// ============================================================================
// public API
// ============================================================================

void BareMetalFaultHandler::install( EmergencySinkBase* sink ) noexcept
{
	_sink = sink;
}

void BareMetalFaultHandler::handleFault() noexcept
{
	if ( _sink == nullptr )
	{
		__builtin_trap();
	}

	FaultContext ctx {};

	// aslrOffset is always 0 on bare-metal; binaries execute from the fixed
	// address in the linker script so stack addresses are already static
	ctx.aslrOffset = 0;

	extractExceptionFrame( ctx );
	extractFaultAddress( ctx );

	// build a minimal Nova record; source location is not meaningful here
	// (we are inside the fault handler) - leave file/function/line at defaults
	kmac::nova::Record record {};
	static constexpr const char* FAULT_MSG = "HardFault";
	record.message = FAULT_MSG;
	record.messageSize = std::uint32_t( std::strlen( FAULT_MSG ) );
	record.tag = "kmac::flare::BareMetalFaultHandler";

	_sink->processWithFaultContext( record, ctx );

	// halt - do not return to faulting code
	__builtin_trap();
}

// ============================================================================
// private implementation
// ============================================================================

void BareMetalFaultHandler::extractExceptionFrame( FaultContext& ctx ) noexcept
{
#if defined( __arm__ ) || defined( __thumb__ )

	std::uint32_t msp = 0;
	std::uint32_t psp = 0;
	std::uint32_t lr = 0;

	// .arch armv7-m selects a processor that supports mrs with msp/psp;
	// .syntax unified switches to UAL (Thumb-2) instruction encoding.
	// Both directives are needed because the external assembler may not
	// receive -mcpu/-mthumb from the compiler driver when processing the
	// temporary .s file emitted for inline assembly.  mov captures lr
	// before any compiler prologue can overwrite it.
	// NOLINTNEXTLINE(hicpp-no-assembler)
	__asm volatile (
		".arch armv7-m    \n"
		".syntax unified  \n"
		"mrs %0, msp      \n"
		"mrs %1, psp      \n"
		"mov %2, lr       \n"
		: "=r" ( msp ), "=r" ( psp ), "=r" ( lr )
		:
		: "memory"
	);

	// determine which stack holds the exception frame:
	// EXC_RETURN in LR tells us whether the exception was taken from
	// thread mode using PSP (0xFFFFFFFD) or any other mode using MSP
	const std::uint32_t frameBase = ( lr == EXC_RETURN_THREAD_PSP ) ? psp : msp;

	// the exception frame is a packed array of 32-bit words at frameBase:
	//   [0] r0, [1] r1, [2] r2, [3] r3, [4] r12, [5] lr, [6] pc, [7] xpsr
	// use memcpy to read each word - avoids strict-aliasing UB from casting
	// the raw uint32_t* to individual register values
	std::uint32_t frame[ 8 ];  // NOLINT(cppcoreguidelines-avoid-c-arrays)
	std::memcpy( frame, reinterpret_cast< const void* >( frameBase ), sizeof( frame ) ); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)

	// store into FaultContext as uint64_t (zero-extended from 32-bit)
	ctx.registers[ 0 ] = frame[ 0 ];  // r0
	ctx.registers[ 1 ] = frame[ 1 ];  // r1
	ctx.registers[ 2 ] = frame[ 2 ];  // r2
	ctx.registers[ 3 ] = frame[ 3 ];  // r3
	ctx.registers[ 4 ] = frame[ 4 ];  // r12
	ctx.registers[ 5 ] = frame[ 5 ];  // lr  (EXC_RETURN or return address)
	ctx.registers[ 6 ] = frame[ 6 ];  // pc  (address of faulting instruction)
	ctx.registers[ 7 ] = frame[ 7 ];  // xpsr
	ctx.registerCount = 8;
	ctx.layoutId = std::uint8_t( RegisterLayoutId::CortexM );

#else
	// RISC-V and other bare-metal architectures: register capture requires
	// an architecture-specific trap entry stub; not yet implemented.
	// The record will be written without register context.
	(void) ctx;
#endif
}

void BareMetalFaultHandler::extractFaultAddress( FaultContext& ctx ) noexcept
{
#if defined( __arm__ ) || defined( __thumb__ )

	// read CFSR to check whether MMFAR or BFAR holds a valid address;
	// reading MMFAR/BFAR when the VALID bit is clear produces unpredictable values
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
	const std::uint32_t cfsr = *reinterpret_cast< volatile const std::uint32_t* >( SCB_CFSR_ADDR );

	if ( ( cfsr & SCB_CFSR_MMARVALID ) != 0U )
	{
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
		const std::uint32_t mmfar = *reinterpret_cast< volatile const std::uint32_t* >( SCB_MMFAR_ADDR );
		ctx.faultAddress = mmfar;
		ctx.hasFaultAddress = true;
	}
	else if ( ( cfsr & SCB_CFSR_BFARVALID ) != 0U )
	{
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
		const std::uint32_t bfar = *reinterpret_cast< volatile const std::uint32_t* >( SCB_BFAR_ADDR );
		ctx.faultAddress = bfar;
		ctx.hasFaultAddress = true;
	}
	// UsageFault and other faults have no associated address; leave hasFaultAddress=false

#else
	// RISC-V: mtval (machine trap value) holds the fault address for most traps;
	// reading it requires CSR access via inline assembly - not yet implemented.
	(void) ctx;
#endif
}

} // namespace flare
} // namespace kmac

// ============================================================================
// Optional vector table symbol definition
// ============================================================================

#if defined( __arm__ ) || defined( __thumb__ )

#if defined( FLARE_FAULT_HANDLER_STRONG )
	#define FLARE_FAULT_ATTR  // strong symbol - linker always picks this one
#elif defined( FLARE_FAULT_HANDLER_WEAK )
	#define FLARE_FAULT_ATTR __attribute__( ( weak ) )
#endif

#if defined( FLARE_FAULT_HANDLER_STRONG ) || defined( FLARE_FAULT_HANDLER_WEAK )

// NOLINTNEXTLINE(hicpp-use-auto,readability-named-parameter) - C linkage, no auto
extern "C" FLARE_FAULT_ATTR void HardFault_Handler( void ) noexcept
{
	kmac::flare::BareMetalFaultHandler::handleFault();
}

#endif // FLARE_FAULT_HANDLER_STRONG || FLARE_FAULT_HANDLER_WEAK

#undef FLARE_FAULT_ATTR

#endif // __arm__ || __thumb__

#endif // NOVA_BARE_METAL
