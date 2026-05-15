/**
 * @file benchmark_throughput.cpp
 * @brief Throughput benchmarks measuring maximum logs per second
 *
 * This file focuses on measuring the maximum sustainable logging throughput
 * for Nova and comparison libraries under various conditions.
 *
 * Metrics:
 * - Single-threaded throughput (msgs/sec)
 * - Multi-threaded throughput (aggregate msgs/sec)
 * - Sustained throughput over time
 * - Throughput under different message sizes
 */

#include <benchmark/benchmark.h>

#include "kmac/nova.h"

#include "kmac/nova/extras/continuation_logging.h"
#include "kmac/nova/extras/null_sink.h"
#include "kmac/nova/extras/spinlock_sink.h"
#include "kmac/nova/extras/streaming_logging.h"
#include "kmac/nova/extras/synchronized_sink.h"

#if defined( HAVE_SPDLOG )
#include <spdlog/spdlog.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/async.h>
#endif

#if defined( HAVE_GLOG )
#include <glog/logging.h>
#endif

#if defined( HAVE_BOOST_LOG )
#include <boost/log/trivial.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#endif

#if defined( HAVE_LOG4CPP )
#include <log4cpp/Category.hh>
#include <log4cpp/OstreamAppender.hh>
#include <log4cpp/Priority.hh>
#endif

#if defined( HAVE_EASYLOGGINGPP )
#include <easylogging++.h>
#endif

#if defined( HAVE_NANOLOG )
#include <NanoLog.hpp>
#endif

#include <atomic>

// ============================================================================
// Test Infrastructure
// ============================================================================

class CountingSink : public kmac::nova::Sink
{
public:
	std::atomic< std::uint64_t > count{ 0 };

	void process( const kmac::nova::Record& ) noexcept override
	{
		count.fetch_add( 1, std::memory_order_relaxed );
	}

	std::uint64_t getCount() const
	{
		return count.load( std::memory_order_relaxed );
	}

	void reset()
	{
		count.store( 0, std::memory_order_relaxed );
	}
};

struct ThroughputTag { };
NOVA_LOGGER_TRAITS( ThroughputTag, THRU, true, kmac::nova::TimestampHelper::steadyNanosecs );

// Each multi-threaded benchmark gets its own tag so their static sink bindings
// don't interfere - all benchmarks share the same process, and std::once_flag
// fires exactly once per flag instance regardless of which benchmark is active.
// Without distinct tags, benchmark 2's call_once is a no-op and it inherits
// benchmark 1's sink binding, causing data races or null-sink crashes.
struct MTSynchronizedTag { };
NOVA_LOGGER_TRAITS( MTSynchronizedTag, THRU_SYNC, true, kmac::nova::TimestampHelper::steadyNanosecs );

struct MTSpinlockTag { };
NOVA_LOGGER_TRAITS( MTSpinlockTag, THRU_SPIN, true, kmac::nova::TimestampHelper::steadyNanosecs );

struct MTAsyncTag { };
NOVA_LOGGER_TRAITS( MTAsyncTag, THRU_ASYNC, true, kmac::nova::TimestampHelper::steadyNanosecs );

struct MTSyncDirectTag { };
NOVA_LOGGER_TRAITS( MTSyncDirectTag, THRU_DIRECT, true, kmac::nova::TimestampHelper::steadyNanosecs );

struct MTNullptrTag { };
NOVA_LOGGER_TRAITS( MTNullptrTag, THRU_DPTR, true, kmac::nova::TimestampHelper::steadyNanosecs );

struct MTNullSinkTag { };
NOVA_LOGGER_TRAITS( MTNullSinkTag, THRU_DNULL, true, kmac::nova::TimestampHelper::steadyNanosecs );

// ============================================================================
// Nova Single-Threaded Throughput
// ============================================================================
//
// NOTE: Google Benchmark automatically calculates and reports throughput
// (items/second) based on SetItemsProcessed() and the measured time.
// The output will show "items/s" which represents messages/second.
//

static void BM_Throughput_Nova_SingleThread_Truncating( benchmark::State& state )
{
	kmac::nova::ScopedConfigurator config;
	config.bind< ThroughputTag >( &kmac::nova::extras::NullSink::instance() );

	std::uint64_t totalMessages = 0;

	for ( auto _ : state )
	{
		NOVA_LOG( ThroughputTag ) << "Throughput test message " << totalMessages++;
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Throughput_Nova_SingleThread_Truncating );

static void BM_Throughput_Nova_SingleThread_Continuation( benchmark::State& state )
{
	kmac::nova::ScopedConfigurator config;
	config.bind< ThroughputTag >( &kmac::nova::extras::NullSink::instance() );

	std::uint64_t totalMessages = 0;

	for ( auto _ : state )
	{
		NOVA_LOG_CONT( ThroughputTag ) << "Throughput test message " << totalMessages++;
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Throughput_Nova_SingleThread_Continuation );

static void BM_Throughput_Nova_SingleThread_Streaming( benchmark::State& state )
{
	kmac::nova::ScopedConfigurator config;
	config.bind< ThroughputTag >( &kmac::nova::extras::NullSink::instance() );

	std::uint64_t totalMessages = 0;

	for ( auto _ : state )
	{
		NOVA_LOG_STREAM( ThroughputTag ) << "Throughput test message " << totalMessages++;
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Throughput_Nova_SingleThread_Streaming );

// ============================================================================
// Nova Multi-Threaded Throughput
// ============================================================================
//
// Sinks are namespace-scope globals - lifetimes span the entire program with
// no magic-static or once_flag initialization races.  CountingSink is used
// instead of NullSink so the compiler cannot elide the log call as a
// no-observable-effect operation; the atomic increment forces real work.
//

namespace MTSinks
{
// CountingSink forces real work - the atomic increment prevents the compiler
// from eliding the entire log call as a no-observable-effect operation.
// Without this, release builds can optimize NOVA_LOG away entirely
// when routing to NullSink, making timings appear as ~0.2ns (noise floor).
//
CountingSink countSink;
kmac::nova::extras::SynchronizedSink syncSink( countSink );
kmac::nova::extras::SpinlockSink spinSink( countSink );
CountingSink directCountSink;
CountingSink nullSinkTagCountSink;

struct Setup
{
	Setup()
	{
		kmac::nova::Logger< MTSynchronizedTag >::bindSink( &syncSink );
		kmac::nova::Logger< MTSpinlockTag >::bindSink( &spinSink );
		kmac::nova::Logger< MTAsyncTag >::bindSink( &countSink );
		kmac::nova::Logger< MTSyncDirectTag >::bindSink( &directCountSink );
		// MTNullptrTag intentionally unbound (nullptr sink)
		kmac::nova::Logger< MTNullSinkTag >::bindSink( &nullSinkTagCountSink );
	}
};

static Setup setup;
}

static void BM_Throughput_Nova_MultiThread_Synchronized( benchmark::State& state )
{
	for ( auto _ : state )
	{
		NOVA_LOG( MTSynchronizedTag ) << "MT throughput " << state.thread_index();
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Throughput_Nova_MultiThread_Synchronized )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 )->MinTime( 2.0 )->UseRealTime();

static void BM_Throughput_Nova_MultiThread_Spinlock( benchmark::State& state )
{
	for ( auto _ : state )
	{
		NOVA_LOG( MTSpinlockTag ) << "Spinlock MT throughput " << state.thread_index();
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Throughput_Nova_MultiThread_Spinlock )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 )->MinTime( 2.0 )->UseRealTime();

static void BM_Throughput_Nova_MultiThread_MemoryPoolAsyncSink_256KB( benchmark::State& state )
{
	// Routes to countSink (direct atomic increment) rather than a real
	// MemoryPoolAsyncSink.  Starting a background thread as a static-duration
	// global causes Windows/MSVC process-exit crashes (CRT locale FLS teardown
	// race), and running it as a function-local static crashes under multi-thread
	// benchmarking (the pool's 256KB fits ~4000 records; 2+ producers filling it
	// faster than one consumer drains it overflows the pool and corrupts memory).
	// The MemoryPoolAsyncSink is covered by its own dedicated benchmark binary.
	// This entry measures the Nova producer path overhead in a contended MT context.
	for ( auto _ : state )
	{
		NOVA_LOG( MTAsyncTag ) << "Async MT throughput " << state.thread_index();
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Throughput_Nova_MultiThread_MemoryPoolAsyncSink_256KB )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 )->MinTime( 2.0 )->UseRealTime();

// Nova Multi-Thread Synchronous (Direct to NullSink)
static void BM_Throughput_Nova_MultiThread_Sync( benchmark::State& state )
{
	for ( auto _ : state )
	{
		NOVA_LOG( MTSyncDirectTag ) << "Sync MT throughput " << state.thread_index();
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Throughput_Nova_MultiThread_Sync )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 )->MinTime( 2.0 )->UseRealTime();

// ============================================================================
// Nova Message Size Throughput Scaling
// ============================================================================

static void BM_Throughput_Nova_MessageSize( benchmark::State& state )
{
	kmac::nova::ScopedConfigurator config;
	config.bind< ThroughputTag >( &kmac::nova::extras::NullSink::instance() );

	std::string message( state.range( 0 ), 'X' );

	for ( auto _ : state )
	{
		NOVA_LOG( ThroughputTag ) << message;
	}

	state.SetItemsProcessed( state.iterations() );
	state.SetBytesProcessed( state.iterations() * state.range( 0 ) );

	state.SetLabel( std::to_string( state.range( 0 ) ) + " bytes" );
}
BENCHMARK( BM_Throughput_Nova_MessageSize )->Range( 8, 4096 );

// ============================================================================
// Nova Runtime Disabled Tag Throughput - nullptr
// ============================================================================

static void BM_Throughput_Nova_RuntimeDisabledTag_NullptrSink( benchmark::State& state )
{
	// MTNullptrTag has no sink bound - Logger::getSink() returns nullptr, macro is a no-op
	for ( auto _ : state )
	{
		NOVA_LOG( MTNullptrTag ) << "Runtime disabled log message, won't appear in logs";
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Throughput_Nova_RuntimeDisabledTag_NullptrSink )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 )->MinTime( 2.0 )->UseRealTime();

// ============================================================================
// Nova Runtime Disabled Tag Throughput - NullSink
// ============================================================================

static void BM_Throughput_Nova_RuntimeDisabledTag_NullSink( benchmark::State& state )
{
	// MTNullSinkTag routes to a CountingSink - message arrives but the point is
	// measuring the overhead of a fully-wired (non-null) sink with a tag that is
	// "disabled" only in the sense that it's not a meaningful domain, not because
	// the Logger has no sink.  CountingSink prevents loop elision.
	for ( auto _ : state )
	{
		NOVA_LOG( MTNullSinkTag ) << "Runtime disabled log message, won't appear in logs";
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Throughput_Nova_RuntimeDisabledTag_NullSink )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 )->MinTime( 2.0 )->UseRealTime();

// ============================================================================
// Nova Compile-Time Disabled Tag Throughput
// ============================================================================

// Disabled tag (no overhead)
struct NovaDisabledTag {};
NOVA_LOGGER_TRAITS( NovaDisabledTag, DISABLED, false, kmac::nova::TimestampHelper::steadyNanosecs );

static void BM_Throughput_Nova_CompiletimeDisabledTag( benchmark::State& state )
{
	// NovaDisabledTag is compiled out entirely - no sink needed, no binding needed
	for ( auto _ : state )
	{
		NOVA_LOG( NovaDisabledTag ) << "Disabled log message, won't appear in log even with valid sink";
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Throughput_Nova_CompiletimeDisabledTag )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 )->MinTime( 2.0 )->UseRealTime();

// ============================================================================
// Nova Latency-Optimized Benchmark Variants - Theoretical Optimizations
// ============================================================================

// Helper: Null timestamp function for benchmarks (saves ~5-10ns)
inline std::uint64_t nullTimestamp() noexcept
{
	return 0;
}

// Optimized tag (no timestamp overhead)
struct NovaOptimizedTag {};
NOVA_LOGGER_TRAITS( NovaOptimizedTag, OPT, true, nullTimestamp );

// ============================================================================
// Optimization 1: Remove Timestamp
// ============================================================================

static void BM_Throughput_Nova_Optimized_NoTimestamp( benchmark::State& state )
{
	kmac::nova::ScopedConfigurator config;
	config.bind< NovaOptimizedTag >( &kmac::nova::extras::NullSink::instance() );

	std::uint64_t totalMessages = 0;

	for ( auto _ : state )
	{
		NOVA_LOG( NovaOptimizedTag ) << "Throughput test message " << totalMessages++;
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Throughput_Nova_Optimized_NoTimestamp );

// ============================================================================
// Optimization 2: Direct Record API (minimal overhead)
// ============================================================================

// Minimal logging macro (bypasses streaming operators)
// NOTE: checks for null sink - Logger returns nullptr when no sink is bound
#define NOVA_LOG_DIRECT( Tag, msg ) \
do { \
		static constexpr char message[] = msg; \
		kmac::nova::Record rec; \
		rec.timestamp = 0; \
		rec.tagId = kmac::nova::LoggerTraits< Tag >::tagId; \
		rec.tag = #Tag; \
		rec.file = __FILE__; \
		rec.function = __func__; \
		rec.line = __LINE__; \
		rec.messageSize = static_cast< std::uint32_t >( sizeof( message ) - 1 ); \
		rec.message = message; \
		auto* _sink = kmac::nova::Logger< Tag >::getSink(); \
		if ( _sink ) { _sink->process( rec ); } \
} while( 0 )

	static void BM_Throughput_Nova_Optimized_Direct( benchmark::State& state )
{
	kmac::nova::ScopedConfigurator config;
	config.bind< NovaOptimizedTag >( &kmac::nova::extras::NullSink::instance() );

	for ( auto _ : state )
	{
		NOVA_LOG_DIRECT( NovaOptimizedTag, "Throughput test message" );
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Throughput_Nova_Optimized_Direct );

// ============================================================================
// Optimization 3: Compile-Time Constant (ultra-minimal)
// ============================================================================

static void BM_Throughput_Nova_Optimized_Minimal( benchmark::State& state )
{
	kmac::nova::extras::NullSink& sink = kmac::nova::extras::NullSink::instance();
	kmac::nova::ScopedConfigurator config;
	config.bind< NovaOptimizedTag >( &sink );

	// Pre-create record outside loop
	static constexpr char msg[] = "Benchmark";
	kmac::nova::Record rec;
	rec.timestamp = 0;
	rec.tagId = kmac::nova::LoggerTraits< NovaOptimizedTag >::tagId;
	rec.tag = "NovaOptimizedTag";
	rec.file = __FILE__;
	rec.function = __func__;
	rec.line = __LINE__;
	rec.messageSize = static_cast< std::uint32_t >( sizeof( msg ) - 1 );
	rec.message = msg;

	for ( auto _ : state )
	{
		// just call sink directly (absolute minimum overhead)
		sink.process( rec );
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Throughput_Nova_Optimized_Minimal );

#if ! defined( NOVA_BENCHMARKS_ONLY )

// ============================================================================
// spdlog Throughput Benchmarks
// ============================================================================

#if defined( HAVE_SPDLOG )

// spdlog's async thread pool is stored in a global shared_ptr inside spdlog's
// registry.  On Windows/MSVC, the thread it owns races with the CRT locale
// DLL-detach cleanup during process exit (same pattern as Easylogging++).
// Calling spdlog::shutdown() from a global destructor drains and joins the
// thread pool while the heap is still fully intact, preventing the crash.
struct SpdlogShutdown
{
	~SpdlogShutdown() { spdlog::shutdown(); }
};
static SpdlogShutdown spdlogShutdown;

static void BM_Throughput_Spdlog_SingleThread( benchmark::State& state )
{
	auto logger = std::make_shared< spdlog::logger >(
		"throughput",
		std::make_shared< spdlog::sinks::null_sink_mt >()
	);
	logger->set_level( spdlog::level::info );

	std::uint64_t totalMessages = 0;

	for ( auto _ : state )
	{
		logger->info( "Throughput test message {}", totalMessages++ );
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Throughput_Spdlog_SingleThread );

static void BM_Throughput_Spdlog_SingleThreadDisabled( benchmark::State& state )
{
	auto logger = std::make_shared< spdlog::logger >(
		"throughput",
		std::make_shared< spdlog::sinks::null_sink_mt >()
	);
	logger->set_level( spdlog::level::off );

	std::uint64_t totalMessages = 0;

	for ( auto _ : state )
	{
		logger->info( "Throughput test message {}", totalMessages++ );
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Throughput_Spdlog_SingleThreadDisabled );

static void BM_Throughput_Spdlog_Async( benchmark::State& state )
{
	spdlog::init_thread_pool( 16384, 1 );
	auto logger = std::make_shared< spdlog::async_logger >(
		"async_throughput",
		std::make_shared< spdlog::sinks::null_sink_mt >(),
		spdlog::thread_pool()
	);
	logger->set_level( spdlog::level::info );

	std::uint64_t totalMessages = 0;

	for ( auto _ : state )
	{
		logger->info( "Async throughput test message {}", totalMessages++ );
	}

	state.SetItemsProcessed( state.iterations() );

	spdlog::drop( "async_throughput" );
}
BENCHMARK( BM_Throughput_Spdlog_Async );

static void BM_Throughput_Spdlog_MultiThread( benchmark::State& state )
{
	auto logger = std::make_shared< spdlog::logger >(
		"mt_throughput",
		std::make_shared< spdlog::sinks::null_sink_mt >()
	);
	logger->set_level( spdlog::level::info );

	for ( auto _ : state )
	{
		logger->info( "MT throughput {}", state.thread_index() );
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Throughput_Spdlog_MultiThread )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 )->UseRealTime();

// Spdlog Async Multi-Thread
static void BM_Throughput_Spdlog_AsyncMultiThread( benchmark::State& state )
{
	// create thread pool (shared across all benchmark threads)
	// use static to ensure it's only initialized once
	static bool initialized = false;
	if ( ! initialized )
	{
		spdlog::init_thread_pool( 16384, 1 );
		initialized = true;
	}

	// each benchmark thread gets its own logger to avoid contention on logger itself
	auto logger = std::make_shared< spdlog::async_logger >(
		"async_mt_" + std::to_string( state.thread_index() ),
		std::make_shared< spdlog::sinks::null_sink_mt >(),
		spdlog::thread_pool()
	);
	logger->set_level( spdlog::level::info );

	for ( auto _ : state )
	{
		logger->info( "Async MT throughput {}", state.thread_index() );
	}

	state.SetItemsProcessed( state.iterations() );

	// clean up this thread's logger
	spdlog::drop( "async_mt_" + std::to_string( state.thread_index() ) );
}
BENCHMARK( BM_Throughput_Spdlog_AsyncMultiThread )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 )->UseRealTime();

#endif // HAVE_SPDLOG

// ============================================================================
// glog Throughput Benchmarks
// ============================================================================

#if defined( HAVE_GLOG )

struct GlogInit
{
	GlogInit()
	{
		FLAGS_logtostderr = false;
		FLAGS_minloglevel = google::GLOG_INFO;
	}
};

static GlogInit glogInit;

static void BM_Throughput_Glog_SingleThread( benchmark::State& state )
{
	std::uint64_t totalMessages = 0;

	for ( auto _ : state )
	{
		LOG( INFO ) << "Throughput test message " << totalMessages++;
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Throughput_Glog_SingleThread );

static void BM_Throughput_Glog_MultiThread( benchmark::State& state )
{
	for ( auto _ : state )
	{
		LOG( INFO ) << "MT throughput " << state.thread_index();
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Throughput_Glog_MultiThread )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 )->UseRealTime();

#endif // HAVE_GLOG

// ============================================================================
// Boost.Log Throughput Benchmarks
// ============================================================================

#if defined( HAVE_BOOST_LOG )

struct BoostLogInit
{
	BoostLogInit()
	{
		boost::log::core::get()->set_filter(
			boost::log::expressions::attr< boost::log::trivial::severity_level >( "Severity" )
				>= boost::log::trivial::info
			);
	}

	~BoostLogInit()
	{
		// Boost.Log's core singleton holds heap-allocated sinks and attribute state.
		// Removing all sinks and flushing here ensures cleanup happens while the
		// heap is intact, before the CRT locale DLL-detach window on Windows/MSVC.
		auto core = boost::log::core::get();
		core->flush();
		core->remove_all_sinks();
	}
};

static BoostLogInit boostLogInit;

static void BM_Throughput_BoostLog_SingleThread( benchmark::State& state )
{
	std::uint64_t totalMessages = 0;

	for ( auto _ : state )
	{
		BOOST_LOG_TRIVIAL( info ) << "Throughput test message " << totalMessages++;
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Throughput_BoostLog_SingleThread );

static void BM_Throughput_BoostLog_MultiThread( benchmark::State& state )
{
	for ( auto _ : state )
	{
		BOOST_LOG_TRIVIAL( info ) << "MT throughput " << state.thread_index();
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Throughput_BoostLog_MultiThread )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 )->UseRealTime();

#endif // HAVE_BOOST_LOG

// ============================================================================
// log4cpp Throughput Benchmarks
// ============================================================================

#if defined( HAVE_LOG4CPP )

struct Log4cppInit
{
	Log4cppInit()
	{
		std::ostringstream* nullStream = new std::ostringstream();
		log4cpp::OstreamAppender* appender = new log4cpp::OstreamAppender( "null", nullStream );

		log4cpp::Category& root = log4cpp::Category::getRoot();
		root.setPriority( log4cpp::Priority::INFO );
		root.addAppender( appender );
	}

	~Log4cppInit()
	{
		log4cpp::Category::shutdown();
	}
};

static Log4cppInit log4cppInit;

static void BM_Throughput_Log4cpp_SingleThread( benchmark::State& state )
{
	log4cpp::Category& logger = log4cpp::Category::getInstance( "throughput" );
	std::uint64_t totalMessages = 0;

	for ( auto _ : state )
	{
		logger.info( "Throughput test message %lu", totalMessages++ );
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Throughput_Log4cpp_SingleThread );

#endif // HAVE_LOG4CPP

// ============================================================================
// easylogging++ Throughput Benchmarks
// ============================================================================

// NOTE: Easylogging++ disabled on Windows/MSVC - its global singleton
// (el::base::Storage) destructs heap-allocated std::string members during
// process exit in a way that races with the MSVC CRT locale DLL-detach
// cleanup, causing RtlFreeHeap to receive an invalid address and crash.
// This is an Easylogging++ bug, not a Nova bug.  Re-enable on POSIX only.
#if defined( HAVE_EASYLOGGINGPP )

struct EasyloggingInit
{
	EasyloggingInit()
	{
		el::Configurations conf;
		conf.setToDefault();
		conf.set( el::Level::Global, el::ConfigurationType::Enabled, "true" );
		conf.set( el::Level::Global, el::ConfigurationType::ToFile, "false" );
		conf.set( el::Level::Global, el::ConfigurationType::ToStandardOutput, "false" );
		el::Loggers::reconfigureAllLoggers( conf );
	}
};

static EasyloggingInit easyloggingInit;

static void BM_Throughput_Easylogging_SingleThread( benchmark::State& state )
{
	std::uint64_t totalMessages = 0;

	for ( auto _ : state )
	{
		LOG( INFO ) << "Throughput test message " << totalMessages++;
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Throughput_Easylogging_SingleThread );

#endif // HAVE_EASYLOGGINGPP && !_WIN32

// ============================================================================
// NanoLog Throughput Benchmarks
// ============================================================================

#if defined( HAVE_NANOLOG )

struct NanoLogInit
{
	NanoLogInit()
	{
		nanolog::initialize( nanolog::GuaranteedLogger(), "/tmp/", "nanolog_throughput", 1 );
	}

	~NanoLogInit()
	{
		std::remove( "/tmp/nanolog_throughput.txt" );
		std::remove( "/tmp/nanolog_throughput.txt.1" );
	}
};

static NanoLogInit nanoLogInit;

static void BM_Throughput_NanoLog_SingleThread( benchmark::State& state )
{
	std::uint64_t totalMessages = 0;

	for ( auto _ : state )
	{
		LOG_INFO << "Throughput test message " << totalMessages++;
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Throughput_NanoLog_SingleThread );

#endif // HAVE_NANOLOG

// ============================================================================
// Quill Throughput Benchmarks
// ============================================================================

#if defined( HAVE_QUILL )

#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/LogMacros.h>
#include <quill/Logger.h>
#include <quill/core/FrontendOptions.h>
#include <quill/sinks/NullSink.h>

// bounded dropping queue - matches Nova's MemoryPoolAsyncSink policy (drop on full)
// and prevents unbounded queue growth that previously caused OOM crashes under
// sustained benchmark load
struct BoundedDroppingOptions : quill::FrontendOptions
{
	static constexpr quill::QueueType queue_type = quill::QueueType::BoundedDropping;
	static constexpr std::uint32_t initial_queue_capacity = 256 * 1024; // 256 KB - matches Nova MemoryPoolAsyncSink pool size
};

using BoundedFrontend = quill::FrontendImpl< BoundedDroppingOptions >;
using BoundedLogger = quill::LoggerImpl< BoundedDroppingOptions >;

// all loggers and the backend are initialised exactly once across all Quill
// benchmarks - Backend::start() is not idempotent and must not be called again
// after the backend thread is already running
namespace
{

struct QuillState
{
	BoundedLogger* stLogger = nullptr;
	BoundedLogger* mtLogger = nullptr;
	BoundedLogger* sizeLogger = nullptr;
};

QuillState g_quillState;
std::once_flag g_quillInitFlag;

void initQuill()
{
	std::call_once( g_quillInitFlag, []() {
		quill::BackendOptions backend_options;
		quill::Backend::start( backend_options );

		auto stSink = BoundedFrontend::create_or_get_sink< quill::NullSink >( "quill_null_st" );
		g_quillState.stLogger = BoundedFrontend::create_or_get_logger(
			"quill_throughput",
			std::move( stSink ),
			quill::PatternFormatterOptions{ "[%(time)] [%(log_level_short_code)] %(message)" } );

		auto mtSink = BoundedFrontend::create_or_get_sink< quill::NullSink >( "quill_null_mt" );
		g_quillState.mtLogger = BoundedFrontend::create_or_get_logger(
			"quill_throughput_mt",
			std::move( mtSink ),
			quill::PatternFormatterOptions{ "[%(time)] [%(log_level_short_code)] %(message)" } );

		auto sizeSink = BoundedFrontend::create_or_get_sink< quill::NullSink >( "quill_null_size" );
		g_quillState.sizeLogger = BoundedFrontend::create_or_get_logger(
			"quill_size",
			std::move( sizeSink ),
			quill::PatternFormatterOptions{ "[%(time)] [%(log_level_short_code)] %(message)" } );
	} );
}

} // namespace

// stops the Quill backend thread explicitly before process exit, preventing
// a crash in the std::atexit handler when the backend tries to drain queues
// from threads that Google Benchmark has already destroyed
struct QuillStopper
{
	~QuillStopper()
	{
		quill::Backend::stop();
	}
};

static QuillStopper g_quillStopper;

static void BM_Throughput_Quill_SingleThread( benchmark::State& state )
{
	initQuill();

	std::uint64_t totalMessages = 0;

	for ( auto _ : state )
	{
		LOG_INFO( g_quillState.stLogger, "Throughput test message {}", totalMessages++ );
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Throughput_Quill_SingleThread );

static void BM_Throughput_Quill_MultiThread( benchmark::State& state )
{
	initQuill();

	for ( auto _ : state )
	{
		LOG_INFO( g_quillState.mtLogger, "MT throughput {}", state.thread_index() );
	}

	state.SetItemsProcessed( state.iterations() );
}
// Quill multi-thread benchmark disabled: Quill allocates a thread_local
// ScopedThreadContext (SPSC queue) on first use per OS thread and destroys it
// when that thread exits.  Google Benchmark tears down and recreates worker
// threads between each ->Threads(N) configuration, so threads spawned for the
// threads:2 run instantiate a new ScopedThreadContext while the backend is
// still processing invalidated contexts from the threads:1 run, causing a race
// in ThreadContextManager that crashes the process.  This is a structural
// incompatibility between Quill's one-context-per-thread-lifetime model and
// Google Benchmark's thread pool lifecycle; it cannot be worked around without
// forking the benchmark framework or patching Quill.
// Single-thread throughput (BM_Throughput_Quill_SingleThread) is representative
// for Quill's frontend enqueue cost; refer to Quill's own benchmark suite for
// multi-thread figures.
// BENCHMARK( BM_Throughput_Quill_MultiThread )
// 	->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 )
// 	->MinTime( 1.0 )->UseRealTime();

static void BM_Throughput_Quill_MessageSize( benchmark::State& state )
{
	initQuill();

	std::string message( static_cast< std::size_t >( state.range( 0 ) ), 'X' );

	for ( auto _ : state )
	{
		LOG_INFO( g_quillState.sizeLogger, "Message: {}", message );
	}

	state.SetItemsProcessed( state.iterations() );
}
// bounded dropping queue keeps memory usage fixed regardless of message size;
// drop rate at larger sizes reflects realistic backpressure behaviour
BENCHMARK( BM_Throughput_Quill_MessageSize )->Range( 8, 4096 );

#endif // HAVE_QUILL

#endif // !NOVA_BENCHMARKS_ONLY

// explicit main instead of BENCHMARK_MAIN() so we can call Backend::stop()
// before returning — on MinGW the static destructor ordering for QuillStopper
// is not reliable enough to prevent the atexit crash, but an explicit call here
// runs before any static destructors and before the CRT teardown window
int main( int argc, char** argv )
{
	::benchmark::Initialize( &argc, argv );

	if ( ::benchmark::ReportUnrecognizedArguments( argc, argv ) )
	{
		return 1;
	}

	::benchmark::RunSpecifiedBenchmarks();
	::benchmark::Shutdown();

#if defined( HAVE_QUILL )
	quill::Backend::stop();
#endif

	_Exit( 0 );
}
