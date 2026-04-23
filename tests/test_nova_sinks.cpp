/**
 * @file test_nova_sinks.cpp
 * @brief Google Test unit tests for Nova extras sinks
 */

#include "kmac/nova/nova.h"
#include "kmac/nova/scoped_configurator.h"
#include "kmac/nova/extras/filter_sink.h"
#include "kmac/nova/extras/ostream_sink.h"
#include "kmac/nova/extras/null_sink.h"

#include <gtest/gtest.h>

#include <sstream>
#include <string>

// test tag
struct SinkTag { };
std::uint64_t tagTimestamp() noexcept { return 55555ULL; }
NOVA_LOGGER_TRAITS( SinkTag, SINK, true, tagTimestamp );


class NovaSinks : public ::testing::Test
{

protected:
	void SetUp() override { }
	void TearDown() override { }
};

TEST_F( NovaSinks, OStreamSink )
{
	std::ostringstream oss;
	kmac::nova::extras::OStreamSink sink( oss );

	kmac::nova::ScopedConfigurator<> config;
	config.bind< SinkTag >( &sink );

	NOVA_LOG( SinkTag ) << "test output";

	std::string output = oss.str();
	EXPECT_EQ( output, "test output\n" );
}

TEST_F( NovaSinks, OStreamSinkMultipleMessages )
{
	std::ostringstream oss;
	kmac::nova::extras::OStreamSink sink( oss );

	kmac::nova::ScopedConfigurator<> config;
	config.bind< SinkTag >( &sink );

	NOVA_LOG( SinkTag ) << "message 1";
	NOVA_LOG( SinkTag ) << "message 2";
	NOVA_LOG( SinkTag ) << "message 3";

	std::string output = std::string( oss.str() );
	EXPECT_TRUE( ( output.find( "message 1" ) != std::string::npos ) );
	EXPECT_TRUE( ( output.find( "message 2" ) != std::string::npos ) );
	EXPECT_TRUE( ( output.find( "message 3" ) != std::string::npos ) );
}

TEST_F( NovaSinks, NullSink )
{
	kmac::nova::extras::NullSink& sink = kmac::nova::extras::NullSink::instance();

	kmac::nova::ScopedConfigurator<> config;
	config.bind< SinkTag >( &sink );

	// log messages, which should be silently discarded
	NOVA_LOG( SinkTag ) << "this goes nowhere";
	NOVA_LOG( SinkTag ) << "neither does this";

	// test passes if no crash occurred
	SUCCEED();
}

TEST_F( NovaSinks, FilterSinkAccepts )
{
	std::ostringstream oss;
	kmac::nova::extras::OStreamSink baseSink( oss );

	// filter that accepts messages containing "accept"
	auto filterFn = []( const kmac::nova::Record& rec ) {
		std::string msg( rec.message, rec.messageSize );
		return msg.find( "accept" ) != std::string::npos;
	};
	kmac::nova::extras::FilterSink< decltype( filterFn ) > filterSink( baseSink, filterFn );

	kmac::nova::ScopedConfigurator<> config;
	config.bind< SinkTag >( &filterSink );

	NOVA_LOG( SinkTag ) << "accept this message";
	NOVA_LOG( SinkTag ) << "reject this message";
	NOVA_LOG( SinkTag ) << "also accept this";

	std::string output = std::string( oss.str() );
	EXPECT_TRUE( ( output.find( "accept this message" ) != std::string::npos ) );
	EXPECT_TRUE( ! ( output.find( "reject this message" ) != std::string::npos ) );
	EXPECT_TRUE( ( output.find( "also accept this" ) != std::string::npos ) );
}

TEST_F( NovaSinks, FilterSinkRejects )
{
	std::ostringstream oss;
	kmac::nova::extras::OStreamSink baseSink( oss );

	// filter that rejects all messages
	auto filterFn = []( const kmac::nova::Record& ) { return false; };
	kmac::nova::extras::FilterSink< decltype( filterFn ) > filterSink( baseSink, filterFn );

	kmac::nova::ScopedConfigurator<> config;
	config.bind< SinkTag >( &filterSink );

	NOVA_LOG( SinkTag ) << "should not appear";

	std::string output = std::string( oss.str() );
	EXPECT_TRUE( output.empty() );
}

TEST_F( NovaSinks, FilterSinkAcceptsAll )
{
	std::ostringstream oss;
	kmac::nova::extras::OStreamSink baseSink( oss );

	// filter that accepts all messages
	auto filterFn = []( const kmac::nova::Record& ) { return true; };
	kmac::nova::extras::FilterSink< decltype( filterFn ) > filterSink( baseSink, filterFn );

	kmac::nova::ScopedConfigurator<> config;
	config.bind< SinkTag >( &filterSink );

	NOVA_LOG( SinkTag ) << "message 1";
	NOVA_LOG( SinkTag ) << "message 2";

	std::string output = std::string( oss.str() );
	EXPECT_TRUE( ( output.find( "message 1" ) != std::string::npos ) );
	EXPECT_TRUE( ( output.find( "message 2" ) != std::string::npos ) );
}

TEST_F( NovaSinks, FilterSinkByLineNumber )
{
	std::ostringstream oss;
	kmac::nova::extras::OStreamSink baseSink( oss );

	// filter that only accepts records from specific line numbers
	uint32_t capturedLine = 0;
	auto filterFn = [ &capturedLine ]( const kmac::nova::Record& rec ) {
		return capturedLine != 0 && rec.line == capturedLine;
	};
	kmac::nova::extras::FilterSink< decltype( filterFn ) > filterSink( baseSink, filterFn );

	kmac::nova::ScopedConfigurator<> config;
	config.bind< SinkTag >( &filterSink );

	// update captured line to be correct for the filter
	capturedLine = __LINE__ + 1;
	NOVA_LOG( SinkTag ) << "this line should appear";

	// update captured line to be incorrect for the filter
	capturedLine = 0;
	NOVA_LOG( SinkTag ) << "this line should not";

	std::string output = oss.str();
	EXPECT_NE( output.find( "this line should appear" ), std::string::npos );
	EXPECT_EQ( output.find( "this line should not" ), std::string::npos );
}

TEST_F( NovaSinks, FilterSinkComplex )
{
	std::ostringstream oss;
	kmac::nova::extras::OStreamSink baseSink( oss );

	// complex filter: accept if message contains "error" OR line > 1000
	auto filterFn = []( const kmac::nova::Record& rec ) {
		std::string msg( rec.message, rec.messageSize );
		return msg.find( "error" ) != std::string::npos || rec.line > 1000;
	};
	kmac::nova::extras::FilterSink< decltype( filterFn ) > filterSink( baseSink, filterFn );

	kmac::nova::ScopedConfigurator<> config;
	config.bind< SinkTag >( &filterSink );

	NOVA_LOG( SinkTag ) << "error occurred";
	NOVA_LOG( SinkTag ) << "normal message";
	NOVA_LOG( SinkTag ) << "another error";

	std::string output = std::string( oss.str() );
	EXPECT_TRUE( ( output.find( "error occurred" ) != std::string::npos ) );
	EXPECT_TRUE( ! ( output.find( "normal message" ) != std::string::npos ) );
	EXPECT_TRUE( ( output.find( "another error" ) != std::string::npos ) );
}

TEST_F( NovaSinks, OStreamSinkWithCout )
{
	// test with std::cout (just verify no crash)
	kmac::nova::extras::OStreamSink sink( std::cout );

	kmac::nova::ScopedConfigurator<> config;
	config.bind< SinkTag >( &sink );

	NOVA_LOG( SinkTag ) << "to stdout";

	SUCCEED();
}

TEST_F( NovaSinks, OStreamSinkWithCerr )
{
	// test with std::cerr (just verify no crash)
	kmac::nova::extras::OStreamSink sink( std::cerr );

	kmac::nova::ScopedConfigurator<> config;
	config.bind< SinkTag >( &sink );

	NOVA_LOG( SinkTag ) << "to stderr";

	SUCCEED();
}

TEST_F( NovaSinks, SinkChaining )
{
	std::ostringstream oss;
	kmac::nova::extras::OStreamSink baseSink( oss );

	// chain filters
	auto filterFn1 = []( const kmac::nova::Record& rec ) {
		std::string msg( rec.message, rec.messageSize );
		return msg.find( "pass" ) != std::string::npos;
	};
	kmac::nova::extras::FilterSink< decltype( filterFn1 ) > filter1( baseSink, filterFn1 );

	auto filterFn2 = []( const kmac::nova::Record& rec ) {
		std::string msg( rec.message, rec.messageSize );
		return msg.find( "test" ) != std::string::npos;
	};
	kmac::nova::extras::FilterSink< decltype( filterFn2 ) > filter2( filter1, filterFn2 );

	kmac::nova::ScopedConfigurator<> config;
	config.bind< SinkTag >( &filter2 );

	NOVA_LOG( SinkTag ) << "pass test";      // both filters pass
	NOVA_LOG( SinkTag ) << "pass only";      // only first filter passes
	NOVA_LOG( SinkTag ) << "test only";      // only second filter passes
	NOVA_LOG( SinkTag ) << "neither";        // neither filter passes

	std::string output = std::string( oss.str() );
	EXPECT_TRUE( ( output.find( "pass test" ) != std::string::npos ) );
	EXPECT_TRUE( ! ( output.find( "pass only" ) != std::string::npos ) );
	EXPECT_TRUE( ! ( output.find( "test only" ) != std::string::npos ) );
	EXPECT_TRUE( ! ( output.find( "neither" ) != std::string::npos ) );
}
