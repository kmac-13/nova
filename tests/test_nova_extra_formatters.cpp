/**
 * @file test_nova_extra_formatters.cpp
 * @brief Unit tests for CsvFormatter, JsonFormatter, XmlFormatter,
 *        MultilineFormatter, and LargePayloadFormatter.
 *
 * FormattingSink-based formatters (CSV/JSON/XML) are exercised end-to-end
 * via an OStreamSink; their output is captured in an ostringstream and
 * inspected for expected fields.
 *
 * MultiRecordFormatter-based formatters (Multiline/LargePayload) are
 * exercised by calling formatAndWrite() directly with a counting/capture sink.
 */

#include "kmac/nova.h"
#include "kmac/nova/extras/csv_formatter.h"
#include "kmac/nova/extras/formatting_sink.h"
#include "kmac/nova/extras/json_formatter.h"
#include "kmac/nova/extras/large_payload_formatter.h"
#include "kmac/nova/extras/multiline_formatter.h"
#include "kmac/nova/extras/ostream_sink.h"
#include "kmac/nova/extras/xml_formatter.h"

#include <gtest/gtest.h>

#include <atomic>
#include <sstream>
#include <string>
#include <vector>

// test tag - fixed timestamp for predictable ISO 8601 output
struct ExtraFmtTag {};
std::uint64_t extraFmtTimestamp() noexcept
{
	// 2024-01-01 00:00:00.000 UTC in nanoseconds
	return 1704067200000000000ULL;
}
NOVA_LOGGER_TRAITS( ExtraFmtTag, EXTRAFMT, true, extraFmtTimestamp );

// counting sink used by MultiRecordFormatter tests
class CounterSink : public kmac::nova::Sink
{
public:
	std::atomic< std::size_t > count { 0 };
	std::vector< std::string > messages;

	void process( const kmac::nova::Record& record ) noexcept override
	{
		++count;
		messages.emplace_back( record.message, record.messageSize );
	}

	void clear()
	{
		count.store( 0 );
		messages.clear();
	}
};

// helper to build a populated Record for formatAndWrite tests
static kmac::nova::Record makeRecord(
	const char* tag,
	const char* file,
	const char* func,
	std::uint32_t line,
	std::uint64_t ts,
	const char* msg,
	std::uint32_t msgLen
)
{
	kmac::nova::Record r {};
	r.tag = tag;
	r.tagId = 0;
	r.file = file;
	r.function = func;
	r.line = line;
	r.timestamp = ts;
	r.message = msg;
	r.messageSize = msgLen;
	return r;
}

// ============================================================================
// CsvFormatter
// ============================================================================

class NovaCsvFormatter : public ::testing::Test
{
protected:
	std::ostringstream oss;
	kmac::nova::extras::OStreamSink baseSink { oss };
	kmac::nova::extras::CsvFormatter formatter;
	kmac::nova::extras::FormattingSink<> sink { baseSink, formatter };
};

TEST_F( NovaCsvFormatter, BasicOutput )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< ExtraFmtTag >( &sink );

	NOVA_LOG( ExtraFmtTag ) << "hello csv";

	const std::string output = oss.str();
	EXPECT_FALSE( output.empty() );
	EXPECT_NE( output.find( "hello csv" ), std::string::npos );
}

TEST_F( NovaCsvFormatter, ContainsTag )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< ExtraFmtTag >( &sink );

	NOVA_LOG( ExtraFmtTag ) << "tag check";

	EXPECT_NE( oss.str().find( "EXTRAFMT" ), std::string::npos );
}

TEST_F( NovaCsvFormatter, ContainsTimestamp )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< ExtraFmtTag >( &sink );

	NOVA_LOG( ExtraFmtTag ) << "ts check";

	// ISO 8601 timestamp prefix for 2024-01-01
	EXPECT_NE( oss.str().find( "2024-01-01" ), std::string::npos );
}

TEST_F( NovaCsvFormatter, EndsWithCRLF )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< ExtraFmtTag >( &sink );

	NOVA_LOG( ExtraFmtTag ) << "crlf test";

	const std::string output = oss.str();
	ASSERT_GE( output.size(), 2u );
	EXPECT_EQ( output[ output.size() - 2 ], '\r' );
	EXPECT_EQ( output[ output.size() - 1 ], '\n' );
}

TEST_F( NovaCsvFormatter, SevenCommaDelimitedColumns )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< ExtraFmtTag >( &sink );

	NOVA_LOG( ExtraFmtTag ) << "columns";

	// CSV row must have exactly 6 commas (7 fields)
	const std::string output = oss.str();
	// strip trailing CRLF before counting
	const std::string row = output.substr( 0, output.find( '\r' ) );
	std::size_t commaCount = 0;
	for ( char c : row )
	{
		if ( c == ',' )
		{
			++commaCount;
		}
	}
	EXPECT_EQ( commaCount, 6u );
}

TEST_F( NovaCsvFormatter, MultipleMessages )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< ExtraFmtTag >( &sink );

	NOVA_LOG( ExtraFmtTag ) << "first";
	NOVA_LOG( ExtraFmtTag ) << "second";
	NOVA_LOG( ExtraFmtTag ) << "third";

	const std::string output = oss.str();
	EXPECT_NE( output.find( "first" ), std::string::npos );
	EXPECT_NE( output.find( "second" ), std::string::npos );
	EXPECT_NE( output.find( "third" ), std::string::npos );
}

TEST_F( NovaCsvFormatter, TabDelimiter )
{
	// construct a tab-delimited variant
	kmac::nova::extras::CsvFormatter tabFormatter( '\t' );
	kmac::nova::extras::FormattingSink<> tabSink( baseSink, tabFormatter );

	kmac::nova::ScopedConfigurator<> config;
	config.bind< ExtraFmtTag >( &tabSink );

	NOVA_LOG( ExtraFmtTag ) << "tabbed";

	const std::string output = oss.str();
	const std::string row = output.substr( 0, output.find( '\r' ) );
	std::size_t tabCount = 0;
	for ( char c : row )
	{
		if ( c == '\t' )
		{
			++tabCount;
		}
	}
	EXPECT_EQ( tabCount, 6u );
}

// ============================================================================
// JsonFormatter
// ============================================================================

class NovaJsonFormatter : public ::testing::Test
{
protected:
	std::ostringstream oss;
	kmac::nova::extras::OStreamSink baseSink { oss };
	kmac::nova::extras::JsonFormatter formatter;
	kmac::nova::extras::FormattingSink<> sink { baseSink, formatter };
};

TEST_F( NovaJsonFormatter, OutputIsNotEmpty )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< ExtraFmtTag >( &sink );

	NOVA_LOG( ExtraFmtTag ) << "json test";

	EXPECT_FALSE( oss.str().empty() );
}

TEST_F( NovaJsonFormatter, BeginsWithOpenBrace )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< ExtraFmtTag >( &sink );

	NOVA_LOG( ExtraFmtTag ) << "brace";

	EXPECT_EQ( oss.str()[ 0 ], '{' );
}

TEST_F( NovaJsonFormatter, EndsWithCloseBraceNewline )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< ExtraFmtTag >( &sink );

	NOVA_LOG( ExtraFmtTag ) << "end";

	const std::string output = oss.str();
	ASSERT_GE( output.size(), 2u );
	EXPECT_EQ( output[ output.size() - 1 ], '\n' );
	EXPECT_EQ( output[ output.size() - 2 ], '}' );
}

TEST_F( NovaJsonFormatter, ContainsTagField )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< ExtraFmtTag >( &sink );

	NOVA_LOG( ExtraFmtTag ) << "fields";

	const std::string output = oss.str();
	EXPECT_NE( output.find( "\"tag\"" ), std::string::npos );
	EXPECT_NE( output.find( "EXTRAFMT" ), std::string::npos );
}

TEST_F( NovaJsonFormatter, ContainsTimestampField )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< ExtraFmtTag >( &sink );

	NOVA_LOG( ExtraFmtTag ) << "ts";

	EXPECT_NE( oss.str().find( "\"ts\"" ), std::string::npos );
	EXPECT_NE( oss.str().find( "2024-01-01" ), std::string::npos );
}

TEST_F( NovaJsonFormatter, ContainsMessageField )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< ExtraFmtTag >( &sink );

	NOVA_LOG( ExtraFmtTag ) << "json content";

	const std::string output = oss.str();
	EXPECT_NE( output.find( "\"message\"" ), std::string::npos );
	EXPECT_NE( output.find( "json content" ), std::string::npos );
}

TEST_F( NovaJsonFormatter, ContainsLineField )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< ExtraFmtTag >( &sink );

	NOVA_LOG( ExtraFmtTag ) << "line check";

	EXPECT_NE( oss.str().find( "\"line\"" ), std::string::npos );
}

TEST_F( NovaJsonFormatter, ContainsTagIdField )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< ExtraFmtTag >( &sink );

	NOVA_LOG( ExtraFmtTag ) << "tagId check";

	EXPECT_NE( oss.str().find( "\"tagId\"" ), std::string::npos );
}

TEST_F( NovaJsonFormatter, MultipleMessages )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< ExtraFmtTag >( &sink );

	NOVA_LOG( ExtraFmtTag ) << "alpha";
	NOVA_LOG( ExtraFmtTag ) << "beta";

	const std::string output = oss.str();
	EXPECT_NE( output.find( "alpha" ), std::string::npos );
	EXPECT_NE( output.find( "beta" ), std::string::npos );
}

TEST_F( NovaJsonFormatter, JsonEscapesQuoteInMessage )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< ExtraFmtTag >( &sink );

	NOVA_LOG( ExtraFmtTag ) << "say \"hello\"";

	// embedded quotes must be escaped
	EXPECT_NE( oss.str().find( "\\\"hello\\\"" ), std::string::npos );
}

// ============================================================================
// XmlFormatter
// ============================================================================

class NovaXmlFormatter : public ::testing::Test
{
protected:
	std::ostringstream oss;
	kmac::nova::extras::OStreamSink baseSink { oss };
	kmac::nova::extras::XmlFormatter formatter;
	kmac::nova::extras::FormattingSink<> sink { baseSink, formatter };
};

TEST_F( NovaXmlFormatter, OutputIsNotEmpty )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< ExtraFmtTag >( &sink );

	NOVA_LOG( ExtraFmtTag ) << "xml test";

	EXPECT_FALSE( oss.str().empty() );
}

TEST_F( NovaXmlFormatter, ContainsRecordElement )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< ExtraFmtTag >( &sink );

	NOVA_LOG( ExtraFmtTag ) << "record";

	const std::string output = oss.str();
	EXPECT_NE( output.find( "<record>" ), std::string::npos );
	EXPECT_NE( output.find( "</record>" ), std::string::npos );
}

TEST_F( NovaXmlFormatter, ContainsTagElement )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< ExtraFmtTag >( &sink );

	NOVA_LOG( ExtraFmtTag ) << "tag";

	const std::string output = oss.str();
	EXPECT_NE( output.find( "<tag>" ), std::string::npos );
	EXPECT_NE( output.find( "EXTRAFMT" ), std::string::npos );
}

TEST_F( NovaXmlFormatter, ContainsMessageElement )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< ExtraFmtTag >( &sink );

	NOVA_LOG( ExtraFmtTag ) << "xml message";

	const std::string output = oss.str();
	EXPECT_NE( output.find( "<message>" ), std::string::npos );
	EXPECT_NE( output.find( "xml message" ), std::string::npos );
}

TEST_F( NovaXmlFormatter, ContainsTimestampElement )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< ExtraFmtTag >( &sink );

	NOVA_LOG( ExtraFmtTag ) << "ts";

	const std::string output = oss.str();
	EXPECT_NE( output.find( "<ts>" ), std::string::npos );
	EXPECT_NE( output.find( "2024-01-01" ), std::string::npos );
}

TEST_F( NovaXmlFormatter, EndsWithNewline )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< ExtraFmtTag >( &sink );

	NOVA_LOG( ExtraFmtTag ) << "newline";

	const std::string output = oss.str();
	EXPECT_EQ( output.back(), '\n' );
}

TEST_F( NovaXmlFormatter, XmlEscapesAmpersandInMessage )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< ExtraFmtTag >( &sink );

	NOVA_LOG( ExtraFmtTag ) << "foo & bar";

	EXPECT_NE( oss.str().find( "&amp;" ), std::string::npos );
}

TEST_F( NovaXmlFormatter, XmlEscapesLessThanInMessage )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< ExtraFmtTag >( &sink );

	NOVA_LOG( ExtraFmtTag ) << "a < b";

	EXPECT_NE( oss.str().find( "&lt;" ), std::string::npos );
}

TEST_F( NovaXmlFormatter, MultipleMessages )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< ExtraFmtTag >( &sink );

	NOVA_LOG( ExtraFmtTag ) << "one";
	NOVA_LOG( ExtraFmtTag ) << "two";

	const std::string output = oss.str();
	EXPECT_NE( output.find( "one" ), std::string::npos );
	EXPECT_NE( output.find( "two" ), std::string::npos );
}

// ============================================================================
// MultilineFormatter
// ============================================================================

class NovaMultilineFormatter : public ::testing::Test
{
protected:
	CounterSink counter;

	void SetUp() override
	{
		counter.clear();
	}
};

TEST_F( NovaMultilineFormatter, SingleLineEmitsOneRecord )
{
	kmac::nova::extras::MultilineFormatter fmt( false, false );
	const char* msg = "single line";
	auto rec = makeRecord( "TAG", "f.cpp", "fn", 1, 0, msg, static_cast< std::uint32_t >( std::strlen( msg ) ) );
	fmt.formatAndWrite( rec, counter );

	EXPECT_EQ( counter.count.load(), 1u );
	EXPECT_EQ( counter.messages[ 0 ], "single line" );
}

TEST_F( NovaMultilineFormatter, TwoLinesEmitsTwoRecords )
{
	kmac::nova::extras::MultilineFormatter fmt( false, false );
	const char* msg = "line one\nline two";
	auto rec = makeRecord( "TAG", "f.cpp", "fn", 1, 0, msg, static_cast< std::uint32_t >( std::strlen( msg ) ) );
	fmt.formatAndWrite( rec, counter );

	EXPECT_EQ( counter.count.load(), 2u );
	EXPECT_EQ( counter.messages[ 0 ], "line one" );
	EXPECT_EQ( counter.messages[ 1 ], "line two" );
}

TEST_F( NovaMultilineFormatter, ThreeLinesWithCRLF )
{
	kmac::nova::extras::MultilineFormatter fmt( false, false );
	const char* msg = "a\r\nb\r\nc";
	auto rec = makeRecord( "TAG", "f.cpp", "fn", 1, 0, msg, static_cast< std::uint32_t >( std::strlen( msg ) ) );
	fmt.formatAndWrite( rec, counter );

	EXPECT_EQ( counter.count.load(), 3u );
	EXPECT_EQ( counter.messages[ 0 ], "a" );
	EXPECT_EQ( counter.messages[ 1 ], "b" );
	EXPECT_EQ( counter.messages[ 2 ], "c" );
}

TEST_F( NovaMultilineFormatter, LineNumbersEnabled )
{
	kmac::nova::extras::MultilineFormatter fmt( true, false );
	const char* msg = "alpha\nbeta\ngamma";
	auto rec = makeRecord( "TAG", "f.cpp", "fn", 1, 0, msg, static_cast< std::uint32_t >( std::strlen( msg ) ) );
	fmt.formatAndWrite( rec, counter );

	ASSERT_EQ( counter.count.load(), 3u );
	// with line numbers each line should contain "[N/3]"
	EXPECT_NE( counter.messages[ 0 ].find( "[1/3]" ), std::string::npos );
	EXPECT_NE( counter.messages[ 1 ].find( "[2/3]" ), std::string::npos );
	EXPECT_NE( counter.messages[ 2 ].find( "[3/3]" ), std::string::npos );
}

TEST_F( NovaMultilineFormatter, EmptyLinesSkippedByDefault )
{
	kmac::nova::extras::MultilineFormatter fmt( false, false );
	const char* msg = "first\n\nthird";
	auto rec = makeRecord( "TAG", "f.cpp", "fn", 1, 0, msg, static_cast< std::uint32_t >( std::strlen( msg ) ) );
	fmt.formatAndWrite( rec, counter );

	// empty middle line should be skipped - only 2 records
	EXPECT_EQ( counter.count.load(), 2u );
}

TEST_F( NovaMultilineFormatter, EmptyLinesPreservedWhenEnabled )
{
	kmac::nova::extras::MultilineFormatter fmt( false, true );
	const char* msg = "first\n\nthird";
	auto rec = makeRecord( "TAG", "f.cpp", "fn", 1, 0, msg, static_cast< std::uint32_t >( std::strlen( msg ) ) );
	fmt.formatAndWrite( rec, counter );

	// empty middle line should be preserved - 3 records
	EXPECT_EQ( counter.count.load(), 3u );
}

// ============================================================================
// LargePayloadFormatter
// ============================================================================

class NovaLargePayloadFormatter : public ::testing::Test
{
protected:
	CounterSink counter;

	void SetUp() override
	{
		counter.clear();
	}
};

TEST_F( NovaLargePayloadFormatter, EmitsBeginAndEndMarkers )
{
	const char* payload = "some data";
	kmac::nova::extras::LargePayloadFormatter fmt( payload, std::strlen( payload ) );

	auto rec = makeRecord( "TAG", "f.cpp", "fn", 1, 0, "ignored", 7 );
	fmt.formatAndWrite( rec, counter );

	// must contain at least the BEGIN and END records
	ASSERT_GE( counter.count.load(), 2u );

	const std::string first = counter.messages.front();
	const std::string last = counter.messages.back();
	EXPECT_NE( first.find( "BEGIN_PAYLOAD" ), std::string::npos );
	EXPECT_NE( last.find( "END_PAYLOAD" ), std::string::npos );
}

TEST_F( NovaLargePayloadFormatter, AllPayloadBytesDelivered )
{
	// build a payload larger than the default chunk size
	std::string payload( 10000, 'P' );
	kmac::nova::extras::LargePayloadFormatter fmt( payload.c_str(), payload.size() );

	auto rec = makeRecord( "TAG", "f.cpp", "fn", 1, 0, "ign", 3 );
	fmt.formatAndWrite( rec, counter );

	// assemble all chunk records (skip BEGIN/END markers)
	std::string assembled;
	for ( std::size_t i = 1; i + 1 < counter.messages.size(); ++i )
	{
		assembled += counter.messages[ i ];
	}

	// every byte of the original payload must appear in the chunks
	EXPECT_EQ( assembled.size(), payload.size() );
	EXPECT_EQ( assembled, payload );
}

TEST_F( NovaLargePayloadFormatter, SmallPayloadStillHasMarkers )
{
	const char* payload = "tiny";
	kmac::nova::extras::LargePayloadFormatter fmt( payload, std::strlen( payload ) );

	auto rec = makeRecord( "TAG", "f.cpp", "fn", 1, 0, "ign", 3 );
	fmt.formatAndWrite( rec, counter );

	// even a small payload must have BEGIN and END
	ASSERT_GE( counter.count.load(), 2u );
	EXPECT_NE( counter.messages.front().find( "BEGIN_PAYLOAD" ), std::string::npos );
	EXPECT_NE( counter.messages.back().find( "END_PAYLOAD" ), std::string::npos );
}

TEST_F( NovaLargePayloadFormatter, ChunkCountMatchesExpected )
{
	const std::size_t chunkSize = kmac::nova::extras::LargePayloadFormatter( "x", 1 ).maxChunkSize();

	// construct a payload that requires exactly 2 chunks
	std::string payload( 2 * chunkSize, 'X' );
	kmac::nova::extras::LargePayloadFormatter fmt( payload.c_str(), payload.size() );

	auto rec = makeRecord( "TAG", "f.cpp", "fn", 1, 0, "ign", 3 );
	fmt.formatAndWrite( rec, counter );

	// expected: 1 BEGIN + 2 chunks + 1 END = 4 records
	EXPECT_EQ( counter.count.load(), 4u );
}
