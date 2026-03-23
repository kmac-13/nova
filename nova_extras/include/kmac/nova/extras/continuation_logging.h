#pragma once
#ifndef KMAC_NOVA_EXTRAS_CONTINUATION_LOGGING_H
#define KMAC_NOVA_EXTRAS_CONTINUATION_LOGGING_H

/**
 * @file continuation_logging.h
 * @brief Continuation-based logging for Nova.
 *
 * Include this header to enable continuation logging, which preserves complete
 * message data by emitting multiple records when the buffer fills rather than
 * truncating.
 *
 * Provides:
 * - ContinuationRecordBuilder<BufferSize>  : the builder itself
 * - StackContinuationBuilder<Tag, Size>    : stack-based RAII wrapper
 * - TlsContBuilderStorage<Size>            : TLS storage (when NOVA_HAS_TLS)
 * - TlsContBuilderWrapper<Tag, Size>       : TLS RAII wrapper (when NOVA_HAS_TLS)
 * - NOVA_LOG_CONT(Tag)                     : TLS logger (or stack if NOVA_NO_TLS)
 * - NOVA_LOG_CONT_BUF(Tag, Size)           : TLS logger with custom buffer
 * - NOVA_LOG_CONT_STACK(Tag)               : stack-based logger
 * - NOVA_LOG_CONT_BUF_STACK(Tag, Size)     : stack-based logger with custom buffer
 *
 * Usage:
 *   #include <kmac/nova/nova.h>
 *   #include <kmac/nova/extras/continuation_logging.h>
 *
 *   NOVA_LOG_CONT(DiagTag)
 *     << "User: " << username
 *     << " performed action: " << action
 *     << " on file: " << longFilePath;
 *   // automatically emits continuation records if message exceeds buffer size
 *
 * Output format:
 *   [DIAG] main.cpp:42 (process): Long message part 1
 *   [DIAG] main.cpp:42 (process): [cont] ...part 2
 *   [DIAG] main.cpp:42 (process): [cont] ...part 3
 *
 * When to prefer continuation over truncating (NOVA_LOG):
 * - data completeness is critical (diagnostics, error details, stack traces)
 * - message lengths are highly variable or unpredictable
 * - multi-record emission is acceptable at the sink
 *
 * @see NOVA_LOG in nova.h for the default truncating logger
 */

#include <kmac/nova/nova.h>
#include <kmac/nova/immovable.h>
#include <kmac/nova/logger.h>
#include <kmac/nova/logger_traits.h>
#include <kmac/nova/platform/array.h>
#include <kmac/nova/platform/config.h>
#include <kmac/nova/platform/float_to_chars.h>
#include <kmac/nova/platform/int_to_chars.h>

#include <cassert>
#include <cstddef>
#include <cstring>
#include <string_view>

namespace kmac::nova
{

// ============================================================================
// ContinuationRecordBuilder
// ============================================================================

/**
 * @brief Fixed-buffer record builder that emits continuations when full.
 *
 * ContinuationRecordBuilder builds log messages in a fixed-size buffer,
 * automatically emitting continuation records when the buffer fills.  This
 * ensures complete data preservation with zero heap allocation.
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
 * - see TlsContBuilderWrapper and TlsContBuilderStorage for TLS details
 * - nested logging detection: assert in debug, silent drop in release
 *
 * Stack-based usage (opt-in via macros or direct usage):
 * - NOVA_LOG_CONT_STACK uses StackContinuationBuilder wrapper
 * - required for signal handlers
 * - required for functions called within log expressions
 *
 * Continuation behavior:
 * - first record: contains initial message data (no prefix)
 * - subsequent records: prefixed with "[cont] " (7 characters)
 * - all records: share same timestamp, file, function, line, tag
 *
 * Log analysis - reassembling messages:
 * 1. records with same timestamp + file + line + function + tag belong together
 * 2. records with "[cont] " prefix follow an initial record
 * 3. sort by timestamp, then by file/line to get original order
 * 4. concatenate message content, removing "[cont] " prefixes
 *
 * @tparam BufferSize buffer size in bytes (default 1024)
 */
template< std::size_t BufferSize = 1024 >
class ContinuationRecordBuilder : private Immovable
{
private:
	static constexpr const char* CONTINUATION_PREFIX = "[cont] ";
	static constexpr std::size_t CONTINUATION_PREFIX_SIZE = 7; // strlen("[cont] ")

	static_assert( BufferSize >= 16, "Buffer size must be at least 16 bytes" );
	static_assert( BufferSize <= 65536, "Buffer size must not exceed 64KB (stack safety)" );

	platform::Array< char, BufferSize > _buffer{};  ///< stack-allocated message buffer
	std::size_t _offset = 0;     ///< current write position
	bool _busy = false;          ///< non-atomic, thread-local reentrancy guard
	bool _committed = false;     ///< true if already committed

	const char* _file = nullptr;       ///< source file
	const char* _function = nullptr;   ///< function name
	std::uint32_t _line = 0;           ///< line number
	std::uint64_t _timestamp = 0;      ///< captured timestamp (shared by all continuations)

	bool _isContinuation = false;       ///< true if currently building a continuation
	std::size_t _continuationCount = 0; ///< number of continuations emitted

	const char* _tagName = nullptr;
	std::uint64_t _tagId = 0;

	using LogFunc = void (*)( const Record& ) noexcept;
	LogFunc _logFunc = nullptr;

public:
	ContinuationRecordBuilder() noexcept = default;

	/**
	 * @brief Destroy builder.
	 *
	 * Defaulted (trivial) to avoid MSVC tls_atexit heap allocation and
	 * FLS/sink rebind race on Windows.  See TruncatingRecordBuilder for
	 * the full explanation.  Commit is always performed by the RAII wrappers
	 * before this destructor is reached.
	 */
	~ContinuationRecordBuilder() noexcept = default;

	template< typename Tag >
	void setContext( const char* file, const char* function, std::uint32_t line ) noexcept;

	template< typename T >
	ContinuationRecordBuilder& operator<<( const T& value ) noexcept;

	std::size_t continuationCount() const noexcept;

	void commit() noexcept;

private:
	void commitCurrent() noexcept;
	void startContinuation() noexcept;
	std::size_t availableSpace() const noexcept;
	void ensureSpace( std::size_t needed ) noexcept;

	void append( char chr ) noexcept;
	void append( const char* str ) noexcept;

	template< std::size_t N >
	void append( const char ( &lit )[ N ] ) noexcept;  // NOLINT(cppcoreguidelines-avoid-c-arrays)

	void append( const std::string_view& str ) noexcept;

	inline void append( int value ) noexcept;
	inline void append( unsigned int value ) noexcept;
	inline void append( long value ) noexcept;
	inline void append( unsigned long value ) noexcept;
	inline void append( long long value ) noexcept;
	inline void append( unsigned long long value ) noexcept;
	inline void append( float value ) noexcept;
	void append( double value ) noexcept;

	void append( bool value ) noexcept;
	void append( const void* ptr ) noexcept;

	void appendSigned( std::size_t maxChars, std::int64_t value ) noexcept;
	void appendUnsigned( std::size_t maxChars, std::uint64_t value ) noexcept;
};

// ============================================================================
// ContinuationRecordBuilder - implementation
// ============================================================================

template< std::size_t BufferSize >
template< typename Tag >
void ContinuationRecordBuilder< BufferSize >::setContext( const char* file, const char* function, std::uint32_t line ) noexcept
{
	assert( ! _busy && "Nested logging detected! Use NOVA_LOG_CONT_STACK" );

	if ( _busy )
	{
		return;
	}

	_busy = true;
	_offset = 0;
	_committed = false;
	_isContinuation = false;
	_continuationCount = 0;

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
	if ( ! _busy || _committed )
	{
		return;
	}

	commitCurrent();

	_busy = false;
	_committed = true;
}

template< std::size_t BufferSize >
void ContinuationRecordBuilder< BufferSize >::commitCurrent() noexcept
{
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
}

template< std::size_t BufferSize >
void ContinuationRecordBuilder< BufferSize >::startContinuation() noexcept
{
	_offset = 0;
	_isContinuation = true;
	++_continuationCount;

	std::memcpy( _buffer.data(), CONTINUATION_PREFIX, CONTINUATION_PREFIX_SIZE );
	_offset = CONTINUATION_PREFIX_SIZE;
}

template< std::size_t BufferSize >
std::size_t ContinuationRecordBuilder< BufferSize >::availableSpace() const noexcept
{
	return BufferSize - 1 - _offset;
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
void ContinuationRecordBuilder< BufferSize >::append( char chr ) noexcept
{
	ensureSpace( 1 );
	_buffer[ _offset++ ] = chr;
}

template< std::size_t BufferSize >
void ContinuationRecordBuilder< BufferSize >::append( const char* str ) noexcept
{
	const std::size_t len = std::strlen( str );
	append( std::string_view( str, len ) );
}

template< std::size_t BufferSize >
template< std::size_t N >
void ContinuationRecordBuilder< BufferSize >::append( const char ( &lit )[ N ] ) noexcept  // NOLINT(cppcoreguidelines-avoid-c-arrays)
{
	static_assert( N > 0 );
	if constexpr ( N > 1 )
	{
		append( std::string_view( lit, N - 1 ) );
	}
}

template< std::size_t BufferSize >
void ContinuationRecordBuilder< BufferSize >::append( const std::string_view& str ) noexcept
{
	if ( str.empty() )
	{
		return;
	}

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
		std::memcpy( _buffer.data() + _offset, str.data() + pos, toCopy );
		_offset += toCopy;
		pos += toCopy;
	}
}

template< std::size_t BufferSize >
void ContinuationRecordBuilder< BufferSize >::append( int value ) noexcept
{
	// worst case: "-2147483648"
	appendSigned( 11, static_cast< std::int64_t >( value ) );
}

template< std::size_t BufferSize >
void ContinuationRecordBuilder< BufferSize >::append( unsigned int value ) noexcept
{
	// worst case: "4294967295"
	appendUnsigned( 10, static_cast< std::uint64_t >( value ) );
}

template< std::size_t BufferSize >
void ContinuationRecordBuilder< BufferSize >::append( long value ) noexcept
{
	appendSigned( 20, static_cast< std::int64_t >( value ) );
}

template< std::size_t BufferSize >
void ContinuationRecordBuilder< BufferSize >::append( unsigned long value ) noexcept
{
	appendUnsigned( 20, static_cast< std::uint64_t >( value ) );
}

template< std::size_t BufferSize >
void ContinuationRecordBuilder< BufferSize >::append( long long value ) noexcept
{
	appendSigned( 20, static_cast< std::int64_t >( value ) );
}

template< std::size_t BufferSize >
void ContinuationRecordBuilder< BufferSize >::append( unsigned long long value ) noexcept
{
	appendUnsigned( 20, static_cast< std::uint64_t >( value ) );
}

template< std::size_t BufferSize >
void ContinuationRecordBuilder< BufferSize >::appendSigned( std::size_t maxSize, std::int64_t value ) noexcept
{
	ensureSpace( maxSize );  // worst case: "-9223372036854775808"
	auto result = kmac::nova::platform::intToChars( _buffer.data() + _offset, _buffer.data() + BufferSize - 1, value );
	if ( result.ok )
	{
		_offset = static_cast< std::size_t >( result.ptr - _buffer.data() );
	}
}

template< std::size_t BufferSize >
void ContinuationRecordBuilder< BufferSize >::appendUnsigned( std::size_t maxSize, std::uint64_t value ) noexcept
{
	ensureSpace( maxSize );  // worst case: "18446744073709551615"
	auto result = kmac::nova::platform::intToChars( _buffer.data() + _offset, _buffer.data() + BufferSize - 1, value );
	if ( result.ok )
	{
		_offset = static_cast< std::size_t >( result.ptr - _buffer.data() );
	}
}

template< std::size_t BufferSize >
void ContinuationRecordBuilder< BufferSize >::append( float value ) noexcept
{
	append( static_cast< double >( value ) );
}

template< std::size_t BufferSize >
void ContinuationRecordBuilder< BufferSize >::append( double value ) noexcept
{
	ensureSpace( 32 );  // worst case: sign + 20 integer digits + '.' + 6 fractional
	auto result = kmac::nova::platform::floatToChars( _buffer.data() + _offset, _buffer.data() + BufferSize - 1, value );
	if ( result.ok )
	{
		_offset = static_cast< std::size_t >( result.ptr - _buffer.data() );
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
	ensureSpace( 18 );
	auto result = kmac::nova::platform::intToChars(
		_buffer.data() + _offset,
		_buffer.data() + BufferSize - 1,
		static_cast< std::uint64_t >( reinterpret_cast< std::uintptr_t >( ptr ) ),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
		16
	);
	if ( result.ok )
	{
		_offset = static_cast< std::size_t >( result.ptr - _buffer.data() );
	}
}

// ============================================================================
// TLS-Based Wrappers
// ============================================================================

#if NOVA_HAS_TLS

/**
 * @brief Thread-local storage for ContinuationRecordBuilder instances.
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
 * @tparam Tag the logging tag type
 * @tparam BufferSize size of the builder buffer in bytes
 */
template< typename Tag, std::size_t BufferSize >
struct TlsContBuilderWrapper
{
	TlsContBuilderWrapper( const char* file, const char* function, std::uint32_t line );
	~TlsContBuilderWrapper();
	inline ContinuationRecordBuilder< BufferSize >& builder() noexcept;
};

#endif // NOVA_HAS_TLS

// ============================================================================
// Stack-Based Wrapper
// ============================================================================

/**
 * @brief Stack-based wrapper for ContinuationRecordBuilder.
 *
 * @tparam Tag the logging tag type
 * @tparam BufferSize size of the builder buffer in bytes
 */
template< typename Tag, std::size_t BufferSize >
class StackContinuationBuilder : private Immovable
{
private:
	ContinuationRecordBuilder< BufferSize > _builder;

public:
	StackContinuationBuilder( const char* file, const char* function, std::uint32_t line ) noexcept;
	~StackContinuationBuilder() noexcept;

	template< typename T >
	StackContinuationBuilder& operator<<( const T& value ) noexcept;

	std::size_t continuationCount() const noexcept;
};

// ============================================================================
// TLS-Based Wrappers - implementation
// ============================================================================

#if NOVA_HAS_TLS

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

#endif // NOVA_HAS_TLS

// ============================================================================
// Stack-Based Wrapper - implementation
// ============================================================================

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

} // namespace kmac::nova

// ============================================================================
// Continuation Logging Macros
// ============================================================================

// NOLINT comments suppress cppcoreguidelines-macro-usage on each definition.

/**
 * @brief Thread-local continuation logger with default buffer size (1024 bytes).
 *
 * Emits continuation records rather than truncating when the buffer fills,
 * preserving the complete message across multiple records.
 *
 * When NOVA_NO_TLS is defined, falls back to NOVA_LOG_CONT_STACK transparently.
 *
 * Usage:
 *   NOVA_LOG_CONT(DiagTag) << "Long diagnostic: " << veryLongString;
 *
 * @param TagType the logging tag type
 * @see NOVA_LOG_CONT_BUF for custom buffer sizes
 * @see NOVA_LOG_CONT_STACK for stack-based variant
 */
#define NOVA_LOG_CONT( TagType ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	NOVA_LOG_CONT_BUF( TagType, ::kmac::nova::NOVA_DEFAULT_BUFFER_SIZE )

/**
 * @brief Thread-local continuation logger with custom buffer size.
 *
 * @param TagType the logging tag type
 * @param BufferSize buffer size in bytes (16-65536)
 */
#if NOVA_HAS_TLS
#define NOVA_LOG_CONT_BUF( TagType, BufferSize ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	if constexpr ( ::kmac::nova::logger_traits< TagType >::enabled ) \
		if ( ::kmac::nova::Logger< TagType >::getSink() != nullptr ) \
			::kmac::nova::TlsContBuilderWrapper< TagType, BufferSize >( FILE_NAME, __func__, __LINE__ ).builder()
#else
#define NOVA_LOG_CONT_BUF( TagType, BufferSize ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	NOVA_LOG_CONT_BUF_STACK( TagType, BufferSize )
#endif

/**
 * @brief Stack-based continuation logger with default buffer size (1024 bytes).
 *
 * Required for signal handlers and nested logging contexts.
 *
 * @param TagType the logging tag type
 */
#define NOVA_LOG_CONT_STACK( TagType ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	NOVA_LOG_CONT_BUF_STACK( TagType, ::kmac::nova::NOVA_DEFAULT_BUFFER_SIZE )

/**
 * @brief Stack-based continuation logger with custom buffer size.
 *
 * @param TagType the logging tag type
 * @param BufferSize buffer size in bytes (16-65536, keep <2KB for signal handlers)
 */
#define NOVA_LOG_CONT_BUF_STACK( TagType, BufferSize ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	if constexpr ( ::kmac::nova::logger_traits< TagType >::enabled ) \
		if ( ::kmac::nova::Logger< TagType >::getSink() != nullptr ) \
			::kmac::nova::StackContinuationBuilder< TagType, BufferSize >( FILE_NAME, __func__, __LINE__ )

#endif // KMAC_NOVA_EXTRAS_CONTINUATION_LOGGING_H
