#pragma once
#ifndef KMAC_NOVA_EXTRAS_FORMATTING_FILE_SINK_H
#define KMAC_NOVA_EXTRAS_FORMATTING_FILE_SINK_H

#include "kmac/nova/record.h"
#include "kmac/nova/sink.h"
#include "kmac/nova/extras/buffer.h"
#include "kmac/nova/extras/formatter.h"

#include <cstdio>

namespace kmac::nova::extras
{

/**
 * @brief Sink that writes formatted log messages to a FILE* with explicit flush control.
 *
 * ✅ SAFE FOR SAFETY-CRITICAL SYSTEMS (with caveats on FILE* behavior)
 *
 * FormattingFileSink writes log messages to a C FILE* stream with optional
 * formatting.  Unlike OStreamSink, this uses the C stdio API, which can be
 * more predictable in embedded/real-time environments.
 *
 * Features:
 * - writes to any FILE* (stdout, stderr, fopen'd files)
 * - optional formatting via Formatter interface
 * - explicit flush control (never auto-flushes)
 * - uses setvbuf for explicit buffer control
 * - fixed-size formatting buffer (no heap allocation)
 *
 * Flushing behavior:
 * - NEVER auto-flushes on write (deterministic behavior)
 * - ONLY flushes when flush() is explicitly called
 * - use SynchronizedSink wrapper if thread safety needed
 *
 * Limitations:
 * - not thread-safe (wrap with SynchronizedSink if needed)
 * - caller manages FILE* lifetime (must remain valid)
 * - formatter must remain valid during sink lifetime
 * - formatting buffer is fixed-size (256KB)
 *
 * Usage without formatter (raw output):
 *   FILE* file = fopen("log.txt", "w");
 *   setvbuf( file, nullptr, _IOFBF, 128 * 1024 );
 *   FormattingFileSink<> sink( file );
 *   
 *   NOVA_LOG_TRUNC(Tag) << "Raw message\n";
 *   sink.flush();  // explicitly flush to disk
 *   fclose( file );
 *
 * Usage with formatter:
 *   FILE* file = fopen( "log.txt", "w" );
 *   setvbuf( file, nullptr, _IOFBF, 128 * 1024 );
 *   ISO8601Formatter formatter;
 *   FormattingFileSink<> sink( file, &formatter );
 *   
 *   NOVA_LOG_TRUNC(Tag) << "Formatted message";
 *   sink.flush();  // explicitly flush formatted output
 *   fclose( file );
 *
 * Buffering control:
 *   FILE* file = fopen( "log.txt", "w" );
 *   FormattingFileSink< 256 * 1024 > sink( file, nullptr ); // 256k formatting buffer
 *   // ...
 *   sink.flush();
 *
 * @tparam BufferSize size of formatting buffer
 */
template< std::size_t BufferSize = 256 * 1024 >
class FormattingFileSink final : public kmac::nova::Sink
{
private:
	FILE* _file;                         ///< output file (not owned, must remain valid)
	Formatter* _formatter;               ///< formatter (optional, not owned)
	
	// fixed-size formatting buffer
	char _formatBuffer[ BufferSize ];
	std::size_t _formatOffset;

public:
	/**
	 * @brief Construct sink with FILE* and optional formatter.
	 *
	 * @param file output file (must remain valid during sink lifetime)
	 * @param formatter optional formatter for records (nullptr for raw output)
	 *
	 * @note sink does not own file or formatter (caller manages lifetime)
	 * @note file must be opened for writing
	 * @note NEVER auto-flushes (call flush() explicitly)
	 */
	explicit FormattingFileSink( FILE* file, Formatter* formatter = nullptr ) noexcept;

	NO_COPY_NO_MOVE( FormattingFileSink );

	/**
	 * @brief Process a log record (format and write to file immediately).
	 *
	 * If formatter is set:
	 * - formats record into internal buffer
	 * - immediately writes formatted data to FILE* (calls fwrite)
	 * - DOES NOT flush FILE* stream (no fflush call)
	 *
	 * If no formatter:
	 * - immediately writes raw message bytes to FILE* (calls fwrite)
	 * - DOES NOT flush FILE* stream (no fflush call)
	 *
	 * This means:
	 * - data goes to FILE*'s internal buffer immediately (via fwrite)
	 * - data is NOT yet on disk (use flush() to force disk write via fflush)
	 *
	 * @param record log record to process
	 *
	 * @note writes to FILE* immediately, but does NOT flush to disk
	 * @note call flush() to force disk write (fflush)
	 * @note not thread-safe (use (e.g.) SynchronizedSink wrapper)
	 */
	void process( const kmac::nova::Record& record ) noexcept override;

	/**
	 * @brief Explicitly flush FILE* stream to disk.
	 *
	 * Since process() already writes formatted data via fwrite(),
	 * flush() only needs to flush the FILE* stream to disk via fflush().
	 *
	 * This ensures all buffered data in the FILE* reaches the disk.
	 *
	 * Thread safety: caller must ensure no concurrent process() calls
	 */
	void flush() noexcept;

private:
	/**
	 * @brief Flush internal formatting buffer to file.
	 *
	 * Writes any accumulated formatted data to the FILE*.
	 * Does NOT call fflush() on the file.
	 */
	void flushFormatBuffer() noexcept;
};

template< std::size_t BufferSize >
FormattingFileSink< BufferSize >::FormattingFileSink( FILE* file, Formatter* formatter ) noexcept
	: _file( file )
	, _formatter( formatter )
	, _formatOffset( 0 )
{
}

template< std::size_t BufferSize >
void FormattingFileSink< BufferSize >::process( const kmac::nova::Record& record ) noexcept
{
	if ( ! _file )
	{
		return;
	}

	if ( _formatter )
	{
		// format record using Formatter interface
		_formatter->begin( record );

		// Format in chunks until complete
		bool done = false;
		while ( ! done )
		{
			// create Buffer wrapper for remaining space
			Buffer buf( _formatBuffer + _formatOffset, BufferSize - _formatOffset );

			// format into buffer
			done = _formatter->format( record, buf );

			// update offset
			_formatOffset += buf.size();

			// if buffer is full and not done, flush and retry
			if ( ! done )
			{
				flushFormatBuffer();
				// formatter maintains state, so we don't call begin() again
			}
		}

		// immediately write the formatted record to FILE*
		flushFormatBuffer();
	}
	else
	{
		// no formatter - write raw message bytes immediately
		fwrite( record.message, 1, record.messageSize, _file );
		// NOTE: does NOT call fflush() - caller must do that explicitly
	}
}

template< std::size_t BufferSize >
void FormattingFileSink< BufferSize >::flush() noexcept
{
	if ( ! _file )
	{
		return;
	}

	// since process() already writes formatted data immediately,
	// we only need to flush the FILE* stream to disk
	fflush( _file );
}

template< std::size_t BufferSize >
void FormattingFileSink< BufferSize >::flushFormatBuffer() noexcept
{
	if ( _formatOffset == 0 || ! _file )
	{
		return;
	}

	// write formatted buffer to file
	fwrite( _formatBuffer, 1, _formatOffset, _file );

	// reset buffer
	_formatOffset = 0;

	// NOTE: does NOT call fflush() - only flush() method does that
}

} // namespace kmac::nova::extras

#endif // KMAC_NOVA_EXTRAS_FORMATTING_FILE_SINK_H
