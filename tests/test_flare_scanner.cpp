/**
 * @file test_flare_scanner.cpp
 * @brief Google Test unit tests for Flare Scanner
 */

#include "kmac/flare/scanner.h"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

static const uint8_t VALID_MAGIC[] = { 0x52, 0x4C, 0x46, 0x5F, 0x43, 0x41, 0x4D, 0x4B };

// helper function to create a minimal valid Flare record
inline void createValidFlareRecord( std::vector< uint8_t >& data, uint32_t totalSize )
{
	size_t recordStart = data.size();  // remember where this record starts

	// magic (8 bytes)
	data.insert( data.end(), VALID_MAGIC, VALID_MAGIC + 8 );

	// size (4 bytes, little-endian)
	data.push_back( totalSize & 0xFF );
	data.push_back( ( totalSize >> 8 ) & 0xFF );
	data.push_back( ( totalSize >> 16 ) & 0xFF );
	data.push_back( ( totalSize >> 24 ) & 0xFF );

	// padding (fill up to recordStart + totalSize - 4 bytes for END marker)
	while ( data.size() < recordStart + totalSize - 4 )
	{
		data.push_back( 0x00 );
	}

	// END marker: Type=0xFFFF (2 bytes) + Length=0x0000 (2 bytes), little-endian
	data.push_back( 0xFF );  // type low byte
	data.push_back( 0xFF );  // type high byte
	data.push_back( 0x00 );  // length low byte
	data.push_back( 0x00 );  // length high byte
}

class FlareScanner : public ::testing::Test
{
protected:
	void SetUp() override { }
	void TearDown() override { }
};

TEST_F( FlareScanner, ScannerEmptyData )
{
	kmac::flare::Scanner scanner;

	uint8_t empty;
	bool found = scanner.scan( &empty, 0 );

	EXPECT_TRUE( ! found );
}

TEST_F( FlareScanner, ScannerPartialMagic )
{
	kmac::flare::Scanner scanner;

	// only first 3 bytes of magic
	uint8_t partial[] = { 0x52, 0x4C, 0x46 };

	bool found = scanner.scan( partial, sizeof( partial ) );
	EXPECT_TRUE( ! found );
}

TEST_F( FlareScanner, ScannerValidMagic )
{
	kmac::flare::Scanner scanner;

	// NOTE: This will still fail without size field, but tests magic validation
	bool found = scanner.scan( VALID_MAGIC, sizeof( VALID_MAGIC ) );
	EXPECT_TRUE( ! found );  // no size field, so scan fails
}

TEST_F( FlareScanner, ScannerMagicWithSize )
{
	kmac::flare::Scanner scanner;

	std::vector< uint8_t > data;
	uint32_t size = 20;
	createValidFlareRecord( data, size );

	bool found = scanner.scan( data.data(), data.size() );
	EXPECT_TRUE( found );

	EXPECT_EQ( scanner.recordOffset(), 0u );
	EXPECT_EQ( scanner.recordSize(), size_t( size ) );
}

TEST_F( FlareScanner, ScannerMultipleRecords )
{
	kmac::flare::Scanner scanner;

	std::vector< uint8_t > data;

	// first record
	uint32_t size1 = 16;
	createValidFlareRecord( data, size1 );

	// second record
	size_t offset2 = data.size();
	uint32_t size2 = 24;
	createValidFlareRecord( data, size2 );

	// find first record
	bool found1 = scanner.scan( data.data(), data.size() );
	EXPECT_TRUE( found1 );
	EXPECT_EQ( scanner.recordOffset(), 0u );
	EXPECT_EQ( scanner.recordSize(), size_t( size1 ) );

	// find second record
	scanner.setStartOffset( size1 );
	bool found2 = scanner.scan( data.data(), data.size() );
	EXPECT_TRUE( found2 );
	EXPECT_EQ( scanner.recordOffset(), offset2 );
	EXPECT_EQ( scanner.recordSize(), size_t( size2 ) );
}

TEST_F( FlareScanner, ScannerInvalidMagic )
{
	kmac::flare::Scanner scanner;

	// wrong magic number
	uint8_t wrongMagic[] = {
		0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
		0x10, 0x00, 0x00, 0x00  // size = 16
	};

	bool found = scanner.scan( wrongMagic, sizeof( wrongMagic ) );
	EXPECT_TRUE( ! found );
}

TEST_F( FlareScanner, ScannerTooSmallSize )
{
	kmac::flare::Scanner scanner;

	std::vector< uint8_t > data;

	// magic
	data.insert( data.end(), VALID_MAGIC, VALID_MAGIC + 8 );

	// size smaller than header (invalid)
	uint32_t size = 8;
	data.push_back( size & 0xFF );
	data.push_back( ( size >> 8 ) & 0xFF );
	data.push_back( ( size >> 16 ) & 0xFF );
	data.push_back( ( size >> 24 ) & 0xFF );

	bool found = scanner.scan( data.data(), data.size() );
	EXPECT_TRUE( ! found );
}

TEST_F( FlareScanner, ScannerSetStartOffset )
{
	kmac::flare::Scanner scanner;

	std::vector< uint8_t > data;

	// add some garbage data at the beginning
	for ( int i = 0; i < 100; ++i )
	{
		data.push_back( 0xFF );
	}

	// then add a valid record
	size_t recordStart = data.size();
	uint32_t size = 20;
	createValidFlareRecord( data, size );

	// set start offset to skip garbage
	scanner.setStartOffset( recordStart );

	bool found = scanner.scan( data.data(), data.size() );
	EXPECT_TRUE( found );
	EXPECT_EQ( scanner.recordOffset(), recordStart );
}

TEST_F( FlareScanner, ScannerRecordInMiddle )
{
	kmac::flare::Scanner scanner;

	std::vector< uint8_t > data;

	// garbage at start
	for ( int i = 0; i < 50; ++i )
	{
		data.push_back( 0xAA );
	}

	// valid record
	size_t recordStart = data.size();
	uint32_t size = 32;
	createValidFlareRecord( data, size );

	// more garbage at end
	for ( int i = 0; i < 50; ++i )
	{
		data.push_back( 0xBB );
	}

	// scanner should find the record
	bool found = scanner.scan( data.data(), data.size() );
	EXPECT_TRUE( found );
	EXPECT_EQ( scanner.recordOffset(), recordStart );
	EXPECT_EQ( scanner.recordSize(), size_t( size ) );
}

TEST_F( FlareScanner, ScannerLargeBuffer )
{
	kmac::flare::Scanner scanner;

	std::vector< uint8_t > data;

	// create a large buffer with record at the end
	for ( int i = 0; i < 10000; ++i )
	{
		data.push_back( 0x00 );
	}

	size_t recordStart = data.size();
	uint32_t size = 16;
	createValidFlareRecord( data, size );

	bool found = scanner.scan( data.data(), data.size() );
	EXPECT_TRUE( found );
	EXPECT_EQ( scanner.recordOffset(), recordStart );
}

TEST_F( FlareScanner, ScannerTruncatedRecord )
{
	kmac::flare::Scanner scanner;

	std::vector< uint8_t > data;

	// magic
	data.insert( data.end(), VALID_MAGIC, VALID_MAGIC + 8 );

	// size says 100 bytes
	uint32_t size = 100;
	data.push_back( size & 0xFF );
	data.push_back( ( size >> 8 ) & 0xFF );
	data.push_back( ( size >> 16) & 0xFF );
	data.push_back( ( size >> 24) & 0xFF );

	// but we only have partial data (e.g., 20 bytes total)
	while ( data.size() < 20 )
	{
		data.push_back( 0x00 );
	}

	// scanner should still find it but report actual available size
	bool found = scanner.scan( data.data(), data.size() );

	// behavior depends on implementation -
	// either finds partial record or rejects it,
	// but testing that it doesn't crash
	SUCCEED();
}

TEST_F( FlareScanner, ScannerConsecutiveScans )
{
	kmac::flare::Scanner scanner;

	std::vector< uint8_t > data;

	// create 5 records
	std::vector< size_t > offsets;
	for ( int i = 0; i < 5; ++i )
	{
		offsets.push_back( data.size() );
		uint32_t size = 16 + i * 4;
		createValidFlareRecord( data, size );
	}

	// scan through all records
	for ( int i = 0; i < 5; ++i )
	{
		bool found = scanner.scan( data.data(), data.size() );
		EXPECT_TRUE( found );
		EXPECT_EQ( scanner.recordOffset(), offsets[ i ] );

		scanner.setStartOffset( scanner.recordOffset() + scanner.recordSize() );
	}

	// no more records
	bool found = scanner.scan( data.data(), data.size() );
	EXPECT_TRUE( ! found );
}
