#include "kmac/nova/extras/circular_file_sink.h"

#include "kmac/nova/record.h"
#include "kmac/nova/extras/buffer.h"
#include "kmac/nova/extras/formatter.h"

#include <cstring>

namespace kmac::nova::extras
{

CircularFileSink::CircularFileSink( const std::string& filename, std::size_t maxFileSize, Formatter* formatter ) noexcept
	: _filename( filename )
	, _maxFileSize( maxFileSize )
	, _formatter( formatter )
	, _process( _formatter != nullptr ? &CircularFileSink::processFormatted : &CircularFileSink::processRaw )
{
	// open file for writing (create or truncate)
	_file = std::fopen( _filename.c_str(), "wb" );

	if ( _file == nullptr ) /*[[unlikely]]*/
	{
		return;
	}

	// set full buffering with large buffer for better performance
	if ( std::setvbuf( _file, nullptr, _IOFBF, 128UL * 1024UL ) != 0 )
	{
		// buffer hint rejected,q file remains open with default buffering
	}
}

CircularFileSink::~CircularFileSink() noexcept
{
	flush();

	if ( _file != nullptr )
	{
		if ( std::fclose( _file ) != 0 )
		{
			// flush failed, buffered data may have been lost;
			// nothing actionable in destructor
		}
		_file = nullptr;
	}
}

void CircularFileSink::process( const kmac::nova::Record& record ) noexcept
{
	constexpr std::size_t BUFFER_HALF_SIZE = WRITE_BUFFER_SIZE / 2;

	if ( _file == nullptr ) /*[[unlikely]]*/
	{
		return;
	}

	// ensure space in buffer before formatting
	if ( _bufferOffset + BUFFER_HALF_SIZE > WRITE_BUFFER_SIZE ) /*[[unlikely]]*/
	{
		flush();
	}

	((*this).*(_process))( record );
}

void CircularFileSink::flush() noexcept
{
	if ( _bufferOffset == 0 )
	{
		return;
	}

	if ( _file == nullptr ) /*[[unlikely]]*/
	{
		return;
	}

	// handle edge case where maxFileSize is 0 and nothing can be written
	if ( _maxFileSize == 0 ) /*[[unlikely]]*/
	{
		_bufferOffset = 0;
		return;
	}

	std::size_t remaining = _bufferOffset;
	std::size_t offset = 0;

	while ( remaining > 0 )
	{
		// if we're at max size, wrap before writing
		if ( _currentSize >= _maxFileSize )
		{
			if ( std::fflush( _file ) == 0 )
			{
				// flush failed, OS buffer may not have been committed to disk
			}
			wrap();
		}

		// how much space left in current file position
		const std::size_t spaceLeft = _maxFileSize - _currentSize;

		// write as much as fits
		const std::size_t toWrite = ( remaining <= spaceLeft ) ? remaining : spaceLeft;
		const std::size_t written = std::fwrite( _writeBuffer.data() + offset, 1, toWrite, _file );
		if ( written != toWrite )
		{
			// partial or failed write, data lost, nothing actionable in noexcept context
		}

		_currentSize += toWrite;
		_totalWritten += toWrite;
		remaining -= toWrite;
		offset += toWrite;
	}

	if ( std::fflush( _file ) == 0 )
	{
		// flush failed, OS buffer may not have been committed to disk
	}
	_bufferOffset = 0;
}

std::size_t CircularFileSink::currentPosition() const noexcept
{
	return _currentSize;
}

std::size_t CircularFileSink::maxFileSize() const noexcept
{
	return _maxFileSize;
}

const std::string& CircularFileSink::filename() const noexcept
{
	return _filename;
}

bool CircularFileSink::hasWrapped() const noexcept
{
	return _hasWrapped;
}

std::size_t CircularFileSink::totalWritten() const noexcept
{
	return _totalWritten;
}

void CircularFileSink::processRaw( const kmac::nova::Record& record ) noexcept
{
	write( record.message, record.messageSize );
}

void CircularFileSink::processFormatted( const kmac::nova::Record& record ) noexcept
{
	_formatter->begin( record );

	while ( true )
	{
		// safety check for buffer capacity
		if ( _bufferOffset >= WRITE_BUFFER_SIZE ) /*[[unlikely]]*/
		{
			flush();
		}

		// format the record into the buffer
		Buffer buf( _writeBuffer.data() + _bufferOffset, WRITE_BUFFER_SIZE - _bufferOffset );
		const bool done = _formatter->format( record, buf );

		// update buffer offset
		const std::size_t produced = buf.size();
		_bufferOffset += produced;

		// formatted successfully, done
		if ( done )
		{
			break;
		}

		// buffer full or formatter needs more space
		flush();
	}
}

void CircularFileSink::wrap() noexcept
{
	if ( _file == nullptr ) /*[[unlikely]]*/
	{
		return;
	}

	// seek to beginning of file
	if ( std::fseek( _file, 0, SEEK_SET ) != 0 )
	{
		// seek failed, circular wrap position lost, subsequent writes may be incorrect
	}

	// reset position
	_currentSize = 0;
	_hasWrapped = true;
}

void CircularFileSink::write( const char* data, std::size_t size ) noexcept
{
	if ( _file == nullptr || data == nullptr || size == 0 ) /*[[unlikely]]*/
	{
		return;
	}

	// check if data fits in buffer
	if ( _bufferOffset + size <= WRITE_BUFFER_SIZE )
	{
		// copy to buffer
		std::memcpy( _writeBuffer.data() + _bufferOffset, data, size );
		_bufferOffset += size;
	}
	else
	{
		// data larger than remaining buffer space
		// flush buffer first, then handle large data
		flush();

		if ( size >= WRITE_BUFFER_SIZE )
		{
			// data larger than entire buffer - write directly
			// this bypasses buffering for very large writes

			// check if we need to wrap
			if ( _currentSize + size > _maxFileSize )
			{
				const std::size_t beforeWrap = _maxFileSize - _currentSize;

				if ( beforeWrap > 0 )
				{
					std::fwrite( data, 1, beforeWrap, _file );
					_totalWritten += beforeWrap;
				}

				wrap();

				const std::size_t afterWrap = size - beforeWrap;
				if ( afterWrap > 0 )
				{
					std::fwrite( data + beforeWrap, 1, afterWrap, _file );
					_currentSize = afterWrap;
					_totalWritten += afterWrap;
				}
			}
			else
			{
				std::fwrite( data, 1, size, _file );
				_currentSize += size;
				_totalWritten += size;
			}
		}
		else
		{
			// data fits in buffer now that it's empty
			std::memcpy( _writeBuffer.data(), data, size );
			_bufferOffset = size;
		}
	}
}

} // namespace kmac::nova::extras
