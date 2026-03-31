#include "kmac/flare/ram_writer.h"

#include <cstring>

namespace kmac::flare
{

RamWriter::RamWriter( void* buf, std::size_t capacity ) noexcept
	: _buf( static_cast< std::uint8_t* >( buf ) )
	, _capacity( capacity )
{
}

std::size_t RamWriter::bytesWritten() const noexcept
{
	return _offset;
}

bool RamWriter::isFull() const noexcept
{
	return _offset >= _capacity;
}

const std::uint8_t* RamWriter::data() const noexcept
{
	return _buf;
}

std::size_t RamWriter::write( const void* data, std::size_t size ) noexcept
{
	if ( _buf == nullptr || _offset >= _capacity )
	{
		return 0;
	}

	const std::size_t available = _capacity - _offset;
	const std::size_t toWrite = size < available ? size : available;

	std::memcpy( _buf + _offset, data, toWrite );
	_offset += toWrite;

	return toWrite;
}

void RamWriter::flush() noexcept
{
}

void RamWriter::reset() noexcept
{
	_offset = 0;
}

} // namespace kmac::flare
