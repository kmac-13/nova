// test_nova_rolling_file_sink.cpp
// Comprehensive tests for RollingFileSink

#include "kmac/nova/record.h"
#include "kmac/nova/extras/buffer.h"
#include "kmac/nova/extras/formatter.h"
#include "kmac/nova/extras/rolling_file_sink.h"

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

// ============================================================================
// Helper: Capture Sink
// ============================================================================

class TestFormatter final : public kmac::nova::extras::Formatter
{
private:
	bool _done = false;

public:
	void begin( const kmac::nova::Record& ) noexcept override
	{
		_done = false;
	}

	bool format( const kmac::nova::Record&, kmac::nova::extras::Buffer& buf ) noexcept override
	{
		if ( _done )
		{
			return true;
		}

		// try to append the entire message at once
		if ( buf.appendLiteral( "TEST\n" ) )
		{
			_done = true;
			return true;
		}

		// not enough space: caller must flush and retry
		return false;
	}
};

class TempDir
{
private:
	std::filesystem::path _path;

public:
	TempDir()
	{
		_path = std::filesystem::temp_directory_path()
			/ std::filesystem::path( "rolling_sink_test_" + std::to_string( ::getpid() ) );

		std::filesystem::create_directories( _path );
	}

	~TempDir()
	{
		std::filesystem::remove_all( _path );
	}

	std::filesystem::path path() const
	{
		return _path;
	}
};


TEST( RollingFileSink, CreatesInitialFile )
{
	TempDir dir;
	TestFormatter formatter;

	const std::string base = ( dir.path() / "test.log" ).string();

	kmac::nova::extras::RollingFileSink sink( base, 1024, &formatter );

	EXPECT_TRUE( std::filesystem::exists( base + ".1" ) );
}


TEST( RollingFileSink, WritesFormattedRecord )
{
	TempDir dir;
	TestFormatter formatter;

	const std::string base = ( dir.path() / "test.log" ).string();

	kmac::nova::extras::RollingFileSink sink(
		base,
		1024,
		&formatter
	);

	kmac::nova::Record record {};
	sink.process( record );
	sink.flush();

	const std::string filename = base + ".1";

	std::ifstream in( filename );
	std::string contents(
		( std::istreambuf_iterator< char >( in ) ),
		std::istreambuf_iterator< char >()
	);

	EXPECT_EQ( contents, "TEST\n" );
}


TEST( RollingFileSink, RotatesOnMaxSize )
{
	TempDir dir;  // This should clean up before test starts
	TestFormatter formatter;

	const std::string base = ( dir.path() / "rotate.log" ).string();

	// ensure no files exist before test
	for ( int i = 1; i <= 10; ++i )
	{
		std::filesystem::remove( base + "." + std::to_string(i) );
	}

	kmac::nova::extras::RollingFileSink sink( base, 6 /* size of "TEST" */, &formatter );

	kmac::nova::Record record {};

	sink.process( record );
	sink.process( record ); // should rotate here
	sink.flush();

	const std::string f1 = base + ".1";
	const std::string f2 = base + ".2";

	ASSERT_TRUE( std::filesystem::exists( f1 ) );
	ASSERT_TRUE( std::filesystem::exists( f2 ) );

	auto readFile = []( const std::string& path )
	{
		std::ifstream in( path, std::ios::binary );
		return std::string(
			( std::istreambuf_iterator< char >( in ) ),
			std::istreambuf_iterator< char >()
		);
	};

	const std::string c1 = readFile( f1 );
	const std::string c2 = readFile( f2 );

	EXPECT_EQ( c1, "TEST\n" );
	EXPECT_EQ( c2, "TEST\n" );
}


TEST( RollingFileSink, RolloverCallbackCalled )
{
	TempDir dir;
	TestFormatter formatter;

	const std::string base = ( dir.path() / "callback.log" ).string();

	bool called = false;

	kmac::nova::extras::RollingFileSink sink( base, 5, &formatter );

	sink.setRolloverCallback(
		[ & ]( const std::string& oldFile, const std::string& newFile )
		{
			called = true;
			EXPECT_TRUE( std::filesystem::exists( oldFile ) );
			EXPECT_EQ( newFile, base + ".2" );
		}
	);

	kmac::nova::Record record {};
	sink.process( record );
	sink.process( record );

	EXPECT_TRUE( called );
}

TEST( RollingFileSink, ForceRotateCreatesNewFile )
{
	TempDir dir;
	TestFormatter formatter;

	const std::string base = ( dir.path() / "force.log" ).string();

	kmac::nova::extras::RollingFileSink sink( base, 1024, &formatter );

	sink.forceRotate();

	EXPECT_TRUE( std::filesystem::exists( base + ".2" ) );
}
