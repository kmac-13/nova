#include "kmac/flare/reader.h"

#include "kmac/flare/tlv.h"

#include <cstring>

namespace kmac::flare
{

bool Reader::parseNext( const uint8_t* data, size_t size, Record& outRecord )
{
	if ( ! _scanner.scan( data, size ) )
	{
		return false;
	}

	bool success = parseRecord(
		data + _scanner.recordOffset(),
		_scanner.recordSize(),
		outRecord
	);
	
	if ( success )
	{
		// advance scanner past this record for next call
		std::size_t nextOffset = _scanner.recordOffset() + _scanner.recordSize();
		_scanner.setStartOffset( nextOffset );
	}
	
	return success;
}

bool Reader::parseRecord( const uint8_t* data, size_t size, Record& outRecord )
{
	// clear the output record
	outRecord.clear();
	
	size_t offset = sizeof( uint64_t ) + sizeof( uint32_t );

	while ( offset + sizeof( uint16_t ) * 2 <= size )
	{
		uint16_t type;
		uint16_t length;

		std::memcpy( &type, data + offset, sizeof( type ) );
		offset += sizeof( type );

		std::memcpy( &length, data + offset, sizeof( length ) );
		offset += sizeof( length );

		if ( ( offset + length ) > size )
		{
			return false;
		}

		const uint8_t* value = data + offset;

		switch ( TlvType( type ) )
		{
			case TlvType::RecordStatus:
				if ( length == sizeof( uint8_t ) )
				{
					std::memcpy( &outRecord.status, value, sizeof( uint8_t ) );
				}
				break;

			case TlvType::SequenceNumber:
				if ( length == sizeof( uint64_t ) )
				{
					std::memcpy( &outRecord.sequenceNumber, value, sizeof( uint64_t ) );
				}
				break;

			case TlvType::TimestampNs:
				if ( length == sizeof( uint64_t ) )
				{
					std::memcpy( &outRecord.timestampNs, value, sizeof( uint64_t ) );
				}
				break;

			case TlvType::TagId:
				if ( length == sizeof( uint64_t ) )
				{
					std::memcpy( &outRecord.tagId, value, sizeof( uint64_t ) );
				}
				break;

			case TlvType::LineNumber:
				if ( length == sizeof( uint32_t ) )
				{
					std::memcpy( &outRecord.line, value, sizeof( uint32_t ) );
				}
				break;
			
			case TlvType::ProcessId:
				if ( length == sizeof( uint32_t ) )
				{
					std::memcpy( &outRecord.processId, value, sizeof( uint32_t ) );
				}
				break;
			
			case TlvType::ThreadId:
				if ( length == sizeof( uint32_t ) )
				{
					std::memcpy( &outRecord.threadId, value, sizeof( uint32_t ) );
				}
				break;
			
			case TlvType::FileName:
			{
				std::size_t copyLen = (length < Record::MAX_FILENAME_LEN - 1) 
					? length 
					: (Record::MAX_FILENAME_LEN - 1);
				std::memcpy( outRecord.file, value, copyLen );
				outRecord.file[copyLen] = '\0';
				outRecord.fileLen = copyLen;
				break;
			}
			
			case TlvType::FunctionName:
			{
				std::size_t copyLen = (length < Record::MAX_FUNCTION_LEN - 1)
					? length
					: (Record::MAX_FUNCTION_LEN - 1);
				std::memcpy( outRecord.function, value, copyLen );
				outRecord.function[copyLen] = '\0';
				outRecord.functionLen = copyLen;
				break;
			}

			case TlvType::MessageBytes:
			{
				std::size_t copyLen = (length < Record::MAX_MESSAGE_LEN - 1)
					? length
					: (Record::MAX_MESSAGE_LEN - 1);
				std::memcpy( outRecord.message, value, copyLen );
				outRecord.message[copyLen] = '\0';
				outRecord.messageLen = copyLen;
				break;
			}
			
			case TlvType::MessageTruncated:
				if ( length == sizeof( uint8_t ) )
				{
					uint8_t flag;
					std::memcpy( &flag, value, sizeof( uint8_t ) );
					outRecord.messageTruncated = (flag != 0);
				}
				break;

			case TlvType::RecordEnd:
				return true;

			default:
				// Unknown TLV type - skip it
				break;
		}

		offset += length;
	}

	return false;
}

} // namespace kmac::flare
