/**
 * @file benchmark_nova_sinks.cpp
 * @brief Benchmark various Nova Extras sink implementations
 *
 * Tests include:
 * - NullSink (baseline)
 * - OStreamSink (std::ostream, std::ofstream)
 * - RollingFileSink (automatic file rotation)
 * - CompositeSink (multiple destinations)
 * - SynchronizedSink (mutex, Spinlock)
 * - AsyncQueueSink (async I/O)
 * - FilterSink (conditional logging)
 * - FormattingSink (formatting, TODO)
 * - composition, i.e. sink chaining
 */

#include <benchmark/benchmark.h>

#include "kmac/nova/nova.h"
#include "kmac/nova/scoped_configurator.h"
#include "kmac/nova/timestamp_helper.h"

#include "kmac/nova/extras/composite_sink.h"
#include "kmac/nova/extras/filter_sink.h"
#include "kmac/nova/extras/formatting_sink.h"
#include "kmac/nova/extras/memory_pool_async_sink.h"
#include "kmac/nova/extras/null_sink.h"
#include "kmac/nova/extras/ostream_sink.h"
#include "kmac/nova/extras/rolling_file_sink.h"
#include "kmac/nova/extras/spinlock_sink.h"
#include "kmac/nova/extras/synchronized_sink.h"

#include <sstream>
#include <mutex>
#include <fstream>
#include <cstdio>

// ============================================================================
// Test Infrastructure
// ============================================================================

struct SinkTag { };
NOVA_LOGGER_TRAITS( SinkTag, SINK, true, kmac::nova::TimestampHelper::steadyNanosecs );

// ============================================================================
// NullSink Benchmarks (baseline)
// ============================================================================

static void BM_NullSink_Baseline( benchmark::State& state )
{
	kmac::nova::extras::NullSink sink;
	kmac::nova::ScopedConfigurator config;
	config.bind< SinkTag >( &sink );

	for ( auto _ : state )
	{
		NOVA_LOG( SinkTag ) << "Null sink baseline message";
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_NullSink_Baseline );

// ============================================================================
// OStreamSink Benchmarks
// ============================================================================

static void BM_OStreamSink_StringStream( benchmark::State& state )
{
	std::ostringstream oss;
	kmac::nova::extras::OStreamSink sink( oss );
	kmac::nova::ScopedConfigurator config;
	config.bind< SinkTag >( &sink );

	for ( auto _ : state )
	{
		NOVA_LOG( SinkTag ) << "OStream benchmark message " << state.iterations();
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_OStreamSink_StringStream );

static void BM_OStreamSink_FileStream( benchmark::State& state )
{
	const char* filename = "/tmp/ostream_bench.log";
	std::remove( filename );

	std::ofstream ofs( filename, std::ios::binary | std::ios::app );
	kmac::nova::extras::OStreamSink sink( ofs );
	kmac::nova::ScopedConfigurator config;
	config.bind< SinkTag >( &sink );

	for ( auto _ : state )
	{
		NOVA_LOG( SinkTag ) << "File stream benchmark message " << state.iterations();
	}

	state.SetItemsProcessed( state.iterations() );

	ofs.close();
	std::remove( filename );
}
BENCHMARK( BM_OStreamSink_FileStream );

static void BM_OStreamSink_HighVolume( benchmark::State& state )
{
	std::ostringstream oss;
	kmac::nova::extras::OStreamSink sink( oss );
	kmac::nova::ScopedConfigurator config;
	config.bind< SinkTag >( &sink );

	for ( auto _ : state )
	{
		NOVA_LOG( SinkTag ) << "High volume message " << state.iterations();
	}

	state.SetItemsProcessed( state.iterations() );
	state.SetBytesProcessed( state.iterations() * 50 ); // approximate message size
}
BENCHMARK( BM_OStreamSink_HighVolume );

// ============================================================================
// RollingFileSink Benchmarks
// ============================================================================

static void BM_RollingFileSink_NoRotation( benchmark::State& state )
{
	const char* filename = "/tmp/rolling_bench.log";
	std::remove( filename );

	// large max size to avoid rotation during benchmark
	kmac::nova::extras::RollingFileSink sink( filename, 10 * 1024 * 1024 ); // 10MB
	kmac::nova::ScopedConfigurator config;
	config.bind< SinkTag >( &sink );

	for ( auto _ : state )
	{
		NOVA_LOG( SinkTag ) << "Rolling file benchmark message " << state.iterations();
	}

	state.SetItemsProcessed( state.iterations() );

	std::remove( filename );
	std::remove( ( std::string( filename ) + ".1" ).c_str() );
}
BENCHMARK( BM_RollingFileSink_NoRotation );

static void BM_RollingFileSink_SmallFile( benchmark::State& state )
{
	const char* filename = "/tmp/rolling_small.log";
	std::remove( filename );

	// small max size to test rotation overhead (but won't rotate during single message)
	kmac::nova::extras::RollingFileSink sink( filename, 1024 ); // 1KB
	kmac::nova::ScopedConfigurator config;
	config.bind< SinkTag >( &sink );

	for ( auto _ : state )
	{
		NOVA_LOG( SinkTag ) << "Small rotating file " << state.iterations();
	}

	state.SetItemsProcessed( state.iterations() );

	// clean up all potential rotation files
	for ( int i = 0; i < 10; ++i )
	{
		std::string rotated = std::string( filename ) + "." + std::to_string( i );
		std::remove( rotated.c_str() );
	}
	std::remove( filename );
}
BENCHMARK( BM_RollingFileSink_SmallFile );

// ============================================================================
// CompositeSink Benchmarks
// ============================================================================

static void BM_CompositeSink_TwoSinks( benchmark::State& state )
{
	kmac::nova::extras::NullSink sink1;
	kmac::nova::extras::NullSink sink2;

	kmac::nova::extras::CompositeSink composite;
	composite.add( sink1 );
	composite.add( sink2 );

	kmac::nova::ScopedConfigurator config;
	config.bind< SinkTag >( &composite );

	for ( auto _ : state )
	{
		NOVA_LOG( SinkTag ) << "Composite benchmark message";
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_CompositeSink_TwoSinks );

static void BM_CompositeSink_FiveSinks( benchmark::State& state )
{
	kmac::nova::extras::NullSink sink1, sink2, sink3, sink4, sink5;

	kmac::nova::extras::CompositeSink composite;
	composite.add( sink1 );
	composite.add( sink2 );
	composite.add( sink3 );
	composite.add( sink4 );
	composite.add( sink5 );

	kmac::nova::ScopedConfigurator config;
	config.bind< SinkTag >( &composite );

	for ( auto _ : state )
	{
		NOVA_LOG( SinkTag ) << "Composite benchmark message";
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_CompositeSink_FiveSinks );

// ============================================================================
// SynchronizedSink Benchmarks
// ============================================================================

// Note: SynchronizedSink<> with no template parameter uses std::mutex by default

static void BM_SynchronizedSink_Mutex_SingleThread( benchmark::State& state )
{
	kmac::nova::extras::NullSink nullSink;
	kmac::nova::extras::SynchronizedSink syncSink( nullSink );  // uses mutex
	kmac::nova::ScopedConfigurator config;
	config.bind< SinkTag >( &syncSink );

	for ( auto _ : state )
	{
		NOVA_LOG( SinkTag ) << "Mutex sync benchmark";
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_SynchronizedSink_Mutex_SingleThread );

static void BM_SynchronizedSink_Mutex_MultiThread( benchmark::State& state )
{
	static kmac::nova::extras::NullSink nullSink;
	static kmac::nova::extras::SynchronizedSink syncSink( nullSink );
	static std::once_flag initFlag;

	std::call_once( initFlag, []() {
		kmac::nova::Logger< SinkTag >::bindSink( &syncSink );
	} );

	for ( auto _ : state )
	{
		NOVA_LOG( SinkTag ) << "Mutex MT benchmark " << state.thread_index();
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_SynchronizedSink_Mutex_MultiThread )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 );

static void BM_SynchronizedSink_Spinlock_SingleThread( benchmark::State& state )
{
	kmac::nova::extras::NullSink nullSink;
	kmac::nova::extras::SpinlockSink syncSink( nullSink );
	kmac::nova::ScopedConfigurator config;
	config.bind< SinkTag >( &syncSink );

	for ( auto _ : state )
	{
		NOVA_LOG( SinkTag ) << "Spinlock sync benchmark";
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_SynchronizedSink_Spinlock_SingleThread );

static void BM_SynchronizedSink_Spinlock_MultiThread( benchmark::State& state )
{
	static kmac::nova::extras::NullSink nullSink;
	static kmac::nova::extras::SpinlockSink syncSink( nullSink );
	static std::once_flag initFlag;

	std::call_once( initFlag, []() {
		kmac::nova::Logger< SinkTag >::bindSink( &syncSink );
	} );

	for ( auto _ : state )
	{
		NOVA_LOG( SinkTag ) << "Spinlock MT benchmark " << state.thread_index();
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_SynchronizedSink_Spinlock_MultiThread )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 );

// ============================================================================
// MemoryPoolAsyncSink Benchmarks
// ============================================================================

static void BM_AsynchrSink_MemoryPool_SingleThread( benchmark::State& state )
{
	kmac::nova::extras::NullSink nullSink;
	kmac::nova::extras::MemoryPoolAsyncSink<> asyncSink( nullSink );
	kmac::nova::ScopedConfigurator config;
	config.bind< SinkTag >( &asyncSink );

	for ( auto _ : state )
	{
		NOVA_LOG( SinkTag ) << "Async small queue benchmark";
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_AsynchrSink_MemoryPool_SingleThread );

static void BM_AsynchrSink_MemoryPool_MultiThread( benchmark::State& state )
{
	static kmac::nova::extras::NullSink nullSink;
	static kmac::nova::extras::MemoryPoolAsyncSink<> asyncSink( nullSink );
	static std::once_flag initFlag;

	std::call_once( initFlag, []() {
		kmac::nova::Logger< SinkTag >::bindSink( &asyncSink );
	} );

	for ( auto _ : state )
	{
		NOVA_LOG( SinkTag ) << "Async small queue benchmark";
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_AsynchrSink_MemoryPool_MultiThread )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 );

// ============================================================================
// FilterSink Benchmarks
// ============================================================================

static void BM_FilterSink_AllPass( benchmark::State& state )
{
	kmac::nova::extras::NullSink nullSink;

	// filter that always passes
	kmac::nova::extras::FilterSink filterSink(
		nullSink,
		[]( const kmac::nova::Record& ) { return true; }
	);

	kmac::nova::ScopedConfigurator config;
	config.bind< SinkTag >( &filterSink );

	for ( auto _ : state )
	{
		NOVA_LOG( SinkTag ) << "Filter all-pass benchmark";
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_FilterSink_AllPass );

static void BM_FilterSink_AllBlock( benchmark::State& state )
{
	kmac::nova::extras::NullSink nullSink;

	// filter that always blocks
	kmac::nova::extras::FilterSink filterSink(
		nullSink,
		[]( const kmac::nova::Record& ) { return false; }
	);

	kmac::nova::ScopedConfigurator config;
	config.bind< SinkTag >( &filterSink );

	for ( auto _ : state )
	{
		NOVA_LOG( SinkTag ) << "Filter all-block benchmark";
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_FilterSink_AllBlock );

static void BM_FilterSink_ComplexPredicate( benchmark::State& state )
{
	kmac::nova::extras::NullSink nullSink;

	// more complex filter predicate
	kmac::nova::extras::FilterSink filterSink(
		nullSink,
		[]( const kmac::nova::Record& record ) {
			// filter based on message length
			return record.messageSize > 10 && record.messageSize < 100;
		}
	);

	kmac::nova::ScopedConfigurator config;
	config.bind< SinkTag >( &filterSink );

	for ( auto _ : state )
	{
		NOVA_LOG( SinkTag ) << "Filter complex benchmark with some text";
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_FilterSink_ComplexPredicate );

// ============================================================================
// FormattingSink Benchmarks
// ============================================================================

// static void BM_FormattingSink_SimpleFormatter( benchmark::State& state )
// {
// 	kmac::nova::extras::NullSink nullSink;
// 	// TODO: initialize a Formatter that implements the following format:
//	// []( const kmac::nova::Record& record ) {
//	// 	return std::string( record.message, record.messageSize ) + "\n";
//	// }

// 	// simple formatter - just the message
// 	kmac::nova::extras::FormattingSink formattingSink( nullSink, formatter );

// 	kmac::nova::ScopedConfigurator config;
// 	config.bind< SinkTag >( &formattingSink );

// 	for ( auto _ : state )
// 	{
// 		NOVA_LOG( SinkTag ) << "Formatting simple benchmark";
// 	}

// 	state.SetItemsProcessed( state.iterations() );
// }
// BENCHMARK( BM_FormattingSink_SimpleFormatter );

// static void BM_FormattingSink_ComplexFormatter( benchmark::State& state )
// {
// 	kmac::nova::extras::NullSink nullSink;

// 	// complex formatter - timestamp, tag, file, line, message
// 	// TODO: initialize a Formatter that implements the following format:
// 	// []( const kmac::nova::Record& record ) {
// 	// 	return std::string( "[" ) + std::to_string( record.timestamp ) + "] "
// 	// 		+ "[" + record.tag + "] "
// 	// 		+ record.file + ":" + std::to_string( record.line ) + " "
// 	// 		+ record.message + "\n";
// 	// }

// 	kmac::nova::extras::FormattingSink formattingSink( nullSink, formatter );

// 	kmac::nova::ScopedConfigurator config;
// 	config.bind< SinkTag >( &formattingSink );

// 	for ( auto _ : state )
// 	{
// 		NOVA_LOG( SinkTag ) << "Formatting complex benchmark";
// 	}

// 	state.SetItemsProcessed( state.iterations() );
// }
// BENCHMARK( BM_FormattingSink_ComplexFormatter );

// ============================================================================
// Combined Sink Chains (realistic patterns)
// ============================================================================

// static void BM_Combined_Filter_Format_File( benchmark::State& state )
// {
// 	const char* filename = "/tmp/combined_bench.log";
// 	std::remove( filename );

// 	// chain: Filter -> Format -> OStreamSink
// 	std::ofstream ofs( filename, std::ios::binary | std::ios::app );
// 	kmac::nova::extras::OStreamSink fileSink( ofs );

// 	// TODO: initialize a Formatter that implements the following format:
// 	// []( const kmac::nova::Record& record ) {
// 	// 	return std::string( "[" ) + record.tag + "] " + record.message + "\n";
// 	// }

// 	kmac::nova::extras::FormattingSink formattingSink( fileSink, formatter );

// 	kmac::nova::extras::FilterSink filterSink(
// 		formattingSink,
// 		[]( const kmac::nova::Record& record ) {
// 			return record.messageSize > 0;  // simple filter
// 		}
// 	);

// 	kmac::nova::ScopedConfigurator config;
// 	config.bind< SinkTag >( &filterSink );

// 	for ( auto _ : state )
// 	{
// 		NOVA_LOG( SinkTag ) << "Combined chain benchmark";
// 	}

// 	state.SetItemsProcessed( state.iterations() );

// 	ofs.close();
// 	std::remove( filename );
// }
// BENCHMARK( BM_Combined_Filter_Format_File );

static void BM_Combined_Sync_Composite( benchmark::State& state )
{
	// multiple destinations with synchronization
	static kmac::nova::extras::NullSink sink1, sink2;
	static kmac::nova::extras::CompositeSink composite;
	static kmac::nova::extras::SynchronizedSink syncComposite( composite );
	static std::once_flag initFlag;

	std::call_once( initFlag, []() {
		composite.add( sink1 );
		composite.add( sink2 );
		kmac::nova::Logger< SinkTag >::bindSink( &syncComposite );
	} );

	for ( auto _ : state )
	{
		NOVA_LOG( SinkTag ) << "Sync composite benchmark " << state.thread_index();
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Combined_Sync_Composite )->Threads( 1 )->Threads( 2 )->Threads( 4 );

BENCHMARK_MAIN();
