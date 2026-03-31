#include "kmac/flare/scanner.h"

#include "kmac/flare/tlv.h"

#include <cstring>

namespace
{

// returns true if the candidate record starting at data[offset] is complete
// and well-formed: the record fits within size, and the final TLV field is
// a valid RecordEnd marker
bool validateCandidate(
	const std::uint8_t* data,
	std::size_t offset,
	std::size_t size,
	std::size_t expectedSize
) noexcept;

} // namespace

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
		std::uint64_t magic = 0;
		std::memcpy( &magic, data + i, sizeof( magic ) );

		// use continue to avoid nesting
		if ( magic != FLARE_MAGIC )
		{
			continue;
		}

		if ( ( i + sizeof( magic ) + sizeof( std::uint32_t ) ) > size )
		{
			return false;
		}

		std::memcpy( &_expectedSize, data + i + sizeof( magic ), sizeof( std::uint32_t ) );

		// use continue to avoid nesting
		if ( _expectedSize == 0 || _expectedSize > MAX_RECORD_SIZE )
		{
			continue;
		}

		if ( validateCandidate( data, i, size, _expectedSize ) )
		{
			_offset = i;
			return true;
		}
	}

	return false;
}

} // namespace kmac::flare

namespace
{

bool validateCandidate(
	const std::uint8_t* data,
	std::size_t offset,
	std::size_t size,
	std::size_t expectedSize
) noexcept
{
	if ( ( offset + expectedSize ) > size )
	{
		return false;
	}

	// END marker is at the end of the record: Type (2 bytes) + Length (2 bytes)
	const std::size_t endMarkerOffset = offset + expectedSize - sizeof( std::uint16_t ) * 2;

	// make sure we can read the END marker
	if ( ( endMarkerOffset + sizeof( std::uint16_t ) ) > size )
	{
		return false;
	}

	std::uint16_t endMarkerType = 0;
	std::memcpy( &endMarkerType, data + endMarkerOffset, sizeof( endMarkerType ) );

	// verify it's the RecordEnd marker
	return endMarkerType == std::uint16_t( kmac::flare::TlvType::RecordEnd );
}

} // namespace
