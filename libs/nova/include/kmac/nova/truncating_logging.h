#pragma once
#ifndef KMAC_NOVA_TRUNCATING_LOGGING_H
#define KMAC_NOVA_TRUNCATING_LOGGING_H

/**
 * @file truncating_logging.h
 * @brief Truncating builder, TLS/stack wrappers for Nova core logging.
 *
 * This header is included transitively by nova.h and is not typically
 * included directly.  It provides:
 *
 * - TruncatingRecordBuilder<BufferSize>  : the builder itself
 * - TlsTruncBuilderStorage<Size>         : TLS storage (when NOVA_HAS_TLS)
 * - TlsTruncBuilderWrapper<Tag, Size>    : TLS RAII wrapper (when NOVA_HAS_TLS)
 * - StackTruncatingBuilder<Tag, Size>    : stack-based RAII wrapper
 *
 * The NOVA_LOG* macros that drive these types are defined in nova.h.
 *
 * For continuation logging (complete data preservation without truncation),
 * include <kmac/nova/extras/continuation_logging.h>.
 */

#include "immovable.h"
#include "logger.h"
#include "logger_traits.h"
#include "platform/array.h"
#include "platform/config.h"
#include "platform/float_to_chars.h"
#include "platform/int_to_chars.h"
#include "platform/string_view.h"

#include <cstddef>
#include <cstring>

namespace kmac::nova
{

// ============================================================================
// TruncatingRecordBuilder
// ============================================================================

/**
 * @brief Fixed-buffer record builder that truncates when full.
 *
 * TruncatingRecordBuilder builds log messages in a fixed-size buffer, truncating
 * overflow with a "..." marker.  This provides deterministic behavior with zero
 * heap allocation.
 *
 * Key characteristics:
 * - zero heap allocation (completely deterministic)
 * - fixed buffer size (predictable memory usage)
 * - fast performance (no allocation overhead)
 * - single record per log statement (no continuation)
 * - simple truncation behavior (appends "..." when full)
 * - tag-agnostic design (stores tag info at runtime via setContext)
 *
 * Thread-local usage (default via macros):
 * - NOVA_LOG uses TlsTruncBuilderWrapper for thread-local storage
 * - provides markedly improved performance over stack-based
 * - nested logging detection: NOVA_ASSERT on re-entry, silent drop in release
 *
 * Stack-based usage (opt-in via macros or direct usage):
 * - NOVA_LOG_STACK uses StackTruncatingBuilder wrapper
 * - required for signal handlers (avoids stack overflow)
 * - required for functions called within log expressions
 *
 * Comparison with other builders:
 * - vs ContinuationRecordBuilder (nova_extras/continuation_logging.h):
 *   single record vs multiple records; truncates vs preserves all data
 * - vs StreamingRecordBuilder (Nova Extras): deterministic vs heap allocation
 * - best for: real-time systems, hot paths, most production logging
 *
 * Usage via macros (recommended):
 *
 *   NOVA_LOG(InfoTag) << "User " << username << " logged in";
 *   NOVA_LOG_BUF(DebugTag, 256) << "Short message";
 *   NOVA_LOG_STACK(CrashTag) << "Signal handler logging";
 *
 * Direct usage (advanced):
 *
 *   TruncatingRecordBuilder<1024> builder;
 *   builder.setContext<InfoTag>(__FILE__, __func__, __LINE__);
 *   builder << "Message " << value;
 *   builder.commit();
 *
 * Buffer size guidelines:
 * - minimum: 16 bytes (compile-time enforced)
 * - typical: 512-1024 bytes (covers 90-95% of messages)
 * - default: 1024 bytes (good balance)
 * - maximum: 65536 bytes (64KB stack safety limit)
 *
 * @tparam BufferSize buffer size in bytes (default 1024)
 */
template< std::size_t BufferSize = 1024 >
class TruncatingRecordBuilder : private Immovable
{
private:
	static constexpr std::size_t TRUNCATION_MARKER_SIZE = 3; // "..."
	static constexpr std::size_t USABLE_SIZE = BufferSize - TRUNCATION_MARKER_SIZE - 1; // -1 for null

	static_assert( BufferSize >= 16, "Buffer size must be at least 16 bytes" );
	static_assert( BufferSize <= 65536, "Buffer size must not exceed 64KB (stack safety)" );

	platform::Array< char, BufferSize > _buffer = { };  ///< stack-allocated message buffer
	std::size_t _offset = 0;     ///< current write position
	bool _busy = false;          ///< non-atomic, thread-local reentrancy guard, not used for synchronization
	bool _truncated = false;     ///< true if truncation occurred
	bool _committed = false;     ///< true if already committed

	const char* _file = nullptr;       ///< source file (e.g __FILE__)
	const char* _function = nullptr;   ///< function name (e.g. __FUNCTION__)
	std::uint32_t _line = 0;           ///< line number (e.g. __LINE__)
	std::uint64_t _timestamp = 0;      ///< captured timestamp

	const char* _tagName = nullptr;
	std::uint64_t _tagId = 0;

	using LogFunc = void (*)( const Record& ) noexcept;
	LogFunc _logFunc = nullptr;

public:
	TruncatingRecordBuilder() noexcept = default;

	/**
	 * @brief Destroy builder.
	 *
	 * Defaulted (trivial) rather than user-provided for two reasons:
	 *
	 * 1. MSVC tls_atexit crash (Windows):
	 * A user-provided destructor - even with an empty body - causes MSVC to
	 * heap-allocate a tls_atexit registration record for every thread that
	 * constructs this thread_local.  That record is freed via atexit during
	 * CRT shutdown, which races with other library atexit handlers and causes
	 * RtlFreeHeap to receive an invalid address at process exit.  A defaulted
	 * destructor is trivial (all members are trivially destructible), so MSVC
	 * never registers tls_atexit for it - no heap allocation, no shutdown race.
	 *
	 * 2. FLS/sink rebind race (Windows):
	 * Even if the crash were absent, calling commit() here would be hazardous.
	 * On thread exit the FLS callback fires this destructor asynchronously,
	 * which could race with a ScopedConfigurator on another thread rebinding
	 * the same tag's sink - causing phantom records to land in the new sink
	 * and inflating delivery counts (which affects unit test results).
	 *
	 * Commit safety: TlsTruncBuilderWrapper::~TlsTruncBuilderWrapper() is the
	 * sole commit point.  It always calls commit() before the wrapper destructs,
	 * leaving _committed=true, so any destructor call here is always a no-op.
	 * Explicit use of the builder (without the macro/wrapper) requires an
	 * explicit call to commit() for RAII delivery.
	 */
	~TruncatingRecordBuilder() noexcept = default;

	/**
	 * @brief Set context for the log statement.
	 *
	 * @param file source filename (e.g. __FILE__)
	 * @param function function name (e.g. __func__)
	 * @param line line number (e.g. __LINE__)
	 */
	template< typename Tag >
	void setContext( const char* file, const char* function, std::uint32_t line ) noexcept;

	/** @brief Adds a character. */
	TruncatingRecordBuilder& operator<<( char chr ) noexcept;

	/** @brief Adds a string literal. */
	template< std::size_t N >
	TruncatingRecordBuilder& operator<<( const char ( &lit )[ N ] ) noexcept;  // NOLINT(cppcoreguidelines-avoid-c-arrays)

	/** @brief Adds a string. */
	TruncatingRecordBuilder& operator<<( const char* str ) noexcept;

	/** @brief Adds a string. */
	TruncatingRecordBuilder& operator<<( const platform::StringView& str ) noexcept;

	/** @brief Adds an integer. */
	TruncatingRecordBuilder& operator<<( int value ) noexcept;

	/** @brief Adds an unsigned integer. */
	TruncatingRecordBuilder& operator<<( unsigned int value ) noexcept;

	/** @brief Adds a long integer. */
	TruncatingRecordBuilder& operator<<( long value ) noexcept;

	/** @brief Adds an unsigned long integer. */
	TruncatingRecordBuilder& operator<<( unsigned long value ) noexcept;

	/** @brief Adds a long long integer. */
	TruncatingRecordBuilder& operator<<( long long value ) noexcept;

	/** @brief Adds an unsigned long long integer. */
	TruncatingRecordBuilder& operator<<( unsigned long long value ) noexcept;

	/** @brief Adds a float. */
	TruncatingRecordBuilder& operator<<( float value ) noexcept;

	/** @brief Adds a float. */
	TruncatingRecordBuilder& operator<<( double value ) noexcept;

	/** @brief Adds a boolean (as a string "true" or "false"). */
	TruncatingRecordBuilder& operator<<( bool value ) noexcept;

	/** @brief Adds a pointer (as hex). */
	TruncatingRecordBuilder& operator<<( const void* ptr ) noexcept;

	/**
	 * @brief Check if truncation occurred.
	 * @return true if buffer filled and data was truncated
	 */
	bool wasTruncated() const noexcept;

	/**
	 * @brief Finalize and emit the log record.
	 *
	 * Appends "..." marker if truncated, null-terminates, creates Record,
	 * calls Logger<Tag>::log() via stored function pointer.
	 */
	void commit() noexcept;

private:
	bool hasSpace( std::size_t needed ) const noexcept;

	TruncatingRecordBuilder& appendSigned( std::size_t maxChars, std::int64_t value ) noexcept;
	TruncatingRecordBuilder& appendUnsigned( std::size_t maxChars, std::uint64_t value ) noexcept;
};

// ============================================================================
// TruncatingRecordBuilder - implementation
// ============================================================================

template< std::size_t BufferSize >
template< typename Tag >
void TruncatingRecordBuilder< BufferSize >::setContext( const char* file, const char* function, std::uint32_t line ) noexcept
{
	NOVA_ASSERT( ! _busy && "Nested logging detected! Use NOVA_LOG_STACK to avoid TLS conflicts" );

	if ( _busy )
	{
		return;
	}

	_busy = true;
	_offset = 0;
	_truncated = false;
	_committed = false;

	_file = file;
	_function = function;
	_line = line;
	_timestamp = logger_traits< Tag >::timestamp();

	_tagName = logger_traits< Tag >::tagName;
	_tagId = logger_traits< Tag >::tagId;

	_logFunc = &Logger< Tag >::log;
}

template< std::size_t BufferSize >
TruncatingRecordBuilder< BufferSize >& TruncatingRecordBuilder< BufferSize >::operator<<( char chr ) noexcept
{
	if ( _truncated )
	{
		return *this;
	}

	if ( ! hasSpace( 1 ) )
	{
		_truncated = true;
		return *this;
	}

	_buffer[ _offset++ ] = chr;
	return *this;
}

template< std::size_t BufferSize >
template< std::size_t N >
TruncatingRecordBuilder< BufferSize >& TruncatingRecordBuilder< BufferSize >::operator<<( const char ( &lit )[ N ] ) noexcept  // NOLINT(cppcoreguidelines-avoid-c-arrays)
{
	// N includes the null terminator; pass N-1 as the actual string length;
	// this overload avoids a -Wstringop-overread warning that occurs when the
	// string_view overload is called with a length-1 literal (e.g. "\n") and
	// the compiler loses track of the source bound after inlining
	static_assert( N > 0 );
	if constexpr ( N > 1 )
	{
		return operator<<( platform::StringView( lit, N - 1 ) );
	}
	return *this;
}

template< std::size_t BufferSize >
TruncatingRecordBuilder< BufferSize >& TruncatingRecordBuilder< BufferSize >::operator<<( const char* str ) noexcept
{
	if ( _truncated )
	{
		return *this;
	}

	const std::size_t len = std::strlen( str );
	return operator<<( platform::StringView( str, len ) );
}

template< std::size_t BufferSize >
TruncatingRecordBuilder< BufferSize >& TruncatingRecordBuilder< BufferSize >::operator<<( const platform::StringView& str ) noexcept
{
	if ( _truncated || str.empty() )
	{
		return *this;
	}

	if ( ! hasSpace( str.length() ) )
	{
		// copy what we can, capped at the source length so the compiler can verify
		// the memcpy doesn't read past the source region; written as an explicit
		// ternary as std::min is not available due to <algorithm> being absent
		// under NOVA_NO_STD (bare-metal / freestanding targets)
		const std::size_t spaceLeft = USABLE_SIZE - _offset;
		const std::size_t available = spaceLeft < str.length() ? spaceLeft : str.length();
		std::memcpy( _buffer.data() + _offset, str.data(), available );
		_offset += available;
		_truncated = true;
		return *this;
	}

	std::memcpy( _buffer.data() + _offset, str.data(), str.length() );
	_offset += str.length();
	return *this;
}

template< std::size_t BufferSize >
TruncatingRecordBuilder< BufferSize >& TruncatingRecordBuilder< BufferSize >::operator<<( int value ) noexcept
{
	// worst case: "-2147483648"
	return appendSigned( 11, static_cast< std::int64_t >( value ) );
}

template< std::size_t BufferSize >
TruncatingRecordBuilder< BufferSize >& TruncatingRecordBuilder< BufferSize >::operator<<( unsigned int value ) noexcept
{
	// worst case: "4294967295"
	return appendUnsigned( 10, static_cast< std::uint64_t >( value ) );
}

template< std::size_t BufferSize >
TruncatingRecordBuilder< BufferSize >& TruncatingRecordBuilder< BufferSize >::operator<<( long value ) noexcept
{
	return appendSigned( 20, static_cast< std::int64_t >( value ) );
}

template< std::size_t BufferSize >
TruncatingRecordBuilder< BufferSize >& TruncatingRecordBuilder< BufferSize >::operator<<( unsigned long value ) noexcept
{
	return appendUnsigned( 20, static_cast< std::uint64_t >( value ) );
}

template< std::size_t BufferSize >
TruncatingRecordBuilder< BufferSize >& TruncatingRecordBuilder< BufferSize >::operator<<( long long value ) noexcept
{
	return appendSigned( 20, static_cast< std::int64_t >( value ) );
}

template< std::size_t BufferSize >
TruncatingRecordBuilder< BufferSize >& TruncatingRecordBuilder< BufferSize >::operator<<( unsigned long long value ) noexcept
{
	return appendUnsigned( 20, static_cast< std::uint64_t >( value ) );
}

template< std::size_t BufferSize >
TruncatingRecordBuilder< BufferSize >& TruncatingRecordBuilder< BufferSize >::operator<<( float value ) noexcept
{
	return operator<<( static_cast< double >( value ) );
}

template< std::size_t BufferSize >
TruncatingRecordBuilder< BufferSize >& TruncatingRecordBuilder< BufferSize >::operator<<( double value ) noexcept
{
	if ( _truncated )
	{
		return *this;
	}
	if ( ! hasSpace( 32 ) )  // worst case: sign + 20 integer digits + '.' + 6 fractional
	{
		_truncated = true;
		return *this;
	}
	auto result = platform::floatToChars( _buffer.data() + _offset, _buffer.data() + BufferSize - 1, value );
	if ( result.ok )
	{
		_offset = static_cast< std::size_t >( result.ptr - _buffer.data() );
	}
	return *this;
}

template< std::size_t BufferSize >
TruncatingRecordBuilder< BufferSize >& TruncatingRecordBuilder< BufferSize >::operator<<( bool value ) noexcept
{
	return operator<<( value ? "true" : "false" );
}

template< std::size_t BufferSize >
TruncatingRecordBuilder< BufferSize >& TruncatingRecordBuilder< BufferSize >::operator<<( const void* ptr ) noexcept
{
	if ( _truncated )
	{
		return *this;
	}
	if ( ! hasSpace( 18 ) )  // "0x" + 16 hex digits
	{
		_truncated = true;
		return *this;
	}

	// NOLINT NOTE: pointer-to-integer for address formatting (std::bit_cast requires C++20)
	auto result = platform::intToChars(
		_buffer.data() + _offset,
		_buffer.data() + BufferSize - 1,
		static_cast< std::uint64_t >( reinterpret_cast< std::uintptr_t >( ptr ) ),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
		16
	);

	if ( result.ok )
	{
		_offset = static_cast< std::size_t >( result.ptr - _buffer.data() );
	}
	return *this;
}

template< std::size_t BufferSize >
bool TruncatingRecordBuilder< BufferSize >::wasTruncated() const noexcept
{
	return _truncated;
}

template< std::size_t BufferSize >
void TruncatingRecordBuilder< BufferSize >::commit() noexcept
{
	NOVA_ASSERT( _logFunc != nullptr && "commit() called without setContext()" );

	if ( ! _busy || _committed )
	{
		return;
	}

	if ( _truncated )
	{
		const char* marker = "...";
		std::memcpy( _buffer.data() + _offset, marker, TRUNCATION_MARKER_SIZE );
		_offset += TRUNCATION_MARKER_SIZE;
	}

	_buffer[ _offset ] = '\0';

	Record record {
		_timestamp,
		_tagId,
		_tagName,
		_file,
		_function,
		_line,
		static_cast< std::uint32_t >( _offset ),
		_buffer.data()
	};

	_logFunc( record );

	_busy = false;
	_committed = true;
}

template< std::size_t BufferSize >
bool TruncatingRecordBuilder< BufferSize >::hasSpace( std::size_t needed ) const noexcept
{
	return _offset + needed <= USABLE_SIZE;
}

template< std::size_t BufferSize >
TruncatingRecordBuilder< BufferSize >& TruncatingRecordBuilder< BufferSize >::appendSigned( std::size_t maxChars, std::int64_t value ) noexcept
{
	if ( _truncated )
	{
		return *this;
	}
	if ( ! hasSpace( maxChars ) )  // worst case: "-9223372036854775808"
	{
		_truncated = true;
		return *this;
	}
	auto result = platform::intToChars( _buffer.data() + _offset, _buffer.data() + BufferSize - 1, value );
	if ( result.ok )
	{
		_offset = static_cast< std::size_t >( result.ptr - _buffer.data() );
	}
	return *this;
}

template< std::size_t BufferSize >
TruncatingRecordBuilder< BufferSize >& TruncatingRecordBuilder< BufferSize >::appendUnsigned( std::size_t maxChars, std::uint64_t value ) noexcept
{
	if ( _truncated )
	{
		return *this;
	}
	if ( ! hasSpace( maxChars ) )  // worst case: "18446744073709551615"
	{
		_truncated = true;
		return *this;
	}
	auto result = platform::intToChars( _buffer.data() + _offset, _buffer.data() + BufferSize - 1, value );
	if ( result.ok )
	{
		_offset = static_cast< std::size_t >( result.ptr - _buffer.data() );
	}
	return *this;
}

// ============================================================================
// TLS-Based Wrappers
// ============================================================================

#if NOVA_HAS_TLS

/**
 * @brief Thread-local storage for TruncatingRecordBuilder instances.
 *
 * One builder instance per thread per buffer size, persisting until thread exit.
 * Not used directly - access via TlsTruncBuilderWrapper (through NOVA_LOG macro).
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
 * Allocated on the stack per log statement.  Constructor calls setContext(),
 * destructor calls commit().
 *
 * @tparam Tag the logging tag type
 * @tparam BufferSize size of the builder buffer in bytes
 */
template< typename Tag, std::size_t BufferSize >
struct TlsTruncBuilderWrapper
{
	TlsTruncBuilderWrapper( const char* file, const char* function, std::uint32_t line );
	~TlsTruncBuilderWrapper();
	inline TruncatingRecordBuilder< BufferSize >& builder() noexcept;
};

#endif // NOVA_HAS_TLS

// ============================================================================
// Stack-Based Wrapper
// ============================================================================

/**
 * @brief Stack-based RAII wrapper for TruncatingRecordBuilder.
 *
 * Allocates the builder on the stack per log statement.  Required for signal
 * handlers, nested logging contexts, and bare-metal targets (NOVA_NO_TLS).
 * NOVA_LOG falls back to this transparently when NOVA_NO_TLS is defined.
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
	StackTruncatingBuilder( const char* file, const char* function, std::uint32_t line ) noexcept;
	~StackTruncatingBuilder() noexcept;

	template< typename T >
	StackTruncatingBuilder& operator<<( const T& value ) noexcept;

	bool wasTruncated() const noexcept;
};

// ============================================================================
// TLS-Based Wrappers - implementation
// ============================================================================

#if NOVA_HAS_TLS

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
// Stack-Based Wrapper - implementation
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

#endif // KMAC_NOVA_TRUNCATING_LOGGING_H
