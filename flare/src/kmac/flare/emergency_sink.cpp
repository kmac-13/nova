#include "kmac/flare/emergency_sink.h"

#include "kmac/flare/tlv.h"

#include <kmac/nova/details.h>
#include <kmac/nova/record.h>

#include <array>
#include <cstring>

// Platform-specific includes for process/thread IDs
#if defined( __linux__ ) || defined( __unix__ ) || defined( __APPLE__ ) || defined( __FreeBSD__ )
#include <unistd.h>  // getpid()
#endif

#ifdef __linux__
#include <sys/syscall.h>  // SYS_gettid
#endif

#ifdef _WIN32
#include <windows.h>  // GetCurrentProcessId()
#endif

namespace kmac::flare
{

EmergencySink::EmergencySink( IWriter* writer, bool captureProcessInfo ) noexcept
	: _writer( writer )
	, _sequenceNumber( 0 )
	, _captureProcessInfo( captureProcessInfo )
{
}

void EmergencySink::process( const kmac::nova::Record& record ) noexcept
{
	if ( ! _writer )
	{
		return;
	}

	// encode to stack buffer
	std::array< char, ENCODING_BUFFER_SIZE > buffer;
	std::size_t encodedSize = encodeRecordTlv( record, buffer.data(), buffer.size() );

	if ( encodedSize > 0 )
	{
		// single atomic write
		_writer->write( buffer.data(), encodedSize );

		// flush for crash safety
		_writer->flush();

		// increment sequence number for next record
		++_sequenceNumber;
	}
}

void EmergencySink::flush() noexcept
{
	if ( _writer )
	{
		_writer->flush();
	}
}

std::size_t EmergencySink::encodeRecordTlv( const kmac::nova::Record& record, char* buffer, std::size_t bufferSize ) noexcept
{
	std::size_t offset = 0;

	// helper lambda to write TLV
	auto writeTlv = [ & ]( TlvType type, const void* data, std::uint16_t len ) -> bool
	{
		const std::size_t needed = sizeof( std::uint16_t ) * 2 + len;
		if ( ( offset + needed ) > bufferSize )
		{
			return false; // not enough space
		}

		std::uint16_t typeVal = std::uint16_t( type );
		std::memcpy( buffer + offset, &typeVal, sizeof( typeVal ) );
		offset += sizeof( typeVal );

		std::memcpy( buffer + offset, &len, sizeof( len ) );
		offset += sizeof(len);

		std::memcpy( buffer + offset, data, len );
		offset += len;

		return true;
	};

	// write magic number
	if ( offset + sizeof( FLARE_MAGIC ) > bufferSize )
	{
		return 0;
	}
	std::memcpy( buffer + offset, &FLARE_MAGIC, sizeof( FLARE_MAGIC ) );
	offset += sizeof( FLARE_MAGIC );

	// reserve space for total size (we'll fill it in later)
	std::size_t sizeOffset = offset;
	if ( ( offset + sizeof( std::uint32_t ) ) > bufferSize )
	{
		return 0;
	}
	offset += sizeof( std::uint32_t );

	// write the status as InProgress initially
	// (if we crash before updating it, reader knows it's a torn write)
	std::uint8_t status = std::uint8_t( RecordStatus::InProgress );
	if ( ! writeTlv( TlvType::RecordStatus, &status, sizeof( status ) ) )
	{
		return 0;
	}
	std::size_t statusOffset = offset - sizeof( status );  // remember where status is

	// write the sequence number
	if ( ! writeTlv( TlvType::SequenceNumber, &_sequenceNumber, sizeof( _sequenceNumber ) ) )
	{
		return 0;
	}

	// write the timestamp
	if ( ! writeTlv( TlvType::TimestampNs, &record.timestamp, sizeof( record.timestamp ) ) )
	{
		return 0;
	}

	// write the tag as hash (for compact storage)
	std::uint64_t tagHash = hashString( record.tag );
	if ( ! writeTlv( TlvType::TagId, &tagHash, sizeof( tagHash ) ) )
	{
		return 0;
	}

	// write the file name (if not too long)
	if ( record.file )
	{
		std::size_t fileLen = std::strlen( record.file );
		if ( fileLen > 0 && fileLen <= UINT16_MAX )
		{
			// try to write, but don't fail if it doesn't fit
			writeTlv( TlvType::FileName, record.file, std::uint16_t( fileLen ) );
		}
	}

	// write the line number
	if ( ! writeTlv( TlvType::LineNumber, &record.line, sizeof( record.line ) ) )
	{
		return 0;
	}

	// write the function name (if not too long)
	if ( record.function )
	{
		std::size_t funcLen = std::strlen( record.function );
		if ( funcLen > 0 && funcLen <= UINT16_MAX )
		{
			writeTlv( TlvType::FunctionName, record.function, std::uint16_t( funcLen ) );
		}
	}

	// write the process/thread info if enabled
	if ( _captureProcessInfo )
	{
		// Process ID - available on most platforms
#if defined( _WIN32 )
		std::uint32_t pid = std::uint32_t( GetCurrentProcessId() );
		writeTlv( TlvType::ProcessId, &pid, sizeof( pid ) );
#elif defined( __linux__ ) || defined( __unix__ ) || defined( __APPLE__ ) || defined( __FreeBSD__ )
		std::uint32_t pid = std::uint32_t( getpid() );
		writeTlv( TlvType::ProcessId, &pid, sizeof( pid ) );
#endif

// thread ID - Linux only for async-signal-safety
#ifdef __linux__
		std::uint32_t tid = std::uint32_t( syscall( SYS_gettid ) );
		writeTlv( TlvType::ThreadId, &tid, sizeof( tid ) );
#endif
	}

	// write the message bytes (using messageSize - no strlen needed!)
	bool messageTruncated = false;
	if ( record.message && record.messageSize > 0 )
	{
		if ( record.messageSize <= UINT16_MAX )
		{
			if ( ! writeTlv( TlvType::MessageBytes, record.message, std::uint16_t( record.messageSize ) ) )
			{
				// couldn't fit full message - try to fit what we can
				std::size_t remaining = bufferSize - offset - ( sizeof( std::uint16_t ) * 2 + sizeof( std::uint16_t ) * 2 );  // reserve space for end marker
				if ( remaining > 0 && remaining <= UINT16_MAX )
				{
					writeTlv( TlvType::MessageBytes, record.message, std::uint16_t( remaining ) );
					messageTruncated = true;
				}
			}
		}
		else
		{
			// message too long - truncate to max uint16
			writeTlv( TlvType::MessageBytes, record.message, UINT16_MAX );
			messageTruncated = true;
		}
	}

	// if message was truncated, add truncation marker
	if ( messageTruncated )
	{
		std::uint8_t truncFlag = 1;
		writeTlv( TlvType::MessageTruncated, &truncFlag, sizeof( truncFlag ) );
	}

	// write end marker
	std::uint16_t endType = std::uint16_t( TlvType::RecordEnd );
	std::uint16_t zero = 0;
	if ( ( offset + sizeof( endType ) + sizeof( zero ) ) > bufferSize )
	{
		return 0;
	}

	std::memcpy( buffer + offset, &endType, sizeof( endType ) );
	offset += sizeof( endType );
	std::memcpy( buffer + offset, &zero, sizeof( zero ) );
	offset += sizeof( zero );

	// update the status to Complete (we made it to the end!)
	status = messageTruncated
		? std::uint8_t( RecordStatus::Truncated )
		: std::uint8_t( RecordStatus::Complete );
	std::memcpy( buffer + statusOffset, &status, sizeof( status ) );

	// write the total size
	std::uint32_t totalSize = std::uint32_t( offset );
	std::memcpy( buffer + sizeOffset, &totalSize, sizeof( totalSize ) );

	return offset;
}

std::uint64_t EmergencySink::hashString( const char* str ) noexcept
{
	return ::kmac::nova::details::fnv1a( str );
}

} // namespace kmac::flare
