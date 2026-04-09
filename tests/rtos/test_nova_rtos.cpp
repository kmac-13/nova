/**
 * @file test_nova_rtos.cpp
 * @brief Nova core execution test under FreeRTOS (no GTest dependency).
 *
 * Runs on two CI targets:
 *   - POSIX simulator (hosted Linux, native execution, no QEMU)
 *   - ARM Cortex-M3 under qemu-system-arm -machine mps2-an385 -semihosting
 *
 * The test exercises Nova core under NOVA_RTOS conditions (NOVA_NO_TLS
 * implied, so NOVA_LOG falls back to stack-based logging).  It is
 * deliberately narrow: compile-time path coverage and basic record delivery
 * are the goals.  Async sinks, file I/O, and Flare are not tested here
 * because Nova Extras is disabled in RTOS mode.
 *
 * Test structure
 * --------------
 *   A single FreeRTOS task (testTask) runs the checks and exits the process
 *   directly on both pass and fail.  On ARM, semihostingExit() is called so
 *   QEMU translates the exit code to a host process result.  On POSIX,
 *   _Exit() is called, which terminates the process immediately without
 *   running atexit handlers or cancelling pthreads.  This seems to be the only
 *   reliable termination pattern on the POSIX port: vTaskDelete, vTaskSuspend,
 *   and vTaskEndScheduler all trigger the port's configASSERT(pdFALSE) guard
 *   in prvWaitForStart when the task function returns or the scheduler tears
 *   down active threads.
 *
 * Inline RamSink
 * --------------
 *   A fixed-buffer sink is defined here rather than pulling in Nova Extras
 *   (which is disabled in RTOS mode).
 */

/* FreeRTOS - must be first before any other headers that pull in stdlib */
#include "FreeRTOS.h"
#include "task.h"

/* Nova core */
#include <kmac/nova/nova.h>
#include <kmac/nova/scoped_configurator.h>
#include <kmac/nova/sink.h>

/* Semihosting exit (ARM) / no-op stub (POSIX) */
#include "qemu/semihosting.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// ============================================================================
// Test infrastructure
// ============================================================================

static char g_failMsg[ 128 ] = {};

// Formats a failure message into g_failMsg for CHECK to print before exiting.
static void failTest( const char* expr, const char* file, int line )
{
	snprintf( g_failMsg, sizeof( g_failMsg ), "FAIL %s:%d  %s", file, line, expr );
}

// CHECK( expr, n ) - evaluates expr and exits with code n on failure.
//
// On ARM, the exit code is passed directly to semihostingExit() so QEMU
// propagates it as the CI step result, making the failing check immediately
// visible in the log (e.g. exit code 3 = check 3 failed).
// On POSIX _Exit() is used with the same code; printf prints the message first.
//
// Exit code convention:
//   0   = all tests passed
//   1-N = check N failed
//   99  = vTaskStartScheduler returned unexpectedly
// clang-format off
#if defined( __arm__ ) || defined( __ARM_ARCH )
#define CHECK( expr, n ) \
	do { \
		if ( ! ( expr ) ) { \
			semihostingExit( n ); \
		} \
	} while( 0 )
#else
#define CHECK( expr, n ) \
	do { \
		if ( ! ( expr ) ) { \
			failTest( #expr, __FILE__, __LINE__ ); \
			printf( "%s\n", g_failMsg ); \
			_Exit( n ); \
		} \
	} while( 0 )
#endif

// clang-format on

// ============================================================================
// Nova tag
// ============================================================================

struct RtosTestTag {};

static std::uint64_t rtosTimestamp() noexcept { return 0ULL; }

NOVA_LOGGER_TRAITS( RtosTestTag, RTOS_TEST, true, rtosTimestamp );

// ============================================================================
// Inline RamSink - fixed buffer, no heap, no thread-safety
// ============================================================================

/**
 * Minimal fixed-buffer sink for RTOS test environments.
 *
 * Stores the message from the most recently delivered record.  A new record
 * overwrites the previous contents.  Buffer is null-terminated for contains()
 * comparisons.  messageSize is capped at BUF_SIZE - 1; overflow is silently
 * truncated (consistent with TruncatingRecordBuilder's own contract).
 *
 * Not thread-safe: intended for single-task test use only.
 */
class RamSink : public kmac::nova::Sink
{
private:
	static constexpr std::size_t BUF_SIZE = 128;

	char _buf[ BUF_SIZE ] = {};
	std::size_t _size = 0;
	std::size_t _count = 0;

public:
	// Returns true if the last record's message contains the given substring.
	bool contains( const char* needle ) const noexcept;

	std::size_t recordCount() const noexcept;

	// Returns true if any record has been delivered.
	bool received() const noexcept;

	void process( const kmac::nova::Record& record ) noexcept override;

	void clear() noexcept;
};

// ============================================================================
// Test task
// ============================================================================

static void testTask( void* /*params*/ );

// ============================================================================
// Entry point
// ============================================================================

int main( void )
{
	// stack size in words: 512 words = 2K bytes, sufficient for Nova's
	// stack builder (NOVA_DEFAULT_BUFFER_SIZE = 256 bytes) plus FreeRTOS
	// task overhead and the test frame
	static constexpr uint16_t TASK_STACK_WORDS = 512U;

	xTaskCreate(
		testTask,
		"NovaTest",
		TASK_STACK_WORDS,
		nullptr,
		tskIDLE_PRIORITY + 1U,
		nullptr
	);

	vTaskStartScheduler();

	// should never reach here: testTask exits the process directly via
	// semihostingExit() on ARM or _Exit() on POSIX before the scheduler
	// would ever return;
	// treat as a hard failure (e.g. heap exhaustion during xTaskCreate
	// preventing the scheduler from starting)
	printf( "FAIL: vTaskStartScheduler returned unexpectedly\n" );

#if defined( __arm__ ) || defined( __ARM_ARCH )
	semihostingExit( -99 );
#else
	_Exit( -99 );
#endif

	return 0;
}

bool RamSink::contains( const char* needle ) const noexcept
{
	return std::strstr( _buf, needle ) != nullptr;
}

std::size_t RamSink::recordCount() const noexcept
{
	return _count;
}

// Returns true if any record has been delivered.
bool RamSink::received() const noexcept
{
	return _count > 0;
}

void RamSink::process( const kmac::nova::Record& record ) noexcept
{
	const std::size_t len = record.messageSize < BUF_SIZE
		? record.messageSize
		: BUF_SIZE - 1;
	std::memcpy( _buf, record.message, len );
	_buf[ len ] = '\0';
	_size = len;
	_count++;
}

void RamSink::clear() noexcept
{
	_buf[ 0 ] = '\0';
	_size = 0;
	_count = 0;
}

static void testTask( void* /*params*/ )
{
	RamSink sink;
	kmac::nova::ScopedConfigurator config;
	config.bind< RtosTestTag >( &sink );

	// ------------------------------------------------------------------
	// Test 1 & 2: basic string delivery and verification
	// ------------------------------------------------------------------
	{
		NOVA_LOG_STACK( RtosTestTag ) << "hello rtos";
		CHECK( sink.received(), -1 );
		CHECK( sink.contains( "hello rtos" ), -2 );
		sink.clear();
	}

	// ------------------------------------------------------------------
	// Test 3: integer append (exercises NOVA_NO_CHARCONV fallback path)
	// ------------------------------------------------------------------
	{
		NOVA_LOG_STACK( RtosTestTag ) << "count=" << 42;
		CHECK( sink.contains( "count=42" ), -3 );
		sink.clear();
	}

	// ------------------------------------------------------------------
	// Test 4: negative integer
	// ------------------------------------------------------------------
	{
		NOVA_LOG_STACK( RtosTestTag ) << "neg=" << -7;
		CHECK( sink.contains( "neg=-7" ), -4 );
		sink.clear();
	}

	// ------------------------------------------------------------------
	// Test 5, 6, & 7: multiple appends in one record
	// ------------------------------------------------------------------
	{
		NOVA_LOG_STACK( RtosTestTag ) << "a=" << 1 << " b=" << 2 << " c=" << 3;
		CHECK( sink.contains( "a=1" ), -5 );
		CHECK( sink.contains( "b=2" ), -6 );
		CHECK( sink.contains( "c=3" ), -7 );
		sink.clear();
	}

	// ------------------------------------------------------------------
	// Test 8: unsigned integer (exercises unsigned path in intToChars)
	// ------------------------------------------------------------------
	{
		NOVA_LOG_STACK( RtosTestTag ) << "u=" << 255u;
		CHECK( sink.contains( "u=255" ), -8 );
		sink.clear();
	}

	// ------------------------------------------------------------------
	// all tests passed, exit the process directly from task context
	// ------------------------------------------------------------------

#if defined( __arm__ ) || defined( __ARM_ARCH )
	semihostingExit( 0 );
#else
	_Exit( 0 );
#endif
}
