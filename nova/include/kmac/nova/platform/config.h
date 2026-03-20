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
 * - C++17 or later: required (enforced with #error)
 *
 * Nova uses C++17 features including if constexpr, std::string_view, and
 * std::to_chars for zero-cost abstractions and optimal performance, and
 * __has_include for automatic configuration.
 *
 * ============================================================================
 * Mode Descriptors
 * ============================================================================
 * Define before including any Nova header to set the overall operating context.
 *
 * NOVA_BARE_METAL
 *   Bare-metal / freestanding target (no OS, no stdlib).  Implies NOVA_NO_STD,
 *   NOVA_NO_ATOMIC, NOVA_NO_CHRONO, NOVA_NO_ARRAY, and NOVA_NO_TLS.
 *   Individual flags can still be overridden after this is set.
 *
 * NOVA_RTOS
 *   RTOS target.  Set automatically when a supported RTOS is detected
 *   (FreeRTOS, Zephyr, VxWorks, QNX, ThreadX, embOS).  Can also be set
 *   manually for unsupported RTOS environments.
 *
 * FLARE_NO_STDIO
 *   Flare only.  Use raw file descriptors (open/write/close) instead of
 *   FILE* for async-signal-safe emergency logging.
 *
 * ============================================================================
 * Fine-Grained Disable Flags
 * ============================================================================
 * Set individually for partial stdlib environments (e.g. RTOS with some but
 * not all stdlib headers available).  All are implied by NOVA_BARE_METAL.
 * Auto-detected via __has_include where possible.
 *
 * NOVA_NO_STD      : disable all stdlib usage (implies the flags below)
 * NOVA_NO_ATOMIC   : disable std::atomic; provide AtomicPtr via platform/atomic.h
 * NOVA_NO_CHRONO   : disable std::chrono; implement steadyNanosecs() in platform/chrono.h
 * NOVA_NO_ARRAY    : disable std::array; C-style array wrapper used instead
 * NOVA_NO_TLS      : disable thread_local; all logging uses stack-based builders.
 *                    Unlike the flags above, this is NOT implied by NOVA_NO_STD —
 *                    TLS is a runtime dependency separate from stdlib header
 *                    availability.  Useful independently on hosted platforms where
 *                    injecting per-thread state into a host process is undesirable
 *                    (e.g. Android JNI-attached threads).
 *
 * ============================================================================
 * Opt-In Features
 * ============================================================================
 * Never auto-enabled.  Define before including Nova headers to activate.
 *
 * NOVA_ENABLE_FRAME_COUNTER
 *   Enable the platform::frameCounter() timestamp source for game engines and
 *   simulation systems.  Requires g_frameCounter in the kmac::nova::platform
 *   namespace.  Disabled by default to avoid link errors on bare-metal
 *   toolchains that resolve all extern symbols regardless of call sites.
 *   See platform/chrono.h for usage.
 *
 * NOVA_ENABLE_DIAGNOSTICS
 *   Print compile-time configuration messages showing which features and
 *   platforms Nova detected.  Useful for verifying bare-metal or RTOS setup.
 *   See the diagnostics section below.
 *
 * ============================================================================
 * Computed Capability Flags
 * ============================================================================
 * Set by this header based on the flags above and platform detection.
 * Do not define these manually.
 *
 * NOVA_HAS_STD_ATOMIC   : 1 if std::atomic is available, 0 otherwise
 * NOVA_HAS_STD_CHRONO   : 1 if std::chrono is available, 0 otherwise
 * NOVA_HAS_STD_ARRAY    : 1 if std::array is available, 0 otherwise
 * NOVA_HAS_TLS          : 1 if thread_local is available, 0 otherwise
 * NOVA_HAS_THREADING    : 1 if threading primitives are available, 0 otherwise
 *
 * Platform markers (set when detected, undefined otherwise):
 * NOVA_PLATFORM_ARM_BAREMETAL, NOVA_PLATFORM_FREERTOS, NOVA_PLATFORM_ZEPHYR,
 * NOVA_PLATFORM_VXWORKS, NOVA_PLATFORM_QNX, NOVA_PLATFORM_THREADX,
 * NOVA_PLATFORM_EMBOS, NOVA_PLATFORM_POSIX, NOVA_PLATFORM_WINDOWS
 *
 * ============================================================================
 * Not Needed — Commonly Expected But Unnecessary
 * ============================================================================
 * Nova's architecture makes several flags that other logging libraries expose
 * unnecessary.  If you are looking for one of these, here is why it does not
 * exist:
 *
 * LOG_LEVEL_*, MIN_LOG_LEVEL, MAX_LOG_LEVEL and similar severity flags:
 *   Nova uses types, not severity levels.  Compile-time filtering is controlled
 *   by logger_traits<Tag>::enabled.  Runtime filtering is via sink binding and
 *   unbinding, or a filtering sink (e.g. FilterSink from Nova Extras, or your
 *   own Sink implementation).
 *
 * ENABLE_ASYNC, DISABLE_THREADING, THREAD_SAFE, ENABLE_BUFFERING,
 * DISABLE_FLUSH_ON_WRITE, ENABLE_CUSTOM_FORMATTING and similar:
 *   Threading, buffering, flushing, and formatting are sink-level decisions,
 *   not library-wide modes.  Choose the appropriate sink(s) from Nova Extras
 *   or implement your own.
 *
 * NO_EXCEPTIONS, DISABLE_EXCEPTIONS and similar:
 *   Nova never uses exceptions.  Pass -fno-exceptions to the compiler directly;
 *   no Nova flag is needed.
 *
 * HEADER_ONLY:
 *   Nova core is always header-only.  No flag needed.
 *
 * ============================================================================
 * Automatic Detection
 * ============================================================================
 * Nova uses __has_include (C++17) to auto-detect stdlib header availability.
 * In most cases you do not need to set NOVA_NO_* flags manually — they will
 * be set automatically if the corresponding headers are absent.
 *
 * ============================================================================
 * Usage Examples
 * ============================================================================
 *
 *   // Example 1: full automatic detection (recommended for hosted targets)
 *   #include <kmac/nova/logger.h>
 *
 *   // Example 2: bare-metal (ARM Cortex-M, no OS)
 *   #define NOVA_BARE_METAL
 *   #include <kmac/nova/logger.h>
 *
 *   // Example 3: RTOS with partial stdlib (fine-grained control)
 *   #define NOVA_NO_CHRONO  // no std::chrono, but std::atomic is available
 *   #include <kmac/nova/logger.h>
 *
 *   // Example 4: hosted platform, disable TLS only (e.g. Android JNI library)
 *   #define NOVA_NO_TLS
 *   #include <kmac/nova/logger.h>
 *
 *   // Example 5: print detected configuration during compilation
 *   #define NOVA_ENABLE_DIAGNOSTICS
 *   #include <kmac/nova/logger.h>
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

	// TLS has a separate runtime dependency from the stdlib headers above.
	// On bare-metal toolchains (e.g. arm-none-eabi with newlib-nano), thread_local
	// compiles but links against __tls_get_addr or __emutls_get_address, which
	// bare-metal startup code does not provide.  Disable unconditionally here;
	// hosted targets that want stack-based builders can set NOVA_NO_TLS independently.
	#ifndef NOVA_NO_TLS
		#define NOVA_NO_TLS
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

// thread_local storage available?
// TLS is a runtime dependency separate from stdlib header availability.
// Disabled by NOVA_BARE_METAL or NOVA_NO_TLS; can also be set independently
// on hosted platforms where stack-based builders are preferred (e.g. Android
// library code that should not inject per-thread state into the host process).
#if ! defined( NOVA_NO_TLS )
	#define NOVA_HAS_TLS 1
#else
	#define NOVA_HAS_TLS 0
#endif

// ============================================================================
// DIAGNOSTIC MODE
// ============================================================================

// define NOVA_ENABLE_DIAGNOSTICS before including Nova headers to see configuration
#ifdef NOVA_ENABLE_DIAGNOSTICS
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

	#if NOVA_HAS_TLS
		#pragma message( "  thread_local: available (TLS-based builders enabled)" )
	#else
		#pragma message( "  thread_local: NOT available (stack-based builders only)" )
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
#endif // NOVA_ENABLE_DIAGNOSTICS

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
