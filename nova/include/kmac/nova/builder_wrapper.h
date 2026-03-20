#pragma once
#ifndef KMAC_NOVA_BUILDER_WRAPPER_H
#define KMAC_NOVA_BUILDER_WRAPPER_H

#include "immovable.h"
#include "sink.h"
#include "truncating_record_builder.h"
#include "platform/config.h"

#include <cstddef>

namespace kmac::nova
{

// ============================================================================
// TLS-Based Wrappers
// ============================================================================

#if NOVA_HAS_TLS

/**
 * @brief Thread-local storage for TruncatingRecordBuilder instances.
 *
 * Provides one builder instance per thread per buffer size.  The builder is
 * allocated when first accessed on a thread and persists until thread exit.
 * This enables high-performance logging without per-call stack allocation overhead.
 *
 * Memory characteristics:
 * - one instance per thread per buffer size
 * - example: 128 threads x 1KB buffer = 128KB total
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
 *   NOVA_LOG(DomainTag) << "message";
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
	 * @return reference to thread-local TruncatingRecordBuilder
	 */
	inline TruncatingRecordBuilder< BufferSize >& builder() noexcept;
};

#endif // NOVA_HAS_TLS

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
 * - bare-metal: TLS unavailable; NOVA_LOG falls back to this automatically
 *
 * Memory:
 * - BufferSize bytes on stack per log statement (temporary)
 * - immediately freed when log statement completes
 * - risk: large buffers in signal handlers may overflow stack (default 8KB on Linux)
 *
 * Lifecycle:
 * 1. constructor: creates builder on stack, calls setContext<Tag>()
 * 2. user streams data: operator<< forwards to builder
 * 3. destructor: calls builder.commit() to emit the record
 *
 * Usage (via macro):
 *   NOVA_LOG_STACK(InfoTag) << "message";
 *
 * Signal handler example:
 *   void signalHandler(int sig) {
 *       NOVA_LOG_STACK(CrashTag) << "Signal " << sig;
 *       _Exit(128 + sig);
 *   }
 *
 * @tparam Tag the logging tag type
 * @tparam BufferSize size of the builder buffer in bytes
 */
template< typename Tag, std::size_t BufferSize >
class StackTruncatingBuilder : private Immovable
{
private:
	TruncatingRecordBuilder< BufferSize > _builder;

public:
	/**
	 * @brief Construct wrapper and initialize builder.
	 *
	 * @param file source file (__FILE__)
	 * @param function function name (__func__)
	 * @param line line number (__LINE__)
	 */
	StackTruncatingBuilder( const char* file, const char* function, std::uint32_t line ) noexcept;

	/**
	 * @brief Destroy wrapper and emit log record by calling commit on the builder.
	 */
	~StackTruncatingBuilder() noexcept;

	/**
	 * @brief Stream insertion operator, which forwards to the builder.
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

// ============================================================================
// TLS-Based Wrappers - implementation
// ============================================================================

#if NOVA_HAS_TLS

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

#endif // NOVA_HAS_TLS

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

} // namespace kmac::nova

#endif // KMAC_NOVA_BUILDER_WRAPPER_H
