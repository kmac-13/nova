#pragma once
#ifndef KMAC_NOVA_EXTRAS_STREAMING_LOGGING_H
#define KMAC_NOVA_EXTRAS_STREAMING_LOGGING_H

/**
 * @file streaming_logging.h
 * @brief Heap-allocated streaming builder and macro for Nova.
 *
 * Include this header to enable streaming logging via std::ostringstream.
 * Suitable for non-real-time code where unlimited message length and broad
 * type support matter more than deterministic allocation behaviour.
 *
 * Provides:
 * - StreamingRecordBuilder<Tag>  : heap-allocated builder using std::ostringstream
 * - NOVA_LOG_STREAM(Tag)         : streaming logger macro
 *
 * WARNING: StreamingRecordBuilder performs heap allocation and is NOT suitable
 * for real-time, safety-critical, bare-metal, or crash-logging scenarios.
 * For deterministic logging use NOVA_LOG (truncating) or NOVA_LOG_CONT
 * (continuation) instead.
 *
 * Usage:
 *   #include <kmac/nova/extras/streaming_logging.h>
 *
 *   NOVA_LOG_STREAM( DiagTag ) << "Value: " << complexObject << " status: " << obj.status();
 */

#include <kmac/nova/logger.h>
#include <kmac/nova/logger_traits.h>
#include <kmac/nova/record.h>

#include <sstream>
#include <string>

namespace kmac::nova::extras
{

// ============================================================================
// StreamingRecordBuilder
// ============================================================================

/**
 * @brief Dynamic record builder using std::ostringstream.
 *
 * StreamingRecordBuilder uses std::ostringstream for maximum flexibility,
 * providing unlimited message length and support for all types that have
 * operator<< defined.
 *
 * Key characteristics:
 * - unlimited message length (heap allocated)
 * - supports all streamable types natively
 * - familiar std::ostream API
 * - PERFORMS HEAP ALLOCATION - not suitable for real-time or safety-critical use
 *
 * When to use:
 * - convenience matters more than determinism
 * - message content is highly variable or uses complex streamable types
 * - application is not real-time or safety-critical
 *
 * For allocation-free, deterministic logging:
 * - NOVA_LOG (truncating) in nova.h
 * - NOVA_LOG_CONT (continuation) in extras/continuation_logging.h
 *
 * @tparam Tag the logging tag type
 */
template< typename Tag >
class StreamingRecordBuilder
{
private:
	std::ostringstream _stream;
	std::string _message;  ///< owns the message data at commit time

	const char* _file = nullptr;
	const char* _function = nullptr;
	std::uint32_t _line = 0;
	std::uint64_t _timestamp = 0;

public:
	explicit StreamingRecordBuilder( const char* file, const char* function, std::uint32_t line );

	~StreamingRecordBuilder();

	/**
	 * @brief Stream insertion operator.
	 *
	 * Accepts any type with operator<< defined, including all primitive types,
	 * std::string, std::string_view, pointers, and user-defined streamable types.
	 *
	 * @note may allocate heap memory
	 */
	template< typename T >
	StreamingRecordBuilder& operator<<( const T& value );

	/**
	 * @brief Get the current size of the accumulated message.
	 */
	std::size_t size() const;

private:
	void commit();
};

// ============================================================================
// StreamingRecordBuilder - implementation
// ============================================================================

template< typename Tag >
StreamingRecordBuilder< Tag >::StreamingRecordBuilder( const char* file, const char* function, std::uint32_t line )
	: _file( file )
	, _function( function )
	, _line( line )
	, _timestamp( kmac::nova::logger_traits< Tag >::timestamp() )
{
}

template< typename Tag >
StreamingRecordBuilder< Tag >::~StreamingRecordBuilder()
{
	commit();
}

template< typename Tag >
template< typename T >
StreamingRecordBuilder< Tag >& StreamingRecordBuilder< Tag >::operator<<( const T& value )
{
	_stream << value;
	return *this;
}

template< typename Tag >
std::size_t StreamingRecordBuilder< Tag >::size() const
{
	return _stream.str().size();
}

template< typename Tag >
void StreamingRecordBuilder< Tag >::commit()
{
	_message = _stream.str();

	kmac::nova::Record record {
		_timestamp,
		kmac::nova::logger_traits< Tag >::tagId,
		kmac::nova::logger_traits< Tag >::tagName,
		_file,
		_function,
		_line,
		static_cast< std::uint32_t >( _message.length() ),
		_message.c_str()
	};

	// record is processed before _message is destroyed, so pointer is valid
	kmac::nova::Logger< Tag >::log( record );
}

} // namespace kmac::nova::extras

// TODO: consider TLS wrappers to ensure the stream memory is retained

// ============================================================================
// Streaming Logging Macro
// ============================================================================

/**
 * @brief Heap-allocated streaming logger.
 *
 * Uses std::ostringstream for unlimited message length and broad type support.
 *
 * WARNING: Performs heap allocation.  NOT suitable for real-time, safety-critical,
 * bare-metal, or crash-logging scenarios.  Use NOVA_LOG or NOVA_LOG_CONT for
 * deterministic, allocation-free logging.
 *
 * Usage:
 *   NOVA_LOG_STREAM( DiagTag ) << "Value: " << complexObject;
 *
 * @param TagType the logging tag type (must have logger_traits specialization)
 */
#define NOVA_LOG_STREAM( TagType ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	if constexpr ( ::kmac::nova::logger_traits< TagType >::enabled ) \
		if ( ::kmac::nova::Logger< TagType >::getSink() != nullptr ) \
			::kmac::nova::extras::StreamingRecordBuilder< TagType >( \
				FILE_NAME, __func__, __LINE__ \
			)

#endif // KMAC_NOVA_EXTRAS_STREAMING_LOGGING_H
