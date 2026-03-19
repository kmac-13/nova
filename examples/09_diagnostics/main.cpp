/**
 * @file main.cpp
 * @brief Example demonstrating Nova's auto-detection and diagnostic features
 * 
 * This example shows how to use Nova's automatic platform detection and
 * diagnostic mode to understand what features are available on your platform.
 * 
 * Build with:
 *   g++ -std=c++17 -I<nova_include> main.cpp
 * 
 * The compiler will output messages showing detected configuration.
 */

// ============================================================================
// EXAMPLE 1: Full Automatic Detection
// ============================================================================

// enable diagnostics to see what Nova detected
#define NOVA_ENABLE_DIAGNOSTICS

// just include Nova - it will auto-detect everything
#include <kmac/nova/nova.h>

// you'll see compile-time output like:
//   Nova Platform Configuration:
//     std::atomic: available
//     std::chrono: available
//     std::array: available
//     platform: POSIX (or Windows)
//     __has_include: supported

// ============================================================================
// EXAMPLE 2: Verify Configuration Programmatically
// ============================================================================

#include <iostream>
#include <cstdio>

void print_configuration()
{
	printf( "\n=== Nova Configuration ===\n" );

	#if NOVA_HAS_STD_ATOMIC
		printf( "std::atomic: Available\n" );
	#else
		printf( "std::atomic: NOT available (using volatile)\n" );
	#endif

	#if NOVA_HAS_STD_CHRONO
		printf( "std::chrono: Available\n" );
	#else
		printf( "std::chrono: NOT available (requires user implementation)\n" );
	#endif

	#if NOVA_HAS_STD_ARRAY
		printf( "std::array: Available\n" );
	#else
		printf( "std::array: NOT available (using C array wrapper)\n" );
	#endif

	#if NOVA_HAS_THREADING
		printf( "Threading: Supported\n" );
	#else
		printf( "Threading: Not detected\n" );
	#endif

	// platform detection
	#ifdef NOVA_PLATFORM_ARM_BAREMETAL
		printf( "Platform: ARM Bare-Metal\n" );
	#endif

	#ifdef NOVA_PLATFORM_FREERTOS
		printf( "Platform: FreeRTOS\n" );
	#endif

	#ifdef NOVA_PLATFORM_ZEPHYR
		printf( "Platform: Zephyr\n" );
	#endif

	#ifdef NOVA_PLATFORM_VXWORKS
		printf( "Platform: VxWorks\n" );
	#endif

	#ifdef NOVA_PLATFORM_QNX
		printf( "Platform: QNX\n" );
	#endif

	#ifdef NOVA_PLATFORM_THREADX
		printf( "Platform: ThreadX\n" );
	#endif

	#ifdef NOVA_PLATFORM_EMBOS
		printf( "Platform: embOS\n" );
	#endif

	#ifdef NOVA_PLATFORM_POSIX
		printf( "Platform: POSIX (Linux/Unix/macOS)\n" );
	#endif

	#ifdef NOVA_PLATFORM_WINDOWS
		printf( "Platform: Windows\n" );
	#endif

	#ifdef NOVA_BARE_METAL
		printf( "Mode: BARE-METAL\n" );
	#endif

	#ifdef NOVA_RTOS
		printf( "Mode: RTOS\n" );
	#endif

	#ifdef __has_include
		printf( "__has_include: Supported (auto-detection enabled)\n" );
	#else
		printf( "__has_include: NOT supported (manual config required)\n" );
	#endif
}

// ============================================================================
// EXAMPLE 3: Simple Logging Test
// ============================================================================

struct InfoTag {};
NOVA_LOGGER_TRAITS( InfoTag, INFO, true, kmac::nova::TimestampHelper::steadyNanosecs );

// simple console sink (C++11 compatible)
class ConsoleSink : public kmac::nova::Sink
{
public:
	void process( const kmac::nova::Record& record ) noexcept override
	{
		printf( "[%s] ", record.tag );
		fwrite( record.message, 1, record.messageSize, stdout );
		printf( "\n" );
	}
};

int main()
{
	print_configuration();

	printf( "\n=== Testing Nova Logging ===\n" );

	// create a simple sink
	ConsoleSink sink;
	kmac::nova::Logger< InfoTag >::bindSink( &sink );

	NOVA_LOG( InfoTag ) << "Hello from Nova!";
	NOVA_LOG( InfoTag ) << "C++ version: " << __cplusplus;

	printf( "\nLogging test complete!\n" );

	return 0;
}

// ============================================================================
// NOTES
// ============================================================================

/*
 * Diagnostic Messages During Compilation:
 * 
 * When you compile this with NOVA_ENABLE_DIAGNOSTICS defined, you'll see compile-time
 * messages like:
 * 
 *   note: #pragma message: Nova Platform Configuration:
 *   note: #pragma message:   std::atomic: available
 *   note: #pragma message:   std::chrono: available
 *   note: #pragma message:   std::array: available
 *   note: #pragma message:   Platform: POSIX
 *   note: #pragma message:   __has_include: supported
 * 
 * This helps you verify that Nova detected your platform correctly.
 * 
 * Common Use Cases:
 * 
 * 1. Porting to new platform:
 *    - enable NOVA_ENABLE_DIAGNOSTICS
 *    - check what was auto-detected
 *    - manually define NOVA_NO_* flags if needed
 * 
 * 2. Troubleshooting compilation errors:
 *    - enable NOVA_ENABLE_DIAGNOSTICS
 *    - verify expected features are detected
 *    - add missing defines if auto-detection failed
 * 
 * 3. CI/CD verification:
 *    - build with NOVA_ENABLE_DIAGNOSTICS
 *    - parse compiler output
 *    - verify configuration matches expectations
 * 
 * 4. Cross-compilation:
 *    - different toolchains may report different capabilities
 *    - use diagnostics to verify each target configuration
 */
