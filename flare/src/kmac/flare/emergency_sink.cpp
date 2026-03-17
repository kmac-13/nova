#include "kmac/flare/emergency_sink.h"

#include "kmac/flare/tlv.h"

#include <kmac/nova/details.h>
#include <kmac/nova/record.h>

#include <array>
#include <cstring>
#include <stdint.h>

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

namespace
{

// helper class used to address clang-tidy cognitive complexity
// issues by breaking up the encoding into smaller pieces
struct TlvWriteHelper
{
	char* buffer;
	std::size_t& offset;
	const std::size_t bufferSize;

	bool writeTlv( kmac::flare::TlvType type, const void* data, std::uint16_t len ) noexcept;

	void writeStringTlv( kmac::flare::TlvType type, const char* str ) noexcept;

	void writeProcessInfoTlv() noexcept;

	// returns true if message was truncated
	bool writeMessageTlv( const kmac::nova::Record& record ) noexcept;
};

} // anonymous namespace

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
	if ( _writer == nullptr )
	{
		return;
	}

	// encode to stack buffer
	std::array< char, ENCODING_BUFFER_SIZE > buffer = { };
	const std::size_t encodedSize = encodeRecordTlv( record, buffer.data(), buffer.size() );

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
	if ( _writer != nullptr )
	{
		_writer->flush();
	}
}

std::size_t EmergencySink::encodeRecordTlv( const kmac::nova::Record& record, char* buffer, std::size_t bufferSize ) noexcept
{
	std::size_t offset = 0;
	::TlvWriteHelper writeHelper{ buffer, offset, bufferSize };

	// write magic number
	if ( offset + sizeof( FLARE_MAGIC ) > bufferSize )
	{
		return 0;
	}
	std::memcpy( buffer + offset, &FLARE_MAGIC, sizeof( FLARE_MAGIC ) );
	offset += sizeof( FLARE_MAGIC );

	// reserve space for total size (filled in at the end)
	const std::size_t sizeOffset = offset;
	if ( ( offset + sizeof( std::uint32_t ) ) > bufferSize )
	{
		return 0;
	}
	offset += sizeof( std::uint32_t );

	// write the status as InProgress initially
	// (if we crash before updating it, reader knows it's a torn write)
	std::uint8_t status = std::uint8_t( RecordStatus::InProgress );
	if ( ! writeHelper.writeTlv( TlvType::RecordStatus, &status, sizeof( status ) ) )
	{
		return 0;
	}
	const std::size_t statusOffset = offset - sizeof( status );  // remember where status is

	// write mandatory fields, starting with the sequence number
	if ( ! writeHelper.writeTlv( TlvType::SequenceNumber, &_sequenceNumber, sizeof( _sequenceNumber ) ) )
	{
		return 0;
	}

	// write the timestamp
	if ( ! writeHelper.writeTlv( TlvType::TimestampNs, &record.timestamp, sizeof( record.timestamp ) ) )
	{
		return 0;
	}

	// write the tag as hash (for compact storage)
	std::uint64_t tagHash = hashString( record.tag );
	if ( ! writeHelper.writeTlv( TlvType::TagId, &tagHash, sizeof( tagHash ) ) )
	{
		return 0;
	}

	// write optional source location fields, starting with the file name (if not too long)
	writeHelper.writeStringTlv( TlvType::FileName, record.file );

	// write the line number
	if ( ! writeHelper.writeTlv( TlvType::LineNumber, &record.line, sizeof( record.line ) ) )
	{
		return 0;
	}

	// write the function name (if not too long)
	writeHelper.writeStringTlv( TlvType::FunctionName, record.function );

	// write the process/thread info if enabled
	if ( _captureProcessInfo )
	{
		writeHelper.writeProcessInfoTlv();
	}

	// write message, with truncation fallback
	const bool messageTruncated = writeHelper.writeMessageTlv( record );

	// if message was truncated, add truncation marker
	if ( messageTruncated )
	{
		const std::uint8_t truncFlag = 1;
		writeHelper.writeTlv( TlvType::MessageTruncated, &truncFlag, sizeof( truncFlag ) );
	}

	// write end marker
	const std::uint16_t endType = std::uint16_t( TlvType::RecordEnd );
	const std::uint16_t zero = 0;
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
	const std::uint32_t totalSize = std::uint32_t( offset );
	std::memcpy( buffer + sizeOffset, &totalSize, sizeof( totalSize ) );

	return offset;
}

std::uint64_t EmergencySink::hashString( const char* str ) noexcept
{
	return ::kmac::nova::details::fnv1a( str );
}

} // namespace kmac::flare

namespace
{

bool TlvWriteHelper::writeTlv( kmac::flare::TlvType type, const void* data, std::uint16_t len ) noexcept
{
	const std::size_t needed = sizeof( std::uint16_t ) * 2 + len;
	if ( ( offset + needed ) > bufferSize )
	{
		// not enough space
		return false;
	}

	std::uint16_t typeVal = std::uint16_t( type );
	std::memcpy( buffer + offset, &typeVal, sizeof( typeVal ) );
	offset += sizeof( typeVal );

	std::memcpy( buffer + offset, &len, sizeof( len ) );
	offset += sizeof( len );

	std::memcpy( buffer + offset, data, len );
	offset += len;

	return true;
}

void TlvWriteHelper::writeStringTlv( kmac::flare::TlvType type, const char* str ) noexcept
{
	if ( str == nullptr )
	{
		return;
	}
	const std::size_t len = std::strlen( str );
	if ( len > 0 && len <= UINT16_MAX )
	{
		writeTlv( type, str, std::uint16_t( len ) );
	}
}

void TlvWriteHelper::writeProcessInfoTlv() noexcept
{
	// process ID
#if defined( _WIN32 )
	std::uint32_t pid = std::uint32_t( GetCurrentProcessId() );
	writeTlv( kmac::flare::TlvType::ProcessId, &pid, sizeof( pid ) );
#elif defined( __linux__ ) || defined( __unix__ ) || defined( __APPLE__ ) || defined( __FreeBSD__ )
	std::uint32_t pid = std::uint32_t( getpid() );
	writeTlv( kmac::flare::TlvType::ProcessId, &pid, sizeof( pid ) );
#endif

	// thread ID
#ifdef __linux__
	// TODO: MISRA deviation
	std::uint32_t tid = std::uint32_t( syscall( SYS_gettid ) ) ; // NOLINT(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
	writeTlv( kmac::flare::TlvType::ThreadId, &tid, sizeof( tid ) );
#endif
}

// returns true if message was truncated
bool TlvWriteHelper::writeMessageTlv( const kmac::nova::Record& record ) noexcept
{
	// make sure message and messageSize are valid
	if ( record.message == nullptr || record.messageSize == 0 )
	{
		return false;
	}

	// check if the message is smaller than uint16 max
	if ( record.messageSize <= UINT16_MAX )
	{
		if ( writeTlv( kmac::flare::TlvType::MessageBytes, record.message, std::uint16_t( record.messageSize ) ) )
		{
			// wrote successfully, no truncation
			return false;
		}

		// couldn't fit full message, so try to fit what we can
		const std::size_t remaining = bufferSize - offset - ( sizeof( std::uint16_t ) * 2 + sizeof( std::uint16_t ) * 2 );
		if ( remaining > 0 && remaining <= UINT16_MAX )
		{
			writeTlv( kmac::flare::TlvType::MessageBytes, record.message, std::uint16_t( remaining ) );
		}
	}

	// message exceeds uint16 max, so truncate to fit
	else
	{
		writeTlv( kmac::flare::TlvType::MessageBytes, record.message, UINT16_MAX );
	}

	// truncated
	return true;
}

} // anonymous namespace
