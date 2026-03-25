#include "kmac/flare/reader.h"

#include "kmac/flare/record.h"
#include "kmac/flare/tlv.h"

#include <cstdint>
#include <cstring>

namespace
{

struct TlvFieldParseHelper
{
	kmac::flare::Record& record;

	void parseFixedField( kmac::flare::TlvType type, const std::uint8_t* value, std::uint16_t length ) noexcept;

	void parseStringField( kmac::flare::TlvType type, const std::uint8_t* value, std::uint16_t length ) noexcept;

	void parseStackFramesField( const std::uint8_t* value, std::uint16_t length ) noexcept;
};

} // namespace

namespace kmac::flare
{

bool Reader::parseNext( const std::uint8_t* data, std::size_t size, Record& outRecord )
{
	if ( ! _scanner.scan( data, size ) )
	{
		return false;
	}

	const bool success = parseRecord(
		data + _scanner.recordOffset(),
		_scanner.recordSize(),
		outRecord
	);

	if ( success )
	{
		// advance scanner past this record for next call
		const std::size_t nextOffset = _scanner.recordOffset() + _scanner.recordSize();
		_scanner.setStartOffset( nextOffset );
	}

	return success;
}

bool Reader::parseRecord( const std::uint8_t* data, std::size_t size, Record& outRecord )
{
	// clear the output record
	outRecord.clear();

	::TlvFieldParseHelper helper{ outRecord };
	std::size_t offset = sizeof( std::uint64_t ) + sizeof( std::uint32_t );

	while ( offset + sizeof( std::uint16_t ) * 2 <= size )
	{
		std::uint16_t type = 0;
		std::uint16_t length = 0;

		std::memcpy( &type, data + offset, sizeof( type ) );
		offset += sizeof( type );

		std::memcpy( &length, data + offset, sizeof( length ) );
		offset += sizeof( length );

		if ( ( offset + length ) > size )
		{
			return false;
		}

		const std::uint8_t* value = data + offset;
		const auto tlvType = TlvType( type );

		if ( tlvType == TlvType::RecordEnd )
		{
			return true;
		}

		if ( tlvType == TlvType::FileName
			|| tlvType == TlvType::FunctionName
			|| tlvType == TlvType::MessageBytes )
		{
			helper.parseStringField( tlvType, value, length );
		}
		else if ( tlvType == TlvType::StackFrames )
		{
			helper.parseStackFramesField( value, length );
		}
		else
		{
			helper.parseFixedField( tlvType, value, length );
		}

		offset += length;
	}

	return false;
}

} // namespace kmac::flare

namespace
{

void TlvFieldParseHelper::parseFixedField( kmac::flare::TlvType type, const std::uint8_t* value, std::uint16_t length ) noexcept
{
	switch ( type )
	{
	case kmac::flare::TlvType::RecordStatus:
		if ( length == sizeof( std::uint8_t ) )
		{
			std::memcpy( &record.status, value, sizeof( std::uint8_t ) );
		}
		break;

	case kmac::flare::TlvType::SequenceNumber:
		if ( length == sizeof( std::uint64_t ) )
		{
			std::memcpy( &record.sequenceNumber, value, sizeof( std::uint64_t ) );
		}
		break;

	case kmac::flare::TlvType::TimestampNs:
		if ( length == sizeof( std::uint64_t ) )
		{
			std::memcpy( &record.timestampNs, value, sizeof( std::uint64_t ) );
		}
		break;

	case kmac::flare::TlvType::TagId:
		if ( length == sizeof( std::uint64_t ) )
		{
			std::memcpy( &record.tagId, value, sizeof( std::uint64_t ) );
		}
		break;

	case kmac::flare::TlvType::LineNumber:
		if ( length == sizeof( std::uint32_t ) )
		{
			std::memcpy( &record.line, value, sizeof( std::uint32_t ) );
		}
		break;

	case kmac::flare::TlvType::ProcessId:
		if ( length == sizeof( std::uint32_t ) )
		{
			std::memcpy( &record.processId, value, sizeof( std::uint32_t ) );
		}
		break;

	case kmac::flare::TlvType::ThreadId:
		if ( length == sizeof( std::uint32_t ) )
		{
			std::memcpy( &record.threadId, value, sizeof( std::uint32_t ) );
		}
		break;

	case kmac::flare::TlvType::MessageTruncated:
		if ( length == sizeof( std::uint8_t ) )
		{
			std::uint8_t flag = 0;
			std::memcpy( &flag, value, sizeof( std::uint8_t ) );
			record.messageTruncated = ( flag != 0 );
		}
		break;

	case kmac::flare::TlvType::LoadBaseAddress:
		if ( length == sizeof( std::uint64_t ) )
		{
			std::memcpy( &record.loadBaseAddress, value, sizeof( std::uint64_t ) );
		}
		break;

	default:
		break;
	}
}

void TlvFieldParseHelper::parseStackFramesField( const std::uint8_t* value, std::uint16_t length ) noexcept
{
	// payload is a packed array of uint64_t addresses, little-endian;
	// silently ignore malformed lengths (not a multiple of 8)
	if ( length == 0 || ( length % sizeof( std::uint64_t ) ) != 0 )
	{
		return;
	}

	const std::size_t frameCount = length / sizeof( std::uint64_t );
	const std::size_t copyCount = frameCount < kmac::flare::Record::MAX_STACK_FRAMES
		? frameCount
		: kmac::flare::Record::MAX_STACK_FRAMES;

	for ( std::size_t i = 0; i < copyCount; ++i )
	{
		std::memcpy( &record.stackFrames.data()[ i ], value + i * sizeof( std::uint64_t ), sizeof( std::uint64_t ) );
	}
	record.stackFrameCount = copyCount;
}

void TlvFieldParseHelper::parseStringField( kmac::flare::TlvType type, const std::uint8_t* value, std::uint16_t length ) noexcept
{
	switch ( type )
	{
	case kmac::flare::TlvType::FileName:
	{
		const std::size_t copyLen = ( length < kmac::flare::Record::MAX_FILENAME_LEN - 1 )
			? length
			: ( kmac::flare::Record::MAX_FILENAME_LEN - 1 );
		std::memcpy( record.file.data(), value, copyLen );
		record.file.data()[ copyLen ] = '\0';
		record.fileLen = copyLen;
		break;
	}

	case kmac::flare::TlvType::FunctionName:
	{
		const std::size_t copyLen = ( length < kmac::flare::Record::MAX_FUNCTION_LEN - 1 )
			? length
			: ( kmac::flare::Record::MAX_FUNCTION_LEN - 1 );
		std::memcpy( record.function.data(), value, copyLen );
		record.function.data()[ copyLen ] = '\0';
		record.functionLen = copyLen;
		break;
	}

	case kmac::flare::TlvType::MessageBytes:
	{
		const std::size_t copyLen = ( length < kmac::flare::Record::MAX_MESSAGE_LEN - 1 )
			? length
			: ( kmac::flare::Record::MAX_MESSAGE_LEN - 1 );
		std::memcpy( record.message.data(), value, copyLen );
		record.message.data()[ copyLen ] = '\0';
		record.messageLen = copyLen;
		break;
	}

	default:
		break;
	}
}

} // namespace
