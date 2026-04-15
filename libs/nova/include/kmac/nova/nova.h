#pragma once
#ifndef KMAC_NOVA_MACROS_H
#define KMAC_NOVA_MACROS_H

#include "truncating_logging.h"

namespace kmac {
namespace nova {

/**
 * Configuration: Default buffer size for all builders.
 */
NOVA_INLINE_VAR constexpr std::size_t NOVA_DEFAULT_BUFFER_SIZE =
#ifdef NOVA_DEFAULT_BUFFER_SIZE_OVERRIDE
	NOVA_DEFAULT_BUFFER_SIZE_OVERRIDE;
#else
	1024;
#endif

} // namespace nova
} // namespace kmac

#define FILE_NAME ::kmac::nova::details::fileName( __FILE__ )


// The following macros cannot be replaced with constexpr template functions:
// - __FILE__, __func__, __LINE__ require macro expansion at the call site (C++20
//   std::source_location would allow functions, but Nova targets C++14/17)
// - if constexpr (C++17 and later) eliminates disabled tag branches entirely; a
//   function call cannot suppress evaluation of its own arguments
// - if (pre-C++17) is not guaranteed to disable tag branches, relying on
//   compiler optimizations to compile out the code
// - alias macros expand to the above and inherit the same constraints
// NOLINT comments suppress cppcoreguidelines-macro-usage on each definition.

/**
 * @brief Thread-local logger with default buffer size (1024 bytes).
 *
 * High-performance logging using thread-local storage.  Provides better
 * performance than stack-based logging with minimal memory overhead.
 * Truncates with a "..." marker when the buffer is full.
 *
 * Features:
 * - zero heap allocation (completely deterministic)
 * - thread-local storage (one buffer per thread)
 * - tag-agnostic (buffer shared across all tags on same thread)
 * - truncates with "..." marker when buffer full
 * - nested logging detection (assert in debug, silent drop in release)
 *
 * Memory cost:
 * - 1KB per thread (default buffer size)
 * - example: 128 threads = 128KB total
 *
 * Usage:
 *   NOVA_LOG(InfoTag) << "User " << username << " logged in";
 *
 * When TagType is compile-time disabled (logger_traits<TagType>::enabled == false),
 * the entire statement including all streaming operations is completely compiled out.
 *
 * Important warnings:
 * - do NOT use in signal handlers (use NOVA_LOG_STACK instead)
 * - do NOT log inside expressions being logged (causes nested logging corruption)
 * - DO use NOVA_LOG_STACK for functions called within log expressions
 *
 * When NOVA_NO_TLS is defined (e.g. NOVA_BARE_METAL or Android JNI library),
 * this macro falls back to NOVA_LOG_STACK transparently.
 *
 * For complete data preservation without truncation, include
 * <kmac/nova/extras/continuation_logging.h> and use NOVA_LOG_CONT.
 *
 * @param TagType the logging tag type (must have logger_traits specialization)
 *
 * @see NOVA_LOG_BUF for custom buffer sizes
 * @see NOVA_LOG_STACK for stack-based logging (signal handlers, nested contexts)
 * @see TlsTruncBuilderWrapper for implementation details
 */
#define NOVA_LOG( TagType ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	NOVA_LOG_BUF( TagType, ::kmac::nova::NOVA_DEFAULT_BUFFER_SIZE )

/**
 * @brief Thread-local logger with custom buffer size.
 *
 * Same as NOVA_LOG but allows specifying a custom buffer size.
 * Useful for tuning memory usage vs truncation rate.
 *
 * Buffer size guidelines:
 * - 256-512 bytes: high-frequency hot paths, simple messages
 * - 1024 bytes: default, covers most messages
 * - 2048-4096 bytes: verbose logging, diagnostic output
 *
 * Usage:
 *   NOVA_LOG_BUF(DebugTag, 512) << "Short message";
 *   NOVA_LOG_BUF(VerboseTag, 4096) << "Long diagnostic output...";
 *
 * @param TagType the logging tag type
 * @param BufferSize buffer size in bytes (16-65536)
 *
 * @see NOVA_LOG for default buffer size
 */
#if NOVA_HAS_TLS
#define NOVA_LOG_BUF( TagType, BufferSize ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	NOVA_IF_CONSTEXPR ( ::kmac::nova::logger_traits< TagType >::enabled ) \
		if ( ::kmac::nova::Logger< TagType >::getSink() != nullptr ) \
			::kmac::nova::TlsTruncBuilderWrapper< TagType, BufferSize >( FILE_NAME, __func__, __LINE__ ).builder()
#else
// NOVA_NO_TLS: fall through to stack-based builder transparently
#define NOVA_LOG_BUF( TagType, BufferSize ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	NOVA_LOG_BUF_STACK( TagType, BufferSize )
#endif

/**
 * @brief Stack-based logger with default buffer size (1024 bytes).
 *
 * Stack-based logging for cases where thread-local storage cannot be used.
 * Allocates builder on stack per log statement.  Required for signal handlers
 * and nested logging contexts.
 *
 * When to use:
 * - signal handlers: MUST use stack-based (thread-local can be interrupted mid-log)
 * - nested functions: functions called within log expressions
 * - libraries: when thread-local storage is undesirable
 *
 * Memory:
 * - 1KB on stack per active log statement (temporary)
 * - freed immediately when statement completes
 * - risk: large buffers in signal handlers (8KB stack limit on Linux)
 *
 * Usage:
 *   NOVA_LOG_STACK(CrashTag) << "Signal handler message";
 *
 * Signal handler example:
 *   void signalHandler(int sig) {
 *       NOVA_LOG_STACK(CrashTag) << "Signal " << sig;
 *       _Exit(128 + sig);
 *   }
 *
 * Nested logging example:
 *   void helper() {
 *       NOVA_LOG_STACK(DebugTag) << "helper called";
 *   }
 *   void main() {
 *       NOVA_LOG(InfoTag) << "Result: " << helper();  // Safe!
 *   }
 *
 * @param TagType the logging tag type
 *
 * @see NOVA_LOG_BUF_STACK for custom buffer sizes
 * @see StackTruncatingBuilder for implementation details
 */
#define NOVA_LOG_STACK( TagType ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	NOVA_LOG_BUF_STACK( TagType, ::kmac::nova::NOVA_DEFAULT_BUFFER_SIZE )

/**
 * @brief Stack-based logger with custom buffer size.
 *
 * Same as NOVA_LOG_STACK but allows specifying a custom buffer size.
 *
 * Warning: Large buffers risk stack overflow in signal handlers.
 * Signal handlers typically have only 8KB stack on Linux.
 *
 * Usage:
 *   NOVA_LOG_BUF_STACK(CrashTag, 512) << "Signal handler message";
 *
 * @param TagType the logging tag type
 * @param BufferSize buffer size in bytes (16-65536, but keep <2KB for signal handlers)
 */
#define NOVA_LOG_BUF_STACK( TagType, BufferSize ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	NOVA_IF_CONSTEXPR ( ::kmac::nova::logger_traits< TagType >::enabled ) \
		if ( ::kmac::nova::Logger< TagType >::getSink() != nullptr ) \
			::kmac::nova::StackTruncatingBuilder< TagType, BufferSize >( FILE_NAME, __func__, __LINE__ )

//
// Convenience Aliases for Common Buffer Sizes
//

/**
 * NOVA_LOG_SMALL - Uses 512 byte buffer
 */
#define NOVA_LOG_SMALL( TagType ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	NOVA_LOG_BUF( TagType, 512 )

/**
 * NOVA_LOG_MEDIUM - Uses 4KB buffer
 */
#define NOVA_LOG_MEDIUM( TagType ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	NOVA_LOG_BUF( TagType, 4096 )

/**
 * NOVA_LOG_LARGE - Uses 16KB buffer
 */
#define NOVA_LOG_LARGE( TagType ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	NOVA_LOG_BUF( TagType, 16384 )

/**
 * NOVA_LOG_HUGE - Uses 64KB buffer
 */
#define NOVA_LOG_HUGE( TagType ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	NOVA_LOG_BUF( TagType, 65536 )

#endif // KMAC_NOVA_MACROS_H
