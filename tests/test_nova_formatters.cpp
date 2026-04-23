/**
 * @file test_nova_formatters.cpp
 * @brief Google Test unit tests for Nova extras formatters
 */

#include "kmac/nova/nova.h"
#include "kmac/nova/scoped_configurator.h"
#include "kmac/nova/extras/formatting_sink.h"
#include "kmac/nova/extras/iso8601_formatter.h"
#include "kmac/nova/extras/ostream_sink.h"

#include <gtest/gtest.h>

#include <sstream>
#include <string>

// test tag
struct FormatterTag { };
std::uint64_t tagTimestamp() noexcept
{
	// fixed timestamp for predictable testing
	return 1704067200000000000ULL; // 2024-01-01 00:00:00 UTC
}
NOVA_LOGGER_TRAITS( FormatterTag, FMT, true, tagTimestamp );


// custom formatter functions or non-capturing lambdas
namespace
{
std::string CustomFormatter( const kmac::nova::Record& rec )
{
	std::string msg( rec.message, rec.messageSize );
	return "[CUSTOM] " + msg + "\n";
}

std::string TagFormatter( const kmac::nova::Record& rec )
{
	std::string msg( rec.message, rec.messageSize );
	return std::string( "[" ) + rec.tag + "] " + msg + "\n";
}

std::string TimestampFormatter( const kmac::nova::Record& rec )
{
	std::string msg( rec.message, rec.messageSize );
	return std::to_string( rec.timestamp ) + ": " + msg + "\n";
}

std::string LineFormatter( const kmac::nova::Record& rec )
{
	std::string msg( rec.message, rec.messageSize );
	return std::string( rec.file ) + ":" + std::to_string( rec.line ) + " " + msg + "\n";
}

std::string MessageOnlyFormatter( const kmac::nova::Record& rec )
{
	return std::string( rec.message, rec.messageSize );
}

std::string JSONFormatter( const kmac::nova::Record& rec )
{
	std::string msg( rec.message, rec.messageSize );
	return "{\"tag\":\"" + std::string( rec.tag ) +
		  "\",\"message\":\"" + msg + "\"}\n";
}
} // namespace

class NovaFormatters : public ::testing::Test
{
protected:
	void SetUp() override { }
	void TearDown() override { }
};

// TEST_F( NovaFormatters, FormattingSinkBasic )
// {
// 	std::ostringstream oss;
// 	kmac::nova::extras::OStreamSink baseSink( oss );
// 	kmac::nova::extras::FormattingSink formattingSink( baseSink );

// 	kmac::nova::ScopedConfigurator config;
// 	config.bind< FormatterTag >( &formattingSink );

// 	NOVA_LOG( FormatterTag ) << "formatted message";

// 	std::string output = oss.str();
// 	EXPECT_FALSE( output.empty() );
// 	EXPECT_NE( output.find( "formatted message" ), std::string::npos );
// }

// TEST_F( NovaFormatters, FormattingSinkContainsTag )
// {
// 	std::ostringstream oss;
// 	kmac::nova::extras::OStreamSink baseSink( oss );
// 	kmac::nova::extras::FormattingSink formattingSink( baseSink );

// 	kmac::nova::ScopedConfigurator config;
// 	config.bind< FormatterTag >( &formattingSink );

// 	NOVA_LOG( FormatterTag ) << "test";

// 	std::string output = oss.str();
// 	EXPECT_NE( output.find( "FMT" ), std::string::npos );
// }

// TEST_F( NovaFormatters, FormattingSinkContainsFile )
// {
// 	std::ostringstream oss;
// 	kmac::nova::extras::OStreamSink baseSink( oss );
// 	kmac::nova::extras::FormattingSink formattingSink( baseSink );

// 	kmac::nova::ScopedConfigurator config;
// 	config.bind< FormatterTag >( &formattingSink );

// 	NOVA_LOG( FormatterTag ) << "test";

// 	std::string output = oss.str();
// 	EXPECT_NE( output.find( "test_nova_formatters.cpp" ), std::string::npos );
// }

// TEST_F( NovaFormatters, FormattingSinkContainsFunction )
// {
// 	std::ostringstream oss;
// 	kmac::nova::extras::OStreamSink baseSink( oss );
// 	kmac::nova::extras::FormattingSink formattingSink( baseSink );

// 	kmac::nova::ScopedConfigurator config;
// 	config.bind< FormatterTag >( &formattingSink );

// 	NOVA_LOG( FormatterTag ) << "test";

// 	std::string output = oss.str();
// 	// should contain the test file name (from default formatter)
// 	EXPECT_NE( output.find( "test_nova_formatters.cpp" ), std::string::npos );
// 	// should contain the message
// 	EXPECT_NE( output.find( "test" ), std::string::npos );
// }

// TEST_F( NovaFormatters, FormattingSinkMultipleMessages )
// {
// 	std::ostringstream oss;
// 	kmac::nova::extras::OStreamSink baseSink( oss );
// 	kmac::nova::extras::FormattingSink formattingSink( baseSink );

// 	kmac::nova::ScopedConfigurator config;
// 	config.bind< FormatterTag >( &formattingSink );

// 	NOVA_LOG( FormatterTag ) << "first";
// 	NOVA_LOG( FormatterTag ) << "second";
// 	NOVA_LOG( FormatterTag ) << "third";

// 	std::string output = oss.str();
// 	EXPECT_NE( output.find( "first" ), std::string::npos );
// 	EXPECT_NE( output.find( "second" ), std::string::npos );
// 	EXPECT_NE( output.find( "third" ), std::string::npos );
// }

// TEST_F( NovaFormatters, CustomFormatter )
// {
// 	std::ostringstream oss;
// 	kmac::nova::extras::OStreamSink baseSink( oss );
// 	kmac::nova::extras::FormattingSink formattingSink( baseSink, CustomFormatter );

// 	kmac::nova::ScopedConfigurator config;
// 	config.bind< FormatterTag >( &formattingSink );

// 	NOVA_LOG( FormatterTag ) << "custom format";

// 	std::string output = oss.str();
// 	EXPECT_NE( output.find( "[CUSTOM]" ), std::string::npos );
// 	EXPECT_NE( output.find( "custom format" ), std::string::npos );
// }

// TEST_F( NovaFormatters, FormatterWithTagInOutput )
// {
// 	std::ostringstream oss;
// 	kmac::nova::extras::OStreamSink baseSink( oss );
// 	kmac::nova::extras::FormattingSink formattingSink( baseSink, TagFormatter );

// 	kmac::nova::ScopedConfigurator config;
// 	config.bind< FormatterTag >( &formattingSink );

// 	NOVA_LOG( FormatterTag ) << "test message";

// 	std::string output = oss.str();
// 	EXPECT_NE( output.find( "[FMT]" ), std::string::npos );
// 	EXPECT_NE( output.find( "test message" ), std::string::npos );
// }

// TEST_F( NovaFormatters, FormatterWithTimestamp )
// {
// 	std::ostringstream oss;
// 	kmac::nova::extras::OStreamSink baseSink( oss );
// 	kmac::nova::extras::FormattingSink formattingSink( baseSink, TimestampFormatter );

// 	kmac::nova::ScopedConfigurator config;
// 	config.bind< FormatterTag >( &formattingSink );

// 	NOVA_LOG( FormatterTag ) << "timestamp test";

// 	std::string output = oss.str();
// 	EXPECT_NE( output.find( "1704067200000000000" ), std::string::npos );
// 	EXPECT_NE( output.find( "timestamp test" ), std::string::npos );
// }

// TEST_F( NovaFormatters, FormatterWithLineNumber )
// {
// 	std::ostringstream oss;
// 	kmac::nova::extras::OStreamSink baseSink( oss );
// 	kmac::nova::extras::FormattingSink formattingSink( baseSink, LineFormatter );

// 	kmac::nova::ScopedConfigurator config;
// 	config.bind< FormatterTag >( &formattingSink );

// 	const uint32_t expectedLine = __LINE__ + 1;
// 	NOVA_LOG( FormatterTag ) << "line test";

// 	std::string output = oss.str();
// 	EXPECT_NE( output.find( std::to_string( expectedLine ) ), std::string::npos );
// 	EXPECT_NE( output.find( "test_nova_formatters.cpp" ), std::string::npos );
// }

// TEST_F( NovaFormatters, FormatterOnlyMessage )
// {
// 	std::ostringstream oss;
// 	kmac::nova::extras::OStreamSink baseSink( oss );
// 	kmac::nova::extras::FormattingSink formattingSink( baseSink, MessageOnlyFormatter );

// 	kmac::nova::ScopedConfigurator config;
// 	config.bind< FormatterTag >( &formattingSink );

// 	NOVA_LOG( FormatterTag ) << "plain message";

// 	std::string output = oss.str();
// 	// the MessageOnlyFormatter formats to just the message,
// 	// so we verify the message is present and is the only contents
// 	EXPECT_EQ( output, "plain message" );
// }

// TEST_F( NovaFormatters, FormatterJSON )
// {
// 	std::ostringstream oss;
// 	kmac::nova::extras::OStreamSink baseSink( oss );
// 	kmac::nova::extras::FormattingSink formattingSink( baseSink, JSONFormatter );

// 	kmac::nova::ScopedConfigurator config;
// 	config.bind< FormatterTag >( &formattingSink );

// 	NOVA_LOG( FormatterTag ) << "json test";

// 	std::string output = oss.str();
// 	EXPECT_NE( output.find( "{\"tag\":\"FMT\"" ), std::string::npos );
// 	EXPECT_NE( output.find( "\"message\":\"json test\"}" ), std::string::npos );
// }

// TEST_F( NovaFormatters, DefaultFormatterExplicit )
// {
// 	std::ostringstream oss;
// 	kmac::nova::extras::OStreamSink baseSink( oss );

// 	// use DefaultFormatter explicitly
// 	kmac::nova::extras::FormattingSink formattingSink( baseSink, kmac::nova::extras::DefaultFormatter );

// 	kmac::nova::ScopedConfigurator config;
// 	config.bind< FormatterTag >( &formattingSink );

// 	NOVA_LOG( FormatterTag ) << "default formatted";

// 	std::string output = oss.str();
// 	EXPECT_FALSE( output.empty() );
// 	EXPECT_NE( output.find( "default formatted" ), std::string::npos );
// 	EXPECT_NE( output.find( "FMT" ), std::string::npos );
// }

// TEST_F( NovaFormatters, NullFormatterUsesDefault )
// {
// 	std::ostringstream oss;
// 	kmac::nova::extras::OStreamSink baseSink( oss );

// 	// pass nullptr, which should fall back to default
// 	kmac::nova::extras::FormattingSink formattingSink( baseSink, nullptr );

// 	kmac::nova::ScopedConfigurator config;
// 	config.bind< FormatterTag >( &formattingSink );

// 	NOVA_LOG( FormatterTag ) << "null formatter";

// 	std::string output = oss.str();
// 	// should use default format, which includes tag
// 	EXPECT_NE( output.find( "[FMT]" ), std::string::npos );
// 	EXPECT_NE( output.find( "null formatter" ), std::string::npos );
// }

TEST_F( NovaFormatters, ISO8601FormattingSink )
{
	std::ostringstream oss;
	kmac::nova::extras::OStreamSink baseSink( oss );
	kmac::nova::extras::ISO8601Formatter formatter;
	kmac::nova::extras::FormattingSink<> iso8601Sink( baseSink, formatter );

	kmac::nova::ScopedConfigurator<> config;
	config.bind< FormatterTag >( &iso8601Sink );

	NOVA_LOG( FormatterTag ) << "iso8601 test";

	std::string output = oss.str();
	EXPECT_FALSE( output.empty() );
	// check for all components since default formatter includes them
	EXPECT_NE( output.find( "2024-01-01T00:00:00" ), std::string::npos );
	EXPECT_NE( output.find( "[FMT]" ), std::string::npos );
	EXPECT_NE( output.find( "test_nova_formatters.cpp" ), std::string::npos );
	EXPECT_NE( output.find( "iso8601 test" ), std::string::npos );
}
