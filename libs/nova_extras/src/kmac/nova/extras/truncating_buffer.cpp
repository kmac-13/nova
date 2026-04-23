#include "kmac/nova/extras/truncating_buffer.h"

#include <cstring>

namespace kmac {
namespace nova {
namespace extras {

TruncatingBuffer::TruncatingBuffer( char* buffer, std::size_t capacity ) noexcept
	: _buffer( buffer )
	, _capacity( capacity )
{
}

const char* TruncatingBuffer::data() const noexcept
{
	return _buffer;
}

std::size_t TruncatingBuffer::size() const noexcept
{
	return _size;
}

std::size_t TruncatingBuffer::remaining() const noexcept
{
	return _capacity - _size;
}

bool TruncatingBuffer::truncated() const noexcept
{
	return _truncated;
}

bool TruncatingBuffer::append( const char* data, std::size_t length ) noexcept
{
	// if nothing to append, always indicate success
	if ( length == 0 )
	{
		return false;
	}

	if ( _size + length > _capacity )
	{
		_truncated = true;
		length = _capacity - _size;
	}

	std::memcpy( _buffer + _size, data, length );
	_size += length;
	return true;
}

bool TruncatingBuffer::appendChar( char chr ) noexcept
{
	if ( _size >= _capacity )
	{
		_truncated = true;
		return false;
	}

	_buffer[ _size ] = chr;
	++_size;
	return true;
}

} // namespace extras
} // namespace nova
} // namespace kmac
