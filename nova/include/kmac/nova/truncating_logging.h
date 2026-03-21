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
#include "platform/config.h"

#include <array>
#include <cassert>
#include <charconv>
#include <cstddef>
#include <cstring>
#include <string_view>

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
 * - nested logging detection: assert in debug, silent drop in release
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

	std::array< char, BufferSize > _buffer = { };  ///< stack-allocated message buffer
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

	/**
	 * @brief Stream insertion operator.
	 *
	 * Supported types: const char*, char, int, unsigned int, long, long long,
	 * unsigned long, unsigned long long, float, double, bool, const void*.
	 *
	 * @note after truncation, all further appends are no-ops
	 */
	template< typename T >
	TruncatingRecordBuilder& operator<<( const T& value ) noexcept;

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

	void append( char chr ) noexcept;
	void append( const char* str ) noexcept;

	template< std::size_t N >
	void append( const char ( &lit )[ N ] ) noexcept;  // NOLINT(cppcoreguidelines-avoid-c-arrays)

	void append( const std::string_view& str ) noexcept;
	void append( int value ) noexcept;
	void append( unsigned int value ) noexcept;
	void append( long value ) noexcept;
	void append( long long value ) noexcept;
	void append( unsigned long value ) noexcept;
	void append( unsigned long long value ) noexcept;
	void append( double value ) noexcept;
	void append( float value ) noexcept;
	void append( bool value ) noexcept;
	void append( const void* ptr ) noexcept;
};

// ============================================================================
// TruncatingRecordBuilder - implementation
// ============================================================================

template< std::size_t BufferSize >
template< typename Tag >
void TruncatingRecordBuilder< BufferSize >::setContext( const char* file, const char* function, std::uint32_t line ) noexcept
{
	assert( ! _busy && "Nested logging detected! Use NOVA_LOG_STACK to avoid TLS conflicts" );

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
template< typename T >
TruncatingRecordBuilder< BufferSize >& TruncatingRecordBuilder< BufferSize >::operator<<( const T& value ) noexcept
{
	append( value );
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
		_tagName,
		_tagId,
		_file,
		_function,
		_line,
		_timestamp,
		_buffer.data(),
		_offset
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
void TruncatingRecordBuilder< BufferSize >::append( char chr ) noexcept
{
	if ( _truncated )
	{
		return;
	}

	if ( ! hasSpace( 1 ) )
	{
		_truncated = true;
		return;
	}

	_buffer[ _offset++ ] = chr;
}

template< std::size_t BufferSize >
void TruncatingRecordBuilder< BufferSize >::append( const char* str ) noexcept
{
	if ( _truncated )
	{
		return;
	}

	const std::size_t len = std::strlen( str );
	append( std::string_view( str, len ) );
}

template< std::size_t BufferSize >
template< std::size_t N >
void TruncatingRecordBuilder< BufferSize >::append( const char ( &lit )[ N ] ) noexcept  // NOLINT(cppcoreguidelines-avoid-c-arrays)
{
	// N includes the null terminator; pass N-1 as the actual string length;
	// this overload avoids a -Wstringop-overread warning that occurs when the
	// string_view overload is called with a length-1 literal (e.g. "\n") and
	// the compiler loses track of the source bound after inlining
	static_assert( N > 0 );
	if constexpr ( N > 1 )
	{
		append( std::string_view( lit, N - 1 ) );
	}
}

template< std::size_t BufferSize >
void TruncatingRecordBuilder< BufferSize >::append( const std::string_view& str ) noexcept
{
	if ( _truncated || str.empty() )
	{
		return;
	}

	if ( ! hasSpace( str.length() ) )
	{
		// copy what we can, capped at the source length so the compiler can verify
		// the memcpy doesn't read past the source region; without the min(), the
		// compiler can lose track of the source bound when this is inlined from
		// the array-reference append overload, generating -Wstringop-overread warning
		const std::size_t available = std::min( USABLE_SIZE - _offset, str.length() );
		std::memcpy( _buffer.data() + _offset, str.data(), available );
		_offset += available;
		_truncated = true;
		return;
	}

	std::memcpy( _buffer.data() + _offset, str.data(), str.length() );
	_offset += str.length();
}

template< std::size_t BufferSize >
void TruncatingRecordBuilder< BufferSize >::append( int value ) noexcept
{
	if ( _truncated )
	{
		return;
	}
	if ( ! hasSpace( 11 ) )  // worst case: "-2147483648"
	{
		_truncated = true;
		return;
	}
	auto [ ptr, ec ] = std::to_chars( _buffer.data() + _offset, _buffer.data() + BufferSize - 1, value );
	if ( ec == std::errc{} )
	{
		_offset = ptr - _buffer.data();
	}
}

template< std::size_t BufferSize >
void TruncatingRecordBuilder< BufferSize >::append( unsigned int value ) noexcept
{
	if ( _truncated )
	{
		return;
	}
	if ( ! hasSpace( 10 ) )  // worst case: "4294967295"
	{
		_truncated = true;
		return;
	}
	auto [ ptr, ec ] = std::to_chars( _buffer.data() + _offset, _buffer.data() + BufferSize - 1, value );
	if ( ec == std::errc{} )
	{
		_offset = ptr - _buffer.data();
	}
}

template< std::size_t BufferSize >
void TruncatingRecordBuilder< BufferSize >::append( long value ) noexcept
{
	if ( _truncated )
	{
		return;
	}
	if ( ! hasSpace( 20 ) )  // worst case: 64-bit signed
	{
		_truncated = true;
		return;
	}
	auto [ ptr, ec ] = std::to_chars( _buffer.data() + _offset, _buffer.data() + BufferSize - 1, value );
	if ( ec == std::errc{} )
	{
		_offset = ptr - _buffer.data();
	}
}

template< std::size_t BufferSize >
void TruncatingRecordBuilder< BufferSize >::append( long long value ) noexcept
{
	if ( _truncated )
	{
		return;
	}
	if ( ! hasSpace( 20 ) )
	{
		_truncated = true;
		return;
	}
	auto [ ptr, ec ] = std::to_chars( _buffer.data() + _offset, _buffer.data() + BufferSize - 1, value );
	if ( ec == std::errc{} )
	{
		_offset = ptr - _buffer.data();
	}
}

template< std::size_t BufferSize >
void TruncatingRecordBuilder< BufferSize >::append( unsigned long value ) noexcept
{
	if ( _truncated )
	{
		return;
	}
	if ( ! hasSpace( 20 ) )
	{
		_truncated = true;
		return;
	}
	auto [ ptr, ec ] = std::to_chars( _buffer.data() + _offset, _buffer.data() + BufferSize - 1, value );
	if ( ec == std::errc{} )
	{
		_offset = ptr - _buffer.data();
	}
}

template< std::size_t BufferSize >
void TruncatingRecordBuilder< BufferSize >::append( unsigned long long value ) noexcept
{
	if ( _truncated )
	{
		return;
	}
	if ( ! hasSpace( 20 ) )
	{
		_truncated = true;
		return;
	}
	auto [ ptr, ec ] = std::to_chars( _buffer.data() + _offset, _buffer.data() + BufferSize - 1, value );
	if ( ec == std::errc{} )
	{
		_offset = ptr - _buffer.data();
	}
}

template< std::size_t BufferSize >
void TruncatingRecordBuilder< BufferSize >::append( double value ) noexcept
{
	if ( _truncated )
	{
		return;
	}
#if NOVA_HAS_FLOAT_CHARCONV
	if ( ! hasSpace( 25 ) )  // worst case for double
	{
		_truncated = true;
		return;
	}
	auto [ ptr, ec ] = std::to_chars( _buffer.data() + _offset, _buffer.data() + BufferSize - 1, value );
	if ( ec == std::errc{} )
	{
		_offset = ptr - _buffer.data();
	}
#else
	// NOVA_NO_FLOAT_CHARCONV: emit integer part + marker (e.g. "3.<float>")
	// full float formatting requires a custom to_chars implementation (see TODO
	// in platform/config.h NOVA_HAS_FLOAT_CHARCONV section)
	append( static_cast< long long >( value ) );
	append( ".<float>" );
#endif
}

template< std::size_t BufferSize >
void TruncatingRecordBuilder< BufferSize >::append( float value ) noexcept
{
	if ( _truncated )
	{
		return;
	}
#if NOVA_HAS_FLOAT_CHARCONV
	if ( ! hasSpace( 15 ) )
	{
		_truncated = true;
		return;
	}
	auto [ ptr, ec ] = std::to_chars( _buffer.data() + _offset, _buffer.data() + BufferSize - 1, value );
	if ( ec == std::errc{} )
	{
		_offset = ptr - _buffer.data();
	}
#else
	// NOVA_NO_FLOAT_CHARCONV: emit integer part + marker (e.g. "3.<float>")
	append( static_cast< long long >( value ) );
	append( ".<float>" );
#endif
}

template< std::size_t BufferSize >
void TruncatingRecordBuilder< BufferSize >::append( bool value ) noexcept
{
	append( value ? "true" : "false" );
}

template< std::size_t BufferSize >
void TruncatingRecordBuilder< BufferSize >::append( const void* ptr ) noexcept
{
	if ( _truncated )
	{
		return;
	}
	if ( ! hasSpace( 18 ) )  // "0x" + 16 hex digits
	{
		_truncated = true;
		return;
	}

	// NOLINT NOTE: pointer-to-integer for address formatting (std::bit_cast requires C++20)
	auto [ p, ec ] = std::to_chars(
		_buffer.data() + _offset,
		_buffer.data() + BufferSize - 1,
		reinterpret_cast< std::uintptr_t >( ptr ),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
		16
	);

	if ( ec == std::errc{} )
	{
		_offset = p - _buffer.data();
	}
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
