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
#include <kmac/nova/platform/config.h>

#include <sstream>
#include <string>

namespace kmac {
namespace nova {
namespace extras {

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
class StreamingRecordBuilder
{
private:
	std::ostringstream _stream;
	std::string _message;  ///< owns the message data at commit time
	bool _committed = false;

	const char* _file = nullptr;
	const char* _function = nullptr;
	std::uint32_t _line = 0;
	std::uint64_t _timestamp = 0;

	const char* _tagName = nullptr;
	std::uint64_t _tagId = 0;

	using LogFunc = void (*)( const Record& );  // noexcept not allowed before C++17
	LogFunc _logFunc = nullptr;

public:
	StreamingRecordBuilder() noexcept = default;

	~StreamingRecordBuilder();

	template< typename Tag >
	StreamingRecordBuilder& setContext( const char* file, const char* function, std::uint32_t line ) noexcept;

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

StreamingRecordBuilder::~StreamingRecordBuilder()
{
	commit();
}

template< typename Tag >
StreamingRecordBuilder& StreamingRecordBuilder::setContext( const char* file, const char* function, std::uint32_t line ) noexcept
{
	_file = file;
	_function = function;
	_line = line;
	_timestamp = ::kmac::nova::logger_traits< Tag >::timestamp();

	_tagName = ::kmac::nova::logger_traits< Tag >::tagName;
	_tagId = ::kmac::nova::logger_traits< Tag >::tagId;

	_logFunc = &::kmac::nova::Logger< Tag >::log;

	return *this;
}

template< typename T >
StreamingRecordBuilder& StreamingRecordBuilder::operator<<( const T& value )
{
	_stream << value;
	return *this;
}

std::size_t StreamingRecordBuilder::size() const
{
	return _stream.str().size();
}

void StreamingRecordBuilder::commit()
{
	NOVA_ASSERT( _logFunc != nullptr && "commit() called without setContext()" );

	// don't commit if already committed
	if ( _committed )
	{
		return;
	}

	_message = _stream.str();

	const kmac::nova::Record record {
		_timestamp,
		_tagId,
		_tagName,
		_file,
		_function,
		_line,
		static_cast< std::uint32_t >( _message.length() ),
		_message.c_str()
	};

	// record is processed before _message is destroyed, so pointer is valid
	_logFunc( record );
	_committed = true;
}

} // namespace extras
} // namespace nova
} // namespace kmac

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
			::kmac::nova::extras::StreamingRecordBuilder().setContext< TagType >( \
				FILE_NAME, __func__, __LINE__ \
			)

#endif // KMAC_NOVA_EXTRAS_STREAMING_LOGGING_H
