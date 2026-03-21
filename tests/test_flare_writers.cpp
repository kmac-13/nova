/**
 * @file test_flare_writers.cpp
 * @brief Unit tests for RamWriter and UartWriter IWriter implementations.
 */

#include "kmac/flare/emergency_sink.h"
#include "kmac/flare/ram_writer.h"
#include "kmac/flare/uart_writer.h"
#include "kmac/nova/nova.h"
#include "kmac/nova/scoped_configurator.h"

#include <gtest/gtest.h>

#include <cstring>

// tags used in EmergencySink integration tests
struct RamTestTag {};
struct UartTestTag {};
NOVA_LOGGER_TRAITS( RamTestTag, RAMTEST, true, ::kmac::nova::TimestampHelper::steadyNanosecs );
NOVA_LOGGER_TRAITS( UartTestTag, UARTTEST, true, ::kmac::nova::TimestampHelper::steadyNanosecs );

// ============================================================================
// RamWriter tests
// ============================================================================

class FlareRamWriter : public ::testing::Test
{
protected:
	static constexpr std::size_t CAPACITY = 64;
	std::uint8_t buf[ CAPACITY ] = {};
	kmac::flare::RamWriter writer{ buf, CAPACITY };
};

TEST_F( FlareRamWriter, InitialState )
{
	EXPECT_EQ( writer.bytesWritten(), 0u );
	EXPECT_FALSE( writer.isFull() );
	EXPECT_EQ( writer.data(), buf );
}

TEST_F( FlareRamWriter, WriteSmall )
{
	const char data[] = "hello";
	const std::size_t len = std::strlen( data );

	const std::size_t written = writer.write( data, len );

	EXPECT_EQ( written, len );
	EXPECT_EQ( writer.bytesWritten(), len );
	EXPECT_FALSE( writer.isFull() );
	EXPECT_EQ( std::memcmp( writer.data(), data, len ), 0 );
}

TEST_F( FlareRamWriter, WriteMultiple )
{
	const std::size_t written1 = writer.write( "abc", 3 );
	const std::size_t written2 = writer.write( "defg", 4 );

	EXPECT_EQ( written1, 3u );
	EXPECT_EQ( written2, 4u );
	EXPECT_EQ( writer.bytesWritten(), 7u );
	EXPECT_EQ( std::memcmp( writer.data(), "abcdefg", 7 ), 0 );
}

TEST_F( FlareRamWriter, WriteExactCapacity )
{
	std::uint8_t src[ CAPACITY ] = {};
	std::memset( src, 0xAB, CAPACITY );

	const std::size_t written = writer.write( src, CAPACITY );

	EXPECT_EQ( written, CAPACITY );
	EXPECT_EQ( writer.bytesWritten(), CAPACITY );
	EXPECT_TRUE( writer.isFull() );
	EXPECT_EQ( std::memcmp( writer.data(), src, CAPACITY ), 0 );
}

TEST_F( FlareRamWriter, WriteTruncatesAtCapacity )
{
	// fill to within 4 bytes of capacity
	std::uint8_t src[ CAPACITY ] = {};
	writer.write( src, CAPACITY - 4 );

	// attempt to write 8 bytes, but only 4 should fit
	const std::size_t written = writer.write( src, 8 );

	EXPECT_EQ( written, 4u );
	EXPECT_EQ( writer.bytesWritten(), CAPACITY );
	EXPECT_TRUE( writer.isFull() );
}

TEST_F( FlareRamWriter, WriteWhenFullReturnsZero )
{
	std::uint8_t src[ CAPACITY ] = {};
	writer.write( src, CAPACITY );

	const std::size_t written = writer.write( "more", 4 );

	EXPECT_EQ( written, 0u );
	EXPECT_EQ( writer.bytesWritten(), CAPACITY );
}

TEST_F( FlareRamWriter, Reset )
{
	writer.write( "hello", 5 );
	EXPECT_EQ( writer.bytesWritten(), 5u );

	writer.reset();

	EXPECT_EQ( writer.bytesWritten(), 0u );
	EXPECT_FALSE( writer.isFull() );

	// can write again after reset
	const std::size_t written = writer.write( "world", 5 );
	EXPECT_EQ( written, 5u );
	EXPECT_EQ( std::memcmp( writer.data(), "world", 5 ), 0 );
}

TEST_F( FlareRamWriter, FlushIsNoOp )
{
	writer.write( "data", 4 );
	writer.flush();  // must not crash or alter state
	EXPECT_EQ( writer.bytesWritten(), 4u );
}

TEST_F( FlareRamWriter, ZeroSizeWrite )
{
	const std::size_t written = writer.write( "data", 0 );
	EXPECT_EQ( written, 0u );
	EXPECT_EQ( writer.bytesWritten(), 0u );
}

TEST_F( FlareRamWriter, WorksWithEmergencySink )
{
	kmac::flare::EmergencySink sink( &writer );
	kmac::nova::ScopedConfigurator<> cfg;
	cfg.bind< RamTestTag >( &sink );

	NOVA_LOG_STACK( RamTestTag ) << "crash log via ram";

	EXPECT_GT( writer.bytesWritten(), 0u );
}

// ============================================================================
// UartWriter tests
// ============================================================================

class FlareUartWriter : public ::testing::Test
{
protected:
	// capture buffer for the write callback
	static char uartBuf[ 256 ];
	static std::size_t uartPos;
	static bool flushCalled;

	static std::size_t captureWrite( const void* data, std::size_t size ) noexcept
	{
		const std::size_t available = sizeof( uartBuf ) - uartPos;
		const std::size_t toWrite = size < available ? size : available;
		std::memcpy( uartBuf + uartPos, data, toWrite );
		uartPos += toWrite;
		return toWrite;
	}

	static void captureFlush() noexcept
	{
		flushCalled = true;
	}

	void SetUp() override
	{
		std::memset( uartBuf, 0, sizeof( uartBuf ) );
		uartPos = 0;
		flushCalled = false;
	}
};

char FlareUartWriter::uartBuf[ 256 ] = {};
std::size_t FlareUartWriter::uartPos = 0;
bool FlareUartWriter::flushCalled = false;

TEST_F( FlareUartWriter, WriteInvokesCallback )
{
	kmac::flare::UartWriter writer( captureWrite );

	const std::size_t written = writer.write( "hello", 5 );

	EXPECT_EQ( written, 5u );
	EXPECT_EQ( uartPos, 5u );
	EXPECT_EQ( std::memcmp( uartBuf, "hello", 5 ), 0 );
}

TEST_F( FlareUartWriter, FlushInvokesCallback )
{
	kmac::flare::UartWriter writer( captureWrite, captureFlush );

	writer.write( "data", 4 );
	EXPECT_FALSE( flushCalled );

	writer.flush();
	EXPECT_TRUE( flushCalled );
}

TEST_F( FlareUartWriter, FlushNoOpWithoutCallback )
{
	kmac::flare::UartWriter writer( captureWrite );  // no flush callback

	writer.flush();  // must not crash
	EXPECT_FALSE( flushCalled );
}

TEST_F( FlareUartWriter, NullWriteFnReturnsZero )
{
	kmac::flare::UartWriter writer( nullptr );

	const std::size_t written = writer.write( "data", 4 );

	EXPECT_EQ( written, 0u );
	EXPECT_EQ( uartPos, 0u );
}

TEST_F( FlareUartWriter, ZeroSizeWriteReturnsZero )
{
	kmac::flare::UartWriter writer( captureWrite );

	const std::size_t written = writer.write( "data", 0 );

	EXPECT_EQ( written, 0u );
	EXPECT_EQ( uartPos, 0u );
}

TEST_F( FlareUartWriter, ShortWritePropagated )
{
	// callback that only accepts 3 bytes at a time
	auto limitedWrite = []( const void* data, std::size_t size ) noexcept -> std::size_t
	{
		const std::size_t toWrite = size < 3 ? size : 3;
		std::memcpy( uartBuf + uartPos, data, toWrite );
		uartPos += toWrite;
		return toWrite;
	};

	kmac::flare::UartWriter writer( limitedWrite );

	const std::size_t written = writer.write( "hello", 5 );

	// writer returns exactly what the callback returned
	EXPECT_EQ( written, 3u );
}

TEST_F( FlareUartWriter, WorksWithEmergencySink )
{
	kmac::flare::UartWriter writer( captureWrite, captureFlush );
	kmac::flare::EmergencySink sink( &writer );
	kmac::nova::ScopedConfigurator<> cfg;
	cfg.bind< UartTestTag >( &sink );

	NOVA_LOG_STACK( UartTestTag ) << "crash log via uart";
	sink.flush();

	EXPECT_GT( uartPos, 0u );
	EXPECT_TRUE( flushCalled );
}
