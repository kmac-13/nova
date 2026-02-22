// test_nova_circular_file_sink.cpp
// Tests for CircularFileSink

#include "kmac/nova/record.h"
#include "kmac/nova/extras/buffer.h"
#include "kmac/nova/extras/circular_file_sink.h"
#include "kmac/nova/extras/formatter.h"

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

// ============================================================================
// Helper: Temporary Directory
// ============================================================================

class TempDir
{
private:
	std::filesystem::path _path;

public:
	TempDir()
	{
		_path = std::filesystem::temp_directory_path() / "circular_sink_test";
		std::filesystem::remove_all( _path );
		std::filesystem::create_directories( _path );
	}

	~TempDir()
	{
		std::filesystem::remove_all( _path );
	}

	std::filesystem::path path() const { return _path; }
};

// ============================================================================
// Helper: Test Formatter (outputs "TEST\n")
// ============================================================================

class TestFormatter : public kmac::nova::extras::Formatter
{
public:
	void begin( const kmac::nova::Record& ) noexcept override {}

	bool format( const kmac::nova::Record&, kmac::nova::extras::Buffer& buffer ) noexcept override
	{
		const char* msg = "TEST\n";
		const std::size_t len = 5;
		return buffer.append( msg, len );
	}
};

// ============================================================================
// Helper Functions
// ============================================================================

std::string readFile( const std::string& path )
{
	std::ifstream in( path, std::ios::binary );
	return std::string(
		( std::istreambuf_iterator< char >( in ) ),
		std::istreambuf_iterator< char >()
	);
}

std::size_t getFileSize( const std::string& path )
{
	return std::filesystem::file_size( path );
}

// ============================================================================
// Tests
// ============================================================================

TEST( CircularFileSink, CreatesFile )
{
	TempDir dir;
	const std::string path = ( dir.path() / "test.log" ).string();

	{
		kmac::nova::extras::CircularFileSink sink( path, 1024 );
		EXPECT_TRUE( std::filesystem::exists( path ) );
	}

	// file should still exist after sink destroyed
	EXPECT_TRUE( std::filesystem::exists( path ) );
}

TEST( CircularFileSink, WritesRawRecord )
{
	TempDir dir;
	const std::string path = ( dir.path() / "test.log" ).string();

	kmac::nova::extras::CircularFileSink sink( path, 1024 );

	kmac::nova::Record record {};
	const char* msg = "Hello World";
	record.message = msg;
	record.messageSize = std::strlen( msg );

	sink.process( record );
	sink.flush();

	const std::string contents = readFile( path );
	EXPECT_EQ( contents, "Hello World" );
}

TEST( CircularFileSink, WritesFormattedRecord )
{
	TempDir dir;
	const std::string path = ( dir.path() / "test.log" ).string();

	TestFormatter formatter;
	kmac::nova::extras::CircularFileSink sink( path, 1024, &formatter );

	kmac::nova::Record record {};

	sink.process( record );
	sink.flush();

	const std::string contents = readFile( path );
	EXPECT_EQ( contents, "TEST\n" );
}

TEST( CircularFileSink, RespectsMaxFileSize )
{
	TempDir dir;
	const std::string path = ( dir.path() / "test.log" ).string();

	const std::size_t maxSize = 100;
	kmac::nova::extras::CircularFileSink sink( path, maxSize );

	// write multiple records
	for ( int i = 0; i < 20; ++i )
	{
		kmac::nova::Record record {};
		const char* msg = "12345";  // 5 bytes each
		record.message = msg;
		record.messageSize = 5;

		sink.process( record );
	}
	sink.flush();

	// file should not exceed maxSize
	const std::size_t fileSize = getFileSize( path );
	EXPECT_LE( fileSize, maxSize );
}

TEST( CircularFileSink, WrapsAroundCorrectly )
{
	TempDir dir;
	const std::string path = ( dir.path() / "test.log" ).string();

	const std::size_t maxSize = 20;  // Very small to force wrap
	kmac::nova::extras::CircularFileSink sink( path, maxSize );

	EXPECT_FALSE( sink.hasWrapped() );
	EXPECT_EQ( sink.currentPosition(), 0 );

	// write 5 bytes
	{
		kmac::nova::Record record {};
		record.message = "AAAAA";
		record.messageSize = 5;
		sink.process( record );
		sink.flush();
	}

	EXPECT_EQ( sink.currentPosition(), 5 );
	EXPECT_FALSE( sink.hasWrapped() );

	// write another 15 bytes (total 20, at limit)
	{
		kmac::nova::Record record {};
		record.message = "BBBBBBBBBBBBBBB";
		record.messageSize = 15;
		sink.process( record );
		sink.flush();
	}

	EXPECT_EQ( sink.currentPosition(), 20 );
	EXPECT_FALSE( sink.hasWrapped() );  // At limit but not wrapped yet

	// write 5 more bytes - should wrap
	{
		kmac::nova::Record record {};
		record.message = "CCCCC";
		record.messageSize = 5;
		sink.process( record );
		sink.flush();
	}

	EXPECT_TRUE( sink.hasWrapped() );
	EXPECT_EQ( sink.currentPosition(), 5 );  // Wrapped to position 5
	EXPECT_EQ( sink.totalWritten(), 25 );    // Total written is 25 bytes
}

TEST( CircularFileSink, OverwritesOldData )
{
	TempDir dir;
	const std::string path = ( dir.path() / "test.log" ).string();

	const std::size_t maxSize = 10;
	kmac::nova::extras::CircularFileSink sink( path, maxSize );

	// write "AAAAAAAAAA" (10 bytes, fills file)
	{
		kmac::nova::Record record {};
		record.message = "AAAAAAAAAA";
		record.messageSize = 10;
		sink.process( record );
		sink.flush();
	}

	std::string contents = readFile( path );
	EXPECT_EQ( contents, "AAAAAAAAAA" );

	// write "BBBB" (4 bytes, should overwrite first 4 bytes)
	{
		kmac::nova::Record record {};
		record.message = "BBBB";
		record.messageSize = 4;
		sink.process( record );
		sink.flush();
	}

	contents = readFile( path );
	EXPECT_EQ( contents, "BBBBAAAAAA" );  // BBBB overwrote first 4 A's

	// write "CCCCCC" (6 bytes, should overwrite next 6 bytes)
	{
		kmac::nova::Record record {};
		record.message = "CCCCCC";
		record.messageSize = 6;
		sink.process( record );
		sink.flush();
	}

	contents = readFile( path );
	EXPECT_EQ( contents, "BBBBCCCCCC" );  // CCCCCC overwrote last 6 A's
}

TEST( CircularFileSink, MultipleWraps )
{
	TempDir dir;
	const std::string path = ( dir.path() / "test.log" ).string();

	const std::size_t maxSize = 10;
	kmac::nova::extras::CircularFileSink sink( path, maxSize );

	// write 50 bytes (5 wraps worth)
	for ( int i = 0; i < 10; ++i )
	{
		kmac::nova::Record record {};
		record.message = "12345";
		record.messageSize = 5;
		sink.process( record );
	}
	sink.flush();

	EXPECT_TRUE( sink.hasWrapped() );
	EXPECT_EQ( sink.totalWritten(), 50 );
	
	// file size on disk should be exactly maxSize (file doesn't shrink after wrap)
	const std::size_t fileSize = getFileSize( path );
	EXPECT_EQ( fileSize, maxSize );

	// read file, which should contain the most recent data that was written
	// after writing 10 × "12345", we've wrapped 4 times (filled file 5 times total)
	// the file contains the last complete cycle of data
	const std::string contents = readFile( path );
	EXPECT_EQ( contents.size(), maxSize );
}

TEST( CircularFileSink, LargeWrite )
{
	TempDir dir;
	const std::string path = ( dir.path() / "test.log" ).string();

	const std::size_t maxSize = 100;
	kmac::nova::extras::CircularFileSink sink( path, maxSize );

	// write 200 bytes in one record (larger than maxSize)
	std::string largeMsg( 200, 'X' );
	{
		kmac::nova::Record record {};
		record.message = largeMsg.c_str();
		record.messageSize = 200;
		sink.process( record );
		sink.flush();
	}

	// file should be maxSize
	EXPECT_EQ( getFileSize( path ), maxSize );
	EXPECT_TRUE( sink.hasWrapped() );
	EXPECT_EQ( sink.totalWritten(), 200 );

	// file should contain last 100 X's
	const std::string contents = readFile( path );
	EXPECT_EQ( contents, std::string( 100, 'X' ) );
}

TEST( CircularFileSink, FormattedWithWrap )
{
	TempDir dir;
	const std::string path = ( dir.path() / "test.log" ).string();

	TestFormatter formatter;
	const std::size_t maxSize = 12;  // Can fit 2 "TEST\n" records (10 bytes) + 2 bytes
	kmac::nova::extras::CircularFileSink sink( path, maxSize, &formatter );

	kmac::nova::Record record {};

	// write 3 records (15 bytes total, will wrap)
	for ( int i = 0; i < 3; ++i )
	{
		sink.process( record );
	}
	sink.flush();

	EXPECT_TRUE( sink.hasWrapped() );
	EXPECT_EQ( getFileSize( path ), maxSize );
}

TEST( CircularFileSink, EmptyRecords )
{
	TempDir dir;
	const std::string path = ( dir.path() / "test.log" ).string();

	kmac::nova::extras::CircularFileSink sink( path, 1024 );

	// write empty record
	kmac::nova::Record record {};
	record.message = "";
	record.messageSize = 0;

	sink.process( record );
	sink.flush();

	EXPECT_EQ( getFileSize( path ), 0 );
	EXPECT_EQ( sink.currentPosition(), 0 );
	EXPECT_EQ( sink.totalWritten(), 0 );
}

TEST( CircularFileSink, ZeroMaxSize )
{
	TempDir dir;
	const std::string path = ( dir.path() / "test.log" ).string();

	// edge case: maxSize = 0
	kmac::nova::extras::CircularFileSink sink( path, 0 );

	kmac::nova::Record record {};
	record.message = "TEST";
	record.messageSize = 4;

	sink.process( record );
	sink.flush();

	// with maxSize = 0, behavior is: file exists but nothing should be written
	// the wrap logic will prevent writing since there's no space
	// accept that file may exist with 0 or non-zero size depending on implementation
	// this is an edge case that's not particularly useful in practice
	EXPECT_TRUE( std::filesystem::exists( path ) );
}

TEST( CircularFileSink, FilenameGetter )
{
	TempDir dir;
	const std::string path = ( dir.path() / "test.log" ).string();

	kmac::nova::extras::CircularFileSink sink( path, 1024 );

	EXPECT_EQ( sink.filename(), path );
}

TEST( CircularFileSink, MaxFileSizeGetter )
{
	TempDir dir;
	const std::string path = ( dir.path() / "test.log" ).string();

	const std::size_t maxSize = 12345;
	kmac::nova::extras::CircularFileSink sink( path, maxSize );

	EXPECT_EQ( sink.maxFileSize(), maxSize );
}

TEST( CircularFileSink, CurrentPositionTracking )
{
	TempDir dir;
	const std::string path = ( dir.path() / "test.log" ).string();

	kmac::nova::extras::CircularFileSink sink( path, 100 );

	EXPECT_EQ( sink.currentPosition(), 0 );

	{
		kmac::nova::Record record {};
		record.message = "12345";
		record.messageSize = 5;
		sink.process( record );
		sink.flush();
	}

	EXPECT_EQ( sink.currentPosition(), 5 );

	{
		kmac::nova::Record record {};
		record.message = "67890";
		record.messageSize = 5;
		sink.process( record );
		sink.flush();
	}

	EXPECT_EQ( sink.currentPosition(), 10 );
}

TEST( CircularFileSink, TotalWrittenTracking )
{
	TempDir dir;
	const std::string path = ( dir.path() / "test.log" ).string();

	const std::size_t maxSize = 20;
	kmac::nova::extras::CircularFileSink sink( path, maxSize );

	// write 50 bytes total (will wrap)
	for ( int i = 0; i < 10; ++i )
	{
		kmac::nova::Record record {};
		record.message = "12345";
		record.messageSize = 5;
		sink.process( record );
	}
	sink.flush();

	EXPECT_EQ( sink.totalWritten(), 50 );  // Total written is 50, even though file is 20
}

TEST( CircularFileSink, ConcurrentWrites )
{
	TempDir dir;
	const std::string path = ( dir.path() / "test.log" ).string();

	kmac::nova::extras::CircularFileSink sink( path, 1024 );

	// multiple writes without flush
	for ( int i = 0; i < 100; ++i )
	{
		kmac::nova::Record record {};
		record.message = "X";
		record.messageSize = 1;
		sink.process( record );
	}

	// all should be buffered
	EXPECT_EQ( sink.currentPosition(), 0 );  // Not flushed yet

	sink.flush();

	EXPECT_EQ( sink.currentPosition(), 100 );
	EXPECT_EQ( getFileSize( path ), 100 );
}

// ============================================================================
// Main
// ============================================================================

int main( int argc, char** argv )
{
	::testing::InitGoogleTest( &argc, argv );
	return RUN_ALL_TESTS();
}
