/**
 * @file test_nova_ram_sink.cpp
 * @brief Unit tests for RamSink - both external-buffer and internal-buffer modes.
 *
 * RamSink<0> (external buffer): constructed with a caller-supplied char* and size.
 * RamSink<N> (internal buffer): owns a statically-allocated N-byte buffer.
 *
 * Both modes share the same process/reset/overflow semantics; the tests for
 * each mode verify those semantics independently.
 */

#include "kmac/nova.h"
#include "kmac/nova/extras/ram_sink.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using kmac::nova::extras::RamSink;

// test tag
struct RamSinkTag {};
std::uint64_t ramSinkTimestamp() noexcept { return 12345ULL; }
NOVA_LOGGER_TRAITS( RamSinkTag, RAMSINK, true, ramSinkTimestamp );

// helper to build a minimal Record with a given message
static kmac::nova::Record makeRecord( const char* msg, std::size_t len )
{
	kmac::nova::Record r {};
	r.tag = "RAMSINK";
	r.tagId = 0;
	r.file = "test.cpp";
	r.function = "test";
	r.line = 1;
	r.timestamp = 0;
	r.message = msg;
	r.messageSize = static_cast< std::uint32_t >( len );
	return r;
}

// ============================================================================
// External-buffer mode: RamSink<0>
// ============================================================================

class NovaRamSinkExternal : public ::testing::Test
{
protected:
	static constexpr std::size_t CAPACITY = 128;
	char storage[ CAPACITY ] {};
	RamSink< 0 > sink { storage, CAPACITY };

	void SetUp() override
	{
		std::memset( storage, 0, sizeof( storage ) );
		sink.reset();
	}
};

TEST_F( NovaRamSinkExternal, InitialState )
{
	EXPECT_EQ( sink.bytesWritten(), 0u );
	EXPECT_EQ( sink.overflowCount(), 0u );
	EXPECT_EQ( sink.capacity(), CAPACITY );
	EXPECT_EQ( sink.data(), storage );
}

TEST_F( NovaRamSinkExternal, BasicWrite )
{
	const char* msg = "hello world";
	const std::size_t len = std::strlen( msg );
	auto rec = makeRecord( msg, len );

	sink.process( rec );

	EXPECT_EQ( sink.bytesWritten(), len );
	EXPECT_EQ( sink.overflowCount(), 0u );
	EXPECT_EQ( std::string( sink.data(), sink.bytesWritten() ), "hello world" );
}

TEST_F( NovaRamSinkExternal, MultipleWrites )
{
	auto rec1 = makeRecord( "foo", 3 );
	auto rec2 = makeRecord( "bar", 3 );
	auto rec3 = makeRecord( "baz", 3 );

	sink.process( rec1 );
	sink.process( rec2 );
	sink.process( rec3 );

	EXPECT_EQ( sink.bytesWritten(), 9u );
	EXPECT_EQ( sink.overflowCount(), 0u );
	EXPECT_EQ( std::string( sink.data(), sink.bytesWritten() ), "foobarbaz" );
}

TEST_F( NovaRamSinkExternal, WriteExactlyFull )
{
	std::string full( CAPACITY, 'X' );
	auto rec = makeRecord( full.c_str(), full.size() );

	sink.process( rec );

	EXPECT_EQ( sink.bytesWritten(), CAPACITY );
	EXPECT_EQ( sink.overflowCount(), 0u );
}

TEST_F( NovaRamSinkExternal, OverflowDropsRecord )
{
	std::string full( CAPACITY, 'A' );
	auto fillRec = makeRecord( full.c_str(), full.size() );
	sink.process( fillRec );

	// buffer is now full - next write must be dropped
	auto overflowRec = makeRecord( "B", 1 );
	sink.process( overflowRec );

	EXPECT_EQ( sink.bytesWritten(), CAPACITY );
	EXPECT_EQ( sink.overflowCount(), 1u );
}

TEST_F( NovaRamSinkExternal, MultipleOverflows )
{
	std::string fill( CAPACITY, 'Z' );
	auto fillRec = makeRecord( fill.c_str(), fill.size() );
	sink.process( fillRec );

	auto extra1 = makeRecord( "x", 1 );
	auto extra2 = makeRecord( "y", 1 );
	auto extra3 = makeRecord( "z", 1 );
	sink.process( extra1 );
	sink.process( extra2 );
	sink.process( extra3 );

	EXPECT_EQ( sink.overflowCount(), 3u );
}

TEST_F( NovaRamSinkExternal, ResetClearsState )
{
	auto rec = makeRecord( "data", 4 );
	sink.process( rec );
	EXPECT_EQ( sink.bytesWritten(), 4u );

	sink.reset();

	EXPECT_EQ( sink.bytesWritten(), 0u );
	EXPECT_EQ( sink.overflowCount(), 0u );
}

TEST_F( NovaRamSinkExternal, ResetAllowsReuseAfterOverflow )
{
	std::string fill( CAPACITY, 'F' );
	auto fillRec = makeRecord( fill.c_str(), fill.size() );
	sink.process( fillRec );

	auto extra = makeRecord( "DROPPED", 7 );
	sink.process( extra );
	EXPECT_EQ( sink.overflowCount(), 1u );

	sink.reset();
	EXPECT_EQ( sink.overflowCount(), 0u );
	EXPECT_EQ( sink.bytesWritten(), 0u );

	auto after = makeRecord( "ok", 2 );
	sink.process( after );
	EXPECT_EQ( sink.bytesWritten(), 2u );
	EXPECT_EQ( sink.overflowCount(), 0u );
}

TEST_F( NovaRamSinkExternal, EmptyMessageWritesZeroBytes )
{
	auto rec = makeRecord( "", 0 );
	sink.process( rec );
	EXPECT_EQ( sink.bytesWritten(), 0u );
	EXPECT_EQ( sink.overflowCount(), 0u );
}

TEST_F( NovaRamSinkExternal, ContentPreservedInOrder )
{
	// verify byte content in storage matches what was logged
	auto r1 = makeRecord( "AA", 2 );
	auto r2 = makeRecord( "BB", 2 );
	sink.process( r1 );
	sink.process( r2 );

	EXPECT_EQ( storage[ 0 ], 'A' );
	EXPECT_EQ( storage[ 1 ], 'A' );
	EXPECT_EQ( storage[ 2 ], 'B' );
	EXPECT_EQ( storage[ 3 ], 'B' );
}

TEST_F( NovaRamSinkExternal, ViaLogger )
{
	// verify the sink integrates correctly with the logger macro pipeline
	kmac::nova::ScopedConfigurator<> config;
	config.bind< RamSinkTag >( &sink );

	NOVA_LOG( RamSinkTag ) << "via macro";

	EXPECT_GT( sink.bytesWritten(), 0u );
	EXPECT_EQ( sink.overflowCount(), 0u );
	const std::string content( sink.data(), sink.bytesWritten() );
	EXPECT_NE( content.find( "via macro" ), std::string::npos );
}

// ============================================================================
// Internal-buffer mode: RamSink<N>
// ============================================================================

class NovaRamSinkInternal : public ::testing::Test
{
protected:
	static constexpr std::size_t SIZE = 64;
	RamSink< SIZE > sink;

	void SetUp() override
	{
		sink.reset();
	}
};

TEST_F( NovaRamSinkInternal, InitialState )
{
	EXPECT_EQ( sink.bytesWritten(), 0u );
	EXPECT_EQ( sink.overflowCount(), 0u );
	EXPECT_EQ( sink.capacity(), SIZE );
	EXPECT_NE( sink.data(), nullptr );
}

TEST_F( NovaRamSinkInternal, BasicWrite )
{
	auto rec = makeRecord( "internal", 8 );
	sink.process( rec );

	EXPECT_EQ( sink.bytesWritten(), 8u );
	EXPECT_EQ( std::string( sink.data(), sink.bytesWritten() ), "internal" );
}

TEST_F( NovaRamSinkInternal, OverflowDropsRecord )
{
	std::string fill( SIZE, 'I' );
	auto fillRec = makeRecord( fill.c_str(), fill.size() );
	sink.process( fillRec );

	auto extra = makeRecord( "X", 1 );
	sink.process( extra );

	EXPECT_EQ( sink.bytesWritten(), SIZE );
	EXPECT_EQ( sink.overflowCount(), 1u );
}

TEST_F( NovaRamSinkInternal, ResetClearsState )
{
	auto rec = makeRecord( "data", 4 );
	sink.process( rec );

	sink.reset();

	EXPECT_EQ( sink.bytesWritten(), 0u );
	EXPECT_EQ( sink.overflowCount(), 0u );
}

TEST_F( NovaRamSinkInternal, ResetAndReuse )
{
	auto r1 = makeRecord( "first", 5 );
	sink.process( r1 );
	sink.reset();

	auto r2 = makeRecord( "second", 6 );
	sink.process( r2 );

	EXPECT_EQ( sink.bytesWritten(), 6u );
	EXPECT_EQ( std::string( sink.data(), sink.bytesWritten() ), "second" );
}
