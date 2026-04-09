/**
 * @file test_nova_custom_formatter.cpp
 * @brief Unit tests for CustomFormatter
 *
 * Tests cover:
 *  - single and multi-field layouts
 *  - all Field types (Tag, Message, File, Function, Line, TimestampRaw, TimestampISO, None)
 *  - open/close delimiter emission and '\0' suppression
 *  - Field::None literal-only output
 *  - null-safe fields (null tag, file, function)
 *  - buffer-overflow resume: record larger than the buffer correctly resumes
 *  - debug assert fires when begin() is called before format() completes
 *  - empty record (zero-length message)
 *  - TimestampISO produces valid ISO 8601 output
 *  - TimestampRaw produces the exact decimal nanosecond value
 *  - integration through FormattingSink
 */

#include "kmac/nova/nova.h"
#include "kmac/nova/record.h"
#include "kmac/nova/scoped_configurator.h"
#include "kmac/nova/extras/buffer.h"
#include "kmac/nova/extras/custom_formatter.h"
#include "kmac/nova/extras/formatting_sink.h"
#include "kmac/nova/extras/ostream_sink.h"

#include <gtest/gtest.h>

#include <cstring>
#include <sstream>
#include <string>

using namespace kmac::nova::extras;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{

/// Build a fully-populated Record pointing at the provided string storage.
kmac::nova::Record makeRecord(
	const char* tag,
	const char* file,
	const char* function,
	std::uint32_t line,
	std::uint64_t timestamp,
	const char* message,
	std::size_t messageSize
) noexcept
{
	kmac::nova::Record r{};
	r.tag = tag;
	r.tagId = 0;
	r.file = file;
	r.function = function;
	r.line = line;
	r.timestamp = timestamp;
	r.message = message;
	r.messageSize = messageSize;
	return r;
}

/// Drive a Formatter to completion and return the output as a std::string.
/// Uses a tiny buffer size to exercise the multi-call resume path.
std::string formatRecord(
	Formatter& formatter,
	const kmac::nova::Record& record,
	std::size_t bufSize = 4096
)
{
	std::string output;
	formatter.begin( record );
	bool done = false;
	while ( ! done )
	{
		std::string chunk( bufSize, '\0' );
		Buffer buf( chunk.data(), bufSize );
		done = formatter.format( record, buf );
		output.append( chunk.data(), buf.size() );
	}
	return output;
}

/// Fixed timestamp: 2024-01-01T00:00:00.000Z -> nanoseconds since Unix epoch.
static constexpr std::uint64_t FIXED_TIMESTAMP = 1704067200000000000ULL;

static const char* const TAG = "INFO";
static const char* const SRC_FILE = "myfile.cpp";
static const char* const FUNC_NAME = "myFunction";
static constexpr std::uint32_t LINE_NUM = 42;

} // namespace

// ---------------------------------------------------------------------------
// Tag definitions needed for the FormattingSink integration test
// ---------------------------------------------------------------------------
struct CustomFormatterTestTag {};
static std::uint64_t customFormatterTimestamp() noexcept { return FIXED_TIMESTAMP; }
NOVA_LOGGER_TRAITS( CustomFormatterTestTag, CFTEST, true, customFormatterTimestamp );

// ===========================================================================
// Test fixture
// ===========================================================================

class CustomFormatterTest : public ::testing::Test
{
protected:
	kmac::nova::Record _record;

	void SetUp() override
	{
		_record = makeRecord( TAG, SRC_FILE, FUNC_NAME, LINE_NUM, FIXED_TIMESTAMP, "hello world", 11 );
	}
};

// ===========================================================================
// Basic single-field layouts
// ===========================================================================

TEST_F( CustomFormatterTest, TagOnly )
{
	CustomFormatter< FieldSpec< '\0', Field::Tag, '\0' > > fmt;
	const std::string out = formatRecord( fmt, _record );
	EXPECT_EQ( out, "INFO" );
}

TEST_F( CustomFormatterTest, MessageOnly )
{
	CustomFormatter< FieldSpec< '\0', Field::Message, '\0' > > fmt;
	const std::string out = formatRecord( fmt, _record );
	EXPECT_EQ( out, "hello world" );
}

TEST_F( CustomFormatterTest, FileOnly )
{
	CustomFormatter< FieldSpec< '\0', Field::File, '\0' > > fmt;
	const std::string out = formatRecord( fmt, _record );
	EXPECT_EQ( out, "myfile.cpp" );
}

TEST_F( CustomFormatterTest, FunctionOnly )
{
	CustomFormatter< FieldSpec< '\0', Field::Function, '\0' > > fmt;
	const std::string out = formatRecord( fmt, _record );
	EXPECT_EQ( out, "myFunction" );
}

TEST_F( CustomFormatterTest, LineOnly )
{
	CustomFormatter< FieldSpec< '\0', Field::Line, '\0' > > fmt;
	const std::string out = formatRecord( fmt, _record );
	EXPECT_EQ( out, "42" );
}

TEST_F( CustomFormatterTest, TimestampRawOnly )
{
	CustomFormatter< FieldSpec< '\0', Field::TimestampRaw, '\0' > > fmt;
	const std::string out = formatRecord( fmt, _record );
	EXPECT_EQ( out, "1704067200000000000" );
}

TEST_F( CustomFormatterTest, TimestampISOOnly )
{
	CustomFormatter< FieldSpec< '\0', Field::TimestampISO, '\0' > > fmt;
	const std::string out = formatRecord( fmt, _record );
	EXPECT_EQ( out, "2024-01-01T00:00:00.000Z" );
}

// ===========================================================================
// Delimiters
// ===========================================================================

TEST_F( CustomFormatterTest, OpenDelimiterEmitted )
{
	CustomFormatter< FieldSpec< '[', Field::Tag, '\0' > > fmt;
	const std::string out = formatRecord( fmt, _record );
	EXPECT_EQ( out, "[INFO" );
}

TEST_F( CustomFormatterTest, CloseDelimiterEmitted )
{
	CustomFormatter< FieldSpec< '\0', Field::Tag, ']' > > fmt;
	const std::string out = formatRecord( fmt, _record );
	EXPECT_EQ( out, "INFO]" );
}

TEST_F( CustomFormatterTest, BothDelimitersEmitted )
{
	CustomFormatter< FieldSpec< '[', Field::Tag, ']' > > fmt;
	const std::string out = formatRecord( fmt, _record );
	EXPECT_EQ( out, "[INFO]" );
}

TEST_F( CustomFormatterTest, NullOpenNotEmitted )
{
	// '\0' means suppress the character entirely
	CustomFormatter< FieldSpec< '\0', Field::Tag, ' ' > > fmt;
	const std::string out = formatRecord( fmt, _record );
	EXPECT_EQ( out, "INFO " );
}

TEST_F( CustomFormatterTest, NullCloseNotEmitted )
{
	CustomFormatter< FieldSpec< ' ', Field::Tag, '\0' > > fmt;
	const std::string out = formatRecord( fmt, _record );
	EXPECT_EQ( out, " INFO" );
}

// ===========================================================================
// Field::None - delimiter-only literal
// ===========================================================================

TEST_F( CustomFormatterTest, FieldNoneEmitsBothDelimiters )
{
	CustomFormatter< FieldSpec< '|', Field::None, '|' > > fmt;
	const std::string out = formatRecord( fmt, _record );
	EXPECT_EQ( out, "||" );
}

TEST_F( CustomFormatterTest, FieldNoneAsSpaceSeparator )
{
	CustomFormatter<
		FieldSpec< '\0', Field::Tag,     '\0' >,
		FieldSpec< ' ',  Field::None,    ' '  >,
		FieldSpec< '\0', Field::Message, '\0' >
	> fmt;
	const std::string out = formatRecord( fmt, _record );
	EXPECT_EQ( out, "INFO  hello world" );
}

// ===========================================================================
// Multi-field layout
// ===========================================================================

TEST_F( CustomFormatterTest, TypicalLayout )
{
	// "[INFO] myfile.cpp:42 myFunction - hello world\n"
	// Note: FieldSpec<' ', None, '-'> emits " -", then FieldSpec<' ', Message, '\n'> emits " hello world\n"
	CustomFormatter<
		FieldSpec< '[',  Field::Tag,      ']'  >,
		FieldSpec< ' ',  Field::File,     ':'  >,
		FieldSpec< '\0', Field::Line,     ' '  >,
		FieldSpec< '\0', Field::Function, '\0' >,
		FieldSpec< ' ',  Field::None,     '-'  >,
		FieldSpec< ' ',  Field::Message,  '\n' >
	> fmt;

	const std::string out = formatRecord( fmt, _record );
	// " -" from None spec + " hello world\n" from Message spec -> " - hello world\n"
	EXPECT_EQ( out, "[INFO] myfile.cpp:42 myFunction - hello world\n" );
}

TEST_F( CustomFormatterTest, TimestampISOInLayout )
{
	CustomFormatter<
		FieldSpec< '\0', Field::TimestampISO, ' '  >,
		FieldSpec< '[',  Field::Tag,          ']'  >,
		FieldSpec< ' ',  Field::Message,      '\n' >
	> fmt;

	const std::string out = formatRecord( fmt, _record );
	EXPECT_EQ( out, "2024-01-01T00:00:00.000Z [INFO] hello world\n" );
}

TEST_F( CustomFormatterTest, MultipleFieldsOrdering )
{
	// verify specs emit in declaration order
	CustomFormatter<
		FieldSpec< '\0', Field::Message, '\0' >,
		FieldSpec< '/',  Field::Tag,     '\0' >
	> fmt;
	const std::string out = formatRecord( fmt, _record );
	EXPECT_EQ( out, "hello world/INFO" );
}

// ===========================================================================
// Null-safe fields
// ===========================================================================

TEST_F( CustomFormatterTest, NullTagEmitsEmpty )
{
	auto rec = _record;
	rec.tag = nullptr;
	CustomFormatter< FieldSpec< '[', Field::Tag, ']' > > fmt;
	const std::string out = formatRecord( fmt, rec );
	EXPECT_EQ( out, "[]" );
}

TEST_F( CustomFormatterTest, NullFileEmitsEmpty )
{
	auto rec = _record;
	rec.file = nullptr;
	CustomFormatter< FieldSpec< '\0', Field::File, '\0' > > fmt;
	const std::string out = formatRecord( fmt, rec );
	EXPECT_EQ( out, "" );
}

TEST_F( CustomFormatterTest, NullFunctionEmitsEmpty )
{
	auto rec = _record;
	rec.function = nullptr;
	CustomFormatter< FieldSpec< '(', Field::Function, ')' > > fmt;
	const std::string out = formatRecord( fmt, rec );
	EXPECT_EQ( out, "()" );
}

// ===========================================================================
// Empty message
// ===========================================================================

TEST_F( CustomFormatterTest, EmptyMessage )
{
	auto rec = _record;
	rec.message     = "";
	rec.messageSize = 0;
	CustomFormatter< FieldSpec< '[', Field::Message, ']' > > fmt;
	const std::string out = formatRecord( fmt, rec );
	// delimiters still emitted; field content is empty
	EXPECT_EQ( out, "[]" );
}

// ===========================================================================
// Buffer-overflow resume path
// ===========================================================================

TEST_F( CustomFormatterTest, ResumesCorrectlyWithTinyBuffer )
{
	// force many format() calls by using a 1-byte buffer;
	// the total output must equal the same full string produced by a big buffer
	using FmtType = CustomFormatter<
		FieldSpec< '[',  Field::Tag,      ']'  >,
		FieldSpec< ' ',  Field::File,     ':'  >,
		FieldSpec< '\0', Field::Line,     ' '  >,
		FieldSpec< '\0', Field::Function, '\0' >,
		FieldSpec< ' ',  Field::None,     '-'  >,
		FieldSpec< ' ',  Field::Message,  '\n' >
	>;

	FmtType fmtBig;
	FmtType fmtTiny;

	const std::string bigOut  = formatRecord( fmtBig,  _record, 4096 );
	const std::string tinyOut = formatRecord( fmtTiny, _record, 1 );

	EXPECT_EQ( bigOut, tinyOut );
	EXPECT_FALSE( bigOut.empty() );
}

TEST_F( CustomFormatterTest, ResumesWithSmallBufferAndLargeMessage )
{
	// 500-byte message to guarantee multiple buffer fills with a 64-byte buffer
	const std::string bigMsg( 500, 'X' );
	auto rec = _record;
	rec.message = bigMsg.data();
	rec.messageSize = bigMsg.size();

	using FmtType = CustomFormatter< FieldSpec< '\0', Field::Message, '\n' > >;
	FmtType fmtBig;
	FmtType fmtTiny;

	const std::string bigOut = formatRecord( fmtBig, rec, 4096 );
	const std::string tinyOut = formatRecord( fmtTiny, rec, 64 );

	EXPECT_EQ( bigOut, tinyOut );
	EXPECT_EQ( bigOut, bigMsg + "\n" );
}

// ===========================================================================
// Multiple records in sequence
// ===========================================================================

TEST_F( CustomFormatterTest, MultipleRecordsInSequence )
{
	CustomFormatter< FieldSpec< '\0', Field::Message, '\n' > > fmt;

	const std::string msg1 = "first";
	const std::string msg2 = "second";
	const std::string msg3 = "third";

	auto r1 = makeRecord( TAG, SRC_FILE, FUNC_NAME, LINE_NUM, FIXED_TIMESTAMP, msg1.data(), msg1.size() );
	auto r2 = makeRecord( TAG, SRC_FILE, FUNC_NAME, LINE_NUM, FIXED_TIMESTAMP, msg2.data(), msg2.size() );
	auto r3 = makeRecord( TAG, SRC_FILE, FUNC_NAME, LINE_NUM, FIXED_TIMESTAMP, msg3.data(), msg3.size() );

	EXPECT_EQ( formatRecord( fmt, r1 ), "first\n"  );
	EXPECT_EQ( formatRecord( fmt, r2 ), "second\n" );
	EXPECT_EQ( formatRecord( fmt, r3 ), "third\n"  );
}

// ===========================================================================
// Debug assert: begin() before previous record completed
// ===========================================================================

#ifndef NDEBUG
TEST_F( CustomFormatterTest, AssertOnBeginBeforeComplete )
{
	CustomFormatter< FieldSpec< '\0', Field::Message, '\0' > > fmt;

	char storage[ 4096 ];
	Buffer buf( storage, sizeof( storage ) );

	// begin() then call format() and *don't* consume the true return, but then
	// call begin() a second time, which should assert
	fmt.begin( _record );

	// call format(), but deliberately ignore the return value so _inProgress stays
	// true.  we need to NOT call it at all actually, just call begin() again.
	// the assert fires in begin() when _inProgress == true.
	EXPECT_DEATH( fmt.begin( _record ), "begin\\(\\) called before previous record completed" );
}
#endif

// ===========================================================================
// TimestampISO format correctness
// ===========================================================================

TEST_F( CustomFormatterTest, TimestampISOHasCorrectFormat )
{
	CustomFormatter< FieldSpec< '\0', Field::TimestampISO, '\0' > > fmt;
	const std::string out = formatRecord( fmt, _record );

	// "YYYY-MM-DDTHH:MM:SS.mmmZ" = 24 chars
	EXPECT_EQ( out.size(), 24u );

	// make sure it contains a T
	EXPECT_NE( out.find( 'T' ), std::string::npos );

	// make sure it ends with a Z
	EXPECT_EQ( out.back(), 'Z' );
}

// ===========================================================================
// TimestampRaw decimal correctness
// ===========================================================================

TEST_F( CustomFormatterTest, TimestampRawMatchesExpectedValue )
{
	CustomFormatter< FieldSpec< '\0', Field::TimestampRaw, '\0' > > fmt;
	const std::string out = formatRecord( fmt, _record );
	EXPECT_EQ( out, std::to_string( FIXED_TIMESTAMP ) );
}

TEST_F( CustomFormatterTest, TimestampRawZero )
{
	auto rec = _record;
	rec.timestamp = 0;
	CustomFormatter< FieldSpec< '\0', Field::TimestampRaw, '\0' > > fmt;
	const std::string out = formatRecord( fmt, rec );
	EXPECT_EQ( out, "0" );
}

// ===========================================================================
// Line number edge cases
// ===========================================================================

TEST_F( CustomFormatterTest, LineZero )
{
	auto rec = _record;
	rec.line = 0;
	CustomFormatter< FieldSpec< '\0', Field::Line, '\0' > > fmt;
	const std::string out = formatRecord( fmt, rec );
	EXPECT_EQ( out, "0" );
}

TEST_F( CustomFormatterTest, LineLargeValue )
{
	auto rec = _record;
	rec.line = 999999;
	CustomFormatter< FieldSpec< '\0', Field::Line, '\0' > > fmt;
	const std::string out = formatRecord( fmt, rec );
	EXPECT_EQ( out, "999999" );
}

// ===========================================================================
// Integration: CustomFormatter through FormattingSink
// ===========================================================================

TEST_F( CustomFormatterTest, IntegrationViaFormattingSink )
{
	using FmtType = CustomFormatter<
		FieldSpec< '\0', Field::TimestampISO, ' '  >,
		FieldSpec< '[',  Field::Tag,          ']'  >,
		FieldSpec< ' ',  Field::Message,      '\n' >
	>;

	std::ostringstream oss;
	OStreamSink baseSink( oss );
	FmtType formatter;
	FormattingSink<> sink( baseSink, formatter );

	kmac::nova::ScopedConfigurator config;
	config.bind< CustomFormatterTestTag >( &sink );

	NOVA_LOG( CustomFormatterTestTag ) << "integration test";

	const std::string out = oss.str();
	EXPECT_NE( out.find( "2024-01-01T00:00:00" ), std::string::npos );
	EXPECT_NE( out.find( "[CFTEST]" ),             std::string::npos );
	EXPECT_NE( out.find( "integration test" ),     std::string::npos );
	EXPECT_EQ( out.back(), '\n' );
}
