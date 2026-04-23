#include "kmac/flare/signal_handler.h"

#if defined( FLARE_HAVE_FAULT_CONTEXT )

#include "kmac/flare/tlv.h"

#include <kmac/nova/record.h>
#include <kmac/nova/platform/array.h>

#include <cstring>
#include <exception>    // std::set_terminate
#include <signal.h>
#include <stdint.h>
#include <ucontext.h>

namespace
{

/**
 * @brief Architecture-specific register extraction from ucontext_t.
 *
 * Each platform exposes the saved register state differently.  We extract
 * only what ucontext_t gives us directly - no extra unwinding required.
 *
 * Layout IDs must match RegisterLayoutId in tlv.h:
 *   X86_64 = 1 : rax, rbx, rcx, rdx, rsi, rdi, rbp, rsp, r8-r15, rip, rflags
 *   Arm64  = 2 : x0-x30, sp, pc, pstate
 *   Arm32  = 3 : r0-r15, cpsr
 *
 * @param uctx ucontext_t captured by the signal handler (may be nullptr on
 *        unsupported platforms, in which case no registers are extracted)
 * @param ctx output FaultContext to populate with register values and layout ID
 */
void extractRegisters( const ucontext_t* uctx, kmac::flare::FaultContext& ctx ) noexcept;

} // anonymous namespace

namespace kmac {
namespace flare {

// ============================================================================
// static member definitions
// ============================================================================

const kmac::nova::platform::Array< int, SignalHandlerBase::NUM_SIGNALS > SignalHandlerBase::HANDLED_SIGNALS = {
	SIGSEGV,
	SIGBUS,
	SIGFPE,
	SIGILL,
	SIGABRT,
};

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
EmergencySinkBase* SignalHandlerBase::_sink = nullptr;

kmac::nova::platform::Array< struct sigaction, SignalHandlerBase::NUM_SIGNALS > SignalHandlerBase::_previousActions {};

void ( *SignalHandlerBase::_previousTerminateHandler )() = nullptr;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

// ============================================================================
// install / uninstall
// ============================================================================

void SignalHandlerBase::install( char* altStackBuffer, std::size_t altStackSize, EmergencySinkBase* sink ) noexcept
{
	_sink = sink;

	// configure alternate signal stack so we can handle stack-overflow SIGSEGV;
	// the faulting stack cannot be used to execute the handler
	stack_t altStack {};
	altStack.ss_sp = altStackBuffer;
	altStack.ss_size = altStackSize;
	altStack.ss_flags = 0;
	sigaltstack( &altStack, nullptr );

	struct sigaction sigAct {};
	sigAct.sa_sigaction = signalHandler;
	sigAct.sa_flags = SA_SIGINFO | SA_ONSTACK;

	// block all other handled signals during handler execution to prevent
	// re-entrant delivery while writing the crash record
	sigemptyset( &sigAct.sa_mask );
	for ( std::size_t i = 0; i < NUM_SIGNALS; ++i )
	{
		sigaddset( &sigAct.sa_mask, HANDLED_SIGNALS.at( i ) );
	}

	for ( std::size_t i = 0; i < NUM_SIGNALS; ++i )
	{
		sigaction( HANDLED_SIGNALS.at( i ), &sigAct, &_previousActions.at( i ) );
	}

	_previousTerminateHandler = std::set_terminate( terminateHandler );
}

void SignalHandlerBase::uninstall() noexcept
{
	for ( std::size_t i = 0; i < NUM_SIGNALS; ++i )
	{
		sigaction( HANDLED_SIGNALS.at( i ), &_previousActions.at( i ), nullptr );
	}

	if ( _previousTerminateHandler != nullptr )
	{
		std::set_terminate( _previousTerminateHandler );
		_previousTerminateHandler = nullptr;
	}

	// disable the alternate stack
	stack_t disabledStack {};
	disabledStack.ss_flags = SS_DISABLE;
	sigaltstack( &disabledStack, nullptr );

	_sink = nullptr;
}

// ============================================================================
// private implementation
// ============================================================================

void SignalHandlerBase::signalHandler( int signum, siginfo_t* info, void* uctx ) noexcept
{
	if ( _sink != nullptr )
	{
		FaultContext ctx {};
		buildFaultContext( info, uctx, ctx );

		// build a minimal Nova record carrying the signal number as the message;
		// source location is not meaningful here (we are inside the signal handler)
		// so we leave file/function/line at their zero-initialised defaults
		kmac::nova::Record record {};

		// write the signal name into a small stack buffer for the message field
		// async-signal-safe (no heap, no stdio)
		static constexpr std::size_t MSG_BUF = 32;
		kmac::nova::platform::Array< char, MSG_BUF > msgBuf {};

		// signal number to short name - covers the five signals we install for;
		// no default initialiser to avoid a dead-store warning: every reachable
		// path through the switch sets signame before strncpy uses it
		const char* signame = nullptr;
		switch ( signum )
		{
		case SIGSEGV: signame = "SIGSEGV"; break;
		case SIGBUS:  signame = "SIGBUS";  break;
		case SIGFPE:  signame = "SIGFPE";  break;
		case SIGILL:  signame = "SIGILL";  break;
		case SIGABRT: signame = "SIGABRT"; break;
		default:      signame = "SIG?";   break;
		}

		// copy the character array into the buffer
		std::strncpy( msgBuf.data(), signame, MSG_BUF - 1 );

		// force the last character in the buffer to be the string terminator
		msgBuf[ MSG_BUF - 1 ] = '\0';

		record.message = msgBuf.data();
		record.messageSize = std::uint32_t( std::strlen( msgBuf.data() ) );
		record.tag = "kmac::flare::SignalHandler";

		_sink->processWithFaultContext( record, ctx );
	}

	// restore the previous disposition and re-raise so the process exits with
	// the correct signal status and a core dump is produced if configured
	for ( std::size_t i = 0; i < NUM_SIGNALS; ++i )
	{
		if ( HANDLED_SIGNALS.at( i ) == signum )
		{
			sigaction( signum, &_previousActions.at( i ), nullptr );
			break;
		}
	}
	(void) raise( signum ); // NOLINT(cert-err33-c) - no meaningful recovery if raise fails in a crash handler
}

void SignalHandlerBase::terminateHandler() noexcept
{
	if ( _sink != nullptr )
	{
		FaultContext ctx {};
		// no siginfo_t available from terminate; aslrOffset is still useful
		ctx.aslrOffset = _sink->loadBaseAddress();

		kmac::nova::Record record {};
		static constexpr const char* msg = "std::terminate";
		record.message = msg;
		record.messageSize = std::uint32_t( std::strlen( msg ) );
		record.tag = "kmac::flare::SignalHandler";

		_sink->processWithFaultContext( record, ctx );
	}

	// chain to the previous terminate handler if there was one, otherwise abort
	if ( _previousTerminateHandler != nullptr )
	{
		_previousTerminateHandler();
	}
	else
	{
		__builtin_trap();
	}
}

void SignalHandlerBase::buildFaultContext( const siginfo_t* info, const void* uctx, FaultContext& ctx ) noexcept
{
	// fault address from siginfo_t - meaningful for SIGSEGV, SIGBUS, SIGFPE, SIGILL;
	// SIGABRT sets si_addr to 0 or the address of the abort() call, so we only
	// record hasFaultAddress=true when si_addr is non-null
	if ( info != nullptr )
	{
		// NOLINT NOTE: reinterpret_cast required to convert void* si_addr to uint64_t
		ctx.faultAddress = reinterpret_cast< std::uint64_t >( info->si_addr ); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
		ctx.hasFaultAddress = ( info->si_addr != nullptr );
	}

	// ASLR offset: stored in the sink at construction time; copy it here so the
	// TLV record is self-contained without requiring the reader to have the binary
	if ( _sink != nullptr )
	{
		ctx.aslrOffset = _sink->loadBaseAddress();
	}

	// CPU registers from ucontext_t
	const auto* ucontextTyped = static_cast< const ucontext_t* >( uctx );
	extractRegisters( ucontextTyped, ctx );
}

} // namespace flare
} // namespace kmac

namespace
{

// define extractRegisters for each platform

#if defined( __x86_64__ )

void extractRegisters( const ucontext_t* uctx, kmac::flare::FaultContext& ctx ) noexcept
{
	if ( uctx == nullptr )
	{
		return;
	}

#if defined( __linux__ ) || defined( __ANDROID__ )
	const mcontext_t& mctx = uctx->uc_mcontext;
	ctx.registers[ 0 ]  = static_cast< std::uint64_t >( mctx.gregs[ REG_RAX ] );
	ctx.registers[ 1 ]  = static_cast< std::uint64_t >( mctx.gregs[ REG_RBX ] );
	ctx.registers[ 2 ]  = static_cast< std::uint64_t >( mctx.gregs[ REG_RCX ] );
	ctx.registers[ 3 ]  = static_cast< std::uint64_t >( mctx.gregs[ REG_RDX ] );
	ctx.registers[ 4 ]  = static_cast< std::uint64_t >( mctx.gregs[ REG_RSI ] );
	ctx.registers[ 5 ]  = static_cast< std::uint64_t >( mctx.gregs[ REG_RDI ] );
	ctx.registers[ 6 ]  = static_cast< std::uint64_t >( mctx.gregs[ REG_RBP ] );
	ctx.registers[ 7 ]  = static_cast< std::uint64_t >( mctx.gregs[ REG_RSP ] );
	ctx.registers[ 8 ]  = static_cast< std::uint64_t >( mctx.gregs[ REG_R8  ] );
	ctx.registers[ 9 ]  = static_cast< std::uint64_t >( mctx.gregs[ REG_R9  ] );
	ctx.registers[ 10 ] = static_cast< std::uint64_t >( mctx.gregs[ REG_R10 ] );
	ctx.registers[ 11 ] = static_cast< std::uint64_t >( mctx.gregs[ REG_R11 ] );
	ctx.registers[ 12 ] = static_cast< std::uint64_t >( mctx.gregs[ REG_R12 ] );
	ctx.registers[ 13 ] = static_cast< std::uint64_t >( mctx.gregs[ REG_R13 ] );
	ctx.registers[ 14 ] = static_cast< std::uint64_t >( mctx.gregs[ REG_R14 ] );
	ctx.registers[ 15 ] = static_cast< std::uint64_t >( mctx.gregs[ REG_R15 ] );
	ctx.registers[ 16 ] = static_cast< std::uint64_t >( mctx.gregs[ REG_RIP ] );
	ctx.registers[ 17 ] = static_cast< std::uint64_t >( mctx.gregs[ REG_EFL ] );
	ctx.registerCount   = 18;
#elif defined( __APPLE__ )
	const mcontext_t mctx = uctx->uc_mcontext;
	ctx.registers[ 0 ]  = mctx->__ss.__rax;
	ctx.registers[ 1 ]  = mctx->__ss.__rbx;
	ctx.registers[ 2 ]  = mctx->__ss.__rcx;
	ctx.registers[ 3 ]  = mctx->__ss.__rdx;
	ctx.registers[ 4 ]  = mctx->__ss.__rsi;
	ctx.registers[ 5 ]  = mctx->__ss.__rdi;
	ctx.registers[ 6 ]  = mctx->__ss.__rbp;
	ctx.registers[ 7 ]  = mctx->__ss.__rsp;
	ctx.registers[ 8 ]  = mctx->__ss.__r8;
	ctx.registers[ 9 ]  = mctx->__ss.__r9;
	ctx.registers[ 10 ] = mctx->__ss.__r10;
	ctx.registers[ 11 ] = mctx->__ss.__r11;
	ctx.registers[ 12 ] = mctx->__ss.__r12;
	ctx.registers[ 13 ] = mctx->__ss.__r13;
	ctx.registers[ 14 ] = mctx->__ss.__r14;
	ctx.registers[ 15 ] = mctx->__ss.__r15;
	ctx.registers[ 16 ] = mctx->__ss.__rip;
	ctx.registers[ 17 ] = mctx->__ss.__rflags;
	ctx.registerCount   = 18;
#endif

	ctx.layoutId = std::uint8_t( kmac::flare::RegisterLayoutId::X86_64 );
}

#elif defined( __aarch64__ )

void extractRegisters( const ucontext_t* uctx, kmac::flare::FaultContext& ctx ) noexcept
{
	if ( uctx == nullptr )
	{
		return;
	}

#if defined( __linux__ ) || defined( __ANDROID__ )
	const mcontext_t& mctx = uctx->uc_mcontext;
	// x0-x30 (31 general-purpose registers)
	for ( std::size_t i = 0; i < 31; ++i )
	{
		ctx.registers[ i ] = mctx.regs[ i ];
	}
	ctx.registers[ 31 ] = mctx.sp;
	ctx.registers[ 32 ] = mctx.pc;
	ctx.registers[ 33 ] = static_cast< std::uint64_t >( mctx.pstate );
	ctx.registerCount   = 34;
#elif defined( __APPLE__ )
	const mcontext_t mctx = uctx->uc_mcontext;
	for ( std::size_t i = 0; i < 29; ++i )
	{
		ctx.registers[ i ] = mctx->__ss.__x[ i ];
	}
	ctx.registers[ 29 ] = mctx->__ss.__fp;
	ctx.registers[ 30 ] = mctx->__ss.__lr;
	ctx.registers[ 31 ] = mctx->__ss.__sp;
	ctx.registers[ 32 ] = mctx->__ss.__pc;
	ctx.registers[ 33 ] = mctx->__ss.__cpsr;
	ctx.registerCount   = 34;
#endif

	ctx.layoutId = std::uint8_t( kmac::flare::RegisterLayoutId::Arm64 );
}

#elif defined( __arm__ )

void extractRegisters( const ucontext_t* uctx, kmac::flare::FaultContext& ctx ) noexcept
{
	if ( uctx == nullptr )
	{
		return;
	}

#if defined( __linux__ ) || defined( __ANDROID__ )
	const mcontext_t& mctx = uctx->uc_mcontext;
	// r0-r10 (arm_r0..arm_r10), fp=r11, ip=r12, sp=r13, lr=r14, pc=r15, cpsr
	ctx.registers[ 0 ]  = mctx.arm_r0;
	ctx.registers[ 1 ]  = mctx.arm_r1;
	ctx.registers[ 2 ]  = mctx.arm_r2;
	ctx.registers[ 3 ]  = mctx.arm_r3;
	ctx.registers[ 4 ]  = mctx.arm_r4;
	ctx.registers[ 5 ]  = mctx.arm_r5;
	ctx.registers[ 6 ]  = mctx.arm_r6;
	ctx.registers[ 7 ]  = mctx.arm_r7;
	ctx.registers[ 8 ]  = mctx.arm_r8;
	ctx.registers[ 9 ]  = mctx.arm_r9;
	ctx.registers[ 10 ] = mctx.arm_r10;
	ctx.registers[ 11 ] = mctx.arm_fp;   // r11
	ctx.registers[ 12 ] = mctx.arm_ip;   // r12
	ctx.registers[ 13 ] = mctx.arm_sp;   // r13
	ctx.registers[ 14 ] = mctx.arm_lr;   // r14
	ctx.registers[ 15 ] = mctx.arm_pc;   // r15
	ctx.registers[ 16 ] = mctx.arm_cpsr;
	ctx.registerCount   = 17;
#endif

	ctx.layoutId = std::uint8_t( kmac::flare::RegisterLayoutId::Arm32 );
}

#else

// Unsupported architecture - no register capture
void extractRegisters( const ucontext_t* /*uctx*/, kmac::flare::FaultContext& /*ctx*/ ) noexcept
{
}

#endif  // architecture dispatch

} // anonymous namespace

#endif // FLARE_HAVE_FAULT_CONTEXT (i.e. POSIX platforms)
