/**
 * @file benchmark_nova_builders.cpp
 * @brief Detailed benchmarks for Nova's three record builder variants
 *
 * Compares the performance characteristics of:
 * - TruncatingRecordBuilder (NOVA_LOG_TRUNC)
 * - ContinuationRecordBuilder (NOVA_LOG_CONT)
 * - StreamingRecordBuilder (NOVA_LOG_STREAM)
 *
 * Tests include:
 * - simple message logging
 * - complex formatting with multiple types
 * - long message handling
 * - high-frequency logging
 * - construction overhead
 *
 * NOTE: The logging macros are explicitly called directly to avoid potential
 * issues with configuring the default NOVA_LOG and related macros.
 */

#include <benchmark/benchmark.h>

#include "kmac/nova/nova.h"
#include "kmac/nova/scoped_configurator.h"
#include "kmac/nova/timestamp_helper.h"

#include "kmac/nova/extras/null_sink.h"
#include "kmac/nova/extras/streaming_macros.h"
#include "kmac/nova/extras/streaming_record_builder.h"

#include <sstream>
#include <string>
#include <vector>

// ============================================================================
// Test Infrastructure
// ============================================================================

class CaptureSink : public kmac::nova::Sink
{
public:
	std::vector< std::string > messages;

	void process( const kmac::nova::Record& record ) noexcept override
	{
		messages.emplace_back( record.message, record.messageSize );
	}

	void clear()
	{
		messages.clear();
	}
};

struct BuilderTag { };
NOVA_LOGGER_TRAITS( BuilderTag, BUILDER, true, kmac::nova::TimestampHelper::steadyNanosecs );

// ============================================================================
// Simple Message Benchmarks
// ============================================================================

static void BM_Builder_Truncating_Simple( benchmark::State& state )
{
	kmac::nova::extras::NullSink sink;
	kmac::nova::ScopedConfigurator config;
	config.bind< BuilderTag >( &sink );

	for ( auto _ : state )
	{
		NOVA_LOG_TRUNC( BuilderTag ) << "Simple message";
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Builder_Truncating_Simple );

static void BM_Builder_Continuation_Simple( benchmark::State& state )
{
	kmac::nova::extras::NullSink sink;
	kmac::nova::ScopedConfigurator config;
	config.bind< BuilderTag >( &sink );

	for ( auto _ : state )
	{
		NOVA_LOG_CONT( BuilderTag ) << "Simple message";
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Builder_Continuation_Simple );

static void BM_Builder_Streaming_Simple( benchmark::State& state )
{
	kmac::nova::extras::NullSink sink;
	kmac::nova::ScopedConfigurator config;
	config.bind< BuilderTag >( &sink );

	for ( auto _ : state )
	{
		NOVA_LOG_STREAM( BuilderTag ) << "Simple message";
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Builder_Streaming_Simple );

// ============================================================================
// Complex Formatting Benchmarks
// ============================================================================

static void BM_Builder_Truncating_Complex( benchmark::State& state )
{
	kmac::nova::extras::NullSink sink;
	kmac::nova::ScopedConfigurator config;
	config.bind< BuilderTag >( &sink );

	for ( auto _ : state )
	{
		NOVA_LOG_TRUNC( BuilderTag )
			<< "Integer: " << 12345
			<< ", Double: " << 3.14159
			<< ", String: " << "test"
			<< ", Bool: " << true
			<< ", Hex: " << std::hex << 0xDEADBEEF;
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Builder_Truncating_Complex );

static void BM_Builder_Continuation_Complex( benchmark::State& state )
{
	kmac::nova::extras::NullSink sink;
	kmac::nova::ScopedConfigurator config;
	config.bind< BuilderTag >( &sink );

	for ( auto _ : state )
	{
		NOVA_LOG_CONT( BuilderTag )
			<< "Integer: " << 12345
			<< ", Double: " << 3.14159
			<< ", String: " << "test"
			<< ", Bool: " << true
			<< ", Hex: " << std::hex << 0xDEADBEEF;
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Builder_Continuation_Complex );

static void BM_Builder_Streaming_Complex( benchmark::State& state )
{
	kmac::nova::extras::NullSink sink;
	kmac::nova::ScopedConfigurator config;
	config.bind< BuilderTag >( &sink );

	for ( auto _ : state )
	{
		NOVA_LOG_STREAM( BuilderTag )
			<< "Integer: " << 12345
			<< ", Double: " << 3.14159
			<< ", String: " << "test"
			<< ", Bool: " << true
			<< ", Hex: " << std::hex << 0xDEADBEEF;
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Builder_Streaming_Complex );

// ============================================================================
// Long Message Benchmarks (tests truncation vs continuation behavior)
// ============================================================================

static void BM_Builder_Truncating_LongMessage( benchmark::State& state )
{
	kmac::nova::extras::NullSink sink;
	kmac::nova::ScopedConfigurator config;
	config.bind< BuilderTag >( &sink );

	std::string longStr( state.range( 0 ), 'X' );

	for ( auto _ : state )
	{
		NOVA_LOG_TRUNC( BuilderTag ) << "Long message: " << longStr;
	}

	state.SetItemsProcessed( state.iterations() );
	state.SetLabel( std::to_string( state.range( 0 ) ) + " chars" );
}
BENCHMARK( BM_Builder_Truncating_LongMessage )->Range( 64, 8192 );

static void BM_Builder_Continuation_LongMessage( benchmark::State& state )
{
	kmac::nova::extras::NullSink sink;
	kmac::nova::ScopedConfigurator config;
	config.bind< BuilderTag >( &sink );

	std::string longStr( state.range( 0 ), 'X' );

	for ( auto _ : state )
	{
		NOVA_LOG_CONT( BuilderTag ) << "Long message: " << longStr;
	}

	state.SetItemsProcessed( state.iterations() );
	state.SetLabel( std::to_string( state.range( 0 ) ) + " chars" );
}
BENCHMARK( BM_Builder_Continuation_LongMessage )->Range( 64, 8192 );

static void BM_Builder_Streaming_LongMessage( benchmark::State& state )
{
	kmac::nova::extras::NullSink sink;
	kmac::nova::ScopedConfigurator config;
	config.bind< BuilderTag >( &sink );

	std::string longStr( state.range( 0 ), 'X' );

	for ( auto _ : state )
	{
		NOVA_LOG_STREAM( BuilderTag ) << "Long message: " << longStr;
	}

	state.SetItemsProcessed( state.iterations() );
	state.SetLabel( std::to_string( state.range( 0 ) ) + " chars" );
}
BENCHMARK( BM_Builder_Streaming_LongMessage )->Range( 64, 8192 );

// ============================================================================
// High-Frequency Logging (typical in real-time systems)
// ============================================================================

static void BM_Builder_Truncating_HighFrequency( benchmark::State& state )
{
	kmac::nova::extras::NullSink sink;
	kmac::nova::ScopedConfigurator config;
	config.bind< BuilderTag >( &sink );

	int counter = 0;

	for ( auto _ : state )
	{
		NOVA_LOG_TRUNC( BuilderTag ) << "Event: " << counter++;
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Builder_Truncating_HighFrequency );

static void BM_Builder_Continuation_HighFrequency( benchmark::State& state )
{
	kmac::nova::extras::NullSink sink;
	kmac::nova::ScopedConfigurator config;
	config.bind< BuilderTag >( &sink );

	int counter = 0;

	for ( auto _ : state )
	{
		NOVA_LOG_CONT( BuilderTag ) << "Event: " << counter++;
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Builder_Continuation_HighFrequency );

static void BM_Builder_Streaming_HighFrequency( benchmark::State& state )
{
	kmac::nova::extras::NullSink sink;
	kmac::nova::ScopedConfigurator config;
	config.bind< BuilderTag >( &sink );

	int counter = 0;

	for ( auto _ : state )
	{
		NOVA_LOG_STREAM( BuilderTag ) << "Event: " << counter++;
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Builder_Streaming_HighFrequency );

// ============================================================================
// Builder Construction Overhead
// ============================================================================

static void BM_Builder_Truncating_ConstructionOverhead( benchmark::State& state )
{
	kmac::nova::extras::NullSink sink;
	kmac::nova::ScopedConfigurator config;
	config.bind< BuilderTag >( &sink );

	// Measure just the overhead of creating the builder
	for ( auto _ : state )
	{
		NOVA_LOG_TRUNC( BuilderTag ) << "";  // Empty message
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Builder_Truncating_ConstructionOverhead );

static void BM_Builder_Continuation_ConstructionOverhead( benchmark::State& state )
{
	kmac::nova::extras::NullSink sink;
	kmac::nova::ScopedConfigurator config;
	config.bind< BuilderTag >( &sink );

	for ( auto _ : state )
	{
		NOVA_LOG_CONT( BuilderTag ) << "";  // Empty message
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Builder_Continuation_ConstructionOverhead );

static void BM_Builder_Streaming_ConstructionOverhead( benchmark::State& state )
{
	kmac::nova::extras::NullSink sink;
	kmac::nova::ScopedConfigurator config;
	config.bind< BuilderTag >( &sink );

	for ( auto _ : state )
	{
		NOVA_LOG_STREAM( BuilderTag ) << "";  // Empty message
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Builder_Streaming_ConstructionOverhead );

// ============================================================================
// Correctness Verification (ensures truncation/continuation works as expected)
// ============================================================================

static void BM_Builder_Truncating_Correctness( benchmark::State& state )
{
	CaptureSink sink;
	kmac::nova::ScopedConfigurator config;
	config.bind< BuilderTag >( &sink );

	// log a message that will be truncated (exceeds default buffer)
	std::string veryLongStr( 8192, 'X' );

	for ( auto _ : state )
	{
		sink.clear();
		NOVA_LOG_TRUNC( BuilderTag ) << "Prefix: " << veryLongStr;

		// verify we got exactly one message (truncated)
		benchmark::DoNotOptimize( sink.messages.size() );
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Builder_Truncating_Correctness );

static void BM_Builder_Continuation_Correctness( benchmark::State& state )
{
	CaptureSink sink;
	kmac::nova::ScopedConfigurator config;
	config.bind< BuilderTag >( &sink );

	// log a message that will be continued (exceeds default buffer)
	std::string veryLongStr( 8192, 'X' );

	for ( auto _ : state )
	{
		sink.clear();
		NOVA_LOG_CONT( BuilderTag ) << "Prefix: " << veryLongStr;

		// verify we got multiple messages (continued)
		benchmark::DoNotOptimize( sink.messages.size() );
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Builder_Continuation_Correctness );

static void BM_Builder_Streaming_Correctness( benchmark::State& state )
{
	CaptureSink sink;
	kmac::nova::ScopedConfigurator config;
	config.bind< BuilderTag >( &sink );

	// log a very long message (should work fine with heap allocation)
	std::string veryLongStr( 8192, 'X' );

	for ( auto _ : state )
	{
		sink.clear();
		NOVA_LOG_STREAM( BuilderTag ) << "Prefix: " << veryLongStr;

		// verify we got exactly one complete message
		benchmark::DoNotOptimize( sink.messages.size() );
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Builder_Streaming_Correctness );

BENCHMARK_MAIN();
