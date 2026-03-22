/**
 * @file test_nova_logger.cpp
 * @brief Google Test unit tests for Nova logger macros
 */

#include "kmac/nova/nova.h"
#include "kmac/nova/scoped_configurator.h"
#include "kmac/nova/extras/continuation_logging.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

// test tags
struct MacroTag { };
std::uint64_t macroTimestamp() noexcept { return 11111ULL; }
NOVA_LOGGER_TRAITS( MacroTag, MACRO, true, macroTimestamp );

struct StreamTag { };
std::uint64_t streamTimestamp() noexcept { return 22222ULL; }
NOVA_LOGGER_TRAITS( StreamTag, STREAM, true, streamTimestamp );


// capture sink
class MacroCaptureSink : public kmac::nova::Sink
{
public:
	std::vector< kmac::nova::Record > records;
	std::vector< std::string > storage;

	void process( const kmac::nova::Record& record ) override
	{
		// store the message string
		storage.emplace_back( record.message, record.messageSize );

		// reserve space to prevent reallocation invalidating pointers
		if ( storage.capacity() == storage.size() )
		{
			storage.reserve( storage.capacity() * 2 );
		}

		// create a copy of the record with pointer to stored message
		kmac::nova::Record copy = record;
		copy.messageSize = static_cast< std::uint32_t >( storage.back().size() );
		copy.message = storage.back().c_str();
		records.push_back( copy );
	}

	void clear()
	{
		records.clear();
		storage.clear();
	}

	std::string getMessage( size_t index ) const
	{
		if ( index >= storage.size() )
		{
			return "";
		}
		return storage[ index ];  // return the stored string directly
	}
};

class NovaLogger : public ::testing::Test
{
protected:
	MacroCaptureSink sink;

	void SetUp() override
	{
		sink.clear();
	}
};

TEST_F( NovaLogger, NOVA_LOG_Basic )
{
	kmac::nova::ScopedConfigurator config;
	config.bind< MacroTag >( &sink );

	NOVA_LOG( MacroTag ) << "test message";

	EXPECT_EQ( sink.records.size(), 1u );
	EXPECT_NE( sink.getMessage( 0 ).find( "test message" ), std::string::npos );
}

TEST_F( NovaLogger, NOVA_LOG_CONT_Basic )
{
	kmac::nova::ScopedConfigurator config;
	config.bind< MacroTag >( &sink );

	NOVA_LOG_CONT( MacroTag ) << "continuation test";

	EXPECT_EQ( sink.records.size(), 1u );
	EXPECT_NE( sink.getMessage( 0 ).find( "continuation test" ), std::string::npos );
}

TEST_F( NovaLogger, MacroWithNumbers )
{
	kmac::nova::ScopedConfigurator config;
	config.bind< MacroTag >( &sink );

	NOVA_LOG( MacroTag ) << "value: " << 42;

	EXPECT_EQ( sink.records.size(), 1u );
	EXPECT_NE( sink.getMessage( 0 ).find( "42" ), std::string::npos );
}

TEST_F( NovaLogger, MacroWithMultipleValues )
{
	kmac::nova::ScopedConfigurator config;
	config.bind< MacroTag >( &sink );

	int x = 10;
	double y = 20.5;
	const char* z = "test";

	NOVA_LOG( MacroTag ) << "x=" << x << " y=" << y << " z=" << z;

	EXPECT_EQ( sink.records.size(), 1u );
	std::string msg = sink.getMessage( 0 );
	EXPECT_NE( msg.find( "10" ), std::string::npos );
	EXPECT_NE( msg.find( "20.5" ), std::string::npos );
	EXPECT_NE( msg.find( "test" ), std::string::npos );
}

TEST_F( NovaLogger, MacroCaptures__FILE__ )
{
	kmac::nova::ScopedConfigurator config;
	config.bind< MacroTag >( &sink );

	NOVA_LOG( MacroTag ) << "file test";

	EXPECT_EQ( sink.records.size(), 1u );
	EXPECT_NE( std::string( sink.records[ 0 ].file ).find( "test_nova_logger.cpp" ), std::string::npos );
}

TEST_F( NovaLogger, MacroCaptures__FUNCTION__ )
{
	kmac::nova::ScopedConfigurator config;
	config.bind< MacroTag >( &sink );

	NOVA_LOG( MacroTag ) << "function test";

	EXPECT_EQ( sink.records.size(), 1u );
	// gtest generates function names like "NovaLogger_MacroCaptures__FUNCTION___Test::TestBody",
	// just verify function field is not empty
	EXPECT_GT( std::string( sink.records[ 0 ].function ).length(), 0u );
	EXPECT_NE( std::string( sink.records[ 0 ].message, sink.records[ 0 ].messageSize ).find( "function test" ), std::string::npos );
}

TEST_F( NovaLogger, MacroCaptures__LINE__ )
{
	kmac::nova::ScopedConfigurator config;
	config.bind< MacroTag >( &sink );

	const uint32_t expectedLine = __LINE__ + 1;
	NOVA_LOG( MacroTag ) << "line test";

	EXPECT_EQ( sink.records.size(), 1u );
	EXPECT_EQ( sink.records[ 0 ].line, expectedLine );
}

TEST_F( NovaLogger, MultipleMacroCallsInSequence )
{
	kmac::nova::ScopedConfigurator config;
	config.bind< MacroTag >( &sink );

	NOVA_LOG( MacroTag ) << "first";
	NOVA_LOG( MacroTag ) << "second";
	NOVA_LOG( MacroTag ) << "third";

	EXPECT_EQ( sink.records.size(), 3u );
	EXPECT_NE( sink.getMessage( 0 ).find( "first" ), std::string::npos );
	EXPECT_NE( sink.getMessage( 1 ).find( "second" ), std::string::npos );
	EXPECT_NE( sink.getMessage( 2 ).find( "third" ), std::string::npos );
}

TEST_F( NovaLogger, MacroWithDifferentTags )
{
	MacroCaptureSink sink1;
	MacroCaptureSink sink2;

	kmac::nova::ScopedConfigurator config;
	config.bind< MacroTag >( &sink1 );
	config.bind< StreamTag >( &sink2 );

	NOVA_LOG( MacroTag ) << "macro tag";
	NOVA_LOG( StreamTag ) << "stream tag";

	EXPECT_EQ( sink1.records.size(), 1u );
	EXPECT_EQ( sink2.records.size(), 1u );
	EXPECT_STREQ( sink1.records[ 0 ].tag, "MACRO" );
	EXPECT_STREQ( sink2.records[ 0 ].tag, "STREAM" );
}

TEST_F( NovaLogger, MacroStreamingInterface )
{
	kmac::nova::ScopedConfigurator config;
	config.bind< MacroTag >( &sink );

	// test streaming various types
	NOVA_LOG( MacroTag )
		<< "string" << ' '
		<< 123 << ' '
		<< 4.56 << ' '
		<< true;

	EXPECT_EQ( sink.records.size(), 1u );
	std::string msg = sink.getMessage( 0 );
	EXPECT_NE( msg.find( "string" ), std::string::npos );
	EXPECT_NE( msg.find( "123" ), std::string::npos );
	EXPECT_NE( msg.find( "4.56" ), std::string::npos );
}

TEST_F( NovaLogger, MacroWithoutSink )
{
	// no sink bound and should not crash
	NOVA_LOG( MacroTag ) << "no sink";
	NOVA_LOG_CONT( MacroTag ) << "still no sink";

	// test passes if no crash occurred
	SUCCEED();
}

TEST_F( NovaLogger, EmptyMacroCall )
{
	kmac::nova::ScopedConfigurator config;
	config.bind< MacroTag >( &sink );

	NOVA_LOG( MacroTag );

	EXPECT_EQ( sink.records.size(), 1u );
	EXPECT_EQ( sink.records[ 0 ].messageSize, 0u );
}

TEST_F( NovaLogger, MacroInConditional )
{
	kmac::nova::ScopedConfigurator config;
	config.bind< MacroTag >( &sink );

	bool condition = true;
	if ( condition )
	{
		NOVA_LOG( MacroTag ) << "conditional true";
	}

	EXPECT_EQ( sink.records.size(), 1u );

	condition = false;
	if ( condition )
	{
		NOVA_LOG( MacroTag ) << "conditional false";
	}

	// still just 1 record
	EXPECT_EQ( sink.records.size(), 1u );
}

TEST_F( NovaLogger, MacroInLoop )
{
	kmac::nova::ScopedConfigurator config;
	config.bind< MacroTag >( &sink );

	for ( int i = 0; i < 5; ++i )
	{
		NOVA_LOG( MacroTag ) << "iteration " << i;
	}

	EXPECT_EQ( sink.records.size(), 5u );
	for ( size_t i = 0; i < 5; ++i )
	{
		EXPECT_NE( sink.getMessage( i ).find( std::to_string( i ) ), std::string::npos );
	}
}
