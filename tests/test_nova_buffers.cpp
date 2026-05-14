/**
 * @file test_nova_buffers.cpp
 * @brief Unit tests for Buffer and TruncatingBuffer.
 *
 * Both classes are lightweight wrappers around a caller-supplied char array.
 * Buffer: all-or-nothing append - rejects writes that would overflow.
 * TruncatingBuffer: partial-fill append - writes as many bytes as fit and
 * sets a permanent truncation flag on overflow.
 */

#include "kmac/nova/extras/buffer.h"
#include "kmac/nova/extras/truncating_buffer.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using kmac::nova::extras::Buffer;
using kmac::nova::extras::TruncatingBuffer;

// ============================================================================
// Buffer tests
// ============================================================================

class NovaBuffer : public ::testing::Test
{
protected:
	static constexpr std::size_t CAPACITY = 64;
	char storage[ CAPACITY ] {};

	Buffer makeBuffer()
	{
		std::memset( storage, 0, sizeof( storage ) );
		return Buffer( storage, CAPACITY );
	}
};

TEST_F( NovaBuffer, InitialState )
{
	Buffer buf = makeBuffer();
	EXPECT_EQ( buf.size(), 0u );
	EXPECT_EQ( buf.remaining(), CAPACITY );
	EXPECT_EQ( buf.data(), storage );
}

TEST_F( NovaBuffer, AppendFitsSucceeds )
{
	Buffer buf = makeBuffer();
	const bool ok = buf.append( "hello", 5 );
	EXPECT_TRUE( ok );
	EXPECT_EQ( buf.size(), 5u );
	EXPECT_EQ( buf.remaining(), CAPACITY - 5u );
	EXPECT_EQ( std::string( buf.data(), buf.size() ), "hello" );
}

TEST_F( NovaBuffer, AppendMultipleSucceeds )
{
	Buffer buf = makeBuffer();
	EXPECT_TRUE( buf.append( "foo", 3 ) );
	EXPECT_TRUE( buf.append( "bar", 3 ) );
	EXPECT_EQ( buf.size(), 6u );
	EXPECT_EQ( std::string( buf.data(), buf.size() ), "foobar" );
}

TEST_F( NovaBuffer, AppendExactlyFull )
{
	Buffer buf = makeBuffer();
	std::string full( CAPACITY, 'X' );
	EXPECT_TRUE( buf.append( full.c_str(), full.size() ) );
	EXPECT_EQ( buf.size(), CAPACITY );
	EXPECT_EQ( buf.remaining(), 0u );
}

TEST_F( NovaBuffer, AppendOverflowRejected )
{
	Buffer buf = makeBuffer();
	std::string tooBig( CAPACITY + 1, 'X' );
	const bool ok = buf.append( tooBig.c_str(), tooBig.size() );
	EXPECT_FALSE( ok );
	// all-or-nothing: nothing should have been written
	EXPECT_EQ( buf.size(), 0u );
}

TEST_F( NovaBuffer, AppendAfterPartialFill_OverflowRejected )
{
	Buffer buf = makeBuffer();
	EXPECT_TRUE( buf.append( "hello", 5 ) );

	// try to write more than the remaining space
	std::string tooBig( CAPACITY, 'X' );
	const bool ok = buf.append( tooBig.c_str(), tooBig.size() );
	EXPECT_FALSE( ok );
	// size must not have changed
	EXPECT_EQ( buf.size(), 5u );
}

TEST_F( NovaBuffer, AppendCharSucceeds )
{
	Buffer buf = makeBuffer();
	EXPECT_TRUE( buf.appendChar( 'A' ) );
	EXPECT_EQ( buf.size(), 1u );
	EXPECT_EQ( storage[ 0 ], 'A' );
}

TEST_F( NovaBuffer, AppendCharFull )
{
	Buffer buf = makeBuffer();
	std::string full( CAPACITY, 'Z' );
	buf.append( full.c_str(), full.size() );
	EXPECT_FALSE( buf.appendChar( 'X' ) );
	EXPECT_EQ( buf.size(), CAPACITY );
}

TEST_F( NovaBuffer, AppendLiteralSucceeds )
{
	Buffer buf = makeBuffer();
	EXPECT_TRUE( buf.appendLiteral( "hello" ) );
	EXPECT_EQ( buf.size(), 5u );
	EXPECT_EQ( std::string( buf.data(), buf.size() ), "hello" );
}

TEST_F( NovaBuffer, AppendLiteralExcludesNullTerminator )
{
	Buffer buf = makeBuffer();
	buf.appendLiteral( "hi" );
	// should be 2 bytes, not 3 (no null terminator)
	EXPECT_EQ( buf.size(), 2u );
}

TEST_F( NovaBuffer, AppendEmptySucceeds )
{
	Buffer buf = makeBuffer();
	EXPECT_TRUE( buf.append( "", 0 ) );
	EXPECT_EQ( buf.size(), 0u );
}

TEST_F( NovaBuffer, RemainingTrackedCorrectly )
{
	Buffer buf = makeBuffer();
	EXPECT_EQ( buf.remaining(), CAPACITY );
	buf.append( "abc", 3 );
	EXPECT_EQ( buf.remaining(), CAPACITY - 3u );
	buf.appendChar( 'X' );
	EXPECT_EQ( buf.remaining(), CAPACITY - 4u );
}

// ============================================================================
// TruncatingBuffer tests
// ============================================================================

class NovaTruncatingBuffer : public ::testing::Test
{
protected:
	static constexpr std::size_t CAPACITY = 32;
	char storage[ CAPACITY ] {};

	TruncatingBuffer makeBuffer()
	{
		std::memset( storage, 0, sizeof( storage ) );
		return TruncatingBuffer( storage, CAPACITY );
	}
};

TEST_F( NovaTruncatingBuffer, InitialState )
{
	TruncatingBuffer buf = makeBuffer();
	EXPECT_EQ( buf.size(), 0u );
	EXPECT_EQ( buf.remaining(), CAPACITY );
	EXPECT_FALSE( buf.truncated() );
}

TEST_F( NovaTruncatingBuffer, AppendFitsSucceeds )
{
	TruncatingBuffer buf = makeBuffer();
	EXPECT_TRUE( buf.append( "hello", 5 ) );
	EXPECT_EQ( buf.size(), 5u );
	EXPECT_FALSE( buf.truncated() );
}

TEST_F( NovaTruncatingBuffer, AppendFillsPartialOnOverflow )
{
	TruncatingBuffer buf = makeBuffer();
	// write up to 1 byte before full
	std::string almostFull( CAPACITY - 1, 'A' );
	EXPECT_TRUE( buf.append( almostFull.c_str(), almostFull.size() ) );

	// now try to append 4 bytes - only 1 byte fits
	const bool ok = buf.append( "XXXX", 4 );
	EXPECT_TRUE( ok );
	EXPECT_TRUE( buf.truncated() );

	// 1 byte should have been written (as much as fit)
	EXPECT_EQ( buf.size(), CAPACITY );
}

TEST_F( NovaTruncatingBuffer, TruncationFlagPersists )
{
	TruncatingBuffer buf = makeBuffer();
	std::string full( CAPACITY + 10, 'B' );
	buf.append( full.c_str(), full.size() );
	EXPECT_TRUE( buf.truncated() );

	// subsequent appends also fail and flag stays set
	EXPECT_FALSE( buf.appendChar( 'X' ) );
	EXPECT_TRUE( buf.truncated() );
}

TEST_F( NovaTruncatingBuffer, AppendCharSucceeds )
{
	TruncatingBuffer buf = makeBuffer();
	EXPECT_TRUE( buf.appendChar( 'Z' ) );
	EXPECT_EQ( buf.size(), 1u );
	EXPECT_EQ( storage[ 0 ], 'Z' );
	EXPECT_FALSE( buf.truncated() );
}

TEST_F( NovaTruncatingBuffer, AppendCharTruncates )
{
	TruncatingBuffer buf = makeBuffer();
	std::string full( CAPACITY, 'C' );
	buf.append( full.c_str(), full.size() );

	EXPECT_FALSE( buf.appendChar( 'D' ) );
	EXPECT_TRUE( buf.truncated() );
}

TEST_F( NovaTruncatingBuffer, MultipleAppendsWithinCapacity )
{
	TruncatingBuffer buf = makeBuffer();
	EXPECT_TRUE( buf.append( "foo", 3 ) );
	EXPECT_TRUE( buf.append( "bar", 3 ) );
	EXPECT_TRUE( buf.append( "baz", 3 ) );
	EXPECT_EQ( buf.size(), 9u );
	EXPECT_FALSE( buf.truncated() );
	EXPECT_EQ( std::string( buf.data(), buf.size() ), "foobarbaz" );
}

TEST_F( NovaTruncatingBuffer, AppendEmptyDoesNotTruncate )
{
	TruncatingBuffer buf = makeBuffer();
	EXPECT_TRUE( buf.append( "", 0 ) );
	EXPECT_FALSE( buf.truncated() );
	EXPECT_EQ( buf.size(), 0u );
}

TEST_F( NovaTruncatingBuffer, ExactlyFullIsNotTruncated )
{
	TruncatingBuffer buf = makeBuffer();
	std::string full( CAPACITY, 'F' );
	EXPECT_TRUE( buf.append( full.c_str(), full.size() ) );
	EXPECT_EQ( buf.size(), CAPACITY );
	EXPECT_EQ( buf.remaining(), 0u );
	EXPECT_FALSE( buf.truncated() );
}

TEST_F( NovaTruncatingBuffer, RemainingTrackedCorrectly )
{
	TruncatingBuffer buf = makeBuffer();
	EXPECT_EQ( buf.remaining(), CAPACITY );
	buf.append( "abc", 3 );
	EXPECT_EQ( buf.remaining(), CAPACITY - 3u );
}
