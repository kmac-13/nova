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
 * - C++11 or later: required (enforced with #error)
 *
 * Nova uses C++11+ features including if constexpr, std::string_view, and
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
 *   Implies NOVA_NO_TLS: many RTOS ports do not provide the runtime TLS
 *   support that thread_local requires.  FreeRTOS with newlib-nano is a
 *   common case: the compiler emits references to __tls_get_addr or
 *   __emutls_get_address that the RTOS startup code does not satisfy.
 *   If your RTOS port genuinely supports thread_local (e.g. FreeRTOS with
 *   configUSE_NEWLIB_REENTRANT=1 and full newlib, QNX, or VxWorks 7),
 *   do not define NOVA_RTOS manually.  Let auto-detection set only the
 *   NOVA_PLATFORM_* marker for your RTOS, then define NOVA_NO_TLS yourself
 *   only if needed.  NOVA_NO_TLS is not implied by NOVA_PLATFORM_* alone.
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
 * NOVA_NO_STRING_VIEW : disable std::string_view; a minimal (pointer, length)
 *                    substitute is used instead.  Implied by NOVA_NO_STD.
 *                    Auto-detected via __has_include when possible.
 * NOVA_NO_TLS      : disable thread_local; all logging uses stack-based builders.
 *                    Unlike the flags above, this is NOT implied by NOVA_NO_STD -
 *                    TLS is a runtime dependency separate from stdlib header
 *                    availability.  Useful independently on hosted platforms where
 *                    injecting per-thread state into a host process is undesirable
 *                    (e.g. Android JNI-attached threads).
 * NOVA_NO_CHARCONV : disable <charconv> entirely.  All integer and
 *                    float/double append overloads fall back to platform
 *                    implementations in platform/int_to_chars.h and
 *                    platform/float_to_chars.h.  Required on bare-metal
 *                    toolchains where <charconv> is absent (e.g. arm-none-eabi
 *                    with newlib-nano).  Implied by NOVA_BARE_METAL.
 *                    NOT implied by NOVA_NO_STD since charconv absence is
 *                    toolchain-specific rather than stdlib-wide.
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
 * NOVA_ASSERT( x )
 *   Override Nova's internal assertion macro.  On hosted targets this defaults
 *   to assert() from <cassert>.  On bare-metal targets the default is a no-op
 *   placeholder - production bare-metal builds MUST define this to a meaningful
 *   fault handler before including any Nova header.  See the ASSERTIONS section
 *   below for examples.
 *
 * ============================================================================
 * Computed Capability Flags
 * ============================================================================
 * Set by this header based on the flags above and platform detection.
 * Do not define these manually.
 *
 * NOVA_HAS_STD_ATOMIC      : 1 if std::atomic is available, 0 otherwise
 * NOVA_HAS_STD_CHRONO      : 1 if std::chrono is available, 0 otherwise
 * NOVA_HAS_STD_ARRAY       : 1 if std::array is available, 0 otherwise
 * NOVA_HAS_STD_STRING_VIEW : 1 if std::string_view is available, 0 otherwise
 * NOVA_HAS_TLS             : 1 if thread_local is available, 0 otherwise
 * NOVA_HAS_THREADING       : 1 if threading primitives are available, 0 otherwise
 * NOVA_HAS_CHARCONV        : 1 if <charconv> header is present, 0 otherwise.
 *                            Does not imply that all to_chars overloads are
 *                            available (see NOVA_HAS_INT_CHARCONV and
 *                            NOVA_HAS_FLOAT_CHARCONV for usable overloads)
 * NOVA_HAS_INT_CHARCONV    : 1 if std::to_chars for integers is available
 *                            (requires C++17 on MinGW/GCC), 0 otherwise
 *                            (platform/int_to_chars.h fallback is used instead)
 * NOVA_HAS_FLOAT_CHARCONV  : 1 if std::to_chars for floating-point is
 *                            available (requires C++17), 0 otherwise
 *                            (platform/float_to_chars.h fallback is used instead)
 *
 * Platform markers (set when detected, undefined otherwise):
 * NOVA_PLATFORM_ARM_BAREMETAL, NOVA_PLATFORM_FREERTOS, NOVA_PLATFORM_ZEPHYR,
 * NOVA_PLATFORM_VXWORKS, NOVA_PLATFORM_QNX, NOVA_PLATFORM_THREADX,
 * NOVA_PLATFORM_EMBOS, NOVA_PLATFORM_POSIX, NOVA_PLATFORM_WINDOWS
 *
 * ============================================================================
 * Not Needed - Commonly Expected But Unnecessary
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
 * In most cases you do not need to set NOVA_NO_* flags manually - they will
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

	// newlib-nano omits <charconv> entirely.  Disable unconditionally so
	// bare-metal builds don't fail when the header is absent.
	#ifndef NOVA_NO_CHARCONV
		#define NOVA_NO_CHARCONV
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

	#ifndef NOVA_NO_STRING_VIEW
		#define NOVA_NO_STRING_VIEW
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

	// check for std::string_view availability
	#if defined( __has_include )
		#if ! __has_include( <string_view> ) && ! defined( NOVA_NO_STRING_VIEW )
			#define NOVA_NO_STRING_VIEW
		#endif
	#endif

#endif // ! NOVA_NO_STD && ! NOVA_BARE_METAL

// ============================================================================
// C++ VERSION DETECTION
// ============================================================================

// C++17 is 201703L, C++14 is 201402L, C++11 is 201103L
#if defined( _MSVC_LANG )
	#if _MSVC_LANG < 201103L
		#error "Nova requires C++11 or later. Use /std:c++11 or newer."
	#endif
#elif __cplusplus < 201103L
	#error "Nova requires C++11 or later. Use -std=c++11 or newer."
#endif

// ============================================================================
// RTOS CONVENIENCE MACRO
// ============================================================================

#ifdef NOVA_RTOS
	// implies NOVA_NO_TLS (see the NOVA_RTOS entry in the mode descriptor
	// documentation above for rationale and the override guidance)
	#ifndef NOVA_NO_TLS
		#define NOVA_NO_TLS
	#endif
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

// standard library string_view available?
#if ! defined( NOVA_NO_STRING_VIEW ) && ! defined( NOVA_NO_STD ) \
	&& ( __cplusplus >= 201703L || _MSVC_LANG >= 201703L )
	#define NOVA_HAS_STD_STRING_VIEW 1
#else
	#define NOVA_HAS_STD_STRING_VIEW 0
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

// <charconv> (std::to_chars) available?
// Absent on some bare-metal toolchains (e.g. arm-none-eabi with newlib-nano).
// When disabled, platform/int_to_chars.h and platform/float_to_chars.h provide
// fallback implementations without any libc dependency.
// Implied by NOVA_BARE_METAL; can also be set independently.
// Presence does not guarantee all to_chars overloads are available (int and
// float support are gated separately below)
#if ! defined( NOVA_NO_CHARCONV )
	#define NOVA_HAS_CHARCONV 1
#else
	#define NOVA_HAS_CHARCONV 0
#endif

// std::to_chars for integers available?
// Technically C++17; absent on MinGW/GCC in C++11/14 mode even when
// <charconv> is present.
#if NOVA_HAS_CHARCONV && ( __cplusplus >= 201703L || _MSVC_LANG >= 201703L )
	#define NOVA_HAS_INT_CHARCONV 1
#else
	#define NOVA_HAS_INT_CHARCONV 0
#endif

// std::to_chars for floating-point available?
// Requires C++17; floating-point overloads were not included in earlier
// standards even on platforms that provide integer to_chars.
#if NOVA_HAS_CHARCONV && ( __cplusplus >= 201703L || _MSVC_LANG >= 201703L )
	#define NOVA_HAS_FLOAT_CHARCONV 1
#else
	#define NOVA_HAS_FLOAT_CHARCONV 0
#endif

// NOVA_IF_CONSTEXPR expands to 'if constexpr' on C++17 and later,
// and plain 'if' on C++14 and earlier.  The condition must be a constexpr
// expression in both cases; on C++14 the compiler will optimise away the dead
// branch but instantiation of the disabled branch is not guaranteed to be suppressed.
// Used for non-logging compile-time branches where the non-instantiation
// guarantee of if constexpr is not required.
#if __cplusplus >= 201703L || _MSVC_LANG >= 201703L
	#define NOVA_IF_CONSTEXPR if constexpr
	#define NOVA_INLINE_VAR inline
#else
	#define NOVA_IF_CONSTEXPR if
	#define NOVA_INLINE_VAR
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

	#ifdef NOVA_RTOS
		#pragma message( "  NOVA_RTOS: enabled" )
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

	#if NOVA_HAS_STD_STRING_VIEW
		#pragma message( "  std::string_view: available" )
	#else
		#pragma message( "  std::string_view: NOT available (using platform::StringView)" )
	#endif

	#if NOVA_HAS_TLS
		#pragma message( "  thread_local: available (TLS-based builders enabled)" )
	#else
		#pragma message( "  thread_local: NOT available (stack-based builders only)" )
	#endif

	#if NOVA_HAS_CHARCONV
		#pragma message( "  charconv (std::to_chars): available" )
	#else
		#pragma message( "  charconv (std::to_chars): NOT available (using platform fallbacks)" )
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
// ASSERTIONS
// ============================================================================

#ifndef NOVA_ASSERT
	#if defined( NOVA_BARE_METAL )
		// Bare-metal targets must define NOVA_ASSERT before including Nova headers.
		// The no-op default below allows compilation but silently discards all
		// assertion failures - this is NOT safe for production use.  A failed
		// assertion in Nova (e.g. nested logging detected) will go unnoticed,
		// which can corrupt log output in ways that are difficult to diagnose.
		//
		// Always provide a meaningful implementation, for example:
		//   #define NOVA_ASSERT( x ) do { if ( ! ( x ) ) { bsp_halt(); } } while ( 0 )
		//   #define NOVA_ASSERT( x ) do { if ( ! ( x ) ) { __BKPT( 0 ); } } while ( 0 )
		//
		// The bare-metal example (examples/08_bare_metal/main.cpp) shows how to
		// define this before including Nova headers.
		#define NOVA_ASSERT( x ) ( (void) 0 )  /* PLACEHOLDER ONLY - replace for production */
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
