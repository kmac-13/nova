#include "kmac/flare/scanner.h"

#include "kmac/flare/tlv.h"

#include <cstring>

namespace kmac::flare
{

Scanner::Scanner()
{
	reset();
}

std::size_t Scanner::recordOffset() const
{
	return _offset;
}

std::size_t Scanner::recordSize() const
{
	return _expectedSize;
}

void Scanner::reset()
{
	_state = State::SeekingMagic;
	_offset = 0;
	_expectedSize = 0;
}

void Scanner::setStartOffset( std::size_t offset )
{
	_offset = offset;
}

bool Scanner::scan( const std::uint8_t* data, std::size_t size )
{
	// start searching from after the last found record
	for ( std::size_t i = _offset; ( i + sizeof( std::uint64_t ) ) <= size; ++i )
	{
		std::uint64_t magic;
		std::memcpy( &magic, data + i, sizeof( magic ) );

		if ( magic == FLARE_MAGIC )
		{
			if ( ( i + sizeof( magic ) + sizeof( std::uint32_t ) ) > size )
			{
				return false;
			}

			std::memcpy( &_expectedSize, data + i + sizeof( magic ), sizeof( std::uint32_t ) );

			if ( _expectedSize == 0 || _expectedSize > MAX_RECORD_SIZE )
			{
				continue;
			}

			if ( ( i + _expectedSize ) <= size )
			{
				// END marker is at the end of the record: Type (2 bytes) + Length (2 bytes)
				const std::size_t endMarkerOffset = i + _expectedSize - sizeof( std::uint16_t ) * 2;

				// make sure we can read the END marker
				if ( ( endMarkerOffset + sizeof( std::uint16_t ) ) <= size )
				{
					std::uint16_t endMarkerType;
					std::memcpy( &endMarkerType, data + endMarkerOffset, sizeof( endMarkerType ) );

					// verify it's the RecordEnd marker
					if ( endMarkerType == std::uint16_t( TlvType::RecordEnd ) )
					{
						_offset = i;
						return true;
					}
					// else: False positive (random data matching magic), keep searching
				}
			}
		}
	}

	return false;
}

} // namespace kmac::flare
