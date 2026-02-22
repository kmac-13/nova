/**
 * @file main.cpp
 * @brief Bare-metal Nova example - no standard library
 *
 * This example demonstrates using Nova in a bare-metal embedded environment
 * without the C++ standard library. It shows:
 * - custom timestamp implementation
 * - volatile atomic operations (single-core safe)
 * - stack-based array wrapper
 * - custom sink for UART output
 *
 * Target: ARM Cortex-M4 (or similar bare-metal)
 * Assumptions:
 * - single-core processor
 * - no interrupts during sink binding/unbinding
 * - logging can occur from interrupts (read-only atomic load)
 *
 * Build flags:
 *   -DNOVA_BARE_METAL
 *   -fno-exceptions -fno-rtti
 *   -nostdlib (with appropriate startup code)
 */

// define bare-metal mode BEFORE including Nova headers
#define NOVA_BARE_METAL

// custom assert for bare-metal (halt processor on failure)
#define NOVA_ASSERT( x ) do { if ( ! ( x ) ) { while( 1 ); } } while( 0 )

// include stdio for demo purposes (real bare-metal wouldn't have this)
#include <cstdio>

// include Nova headers (platform abstraction handles the rest)
#include <kmac/nova/nova.h>
#include <kmac/nova/scoped_configurator.h>

// ============================================================================
// BARE-METAL PLATFORM IMPLEMENTATION
// ============================================================================

// simulated hardware registers (replace with actual hardware addresses)
// NOTE: These are example addresses only - dereferencing them will crash!
// for a real implementation, use your actual hardware peripheral addresses.
namespace hw {
// example addresses - DO NOT USE AS-IS
// volatile unsigned int* const UART_DATA = (unsigned int*)0x40001000;
// volatile unsigned int* const UART_STATUS = (unsigned int*)0x40001004;
// volatile unsigned int* const SYSTICK_COUNT = (unsigned int*)0xE000E018;

// for this demo, we'll use a safe counter instead
static std::uint64_t simulatedTickCount = 0;
unsigned int SYSTEM_CLOCK_HZ = 168000000; // 168 MHz (example)
}

// Global timestamp source (implementation required by platform/chrono.h)
namespace kmac::nova::platform {

std::uint64_t steadyNanosecs() noexcept
{
	// DEMO ONLY: increment simulated counter
	// in real bare-metal, read actual hardware timer:
	// std::uint32_t cycles = DWT->CYCCNT;
	// std::uint32_t cycles = *((volatile uint32_t*)0xE0001004); // DWT_CYCCNT
	// etc.

	hw::simulatedTickCount += 1000; // simulate 1 microsecond per call

	// convert to nanoseconds
	std::uint64_t ns = hw::simulatedTickCount;

	return ns;
}

} // namespace kmac::nova::platform

// ============================================================================
// CUSTOM UART SINK FOR BARE-METAL
// ============================================================================

class UartSink : public kmac::nova::Sink
{
public:
	void process( const kmac::nova::Record& record ) noexcept override
	{
		// simple format: [TAG] message
		writeString( "[" );
		writeString( record.tag );
		writeString( "] " );
		writeString( record.message, record.messageSize );
		writeString( "\r\n" );
	}

private:
	void writeString( const char* str ) noexcept
	{
		while ( *str )
		{
			writeChar( *str++ );
		}
	}

	void writeString( const char* str, std::size_t len ) noexcept
	{
		for ( std::size_t i = 0; i < len; ++i )
		{
			writeChar( str[ i ] );
		}
	}

	void writeChar( char c ) noexcept
	{
		// DEMO: use stdout for demonstration
		// in real bare-metal, write to UART hardware:
		//
		// example for real hardware:
		// while ((*UART_STATUS & 0x80) == 0) { /* TX buffer full */ }
		// *UART_DATA = c;

		// for this demo, use putchar
		putchar( c );
	}
};

// ============================================================================
// APPLICATION TAGS
// ============================================================================

struct SystemTag {};
struct ErrorTag {};
struct DebugTag {};

// configure tags with traits
NOVA_LOGGER_TRAITS( SystemTag, SYSTEM, true, kmac::nova::TimestampHelper::steadyNanosecs );
NOVA_LOGGER_TRAITS( ErrorTag, ERROR, true, kmac::nova::TimestampHelper::steadyNanosecs );

// debug tag disabled in release builds
#ifdef NDEBUG
NOVA_LOGGER_TRAITS( DebugTag, DEBUG, false, kmac::nova::TimestampHelper::steadyNanosecs );
#else
NOVA_LOGGER_TRAITS( DebugTag, DEBUG, true, kmac::nova::TimestampHelper::steadyNanosecs );
#endif

// ============================================================================
// BARE-METAL APPLICATION
// ============================================================================

// Global sink (must have static lifetime)
static UartSink gUartSink;

void systemInit()
{
	printf( "--- System Initialization ---\n" );

	// configure logging sinks
	// NOTE: we do this in single-threaded init, so volatile atomic is safe
	printf( "Binding sinks to loggers...\n" );

	// for bare-metal, we want persistent bindings, so use direct binding:
	kmac::nova::Logger< SystemTag >::bindSink( &gUartSink );
	kmac::nova::Logger< ErrorTag >::bindSink( &gUartSink );
	kmac::nova::Logger< DebugTag >::bindSink( &gUartSink );

	printf( "Sinks bound. Testing logging...\n" );
	NOVA_LOG( SystemTag ) << "System initialized";
	printf( "System initialization complete.\n\n" );
}

void peripheralDriverExample()
{
	printf( "--- Peripheral Driver Example ---\n" );
	NOVA_LOG( SystemTag ) << "Configuring peripheral";

	// simulate error
	bool success = false;  // hardware failure

	if ( ! success )
	{
		NOVA_LOG( ErrorTag ) << "Peripheral init failed!";
	}

	NOVA_LOG( DebugTag ) << "Debug: register value = 0x1234";
	printf( "Peripheral driver example complete.\n" );
}

void interruptSafeLogging()
{
	// logging from ISR is safe because:
	// 1. sink pointer load is atomic (even with volatile implementation)
	// 2. UartSink::process() is interrupt-safe (no shared state)
	// 3. record builders use stack-based buffers (no heap)

	NOVA_LOG( SystemTag ) << "ISR triggered";
}

// Simulated main entry point
int main()
{
	printf( "=======================================================\n" );
	printf( "  Nova Bare-Metal Example (Demonstration Mode)\n" );
	printf( "=======================================================\n" );
	printf( "This example demonstrates Nova logging in a bare-metal\n" );
	printf( "environment without standard library dependencies.\n" );
	printf( "\n" );
	printf( "NOTE: This is a runnable demo. In real bare-metal:\n" );
	printf( "  - No printf/stdout (use UART hardware)\n" );
	printf( "  - No putchar (write directly to registers)\n" );
	printf( "  - Main loop runs forever\n" );
	printf( "  - Use actual hardware timers for timestamps\n" );
	printf( "=======================================================\n\n" );

	systemInit();

	peripheralDriverExample();

	// demo: run a few iterations instead of infinite loop
	// in real bare-metal, this would be: while ( true ) { ... }
	printf( "\n--- Main Loop (running 3 iterations for demo) ---\n" );
	for ( int iteration = 0; iteration < 3; ++iteration )
	{
		// application logic here

		// periodic logging
		NOVA_LOG( SystemTag ) << "Main loop iteration " << iteration;

		// simulate delay
		for ( volatile int i = 0; i < 100000; ++i );
	}

	printf( "\n--- Demo Complete ---\n" );
	printf( "In real bare-metal, the main loop would run forever.\n" );

	return 0;
}

/*
 * PLATFORM NOTES:
 *
 * 1. Timestamp Implementation:
 *    - current example uses SysTick (may wrap quickly on 32-bit)
 *    - for production: extend to 64-bit using overflow interrupt
 *    - or use RTC for lower resolution but no wrap
 *
 * 2. Atomic Safety:
 *    - volatile pointer is NOT multi-core safe
 *    - for multi-core: implement proper atomics using LDREX/STREX
 *    - see platform/atomic.h for examples
 *
 * 3. Memory Layout:
 *    - all logging uses stack allocation
 *    - no heap required (zero malloc/free calls)
 *    - static sink lifetime managed by application
 *
 * 4. Interrupt Safety:
 *    - logging from ISR is safe (atomic sink load)
 *    - sink must be interrupt-safe (UartSink is)
 *    - don't bind/unbind sinks from ISR
 *
 * 5. Stack Usage:
 *    - TruncatingRecordBuilder<Tag, 256>: ~256 bytes per log call
 *    - adjust buffer size based on stack constraints
 *    - consider smaller buffers for ISR logging
 *
 * 6. Build Configuration:
 *    - -DNOVA_BARE_METAL enables all no-std features
 *    - -fno-exceptions -fno-rtti for embedded
 *    - -Os or -O2 for size/performance optimization
 *    - link with nostdlib + minimal startup code
 */
