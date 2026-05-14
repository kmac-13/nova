/**
 * @file test_nova_builder_stream_std.cpp
 * @brief Unit tests for builder_stream_std.h.
 *
 * Verifies the operator<< overloads for std containers and utility types
 * against both TruncatingRecordBuilder (NOVA_LOG) and ContinuationRecordBuilder
 * (NOVA_LOG_CONT).
 *
 * Format contracts (from the header):
 *   sequence types  ->  [a, b, c]     (vector, array, list, set, unordered_set)
 *   map types       ->  {k: v, k: v}  (map, unordered_map)
 *   std::pair       ->  (a, b)
 *   std::optional   ->  <value> or <nullopt>
 */

#include "kmac/nova.h"
#include "kmac/nova/extras/builder_stream_std.h"
#include "kmac/nova/extras/continuation_logging.h"

#include <gtest/gtest.h>

#include <array>
#include <list>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// test tags
struct StdStreamTruncTag {};
std::uint64_t stdStreamTs() noexcept { return 1111ULL; }
NOVA_LOGGER_TRAITS( StdStreamTruncTag, STD_TRUNC, true, stdStreamTs );

struct StdStreamContTag {};
NOVA_LOGGER_TRAITS( StdStreamContTag, STD_CONT, true, stdStreamTs );

// ============================================================================
// Capture sink - stores messages for inspection
// ============================================================================

class CaptureSink : public kmac::nova::Sink
{
public:
	std::vector< std::string > messages;

	void process( const kmac::nova::Record& record ) noexcept override
	{
		messages.emplace_back( record.message, record.messageSize );
	}

	void clear() { messages.clear(); }

	// returns the first captured message, or empty string if none
	std::string first() const
	{
		return messages.empty() ? std::string{} : messages.front();
	}
};

// ============================================================================
// Fixture
// ============================================================================

class NovaBuilderStreamStd : public ::testing::Test
{
protected:
	CaptureSink _sink;

	void SetUp() override
	{
		_sink.clear();
	}
};

// ============================================================================
// std::vector
// ============================================================================

TEST_F( NovaBuilderStreamStd, VectorOfInt )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< StdStreamTruncTag >( &_sink );

	const std::vector< int > v = { 1, 2, 3 };
	NOVA_LOG( StdStreamTruncTag ) << v;

	EXPECT_EQ( _sink.first(), "[1, 2, 3]" );
}

TEST_F( NovaBuilderStreamStd, VectorEmpty )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< StdStreamTruncTag >( &_sink );

	const std::vector< int > v;
	NOVA_LOG( StdStreamTruncTag ) << v;

	EXPECT_EQ( _sink.first(), "[]" );
}

TEST_F( NovaBuilderStreamStd, VectorSingleElement )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< StdStreamTruncTag >( &_sink );

	const std::vector< int > v = { 42 };
	NOVA_LOG( StdStreamTruncTag ) << v;

	EXPECT_EQ( _sink.first(), "[42]" );
}

TEST_F( NovaBuilderStreamStd, VectorContinuationBuilder )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< StdStreamContTag >( &_sink );

	const std::vector< int > v = { 10, 20, 30 };
	NOVA_LOG_CONT( StdStreamContTag ) << v;

	EXPECT_NE( _sink.first().find( "[10, 20, 30]" ), std::string::npos );
}

TEST_F( NovaBuilderStreamStd, VectorWithPrefix )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< StdStreamTruncTag >( &_sink );

	const std::vector< int > v = { 7, 8, 9 };
	NOVA_LOG( StdStreamTruncTag ) << "vals=" << v;

	EXPECT_NE( _sink.first().find( "vals=[7, 8, 9]" ), std::string::npos );
}

// ============================================================================
// std::array
// ============================================================================

TEST_F( NovaBuilderStreamStd, StdArray )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< StdStreamTruncTag >( &_sink );

	const std::array< int, 3 > a = { 4, 5, 6 };
	NOVA_LOG( StdStreamTruncTag ) << a;

	EXPECT_EQ( _sink.first(), "[4, 5, 6]" );
}

TEST_F( NovaBuilderStreamStd, StdArraySingleElement )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< StdStreamTruncTag >( &_sink );

	const std::array< int, 1 > a = { 99 };
	NOVA_LOG( StdStreamTruncTag ) << a;

	EXPECT_EQ( _sink.first(), "[99]" );
}

TEST_F( NovaBuilderStreamStd, StdArrayContinuationBuilder )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< StdStreamContTag >( &_sink );

	const std::array< int, 2 > a = { 100, 200 };
	NOVA_LOG_CONT( StdStreamContTag ) << a;

	EXPECT_NE( _sink.first().find( "[100, 200]" ), std::string::npos );
}

// ============================================================================
// std::list
// ============================================================================

TEST_F( NovaBuilderStreamStd, StdList )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< StdStreamTruncTag >( &_sink );

	const std::list< int > lst = { 1, 2, 3 };
	NOVA_LOG( StdStreamTruncTag ) << lst;

	EXPECT_EQ( _sink.first(), "[1, 2, 3]" );
}

TEST_F( NovaBuilderStreamStd, StdListEmpty )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< StdStreamTruncTag >( &_sink );

	const std::list< int > lst;
	NOVA_LOG( StdStreamTruncTag ) << lst;

	EXPECT_EQ( _sink.first(), "[]" );
}

TEST_F( NovaBuilderStreamStd, StdListContinuationBuilder )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< StdStreamContTag >( &_sink );

	const std::list< int > lst = { 5, 6, 7 };
	NOVA_LOG_CONT( StdStreamContTag ) << lst;

	EXPECT_NE( _sink.first().find( "[5, 6, 7]" ), std::string::npos );
}

// ============================================================================
// std::set  (sorted, so output order is deterministic)
// ============================================================================

TEST_F( NovaBuilderStreamStd, StdSet )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< StdStreamTruncTag >( &_sink );

	const std::set< int > s = { 3, 1, 2 };
	NOVA_LOG( StdStreamTruncTag ) << s;

	// set is always sorted
	EXPECT_EQ( _sink.first(), "[1, 2, 3]" );
}

TEST_F( NovaBuilderStreamStd, StdSetEmpty )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< StdStreamTruncTag >( &_sink );

	const std::set< int > s;
	NOVA_LOG( StdStreamTruncTag ) << s;

	EXPECT_EQ( _sink.first(), "[]" );
}

TEST_F( NovaBuilderStreamStd, StdSetContinuationBuilder )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< StdStreamContTag >( &_sink );

	const std::set< int > s = { 9, 8, 7 };
	NOVA_LOG_CONT( StdStreamContTag ) << s;

	EXPECT_NE( _sink.first().find( "[7, 8, 9]" ), std::string::npos );
}

// ============================================================================
// std::unordered_set (order undefined - just check all elements present)
// ============================================================================

TEST_F( NovaBuilderStreamStd, StdUnorderedSet )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< StdStreamTruncTag >( &_sink );

	const std::unordered_set< int > s = { 1, 2, 3 };
	NOVA_LOG( StdStreamTruncTag ) << s;

	const std::string msg = _sink.first();
	EXPECT_EQ( msg.front(), '[' );
	EXPECT_EQ( msg.back(), ']' );
	EXPECT_NE( msg.find( '1' ), std::string::npos );
	EXPECT_NE( msg.find( '2' ), std::string::npos );
	EXPECT_NE( msg.find( '3' ), std::string::npos );
}

// ============================================================================
// std::map  (sorted by key - deterministic order)
// ============================================================================

TEST_F( NovaBuilderStreamStd, StdMap )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< StdStreamTruncTag >( &_sink );

	const std::map< int, int > m = { { 1, 10 }, { 2, 20 }, { 3, 30 } };
	NOVA_LOG( StdStreamTruncTag ) << m;

	EXPECT_EQ( _sink.first(), "{1: 10, 2: 20, 3: 30}" );
}

TEST_F( NovaBuilderStreamStd, StdMapEmpty )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< StdStreamTruncTag >( &_sink );

	const std::map< int, int > m;
	NOVA_LOG( StdStreamTruncTag ) << m;

	EXPECT_EQ( _sink.first(), "{}" );
}

TEST_F( NovaBuilderStreamStd, StdMapContinuationBuilder )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< StdStreamContTag >( &_sink );

	const std::map< int, int > m = { { 1, 100 }, { 2, 200 } };
	NOVA_LOG_CONT( StdStreamContTag ) << m;

	EXPECT_NE( _sink.first().find( "{1: 100, 2: 200}" ), std::string::npos );
}

// ============================================================================
// std::unordered_map (order undefined - check braces and key-value pairs)
// ============================================================================

TEST_F( NovaBuilderStreamStd, StdUnorderedMap )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< StdStreamTruncTag >( &_sink );

	const std::unordered_map< int, int > m = { { 1, 10 }, { 2, 20 } };
	NOVA_LOG( StdStreamTruncTag ) << m;

	const std::string msg = _sink.first();
	EXPECT_EQ( msg.front(), '{' );
	EXPECT_EQ( msg.back(), '}' );
	// each pair appears as "k: v" somewhere in the output
	EXPECT_NE( msg.find( "1: 10" ), std::string::npos );
	EXPECT_NE( msg.find( "2: 20" ), std::string::npos );
}

// ============================================================================
// std::pair
// ============================================================================

TEST_F( NovaBuilderStreamStd, StdPairIntInt )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< StdStreamTruncTag >( &_sink );

	const std::pair< int, int > p = { 3, 7 };
	NOVA_LOG( StdStreamTruncTag ) << p;

	EXPECT_EQ( _sink.first(), "(3, 7)" );
}

TEST_F( NovaBuilderStreamStd, StdPairContinuationBuilder )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< StdStreamContTag >( &_sink );

	const std::pair< int, int > p = { 100, 200 };
	NOVA_LOG_CONT( StdStreamContTag ) << p;

	EXPECT_NE( _sink.first().find( "(100, 200)" ), std::string::npos );
}

// ============================================================================
// std::optional  (C++17)
// ============================================================================

TEST_F( NovaBuilderStreamStd, StdOptionalWithValue )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< StdStreamTruncTag >( &_sink );

	const std::optional< int > opt = 42;
	NOVA_LOG( StdStreamTruncTag ) << opt;

	EXPECT_EQ( _sink.first(), "<42>" );
}

TEST_F( NovaBuilderStreamStd, StdOptionalNullopt )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< StdStreamTruncTag >( &_sink );

	const std::optional< int > opt;
	NOVA_LOG( StdStreamTruncTag ) << opt;

	EXPECT_EQ( _sink.first(), "<nullopt>" );
}

TEST_F( NovaBuilderStreamStd, StdOptionalContinuationBuilder )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< StdStreamContTag >( &_sink );

	const std::optional< int > opt = 99;
	NOVA_LOG_CONT( StdStreamContTag ) << opt;

	EXPECT_NE( _sink.first().find( "<99>" ), std::string::npos );
}
