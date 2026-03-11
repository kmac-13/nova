#pragma once
#ifndef KMAC_NOVA_CONTINUATION_RECORD_BUILDER_H
#define KMAC_NOVA_CONTINUATION_RECORD_BUILDER_H

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
 * @brief Fixed-buffer record builder that emits continuations when full.
 *
 * ContinuationRecordBuilder builds log messages in a fixed-size buffer, automatically
 * emitting continuation records when the buffer fills.  This ensures complete data
 * preservation with zero heap allocation.
 *
 * Key characteristics:
 * - zero heap allocation (completely deterministic)
 * - fixed buffer size (predictable memory usage)
 * - complete data preservation (no truncation, ever)
 * - multiple records for long messages (automatic continuations)
 * - all continuations share original timestamp
 * - continuation markers for reassembly ("[cont] " prefix)
 * - tag-agnostic design (stores tag info at runtime via setContext)
 *
 * Thread-local usage (default via macros):
 * - NOVA_LOG_CONT uses TlsContBuilderWrapper for thread-local storage
 * - provides markedly improved performance over stack-based
 * - see TlsContBuilderWrapper and TlsContBuilderStorage for TLS details
 * - nested logging detection: assert in debug, silent drop in release
 *
 * Stack-based usage (opt-in via macros or direct usage):
 * - NOVA_LOG_CONT_STACK uses StackContinuationBuilder wrapper
 * - required for signal handlers (avoids stack overflow)
 * - required for functions called within log expressions (avoids nested logging)
 * - see StackContinuationBuilder for stack-based wrapper details
 *
 * Comparison with other builders:
 * - vs TruncatingRecordBuilder: complete data vs single record
 * - vs StreamingRecordBuilder (Nova Extras): deterministic vs heap allocation
 * - best for: unpredictable message lengths, complete data preservation
 * - consider TruncatingRecordBuilder when: single record per statement is critical
 *
 * Continuation behavior:
 * - first record: Contains initial message data (no prefix)
 * - subsequent records: prefixed with "[cont] " (7 characters)
 * - all records: share same timestamp, file, function, line, tag
 * - buffer fills: emits current record automatically, starts new continuation
 * - instance falls out of scope: destructor emits final record with remaining data
 *
 * Output example (BufferSize=64):
 *   [INFO] main.cpp:42 (process): User alice performed action: open file /very/long/path/
 *   [INFO] main.cpp:42 (process): [cont] to/document.txt with mode=read, flags=0x123
 *
 * Usage via macros (recommended):
 *
 *   // thread-local (default, fast)
 *   NOVA_LOG_CONT(DebugTag)
 *     << "User: " << username
 *     << " performed action: " << action
 *     << " on file: " << longFilePath;
 *
 *   // custom buffer size
 *   NOVA_LOG_CONT_BUF(VerboseTag, 512) << "Smaller buffer";
 *
 *   // stack-based (for signal handlers, nested contexts)
 *   NOVA_LOG_CONT_STACK(CrashTag) << "Signal handler logging";
 *
 * Direct usage (advanced):
 *
 *   ContinuationRecordBuilder<1024> builder;
 *   builder.setContext<InfoTag>(__FILE__, __func__, __LINE__);
 *   builder << "Very long message...";  // may emit continuations automatically
 *   builder.commit();  // explicitly emits the record (no use of RAII)
 *
 * Performance characteristics:
 * - O(1) space complexity (fixed buffer)
 * - O(n) time complexity for n characters appended
 * - O(k) record emissions where k = ceil(messageLength / BufferSize)
 * - no allocation overhead
 * - cache-friendly (buffer stays hot)
 *
 * Memory safety:
 * - all operations are bounds-checked
 * - no buffer overruns possible
 * - stack safety limits enforced (max 64KB)
 * - each record is null-terminated
 *
 * Thread safety and considerations:
 * - builder itself is not thread-safe (single-threaded use)
 * - thread-local storage (via wrappers) ensures one builder per thread
 * - Logger<Tag>::log() call IS thread-safe (atomic sink pointer)
 * - continuations from different threads may interleave at sink
 * - use SynchronizedSink if atomic multi-record emission is required
 * - each continuation is a separate sink->process() call
 *
 * Nested logging (important):
 * - DO NOT log inside expressions being logged:
 *     UNSAFE: NOVA_LOG_CONT(Tag) << "Result: " << compute();
 *     // if compute() logs, corruption occurs!
 *
 *     SAFE: auto result = compute();  // may log internally
 *           NOVA_LOG_CONT(Tag) << "Result: " << result;
 *
 * - Use stack-based logging in nested functions:
 *     void compute() {
 *         NOVA_LOG_CONT_STACK(Tag) << "computing";  // no conflict
 *     }
 *
 * Signal handlers:
 * - MUST use stack-based builder, e.g. NOVA_LOG_CONT_STACK (not thread-local)
 * - thread-local can be interrupted mid-log, causing corruption
 * - stack-based provides independent buffer
 *
 * When to use vs TruncatingRecordBuilder:
 *
 * Use ContinuationRecordBuilder when:
 * - data completeness is critical (diagnostics, error details, stack traces)
 * - message lengths are highly variable or unpredictable
 * - log analysis tools can handle continuation markers
 * - multi-record emission is acceptable
 *
 * Use TruncatingRecordBuilder when:
 * - single record per statement is important
 * - most messages fit in buffer (truncation is rare)
 * - performance is critical (fewer sink->process() calls)
 * - log analysis tools expect one record per log statement
 *
 * Buffer size guidelines:
 * - minimum: 16 bytes (compile-time enforced, not practical)
 * - typical: 512-2048 bytes (reduces continuation frequency)
 * - default: 1024 bytes (good balance)
 * - maximum: 65536 bytes (64KB stack safety limit)
 * - larger buffers = fewer continuations = better performance
 *
 * Continuation marker format:
 * - constant: "[cont] " (7 characters including space)
 * - added to start of each continuation record
 * - reduces effective buffer size by 7 bytes per continuation
 * - no sequence numbers or other metadata
 *
 * Log analysis - reassembling messages:
 * 1. records with same timestamp + file + line + function + tag belong together
 * 2. records with "[cont] " prefix follow an initial record
 * 3. sort by timestamp, then by file/line to get original order
 * 4. concatenate message content, removing "[cont] " prefixes
 *
 * Implementation details:
 * - tag-agnostic: no Tag template parameter
 * - setContext<Tag>() captures tag name, ID, timestamp, and Logger<Tag>::log function pointer
 * - commitContinuation() emits intermediate records during streaming (private)
 * - commit() emits final record using stored function pointer
 * - _busy flag prevents nested logging (reentrant detection)
 * - _committed flag enables idempotent commit (safe double-call)
 *
 * @tparam BufferSize buffer size in bytes (default 1024)
 */
template< std::size_t BufferSize = 1024 >
class ContinuationRecordBuilder
{
private:
	static constexpr const char* CONTINUATION_PREFIX = "[cont] ";
	static constexpr std::size_t CONTINUATION_PREFIX_SIZE = 7; // strlen("[cont] ")

	static_assert( BufferSize >= 16, "Buffer size must be at least 16 bytes" );
	static_assert( BufferSize <= 65536, "Buffer size must not exceed 64KB (stack safety)" );

	char _buffer[ BufferSize ];  ///< stack-allocated message buffer
	std::size_t _offset = 0;     ///< current write position
	bool _busy = false;          ///< non-atomic, thread-local reentrancy guard, not used for synchronization
	bool _committed = false;     ///< true if already committed

	const char* _file;           ///< source file (e.g. __FILE__)
	const char* _function;       ///< function name (e.g. __FUNCTION__)
	std::uint32_t _line;         ///< line number (e.g. __LINE__)
	std::uint64_t _timestamp;    ///< captured timestamp (shared by all continuations)

	bool _isContinuation = false;       ///< true if currently building a continuation
	std::size_t _continuationCount = 0; ///< number of continuations emitted

	// tag and logging details
	const char* _tagName;
	std::uint64_t _tagId;

	using LogFunc = void (*)( const Record& ) noexcept;
	LogFunc _logFunc;

public:
	/**
	 * @brief Construct builder with no context or data.
	 */
	ContinuationRecordBuilder() noexcept = default;

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
	 * Commit safety: TlsContBuilderWrapper::~TlsContBuilderWrapper() is the
	 * sole commit point.  It always calls commit() before the wrapper destructs,
	 * leaving _committed=true, so any destructor call here is always a no-op.
	 * Explicit use of the builder (without the macro/wrapper) requires an
	 * explicit call to commit() for RAII delivery.
	 */
	~ContinuationRecordBuilder() noexcept = default;

	NO_COPY_NO_MOVE( ContinuationRecordBuilder );

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
	 * Appends data to buffer.  If buffer fills during append, emits current
	 * record and starts a continuation automatically.
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
	 * @note might trigger continuation emission (calls sink->process())
	 * @note subsequent appends continue in new buffer with "[cont] " prefix
	 */
	template< typename T >
	ContinuationRecordBuilder& operator<<( const T& value ) noexcept;

	/**
	 * @brief Get number of continuations emitted so far.
	 *
	 * @return number of continuation records emitted (0 if all in first record)
	 *
	 * @note does not include the final record (emitted in destructor)
	 * @note useful for metrics or debugging, rarely needed in production
	 */
	std::size_t continuationCount() const noexcept;

	void commit() noexcept;

private:
	/**
	 * @brief Emit current buffer as a log record.
	 *
	 * Null-terminates buffer, creates Record, calls Logger<Tag>::log().
	 * Does not reset buffer (caller handles that).
	 */
	void commitCurrent() noexcept;

	/**
	 * @brief Start a new continuation record.
	 *
	 * Resets buffer, increments continuation counter, adds "[cont] " prefix.
	 * Called automatically when buffer fills during append.
	 */
	void startContinuation() noexcept;

	/**
	 * @brief Get available space in buffer.
	 *
	 * @return number of bytes available (accounting for null terminator)
	 */
	std::size_t availableSpace() const noexcept;

	/**
	 * @brief Ensure buffer has space for N bytes.
	 *
	 * If insufficient space, emits current record and starts continuation.
	 *
	 * @param needed number of bytes needed
	 *
	 * @note might trigger continuation emission
	 */
	void ensureSpace( std::size_t needed ) noexcept;

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
	 *
	 * @note might trigger continuations if string is long
	 * @note splits string across continuations if needed
	 */
	void append( const char* str ) noexcept;

	/**
	 * @brief Append string literal to buffer.
	 *
	 * Preferred over append(const char*) for string literals: the array size N
	 * is known at compile time.

	 * @tparam N array size including null terminator
	 * @param lit string literal to append
	 */
	template< std::size_t N >
	void append( const char ( &lit )[ N ] ) noexcept;

	/**
	 * @brief Append string to buffer.
	 *
	 * @param str string_view to append
	 *
	 * @note might trigger continuations if string is long
	 * @note splits string across continuations if needed
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
void ContinuationRecordBuilder< BufferSize >::setContext( const char* file, const char* function, std::uint32_t line ) noexcept
{
	assert( ! _busy && "Nested logging detected! Use NOVA_LOG_STACK" );

	// silently fail in release builds
	if ( _busy ) return;  // drop nested log, prevent corruption

	_busy = true;
	_offset = 0;
	_committed = false;
	_isContinuation = false;
	_continuationCount = 0;

	_file = file;
	_function = function;
	_line = line;
	_timestamp = logger_traits< Tag >::timestamp();

	// store Tag info for later use in commitCurrent
	_tagName = logger_traits< Tag >::tagName;
	_tagId = logger_traits< Tag >::tagId;

	// store function pointer to Logger<Tag>::log
	_logFunc = &Logger< Tag >::log;
}

template< std::size_t BufferSize >
template< typename T >
ContinuationRecordBuilder< BufferSize >& ContinuationRecordBuilder< BufferSize >::operator<<( const T& value ) noexcept
{
	append( value );
	return *this;
}

template< std::size_t BufferSize >
std::size_t ContinuationRecordBuilder< BufferSize >::continuationCount() const noexcept
{
	return _continuationCount;
}

template< std::size_t BufferSize >
void ContinuationRecordBuilder< BufferSize >::commit() noexcept
{
	// don't attempt to commit if not busy or already committed
	if ( ! _busy || _committed ) return;

	commitCurrent();

	_busy = false;
	_committed = true;
}

template< std::size_t BufferSize >
void ContinuationRecordBuilder< BufferSize >::commitCurrent() noexcept
{
	_buffer[ _offset ] = '\0';

	Record record {
		_tagName,
		_tagId,
		_file,
		_function,
		_line,
		_timestamp, // same timestamp for all continuations
		_buffer,
		_offset
	};

	_logFunc( record );
}

template< std::size_t BufferSize >
void ContinuationRecordBuilder< BufferSize >::startContinuation() noexcept
{
	_offset = 0;
	_isContinuation = true;
	++_continuationCount;

	// add continuation prefix
	std::memcpy( _buffer, CONTINUATION_PREFIX, CONTINUATION_PREFIX_SIZE );
	_offset = CONTINUATION_PREFIX_SIZE;
}

template< std::size_t BufferSize >
std::size_t ContinuationRecordBuilder< BufferSize >::availableSpace() const noexcept
{
	return BufferSize - 1 - _offset; // -1 for null terminator
}

template< std::size_t BufferSize >
void ContinuationRecordBuilder< BufferSize >::ensureSpace( std::size_t needed ) noexcept
{
	if ( availableSpace() < needed )
	{
		commitCurrent();
		startContinuation();
	}
}

template< std::size_t BufferSize >
void ContinuationRecordBuilder< BufferSize >::append( char c ) noexcept
{
	ensureSpace( 1 );
	_buffer[ _offset++ ] = c;
}

template< std::size_t BufferSize >
void ContinuationRecordBuilder< BufferSize >::append( const char* str ) noexcept
{
	const std::size_t len = std::strlen( str );
	append( std::string_view( str, len ) );
}

template< std::size_t BufferSize >
template< std::size_t N >
void ContinuationRecordBuilder< BufferSize >::append( const char ( &lit )[ N ] ) noexcept
{
	// N includes the null terminator; pass N-1 as the actual string length
	static_assert( N > 0 );
	if constexpr ( N > 1 )
	{
		append( std::string_view( lit, N - 1 ) );
	}
}

template< std::size_t BufferSize >
void ContinuationRecordBuilder< BufferSize >::append( const std::string_view& str ) noexcept
{
	// return early if the string is empty
	if ( str.empty() ) return;

	std::size_t pos = 0;

	while ( pos < str.size() )
	{
		std::size_t available = availableSpace();
		if ( available == 0 )
		{
			commitCurrent();
			startContinuation();
			available = availableSpace();
		}

		const std::size_t toCopy = std::min( str.size() - pos, available );
		std::memcpy( _buffer + _offset, str.data() + pos, toCopy );
		_offset += toCopy;
		pos += toCopy;
	}
}

template< std::size_t BufferSize >
void ContinuationRecordBuilder< BufferSize >::append( int value ) noexcept
{
	// worst case: "-2147483648" = 11 chars
	ensureSpace( 11 );

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
void ContinuationRecordBuilder< BufferSize >::append( unsigned int value ) noexcept
{
	// worst case: "4294967295" = 10 chars
	ensureSpace( 10 );

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
void ContinuationRecordBuilder< BufferSize >::append( long value ) noexcept
{
	// worst case: 64-bit = 20 chars
	ensureSpace( 20 );

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
void ContinuationRecordBuilder< BufferSize >::append( long long value ) noexcept
{
	// worst case: 64-bit = 20 chars
	ensureSpace( 20 );

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
void ContinuationRecordBuilder< BufferSize >::append( unsigned long value ) noexcept
{
	ensureSpace( 20 );

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
void ContinuationRecordBuilder< BufferSize >::append( unsigned long long value ) noexcept
{
	// worst case: 64-bit = 20 chars
	ensureSpace( 20 );

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
void ContinuationRecordBuilder< BufferSize >::append( double value ) noexcept
{
	// worst case for double in fixed notation: ~25 chars
	ensureSpace( 25 );

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
void ContinuationRecordBuilder< BufferSize >::append( float value ) noexcept
{
	ensureSpace( 15 );

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
void ContinuationRecordBuilder< BufferSize >::append( bool value ) noexcept
{
	append( value ? "true" : "false" );
}

template< std::size_t BufferSize >
void ContinuationRecordBuilder< BufferSize >::append( const void* ptr ) noexcept
{
	// hex pointer: "0x" + 16 hex digits = 18 chars max
	ensureSpace( 18 );

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

#endif // KMAC_NOVA_CONTINUATION_RECORD_BUILDER_H
