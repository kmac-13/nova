#include "kmac/nova/extras/rolling_file_sink.h"

#include "kmac/nova/record.h"
#include "kmac/nova/extras/buffer.h"
#include "kmac/nova/extras/formatter.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <string>
#include <utility>

namespace
{

// returns false if entryName does not match the pattern "<filename>.<integer>"
bool parseFileSuffix(
	const std::string& entryName,
	const std::string& filename,
	std::size_t& outIndex
) noexcept;

} // namespace

namespace kmac::nova::extras
{

RollingFileSink::RollingFileSink( const std::string& baseFilename, std::size_t maxFileSize, Formatter* formatter ) noexcept
	: _baseFilename( baseFilename )
	, _maxFileSize( maxFileSize )
	, _formatter( formatter )
	, _remaining( maxFileSize )
	, _process( _formatter != nullptr ? &RollingFileSink::processFormatted : &RollingFileSink::processRaw )
{
	initialize();
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
	constexpr std::size_t BUFFER_HALF_SIZE = WRITE_BUFFER_SIZE / 2;

	if ( _currentFile == nullptr ) /*[[unlikely]]*/
	{
		return;
	}

	// ensure space before formatting
	if ( _remaining < BUFFER_HALF_SIZE ) /*[[unlikely]]*/
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

	const std::size_t written = std::fwrite( std::data( _writeBuffer ), 1, _bufferOffset, _currentFile );
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
	return _currentSize + _bufferOffset;
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
	if ( _bufferOffset + record.messageSize > WRITE_BUFFER_SIZE ) /*[[unlikely]]*/
	{
		flush();
	}

	std::memcpy(
		std::data( _writeBuffer ) + _bufferOffset,
		record.message,
		record.messageSize
		);
	_bufferOffset += record.messageSize;
	_remaining -= record.messageSize;
}

void RollingFileSink::processFormatted( const kmac::nova::Record& record ) noexcept
{
	_formatter->begin( record );

	// only rotate if we're actually ready to write something
	constexpr std::size_t ESTIMATED_RECORD_SIZE = 256;
	const std::size_t totalWritten = _currentSize + _bufferOffset;
	if ( totalWritten > 0 && _remaining < ESTIMATED_RECORD_SIZE ) /*[[unlikely]]*/
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

		// format the record into the buffer
		Buffer buf( std::data( _writeBuffer ) + _bufferOffset, WRITE_BUFFER_SIZE - _bufferOffset );
		const bool done = _formatter->format( record, buf );

		// check if what was formatted is larger than the remaining space in the file
		const std::size_t produced = buf.size();
		_bufferOffset += produced;
		_remaining -= produced;

		// formatted successfully, so break out of the loop
		if ( done )
		{
			break;
		}

		// buffer full or formatter needs more space
		flush();
	}
}

void RollingFileSink::initialize() noexcept
{
	const std::size_t highestIndex = findHighestIndex();

	_currentIndex = highestIndex + 1;

	openCurrentFile();
}


std::size_t RollingFileSink::findHighestIndex() const noexcept
{
	std::size_t highestIndex = 0;

	try
	{
		namespace fs = std::filesystem;

		const fs::path basePath( _baseFilename );
		fs::path directory = basePath.parent_path();
		const std::string filename = basePath.filename().string();

		if ( directory.empty() )
		{
			directory = ".";
		}

		if ( ! fs::exists( directory ) || ! fs::is_directory( directory ) )
		{
			return highestIndex;
		}

		for ( const auto& entry : fs::directory_iterator( directory ) )
		{
			if ( ! entry.is_regular_file() )
			{
				continue;
			}

			std::size_t index = 0;
			if ( parseFileSuffix( entry.path().filename().string(), filename, index ) )
			{
				if ( index > highestIndex )
				{
					highestIndex = index;
				}
			}
		}
	}
	catch ( ... )
	{
		// filesystem iteration failed (permissions, deleted directory, etc.),
		// so return whatever highest index was found before the error
		(void) 0;
	}

	return highestIndex;
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
	_remaining = _maxFileSize;
}

void RollingFileSink::closeCurrentFile() noexcept
{
	if ( _currentFile != nullptr )
	{
		if ( std::fclose( _currentFile ) != 0 )
		{
			// flush failed - buffered data may have been lost;
			// nothing actionable in a noexcept context
			// TODO: consider using a return value and logging issue in next file aftter rotate
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

	_remaining = _maxFileSize;

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

} // namespace kmac::nova::extras

namespace
{

bool parseFileSuffix(
	const std::string& entryName,
	const std::string& filename,
	std::size_t& outIndex
) noexcept
{
	if ( entryName.size() <= filename.size() + 1 )
	{
		return false;
	}

	if ( entryName.substr( 0, filename.size() ) != filename )
	{
		return false;
	}

	if ( entryName[ filename.size() ] != '.' )
	{
		return false;
	}

	try
	{
		outIndex = std::stoull( entryName.substr( filename.size() + 1 ) );
		return true;
	}
	catch ( ... )
	{
		return false;
	}
}

} // namespace
