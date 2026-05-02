/**
 * @file test_integration.cpp
 * @brief Google Test integration tests for Nova + Flare
 */

#include "test_helpers.h"

#include "kmac/nova/nova.h"
#include "kmac/nova/scoped_configurator.h"
#include "kmac/nova/extras/composite_sink.h"
#include "kmac/nova/extras/filter_sink.h"
#include "kmac/nova/extras/formatting_sink.h"
#include "kmac/nova/extras/iso8601_formatter.h"
#include "kmac/nova/extras/ostream_sink.h"
#include "kmac/nova/extras/synchronized_sink.h"

#include "kmac/flare/file_writer.h"
#include "kmac/flare/emergency_sink.h"
#include "kmac/flare/reader.h"
#include "kmac/flare/record.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// test tags
struct AppTag { };
NOVA_LOGGER_TRAITS( AppTag, APP, true, TimestampHelper::steadyNanosecs );

struct ErrorTag { };
NOVA_LOGGER_TRAITS( ErrorTag, ERROR, true, TimestampHelper::steadyNanosecs );

struct DebugTag { };
NOVA_LOGGER_TRAITS( DebugTag, DEBUG, true, TimestampHelper::steadyNanosecs );


class Integration : public ::testing::Test
{
protected:
	std::filesystem::path tempDir;

	void SetUp() override
	{
		tempDir = test_helpers::createTempDirectory();
		ASSERT_FALSE( tempDir.empty() ) << "Failed to create temp directory";
	}

	void TearDown() override
	{
		test_helpers::removeTempDirectory( tempDir );
	}

	std::string getTempFilePath( const std::string& filename )
	{
		return ( tempDir / filename ).string();
	}

	std::vector<uint8_t> readFile( const std::string& filepath )
	{
		std::ifstream file( filepath, std::ios::binary );
		return std::vector< uint8_t >(
			std::istreambuf_iterator< char >( file ),
			std::istreambuf_iterator< char >()
			);
	}
};



TEST_F( Integration, BasicLoggingPipeline )
{
	std::ostringstream oss;
	kmac::nova::extras::OStreamSink sink( oss );

	kmac::nova::ScopedConfigurator config;
	config.bind< AppTag >( &sink );

	NOVA_LOG( AppTag ) << "Application started";
	NOVA_LOG( AppTag ) << "Processing data";
	NOVA_LOG( AppTag ) << "Application finished";

	std::string output = std::string( oss.str() );
	EXPECT_TRUE( ( output.find( "Application started" ) != std::string::npos ) );
	EXPECT_TRUE( ( output.find( "Processing data" ) != std::string::npos ) );
	EXPECT_TRUE( ( output.find( "Application finished" ) != std::string::npos ) );
}

TEST_F( Integration, MultipleTagsToCompositeSink )
{
	std::ostringstream oss;
	kmac::nova::extras::OStreamSink baseSink( oss );
	kmac::nova::extras::CompositeSink compositeSink;
	compositeSink.add( baseSink );

	kmac::nova::ScopedConfigurator config;
	config.bind< AppTag >( &compositeSink );
	config.bind< ErrorTag >( &compositeSink );
	config.bind< DebugTag >( &compositeSink );

	NOVA_LOG( AppTag ) << "App message";
	NOVA_LOG( ErrorTag ) << "Error message";
	NOVA_LOG( DebugTag ) << "Debug message";

	std::string output = oss.str();
	// OStreamSink doesn't include tags, just messages
	EXPECT_NE( output.find( "App message" ), std::string::npos );
	EXPECT_NE( output.find( "Error message" ), std::string::npos );
	EXPECT_NE( output.find( "Debug message" ), std::string::npos );
}

TEST_F( Integration, FilteredLoggingPipeline )
{
	std::ostringstream oss;
	kmac::nova::extras::OStreamSink baseSink( oss );

	// filter out debug messages
	kmac::nova::extras::FilterSink filterSink(
		baseSink,
		[]( const kmac::nova::Record& rec ) {
			// string-based tag name comparison
			// std::string tag( rec.tag );
			// return tag != "DEBUG";

			// new switch-based conditions on traits' tagId
			switch ( rec.tagId )
			{
			case kmac::nova::LoggerTraits< DebugTag >::tagId:
				return false;
			}
			return true;
		} );

	kmac::nova::ScopedConfigurator config;
	config.bind< AppTag >( &filterSink );
	config.bind< ErrorTag >( &filterSink );
	config.bind< DebugTag >( &filterSink );

	NOVA_LOG( AppTag ) << "App message";
	NOVA_LOG( ErrorTag ) << "Error message";
	NOVA_LOG( DebugTag ) << "Debug message";

	std::string output = std::string( oss.str() );
	EXPECT_TRUE( ( output.find( "App message" ) != std::string::npos ) );
	EXPECT_TRUE( ( output.find( "Error message" ) != std::string::npos ) );
	EXPECT_TRUE( ! ( output.find( "Debug message" ) != std::string::npos ) );
}

TEST_F( Integration, DualOutputPipeline )
{
	std::ostringstream console;
	std::string flareFile = getTempFilePath( "dual_output.flare" );

	kmac::nova::extras::OStreamSink consoleSink( console );

	std::FILE* file = std::fopen( flareFile.c_str(), "wb" );
	EXPECT_TRUE( file != nullptr );
	kmac::flare::FileWriter writer( file );
	kmac::flare::EmergencySink<> flareSink( &writer );

	kmac::nova::extras::CompositeSink compositeSink;
	compositeSink.add( consoleSink );
	compositeSink.add( flareSink );

	kmac::nova::ScopedConfigurator config;
	config.bind< AppTag >( &compositeSink );

	NOVA_LOG( AppTag ) << "Message 1";
	NOVA_LOG( AppTag ) << "Message 2";
	NOVA_LOG( AppTag ) << "Message 3";

	flareSink.flush();
	std::fclose( file );

	// verify console output
	std::string consoleOutput = std::string( console.str() );
	EXPECT_TRUE( ( consoleOutput.find( "Message 1" ) != std::string::npos ) );
	EXPECT_TRUE( ( consoleOutput.find( "Message 2" ) != std::string::npos ) );
	EXPECT_TRUE( ( consoleOutput.find( "Message 3" ) != std::string::npos ) );

	// verify flare file
	auto flareData = readFile( flareFile );
	EXPECT_TRUE( ! flareData.empty() );

	kmac::flare::Reader reader;
	kmac::flare::Record record;
	int count = 0;

	while ( reader.parseNext( flareData.data(), flareData.size(), record ) )
	{
		++count;
	}

	EXPECT_EQ( count, 3 );
}

TEST_F( Integration, ThreadSafeLogging )
{
	// collect into a vector rather than ostringstream -- ostringstream on MSVC
	// acquires an internal locale lock on every write() which causes intermittent
	// lost writes even when calls are serialized by an external mutex
	std::vector< std::string > messages;
	messages.reserve( 500 );

	class VectorSink : public kmac::nova::Sink
	{
	public:
		std::vector< std::string >& out_;
		explicit VectorSink( std::vector< std::string >& v ) : out_( v ) {}
		void process( const kmac::nova::Record& record ) noexcept override
		{
			// called under SynchronizedSink's mutex -- single-writer
			out_.emplace_back( record.message, record.messageSize );
		}
	};

	VectorSink baseSink( messages );
	kmac::nova::extras::SynchronizedSink syncSink( baseSink );

	kmac::nova::ScopedConfigurator config;
	config.bind< AppTag >( &syncSink );

	const int numThreads = 10;
	const int logsPerThread = 50;

	std::vector< std::thread > threads;
	for ( int i = 0; i < numThreads; ++i )
	{
		threads.emplace_back( [ i, logsPerThread ]() {
			for ( int j = 0; j < logsPerThread; ++j )
			{
				NOVA_LOG( AppTag ) << "Thread " << i << " log " << j;
			}
		} );
	}

	for ( auto& t : threads )
	{
		t.join();
	}

	ASSERT_EQ( messages.size(), static_cast< size_t >( numThreads * logsPerThread ) );

	for ( int i = 0; i < numThreads; ++i )
	{
		for ( int j = 0; j < logsPerThread; ++j )
		{
			std::string expected = "Thread " + std::to_string( i ) + " log " + std::to_string( j );
			auto it = std::find( messages.begin(), messages.end(), expected );
			EXPECT_NE( it, messages.end() ) << "Missing: " << expected;
		}
	}
}

TEST_F( Integration, FormattedAndEmergencyLogging )
{
	std::ostringstream oss;
	std::string flareFile = getTempFilePath( "formatted_emergency.flare" );

	kmac::nova::extras::OStreamSink baseSink( oss );
	kmac::nova::extras::ISO8601Formatter formatter;
	kmac::nova::extras::FormattingSink formattedSink( baseSink, formatter );

	std::FILE* file = std::fopen( flareFile.c_str(), "wb" );
	EXPECT_TRUE( file != nullptr );
	kmac::flare::FileWriter writer( file );
	kmac::flare::EmergencySink<> flareSink( &writer );

	kmac::nova::extras::CompositeSink compositeSink;
	compositeSink.add( formattedSink );
	compositeSink.add( flareSink );

	kmac::nova::ScopedConfigurator config;
	config.bind< ErrorTag >( &compositeSink );

	NOVA_LOG( ErrorTag ) << "Critical error occurred";
	NOVA_LOG( ErrorTag ) << "System state: unstable";

	flareSink.flush();
	std::fclose( file );

	// verify formatted output
	std::string consoleOutput = std::string( oss.str() );
	EXPECT_TRUE( ( consoleOutput.find( "ERROR") != std::string::npos ) );
	EXPECT_TRUE( ( consoleOutput.find( "Critical error occurred" ) != std::string::npos ) );

	// verify emergency file
	auto flareData = readFile( flareFile );
	EXPECT_TRUE( ! flareData.empty() );
}

TEST_F( Integration, ComplexLoggingArchitecture )
{
	// console output (formatted)
	std::ostringstream console;
	kmac::nova::extras::OStreamSink consoleSink( console );
	kmac::nova::extras::ISO8601Formatter formatter;
	kmac::nova::extras::FormattingSink formattedConsoleSink( consoleSink, formatter );

	// error file (emergency format)
	std::string errorFilePath = getTempFilePath( "errors.flare" );
	std::FILE* errorFile = std::fopen( errorFilePath.c_str(), "wb" );
	EXPECT_TRUE( errorFile != nullptr );
	kmac::flare::FileWriter errorWriter( errorFile );
	kmac::flare::EmergencySink<> errorSink( &errorWriter );

	// app file (emergency format)
	std::string appFilePath = getTempFilePath( "app.flare" );
	std::FILE* appFile = std::fopen( appFilePath.c_str(), "wb" );
	EXPECT_TRUE( appFile != nullptr );
	kmac::flare::FileWriter appWriter( appFile );
	kmac::flare::EmergencySink<> appSink( &appWriter );

	// error filter (only ERROR tag)
	kmac::nova::extras::FilterSink errorFilter(
		errorSink,
		[]( const kmac::nova::Record& rec ) {
			return std::string( rec.tag ) == "ERROR";
		}
		);

	// composite for all outputs
	kmac::nova::extras::CompositeSink allOutputs;
	allOutputs.add( formattedConsoleSink );
	allOutputs.add( errorFilter );
	allOutputs.add( appSink );

	// thread-safe wrapper
	kmac::nova::extras::SynchronizedSink syncSink( allOutputs );

	// bind tags
	kmac::nova::ScopedConfigurator config;
	config.bind< AppTag >( &syncSink );
	config.bind< ErrorTag >( &syncSink );
	config.bind< DebugTag >( &syncSink );

	// log various messages
	NOVA_LOG( AppTag ) << "Application started";
	NOVA_LOG( DebugTag ) << "Debug info";
	NOVA_LOG( ErrorTag ) << "Error occurred";
	NOVA_LOG( AppTag ) << "Processing";
	NOVA_LOG( ErrorTag ) << "Another error";

	errorSink.flush();
	appSink.flush();
	std::fclose( errorFile );
	std::fclose( appFile );

	// verify console has everything
	std::string consoleOutput = std::string( console.str() );
	EXPECT_TRUE( ( consoleOutput.find( "Application started" ) != std::string::npos ) );
	EXPECT_TRUE( ( consoleOutput.find( "Debug info" ) != std::string::npos ) );
	EXPECT_TRUE( ( consoleOutput.find( "Error occurred" ) != std::string::npos ) );

	// verify error file has only errors
	auto errorData = readFile( errorFilePath );
	kmac::flare::Reader errorReader;
	kmac::flare::Record record;
	int errorCount = 0;

	while ( errorReader.parseNext( errorData.data(), errorData.size(), record ) )
	{
		++errorCount;
		// should only have ERROR tag entries
	}

	EXPECT_EQ( errorCount, 2 );  // two error messages

	// verify app file has everything
	auto appData = readFile( appFilePath );
	kmac::flare::Reader appReader;
	int appCount = 0;

	while ( appReader.parseNext( appData.data(), appData.size(), record ) )
	{
		++appCount;
	}

	EXPECT_EQ( appCount, 5 );  // all five messages
}

TEST_F( Integration, RealWorldCrashScenario )
{
	std::string crashFile = getTempFilePath( "crash.flare" );
	std::FILE* file = std::fopen( crashFile.c_str(), "wb" );
	EXPECT_TRUE( file != nullptr );
	kmac::flare::FileWriter writer( file );
	kmac::flare::EmergencySink<> crashSink( &writer );

	kmac::nova::ScopedConfigurator config;
	config.bind< ErrorTag >( &crashSink );

	// simulate pre-crash logging
	NOVA_LOG( ErrorTag ) << "Memory allocation failed";
	NOVA_LOG( ErrorTag ) << "Attempting recovery";
	NOVA_LOG( ErrorTag ) << "Recovery failed - critical state";

	crashSink.flush();
	std::fclose( file );

	// read back the crash log
	auto crashData = readFile( crashFile );
	EXPECT_TRUE( ! crashData.empty() );

	kmac::flare::Reader reader;
	kmac::flare::Record record;
	std::vector< std::string > messages;

	while ( reader.parseNext( crashData.data(), crashData.size(), record ) )
	{
		messages.push_back( std::string( record.message.data() ) );
	}

	EXPECT_EQ( messages.size(), 3 );
	EXPECT_NE( messages[ 0 ].find( "Memory allocation failed" ), std::string::npos );
	EXPECT_NE( messages[ 1 ].find( "Attempting recovery" ), std::string::npos );
	EXPECT_NE( messages[ 2 ].find( "Recovery failed" ), std::string::npos );
}
