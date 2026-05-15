/**
 * @file test_nova_details.cpp
 * @brief Unit tests for Nova compile-time utilities (fnv1a, fileName).
 *
 * fnv1a and fileName are constexpr, so most correctness properties can be
 * verified at compile time via static_assert; the runtime TEST_F blocks
 * exist to catch regressions that sanitizers and coverage tools can observe,
 * and to make failures produce readable gtest diagnostics.
 */

#include "kmac/nova/details.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

using kmac::nova::details::fileName;
using kmac::nova::details::fnv1a;

// compile-time checks - verified at compile time, confirmed via static_assert
static_assert( fnv1a( "" ) != 0, "empty-string hash must not be zero" );
static_assert( fnv1a( "hello" ) != fnv1a( "world" ), "distinct inputs must produce distinct hashes" );
static_assert( fnv1a( "hello" ) == fnv1a( "hello" ), "identical inputs must produce identical hashes" );

static_assert( fileName( "file.cpp" )[ 0 ] == 'f', "no-separator path returns the string as-is" );
static_assert( fileName( "/a/b/c.cpp" )[ 0 ] == 'c', "unix path: last component is returned" );

// ============================================================================
// fnv1a tests
// ============================================================================

class NovaFnv1a : public ::testing::Test {};

TEST_F( NovaFnv1a, EmptyStringIsNonZero )
{
	// the FNV offset basis is non-zero, so the hash of "" must be non-zero
	EXPECT_NE( fnv1a( "" ), std::uint64_t( 0 ) );
}

TEST_F( NovaFnv1a, Deterministic )
{
	const std::uint64_t h1 = fnv1a( "Nova logging framework" );
	const std::uint64_t h2 = fnv1a( "Nova logging framework" );
	EXPECT_EQ( h1, h2 );
}

TEST_F( NovaFnv1a, DistinctInputsDistinctHashes )
{
	EXPECT_NE( fnv1a( "SENSOR" ), fnv1a( "NETWORK" ) );
	EXPECT_NE( fnv1a( "TRACE" ), fnv1a( "DEBUG" ) );
	EXPECT_NE( fnv1a( "INFO" ), fnv1a( "WARN" ) );
	EXPECT_NE( fnv1a( "ERROR" ), fnv1a( "FATAL" ) );
}

TEST_F( NovaFnv1a, CaseSensitive )
{
	// hashing must be byte-exact - differing only in case must produce different hashes
	EXPECT_NE( fnv1a( "sensor" ), fnv1a( "SENSOR" ) );
	EXPECT_NE( fnv1a( "nova" ), fnv1a( "Nova" ) );
}

TEST_F( NovaFnv1a, SingleCharacterDifference )
{
	EXPECT_NE( fnv1a( "abc" ), fnv1a( "abd" ) );
}

TEST_F( NovaFnv1a, PrefixNotEqualToString )
{
	// "TAG" must hash differently from "TAGS"
	EXPECT_NE( fnv1a( "TAG" ), fnv1a( "TAGS" ) );
}

TEST_F( NovaFnv1a, LongStringDeterministic )
{
	const char* longStr = "kmac::nova::extras::sensor::TemperatureHighResolutionTag";
	EXPECT_EQ( fnv1a( longStr ), fnv1a( longStr ) );
}

TEST_F( NovaFnv1a, LiteralAndPointerOverloadAgree )
{
	// fnv1a<N> (literal) and fnv1a(const char*) must produce the same result
	const char* ptr = "MATCH_TEST";
	const std::uint64_t fromLiteral = fnv1a( "MATCH_TEST" );
	const std::uint64_t fromPointer = fnv1a( ptr );
	EXPECT_EQ( fromLiteral, fromPointer );
}

TEST_F( NovaFnv1a, AvalancheChangesUpperBits )
{
	// the avalanche mix should spread bit changes throughout all 64 bits,
	// including the upper 32; verify that short inputs don't produce hashes
	// where the upper 32 bits are always identical
	const std::uint64_t h1 = fnv1a( "A" );
	const std::uint64_t h2 = fnv1a( "B" );

	const std::uint32_t upper1 = static_cast< std::uint32_t >( h1 >> 32U );
	const std::uint32_t upper2 = static_cast< std::uint32_t >( h2 >> 32U );

	// upper halves must differ (verifies avalanche mix fires)
	EXPECT_NE( upper1, upper2 );
}

// ============================================================================
// fileName tests
// ============================================================================

class NovaFileName : public ::testing::Test {};

TEST_F( NovaFileName, NoSeparatorReturnsInput )
{
	EXPECT_STREQ( fileName( "file.cpp" ), "file.cpp" );
	EXPECT_STREQ( fileName( "main.cpp" ), "main.cpp" );
}

TEST_F( NovaFileName, UnixPathReturnsLastComponent )
{
	EXPECT_STREQ( fileName( "/home/user/project/src/file.cpp" ), "file.cpp" );
	EXPECT_STREQ( fileName( "/a/b/c/d.h" ), "d.h" );
}

TEST_F( NovaFileName, WindowsPathReturnsLastComponent )
{
	EXPECT_STREQ( fileName( "C:\\Users\\dev\\project\\src\\file.cpp" ), "file.cpp" );
	EXPECT_STREQ( fileName( "C:\\a\\b\\c.h" ), "c.h" );
}

TEST_F( NovaFileName, SingleLevelUnixPath )
{
	EXPECT_STREQ( fileName( "/file.cpp" ), "file.cpp" );
}

TEST_F( NovaFileName, SingleLevelWindowsPath )
{
	EXPECT_STREQ( fileName( "C:\\file.cpp" ), "file.cpp" );
}

TEST_F( NovaFileName, MixedSeparators )
{
	// mixed separators can appear in cmake-generated __FILE__ on Windows
	EXPECT_STREQ( fileName( "/project/src\\file.cpp" ), "file.cpp" );
}

TEST_F( NovaFileName, TrailingSeparatorReturnedAsEmpty )
{
	// "dir/" - last component after the final slash is ""
	EXPECT_STREQ( fileName( "dir/" ), "" );
}

TEST_F( NovaFileName, NestedPathDepth )
{
	EXPECT_STREQ( fileName( "a/b/c/d/e/f/g/h.cpp" ), "h.cpp" );
}
