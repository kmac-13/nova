#pragma once
#ifndef KMAC_NOVA_MACROS_H
#define KMAC_NOVA_MACROS_H

#include "builder_wrapper.h"

#include <cstddef>

namespace kmac::nova
{

/**
 * Configuration: Default buffer size for all builders.
 */
inline constexpr std::size_t NOVA_DEFAULT_BUFFER_SIZE =
#ifdef NOVA_DEFAULT_BUFFER_SIZE_OVERRIDE
	NOVA_DEFAULT_BUFFER_SIZE_OVERRIDE;
#else
	1024;
#endif

/**
 * Configuration: Default builder type.
 */
#ifndef NOVA_DEFAULT_BUILDER
#define NOVA_DEFAULT_BUILDER NOVA_BUILDER_TRUNCATING
#endif

inline constexpr std::size_t NOVA_BUILDER_TRUNCATING = 1;
inline constexpr std::size_t NOVA_BUILDER_CONTINUATION = 2;


// compile-time stripping of path from, e.g., __FILE__
constexpr const char* fileName( const char* path );

} // namespace kmac::nova


#define FILE_NAME ::kmac::nova::fileName( __FILE__ )


//
// Core Logging Macros - Truncating Variant
//

// The following macros cannot be replaced with constexpr template functions:
// - __FILE__, __func__, __LINE__ require macro expansion at the call site (C++20
//   std::source_location would allow functions, but Nova targets C++17)
// - if constexpr eliminates disabled tag branches entirely; a function call cannot
//   suppress evaluation of its own arguments
// - alias macros expand to the above and inherit the same constraints
// NOLINT comments suppress cppcoreguidelines-macro-usage on each definition.

/**
 * @brief Thread-local truncating logger with default buffer size (1024 bytes).
 *
 * High-performance logging using thread-local storage.  Provides better
 * performance than stack-based logging with minimal memory overhead.
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
 *   NOVA_LOG_TRUNC(InfoTag) << "User " << username << " logged in";
 *
 * When TagType is compile-time disabled (logger_traits<TagType>::enabled == false),
 * the entire statement including all streaming operations is completely compiled out.
 *
 * Important warnings:
 * - do NOT use in signal handlers (use NOVA_LOG_TRUNC_STACK instead)
 * - do NOT log inside expressions being logged (causes nested logging corruption)
 * - DO use NOVA_LOG_TRUNC_STACK for functions called within log expressions
 *
 * @param TagType the logging tag type (must have logger_traits specialization)
 *
 * @see NOVA_LOG_TRUNC_BUF for custom buffer sizes
 * @see NOVA_LOG_TRUNC_STACK for stack-based logging (signal handlers, nested contexts)
 * @see TlsTruncBuilderWrapper for implementation details
 */
#define NOVA_LOG_TRUNC( TagType ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	NOVA_LOG_TRUNC_BUF( TagType, ::kmac::nova::NOVA_DEFAULT_BUFFER_SIZE )

/**
 * @brief Thread-local truncating logger with custom buffer size.
 *
 * Same as NOVA_LOG_TRUNC but allows specifying a custom buffer size.
 * Useful for tuning memory usage vs truncation rate.
 *
 * Buffer size guidelines:
 * - 256-512 bytes: high-frequency hot paths, simple messages
 * - 1024 bytes: default, likely covers most messages
 * - 2048-4096 bytes: verbose logging, diagnostic output
 *
 * Usage:
 *   NOVA_LOG_TRUNC_BUF(DebugTag, 512) << "Short message";
 *   NOVA_LOG_TRUNC_BUF(VerboseTag, 4096) << "Long diagnostic output...";
 *
 * @param TagType the logging tag type
 * @param BufferSize buffer size in bytes (16-65536)
 *
 * @see NOVA_LOG_TRUNC for default buffer size
 */
#if NOVA_HAS_TLS
#define NOVA_LOG_TRUNC_BUF( TagType, BufferSize ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	if constexpr ( ::kmac::nova::logger_traits< TagType >::enabled ) \
		if ( ::kmac::nova::Logger< TagType >::getSink() != nullptr ) \
			::kmac::nova::TlsTruncBuilderWrapper< TagType, BufferSize >( FILE_NAME, __func__, __LINE__ ).builder()
#else
// NOVA_NO_TLS: fall through to stack-based builder transparently
#define NOVA_LOG_TRUNC_BUF( TagType, BufferSize ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	NOVA_LOG_TRUNC_BUF_STACK( TagType, BufferSize )
#endif


/**
 * @brief Stack-based truncating logger with default buffer size (1024 bytes).
 *
 * Stack-based logging for special cases where thread-local storage cannot be used.
 * Allocates builder on stack per log statement.  Slower than thread-local,
 * but required for signal handlers and nested logging contexts or when thread-local
 * instances are not desireable.
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
 * Performance:
 * - still very fast, just slightly slower than thread-local
 *
 * Usage:
 *   NOVA_LOG_TRUNC_STACK(CrashTag) << "Signal handler message";
 *
 * Signal handler example:
 *   void signalHandler(int sig) {
 *       NOVA_LOG_TRUNC_STACK(CrashTag) << "Signal " << sig;
 *       _Exit(128 + sig);
 *   }
 *
 * Nested logging example:
 *   void helper() {
 *       NOVA_LOG_TRUNC_STACK(DebugTag) << "helper called";
 *   }
 *   void main() {
 *       NOVA_LOG_TRUNC(InfoTag) << "Result: " << helper();  // Safe!
 *   }
 *
 * @param TagType the logging tag type
 *
 * @see NOVA_LOG_TRUNC_BUF_STACK for custom buffer sizes
 * @see StackTruncatingBuilder for implementation details
 */
#define NOVA_LOG_TRUNC_STACK( TagType ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	NOVA_LOG_TRUNC_BUF_STACK( TagType, ::kmac::nova::NOVA_DEFAULT_BUFFER_SIZE )

/**
 * @brief Stack-based truncating logger with custom buffer size.
 *
 * Same as NOVA_LOG_TRUNC_STACK but allows specifying a custom buffer size.
 *
 * Warning: Large buffers risk stack overflow in signal handlers.
 * Signal handlers typically have only 8KB stack on Linux.
 *
 * Usage:
 *   NOVA_LOG_TRUNC_BUF_STACK(CrashTag, 512) << "Signal handler message";
 *
 * @param TagType the logging tag type
 * @param BufferSize buffer size in bytes (16-65536, but keep <2KB for signal handlers)
 */
#define NOVA_LOG_TRUNC_BUF_STACK( TagType, BufferSize ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	if constexpr ( ::kmac::nova::logger_traits< TagType >::enabled ) \
		if ( ::kmac::nova::Logger< TagType >::getSink() != nullptr ) \
			::kmac::nova::StackTruncatingBuilder< TagType, BufferSize >( FILE_NAME, __func__, __LINE__ )



//
// Core Logging Macros - Continuation Variant
//

/**
 * @brief Thread-local continuation logger with default buffer size (1024 bytes).
 *
 * High-performance logging with complete data preservation.  Uses thread-local storage
 * and automatically emits continuation records when buffer fills.  Provides better
 * performance than stack-based logging.
 *
 * Features:
 * - zero heap allocation (completely deterministic)
 * - thread-local storage (one buffer per thread)
 * - tag-agnostic (buffer shared across all tags on same thread)
 * - complete data preservation (no truncation, ever)
 * - automatic continuations when buffer fills
 * - nested logging detection (assert in debug, silent drop in release)
 *
 * Memory cost:
 * - 1KB per thread (default buffer size)
 * - example: 128 threads = 128KB total
 *
 * Usage:
 *   NOVA_LOG_CONT(DomainTag)
 *     << "User: " << username
 *     << " performed action: " << action
 *     << " on file: " << longFilePath;
 *   // automatically emits continuations if message exceeds 1KB
 *
 * Output format:
 *   [DOMAIN] main.cpp:42 (process): Long message part 1
 *   [DOMAIN] main.cpp:42 (process): [cont] ...part 2
 *   [DOMAIN] main.cpp:42 (process): [cont] ...part 3
 *
 * When TagType is compile-time disabled (logger_traits<TagType>::enabled == false),
 * the entire statement including all streaming operations is completely compiled out.
 *
 * Important warnings:
 * - do NOT use in signal handlers (use NOVA_LOG_CONT_STACK instead)
 * - do NOT log inside expressions being logged (causes nested logging corruption)
 * - DO use NOVA_LOG_CONT_STACK for functions called within log expressions
 * - continuations may interleave with logs from other threads at sink
 *
 * @param TagType the logging tag type (must have logger_traits specialization)
 *
 * @see NOVA_LOG_CONT_BUF for custom buffer sizes
 * @see NOVA_LOG_CONT_STACK for stack-based logging (signal handlers, nested contexts)
 * @see TlsContBuilderWrapper for implementation details
 */
#define NOVA_LOG_CONT( TagType ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	NOVA_LOG_CONT_BUF( TagType, ::kmac::nova::NOVA_DEFAULT_BUFFER_SIZE )

/**
 * @brief Thread-local continuation logger with custom buffer size.
 *
 * Same as NOVA_LOG_CONT but allows specifying a custom buffer size.
 * Larger buffers reduce continuation frequency at cost of more memory.
 *
 * Buffer size guidelines:
 * - 512 bytes: frequent short continuations, tight memory constraints
 * - 1024 bytes: default, good balance
 * - 2048-4096 bytes: fewer continuations for verbose logging
 *
 * Usage:
 *   NOVA_LOG_CONT_BUF(VerboseTag, 512) << "Data with frequent continuations...";
 *   NOVA_LOG_CONT_BUF(DiagTag, 4096) << "Long diagnostic output...";
 *
 * @param TagType the logging tag type
 * @param BufferSize buffer size in bytes (16-65536)
 *
 * @see NOVA_LOG_CONT for default buffer size
 */
#if NOVA_HAS_TLS
#define NOVA_LOG_CONT_BUF( TagType, BufferSize ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	if constexpr ( ::kmac::nova::logger_traits< TagType >::enabled ) \
		if ( ::kmac::nova::Logger< TagType >::getSink() != nullptr ) \
			::kmac::nova::TlsContBuilderWrapper< TagType, BufferSize >( FILE_NAME, __func__, __LINE__ ).builder()
#else
// NOVA_NO_TLS: fall through to stack-based builder transparently
#define NOVA_LOG_CONT_BUF( TagType, BufferSize ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	NOVA_LOG_CONT_BUF_STACK( TagType, BufferSize )
#endif

/**
 * @brief Stack-based continuation logger with default buffer size (1024 bytes).
 *
 * Stack-based logging with continuation support for special cases where thread-local
 * storage cannot be used.  Allocates builder on stack per log statement.  Slower
 * than thread-local but required for signal handlers and nested logging contexts.
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
 * Performance:
 * - slightly slower than thread-local
 * - still efficient with automatic continuation support
 *
 * Usage:
 *   NOVA_LOG_CONT_STACK(CrashTag) << "Signal handler with long message...";
 *
 * Signal handler example:
 *   void signalHandler(int sig) {
 *       NOVA_LOG_CONT_STACK(CrashTag)
 *         << "Signal " << sig
 *         << " context: " << getLongContext();
 *       _Exit(128 + sig);
 *   }
 *
 * Nested logging example:
 *   void helper() {
 *       NOVA_LOG_CONT_STACK(DebugTag) << "helper called with data...";
 *   }
 *   void main() {
 *       NOVA_LOG_CONT(InfoTag) << "Result: " << helper();  // safe!
 *   }
 *
 * @param TagType the logging tag type
 *
 * @see NOVA_LOG_CONT_BUF_STACK for custom buffer sizes
 * @see StackContinuationBuilder for implementation details
 */
#define NOVA_LOG_CONT_STACK( TagType ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	NOVA_LOG_CONT_BUF_STACK( TagType, ::kmac::nova::NOVA_DEFAULT_BUFFER_SIZE )

/**
 * @brief Stack-based continuation logger with custom buffer size.
 *
 * Same as NOVA_LOG_CONT_STACK but allows specifying a custom buffer size.
 *
 * Warning: Large buffers risk stack overflow in signal handlers.
 * Signal handlers typically have only 8KB stack on Linux.
 *
 * Usage:
 *   NOVA_LOG_CONT_BUF_STACK(CrashTag, 512) << "Signal handler message...";
 *
 * @param TagType the logging tag type
 * @param BufferSize buffer size in bytes (16-65536, but keep <2KB for signal handlers)
 */
#define NOVA_LOG_CONT_BUF_STACK( TagType, BufferSize ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	if constexpr ( ::kmac::nova::logger_traits< TagType >::enabled ) \
		if ( ::kmac::nova::Logger< TagType >::getSink() != nullptr ) \
			::kmac::nova::StackContinuationBuilder< TagType, BufferSize >( FILE_NAME, __func__, __LINE__ )

//
// Default Logging Macro
//

#if NOVA_DEFAULT_BUILDER == NOVA_BUILDER_TRUNCATING
#define NOVA_LOG( TagType ) /* NOLINT(cppcoreguidelines-macro-usage) */ NOVA_LOG_TRUNC( TagType )
#define NOVA_LOG_BUF( TagType, BufferSize ) /* NOLINT(cppcoreguidelines-macro-usage) */ NOVA_LOG_TRUNC_BUF( TagType, BufferSize )
#define NOVA_LOG_STACK( TagType ) /* NOLINT(cppcoreguidelines-macro-usage) */ NOVA_LOG_TRUNC_STACK( TagType )
#define NOVA_LOG_BUF_STACK( TagType, BufferSize ) /* NOLINT(cppcoreguidelines-macro-usage) */ NOVA_LOG_TRUNC_BUF_STACK( TagType, BufferSize )
#elif NOVA_DEFAULT_BUILDER == NOVA_BUILDER_CONTINUATION
#define NOVA_LOG( TagType ) /* NOLINT(cppcoreguidelines-macro-usage) */ NOVA_LOG_CONT( TagType )
#define NOVA_LOG_BUF( TagType, BufferSize ) /* NOLINT(cppcoreguidelines-macro-usage) */ NOVA_LOG_CONT_BUF( TagType, BufferSize )
#define NOVA_LOG_STACK( TagType ) /* NOLINT(cppcoreguidelines-macro-usage) */ NOVA_LOG_CONT_STACK( TagType )
#define NOVA_LOG_BUF_STACK( TagType, BufferSize ) /* NOLINT(cppcoreguidelines-macro-usage) */ NOVA_LOG_CONT_BUF_STACK( TagType, BufferSize )
#else
#error "Invalid NOVA_DEFAULT_BUILDER - must be NOVA_BUILDER_TRUNCATING or NOVA_BUILDER_CONTINUATION"
#endif

//
// Convenience Aliases for Common Buffer Sizes
//

/**
 * NOVA_LOG_SMALL - Uses 512 byte buffer
 */
#define NOVA_LOG_SMALL( TagType ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	NOVA_LOG_BUF( TagType, 512 )

/**
 * NOVA_LOG_MEDIUM - Uses 4KB buffer (same as default)
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


namespace kmac::nova
{
constexpr const char* fileName( const char* path )
{
	const char* file = path;
	for ( const char* ptr = path; *ptr != '\0'; ++ptr )
	{
		if ( *ptr == '/' || *ptr == '\\' )
		{
			file = ptr + 1;
		}
	}
	return file;
}
} // kmac::nova

#endif // KMAC_NOVA_MACROS_H
