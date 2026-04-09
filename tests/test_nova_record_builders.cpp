/**
 * @file test_nova_record_builders.cpp
 * @brief Google Test unit tests for Nova record builders
 */

#include "kmac/nova/nova.h"
#include "kmac/nova/scoped_configurator.h"
#include "kmac/nova/extras/continuation_logging.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

// test tag
struct BuilderTag { };
std::uint64_t tagTimestamp() noexcept { return 99999ULL; }
NOVA_LOGGER_TRAITS( BuilderTag, BUILDER, true, tagTimestamp );


// capture sink
class BuilderCaptureSink : public kmac::nova::Sink
{
public:
	std::vector< kmac::nova::Record > records;
	std::vector< std::string > storage;

	void process( const kmac::nova::Record& record ) noexcept override
	{
		std::string msg( record.message, record.messageSize );
		storage.push_back( msg );

		kmac::nova::Record copy = record;
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
		if ( index >= records.size() )
		{
			return "";
		}
		return std::string( records[ index ].message, records[ index ].messageSize );
	}
};

class NovaRecordBuilders : public ::testing::Test
{
protected:
	BuilderCaptureSink sink;

	void SetUp() override
	{
		sink.clear();
	}
};

TEST_F( NovaRecordBuilders, TruncatingBuilderBasic )
{
	kmac::nova::ScopedConfigurator config;
	config.bind< BuilderTag >( &sink );

	{
		kmac::nova::TruncatingRecordBuilder<> builder;
		builder.setContext< BuilderTag >( __FILE__, __FUNCTION__, __LINE__ );
		builder << "Hello " << "World";
		builder.commit();
	}

	EXPECT_EQ( sink.records.size(), 1u );
	EXPECT_NE( sink.getMessage( 0 ).find( "Hello" ), std::string::npos );
	EXPECT_NE( sink.getMessage( 0 ).find( "World" ), std::string::npos );
}

TEST_F( NovaRecordBuilders, TruncatingBuilderNumbers )
{
	kmac::nova::ScopedConfigurator config;
	config.bind< BuilderTag >( &sink );

	{
		kmac::nova::TruncatingRecordBuilder<> builder;
		builder.setContext< BuilderTag >( __FILE__, __FUNCTION__, __LINE__ );
		builder << "int: " << 42 << " float: " << 3.14 << " bool: " << true;
		builder.commit();
	}

	EXPECT_EQ( sink.records.size(), 1u );
	std::string msg = sink.getMessage( 0 );
	EXPECT_NE( msg.find( "42 "), std::string::npos );
	EXPECT_NE( msg.find( "3.14" ), std::string::npos );
	EXPECT_TRUE( msg.find( "true" ) != std::string::npos || msg.find( "1" ) != std::string::npos );
}

TEST_F( NovaRecordBuilders, TruncatingBuilderTruncation )
{
	kmac::nova::ScopedConfigurator config;
	config.bind< BuilderTag >( &sink );

	{
		kmac::nova::TruncatingRecordBuilder< 32 > builder;
		builder.setContext< BuilderTag >( __FILE__, __FUNCTION__, __LINE__ );
		builder << "This is a very long message that will be truncated";
		builder.commit();
	}

	EXPECT_EQ( sink.records.size(), 1u );
	EXPECT_LE( sink.records[ 0 ].messageSize, 32u );
}

TEST_F( NovaRecordBuilders, TruncatingBuilderEmpty )
{
	kmac::nova::ScopedConfigurator config;
	config.bind< BuilderTag >( &sink );

	{
		kmac::nova::TruncatingRecordBuilder<> builder;
		builder.setContext< BuilderTag >( __FILE__, __FUNCTION__, __LINE__ );
		// don't stream anything
		builder.commit();
	}

	EXPECT_EQ( sink.records.size(), 1u );
	EXPECT_EQ( sink.records[ 0 ].messageSize, 0u );
}

TEST_F( NovaRecordBuilders, ContinuationBuilderBasic )
{
	kmac::nova::ScopedConfigurator config;
	config.bind< BuilderTag >( &sink );

	{
		kmac::nova::extras::ContinuationRecordBuilder<> builder;
		builder.setContext< BuilderTag >( __FILE__, __FUNCTION__, __LINE__ );
		builder << "Part 1 " << "Part 2";
		builder.commit();
	}

	EXPECT_EQ( sink.records.size(), 1u );
	EXPECT_NE( sink.getMessage( 0 ).find( "Part 1" ), std::string::npos );
	EXPECT_NE( sink.getMessage( 0 ).find( "Part 2" ), std::string::npos );
}

TEST_F( NovaRecordBuilders, ContinuationBuilderOverflow )
{
	kmac::nova::ScopedConfigurator config;
	config.bind< BuilderTag >( &sink );

	{
		kmac::nova::extras::ContinuationRecordBuilder< 32 > builder;
		builder.setContext< BuilderTag >( __FILE__, __FUNCTION__, __LINE__ );
		builder
			<< "This is a very long message that will overflow "
			<< "and create multiple continuation records";
		builder.commit();
	}

	// should have created multiple records due to overflow
	EXPECT_GE( sink.records.size(), 2u );

	// all records should have the same file/function/line
	for ( size_t i = 1; i < sink.records.size(); ++i )
	{
		EXPECT_STREQ( sink.records[ i ].file, sink.records[ 0 ].file );
		EXPECT_STREQ( sink.records[ i ].function, sink.records[ 0 ].function );
		EXPECT_EQ( sink.records[ i ].line, sink.records[ 0 ].line );
	}
}

TEST_F( NovaRecordBuilders, ContinuationBuilderMultipleOverflows )
{
	kmac::nova::ScopedConfigurator config;
	config.bind< BuilderTag >( &sink );

	{
		kmac::nova::extras::ContinuationRecordBuilder< 16 > builder;
		builder.setContext< BuilderTag >( __FILE__, __FUNCTION__, __LINE__ );

		// write enough data to cause multiple overflows
		for ( int i = 0; i < 10; ++i )
		{
			builder << "Message " << i << " | ";
		}

		builder.commit();
	}

	EXPECT_GT( sink.records.size(), 1u );
}

TEST_F( NovaRecordBuilders, BuilderWithoutSink )
{
	// no sink bound, so builders should still work and not crash
	{
		kmac::nova::TruncatingRecordBuilder<> builder;
		builder.setContext< BuilderTag >( __FILE__, __FUNCTION__, __LINE__ );
		builder << "This goes nowhere";
		builder.commit();
	}

	{
		kmac::nova::extras::ContinuationRecordBuilder<> builder;
		builder.setContext< BuilderTag >( __FILE__, __FUNCTION__, __LINE__ );
		builder << "This also goes nowhere";
		builder.commit();
	}

	// test passes if no crash occurred
	SUCCEED();
}

TEST_F( NovaRecordBuilders, BuilderMixedTypes )
{
	kmac::nova::ScopedConfigurator config;
	config.bind< BuilderTag >( &sink );

	{
		kmac::nova::TruncatingRecordBuilder<> builder;
		builder.setContext< BuilderTag >( __FILE__, __FUNCTION__, __LINE__ );
		builder
			<< "int=" << 42
			<< " float=" << 1.5
			<< " string=" << "test"
			<< " char=" << 'X';
		builder.commit();
	}

	EXPECT_EQ( sink.records.size(), 1u );
	std::string msg = sink.getMessage( 0 );
	EXPECT_NE( msg.find( "int=42" ), std::string::npos );
	EXPECT_NE( msg.find( "float=1.5" ), std::string::npos );
	EXPECT_NE( msg.find( "string=test" ), std::string::npos );
	EXPECT_NE( msg.find( "char=X" ), std::string::npos );
}

TEST_F( NovaRecordBuilders, BuilderPreservesMetadata )
{
	kmac::nova::ScopedConfigurator config;
	config.bind< BuilderTag >( &sink );

	const char* testFile = "test_file.cpp";
	const char* testFunction = "testFunction";
	const uint32_t testLine = 12345;

	{
		kmac::nova::TruncatingRecordBuilder<> builder;
		builder.setContext< BuilderTag >( testFile, testFunction, testLine );
		builder << "metadata test";
		builder.commit();
	}

	EXPECT_EQ( sink.records.size(), 1u );
	EXPECT_STREQ( sink.records[ 0 ].file, testFile );
	EXPECT_STREQ( sink.records[ 0 ].function, testFunction );
	EXPECT_EQ( sink.records[ 0 ].line, testLine );
	EXPECT_STREQ( sink.records[ 0 ].tag, "BUILDER" );
	EXPECT_EQ( sink.records[ 0 ].timestamp, 99999ULL );
}

TEST_F( NovaRecordBuilders, ContinuationBuilderRespectsBufferSize )
{
	kmac::nova::ScopedConfigurator config;
	config.bind< BuilderTag >( &sink );

	{
		kmac::nova::extras::ContinuationRecordBuilder< 64 > builder;
		builder.setContext< BuilderTag >( __FILE__, __FUNCTION__, __LINE__ );

		// each record should not exceed buffer size
		builder << "Small message";

		builder.commit();
	}

	EXPECT_EQ( sink.records.size(), 1u );
	EXPECT_LT( sink.records[ 0 ].messageSize, 64u );
}
