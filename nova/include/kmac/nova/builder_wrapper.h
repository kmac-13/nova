#pragma once
#ifndef KMAC_NOVA_BUILDER_WRAPPER_H
#define KMAC_NOVA_BUILDER_WRAPPER_H

#include "continuation_record_builder.h"
#include "sink.h"
#include "truncating_record_builder.h"

#include <cstddef>

namespace kmac::nova
{

// ============================================================================
// TLS-Based Wrappers
// ============================================================================

/**
 * @brief Thread-local storage for TruncatingRecordBuilder instances.
 *
 * Provides one builder instance per thread per buffer size.  The builder is
 * allocated when first accessed on a thread and persists until thread exit.
 * This enables high-performance logging without per-call stack allocation overhead.
 *
 * Memory characteristics:
 * - one instance per thread per buffer size
 * - example: 128 threads × 1KB buffer = 128KB total
 * - tag-agnostic: single buffer shared across all tags per thread
 * - persistent until thread exit
 *
 * Performance benefits:
 * - faster than stack-based allocation overhead per log call
 * - cache-friendly (hot buffer stays in L1/L2)
 * - zero locking overhead (thread-local, no contention)
 *
 * Usage:
 * - not used directly - access via TlsTruncBuilderWrapper
 * - automatically initialized on first log statement per thread
 * - see TlsTruncBuilderWrapper for RAII wrapper
 *
 * @tparam BufferSize size of the builder buffer in bytes
 */
template< std::size_t BufferSize >
struct TlsTruncBuilderStorage
{
	thread_local static TruncatingRecordBuilder< BufferSize > builder;
};

/**
 * @brief RAII wrapper for thread-local TruncatingRecordBuilder.
 *
 * Provides RAII semantics for thread-local logging.  The wrapper is allocated
 * on the stack per log statement and manages the lifecycle of a log operation
 * on the thread-local builder.
 *
 * Lifecycle:
 * 1. constructor: calls builder.setContext<Tag>() to initialize for this log
 * 2. user streams data: builder() returns reference for operator<<
 * 3. destructor: calls builder.commit() to emit the log record
 *
 * Thread safety:
 * - thread-local builder (one per thread) - no contention
 * - nested logging detection via _busy flag in builder
 * - debug builds: assert on nested logging
 * - release builds: silently drop nested log to prevent corruption
 *
 * Usage (via macro):
 *   NOVA_LOG_TRUNC(DomainTag) << "message";
 *   // expands to: TlsTruncBuilderWrapper<InfoTag, 1024>(...).builder() << "message"
 *   // wrapper destroyed at end of statement -> commit() called
 *
 * Memory:
 * - wrapper itself: ~16 bytes on stack (temporary)
 * - builder: BufferSize bytes thread-local (persistent)
 *
 * @tparam Tag the logging tag type
 * @tparam BufferSize size of the builder buffer in bytes
 */
template< typename Tag, std::size_t BufferSize >
struct TlsTruncBuilderWrapper
{
	/**
	 * @brief Initialize the thread-local builder for this log statement.
	 *
	 * Calls setContext<Tag>() on the thread-local builder with source location.
	 *
	 * @param file source file (__FILE__)
	 * @param function function name (__func__)
	 * @param line line number (__LINE__)
	 */
	TlsTruncBuilderWrapper( const char* file, const char* function, std::uint32_t line );

	/**
	 * @brief Finalize and emit the log record.
	 *
	 * Calls commit() on the thread-local builder to emit the record.
	 * The commit() is idempotent (safe to call multiple times).
	 */
	~TlsTruncBuilderWrapper();

	/**
	 * @brief Get reference to the thread-local builder.
	 *
	 * Returns a reference to the thread-local builder for streaming operations.
	 * This enables: wrapper.builder() << "data" << value;
	 *
	 * @return reference to thread-local TruncatingRecordBuilder
	 */
	inline TruncatingRecordBuilder< BufferSize >& builder() noexcept;
};

/**
 * @brief Thread-local storage for ContinuationRecordBuilder instances.
 *
 * Provides one builder instance per thread per buffer size.  The builder is
 * allocated when first accessed on a thread and persists until thread exit.
 * This enables high-performance logging without per-call stack allocation overhead.
 *
 * Memory characteristics:
 * - one instance per thread per buffer size
 * - example: 128 threads × 1KB buffer = 128KB total
 * - tag-agnostic: single buffer shared across all tags per thread
 * - persistent until thread exit
 *
 * Performance benefits:
 * - faster than stack-based allocation overhead per log call
 * - cache-friendly (hot buffer stays in L1/L2)
 * - zero locking overhead (thread-local, no contention)
 *
 * Usage:
 * - not used directly - access via TlsContBuilderWrapper
 * - automatically initialized on first log statement per thread
 * - see TlsContBuilderWrapper for RAII wrapper
 *
 * @tparam BufferSize size of the builder buffer in bytes
 */
template< std::size_t BufferSize >
struct TlsContBuilderStorage
{
	thread_local static ContinuationRecordBuilder< BufferSize > builder;
};

/**
 * @brief RAII wrapper for thread-local ContinuationRecordBuilder.
 *
 * Provides RAII semantics for thread-local logging with continuation support.
 * The wrapper is allocated on the stack per log statement and manages the
 * lifecycle of a log operation on the thread-local builder.
 *
 * Lifecycle:
 * 1. constructor: calls builder.setContext<Tag>() to initialize for this log
 * 2. user streams data: builder() returns reference for operator<<
 *    - during streaming: builder may emit continuation records automatically
 * 3. destructor: calls builder.commit() to emit final record with remaining data
 *
 * Thread safety:
 * - thread-local builder (one per thread) - no contention
 * - nested logging detection via _busy flag in builder
 * - debug builds: assert on nested logging
 * - release builds: silently drop nested log to prevent corruption
 *
 * Usage (via macro):
 *   NOVA_LOG_CONT(DomainTag) << "very long message...";
 *   // expands to: TlsContBuilderWrapper<InfoTag, 1024>(...).builder() << "message"
 *   // may emit continuations during streaming if buffer fills
 *   // wrapper destroyed at end of statement -> commit() emits final record
 *
 * Memory:
 * - wrapper itself: ~16 bytes on stack (temporary)
 * - builder: BufferSize bytes thread-local (persistent)
 *
 * @tparam Tag the logging tag type
 * @tparam BufferSize size of the builder buffer in bytes
 */
template< typename Tag, std::size_t BufferSize >
struct TlsContBuilderWrapper
{
	/**
	 * @brief Initialize the thread-local builder for this log statement.
	 *
	 * Calls setContext<Tag>() on the thread-local builder with source location.
	 *
	 * @param file source file (__FILE__)
	 * @param function function name (__func__)
	 * @param line line number (__LINE__)
	 */
	TlsContBuilderWrapper( const char* file, const char* function, std::uint32_t line );

	/**
	 * @brief Finalize and emit the final log record.
	 *
	 * Calls commit() on the thread-local builder to emit the final record
	 * with any remaining data in the buffer.  The commit() is idempotent
	 * (safe to call multiple times).
	 */
	~TlsContBuilderWrapper();

	/**
	 * @brief Get reference to the thread-local builder.
	 *
	 * Returns a reference to the thread-local builder for streaming operations.
	 * This enables: wrapper.builder() << "data" << value;
	 *
	 * NOTE: During streaming, the builder may automatically emit continuation
	 * records if the buffer fills.
	 *
	 * @return reference to thread-local ContinuationRecordBuilder
	 */
	inline ContinuationRecordBuilder< BufferSize >& builder() noexcept;
};

// ============================================================================
// Stack-Based Wrappers
// ============================================================================

/**
 * @brief Stack-based wrapper for TruncatingRecordBuilder.
 *
 * Provides RAII semantics for stack-based logging.  Unlike thread-local logging,
 * this allocates the builder itself on the stack per log statement.  This is
 * required for signal handlers and nested logging contexts.
 *
 * When to use:
 * - signal handlers: MUST use stack-based (thread-local can be interrupted)
 * - nested functions: functions called within log expressions need stack-based
 * - libraries: when thread-local storage is undesirable
 * - dynamic threading: thread pools with frequent thread creation/destruction
 *
 * Performance:
 * - slower than thread-local (stack allocation overhead)
 * - no contention (independent stack buffer per call)
 *
 * Memory:
 * - BufferSize bytes on stack per log statement (temporary)
 * - example: 1KB buffer = 1KB stack per active log
 * - immediately freed when log statement completes
 * - risk: large buffers in signal handlers may overflow stack (default 8KB on Linux)
 *
 * Lifecycle:
 * 1. constructor: creates builder on stack, calls setContext<Tag>()
 * 2. user streams data: operator<< forwards to builder
 * 3. destructor: calls builder.commit() to emit the record
 *
 * Usage (via macro):
 *   NOVA_LOG_TRUNC_STACK(InfoTag) << "message";
 *   // allocates StackTruncatingBuilder<InfoTag, 1024> on stack
 *   // wrapper destroyed at end of statement -> commit() called
 *
 * Signal handler example:
 *   void signalHandler(int sig) {
 *       NOVA_LOG_TRUNC_STACK(CrashTag) << "Signal " << sig;
 *       _Exit(128 + sig);
 *   }
 *
 * Nested logging example:
 *   void helperFunction() {
 *       NOVA_LOG_TRUNC_STACK(DebugTag) << "helper called";
 *   }
 *
 *   void mainFunction() {
 *       NOVA_LOG_TRUNC(InfoTag) << "Result: " << helperFunction();
 *       // helperFunction() uses stack, mainFunction() uses thread-local - no conflict
 *   }
 *
 * @tparam Tag the logging tag type
 * @tparam BufferSize size of the builder buffer in bytes
 */
template< typename Tag, std::size_t BufferSize >
class StackTruncatingBuilder
{
private:
	TruncatingRecordBuilder< BufferSize > _builder;

public:
	/**
	 * @brief Construct wrapper and initialize builder.
	 *
	 * Allocates builder on stack and calls setContext<Tag>() with source location.
	 *
	 * @param file Source file (__FILE__)
	 * @param function Function name (__func__)
	 * @param line Line number (__LINE__)
	 */
	StackTruncatingBuilder( const char* file, const char* function, std::uint32_t line ) noexcept;

	/**
	 * @brief Destroy wrapper and emit log record.
	 *
	 * Calls commit() on the builder to emit the record.
	 */
	~StackTruncatingBuilder() noexcept;

	NO_COPY_NO_MOVE( StackTruncatingBuilder );

	/**
	 * @brief Stream insertion operator.
	 *
	 * Forwards to underlying builder for data appending.
	 *
	 * @tparam T type to append
	 * @param value value to append
	 * @return reference to this wrapper for chaining
	 */
	template< typename T >
	StackTruncatingBuilder& operator<<( const T& value ) noexcept;

	/**
	 * @brief Check if truncation occurred.
	 *
	 * @return true if buffer filled and data was truncated
	 */
	bool wasTruncated() const noexcept;
};

/**
 * @brief Stack-based wrapper for ContinuationRecordBuilder.
 *
 * Provides RAII semantics for stack-based logging with continuation support.
 * Unlike thread-local logging, this allocates the builder itself on the stack
 * per log statement.  This is required for signal handlers and nested logging contexts.
 *
 * When to use:
 * - signal handlers: MUST use stack-based (thread-local can be interrupted)
 * - nested functions: functions called within log expressions need stack-based
 * - libraries: when thread-local storage is undesirable
 * - dynamic threading: thread pools with frequent thread creation/destruction
 *
 * Performance:
 * - slower than thread-local (stack allocation overhead)
 * - still fast for continuation logging
 * - no contention (independent stack buffer per call)
 *
 * Memory:
 * - BufferSize bytes on stack per log statement (temporary)
 * - example: 1KB buffer = 1KB stack per active log
 * - immediately freed when log statement completes
 * - risk: large buffers in signal handlers may overflow stack (default 8KB on Linux)
 *
 * Lifecycle:
 * 1. constructor: creates builder on stack, calls setContext<Tag>()
 * 2. user streams data: operator<< forwards to builder
 *    - during streaming: builder may emit continuation records automatically
 * 3. destructor: calls builder.commit() to emit final record
 *
 * Usage (via macro):
 *   NOVA_LOG_CONT_STACK(InfoTag) << "very long message...";
 *   // allocates StackContinuationBuilder<InfoTag, 1024> on stack
 *   // may emit continuations during streaming
 *   // wrapper destroyed at end of statement -> commit() emits final record
 *
 * Signal handler example:
 *   void signalHandler(int sig) {
 *       NOVA_LOG_CONT_STACK(CrashTag) << "Signal " << sig << " context...";
 *       _Exit(128 + sig);
 *   }
 *
 * Nested logging example:
 *   void helperFunction() {
 *       NOVA_LOG_CONT_STACK(DebugTag) << "helper called with data...";
 *   }
 *
 *   void mainFunction() {
 *       NOVA_LOG_CONT(InfoTag) << "Result: " << helperFunction();
 *       // helperFunction() uses stack, mainFunction() uses thread-local - no conflict
 *   }
 *
 * @tparam Tag the logging tag type
 * @tparam BufferSize size of the builder buffer in bytes
 */
template< typename Tag, std::size_t BufferSize >
class StackContinuationBuilder
{
private:
	ContinuationRecordBuilder< BufferSize > _builder;

public:
	/**
	 * @brief Construct wrapper and initialize builder.
	 *
	 * Allocates builder on stack and calls setContext<Tag>() with source location.
	 *
	 * @param file source file (__FILE__)
	 * @param function function name (__func__)
	 * @param line line number (__LINE__)
	 */
	StackContinuationBuilder( const char* file, const char* function, std::uint32_t line ) noexcept;

	/**
	 * @brief Destroy wrapper and emit final log record.
	 *
	 * Calls commit() on the builder to emit the final record with any
	 * remaining data.  Previous continuation records were already emitted
	 * during streaming if the buffer filled.
	 */
	~StackContinuationBuilder() noexcept;

	NO_COPY_NO_MOVE( StackContinuationBuilder );

	/**
	 * @brief Stream insertion operator.
	 *
	 * Forwards to underlying builder for data appending.  May trigger
	 * automatic continuation record emission if buffer fills during append.
	 *
	 * @tparam T type to append
	 * @param value value to append
	 * @return reference to this wrapper for chaining
	 */
	template< typename T >
	StackContinuationBuilder& operator<<( const T& value ) noexcept;

	/**
	 * @brief Get number of continuation records emitted so far.
	 *
	 * @return number of continuation records (0 if all data fit in first record)
	 */
	std::size_t continuationCount() const noexcept;
};

// ============================================================================
// TLS-Based Wrappers - implementation
// ============================================================================

// static initialization
template< std::size_t BufferSize >
thread_local TruncatingRecordBuilder< BufferSize > TlsTruncBuilderStorage< BufferSize >::builder;

template< typename Tag, std::size_t BufferSize >
TlsTruncBuilderWrapper< Tag, BufferSize >::TlsTruncBuilderWrapper( const char* file, const char* function, std::uint32_t line )
{
	builder().template setContext< Tag >( file, function, line );
}

template< typename Tag, std::size_t BufferSize >
TlsTruncBuilderWrapper< Tag, BufferSize >::~TlsTruncBuilderWrapper()
{
	auto& builder = TlsTruncBuilderStorage< BufferSize >::builder;
	builder.commit();
}

template< typename Tag, std::size_t BufferSize >
TruncatingRecordBuilder< BufferSize >& TlsTruncBuilderWrapper< Tag, BufferSize >::builder() noexcept
{
	auto& builder = TlsTruncBuilderStorage< BufferSize >::builder;
	return builder;
}

// static initialization
template< std::size_t BufferSize >
thread_local ContinuationRecordBuilder< BufferSize > TlsContBuilderStorage< BufferSize >::builder;

template< typename Tag, std::size_t BufferSize >
TlsContBuilderWrapper< Tag, BufferSize >::TlsContBuilderWrapper( const char* file, const char* function, std::uint32_t line )
{
	builder().template setContext< Tag >( file, function, line );
}

template< typename Tag, std::size_t BufferSize >
TlsContBuilderWrapper< Tag, BufferSize >::~TlsContBuilderWrapper()
{
	auto& builder = TlsContBuilderStorage< BufferSize >::builder;
	builder.commit();
}

template< typename Tag, std::size_t BufferSize >
ContinuationRecordBuilder< BufferSize >& TlsContBuilderWrapper< Tag, BufferSize >::builder() noexcept
{
	auto& builder = TlsContBuilderStorage< BufferSize >::builder;
	return builder;
}

// ============================================================================
// Stack-Based Wrappers - implementation
// ============================================================================

template< typename Tag, std::size_t BufferSize >
StackTruncatingBuilder< Tag, BufferSize >::StackTruncatingBuilder( const char* file, const char* function, std::uint32_t line ) noexcept
{
	_builder.template setContext< Tag >( file, function, line );
}

template< typename Tag, std::size_t BufferSize >
StackTruncatingBuilder< Tag, BufferSize >::~StackTruncatingBuilder() noexcept
{
	_builder.commit();
}

template< typename Tag, std::size_t BufferSize >
template< typename T >
StackTruncatingBuilder< Tag, BufferSize >& StackTruncatingBuilder< Tag, BufferSize >::operator<<( const T& value ) noexcept
{
	_builder << value;
	return *this;
}

template< typename Tag, std::size_t BufferSize >
bool StackTruncatingBuilder< Tag, BufferSize >::wasTruncated() const noexcept
{
	return _builder.wasTruncated();
}

template< typename Tag, std::size_t BufferSize >
StackContinuationBuilder< Tag, BufferSize >::StackContinuationBuilder( const char* file, const char* function, std::uint32_t line ) noexcept
{
	_builder.template setContext< Tag >( file, function, line );
}

template< typename Tag, std::size_t BufferSize >
StackContinuationBuilder< Tag, BufferSize >::~StackContinuationBuilder() noexcept
{
	_builder.commit();
}

template< typename Tag, std::size_t BufferSize >
template< typename T >
StackContinuationBuilder< Tag, BufferSize >& StackContinuationBuilder< Tag, BufferSize >::operator<<( const T& value ) noexcept
{
	_builder << value;
	return *this;
}

template< typename Tag, std::size_t BufferSize >
std::size_t StackContinuationBuilder< Tag, BufferSize >::continuationCount() const noexcept
{
	return _builder.continuationCount();
}

} // kmac::nova

#endif // KMAC_NOVA_BUILDER_WRAPPER_H
