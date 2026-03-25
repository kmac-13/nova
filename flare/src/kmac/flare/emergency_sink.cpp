#include "kmac/flare/emergency_sink.h"

#include "kmac/flare/record.h"
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

// ============================================================================
// Stack trace capture - opt-in via FLARE_ENABLE_STACK_TRACE
//
// None of the APIs below are formally async-signal-safe per POSIX.  In
// practice the glibc/libgcc/libunwind implementations are widely used in
// crash handlers and avoid heap allocation on supported platforms, but this
// must remain an explicit opt-in so callers understand the trade-off.
//
// __ANDROID__ must be tested before __linux__ because Android defines both.
// ============================================================================
#if defined( FLARE_ENABLE_STACK_TRACE )
	#if defined( __ANDROID__ )
		// Bionic does not ship <execinfo.h>, use the NDK unwinder instead.
		// _Unwind_Backtrace is available since NDK r7 and does not allocate.
		// on 32-bit ARM with -fomit-frame-pointer some frames may be missing;
		// AArch64 (most modern targets) unwinds reliably.
		#include <unwind.h>
		#define FLARE_BACKTRACE_UNWIND 1
	#elif defined( __linux__ ) || defined( __APPLE__ ) || defined( __FreeBSD__ )
		#include <execinfo.h>  // backtrace()
		#define FLARE_BACKTRACE_EXECINFO 1
	#elif defined( _WIN32 )
		// RtlCaptureStackBackTrace is in kernel32 (always available) and is
		// lighter than DbgHelp.  It does not resolve symbols, which is
		// intentional; symbolization is a post-processing step in flare_reader.
		#define FLARE_BACKTRACE_WINDOWS 1
	#endif
#endif

// ============================================================================
// Load base address capture
//
// Required to convert runtime frame addresses to static binary addresses for
// addr2line / llvm-symbolizer on PIE (ASLR) binaries.
// Captured once at construction; never called from a signal handler.
// ============================================================================
#if defined( __ANDROID__ ) || defined( __linux__ )
	#include <link.h>          // dl_iterate_phdr, ElfW
#elif defined( __APPLE__ )
	#include <mach-o/dyld.h>   // _dyld_get_image_vmaddr_slide, _dyld_get_image_header
	#include <mach-o/loader.h> // MH_EXECUTE
#elif defined( _WIN32 )
	#include <windows.h>
	#include <psapi.h>         // GetModuleInformation
#endif

namespace
{

// ============================================================================
// Android _Unwind_Backtrace callback state
// ============================================================================
#if defined( FLARE_BACKTRACE_UNWIND )
struct UnwindState
{
	void** frames;
	std::size_t count;
	std::size_t maxFrames;
};

_Unwind_Reason_Code unwindCallback( struct _Unwind_Context* context, void* arg ) noexcept;
#endif

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

	void writeLoadBaseAddressTlv( std::uint64_t loadBaseAddress ) noexcept;

	// frames: array of raw return addresses
	// frameCount: number of valid entries
	void writeStackFramesTlv( void* const* frames, std::size_t frameCount ) noexcept;

	// returns true if message was truncated
	bool writeMessageTlv( const kmac::nova::Record& record ) noexcept;
};

} // anonymous namespace

namespace kmac::flare
{

EmergencySinkBase::EmergencySinkBase( IWriter* writer, bool captureProcessInfo, bool captureStackTrace ) noexcept
	: _writer( writer )
	, _loadBaseAddress( captureLoadBaseAddress() )
	, _captureProcessInfo( captureProcessInfo )
	, _captureStackTrace( captureStackTrace )
{
}

void EmergencySinkBase::flush() noexcept
{
	if ( _writer != nullptr )
	{
		_writer->flush();
	}
}

IWriter* EmergencySinkBase::writer() const noexcept
{
	return _writer;
}

std::size_t EmergencySinkBase::encodeRecordTlv( const kmac::nova::Record& record, char* buffer, std::size_t bufferSize, std::size_t sequenceNumber ) noexcept
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
	if ( ! writeHelper.writeTlv( TlvType::SequenceNumber, &sequenceNumber, sizeof( sequenceNumber ) ) )
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

	// write load base address (always; value is 0 on unsupported platforms)
	writeHelper.writeLoadBaseAddressTlv( _loadBaseAddress );

	// write stack trace if enabled and compile-time support is present
	if ( _captureStackTrace )
	{
		std::array< void*, Record::MAX_STACK_FRAMES > frames{};
		const std::size_t frameCount = captureStackFrames( std::data( frames ), Record::MAX_STACK_FRAMES );
		if ( frameCount > 0 )
		{
			writeHelper.writeStackFramesTlv( std::data( frames ), frameCount );
		}
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

std::uint64_t EmergencySinkBase::hashString( const char* str ) noexcept
{
	return ::kmac::nova::details::fnv1a( str );
}

// ============================================================================
// Load base address capture
// ============================================================================

#if defined( __ANDROID__ ) || defined( __linux__ )

namespace
{
// dl_iterate_phdr callback: finds the first PT_LOAD segment of the main
// executable (info->dlpi_name == "" for the main executable) and records
// the difference between its runtime address and its preferred load address,
// which is the ASLR slide applied by the dynamic linker.
struct PhdrCallbackData
{
	std::uint64_t baseAddress;
	bool found;
};

int phdrCallback( struct dl_phdr_info* info, std::size_t /*size*/, void* data ) noexcept
{
	auto* result = static_cast< PhdrCallbackData* >( data );

	// the main executable has an empty name string
	if ( info->dlpi_name != nullptr && info->dlpi_name[ 0 ] != '\0' )
	{
		return 0;  // continue iteration
	}

	// dlpi_addr is the load bias, the offset added to all virtual addresses
	// in the ELF file; for a non-PIE binary this is 0
	result->baseAddress = static_cast< std::uint64_t >( info->dlpi_addr );
	result->found = true;
	return 1;  // stop iteration
}
} // anonymous namespace

#endif  // Android || Linux

std::uint64_t EmergencySinkBase::captureLoadBaseAddress() noexcept
{
#if defined( __ANDROID__ ) || defined( __linux__ )

	PhdrCallbackData data{ 0, false };
	dl_iterate_phdr( phdrCallback, &data );
	return data.found ? data.baseAddress : 0;

#elif defined( __APPLE__ )

	// walk dyld's image list to find the main executable (MH_EXECUTE);
	// _dyld_get_image_vmaddr_slide() gives the ASLR slide for that image
	const std::uint32_t imageCount = _dyld_image_count();
	for ( std::uint32_t i = 0; i < imageCount; ++i )
	{
		const struct mach_header* header = _dyld_get_image_header( i );
		if ( header != nullptr && header->filetype == MH_EXECUTE )
		{
			// the slide is the difference between the runtime load address
			// and the static preferred address encoded in the Mach-O header.
			const intptr_t slide = _dyld_get_image_vmaddr_slide( i );
			return static_cast< std::uint64_t >( slide );
		}
	}
	return 0;

#elif defined( _WIN32 )

	// GetModuleHandle(NULL) returns the base address of the main executable;
	// no symbol resolution, no heap allocation
	// NOLINT NOTE: Windows macro
	const HMODULE hModule = GetModuleHandle( nullptr ); // NOLINT(cppcoreguidelines-pro-type-cstyle-cast)
	if ( hModule == nullptr )
	{
		return 0;
	}

	MODULEINFO info{};
	if ( GetModuleInformation( GetCurrentProcess(), hModule, &info, sizeof( info ) ) == 0 )
	{
		return 0;
	}
	return reinterpret_cast< std::uint64_t >( info.lpBaseOfDll ); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)

#else

	return 0;

#endif
}

// ============================================================================
// Stack frame capture
// ============================================================================

std::size_t EmergencySinkBase::captureStackFrames( void** frames, std::size_t maxFrames ) noexcept
{
#if defined( FLARE_BACKTRACE_UNWIND )

	::UnwindState state{ frames, 0, maxFrames };
	_Unwind_Backtrace( ::unwindCallback, &state );
	return state.count;

#elif defined( FLARE_BACKTRACE_EXECINFO )

	// backtrace() fills the array with return addresses (the cast
	// is safe since backtrace() returns >= 0 and <= maxFrames)
	const int count = backtrace( frames, static_cast< int >( maxFrames ) );
	return static_cast< std::size_t >( count > 0 ? count : 0 );

#elif defined( FLARE_BACKTRACE_WINDOWS )

	// FramesToSkip=0 starts from this frame; caller can trim frame 0 and 1
	// (captureStackFrames itself + encodeRecordTlv) in the reader
	const USHORT count = RtlCaptureStackBackTrace(
		0,
		static_cast< ULONG >( maxFrames ),
		frames,
		nullptr
	);
	return static_cast< std::size_t >( count );

#else

	return 0;

#endif
}

} // namespace kmac::flare

namespace
{

#if defined( FLARE_BACKTRACE_UNWIND )

_Unwind_Reason_Code unwindCallback( struct _Unwind_Context* context, void* arg ) noexcept
{
	auto* state = static_cast< UnwindState* >( arg );
	if ( state->count >= state->maxFrames )
	{
		return _URC_END_OF_STACK;
	}
	// NOLINT NOTE: reinterpret_cast required to convert uintptr_t from libunwind API to void*
	state->frames[ state->count++ ] = reinterpret_cast< void* >( // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
		_Unwind_GetIP( context )
	);
	return _URC_NO_REASON;
}

#endif  // FLARE_BACKGRACE_UNWIND

bool TlvWriteHelper::writeTlv( kmac::flare::TlvType type, const void* data, std::uint16_t len ) noexcept
{
	const std::size_t needed = sizeof( std::uint16_t ) * 2 + len;
	if ( ( offset + needed ) > bufferSize )
	{
		// not enough space
		return false;
	}

	// use a local copy, committing back to the reference only on success
	// also, which makes the all-or-nothing write semantics explicit
	std::size_t localOffset = offset;

	// even with the bounds check above, GCC's -Wstringop-overflow loses
	// track of the buffer size across the char* member of TlvWriteHelper
	// (the pointer carries no size metadata after the call boundary), so
	// suppress the false positive locally
#if defined( __GNUC__ ) && ! defined( __clang__ )
#	pragma GCC diagnostic push
#	pragma GCC diagnostic ignored "-Wstringop-overflow"
#endif

	std::uint16_t typeVal = std::uint16_t( type );
	std::memcpy( buffer + localOffset, &typeVal, sizeof( typeVal ) );
	localOffset += sizeof( typeVal );

	std::memcpy( buffer + localOffset, &len, sizeof( len ) );
	localOffset += sizeof( len );

	std::memcpy( buffer + localOffset, data, len );
	localOffset += len;

#if defined( __GNUC__ ) && ! defined( __clang__ )
#	pragma GCC diagnostic pop
#endif

	offset = localOffset;
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

void TlvWriteHelper::writeLoadBaseAddressTlv( std::uint64_t loadBaseAddress ) noexcept
{
	writeTlv( kmac::flare::TlvType::LoadBaseAddress, &loadBaseAddress, sizeof( loadBaseAddress ) );
}

void TlvWriteHelper::writeStackFramesTlv( void* const* frames, std::size_t frameCount ) noexcept
{
	if ( frames == nullptr || frameCount == 0 )
	{
		return;
	}

	// frameCount is already clamped to MAX_STACK_FRAMES by the caller, so
	// the VLA-equivalent below is bounded.  Pack void* addresses into uint64_t
	// so the TLV payload format is uniform regardless of the target pointer width.
	// UINT16_MAX / 8 = 8191 frames max in a single TLV; far above our cap of 32.
	static_assert(
		kmac::flare::Record::MAX_STACK_FRAMES * sizeof( std::uint64_t ) <= UINT16_MAX,
		"MAX_STACK_FRAMES too large for uint16_t TLV length field"
	);

	std::array< std::uint64_t, kmac::flare::Record::MAX_STACK_FRAMES > addresses{};
	const std::size_t count = frameCount < kmac::flare::Record::MAX_STACK_FRAMES
		? frameCount
		: kmac::flare::Record::MAX_STACK_FRAMES;

	for ( std::size_t i = 0; i < count; ++i )
	{
		// NOLINT NOTE: reinterpret_cast required to widen void* to uint64_t for serialisation
		addresses.at( i ) = reinterpret_cast< std::uint64_t >( frames[ i ] ); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
	}

	const std::uint16_t payloadLen = static_cast< std::uint16_t >( count * sizeof( std::uint64_t ) );
	writeTlv( kmac::flare::TlvType::StackFrames, std::data( addresses ), payloadLen );
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
