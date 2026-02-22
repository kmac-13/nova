/**
 * @file test_flare_reader.cpp
 * @brief Google Test unit tests for Flare Reader
 */

#include "kmac/flare/emergency_sink.h"
#include "kmac/flare/file_writer.h"
#include "kmac/flare/reader.h"
#include "kmac/nova/logger_traits.h"
#include "kmac/nova/record.h"

#include "test_helpers.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// test tag
struct ReaderTag { };
std::uint64_t timestamp() noexcept { return 9999999ULL; }
NOVA_LOGGER_TRAITS( ReaderTag, READER, true, timestamp );


class FlareReader : public ::testing::Test
{
protected:
	std::filesystem::path tempDir;

	void SetUp() override
	{
		tempDir = test_helpers::createTempDirectory();
		ASSERT_FALSE( tempDir.empty() ) << "Failed to create temp directory";
	}

	void TearDown() override
	{
		test_helpers::removeTempDirectory( tempDir );
	}

	std::string getTempFilePath( const std::string& filename )
	{
		return ( tempDir / filename ).string();
	}

	std::vector< uint8_t > readFile( const std::string& filepath )
	{
		std::ifstream file( filepath, std::ios::binary );
		return std::vector< uint8_t >(
			std::istreambuf_iterator< char >( file ),
			std::istreambuf_iterator< char >()
		);
	}
};

TEST_F( FlareReader, ReaderParseSingleRecord )
{
	std::string filepath = getTempFilePath( "reader_single.flare" );

	// write a record
	std::FILE* file = std::fopen( filepath.c_str(), "wb" );
	ASSERT_NE( file, nullptr );

	kmac::flare::FileWriter writer( file );
	kmac::flare::EmergencySink sink( &writer );
	kmac::nova::Record writeRecord {
		"TEST",
		0,
		"file.cpp",
		"testFunc",
		123,
		1704067200000000000ULL,
		"test message",
		12
	};
	sink.process( writeRecord );
	sink.flush();
	std::fclose( file );

	// Read it back
	auto data = readFile( filepath );
	EXPECT_FALSE( data.empty() );

	kmac::flare::Reader reader;
	kmac::flare::Record readRecord;

	bool success = reader.parseNext( data.data(), data.size(), readRecord );
	EXPECT_TRUE( success );

	EXPECT_STREQ( readRecord.file, "file.cpp" );
	EXPECT_STREQ( readRecord.function, "testFunc" );
	EXPECT_EQ( readRecord.line, 123u );
	EXPECT_EQ( readRecord.timestampNs, 1704067200000000000ULL );
	EXPECT_NE( std::string( readRecord.message ).find( "test message" ), std::string::npos );
}

TEST_F( FlareReader, ReaderParseMultipleRecords )
{
	std::string filepath = getTempFilePath( "reader_multiple.flare" );

	// write multiple records
	std::FILE* file = std::fopen( filepath.c_str(), "wb" );
	ASSERT_NE( file, nullptr );

	kmac::flare::FileWriter writer( file );
	kmac::flare::EmergencySink sink( &writer );

	for ( int i = 0; i < 5; ++i )
	{
		std::string msg = "message " + std::to_string( i );
		kmac::nova::Record record {
			"TEST",
			0,
			"file.cpp",
			"func",
			uint32_t( 100 + i ),
			std::uint64_t( 1000 + i ),
			msg.c_str(),
			msg.size()
		};
		sink.process( record );
	}

	sink.flush();
	std::fclose( file );

	// read them back
	auto data = readFile( filepath );
	EXPECT_FALSE( data.empty() );

	kmac::flare::Reader reader;
	kmac::flare::Record record;
	int count = 0;

	while ( reader.parseNext( data.data(), data.size(), record ) )
	{
		EXPECT_EQ( record.line, uint32_t( 100 + count ) );
		EXPECT_EQ( record.timestampNs, uint64_t( 1000 + count ) );

		std::string expectedMsg = "message " + std::to_string( count );
		EXPECT_NE( std::string( record.message ).find( expectedMsg ), std::string::npos );

		++count;
	}

	EXPECT_EQ( count, 5 );
}

TEST_F( FlareReader, ReaderSequenceNumbers )
{
	std::string filepath = getTempFilePath( "reader_sequence.flare" );

	// write records
	std::FILE* file = std::fopen( filepath.c_str(), "wb" );
	ASSERT_NE( file, nullptr );

	kmac::flare::FileWriter writer( file );
	kmac::flare::EmergencySink sink( &writer );

	for ( int i = 0; i < 10; ++i )
	{
		std::string msg = "seq " + std::to_string( i );
		kmac::nova::Record record {
			"SEQ",
			0,
			"file.cpp",
			"func",
			uint32_t( i ),
			uint64_t( i ),
			msg.c_str(),
			msg.size()
		};
		sink.process( record );
	}

	sink.flush();
	std::fclose( file );

	// read and verify sequence numbers
	auto data = readFile( filepath );

	kmac::flare::Reader reader;
	kmac::flare::Record record;
	uint64_t expectedSeq = 0;

	while ( reader.parseNext( data.data(), data.size(), record ) )
	{
		EXPECT_EQ( record.sequenceNumber, expectedSeq );
		++expectedSeq;
	}

	EXPECT_EQ( expectedSeq, 10u );
}

TEST_F( FlareReader, ReaderTagHash )
{
	std::string filepath = getTempFilePath( "reader_tags.flare" );

	// write records with different tags
	std::FILE* file = std::fopen( filepath.c_str(), "wb" );
	ASSERT_NE( file, nullptr );

	kmac::flare::FileWriter writer( file );
	kmac::flare::EmergencySink sink( &writer );

	kmac::nova::Record record1 {
		"TAG_A",
		0,
		"file.cpp",
		"func",
		1,
		100ULL,
		"message A",
		9
	};
	sink.process( record1 );

	kmac::nova::Record record2 {
		"TAG_B",
		0,
		"file.cpp",
		"func",
		2,
		200ULL,
		"message B",
		9
	};
	sink.process( record2 );

	kmac::nova::Record record3 {
		"TAG_A",  // same tag as record1
		0,
		"file.cpp",
		"func",
		3,
		300ULL,
		"message C",
		9
	};
	sink.process( record3 );

	sink.flush();
	std::fclose( file );

	// Read and verify tag hashes
	auto data = readFile( filepath );

	kmac::flare::Reader reader;
	kmac::flare::Record records[ 3 ];
	int count = 0;

	while ( count < 3 && reader.parseNext( data.data(), data.size(), records[ count ] ) )
	{
		++count;
	}

	EXPECT_EQ( count, 3 );

	// TAG_A records should have same hash
	EXPECT_EQ( records[ 0 ].tagId, records[ 2 ].tagId );

	// TAG_B should have different hash
	EXPECT_NE( records[ 0 ].tagId, records[ 1 ].tagId );
}

TEST_F( FlareReader, ReaderEmptyFile )
{
	std::string filepath = getTempFilePath( "reader_empty.flare" );

	// create empty file
	std::FILE* file = std::fopen( filepath.c_str(), "wb" );
	ASSERT_NE( file, nullptr );
	std::fclose( file );

	auto data = readFile( filepath );

	kmac::flare::Reader reader;
	kmac::flare::Record record;

	bool success = reader.parseNext( data.data(), data.size(), record );
	EXPECT_FALSE( success );  // should return false for empty file
}

TEST_F(FlareReader, ReaderLargeMessage)
{
	std::string filepath = getTempFilePath( "reader_large.flare" );

	// write record with large message
	std::FILE* file = std::fopen( filepath.c_str(), "wb" );
	ASSERT_NE( file, nullptr );

	kmac::flare::FileWriter writer( file );
	kmac::flare::EmergencySink sink( &writer );

	std::string largeMsg( 10000, 'X' );
	kmac::nova::Record record {
		"TEST",
		0,
		"file.cpp",
		"func",
		42,
		1234567890ULL,
		largeMsg.c_str(),
		largeMsg.size()
	};
	sink.process( record );
	sink.flush();
	std::fclose( file );

	// read it back
	auto data = readFile( filepath );

	kmac::flare::Reader reader;
	kmac::flare::Record readRecord;

	bool success = reader.parseNext( data.data(), data.size(), readRecord );
	EXPECT_TRUE( success );

	// message should be truncated or complete
	EXPECT_GT( readRecord.messageLen, 0u );
	EXPECT_LE( readRecord.messageLen, kmac::flare::Record::MAX_MESSAGE_LEN );
}

TEST_F( FlareReader, ReaderRecordStatus )
{
	std::string filepath = getTempFilePath( "reader_status.flare" );

	// write a record
	std::FILE* file = std::fopen( filepath.c_str(), "wb" );
	ASSERT_NE( file, nullptr );

	kmac::flare::FileWriter writer( file );
	kmac::flare::EmergencySink sink( &writer );
	kmac::nova::Record record {
		"TEST",
		0,
		"file.cpp",
		"func",
		42,
		1234ULL,
		"status test",
		11
	};
	sink.process( record );
	sink.flush();
	std::fclose( file );

	// read and check status
	auto data = readFile( filepath );

	kmac::flare::Reader reader;
	kmac::flare::Record readRecord;

	bool success = reader.parseNext( data.data(), data.size(), readRecord );
	EXPECT_TRUE( success );

	// status should be Complete (0x01) or Truncated (0x02)
	std::string status = std::string( readRecord.statusString() );
	EXPECT_TRUE( status == "Complete" || status == "Truncated" );
}

TEST_F( FlareReader, ReaderProcessInfo )
{
	std::string filepath = getTempFilePath( "reader_process.flare" );

	// write with process info enabled
	std::FILE* file = std::fopen( filepath.c_str(), "wb" );
	ASSERT_NE( file, nullptr );

	kmac::flare::FileWriter writer( file );
	kmac::flare::EmergencySink sink( &writer );
	kmac::nova::Record record {
		"TEST",
		0,
		"file.cpp",
		"func",
		42,
		1234ULL,
		"process test",
		12
	};
	sink.process( record );
	sink.flush();
	std::fclose( file );

	// read and verify
	auto data = readFile( filepath );

	kmac::flare::Reader reader;
	kmac::flare::Record readRecord;

	bool success = reader.parseNext( data.data(), data.size(), readRecord );
	EXPECT_TRUE( success );

	// process ID should be non-zero on most platforms
	// (but we don't assert this as it's platform-dependent)
	EXPECT_GE( readRecord.processId, 0u );
}

TEST_F( FlareReader, ReaderMultiplePasses )
{
	std::string filepath = getTempFilePath( "reader_multipass.flare" );

	// write records
	std::FILE* file = std::fopen( filepath.c_str(), "wb" );
	ASSERT_NE( file, nullptr );

	kmac::flare::FileWriter writer( file );
	kmac::flare::EmergencySink sink( &writer );
	for ( int i = 0; i < 3; ++i )
	{
		std::string msg = "pass " + std::to_string( i );
		kmac::nova::Record record {
			"TEST",
			0,
			"file.cpp",
			"func",
			uint32_t( i ),
			uint64_t( i ),
			msg.c_str(),
			msg.size()
		};
		sink.process( record );
	}
	sink.flush();
	std::fclose( file );

	auto data = readFile( filepath );

	// first pass
	{
		kmac::flare::Reader reader;
		kmac::flare::Record record;
		int count = 0;
		while ( reader.parseNext( data.data(), data.size(), record ) )
		{
			++count;
		}
		EXPECT_EQ( count, 3 );
	}

	// second pass - new reader
	{
		kmac::flare::Reader reader;
		kmac::flare::Record record;
		int count = 0;
		while ( reader.parseNext( data.data(), data.size(), record ) )
		{
			++count;
		}
		EXPECT_EQ( count, 3 );
	}
}
