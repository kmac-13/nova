/**
 * @file test_flare_emergency.cpp
 * @brief Google Test unit tests for Flare EmergencySink
 */

#include "test_helpers.h"

#include "kmac/flare/emergency_sink.h"
#include "kmac/flare/file_writer.h"
#include "kmac/nova/nova.h"
#include "kmac/nova/scoped_configurator.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

// test tag
struct EmergencyTag { };
std::uint64_t tagTimestamp() noexcept { return 1704067200000000000ULL; }
NOVA_LOGGER_TRAITS( EmergencyTag, EMERGENCY, true, tagTimestamp );


class FlareEmergency : public ::testing::Test
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
};

TEST_F( FlareEmergency, EmergencySinkConstruction )
{
	std::string filepath = getTempFilePath( "test_construct.flare" );
	std::FILE* file = std::fopen( filepath.c_str(), "wb" );
	ASSERT_NE( file, nullptr );

	kmac::flare::FileWriter writer( file );
	kmac::flare::EmergencySink sink( &writer );

	std::fclose( file );
}

TEST_F( FlareEmergency, EmergencySinkWriteSingleRecord )
{
	std::string filepath = getTempFilePath( "test_single.flare" );
	std::FILE* file = std::fopen( filepath.c_str(), "wb" );
	ASSERT_NE( file, nullptr );

	kmac::flare::FileWriter writer( file );
	kmac::flare::EmergencySink sink( &writer );

	kmac::nova::Record record {
		"TEST",
		0,
		"file.cpp",
		"testFunction",
		42,
		1234567890ULL,
		"test message",
		12
	};

	sink.process( record );
	sink.flush();

	std::fclose( file );

	// verify file was created and has content
	struct stat st;
	EXPECT_EQ( stat( filepath.c_str(), &st ), 0 );
	EXPECT_GT( st.st_size, 0 );
}

TEST_F(FlareEmergency, EmergencySinkMultipleRecords)
{
	std::string filepath = getTempFilePath( "test_multiple.flare" );
	std::FILE* file = std::fopen( filepath.c_str(), "wb" );
	ASSERT_NE( file, nullptr );

	kmac::flare::FileWriter writer( file );
	kmac::flare::EmergencySink sink( &writer );

	for ( int i = 0; i < 10; ++i )
	{
		std::string msg = "message " + std::to_string( i );
		kmac::nova::Record record {
			"TEST",
			0,
			"file.cpp",
			"testFunction",
			uint32_t( 100 + i ),
			std::uint64_t( 1000 + i ),
			msg.c_str(),
			msg.size()
		};
		sink.process( record );
	}

	sink.flush();
	std::fclose( file );

	struct stat st;
	EXPECT_EQ( stat( filepath.c_str(), &st ), 0 );
	EXPECT_GT( st.st_size, 0 );
}

TEST_F( FlareEmergency, EmergencySinkWithLogger )
{
	std::string filepath = getTempFilePath( "test_logger.flare" );
	std::FILE* file = std::fopen( filepath.c_str(), "wb" );
	ASSERT_NE( file, nullptr );

	kmac::flare::FileWriter writer( file );
	kmac::flare::EmergencySink sink( &writer );

	kmac::nova::ScopedConfigurator config;
	config.bind< EmergencyTag >( &sink );

	// NOTE: using the stack-based stream macros to avoid TLS in emergency contexts
	NOVA_LOG_STACK( EmergencyTag ) << "emergency log 1";
	NOVA_LOG_STACK( EmergencyTag ) << "emergency log 2";
	NOVA_LOG_STACK( EmergencyTag ) << "emergency log 3";

	sink.flush();
	std::fclose( file );

	struct stat st;
	EXPECT_EQ( stat( filepath.c_str(), &st ), 0 );
	EXPECT_GT( st.st_size, 0 );
}

TEST_F( FlareEmergency, EmergencySinkLargeMessage )
{
	std::string filepath = getTempFilePath( "test_large.flare" );
	std::FILE* file = std::fopen( filepath.c_str(), "wb" );
	ASSERT_NE( file, nullptr );

	kmac::flare::FileWriter writer( file );
	kmac::flare::EmergencySink sink( &writer );

	// create a very large message
	std::string largeMsg( 10000, 'X' );
	kmac::nova::Record record {
		"TEST",
		0,
		"file.cpp",
		"testFunction",
		42,
		1234567890ULL,
		largeMsg.c_str(),
		largeMsg.size()
	};

	sink.process( record );
	sink.flush();

	std::fclose( file );

	struct stat st;
	EXPECT_EQ( stat( filepath.c_str(), &st ), 0 );
	EXPECT_GT( st.st_size, 0 );
}

TEST_F( FlareEmergency, EmergencySinkFlushBehavior )
{
	std::string filepath = getTempFilePath( "test_flush.flare" );
	std::FILE* file = std::fopen( filepath.c_str(), "wb" );
	ASSERT_NE( file, nullptr );

	kmac::flare::FileWriter writer( file );
	kmac::flare::EmergencySink sink( &writer );

	kmac::nova::Record record {
		"TEST",
		0,
		"file.cpp",
		"testFunction",
		42,
		1234567890ULL,
		"test message",
		12
	};

	sink.process( record );

	// don't flush think sink yet, but flush the file and check position
	std::fflush( file );
	long pos = std::ftell( file );
	EXPECT_GT( pos, 0 );  // data should have been written

	sink.flush();
	std::fclose( file );
}

TEST_F( FlareEmergency, EmergencySinkEmptyMessage )
{
	std::string filepath = getTempFilePath( "test_empty.flare" );
	std::FILE* file = std::fopen( filepath.c_str(), "wb" );
	ASSERT_NE( file, nullptr );

	kmac::flare::FileWriter writer( file );
	kmac::flare::EmergencySink sink( &writer );

	kmac::nova::Record record {
		"TEST",
		0,
		"file.cpp",
		"testFunction",
		42,
		1234567890ULL,
		"",
		0
	};

	sink.process( record );
	sink.flush();

	std::fclose( file );

	struct stat st;
	EXPECT_EQ( stat( filepath.c_str(), &st ), 0 );
	EXPECT_GT( st.st_size, 0 );
}

TEST_F( FlareEmergency, EmergencySinkNullPointers )
{
	std::string filepath = getTempFilePath( "test_null.flare" );
	std::FILE* file = std::fopen( filepath.c_str(), "wb" );
	ASSERT_NE( file, nullptr );

	kmac::flare::FileWriter writer( file );
	kmac::flare::EmergencySink sink( &writer );

	// test with empty strings (not null pointers, as Record requires valid pointers)
	kmac::nova::Record record {
		"",
		0,
		"",
		"",
		0,
		0ULL,
		"",
		0
	};

	sink.process( record );
	sink.flush();

	std::fclose( file );

	struct stat st;
	EXPECT_EQ( stat( filepath.c_str(), &st ), 0 );
}

TEST_F( FlareEmergency, EmergencySinkSequentialWrites )
{
	std::string filepath = getTempFilePath( "test_sequential.flare" );
	std::FILE* file = std::fopen( filepath.c_str(), "wb" );
	ASSERT_NE( file, nullptr );

	kmac::flare::FileWriter writer( file );
	kmac::flare::EmergencySink sink( &writer );

	// write records with different timestamps
	for ( int i = 0; i < 5; ++i )
	{
		std::string msg = "sequential message " + std::to_string( i );
		kmac::nova::Record record {
			"SEQ",
			0,
			"file.cpp",
			"func",
			uint32_t( 100 + i ),
			std::uint64_t( 1000 * ( i + 1 ) ),
			msg.c_str(),
			msg.size()
		};
		sink.process( record );
	}

	sink.flush();
	std::fclose( file );

	struct stat st;
	EXPECT_EQ( stat(filepath.c_str(), &st ), 0 );
	EXPECT_GT( st.st_size, 0 );
}

TEST_F( FlareEmergency, EmergencySinkWithProcessInfo )
{
	std::string filepath = getTempFilePath( "test_process_info.flare" );
	std::FILE* file = std::fopen( filepath.c_str(), "wb" );
	ASSERT_NE( file, nullptr );

	kmac::flare::FileWriter writer( file );
	kmac::flare::EmergencySink sink( &writer );

	kmac::nova::Record record {
		"TEST",
		0,
		"file.cpp",
		"testFunction",
		42,
		1234567890ULL,
		"with process info",
		17
	};

	sink.process( record );
	sink.flush();

	std::fclose( file );

	struct stat st;
	EXPECT_EQ( stat( filepath.c_str(), &st ), 0 );
	EXPECT_GT( st.st_size, 0 );
}

TEST_F( FlareEmergency, EmergencySinkWithoutProcessInfo )
{
	std::string filepath = getTempFilePath( "test_no_process_info.flare" );
	std::FILE* file = std::fopen( filepath.c_str(), "wb" );
	ASSERT_NE( file, nullptr );

	kmac::flare::FileWriter writer( file );
	kmac::flare::EmergencySink sink( &writer );

	kmac::nova::Record record {
		"TEST",
		0,
		"file.cpp",
		"testFunction",
		42,
		1234567890ULL,
		"without process info",
		20
	};

	sink.process( record );
	sink.flush();

	std::fclose( file );

	struct stat st;
	EXPECT_EQ( stat( filepath.c_str(), &st ), 0 );
	EXPECT_GT( st.st_size, 0 );
}
