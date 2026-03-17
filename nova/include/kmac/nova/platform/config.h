#pragma once
#ifndef KMAC_NOVA_PLATFORM_CONFIG_H
#define KMAC_NOVA_PLATFORM_CONFIG_H

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
// config.h is a preprocessor-level platform configuration header.
// All macros here operate before the C++ compiler and cannot be replaced
// with constexpr constructs.  See clang-tidy.yml for the documented rationale.

/**
 * @file config.h
 * @brief Platform configuration and feature detection for Nova logging framework
 *
 * This header provides automatic detection of platform capabilities and allows
 * manual override via preprocessor defines for bare-metal and RTOS environments.
 *
 * C++ Version Requirements:
 * - **C++17 or later**: required (enforced with #error)
 *
 * Nova uses C++17 features including if constexpr, std::string_view, and
 * std::to_chars for zero-cost abstractions and optimal performance, and
 * __has_include for automatic configuration.
 *
 * Feature Flags:
 * - NOVA_NO_STD        : disable all standard library usage
 * - NOVA_NO_ATOMIC     : disable std::atomic (provide custom impl)
 * - NOVA_NO_CHRONO     : disable std::chrono (provide custom timestamp)
 * - NOVA_NO_ARRAY      : disable std::array (use C arrays)
 * - NOVA_NO_EXCEPTIONS : already supported (Nova doesn't use exceptions)
 * - NOVA_BARE_METAL    : alias for NO_STD + NO_ATOMIC + NO_CHRONO + NO_ARRAY
 * - NOVA_RTOS          : enable RTOS-specific features
 * - FLARE_NO_STDIO     : use raw file descriptors instead of FILE*
 *
 * Automatic Detection (using __has_include):
 * Nova will automatically detect standard library availability using __has_include.
 * If your compiler supports this feature, you typically don't need to manually
 * define NOVA_NO_* flags - they will be set automatically if headers are missing.
 *
 * Diagnostic Mode:
 * Define NOVA_DIAGNOSTICS to see what Nova detected at compile time:
 *   #define NOVA_DIAGNOSTICS
 *   #include <kmac/nova/logger.h>
 *
 * This will print configuration messages during compilation showing:
 * - which features are enabled/disabled
 * - which platform was detected
 * - whether __has_include is available
 *
 * Usage Examples:
 *
 *   // Example 1: full automatic detection (recommended)
 *   #include <kmac/nova/logger.h>
 *   // Nova auto-detects everything
 *
 *   // Example 2: bare-metal mode (explicit)
 *   #define NOVA_BARE_METAL
 *   #include <kmac/nova/logger.h>
 *
 *   // Example 3: RTOS with partial stdlib (fine-grained control)
 *   #define NOVA_NO_CHRONO  // no std::chrono, but have std::atomic
 *   #include <kmac/nova/logger.h>
 *
 *   // Example 4: debugging configuration
 *   #define NOVA_DIAGNOSTICS
 *   #include <kmac/nova/logger.h>
 *   // prints detected configuration during compilation
 */

// ============================================================================
// BARE-METAL CONVENIENCE MACRO
// ============================================================================

#ifdef NOVA_BARE_METAL
	#ifndef NOVA_NO_STD
		#define NOVA_NO_STD
	#endif

	#ifndef NOVA_NO_ATOMIC
		#define NOVA_NO_ATOMIC
	#endif

	#ifndef NOVA_NO_CHRONO
		#define NOVA_NO_CHRONO
	#endif

	#ifndef NOVA_NO_ARRAY
		#define NOVA_NO_ARRAY
	#endif
#endif

// ============================================================================
// STANDARD LIBRARY OVERRIDE
// ============================================================================

#ifdef NOVA_NO_STD
	// implies all no-std sub-features

	#ifndef NOVA_NO_ATOMIC
		#define NOVA_NO_ATOMIC
	#endif

	#ifndef NOVA_NO_CHRONO
		#define NOVA_NO_CHRONO
	#endif

	#ifndef NOVA_NO_ARRAY
		#define NOVA_NO_ARRAY
	#endif
#endif

// ============================================================================
// AUTOMATIC STANDARD LIBRARY DETECTION
// ============================================================================

// if user hasn't explicitly disabled features, try to auto-detect them,
// uses __has_include (C++17 feature)
#if ! defined( NOVA_NO_STD ) && ! defined( NOVA_BARE_METAL )

	// check for std::atomic availability
	#if defined( __has_include )
		#if ! __has_include( <atomic> ) && ! defined( NOVA_NO_ATOMIC )
			#define NOVA_NO_ATOMIC
		#endif
	#endif

    // check for std::chrono availability
	#if defined( __has_include )
		#if ! __has_include( <chrono> ) && ! defined( NOVA_NO_CHRONO )
			#define NOVA_NO_CHRONO
		#endif
	#endif

	// check for std::array availability
	#if defined( __has_include )
		#if ! __has_include( <array> ) && ! defined( NOVA_NO_ARRAY )
			#define NOVA_NO_ARRAY
		#endif
	#endif

#endif // ! NOVA_NO_STD && ! NOVA_BARE_METAL

// ============================================================================
// C++ VERSION DETECTION
// ============================================================================

#if defined( _MSVC_LANG )
	#if _MSVC_LANG < 201703L
		#error "Nova requires C++17 or later. Use /std:c++17 or newer."
	#endif
#elif __cplusplus < 201703L
	#error "Nova requires C++17 or later. Use -std=c++17 or newer."
#endif

// ============================================================================
// PLATFORM DETECTION
// ============================================================================

// ARM Cortex-M bare-metal (no OS)
#if defined( __ARM_ARCH ) && ! defined( __unix__ ) && ! defined( _WIN32 ) && ! defined( __APPLE__ )
	#define NOVA_PLATFORM_ARM_BAREMETAL
#endif

// FreeRTOS - multiple detection methods
#if defined( INC_FREERTOS_H ) || defined( FREERTOS_CONFIG_H ) || defined( __FREERTOS__ )
	#define NOVA_PLATFORM_FREERTOS
	#ifndef NOVA_RTOS
		#define NOVA_RTOS
	#endif
#endif

// Zephyr RTOS
#if defined( __ZEPHYR__ )
	#define NOVA_PLATFORM_ZEPHYR
	#ifndef NOVA_RTOS
		#define NOVA_RTOS
	#endif
#endif

// VxWorks
#if defined( __VXWORKS__ ) || defined( _VX_CPU ) || defined( __vxworks )
	#define NOVA_PLATFORM_VXWORKS
	#ifndef NOVA_RTOS
		#define NOVA_RTOS
	#endif
#endif

// QNX
#if defined( __QNX__ ) || defined( __QNXNTO__ )
	#define NOVA_PLATFORM_QNX
	#ifndef NOVA_RTOS
		#define NOVA_RTOS
	#endif
#endif

// ThreadX (Azure RTOS)
#if defined( TX_THREAD_H ) || defined( THREADX )
	#define NOVA_PLATFORM_THREADX
	#ifndef NOVA_RTOS
		#define NOVA_RTOS
	#endif
#endif

// embOS
#if defined( __EMBOS__ )
	#define NOVA_PLATFORM_EMBOS
	#ifndef NOVA_RTOS
		#define NOVA_RTOS
	#endif
#endif

// POSIX systems (Linux, BSD, etc.)
#if defined( __unix__ ) || defined( __APPLE__ )
	#define NOVA_PLATFORM_POSIX
#endif

// Windows
#if defined( _WIN32 ) || defined( _WIN64 )
	#define NOVA_PLATFORM_WINDOWS
#endif

// ============================================================================
// FEATURE AVAILABILITY
// ============================================================================

// standard library atomic available?
#if ! defined( NOVA_NO_ATOMIC ) && ! defined( NOVA_NO_STD )
	#define NOVA_HAS_STD_ATOMIC 1
#else
	#define NOVA_HAS_STD_ATOMIC 0
#endif

// standard library chrono available?
#if ! defined( NOVA_NO_CHRONO ) && ! defined( NOVA_NO_STD )
	#define NOVA_HAS_STD_CHRONO 1
#else
	#define NOVA_HAS_STD_CHRONO 0
#endif

// standard library array available?
#if ! defined( NOVA_NO_ARRAY ) && ! defined( NOVA_NO_STD )
	#define NOVA_HAS_STD_ARRAY 1
#else
	#define NOVA_HAS_STD_ARRAY 0
#endif

// threading support available?
#if defined( NOVA_RTOS ) || defined( NOVA_PLATFORM_POSIX ) || defined( NOVA_PLATFORM_WINDOWS )
	#define NOVA_HAS_THREADING 1
#else
	#define NOVA_HAS_THREADING 0
#endif

// ============================================================================
// DIAGNOSTIC MODE
// ============================================================================

// define NOVA_DIAGNOSTICS before including Nova headers to see configuration
#ifdef NOVA_DIAGNOSTICS
	#pragma message( "Nova Platform Configuration:" )

	#ifdef NOVA_BARE_METAL
		#pragma message( "  NOVA_BARE_METAL: enabled" )
	#endif

	#ifdef NOVA_NO_STD
		#pragma message( "  NOVA_NO_STD: enabled" )
	#endif

	#if NOVA_HAS_STD_ATOMIC
		#pragma message( "  std::atomic: available" )
	#else
		#pragma message( "  std::atomic: NOT available (using volatile)" )
	#endif

	#if NOVA_HAS_STD_CHRONO
		#pragma message( "  std::chrono: available" )
	#else
		#pragma message( "  std::chrono: NOT available (user must implement)" )
	#endif

	#if NOVA_HAS_STD_ARRAY
		#pragma message( "  std::array: available" )
	#else
		#pragma message( "  std::array: NOT available (using C array wrapper)" )
	#endif

	#ifdef NOVA_PLATFORM_ARM_BAREMETAL
		#pragma message( "  Platform: ARM bare-metal" )
	#endif

	#ifdef NOVA_PLATFORM_FREERTOS
		#pragma message( "  Platform: FreeRTOS" )
	#endif

	#ifdef NOVA_PLATFORM_ZEPHYR
		#pragma message( "  Platform: Zephyr" )
	#endif

	#ifdef NOVA_PLATFORM_VXWORKS
		#pragma message( "  Platform: VxWorks" )
	#endif

	#ifdef NOVA_PLATFORM_QNX
		#pragma message( "  Platform: QNX" )
	#endif

	#ifdef NOVA_PLATFORM_THREADX
		#pragma message( "  Platform: ThreadX" )
	#endif

	#ifdef NOVA_PLATFORM_EMBOS
		#pragma message( "  Platform: embOS" )
	#endif

	#ifdef NOVA_PLATFORM_POSIX
		#pragma message( "  Platform: POSIX" )
	#endif

	#ifdef NOVA_PLATFORM_WINDOWS
		#pragma message( "  Platform: Windows" )
	#endif

	#if defined( __has_include )
		#pragma message( "  __has_include: supported" )
	#else
		#pragma message( "  __has_include: NOT supported (manual configuration required)" )
	#endif
#endif // NOVA_DIAGNOSTICS

// ============================================================================
// COMPILER FEATURE DETECTION
// ============================================================================

// constexpr support (C++17 required)
// #define NOVA_CONSTEXPR constexpr

// inline namespace support (C++17 has this)
// #define NOVA_INLINE_NAMESPACE inline namespace

// ============================================================================
// ASSERTIONS
// ============================================================================

#ifndef NOVA_ASSERT
	#if defined( NOVA_BARE_METAL )
		// bare-metal: user must provide their own assert
		// example: #define NOVA_ASSERT( x ) if ( ! ( x ) ) { bsp_halt(); }
		#ifndef NOVA_ASSERT
			#define NOVA_ASSERT( x ) ( (void) 0 )  // default: no-op (unsafe but compiles)
		#endif
	#else
		#include <cassert>
		#define NOVA_ASSERT( x ) assert( x )
	#endif
#endif

// ============================================================================
// DEPRECATION WARNINGS
// ============================================================================

#if defined( __GNUC__ ) || defined( __clang__ )
	#define NOVA_DEPRECATED( msg ) __attribute__( ( deprecated( msg ) ) )
#elif defined( _MSC_VER )
	#define NOVA_DEPRECATED( msg ) __declspec( deprecated( msg ) )
#else
	#define NOVA_DEPRECATED( msg )
#endif

// ============================================================================
// DOCUMENTATION MARKERS
// ============================================================================

// mark functions that must be implemented by user in bare-metal
#define NOVA_USER_IMPL

// mark platform-specific code
#define NOVA_PLATFORM_SPECIFIC

// NOLINTEND(cppcoreguidelines-macro-usage)

#endif // KMAC_NOVA_PLATFORM_CONFIG_H
