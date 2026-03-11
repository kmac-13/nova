#pragma once
#ifndef KMAC_NOVA_EXTRAS_STREAMING_RECORD_BUILDER_H
#define KMAC_NOVA_EXTRAS_STREAMING_RECORD_BUILDER_H

#include "kmac/nova/logger.h"
#include "kmac/nova/logger_traits.h"
#include "kmac/nova/record.h"

#include <sstream>
#include <string>

namespace kmac::nova::extras
{

/**
 * @brief Dynamic record builder using std::ostringstream.
 *
 * StreamingRecordBuilder uses std::ostringstream for maximum flexibility
 * and ease of use. This variant provides:
 * - unlimited message length (heap allocation)
 * - support for all types with operator<<
 * - familiar std::ostream API
 * - simple implementation
 *
 * IMPORTANT: This builder performs heap allocation and is NOT suitable for:
 * - real-time systems
 * - safety-critical applications
 * - crash handlers / emergency logging
 * - systems requiring deterministic behavior
 *
 * Use this builder when:
 * - convenience is more important than determinism
 * - application is not real-time or safety-critical
 * - message lengths are highly variable and unpredictable
 * - heap allocation is acceptable
 *
 * For deterministic, allocation-free logging, use TruncatingRecordBuilder
 * or ContinuationRecordBuilder from the core library instead.
 *
 * @tparam Tag The logging tag type
 */
template< typename Tag >
class StreamingRecordBuilder
{
private:
	std::ostringstream _stream;
	std::string _message; // Owns the message data

	const char* _file;
	const char* _function;
	std::uint32_t _line;
	std::uint64_t _timestamp;

public:
	explicit StreamingRecordBuilder( const char* file, const char* function, std::uint32_t line );

	~StreamingRecordBuilder();

	/**
	 * Stream operator - leverages std::ostream's extensive type support.
	 *
	 * This works with:
	 * - all primitive types (int, float, double, bool, char, etc.)
	 * - strings (const char*, std::string, std::string_view)
	 * - pointers (formatted as hex)
	 * - any type with operator<< defined
	 * - standard library types (if you include appropriate headers)
	 *
	 * Note: This may allocate heap memory multiple times.
	 */
	template< typename T >
	StreamingRecordBuilder& operator<<( const T& value );

	/**
	 * Get the current size of the accumulated message.
	 * Can be used to check message length before commit.
	 */
	std::size_t size() const;

private:
	void commit();
};

/**
 * @brief Null record builder for disabled loggers.
 */
class NullStreamingRecordBuilder
{
public:
	template< typename T >
	constexpr NullStreamingRecordBuilder& operator<<( const T& )
	{
		return *this;
	}
};

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
	// move string out of stream (one allocation minimum)
	_message = _stream.str();

	kmac::nova::Record record {
		kmac::nova::logger_traits< Tag >::tagName,
		kmac::nova::logger_traits< Tag >::tagId,
		_file,
		_function,
		_line,
		_timestamp,
		_message.c_str(), // points to owned string data
		_message.length()
	};

	kmac::nova::Logger< Tag >::log( record );
	// record is processed before _message is destroyed, so pointer is valid
}

} // namespace kmac::nova::extras

#endif // KMAC_NOVA_EXTRAS_STREAMING_RECORD_BUILDER_H
