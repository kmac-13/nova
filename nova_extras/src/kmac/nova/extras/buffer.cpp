#include "kmac/nova/extras/buffer.h"

#include <cstring>

namespace kmac::nova::extras
{

Buffer::Buffer( char* buffer, std::size_t capacity ) noexcept
	: _buffer( buffer )
	, _capacity( capacity )
	, _size( 0 )
{
}

const char* Buffer::data() const noexcept
{
	return _buffer;
}

std::size_t Buffer::size() const noexcept
{
	return _size;
}

std::size_t Buffer::remaining() const noexcept
{
	return _capacity - _size;
}

bool Buffer::append( const char* data, std::size_t length ) noexcept
{
	// if nothing to append, always indicate success
	if ( length == 0 )
	{
		return true;
	}

	// make sure there is enough room to append the data
	if ( _size + length > _capacity )
	{
		return false;
	}

	// append the data
	std::memcpy( _buffer + _size, data, length );
	_size += length;
	return true;
}

bool Buffer::appendChar( char chr ) noexcept
{
	if ( _size >= _capacity )
	{
		return false;
	}

	_buffer[ _size ] = chr;
	++_size;
	return true;
}

} // namespace kmac::nova::extras
