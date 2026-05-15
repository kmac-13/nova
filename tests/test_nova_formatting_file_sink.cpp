/**
 * @file test_nova_formatting_file_sink.cpp
 * @brief Unit tests for FormattingFileSink.
 *
 * All tests use tmpfile() for an anonymous FILE* so no cleanup is needed.
 * After writing, the file is rewound and read back to verify content.
 *
 * Three paths are exercised:
 *   raw mode   - no formatter; record.message bytes written directly via fwrite
 *   formatted  - Formatter interface drives the output
 *   chunking   - FormattingFileSink<N> with a small N forces multiple format()
 *                calls per record, exercising the inner while-loop
 */

#include "kmac/nova.h"
#include "kmac/nova/extras/buffer.h"
#include "kmac/nova/extras/formatter.h"
#include "kmac/nova/extras/formatting_file_sink.h"
#include "kmac/nova/extras/iso8601_formatter.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if ! defined( _WIN32 )
#include <unistd.h>
#endif

// test tag - fixed timestamp for predictable ISO 8601 output
struct FileSinkTag {};
std::uint64_t fileSinkTs() noexcept
{
	return 1704067200000000000ULL; // 2024-01-01 00:00:00.000 UTC
}
NOVA_LOGGER_TRAITS( FileSinkTag, FILESINK, true, fileSinkTs );

// ============================================================================
// Helpers
// ============================================================================

// read the full contents of a FILE* from its current position
static std::string readAll( FILE* file )
{
	std::rewind( file );
	std::string result;
	char buf[ 4096 ];
	std::size_t n;
	while ( ( n = std::fread( buf, 1, sizeof( buf ), file ) ) > 0 )
	{
		result.append( buf, n );
	}
	return result;
}

// open a temporary file in the system temp directory - portable across
// Windows (where tmpfile() requires C:\ write access) and POSIX
static FILE* openTempFile( std::string& pathOut )
{
#if defined( _WIN32 )
	char* name = _tempnam( nullptr, "nova_test_" );
	if ( name == nullptr )
	{
		return nullptr;
	}
	pathOut = name;
	std::free( name );
	return std::fopen( pathOut.c_str(), "w+b" );
#else
	char tmpl[] = "/tmp/nova_test_XXXXXX";
	const int fd = mkstemp( tmpl );
	if ( fd == -1 )
	{
		return nullptr;
	}
	pathOut = tmpl;
	return fdopen( fd, "w+b" );
#endif
}

// build a minimal Record with fixed metadata
static kmac::nova::Record makeRecord(
	const char* msg,
	std::uint32_t msgLen,
	const char* tag = "FILESINK",
	std::uint64_t ts = 1704067200000000000ULL
)
{
	kmac::nova::Record r {};
	r.tag = tag;
	r.tagId = 0;
	r.file = "test.cpp";
	r.function = "testFn";
	r.line = 42;
	r.timestamp = ts;
	r.message = msg;
	r.messageSize = msgLen;
	return r;
}

// ============================================================================
// Raw mode (no formatter)
// ============================================================================

class FormattingFileSinkRaw : public ::testing::Test
{
protected:
	FILE* _file = nullptr;
	std::string _path;

	void SetUp() override
	{
		_file = openTempFile( _path );
		ASSERT_NE( _file, nullptr ) << "failed to open temp file";
	}

	void TearDown() override
	{
		if ( _file != nullptr )
		{
			std::fclose( _file );
		}
		if ( ! _path.empty() )
		{
			std::remove( _path.c_str() );
		}
	}
};

TEST_F( FormattingFileSinkRaw, BasicWrite )
{
	kmac::nova::extras::FormattingFileSink<> sink( _file );
	auto rec = makeRecord( "hello raw", 9 );

	sink.process( rec );
	sink.flush();

	EXPECT_EQ( readAll( _file ), "hello raw" );
}

TEST_F( FormattingFileSinkRaw, MultipleWrites )
{
	kmac::nova::extras::FormattingFileSink<> sink( _file );

	auto r1 = makeRecord( "foo", 3 );
	auto r2 = makeRecord( "bar", 3 );
	auto r3 = makeRecord( "baz", 3 );

	sink.process( r1 );
	sink.process( r2 );
	sink.process( r3 );
	sink.flush();

	EXPECT_EQ( readAll( _file ), "foobarbaz" );
}

TEST_F( FormattingFileSinkRaw, EmptyMessage )
{
	kmac::nova::extras::FormattingFileSink<> sink( _file );
	auto rec = makeRecord( "", 0 );

	sink.process( rec );
	sink.flush();

	EXPECT_EQ( readAll( _file ), "" );
}

TEST_F( FormattingFileSinkRaw, FlushOnNullSinkDoesNotCrash )
{
	kmac::nova::extras::FormattingFileSink<> sink( nullptr );
	auto rec = makeRecord( "msg", 3 );

	sink.process( rec );
	sink.flush();

	SUCCEED();
}

TEST_F( FormattingFileSinkRaw, NullFileSinkDoesNotCrash )
{
	// process() with a null FILE* must be silently ignored
	kmac::nova::extras::FormattingFileSink<> sink( nullptr );
	auto rec = makeRecord( "ignored", 7 );
	sink.process( rec );
	SUCCEED();
}

TEST_F( FormattingFileSinkRaw, ViaLoggerMacro )
{
	kmac::nova::extras::FormattingFileSink<> sink( _file );

	kmac::nova::ScopedConfigurator<> config;
	config.bind< FileSinkTag >( &sink );

	NOVA_LOG( FileSinkTag ) << "via macro";
	sink.flush();

	const std::string content = readAll( _file );
	EXPECT_NE( content.find( "via macro" ), std::string::npos );
}

TEST_F( FormattingFileSinkRaw, ContentMatchesByteForByte )
{
	kmac::nova::extras::FormattingFileSink<> sink( _file );

	// use a message with non-ASCII bytes to verify memcpy fidelity
	const char msg[] = { '\x01', '\xFF', '\x7F', '\x00' };
	auto rec = makeRecord( msg, 3 ); // 3 bytes, not including \x00

	sink.process( rec );
	sink.flush();

	const std::string content = readAll( _file );
	ASSERT_EQ( content.size(), 3u );
	EXPECT_EQ( static_cast< unsigned char >( content[ 0 ] ), 0x01 );
	EXPECT_EQ( static_cast< unsigned char >( content[ 1 ] ), 0xFF );
	EXPECT_EQ( static_cast< unsigned char >( content[ 2 ] ), 0x7F );
}

// ============================================================================
// Formatted mode (ISO8601Formatter)
// ============================================================================

class FormattingFileSinkFormatted : public ::testing::Test
{
protected:
	FILE* _file = nullptr;
	std::string _path;
	kmac::nova::extras::ISO8601Formatter _formatter;

	void SetUp() override
	{
		_file = openTempFile( _path );
		ASSERT_NE( _file, nullptr ) << "failed to open temp file";
	}

	void TearDown() override
	{
		if ( _file != nullptr )
		{
			std::fclose( _file );
		}
		if ( ! _path.empty() )
		{
			std::remove( _path.c_str() );
		}
	}
};

TEST_F( FormattingFileSinkFormatted, OutputIsNotEmpty )
{
	kmac::nova::extras::FormattingFileSink<> sink( _file, &_formatter );
	auto rec = makeRecord( "formatted", 9 );

	sink.process( rec );
	sink.flush();

	EXPECT_FALSE( readAll( _file ).empty() );
}

TEST_F( FormattingFileSinkFormatted, ContainsMessage )
{
	kmac::nova::extras::FormattingFileSink<> sink( _file, &_formatter );
	auto rec = makeRecord( "hello formatter", 15 );

	sink.process( rec );
	sink.flush();

	EXPECT_NE( readAll( _file ).find( "hello formatter" ), std::string::npos );
}

TEST_F( FormattingFileSinkFormatted, ContainsTagName )
{
	kmac::nova::extras::FormattingFileSink<> sink( _file, &_formatter );
	auto rec = makeRecord( "tag check", 9 );

	sink.process( rec );
	sink.flush();

	EXPECT_NE( readAll( _file ).find( "FILESINK" ), std::string::npos );
}

TEST_F( FormattingFileSinkFormatted, ContainsISO8601Timestamp )
{
	kmac::nova::extras::FormattingFileSink<> sink( _file, &_formatter );
	auto rec = makeRecord( "ts check", 8 );

	sink.process( rec );
	sink.flush();

	// fixed timestamp resolves to 2024-01-01T00:00:00
	EXPECT_NE( readAll( _file ).find( "2024-01-01T00:00:00" ), std::string::npos );
}

TEST_F( FormattingFileSinkFormatted, MultipleRecords )
{
	kmac::nova::extras::FormattingFileSink<> sink( _file, &_formatter );

	sink.process( makeRecord( "alpha", 5 ) );
	sink.process( makeRecord( "beta", 4 ) );
	sink.flush();

	const std::string content = readAll( _file );
	EXPECT_NE( content.find( "alpha" ), std::string::npos );
	EXPECT_NE( content.find( "beta" ),  std::string::npos );
}

TEST_F( FormattingFileSinkFormatted, ViaLoggerMacro )
{
	kmac::nova::extras::FormattingFileSink<> sink( _file, &_formatter );

	kmac::nova::ScopedConfigurator<> config;
	config.bind< FileSinkTag >( &sink );

	NOVA_LOG( FileSinkTag ) << "formatted via macro";
	sink.flush();

	const std::string content = readAll( _file );
	EXPECT_NE( content.find( "formatted via macro" ), std::string::npos );
	EXPECT_NE( content.find( "FILESINK" ), std::string::npos );
}

// ============================================================================
// Buffer-chunking path
//
// FormattingFileSink<N> with a small N forces the inner while-loop in
// process() to iterate more than once per record: each format() call that
// returns false triggers a flushFormatBuffer() + retry cycle.
//
// ISO8601Formatter output for a typical record is ~80 characters; using
// BufferSize=32 guarantees at least three iterations.
// ============================================================================

class FormattingFileSinkChunking : public ::testing::Test
{
protected:
	FILE* _file = nullptr;
	std::string _path;
	kmac::nova::extras::ISO8601Formatter _formatter;

	void SetUp() override
	{
		_file = openTempFile( _path );
		ASSERT_NE( _file, nullptr ) << "failed to open temp file";
	}

	void TearDown() override
	{
		if ( _file != nullptr )
		{
			std::fclose( _file );
		}
		if ( ! _path.empty() )
		{
			std::remove( _path.c_str() );
		}
	}
};

TEST_F( FormattingFileSinkChunking, SmallBufferProducesCompleteOutput )
{
	// 32-byte format buffer - ISO8601 output will not fit in one pass
	kmac::nova::extras::FormattingFileSink< 32 > sink( _file, &_formatter );
	auto rec = makeRecord( "chunked output", 14 );

	sink.process( rec );
	sink.flush();

	const std::string content = readAll( _file );

	// the full record must arrive despite requiring multiple buffer flushes
	EXPECT_NE( content.find( "chunked output" ), std::string::npos );
	EXPECT_NE( content.find( "FILESINK" ), std::string::npos );
	EXPECT_NE( content.find( "2024-01-01" ), std::string::npos );
}

TEST_F( FormattingFileSinkChunking, SmallBufferMultipleRecords )
{
	kmac::nova::extras::FormattingFileSink< 32 > sink( _file, &_formatter );

	sink.process( makeRecord( "record one", 10 ) );
	sink.process( makeRecord( "record two", 10 ) );
	sink.process( makeRecord( "record three", 12 ) );
	sink.flush();

	const std::string content = readAll( _file );
	EXPECT_NE( content.find( "record one" ), std::string::npos );
	EXPECT_NE( content.find( "record two" ), std::string::npos );
	EXPECT_NE( content.find( "record three" ), std::string::npos );
}

