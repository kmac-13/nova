/**
 * @file test_nova_core.cpp
 * @brief Google Test unit tests for Nova core functionality
 */

#include "kmac/nova/logger.h"
#include "kmac/nova/logger_traits.h"
#include "kmac/nova/record.h"
#include "kmac/nova/sink.h"
#include "kmac/nova/scoped_configurator.h"
#include "kmac/nova/timestamp_helper.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

// test tags
struct TestTag1 { };
struct TestTag2 { };
struct DisabledTag { };

// capture sink for testing
class CaptureSink : public kmac::nova::Sink
{
public:
	struct CapturedRecord
	{
		std::string tag;
		std::string file;
		std::string function;
		uint32_t line;
		uint64_t timestamp;
		std::string message;
	};

	std::vector< CapturedRecord > records;

	void process( const kmac::nova::Record& record ) override
	{
		CapturedRecord captured;
		captured.tag = std::string( record.tag );
		captured.file = std::string( record.file );
		captured.function = std::string( record.function );
		captured.line = record.line;
		captured.timestamp = record.timestamp;
		captured.message = std::string( record.message, record.messageSize );

		records.push_back( std::move( captured ) );
	}

	void clear()
	{
		records.clear();
	}
};

// define logger traits for test tags
std::uint64_t timestamp1() noexcept { return 12345ULL; }
NOVA_LOGGER_TRAITS( TestTag1, TEST1, true, timestamp1 );
std::uint64_t timestamp2() noexcept { return 67890ULL; }
NOVA_LOGGER_TRAITS( TestTag2, TEST2, true, timestamp2 );
std::uint64_t timestamp3() noexcept { return 0ULL; }
NOVA_LOGGER_TRAITS( DisabledTag, DISABLED, false, timestamp3 );


class NovaCore : public ::testing::Test
{
protected:
	void SetUp() override
	{
		// setup runs before each test
	}

	void TearDown() override
	{
		// cleanup runs after each test
	}
};

TEST_F( NovaCore, RecordStructure )
{
	kmac::nova::Record record {
		"TAG",
		0,
		"file.cpp",
		"function",
		42,
		1234567890ULL,
		"test message",
		12
	};

	EXPECT_STREQ( record.tag, "TAG" );
	EXPECT_STREQ( record.file, "file.cpp" );
	EXPECT_STREQ( record.function, "function" );
	EXPECT_EQ( record.line, 42u );
	EXPECT_EQ( record.timestamp, 1234567890ULL );
	EXPECT_EQ( std::string( record.message, record.messageSize ), "test message" );
	EXPECT_EQ( record.messageSize, 12u );
}

TEST_F( NovaCore, LoggerTraits )
{
	// test enabled tag
	EXPECT_TRUE( kmac::nova::logger_traits< TestTag1 >::enabled );
	EXPECT_STREQ( kmac::nova::logger_traits< TestTag1 >::tagName, "TEST1" );
	EXPECT_EQ( kmac::nova::logger_traits< TestTag1 >::timestamp(), 12345ULL );

	// test disabled tag
	EXPECT_FALSE( kmac::nova::logger_traits< DisabledTag >::enabled );
}

TEST_F( NovaCore, LoggerBinding )
{
	CaptureSink sink;

	{
		kmac::nova::ScopedConfigurator config;
		config.bind< TestTag1 >( &sink );

		// log a message
		kmac::nova::Logger< TestTag1 >::log( __FILE__, __FUNCTION__, __LINE__, "test message" );

		EXPECT_EQ( sink.records.size(), 1u );
		EXPECT_EQ( sink.records[ 0 ].tag, "TEST1" );
		EXPECT_EQ( sink.records[ 0 ].message, "test message" );
	}

	// after scope, sink is unbound
	sink.clear();
	kmac::nova::Logger< TestTag1 >::log( __FILE__, __FUNCTION__, __LINE__, "should not appear" );
	EXPECT_EQ( sink.records.size(), 0u );
}

TEST_F( NovaCore, MultipleTagsSeparateSinks )
{
	CaptureSink sink1;
	CaptureSink sink2;

	kmac::nova::ScopedConfigurator config;
	config.bind< TestTag1 >( &sink1 );
	config.bind< TestTag2 >( &sink2 );

	kmac::nova::Logger< TestTag1 >::log( __FILE__, __FUNCTION__, __LINE__, "message 1" );
	kmac::nova::Logger< TestTag2 >::log( __FILE__, __FUNCTION__, __LINE__, "message 2" );

	EXPECT_EQ( sink1.records.size(), 1u );
	EXPECT_EQ( sink2.records.size(), 1u );
	EXPECT_EQ( sink1.records[ 0 ].tag, "TEST1" );
	EXPECT_EQ( sink2.records[ 0 ].tag, "TEST2" );
}

TEST_F( NovaCore, MultipleTagsSharedSink )
{
	CaptureSink sink;

	kmac::nova::ScopedConfigurator config;
	config.bind< TestTag1 >( &sink );
	config.bind< TestTag2 >( &sink );

	kmac::nova::Logger< TestTag1 >::log( __FILE__, __FUNCTION__, __LINE__, "from tag1" );
	kmac::nova::Logger< TestTag2 >::log( __FILE__, __FUNCTION__, __LINE__, "from tag2" );

	EXPECT_EQ( sink.records.size(), 2u );
	EXPECT_EQ( sink.records[ 0 ].tag, "TEST1" );
	EXPECT_EQ( sink.records[ 1 ].tag, "TEST2" );
}

TEST_F( NovaCore, DisabledTag )
{
	CaptureSink sink;

	kmac::nova::ScopedConfigurator config;
	config.bind< DisabledTag >( &sink );

	// logging with disabled tag should not produce output
	kmac::nova::Logger< DisabledTag >::log( __FILE__, __FUNCTION__, __LINE__, "should not appear" );

	EXPECT_EQ( sink.records.size(), 0u );
}

TEST_F( NovaCore, TimestampHelper )
{
	auto steady1 = kmac::nova::TimestampHelper::steadyNanosecs();
	auto system1 = kmac::nova::TimestampHelper::systemNanosecs();

	// verify timestamps are reasonable (non-zero and increasing)
	EXPECT_GT( steady1, 0u );
	EXPECT_GT( system1, 0u );

	auto steady2 = kmac::nova::TimestampHelper::steadyNanosecs();
	auto system2 = kmac::nova::TimestampHelper::systemNanosecs();

	EXPECT_GE( steady2, steady1 );
	EXPECT_GE( system2, system1 );
}

TEST_F( NovaCore, ScopedConfiguratorNesting )
{
	CaptureSink sink1;
	CaptureSink sink2;

	{
		kmac::nova::ScopedConfigurator config1;
		config1.bind< TestTag1 >( &sink1 );

		kmac::nova::Logger< TestTag1 >::log( __FILE__, __FUNCTION__, __LINE__, "outer" );

		{
			kmac::nova::ScopedConfigurator config2;
			config2.bind< TestTag1 >( &sink2 );

			kmac::nova::Logger< TestTag1 >::log( __FILE__, __FUNCTION__, __LINE__, "inner" );

			EXPECT_EQ( sink2.records.size(), 1u );
		}

		// after inner scope, TestTag1 is unbound (not restored to sink1);
		// ScopedConfigurator does not preserve or restore prior configuration state,
		// need to rebind to sink1
		config1.bind< TestTag1 >( &sink1 );

		sink1.clear();
		kmac::nova::Logger< TestTag1 >::log( __FILE__, __FUNCTION__, __LINE__, "back to outer" );
		EXPECT_EQ( sink1.records.size(), 1u );
	}
}

TEST_F( NovaCore, FileLineFunction )
{
	CaptureSink sink;

	kmac::nova::ScopedConfigurator config;
	config.bind< TestTag1 >( &sink );

	const char* expectedFile = __FILE__;
	const char* expectedFunction = __FUNCTION__;
	const uint32_t expectedLine = __LINE__ + 1;
	kmac::nova::Logger< TestTag1 >::log( expectedFile, expectedFunction, expectedLine, "test" );

	EXPECT_EQ( sink.records.size(), 1u );
	EXPECT_EQ( sink.records[ 0 ].file, expectedFile );
	EXPECT_EQ( sink.records[ 0 ].function, expectedFunction );
	EXPECT_EQ( sink.records[ 0 ].line, expectedLine );
}
