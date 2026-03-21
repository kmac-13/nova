/**
 * @file benchmark_latency.cpp
 * @brief Latency benchmarks measuring per-message latency characteristics
 *
 * This file focuses on measuring the latency (time taken) for individual
 * log operations, including percentile analysis.
 *
 * Metrics:
 * - mean latency
 * - median latency (p50)
 * - 99th percentile (p99)
 * - 99.9th percentile (p999)
 * - maximum latency
 */

#include <benchmark/benchmark.h>

#include "kmac/nova/nova.h"
#include "kmac/nova/scoped_configurator.h"
#include "kmac/nova/timestamp_helper.h"

#include "kmac/nova/extras/continuation_logging.h"
#include "kmac/nova/extras/memory_pool_async_sink.h"
#include "kmac/nova/extras/null_sink.h"
#include "kmac/nova/extras/ostream_sink.h"
#include "kmac/nova/extras/spinlock_sink.h"
#include "kmac/nova/extras/streaming_logging.h"
#include "kmac/nova/extras/synchronized_sink.h"

#ifdef HAVE_SPDLOG
#include <spdlog/spdlog.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/async.h>
#endif

#ifdef HAVE_GLOG
#include <glog/logging.h>
#endif

#ifdef HAVE_BOOST_LOG
#include <boost/log/trivial.hpp>
#endif

#ifdef HAVE_LOG4CPP
#include <log4cpp/Category.hh>
#include <log4cpp/OstreamAppender.hh>
#include <log4cpp/Priority.hh>
#endif

#ifdef HAVE_EASYLOGGINGPP
#define ELPP_THREAD_SAFE
#define ELPP_NO_DEFAULT_LOG_FILE
#include <easylogging++.h>
#endif

#ifdef HAVE_NANOLOG
#include <NanoLog.hpp>
#endif

#include <chrono>
#include <cstdio>
#include <fstream>

// ============================================================================
// Test Infrastructure
// ============================================================================

struct LatencyTag { };
NOVA_LOGGER_TRAITS( LatencyTag, LAT, true, kmac::nova::TimestampHelper::steadyNanosecs );

// ============================================================================
// Nova Latency Benchmarks - Different Builders
// ============================================================================

static void BM_Latency_Nova_Truncating( benchmark::State& state )
{
	kmac::nova::extras::NullSink sink;
	kmac::nova::ScopedConfigurator config;
	config.bind< LatencyTag >( &sink );

	for ( auto _ : state )
	{
		auto start = std::chrono::high_resolution_clock::now();

		// NOTE: using NOVA_LOG (truncating) and NOVA_LOG_CONT for comparison
		// choose to explicitly use the macro to avoid future updates
		// possibly changing which macro is called from NOVA_LOG
		NOVA_LOG( LatencyTag ) << "Latency test message";

		auto end = std::chrono::high_resolution_clock::now();
		auto elapsed = std::chrono::duration_cast< std::chrono::nanoseconds >( end - start );

		state.SetIterationTime( elapsed.count() / 1e9 );
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Latency_Nova_Truncating )->UseManualTime();

static void BM_Latency_Nova_Continuation( benchmark::State& state )
{
	kmac::nova::extras::NullSink sink;
	kmac::nova::ScopedConfigurator config;
	config.bind< LatencyTag >( &sink );

	for ( auto _ : state )
	{
		auto start = std::chrono::high_resolution_clock::now();

		NOVA_LOG_CONT( LatencyTag ) << "Latency test message";

		auto end = std::chrono::high_resolution_clock::now();
		auto elapsed = std::chrono::duration_cast< std::chrono::nanoseconds >( end - start );

		state.SetIterationTime( elapsed.count() / 1e9 );
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Latency_Nova_Continuation )->UseManualTime();

static void BM_Latency_Nova_Streaming( benchmark::State& state )
{
	kmac::nova::extras::NullSink sink;
	kmac::nova::ScopedConfigurator config;
	config.bind< LatencyTag >( &sink );

	for ( auto _ : state )
	{
		auto start = std::chrono::high_resolution_clock::now();

		NOVA_LOG_STREAM( LatencyTag ) << "Latency test message";

		auto end = std::chrono::high_resolution_clock::now();
		auto elapsed = std::chrono::duration_cast< std::chrono::nanoseconds >( end - start );

		state.SetIterationTime( elapsed.count() / 1e9 );
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Latency_Nova_Streaming )->UseManualTime();

// ============================================================================
// Nova Latency - Synchronized Sink
// ============================================================================

// Note: SynchronizedSink uses std::mutex by default when no policy specified
static void BM_Latency_Nova_Synchronized_Mutex( benchmark::State& state )
{
	kmac::nova::extras::NullSink nullSink;
	kmac::nova::extras::SynchronizedSink syncSink( nullSink );  // uses mutex
	kmac::nova::ScopedConfigurator config;
	config.bind< LatencyTag >( &syncSink );

	for ( auto _ : state )
	{
		auto start = std::chrono::high_resolution_clock::now();

		NOVA_LOG( LatencyTag ) << "Mutex latency test";

		auto end = std::chrono::high_resolution_clock::now();
		auto elapsed = std::chrono::duration_cast< std::chrono::nanoseconds >( end - start );

		state.SetIterationTime( elapsed.count() / 1e9 );
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Latency_Nova_Synchronized_Mutex )->UseManualTime();

static void BM_Latency_Nova_Synchronized_Spinlock( benchmark::State& state )
{
	kmac::nova::extras::NullSink nullSink;
	kmac::nova::extras::SpinlockSink syncSink( nullSink );
	kmac::nova::ScopedConfigurator config;
	config.bind< LatencyTag >( &syncSink );

	for ( auto _ : state )
	{
		auto start = std::chrono::high_resolution_clock::now();

		NOVA_LOG( LatencyTag ) << "Spinlock latency test";

		auto end = std::chrono::high_resolution_clock::now();
		auto elapsed = std::chrono::duration_cast< std::chrono::nanoseconds >( end - start );

		state.SetIterationTime( elapsed.count() / 1e9 );
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Latency_Nova_Synchronized_Spinlock )->UseManualTime();

// ============================================================================
// Nova Latency - Memory Pool Async Queue
// ============================================================================

static void BM_Latency_Nova_MemoryPoolAsyncQueue( benchmark::State& state )
{
	kmac::nova::extras::NullSink nullSink;
	kmac::nova::extras::MemoryPoolAsyncSink< 1024 * 1024, 8192 > asyncSink( nullSink );
	kmac::nova::ScopedConfigurator config;
	config.bind< LatencyTag >( &asyncSink );

	for ( auto _ : state )
	{
		auto start = std::chrono::high_resolution_clock::now();

		NOVA_LOG( LatencyTag ) << "Async latency test";

		auto end = std::chrono::high_resolution_clock::now();
		auto elapsed = std::chrono::duration_cast< std::chrono::nanoseconds >( end - start );

		state.SetIterationTime( elapsed.count() / 1e9 );
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Latency_Nova_MemoryPoolAsyncQueue )->UseManualTime();

// ============================================================================
// Nova Latency - File I/O (using OStreamSink)
// ============================================================================

static void BM_Latency_Nova_FileSync( benchmark::State& state )
{
	const char* filename = "/tmp/latency_bench.log";
	std::remove( filename );

	// Use OStreamSink with std::ofstream instead of non-existent FileSink
	std::ofstream file( filename, std::ios::binary | std::ios::app );
	kmac::nova::extras::OStreamSink fileSink( file );
	kmac::nova::ScopedConfigurator config;
	config.bind< LatencyTag >( &fileSink );

	for ( auto _ : state ) {
		auto start = std::chrono::high_resolution_clock::now();

		NOVA_LOG( LatencyTag ) << "File sync latency test";

		auto end = std::chrono::high_resolution_clock::now();
		auto elapsed = std::chrono::duration_cast< std::chrono::nanoseconds >( end - start );

		state.SetIterationTime( elapsed.count() / 1e9 );
	}

	state.SetItemsProcessed( state.iterations() );

	file.close();
	std::remove( filename );
}
BENCHMARK( BM_Latency_Nova_FileSync )->UseManualTime();

// ============================================================================
// Latency Under Load (contention testing)
// ============================================================================

static void BM_Latency_UnderLoad_Mutex( benchmark::State& state )
{
	kmac::nova::extras::NullSink nullSink;
	kmac::nova::extras::SynchronizedSink syncSink( nullSink );
	kmac::nova::ScopedConfigurator config;
	config.bind< LatencyTag >( &syncSink );

	for ( auto _ : state )
	{
		auto start = std::chrono::high_resolution_clock::now();

		NOVA_LOG( LatencyTag ) << "Load test";

		auto end = std::chrono::high_resolution_clock::now();
		auto elapsed = std::chrono::duration_cast< std::chrono::nanoseconds >( end - start );

		state.SetIterationTime( elapsed.count() / 1e9 );
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Latency_UnderLoad_Mutex )->Threads( 1 )->UseManualTime();
BENCHMARK( BM_Latency_UnderLoad_Mutex )->Threads( 2 )->UseManualTime();
BENCHMARK( BM_Latency_UnderLoad_Mutex )->Threads( 4 )->UseManualTime();

// ============================================================================
// Message Size Impact on Latency
// ============================================================================

static void BM_Latency_MessageSize( benchmark::State& state )
{
	kmac::nova::extras::NullSink sink;
	kmac::nova::ScopedConfigurator config;
	config.bind< LatencyTag >( &sink );

	std::string message( state.range( 0 ), 'X' );

	for ( auto _ : state )
	{
		auto start = std::chrono::high_resolution_clock::now();

		NOVA_LOG( LatencyTag ) << message;

		auto end = std::chrono::high_resolution_clock::now();
		auto elapsed = std::chrono::duration_cast< std::chrono::nanoseconds >( end - start );

		state.SetIterationTime( elapsed.count() / 1e9 );
	}

	state.SetItemsProcessed( state.iterations() );
	state.SetLabel( std::to_string( state.range( 0 ) ) + " bytes" );
}
BENCHMARK( BM_Latency_MessageSize )->Range( 8, 4096 )->UseManualTime();

// ============================================================================
// Third-Party Library Latency Benchmarks
// ============================================================================

#ifdef HAVE_SPDLOG
struct SpdlogLatencyInit
{
	SpdlogLatencyInit()
	{
		auto null_sink = std::make_shared< spdlog::sinks::null_sink_mt >();
		auto logger = std::make_shared< spdlog::logger >( "latency", null_sink );
		spdlog::set_default_logger( logger );
	}
};

static SpdlogLatencyInit spdlogLatencyInit;

static void BM_Latency_Spdlog( benchmark::State& state )
{
	for ( auto _ : state )
	{
		auto start = std::chrono::high_resolution_clock::now();

		spdlog::info( "Latency test message" );

		auto end = std::chrono::high_resolution_clock::now();
		auto elapsed = std::chrono::duration_cast< std::chrono::nanoseconds >( end - start );

		state.SetIterationTime( elapsed.count() / 1e9 );
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Latency_Spdlog )->UseManualTime();

#endif // HAVE_SPDLOG

#ifdef HAVE_GLOG

struct GlogLatencyInit
{
	GlogLatencyInit()
	{
		FLAGS_logtostderr = false;
		FLAGS_minloglevel = google::GLOG_INFO;
	}
};

static GlogLatencyInit glogLatencyInit;

static void BM_Latency_Glog( benchmark::State& state )
{
	for ( auto _ : state )
	{
		auto start = std::chrono::high_resolution_clock::now();

		LOG( INFO ) << "Latency test message";

		auto end = std::chrono::high_resolution_clock::now();
		auto elapsed = std::chrono::duration_cast< std::chrono::nanoseconds >( end - start );

		state.SetIterationTime( elapsed.count() / 1e9 );
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Latency_Glog )->UseManualTime();

#endif // HAVE_GLOG

#ifdef HAVE_BOOST_LOG

struct BoostLogLatencyInit
{
	BoostLogLatencyInit()
	{
		boost::log::core::get()->set_filter(
			boost::log::trivial::severity >= boost::log::trivial::info
		);
	}
};

static BoostLogLatencyInit boostLogLatencyInit;

static void BM_Latency_BoostLog( benchmark::State& state )
{
	for ( auto _ : state )
	{
		auto start = std::chrono::high_resolution_clock::now();

		BOOST_LOG_TRIVIAL( info ) << "Latency test message";

		auto end = std::chrono::high_resolution_clock::now();
		auto elapsed = std::chrono::duration_cast< std::chrono::nanoseconds >( end - start );

		state.SetIterationTime( elapsed.count() / 1e9 );
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Latency_BoostLog )->UseManualTime();

#endif // HAVE_BOOST_LOG

#ifdef HAVE_LOG4CPP

struct Log4cppLatencyInit
{
	Log4cppLatencyInit()
	{
		std::ostringstream* nullStream = new std::ostringstream();
		log4cpp::OstreamAppender* appender = new log4cpp::OstreamAppender( "null", nullStream );

		log4cpp::Category& root = log4cpp::Category::getRoot();
		root.setPriority( log4cpp::Priority::INFO );
		root.addAppender( appender );
	}

	~Log4cppLatencyInit()
	{
		log4cpp::Category::shutdown();
	}
};

static Log4cppLatencyInit log4cppLatencyInit;

static void BM_Latency_Log4cpp( benchmark::State& state )
{
	log4cpp::Category& logger = log4cpp::Category::getInstance( "latency" );

	for ( auto _ : state )
	{
		auto start = std::chrono::high_resolution_clock::now();

		logger.info( "Latency test message" );

		auto end = std::chrono::high_resolution_clock::now();
		auto elapsed = std::chrono::duration_cast< std::chrono::nanoseconds >( end - start );

		state.SetIterationTime( elapsed.count() / 1e9 );
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Latency_Log4cpp )->UseManualTime();

#endif // HAVE_LOG4CPP

#ifdef HAVE_EASYLOGGINGPP

struct EasyloggingLatencyInit
{
	EasyloggingLatencyInit()
	{
		el::Configurations conf;
		conf.setToDefault();
		conf.set( el::Level::Global, el::ConfigurationType::Enabled, "true" );
		conf.set( el::Level::Global, el::ConfigurationType::ToFile, "false" );
		conf.set( el::Level::Global, el::ConfigurationType::ToStandardOutput, "false" );
		el::Loggers::reconfigureAllLoggers( conf );
	}
};

static EasyloggingLatencyInit easyloggingLatencyInit;

static void BM_Latency_Easylogging( benchmark::State& state )
{
	for ( auto _ : state )
	{
		auto start = std::chrono::high_resolution_clock::now();

		LOG( INFO ) << "Latency test message";

		auto end = std::chrono::high_resolution_clock::now();
		auto elapsed = std::chrono::duration_cast< std::chrono::nanoseconds >( end - start );

		state.SetIterationTime( elapsed.count() / 1e9 );
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Latency_Easylogging )->UseManualTime();

#endif // HAVE_EASYLOGGINGPP

#ifdef HAVE_NANOLOG

struct NanoLogLatencyInit
{
	NanoLogLatencyInit()
	{
		nanolog::initialize( nanolog::GuaranteedLogger(), "/tmp/", "nanolog_latency", 1 );
	}

	~NanoLogLatencyInit()
	{
		std::remove( "/tmp/nanolog_latency.txt" );
		std::remove( "/tmp/nanolog_latency.txt.1" );
	}
};

static NanoLogLatencyInit nanoLogLatencyInit;

static void BM_Latency_NanoLog( benchmark::State& state )
{
	for ( auto _ : state )
	{
		auto start = std::chrono::high_resolution_clock::now();

		LOG_INFO << "Latency test message";

		auto end = std::chrono::high_resolution_clock::now();
		auto elapsed = std::chrono::duration_cast< std::chrono::nanoseconds >( end - start );

		state.SetIterationTime( elapsed.count() / 1e9 );
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Latency_NanoLog )->UseManualTime();

#endif // HAVE_NANOLOG

BENCHMARK_MAIN();
