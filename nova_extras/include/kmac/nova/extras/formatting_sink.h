#pragma once
#ifndef KMAC_NOVA_EXTRAS_FORMATTING_SINK_H
#define KMAC_NOVA_EXTRAS_FORMATTING_SINK_H

#include "kmac/nova/record.h"
#include "kmac/nova/sink.h"
#include "kmac/nova/extras/buffer.h"
#include "kmac/nova/extras/formatter.h"

#include <array>
#include <cstddef>

namespace kmac::nova::extras
{

/**
 * @brief Sink that formats records via a Formatter before passing to a downstream Sink.
 *
 * ✅ SAFE FOR SAFETY-CRITICAL SYSTEMS
 *
 * FormattingSink applies a Formatter to each record and forwards the result
 * to a downstream Sink.  It is the general-purpose counterpart to
 * FormattingFileSink: where FormattingFileSink writes to a FILE*, this class
 * forwards to any Sink - an OStreamSink, CompositeSink, SynchronizedSink,
 * or any user-defined Sink.
 *
 * Formatting model:
 * The Formatter interface is resumable: begin() is called once per record,
 * then format() is called repeatedly until it returns true (record complete).
 * If the formatting buffer fills before the record is complete, the buffer is
 * flushed to the downstream sink mid-record and format() is called again.
 * Each flush produces a separate downstream process() call carrying a partial
 * formatted message - this is expected behavior for large records with small
 * buffer sizes.  Choose BufferSize large enough for typical records to avoid
 * unnecessary fragmentation.
 *
 * Usage:
 *   OStreamSink console( std::cout );
 *   ISO8601Formatter formatter;
 *   FormattingSink2<> sink( console, formatter );
 *
 *   ScopedConfigurator config;
 *   config.bind< MyTag >( &sink );
 *
 *   NOVA_LOG_TRUNC( MyTag ) << "Hello";
 *   // downstream receives: "2025-02-22T12:34:56.789Z [MyTag] file.cpp:42 fn - Hello\n"
 *
 * Thread safety:
 * - not thread-safe (wrap with SynchronizedSink if needed)
 * - formatter instance must not be shared across threads
 *
 * @tparam BufferSize size of the internal formatting buffer in bytes
 */
template< std::size_t BufferSize = 256UL * 1024UL >
class FormattingSink final : public kmac::nova::Sink
{
private:
	kmac::nova::Sink* _downstream = nullptr;   ///< downstream sink (not owned)
	Formatter* _formatter = nullptr;           ///< formatter (not owned)
	std::array< char, BufferSize > _formatBuffer = { };

public:
	/**
	 * @brief Construct with downstream sink and formatter.
	 *
	 * @param downstream sink to receive formatted records (must remain valid)
	 * @param formatter formatter to apply to each record (must remain valid)
	 */
	FormattingSink( kmac::nova::Sink& downstream, Formatter& formatter ) noexcept;

	/**
	 * @brief Format a record and forward to the downstream sink.
	 *
	 * Calls formatter.begin() once, then formatter.format() repeatedly until
	 * the record is complete.  Each time the buffer fills, the accumulated
	 * bytes are forwarded downstream as a partial record before the buffer is
	 * reset and formatting resumes.
	 *
	 * @param record the record to format and forward
	 */
	void process( const kmac::nova::Record& record ) noexcept override;

private:
	/**
	 * @brief Forward accumulated buffer contents downstream and reset offset.
	 *
	 * Constructs a shallow copy of the supplied record with message and
	 * messageSize replaced by the formatted bytes, then calls
	 * _downstream->process().
	 *
	 * @param record original record (supplies tag, timestamp, file, etc.)
	 * @param size number of formatted bytes to forward
	 */
	void flushBuffer( const kmac::nova::Record& record, std::size_t size ) noexcept;
};

template< std::size_t BufferSize >
FormattingSink< BufferSize >::FormattingSink( kmac::nova::Sink& downstream, Formatter& formatter ) noexcept
	: _downstream( &downstream )
	, _formatter( &formatter )
{
}

template< std::size_t BufferSize >
void FormattingSink< BufferSize >::process( const kmac::nova::Record& record ) noexcept
{
	_formatter->begin( record );

	bool done = false;
	while ( ! done )
	{
		Buffer buf( _formatBuffer.data(), BufferSize );
		done = _formatter->format( record, buf );

		if ( buf.size() > 0 )
		{
			flushBuffer( record, buf.size() );
		}
	}
}

template< std::size_t BufferSize >
void FormattingSink< BufferSize >::flushBuffer( const kmac::nova::Record& record, std::size_t size ) noexcept
{
	// forward a shallow copy of the record with message replaced by formatted bytes;
	// all other fields (tag, timestamp, file, line, function) are preserved so the
	// downstream sink can use them if needed
	kmac::nova::Record formatted = record;
	formatted.messageSize = static_cast< std::uint32_t >( size );
	formatted.message = _formatBuffer.data();
	_downstream->process( formatted );
}

} // namespace kmac::nova::extras

#endif // KMAC_NOVA_EXTRAS_FORMATTING_SINK_H
