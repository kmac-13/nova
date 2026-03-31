#include "kmac/nova/extras/large_payload_formatter.h"

#include "kmac/nova/record.h"
#include "kmac/nova/sink.h"
#include "kmac/nova/extras/truncating_buffer.h"

#include <cstring>
#include <array>

namespace kmac::nova::extras
{

LargePayloadFormatter::LargePayloadFormatter(
	const char* payload,
	std::size_t payloadSize
) noexcept
	: _payload( payload )
	, _payloadSize( payloadSize )
{
}

std::size_t LargePayloadFormatter::maxChunkSize() const noexcept
{
	return 1024; // intentionally modest
}

void LargePayloadFormatter::formatAndWrite(
	const kmac::nova::Record& record,
	kmac::nova::Sink& downstream
) noexcept
{
	// use std::array instead of VLA
	constexpr std::size_t CHUNK_SIZE = 1024;
	std::array< char, CHUNK_SIZE + 1 > bufferStorage{};         // +1 for null terminator
	TruncatingBuffer buff( bufferStorage.data(), CHUNK_SIZE );  // reserve last byte for null

	// first chunk: header
	// handle [[nodiscard]] return values
	(void) buff.append( record.tag, std::strlen( record.tag ) );
	(void) buff.appendChar( ' ' );
	(void) buff.append( "BEGIN_PAYLOAD\n", 14 );

	// null-terminate
	bufferStorage.data()[ buff.size() ] = '\0';

	// create a temporary record with the header message
	kmac::nova::Record headerRecord = record;
	headerRecord.messageSize = static_cast< std::uint32_t >( buff.size() );
	headerRecord.message = buff.data();

	downstream.process( headerRecord );

	// payload chunks
	std::size_t offset = 0;
	while ( offset < _payloadSize )
	{
		buff = TruncatingBuffer( bufferStorage.data(), CHUNK_SIZE );

		const std::size_t remaining = _payloadSize - offset;
		const std::size_t chunk =
			remaining < CHUNK_SIZE
				? remaining
				: CHUNK_SIZE;

		(void) buff.append( _payload + offset, chunk );
		offset += chunk;

		// null-terminate
		bufferStorage.data()[ buff.size() ] = '\0';

		// create record with chunk data
		kmac::nova::Record chunkRecord = record;
		chunkRecord.messageSize = static_cast< std::uint32_t >( buff.size() );
		chunkRecord.message = buff.data();

		downstream.process( chunkRecord );
	}

	// final chunk: footer
	buff = TruncatingBuffer( bufferStorage.data(), CHUNK_SIZE );
	(void) buff.append( "\nEND_PAYLOAD", 12 );

	// null-terminate
	bufferStorage.data()[ buff.size() ] = '\0';

	kmac::nova::Record footerRecord = record;
	footerRecord.messageSize = static_cast< std::uint32_t >( buff.size() );
	footerRecord.message = buff.data();

	downstream.process( footerRecord );
}

} // namespace kmac::nova::extras
