/**
 * @file main.cpp
 * @brief Bare-metal Nova example - no standard library
 *
 * This example demonstrates using Nova in a bare-metal embedded environment.
 * It shows:
 * - NOVA_BARE_METAL mode and what it implies
 * - user-provided timestamp implementation (platform/chrono.h)
 * - custom UART sink with no libc dependency
 * - Flare emergency sink (RamWriter) for crash/forensic logging
 * - direct Logger<Tag> binding (no ScopedConfigurator needed at startup)
 * - ISR-safe logging via stack-based builders
 * - buffer sizing for constrained stacks
 *
 * Target: ARM Cortex-M4 (or similar bare-metal, single-core)
 * Assumptions:
 * - single-core processor
 * - no interrupts during sink binding/unbinding (done once at startup)
 * - logging may occur from interrupts (atomic sink pointer load is safe)
 *
 * Build flags:
 *   -DNOVA_BARE_METAL
 *   -fno-exceptions -fno-rtti
 *   -nostdlib (with appropriate startup code)
 *
 * HOST SIMULATION NOTE:
 * This file compiles and runs on a host machine for demonstration.
 * The two places where real hardware calls would live are clearly marked:
 *   - steadyNanosecs(): reads a hardware timer on target, uses a counter here
 *   - UartSink::writeChar(): writes to a UART register on target, writes
 *     to a fixed output buffer here (no libc involved)
 */

// define bare-metal mode BEFORE including Nova headers
#define NOVA_BARE_METAL

// custom assert: halt the processor on failure
// in production, replace with your hardware's (e.g. BSP) fault handler
#define NOVA_ASSERT( x ) \
	do { if ( ! ( x ) ) { for ( ;; ) {} } } while ( 0 )

#include <kmac/nova/nova.h>
#include "kmac/flare/emergency_sink.h"
#include "kmac/flare/ram_writer.h"

// ============================================================================
// HOST SIMULATION STUB
// On a real target, delete this entire block.  UartSink::writeChar() writes
// directly to a hardware register instead.
// ============================================================================

namespace sim
{

// fixed output buffer, no heap, no libc
static char outputBuf[ 4096 ];
static std::size_t outputPos = 0;

static void writeChar( char c ) noexcept
{
	if ( outputPos < sizeof( outputBuf ) - 1 )
	{
		outputBuf[ outputPos++ ] = c;
		outputBuf[ outputPos   ] = '\0';
	}
}

// declared here, defined after main() alongside the other sim utilities
static void flush() noexcept;

} // namespace sim

// ============================================================================
// TIMESTAMP IMPLEMENTATION
// Required by platform/chrono.h when NOVA_NO_CHRONO is set (implied by
// NOVA_BARE_METAL).  Replace with your hardware timer read on a real target.
//
// Examples for ARM Cortex-M:
//   DWT cycle counter:
//     uint32_t cycles = DWT->CYCCNT;
//     return (uint64_t)cycles * (1000000000ULL / SystemCoreClock);
//
//   SysTick millisecond tick:
//     extern volatile uint64_t g_tick_ms;
//     return g_tick_ms * 1000000ULL;
// ============================================================================

namespace kmac::nova::platform
{

std::uint64_t steadyNanosecs() noexcept
{
	// HOST SIMULATION: increment a counter each call.
	// On target: read your hardware timer here.
	static std::uint64_t simTickNs = 0;
	simTickNs += 1000;
	return simTickNs;
}

} // namespace kmac::nova::platform

// ============================================================================
// UART SINK
// Writes each record as: [TAG] message\r\n
// No heap allocation, no libc, interrupt-safe (no shared mutable state).
// ============================================================================

class UartSink : public kmac::nova::Sink
{
public:
	void process( const kmac::nova::Record& record ) noexcept override
	{
		writeChar( '[' );
		writeStr( record.tag );
		writeStr( "] " );
		writeStr( record.message, record.messageSize );
		writeChar( '\r' );
		writeChar( '\n' );
	}

private:
	void writeChar( char c ) noexcept
	{
		// TARGET: write directly to UART data register, e.g.:
		//   while ( ( UART0->SR & UART_SR_TXE ) == 0 ) {}
		//   UART0->DR = static_cast< uint8_t >( c );
		//
		// HOST SIMULATION: write to in-memory buffer
		sim::writeChar( c );
	}

	void writeStr( const char* str ) noexcept
	{
		while ( *str )
		{
			writeChar( *str++ );
		}
	}

	void writeStr( const char* str, std::size_t len ) noexcept
	{
		for ( std::size_t i = 0; i < len; ++i )
		{
			writeChar( str[ i ] );
		}
	}
};

// ============================================================================
// APPLICATION TAGS
// ============================================================================

struct SystemTag {};
struct ErrorTag {};
struct DebugTag {};
struct CrashTag {};

NOVA_LOGGER_TRAITS( SystemTag, SYSTEM, true, ::kmac::nova::TimestampHelper::steadyNanosecs );
NOVA_LOGGER_TRAITS( ErrorTag, ERROR, true, ::kmac::nova::TimestampHelper::steadyNanosecs );
NOVA_LOGGER_TRAITS( CrashTag, CRASH, true, ::kmac::nova::TimestampHelper::steadyNanosecs );

// compile out debug logs in release builds
#ifdef NDEBUG
NOVA_LOGGER_TRAITS( DebugTag, DEBUG, false, ::kmac::nova::TimestampHelper::steadyNanosecs );
#else
NOVA_LOGGER_TRAITS( DebugTag, DEBUG, true, ::kmac::nova::TimestampHelper::steadyNanosecs );
#endif

// ============================================================================
// APPLICATION
// Static sink - must outlive all Logger<Tag> bindings.
// ============================================================================

// Normal logging sink - human-readable output via UART
static UartSink gUartSink;

// Emergency/crash logging sink - binary TLV stream to a fixed RAM buffer.
// On a real target, place crashBuf in a .noinit section so it survives reset
// and can be drained to UART or flash by the bootloader on next boot.
// See ram_writer.h for linker script guidance.
static std::uint8_t gCrashBuf[ 1024 ];
static kmac::flare::RamWriter gRamWriter( gCrashBuf, sizeof( gCrashBuf ) );
static kmac::flare::EmergencySink gEmergencySink( &gRamWriter );

static void systemInit() noexcept
{
	// bind once at startup during single-threaded init - no interrupts active
	// yet so the volatile AtomicPtr store is safe on a single-core target
	kmac::nova::Logger< SystemTag >::bindSink( &gUartSink );
	kmac::nova::Logger< ErrorTag >::bindSink( &gUartSink );
	kmac::nova::Logger< DebugTag >::bindSink( &gUartSink );

	// CrashTag routes to the emergency sink - binary TLV format, RAM-buffered.
	// Bind this early so any fault after this point is captured.
	kmac::nova::Logger< CrashTag >::bindSink( &gEmergencySink );

	NOVA_LOG( SystemTag ) << "System initialized";
}

static void peripheralInit() noexcept
{
	NOVA_LOG( SystemTag ) << "Configuring peripheral";

	// simulate a hardware failure
	const bool success = false;
	if ( ! success )
	{
		NOVA_LOG( ErrorTag ) << "Peripheral init failed";

		// also emit a crash record - captured in RAM for post-mortem analysis
		NOVA_LOG_STACK( CrashTag ) << "Fatal: peripheral init failed at startup";
		gEmergencySink.flush();
	}

	NOVA_LOG( DebugTag ) << "Register value = 0x1234";
}

// ISR-safe: NOVA_BARE_METAL implies NOVA_NO_TLS so all NOVA_LOG calls already
// use stack-based builders.  Use a small buffer here to respect ISR stack limits.
static void onTimerIsr() noexcept
{
	NOVA_LOG_BUF_STACK( SystemTag, 128 ) << "Timer ISR";
}

static void mainLoop() noexcept
{
	for ( int i = 0; i < 3; ++i )
	{
		NOVA_LOG( SystemTag ) << "Main loop iteration " << i;

		// volatile prevents the loop being optimised away on the host simulation.
		// On target, replace with a hardware delay or __NOP() sequence.
		// The cast drops volatile for the increment to avoid -Wdeprecated-volatile
		// in C++20 - the read of j is still volatile, which is all that matters here.
		for ( volatile int j = 0; j < 10000; static_cast< void >( ++const_cast< int& >( j ) ) ) { }
	}
}

// ============================================================================
// ENTRY POINT
// On a real target, main() is called from your startup/reset handler after
// stack, zero-initialised data (BSS), and data initialisation.  It never returns.
// ============================================================================

int main()
{
	systemInit();
	peripheralInit();
	onTimerIsr();
	mainLoop();

	// TARGET: never reached - replace with: for ( ;; ) {}
	// HOST: flush captured output so the demo produces visible results
	sim::flush();
	return 0;
}

// ============================================================================
// HOST SIMULATION - utilities (target: delete this block)
// ============================================================================

// write to stdout via the write(2) syscall - one call, no libc buffering
#if defined( __linux__ ) || defined( __APPLE__ )
extern "C" long write( int, const void*, unsigned long );
static void flushToStdout( const char* buf, std::size_t len ) noexcept
{
	write( 1, buf, len );
}
#elif defined( _WIN32 )
extern "C" int _write( int, const void*, unsigned int );
static void flushToStdout( const char* buf, std::size_t len ) noexcept
{
	_write( 1, buf, static_cast< unsigned int >( len ) );
}
#else
static void flushToStdout( const char*, std::size_t ) noexcept {}
#endif

// minimal unsigned integer writer - no libc, no heap
static void simWriteUInt( std::size_t v ) noexcept
{
	if ( v == 0 )
	{
		const char zero = '0';
		flushToStdout( &zero, 1 );
		return;
	}

	// build digits in reverse, then emit forwards
	char tmp[ 20 ];
	int n = 0;
	while ( v != 0 )
	{
		tmp[ n++ ] = static_cast< char >( '0' + v % 10 );
		v /= 10;
	}
	while ( n-- > 0 )
	{
		flushToStdout( &tmp[ n ], 1 );
	}
}

namespace sim
{
void flush() noexcept
{
	const char header[] = "\n=== Nova bare-metal example output (";
	const char mid[] = " bytes) ===\n";
	const char footer[] = "=== end ===\n";

	flushToStdout( header, sizeof( header ) - 1 );
	simWriteUInt( outputPos );
	flushToStdout( mid, sizeof( mid ) - 1 );
	flushToStdout( outputBuf, outputPos );
	flushToStdout( footer, sizeof( footer ) - 1 );

	// report crash buffer state
	const char crashHeader[] = "\n=== Flare crash buffer (";
	const char crashMid[] = " bytes, binary TLV) ===\n";
	const char crashNote[] = "    (on target: drain via UART or flash on next boot)\n";
	flushToStdout( crashHeader, sizeof( crashHeader ) - 1 );
	simWriteUInt( gRamWriter.bytesWritten() );
	flushToStdout( crashMid, sizeof( crashMid ) - 1 );
	flushToStdout( crashNote, sizeof( crashNote ) - 1 );
}
} // namespace sim

/*
 * PLATFORM NOTES
 *
 * 1. Timestamp implementation
 *    Replace steadyNanosecs() with your hardware timer read.
 *    Common options for ARM Cortex-M:
 *    - DWT->CYCCNT (32-bit, wraps at ~25s @ 168MHz - extend with overflow ISR)
 *    - SysTick millisecond counter (lower resolution, no wrap concern)
 *    - RTC for wall-clock time (1s resolution, useful for long-running systems)
 *
 * 2. Atomic safety
 *    NOVA_BARE_METAL uses a volatile pointer for AtomicPtr<Sink>.
 *    Safe for single-core systems where sink binding is done before interrupts
 *    are enabled.  For multi-core, provide a proper atomic implementation via
 *    LDREX/STREX - see platform/atomic.h.
 *
 * 3. Memory layout
 *    All logging uses stack allocation - no heap, no malloc/free.
 *    Static sink lifetime is managed by the application (gUartSink above).
 *
 * 4. Interrupt safety
 *    Sink pointer load is atomic (volatile read on single-core).
 *    UartSink::process() has no shared mutable state - interrupt-safe.
 *    Do not bind/unbind sinks from an ISR.
 *    Use NOVA_LOG_BUF_STACK with a small buffer (128-256 bytes) in ISRs.
 *
 * 5. Stack usage
 *    Under NOVA_BARE_METAL, NOVA_NO_TLS is implied so all NOVA_LOG calls
 *    use stack-based builders automatically.  Default buffer is 1024 bytes;
 *    use NOVA_LOG_BUF_STACK with a smaller size for ISR or deeply nested code.
 *
 * 6. Flare emergency logging
 *    CrashTag is bound to an EmergencySink backed by a RamWriter.  The binary
 *    TLV stream is written to a fixed RAM buffer that survives reset on most
 *    Cortex-M targets when placed in a .noinit section.  On next boot the
 *    bootloader or application startup code can drain the buffer via UART or
 *    write it to flash for later retrieval with flare_reader.py.
 *    See ram_writer.h and uart_writer.h for concrete IWriter implementations.
 *
 * 7. Build configuration
 *    -DNOVA_BARE_METAL   enables NOVA_NO_STD, NOVA_NO_TLS, NOVA_NO_ATOMIC,
 *                        NOVA_NO_CHRONO, NOVA_NO_ARRAY
 *    -fno-exceptions     required - Nova never throws
 *    -fno-rtti           safe - Nova never uses dynamic_cast or typeid
 *    -Os / -O2           recommended for size/performance
 */
