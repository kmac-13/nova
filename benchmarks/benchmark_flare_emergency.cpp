/**
 * @file benchmark_flare_emergency.cpp
 * @brief Benchmark Flare's async-signal-safe emergency logging
 *
 * Tests include:
 * - EmergencySink write performance
 * - TLV encoding overhead
 * - buffer management
 * - multi-threaded emergency logging
 * - read-back performance
 */

#include <benchmark/benchmark.h>

#include "kmac/flare/emergency_sink.h"
#include "kmac/flare/file_writer.h"
#include "kmac/flare/reader.h"
#include "kmac/flare/tlv.h"

#include "kmac/nova/nova.h"
#include "kmac/nova/scoped_configurator.h"
#include "kmac/nova/timestamp_helper.h"

#include <cstdio>
#include <cstring>
#include <vector>

// ============================================================================
// Test Tags
// ============================================================================

struct EmergencyTag { };
NOVA_LOGGER_TRAITS( EmergencyTag, EMERGENCY, true, kmac::nova::TimestampHelper::steadyNanosecs );

struct CrashTag { };
NOVA_LOGGER_TRAITS( CrashTag, CRASH, true, kmac::nova::TimestampHelper::steadyNanosecs );

// ============================================================================
// Test Infrastructure
// ============================================================================

// Helper to clean up test files
struct FileCleanup
{
	explicit FileCleanup( const char* path ) : path_( path )
	{
		std::remove( path_ );
	}

	~FileCleanup()
	{
		std::remove( path_ );
	}

	const char* path_;
};

// ============================================================================
// Basic Emergency Logging Benchmarks
// ============================================================================

static void BM_Flare_EmergencySink_SimpleMessage( benchmark::State& state )
{
	const char* filename = "./tmp_flare_simple.bin";
	FileCleanup cleanup( filename );

	std::FILE* file = std::fopen( filename, "wb" );
	if ( ! file )
	{
		state.SkipWithError( "Failed to open file" );
		return;
	}

	kmac::flare::FileWriter writer( file );
	kmac::flare::EmergencySink sink( &writer, false );

	kmac::nova::ScopedConfigurator config;
	config.bind< EmergencyTag >( &sink );

	std::uint64_t counter = 0;

	for ( auto _ : state )
	{
		NOVA_LOG_STACK( EmergencyTag ) << "Emergency event " << counter++;
	}

	std::fclose( file );

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Flare_EmergencySink_SimpleMessage );

static void BM_Flare_EmergencySink_WithMetadata( benchmark::State& state )
{
	const char* filename = "./tmp_flare_metadata.bin";
	FileCleanup cleanup( filename );

	std::FILE* file = std::fopen( filename, "wb" );
	if ( ! file )
	{
		state.SkipWithError( "Failed to open file" );
		return;
	}

	kmac::flare::FileWriter writer( file );
	kmac::flare::EmergencySink sink( &writer, true ); // capture process info

	kmac::nova::ScopedConfigurator config;
	config.bind< EmergencyTag >( &sink );

	std::uint64_t counter = 0;

	for ( auto _ : state )
	{
		NOVA_LOG_STACK( EmergencyTag ) << "Event with PID/TID: " << counter++;
	}

	std::fclose( file );

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Flare_EmergencySink_WithMetadata );

// ============================================================================
// Message Size Scaling
// ============================================================================

static void BM_Flare_MessageSize( benchmark::State& state )
{
	const char* filename = "./tmp_flare_sizes.bin";
	FileCleanup cleanup( filename );

	std::FILE* file = std::fopen( filename, "wb" );
	if ( ! file )
	{
		state.SkipWithError( "Failed to open file" );
		return;
	}

	kmac::flare::FileWriter writer( file );
	kmac::flare::EmergencySink sink( &writer, false );

	kmac::nova::ScopedConfigurator config;
	config.bind< EmergencyTag >( &sink );

	// create message of specified size
	std::string message( state.range( 0 ), 'X' );

	for ( auto _ : state )
	{
		NOVA_LOG_STACK( EmergencyTag ) << message;
	}

	std::fclose( file );

	state.SetBytesProcessed( state.iterations() * state.range( 0 ) );
	state.SetLabel( std::to_string( state.range( 0 ) ) + " bytes" );
}
BENCHMARK( BM_Flare_MessageSize )->Range( 8, 4096 );

// ============================================================================
// Crash Scenario Simulation
// ============================================================================

static void BM_Flare_CrashScenario( benchmark::State& state )
{
	const char* filename = "./tmp_flare_crash.bin";
	FileCleanup cleanup( filename );

	std::FILE* file = std::fopen( filename, "wb" );
	if ( !file )
	{
		state.SkipWithError( "Failed to open file" );
		return;
	}

	kmac::flare::FileWriter writer( file );
	kmac::flare::EmergencySink sink( &writer, true );

	kmac::nova::ScopedConfigurator config;
	config.bind< CrashTag >( &sink );

	for ( auto _ : state )
	{
		// simulate crash logging: signal number, fault address, etc.
		NOVA_LOG_STACK( CrashTag ) << "SIGSEGV at address 0xDEADBEEF";
	}

	std::fclose( file );

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Flare_CrashScenario );

// ============================================================================
// Multi-Threaded Emergency Logging
// ============================================================================

static void BM_Flare_MultiThreaded( benchmark::State& state )
{
	const char* filename = "./tmp_flare_mt.bin";
	FileCleanup cleanup( filename );

	std::FILE* file = std::fopen( filename, "wb" );
	if ( ! file )
	{
		state.SkipWithError( "Failed to open file" );
		return;
	}

	kmac::flare::FileWriter writer( file );
	kmac::flare::EmergencySink sink( &writer, false );

	kmac::nova::ScopedConfigurator config;
	config.bind< EmergencyTag >( &sink );

	std::uint64_t counter = 0;

	for ( auto _ : state )
	{
		NOVA_LOG_STACK( EmergencyTag ) << "Thread event " << counter++;
	}

	std::fclose( file );

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Flare_MultiThreaded )->Threads( 1 )->Threads( 2 )->Threads( 4 );

// ============================================================================
// Read-Back Performance
// ============================================================================

static void BM_Flare_ReadBack( benchmark::State& state )
{
	const char* filename = "./tmp_flare_readback.bin";
	FileCleanup cleanup( filename );

	// first, write some records
	{
		std::FILE* file = std::fopen( filename, "wb" );
		if ( ! file )
		{
			state.SkipWithError( "Failed to open file for writing" );
			return;
		}

		kmac::flare::FileWriter writer( file );
		kmac::flare::EmergencySink sink( &writer, false );

		kmac::nova::ScopedConfigurator config;
		config.bind< EmergencyTag >( &sink );

		// write 1000 test records
		for ( int i = 0; i < 1000; ++i )
		{
			NOVA_LOG_STACK( EmergencyTag ) << "Test message " << i;
		}

		std::fclose( file );
	}

	// Read file into memory
	std::FILE* file = std::fopen( filename, "rb" );
	if ( ! file )
	{
		state.SkipWithError( "Failed to open file for reading" );
		return;
	}

	std::fseek( file, 0, SEEK_END );
	long fileSize = std::ftell( file );
	std::fseek( file, 0, SEEK_SET );

	std::vector< std::uint8_t > buffer( fileSize );
	std::fread( buffer.data(), 1, fileSize, file );
	std::fclose( file );

	// benchmark reading
	kmac::flare::Reader reader;
	kmac::flare::Record record;

	for ( auto _ : state )
	{
		// reset the reader
		reader = { };

		std::size_t recordCount = 0;
		while ( reader.parseNext( buffer.data(), buffer.size(), record ) )
		{
			recordCount++;
			benchmark::DoNotOptimize( record );
		}

		benchmark::DoNotOptimize( recordCount );
	}

	state.SetItemsProcessed( state.iterations() * 1000 ); // 1000 records per iteration
}
BENCHMARK( BM_Flare_ReadBack );

// ============================================================================
// Flush Performance
// ============================================================================

static void BM_Flare_FlushOverhead( benchmark::State& state )
{
	const char* filename = "./tmp_flare_flush.bin";
	FileCleanup cleanup( filename );

	std::FILE* file = std::fopen( filename, "wb" );
	if ( !file ) {
		state.SkipWithError( "Failed to open file" );
		return;
	}

	kmac::flare::FileWriter writer( file );
	kmac::flare::EmergencySink sink( &writer, false );

	for ( auto _ : state )
	{
		// EmergencySink flushes after each record by default
		// this benchmark measures the overhead
		sink.flush();
	}

	std::fclose( file );

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Flare_FlushOverhead );

// ============================================================================
// Comparison: Flare vs Nova Regular Logging
// ============================================================================

class NullSink : public kmac::nova::Sink
{
public:
	void process( const kmac::nova::Record& ) noexcept override
	{
		// discard all logs
	}
};

static void BM_Comparison_NovaRegular( benchmark::State& state )
{
	NullSink sink;

	kmac::nova::ScopedConfigurator config;
	config.bind< EmergencyTag >( &sink );

	std::uint64_t counter = 0;

	for ( auto _ : state )
	{
		NOVA_LOG_STACK( EmergencyTag ) << "Regular message " << counter++;
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Comparison_NovaRegular );

static void BM_Comparison_FlareEmergency( benchmark::State& state )
{
	const char* filename = "./tmp_flare_comparison.bin";
	FileCleanup cleanup( filename );

	std::FILE* file = std::fopen( filename, "wb" );
	if ( ! file )
	{
		state.SkipWithError( "Failed to open file" );
		return;
	}

	kmac::flare::FileWriter writer( file );
	kmac::flare::EmergencySink sink( &writer, false );

	kmac::nova::ScopedConfigurator config;
	config.bind< EmergencyTag >( &sink );

	std::uint64_t counter = 0;

	for ( auto _ : state )
	{
		NOVA_LOG_STACK( EmergencyTag ) << "Emergency message " << counter++;
	}

	std::fclose( file );

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Comparison_FlareEmergency );

BENCHMARK_MAIN();
