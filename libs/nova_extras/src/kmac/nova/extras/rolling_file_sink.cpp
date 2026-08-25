#include "kmac/nova/extras/rolling_file_sink.h"

#include "kmac/nova/extras/buffer.h"
#include "kmac/nova/extras/formatter.h"

#include <kmac/nova/record.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

namespace kmac {
namespace nova {
namespace extras {

RollingFileSink::RollingFileSink( const std::string& baseFilename, std::size_t startIndex, std::size_t maxFileSize, Formatter* formatter ) noexcept
	: _baseFilename( baseFilename )
	, _maxFileSize( maxFileSize )
	, _currentIndex( startIndex )
	, _formatter( formatter )
	, _process( _formatter != nullptr ? &RollingFileSink::processFormatted : &RollingFileSink::processRaw )
{
	openCurrentFile();
}

RollingFileSink::~RollingFileSink() noexcept
{
	flush();
	closeCurrentFile();
}

void RollingFileSink::setRolloverCallback( RolloverCallback callback ) noexcept
{
	_rolloverCallback = std::move( callback );
}

void RollingFileSink::process( const kmac::nova::Record& record ) noexcept
{
	if ( _currentFile == nullptr ) /*[[unlikely]]*/
	{
		return;
	}

	// flush write buffer if less than half remains - keeps buffer healthy
	// without triggering rotation; rotation is handled per-path below
	constexpr std::size_t BUFFER_HALF_SIZE = WRITE_BUFFER_SIZE / 2;
	if ( _bufferOffset > BUFFER_HALF_SIZE ) /*[[unlikely]]*/
	{
		flush();
	}

	((*this).*(_process))( record );
}

void RollingFileSink::flush() noexcept
{
	if ( _bufferOffset == 0 )
	{
		return;
	}

	if ( _currentFile == nullptr ) /*[[unlikely]]*/
	{
		return;
	}

	const std::size_t written = std::fwrite( _writeBuffer.data(), 1, _bufferOffset, _currentFile );
	if ( written != _bufferOffset )
	{
		// partial or failed write - data lost, nothing actionable in noexcept context
		_bufferOffset = 0;
		return;
	}

	if ( std::fflush( _currentFile ) != 0 )
	{
		// flush failed - OS buffer may not have been committed to disk
	}

	_currentSize += _bufferOffset;
	_bufferOffset = 0;
}

std::size_t RollingFileSink::currentFileSize() const noexcept
{
	return _bytesWritten;
}

const std::string& RollingFileSink::baseFilename() const noexcept
{
	return _baseFilename;
}

std::size_t RollingFileSink::maxFileSize() const noexcept
{
	return _maxFileSize;
}

std::size_t RollingFileSink::currentIndex() const noexcept
{
	return _currentIndex;
}

std::string RollingFileSink::currentFilename() const noexcept
{
	return makeFilename( _currentIndex );
}

void RollingFileSink::forceRotate() noexcept
{
	flush();
	rotate();
}

void RollingFileSink::processRaw( const kmac::nova::Record& record ) noexcept
{
	// rotate before writing if this record would exceed the file size limit;
	// skip rotation on the very first record (_bytesWritten == 0) so an empty
	// file is never immediately rotated away
	if ( _bytesWritten > 0 && _bytesWritten + record.messageSize >= _maxFileSize ) /*[[unlikely]]*/
	{
		flush();
		rotate();

		if ( _currentFile == nullptr ) /*[[unlikely]]*/
		{
			return;
		}
	}

	if ( _bufferOffset + record.messageSize > WRITE_BUFFER_SIZE ) /*[[unlikely]]*/
	{
		flush();
	}

	std::memcpy(
		_writeBuffer.data() + _bufferOffset,
		record.message,
		record.messageSize
	);

	_bufferOffset += record.messageSize;
	_bytesWritten += record.messageSize;
}

void RollingFileSink::processFormatted( const kmac::nova::Record& record ) noexcept
{
	_formatter->begin( record );

	// rotate before writing if there is insufficient space for the next record;
	// skip rotation on the very first record (_bytesWritten == 0) so an empty
	// file is never immediately rotated away
	constexpr std::size_t ESTIMATED_RECORD_SIZE = 256;
	if ( _bytesWritten > 0 && _bytesWritten + ESTIMATED_RECORD_SIZE >= _maxFileSize ) /*[[unlikely]]*/
	{
		flush();
		rotate();

		if ( _currentFile == nullptr ) /*[[unlikely]]*/
		{
			return;
		}
	}

	while ( true )
	{
		// safety check for buffer capacity
		if ( _bufferOffset == WRITE_BUFFER_SIZE ) /*[[unlikely]]*/
		{
			flush();
		}

		// format the record into the remaining write buffer space
		Buffer buf( _writeBuffer.data() + _bufferOffset, WRITE_BUFFER_SIZE - _bufferOffset );
		const bool done = _formatter->format( record, buf );

		const std::size_t produced = buf.size();
		_bufferOffset += produced;
		_bytesWritten += produced;

		if ( done )
		{
			break;
		}

		// buffer full - flush and let the formatter continue in the next iteration
		flush();
	}
}

std::string RollingFileSink::makeFilename( std::size_t index ) const noexcept
{
	return _baseFilename + "." + std::to_string( index );
}

void RollingFileSink::openCurrentFile() noexcept
{
	const std::string filename = makeFilename( _currentIndex );

	_currentFile = std::fopen( filename.c_str(), "wb" );

	if ( _currentFile == nullptr ) /*[[unlikely]]*/
	{
		return;
	}

	// set full buffering with large buffer for better performance
	if ( std::setvbuf( _currentFile, nullptr, _IOFBF, std::size_t( 128 * 1024 ) ) != 0 )
	{
		// buffer hint rejected - file remains open with default buffering
	}

	_currentSize = 0;
	_bufferOffset = 0;
	_bytesWritten = 0;
}

void RollingFileSink::closeCurrentFile() noexcept
{
	if ( _currentFile != nullptr )
	{
		if ( std::fclose( _currentFile ) != 0 )
		{
			// flush failed - buffered data may have been lost;
			// nothing actionable in a noexcept context
			// TODO: consider using a return value and logging issue in next file after rotate
		}
		_currentFile = nullptr;
	}
}

void RollingFileSink::rotate() noexcept
{
	const std::string oldFilename = makeFilename( _currentIndex );

	closeCurrentFile();

	++_currentIndex;

	openCurrentFile();

	const std::string newFilename = makeFilename( _currentIndex );

	if ( _rolloverCallback != nullptr )
	{
		try
		{
			_rolloverCallback( oldFilename, newFilename );
		}
		catch ( ... )
		{
			// intentionally suppressed: callback exceptions must not propagate through a noexcept boundary
			(void) 0;
		}
	}
}

} // namespace extras
} // namespace nova
} // namespace kmac
