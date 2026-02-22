#pragma once
#ifndef KMAC_NOVA_TRUNCATING_RECORD_BUILDER_H
#define KMAC_NOVA_TRUNCATING_RECORD_BUILDER_H

#include "logger.h"
#include "logger_traits.h"

#include <cassert>
#include <charconv>
#include <cstddef>
#include <cstring>
#include <string_view>

namespace kmac::nova
{

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
 * - NOVA_LOG_TRUNC uses TlsTruncBuilderWrapper for thread-local storage
 * - provides markedly improved performance over stack-based
 * - see TlsTruncBuilderWrapper and TlsTruncBuilderStorage for TLS details
 * - nested logging detection: assert in debug, silent drop in release
 *
 * Stack-based usage (opt-in via macros or direct usage):
 * - NOVA_LOG_TRUNC_STACK uses StackTruncatingBuilder wrapper
 * - required for signal handlers (avoids stack overflow)
 * - required for functions called within log expressions (avoids nested logging)
 * - see StackTruncatingBuilder for stack-based wrapper details
 *
 * Comparison with other builders:
 * - vs ContinuationRecordBuilder: single record vs multiple records
 * - vs StreamingRecordBuilder (Nova Extras): deterministic vs heap allocation
 * - best for: real-time systems, hot paths, most production logging
 * - Consider ContinuationRecordBuilder when: complete data preservation is critical
 *
 * Truncation behavior:
 * - when buffer fills, additional data is silently dropped
 * - message ends with "..." to indicate truncation
 * - wasTruncated() returns true if truncation occurred
 * - truncation state resets per log statement
 *
 * Usage via macros (recommended):
 *
 *   // thread-local (default, fast)
 *   NOVA_LOG_TRUNC(InfoTag) << "User " << username << " logged in";
 *
 *   // custom buffer size
 *   NOVA_LOG_TRUNC_BUF(DebugTag, 256) << "Short message";
 *
 *   // stack-based (for signal handlers, nested contexts)
 *   NOVA_LOG_TRUNC_STACK(CrashTag) << "Signal handler logging";
 *
 * Direct usage (advanced):
 *
 *   TruncatingRecordBuilder<1024> builder;
 *   builder.setContext<InfoTag>(__FILE__, __func__, __LINE__);
 *   builder << "Message " << value;
 *   builder.commit();  // explicitly emits the record (no use of RAII)
 *
 * Performance characteristics:
 * - O(1) space complexity (fixed buffer)
 * - O(n) time complexity for n characters appended
 * - no allocation overhead
 * - cache-friendly (buffer stays hot)
 *
 * Memory safety:
 * - all operations are bounds-checked
 * - no buffer overruns possible
 * - stack safety limits enforced (max 64KB)
 * - null terminator always added
 *
 * Thread safety:
 * - builder itself is not thread-safe (single-threaded use)
 * - thread-local storage (via wrappers) ensures one builder per thread
 * - Logger<Tag>::log() call IS thread-safe (atomic sink pointer)
 *
 * Nested logging (important):
 * - DO NOT log inside expressions being logged:
 *     UNSAFE: NOVA_LOG_TRUNC(Tag) << "Result: " << compute();
 *     // if compute() logs, corruption occurs!
 *
 *     SAFE: auto result = compute();  // may log internally
 *           NOVA_LOG_TRUNC(Tag) << "Result: " << result;
 *
 * - Use stack-based logging in nested functions:
 *     void compute() {
 *         NOVA_LOG_TRUNC_STACK(Tag) << "computing";  // no conflict
 *     }
 *
 * Signal handlers:
 * - MUST use stack-based builder, e.g. NOVA_LOG_TRUNC_STACK (not thread-local)
 * - thread-local can be interrupted mid-log, causing corruption
 * - stack-based provides independent buffer
 *
 * Buffer size guidelines:
 * - minimum: 16 bytes (compile-time enforced, not practical)
 * - typical: 512-1024 bytes (covers 90-95% of messages)
 * - default: 1024 bytes (good balance)
 * - maximum: 65536 bytes (64KB stack safety limit)
 *
 * Implementation details:
 * - tag-agnostic: no Tag template parameter
 * - setContext<Tag>() captures tag name, ID, timestamp, and Logger<Tag>::log function pointer
 * - commit() uses stored function pointer to emit record
 * - _busy flag prevents nested logging (reentrant detection)
 * - _committed flag enables idempotent commit (safe double-call)
 *
 * @tparam BufferSize buffer size in bytes (default 1024)
 */
template< std::size_t BufferSize = 1024 >
class TruncatingRecordBuilder
{
private:
	static constexpr std::size_t TRUNCATION_MARKER_SIZE = 3; // "..."
	static constexpr std::size_t USABLE_SIZE = BufferSize - TRUNCATION_MARKER_SIZE - 1; // -1 for null

	static_assert( BufferSize >= 16, "Buffer size must be at least 16 bytes" );
	static_assert( BufferSize <= 65536, "Buffer size must not exceed 64KB (stack safety)" );

	char _buffer[ BufferSize ];  ///< stack-allocated message buffer
	std::size_t _offset = 0;     ///< current write position
	bool _busy = false;          ///< non-atomic, thread-local reentrancy guard, not used for synchronization
	bool _truncated = false;     ///< true if truncation occurred
	bool _committed = false;     ///< true if already committed

	const char* _file = nullptr;       ///< source file (e.g __FILE__)
	const char* _function = nullptr;   ///< function name (e.g. __FUNCTION__)
	std::uint32_t _line = 0;           ///< line number (e.g. __LINE__)
	std::uint64_t _timestamp = 0;      ///< captured timestamp

	// tag and logging details
	const char* _tagName;
	std::uintptr_t _tagId;

	using LogFunc = void (*)( const Record& ) noexcept;
	LogFunc _logFunc;

public:
	/**
	 * @brief Construct builder with no context or data.
	 */
	TruncatingRecordBuilder() noexcept = default;

	/**
	 * @brief Destroy builder.
	 *
	 * Defaulted (trivial) rather than user-provided for two reasons:
	 *
	 * 1. MSVC tls_atexit crash (Windows):
	 * A user-provided destructor — even with an empty body — causes MSVC to
	 * heap-allocate a tls_atexit registration record for every thread that
	 * constructs this thread_local.  That record is freed via atexit during
	 * CRT shutdown, which races with other library atexit handlers and causes
	 * RtlFreeHeap to receive an invalid address at process exit.  A defaulted
	 * destructor is trivial (all members are trivially destructible), so MSVC
	 * never registers tls_atexit for it — no heap allocation, no shutdown race.
	 *
	 * 2. FLS/sink rebind race (Windows):
	 * Even if the crash were absent, calling commit() here would be hazardous.
	 * On thread exit the FLS callback fires this destructor asynchronously,
	 * which could race with a ScopedConfigurator on another thread rebinding
	 * the same tag's sink — causing phantom records to land in the new sink
	 * and inflating delivery counts (which affects unit test results).
	 *
	 * Commit safety: TlsTruncBuilderWrapper::~TlsTruncBuilderWrapper() is the
	 * sole commit point.  It always calls commit() before the wrapper destructs,
	 * leaving _committed=true, so any destructor call here is always a no-op.
	 * Explicit use of the builder (without the macro/wrapper) requires an
	 * explicit call to commit() for RAII delivery.
	 */
	~TruncatingRecordBuilder() noexcept = default;

	NO_COPY_NO_MOVE( TruncatingRecordBuilder );

	/**
	 * @brief Sets the context data for the log.
	 *
	 * @param file source filename (e.g. __FILE__)
	 * @param function function name (e.g. __FUNCTION__ or __func__)
	 * @param line line number (e.g. __LINE__)
	 *
	 * @note tag details are retrieved from logger_traits
	 */
	template< typename Tag >
	void setContext( const char* file, const char* function, std::uint32_t line ) noexcept;

	/**
	 * @brief Stream insertion operator for building message.
	 *
	 * @tparam T type to append (must have append() overload)
	 * @param value value to append to message
	 * @return reference to this builder (for chaining)
	 *
	 * Supported types:
	 * - C-strings (const char*)
	 * - characters (char)
	 * - integers (int, unsigned, long, unsigned long)
	 * - floating point (float, double)
	 * - booleans (bool -> "true"/"false")
	 * - pointers (const void* -> hex address)
	 *
	 * @note after truncation, all further appends are no-ops
	 */
	template< typename T >
	TruncatingRecordBuilder& operator<<( const T& value ) noexcept;

	/**
	 * @brief Check if truncation occurred.
	 *
	 * @return true if buffer filled and data was truncated
	 *
	 * @note rarely needed - truncation marker "..." is usually sufficient
	 */
	bool wasTruncated() const noexcept;

	/**
	 * @brief Finalize and emit log record:
	 * 1. add "..." marker if truncated
	 * 2. null-terminate buffer
	 * 3. create Record with all metadata
	 * 4. call Logger<Tag>::log()
	 * 5. set committed flag and reset busy flag
	 */
	void commit() noexcept;

private:
	/**
	 * @brief Check if buffer has space for N bytes.
	 *
	 * @param needed number of bytes needed
	 * @return true if space available, false otherwise
	 */
	bool hasSpace( std::size_t needed ) const noexcept;

	/**
	 * @brief Append single character to buffer.
	 *
	 * @param c character to append
	 */
	void append( char c ) noexcept;

	/**
	 * @brief Append C-string to buffer.
	 *
	 * @param str null-terminated string to append
	 */
	void append( const char* str ) noexcept;

	/**
	 * @brief Append string literal to buffer.
	 *
	 * Preferred over append(const char*) for string literals: the array size N
	 * is known at compile time.
	 * This overload was introduced due to address the following compiler warning,
	 * which appears to be a result of calling the string_view overload with a
	 * string literal of length 1, e.g. "\n":
	 * - warning: 'void* memcpy(void*, const void*, size_t)' reading 1021 or more bytes from a region of size 1 [-Wstringop-overread]
	 * - In member function 'void kmac::nova::TruncatingRecordBuilder<BufferSize>::append(const std::string_view&)',
	 * - inlined from 'void kmac::nova::TruncatingRecordBuilder<BufferSize>::append(const char*)',

	 * @tparam N array size including null terminator
	 * @param lit string literal to append
	 */
	template< std::size_t N >
	void append( const char ( &lit )[ N ] ) noexcept;

	/**
	 * @brief Append string view to buffer.
	 *
	 * @param str string view to append
	 */
	void append( const std::string_view& str ) noexcept;

	/**
	 * @brief Append integer to buffer.
	 *
	 * @param value integer value (base 10)
	 */
	void append( int value ) noexcept;

	/**
	 * @brief Append unsigned integer to buffer.
	 *
	 * @param value unsigned integer value (base 10)
	 */
	void append( unsigned int value ) noexcept;

	/**
	 * @brief Append long integer to buffer.
	 *
	 * @param value long integer value (base 10)
	 */
	void append( long value ) noexcept;

	/**
	 * @brief Append long integer to buffer.
	 *
	 * @param value long integer value (base 10)
	 */
	void append( long long value ) noexcept;

	/**
	 * @brief Append unsigned long to buffer.
	 *
	 * @param value unsigned long value (base 10)
	 */
	void append( unsigned long value ) noexcept;

	/**
	 * @brief Append unsigned long to buffer.
	 *
	 * @param value unsigned long value (base 10)
	 */
	void append( unsigned long long value ) noexcept;

	/**
	 * @brief Append double to buffer.
	 *
	 * @param value double-precision float (default precision)
	 */
	void append( double value ) noexcept;

	/**
	 * @brief Append float to buffer.
	 *
	 * @param value single-precision float (default precision)
	 */
	void append( float value ) noexcept;

	/**
	 * @brief Append boolean to buffer.
	 *
	 * @param value boolean (outputs "true" or "false")
	 */
	void append( bool value ) noexcept;

	/**
	 * @brief Append pointer address to buffer.
	 *
	 * @param ptr pointer (outputs as hex address: 0x...)
	 */
	void append( const void* ptr ) noexcept;
};

template< std::size_t BufferSize >
template< typename Tag >
void TruncatingRecordBuilder< BufferSize >::setContext( const char* file, const char* function, std::uint32_t line ) noexcept
{
	assert( ! _busy && "Nested logging detected! Use NOVA_LOG_STACK to avoid TLS conflicts" );

	// silently fail in release builds
	if ( _busy ) return;  // drop nested log, prevent corruption

	_busy = true;
	_offset = 0;
	_truncated = false;
	_committed = false;

	_file = file;
	_function = function;
	_line = line;
	_timestamp = logger_traits< Tag >::timestamp();

	// store Tag info for later use in commitCurrent
	_tagName = logger_traits< Tag >::tagName;
	_tagId = logger_traits< Tag >::tagId();

	// store function pointer to Logger<Tag>::log
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
	// don't attempt to commit if not busy or already committed
	if ( ! _busy || _committed ) return;

	if ( _truncated )
	{
		// add truncation marker
		const char* marker = "...";
		std::memcpy( _buffer + _offset, marker, TRUNCATION_MARKER_SIZE );
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
		_buffer,
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
void TruncatingRecordBuilder< BufferSize >::append( char c ) noexcept
{
	if ( _truncated ) return;

	if ( ! hasSpace( 1 ) )
	{
		_truncated = true;
		return;
	}

	_buffer[ _offset++ ] = c;
}

template< std::size_t BufferSize >
void TruncatingRecordBuilder< BufferSize >::append( const char* str ) noexcept
{
	if ( _truncated ) return;

	std::size_t len = std::strlen( str );
	append( std::string_view( str, len ) );
}

template< std::size_t BufferSize >
template< std::size_t N >
void TruncatingRecordBuilder< BufferSize >::append( const char ( &lit )[ N ] ) noexcept
{
	// N includes the null terminator; pass N-1 as the actual string length
	static_assert( N > 0 );
	if constexpr ( N > 1 )
	{
		append( std::string_view( lit, N - 1 ) );
	}
}

template< std::size_t BufferSize >
void TruncatingRecordBuilder< BufferSize >::append( const std::string_view& str ) noexcept
{
	// return early if truncation has already occurred or the string is empty
	if ( _truncated || str.empty() ) return;

	if ( ! hasSpace( str.length() ) )
	{
		// copy what we can, capped at the source length so the compiler can verify
		// the memcpy doesn't read past the source region; without the min(), the
		// compiler can lose track of the source bound when this is inlined from
		// the array-reference append overload, generating -Wstringop-overread warning
		const std::size_t available = std::min( USABLE_SIZE - _offset, str.length() );
		std::memcpy( _buffer + _offset, str.data(), available );
		_offset += available;
		_truncated = true;
		return;
	}

	std::memcpy( _buffer + _offset, str.data(), str.length() );
	_offset += str.length();
}

template< std::size_t BufferSize >
void TruncatingRecordBuilder< BufferSize >::append( int value ) noexcept
{
	if ( _truncated ) return;

	// worst case: "-2147483648" = 11 chars
	if ( ! hasSpace( 11 ) )
	{
		_truncated = true;
		return;
	}

	auto [ ptr, ec ] = std::to_chars(
		_buffer + _offset,
		_buffer + BufferSize - 1,
		value
	);

	if ( ec == std::errc{} )
	{
		_offset = ptr - _buffer;
	}
}

template< std::size_t BufferSize >
void TruncatingRecordBuilder< BufferSize >::append( unsigned int value ) noexcept
{
	if ( _truncated ) return;

	// worst case: "4294967295" = 10 chars
	if ( ! hasSpace( 10 ) )
	{
		_truncated = true;
		return;
	}

	auto [ ptr, ec ] = std::to_chars(
		_buffer + _offset,
		_buffer + BufferSize - 1,
		value
	);

	if ( ec == std::errc{} )
	{
		_offset = ptr - _buffer;
	}
}

template< std::size_t BufferSize >
void TruncatingRecordBuilder< BufferSize >::append( long value ) noexcept
{
	if ( _truncated ) return;

	// worst case: 64-bit = 20 chars
	if ( ! hasSpace( 20 ) )
	{
		_truncated = true;
		return;
	}

	auto [ ptr, ec ] = std::to_chars(
		_buffer + _offset,
		_buffer + BufferSize - 1,
		value
	);

	if ( ec == std::errc{} )
	{
		_offset = ptr - _buffer;
	}
}

template< std::size_t BufferSize >
void TruncatingRecordBuilder< BufferSize >::append( long long value ) noexcept
{
	if ( _truncated ) return;

	// worst case: 64-bit = 20 chars
	if ( ! hasSpace( 20 ) )
	{
		_truncated = true;
		return;
	}

	auto [ ptr, ec ] = std::to_chars(
		_buffer + _offset,
		_buffer + BufferSize - 1,
		value
	);

	if ( ec == std::errc{} )
	{
		_offset = ptr - _buffer;
	}
}

template< std::size_t BufferSize >
void TruncatingRecordBuilder< BufferSize >::append( unsigned long value ) noexcept
{
	if ( _truncated ) return;

	if ( ! hasSpace( 20 ) )
	{
		_truncated = true;
		return;
	}

	auto [ ptr, ec ] = std::to_chars(
		_buffer + _offset,
		_buffer + BufferSize - 1,
		value
	);

	if ( ec == std::errc{} )
	{
		_offset = ptr - _buffer;
	}
}

template< std::size_t BufferSize >
void TruncatingRecordBuilder< BufferSize >::append( unsigned long long value ) noexcept
{
	if ( _truncated ) return;

	if ( ! hasSpace( 20 ) )
	{
		_truncated = true;
		return;
	}

	auto [ ptr, ec ] = std::to_chars(
		_buffer + _offset,
		_buffer + BufferSize - 1,
		value
	);

	if ( ec == std::errc{} )
	{
		_offset = ptr - _buffer;
	}
}

template< std::size_t BufferSize >
void TruncatingRecordBuilder< BufferSize >::append( double value ) noexcept
{
	if ( _truncated ) return;

	// worst case for double in fixed notation: ~25 chars
	if ( ! hasSpace( 25 ) )
	{
		_truncated = true;
		return;
	}

	auto [ ptr, ec ] = std::to_chars(
		_buffer + _offset,
		_buffer + BufferSize - 1,
		value
	);

	if ( ec == std::errc{} )
	{
		_offset = ptr - _buffer;
	}
}

template< std::size_t BufferSize >
void TruncatingRecordBuilder< BufferSize >::append( float value ) noexcept
{
	if ( _truncated ) return;

	if ( ! hasSpace( 15 ) )
	{
		_truncated = true;
		return;
	}

	auto [ ptr, ec ] = std::to_chars(
		_buffer + _offset,
		_buffer + BufferSize - 1,
		value
	);

	if ( ec == std::errc{} )
	{
		_offset = ptr - _buffer;
	}
}

template< std::size_t BufferSize >
void TruncatingRecordBuilder< BufferSize >::append( bool value ) noexcept
{
	append( value ? "true" : "false" );
}

template< std::size_t BufferSize >
void TruncatingRecordBuilder< BufferSize >::append( const void* ptr ) noexcept
{
	if ( _truncated ) return;

	// hex pointer: "0x" + 16 hex digits = 18 chars max
	if ( ! hasSpace( 18 ) )
	{
		_truncated = true;
		return;
	}

	auto [ p, ec ] = std::to_chars(
		_buffer + _offset,
		_buffer + BufferSize - 1,
		reinterpret_cast< std::uintptr_t >( ptr ),
		16
	);

	if ( ec == std::errc{} )
	{
		_offset = p - _buffer;
	}
}

} // namespace kmac::nova

#endif // KMAC_NOVA_TRUNCATING_RECORD_BUILDER_H
