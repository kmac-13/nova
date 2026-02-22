/**
 * @file benchmark_formatted_file_output.cpp
 * @brief Apples-to-apples file output benchmarks comparing Nova, spdlog, and easylogging++.
 *
 * Measures the cost of writing formatted log records to actual files under various
 * sync/flush strategies.  All libraries use the same ISO-8601-like format so the
 * results reflect real-world I/O rather than null-sink microbenchmarks.
 *
 * Nova disabled-tag overhead is compared against the equivalent spdlog mechanisms
 * (null-sink, level-disabled, and compile-time SPDLOG_ACTIVE_LEVEL).
 *
 * SCENARIOS
 * - disabled-tag overhead: Nova compile/runtime-disabled vs spdlog equivalents
 * - sync + flush: one fflush() per log call (maximum durability)
 * - sync + no-flush: batch I/O (OS decides when to flush)
 * - async: Nova MemoryPoolAsyncSink vs spdlog async_logger
 *
 * NOTE: Static sinks persist across thread-count variations so file handles remain
 * open throughout.  Metrics therefore accumulate; to isolate a specific thread
 * count subtract the previous run's totals from the current ones.
 */

#include <benchmark/benchmark.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>

// --------------------
// Nova Includes
// --------------------
#include "kmac/nova/nova.h"
#include "kmac/nova/scoped_configurator.h"
#include "kmac/nova/timestamp_helper.h"
#include "kmac/nova/extras/formatting_file_sink.h"
#include "kmac/nova/extras/iso8601_formatter.h"
#include "kmac/nova/extras/null_sink.h"
#include "kmac/nova/extras/synchronized_sink.h"
#include "kmac/nova/extras/memory_pool_async_sink.h"

// --------------------
// spdlog Includes
// --------------------
#ifdef HAVE_SPDLOG
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#define SPDLOG_ENABLE_SOURCE_LOC
#define SPDLOG_NO_EXCEPTIONS
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/async.h>
#endif

// --------------------
// easylogging++
// --------------------
#ifdef HAVE_EASYLOGGINGPP
#define ELPP_THREAD_SAFE
#define ELPP_NO_DEFAULT_LOG_FILE
#include <easylogging++.h>

INITIALIZE_EASYLOGGINGPP
#endif

	namespace fs = std::filesystem;

// ============================================================
// Utilities
// ============================================================

static void removeFileIfExists( const std::string& path )
{
	if ( fs::exists( path ) )
	{
		fs::remove( path );
	}
}

template< typename LoggerT >
static void destroyLogger( std::shared_ptr< LoggerT >& logger )
{
	logger.reset();
}

// ============================================================================
// Nova Disabled-Tag Overhead vs spdlog Equivalents
//
// These benchmarks measure how much overhead remains when logging is disabled.
// Nova uses compile-time (constexpr if) and runtime (nullptr check) gating;
// spdlog uses a null sink, level filtering, or SPDLOG_ACTIVE_LEVEL.
// ============================================================================

struct NovaDisabledThroughputTag {};
NOVA_LOGGER_TRAITS( NovaDisabledThroughputTag, NOVA.NULL, true, kmac::nova::TimestampHelper::steadyNanosecs );

// Nova: runtime-disabled via nullptr sink (no binding)
static void BM_Nova_DisabledTag_NullptrSink( benchmark::State& state )
{
	// sink defaults to nullptr — macro's getSink() check short-circuits
	for ( auto _ : state )
	{
		NOVA_LOG( NovaDisabledThroughputTag ) << "Runtime disabled log message, won't appear in logs";
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Nova_DisabledTag_NullptrSink )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 )->UseRealTime();

// Nova: runtime-disabled via NullSink binding (sink present, but discards)
static void BM_Nova_DisabledTag_NullSink( benchmark::State& state )
{
	static kmac::nova::extras::NullSink sink;
	static std::once_flag initFlag;

	std::call_once( initFlag, []() {
		kmac::nova::Logger< NovaDisabledThroughputTag >::bindSink( &sink );
	} );

	for ( auto _ : state )
	{
		NOVA_LOG( NovaDisabledThroughputTag ) << "Runtime disabled log message, won't appear in logs";
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Nova_DisabledTag_NullSink )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 )->UseRealTime();

// Nova: compile-time disabled tag (entire expression compiled out)
struct NovaCompiledOutTag {};
NOVA_LOGGER_TRAITS( NovaCompiledOutTag, NOVA.DISABLED, false, kmac::nova::TimestampHelper::steadyNanosecs );

static void BM_Nova_DisabledTag_CompileTime( benchmark::State& state )
{
	static kmac::nova::extras::NullSink sink;
	static std::once_flag initFlag;

	std::call_once( initFlag, []() {
		kmac::nova::Logger< NovaCompiledOutTag >::bindSink( &sink );
	} );

	for ( auto _ : state )
	{
		// constexpr if (enabled == false) compiles this entire statement out
		NOVA_LOG( NovaCompiledOutTag ) << "Disabled log message, won't appear in log even with valid sink";
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Nova_DisabledTag_CompileTime )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 )->UseRealTime();

#ifdef HAVE_SPDLOG

// spdlog: null sink (sink present, discards; closest to Nova NullSink binding)
static void BM_Spdlog_NullSinkDisabled( benchmark::State& state )
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
BENCHMARK( BM_Spdlog_NullSinkDisabled )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 )->UseRealTime();

// spdlog: level disabled (level set to info, log at debug — runtime check)
static void BM_Spdlog_LevelDisabled( benchmark::State& state )
{
	auto logger = std::make_shared< spdlog::logger >(
		"throughput",
		std::make_shared< spdlog::sinks::null_sink_mt >()
	);
	logger->set_level( spdlog::level::info );

	std::uint64_t totalMessages = 0;

	for ( auto _ : state )
	{
		logger->debug( "Throughput test message {}", totalMessages++ );
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Spdlog_LevelDisabled )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 )->UseRealTime();

// spdlog: compile-time disabled via SPDLOG_ACTIVE_LEVEL (set to INFO above)
static void BM_Spdlog_CompileTimeDisabled( benchmark::State& state )
{
	for ( auto _ : state )
	{
		SPDLOG_DEBUG( "Compile-time disabled message {}", 42 );
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_Spdlog_CompileTimeDisabled )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 )->UseRealTime();

#endif // HAVE_SPDLOG

// ============================================================
// Nova Sync File Output (single-threaded, varies flush strategy)
// ============================================================

struct SyncFlushTag {};
NOVA_LOGGER_TRAITS( SyncFlushTag, SYNC.FLUSH, true, kmac::nova::TimestampHelper::systemNanosecs );

struct SyncNoFlushTag {};
NOVA_LOGGER_TRAITS( SyncNoFlushTag, SYNC.NOFLUSH, true, kmac::nova::TimestampHelper::systemNanosecs );

struct NoSyncFlushTag {};
NOVA_LOGGER_TRAITS( NoSyncFlushTag, NOSYNC.FLUSH, true, kmac::nova::TimestampHelper::systemNanosecs );

struct NoSyncNoFlushTag {};
NOVA_LOGGER_TRAITS( NoSyncNoFlushTag, NOSYNC.NOFLUSH, true, kmac::nova::TimestampHelper::systemNanosecs );

static void BM_Nova_File_Sync_Flush( benchmark::State& state )
{
	const int threadCount = static_cast< int >( state.range( 0 ) );
	state.SetLabel( "Nova Sync Flush" );

	std::string filename = "nova_sync_flush.log";
	removeFileIfExists( filename );

	FILE* file = std::fopen( filename.c_str(), "wb" );
	std::setvbuf( file, nullptr, _IOFBF, 128 * 1024 );

	auto formatter = std::make_shared< kmac::nova::extras::ISO8601Formatter >();
	auto fileSink = std::make_shared< kmac::nova::extras::FormattingFileSink<> >( file, formatter.get() );
	auto lockSink = std::make_shared< kmac::nova::extras::SynchronizedSink >( *fileSink );

	kmac::nova::ScopedConfigurator<> scopedConfigurator;
	scopedConfigurator.bind< SyncFlushTag >( lockSink.get() );

	for ( auto _ : state )
	{
		int a = 0;
		int b = 0;
		NOVA_LOG( SyncFlushTag ) << "Nova MT file output test " << a << " " << b;
		fileSink->flush();
	}

	fileSink->flush();
	std::fclose( file );
}
BENCHMARK( BM_Nova_File_Sync_Flush )->Arg( 1 )->Arg( 2 )->Arg( 4 )->Arg( 8 )->Threads( 1 )->UseRealTime();

static void BM_Nova_File_Sync_NoFlush( benchmark::State& state )
{
	const int threadCount = static_cast< int >( state.range( 0 ) );
	state.SetLabel( "Nova Sync No Flush" );

	std::string filename = "nova_sync_noflush.log";
	removeFileIfExists( filename );

	FILE* file = std::fopen( filename.c_str(), "wb" );
	std::setvbuf( file, nullptr, _IOFBF, 128 * 1024 );

	auto formatter = std::make_shared< kmac::nova::extras::ISO8601Formatter >();
	auto fileSink = std::make_shared< kmac::nova::extras::FormattingFileSink<> >( file, formatter.get() );
	auto lockSink = std::make_shared< kmac::nova::extras::SynchronizedSink >( *fileSink );

	kmac::nova::ScopedConfigurator<> scopedConfigurator;
	scopedConfigurator.bind< SyncNoFlushTag >( lockSink.get() );

	for ( auto _ : state )
	{
		int a = 0;
		int b = 0;
		NOVA_LOG( SyncNoFlushTag ) << "Nova MT file output test " << a << " " << b;
	}

	fileSink->flush();
	std::fclose( file );
}
BENCHMARK( BM_Nova_File_Sync_NoFlush )->Arg( 1 )->Arg( 2 )->Arg( 4 )->Arg( 8 )->Threads( 1 )->UseRealTime();

static void BM_Nova_File_NoSync_Flush( benchmark::State& state )
{
	const int threadCount = static_cast< int >( state.range( 0 ) );
	state.SetLabel( "Nova No Sync Flush" );

	std::string filename = "nova_nosync_flush.log";
	removeFileIfExists( filename );

	FILE* file = std::fopen( filename.c_str(), "wb" );
	std::setvbuf( file, nullptr, _IOFBF, 128 * 1024 );

	auto formatter = std::make_shared< kmac::nova::extras::ISO8601Formatter >();
	auto fileSink = std::make_shared< kmac::nova::extras::FormattingFileSink<> >( file, formatter.get() );

	kmac::nova::ScopedConfigurator<> scopedConfigurator;
	scopedConfigurator.bind< NoSyncFlushTag >( fileSink.get() );

	for ( auto _ : state )
	{
		int a = 0;
		int b = 0;
		NOVA_LOG( NoSyncFlushTag ) << "Nova MT file output test " << a << " " << b;
		fileSink->flush();
	}

	fileSink->flush();
	std::fclose( file );
}
BENCHMARK( BM_Nova_File_NoSync_Flush )->Arg( 1 )->Arg( 2 )->Arg( 4 )->Arg( 8 )->Threads( 1 )->UseRealTime();

static void BM_Nova_File_NoSync_NoFlush( benchmark::State& state )
{
	const int threadCount = static_cast< int >( state.range( 0 ) );
	state.SetLabel( "Nova No Sync No Flush" );

	std::string filename = "nova_nosync_noflush.log";
	removeFileIfExists( filename );

	FILE* file = std::fopen( filename.c_str(), "wb" );
	std::setvbuf( file, nullptr, _IOFBF, 128 * 1024 );

	auto formatter = std::make_shared< kmac::nova::extras::ISO8601Formatter >();
	auto fileSink = std::make_shared< kmac::nova::extras::FormattingFileSink<> >( file, formatter.get() );

	kmac::nova::ScopedConfigurator<> scopedConfigurator;
	scopedConfigurator.bind< NoSyncNoFlushTag >( fileSink.get() );

	for ( auto _ : state )
	{
		int a = 0;
		int b = 0;
		NOVA_LOG( NoSyncNoFlushTag ) << "Nova MT file output test " << a << " " << b;
	}

	fileSink->flush();
	std::fclose( file );
}
BENCHMARK( BM_Nova_File_NoSync_NoFlush )->Arg( 1 )->Arg( 2 )->Arg( 4 )->Arg( 8 )->Threads( 1 )->UseRealTime();

// ============================================================
// spdlog Sync File Output
// ============================================================

#ifdef HAVE_SPDLOG

static void BM_spdlog_File_Sync_Flush( benchmark::State& state )
{
	const int threadCount = static_cast< int >( state.range( 0 ) );
	state.SetLabel( "spdlog Sync Flush" );

	std::string filename = "spdlog_sync_flush.log";
	removeFileIfExists( filename );

	auto sink = std::make_shared< spdlog::sinks::basic_file_sink_mt >( filename, false );
	auto logger = std::make_shared< spdlog::logger >( "FILESYNCNOFLUSH", sink );
	logger->set_pattern( "%Y-%m-%dT%H:%M:%S.%eZ [%n] %s:%# %! - %v", spdlog::pattern_time_type::utc );
	logger->set_level( spdlog::level::info );

	for ( auto _ : state )
	{
		int a = 0;
		int b = 0;
		SPDLOG_LOGGER_CALL( logger, spdlog::level::info, "spdlog sync flush MT file output test {} {}", a, b );
		logger->flush();
	}

	logger->flush();
	destroyLogger( logger );
}
BENCHMARK( BM_spdlog_File_Sync_Flush )->Arg( 1 )->Arg( 2 )->Arg( 4 )->Arg( 8 )->Threads( 1 )->UseRealTime();

static void BM_spdlog_File_Sync_NoFlush( benchmark::State& state )
{
	const int threadCount = static_cast< int >( state.range( 0 ) );
	state.SetLabel( "spdlog Sync No Flush" );

	std::string filename = "spdlog_sync_noflush.log";
	removeFileIfExists( filename );

	auto sink = std::make_shared< spdlog::sinks::basic_file_sink_mt >( filename, false );
	auto logger = std::make_shared< spdlog::logger >( "FILESYNCNOFLUSH", sink );
	logger->set_pattern( "%Y-%m-%dT%H:%M:%S.%eZ [%n] %s:%# %! - %v", spdlog::pattern_time_type::utc );
	logger->set_level( spdlog::level::info );

	for ( auto _ : state )
	{
		int a = 0;
		int b = 0;
		SPDLOG_LOGGER_CALL( logger, spdlog::level::info, "spdlog sync no flush MT file output test {} {}", a, b );
	}

	logger->flush();
	destroyLogger( logger );
}
BENCHMARK( BM_spdlog_File_Sync_NoFlush )->Arg( 1 )->Arg( 2 )->Arg( 4 )->Arg( 8 )->Threads( 1 )->UseRealTime();

#endif // HAVE_SPDLOG

// ============================================================
// easylogging++ Sync
// ============================================================

#ifdef HAVE_EASYLOGGINGPP

static void configureEasyLogging( const std::string& filename )
{
	el::Configurations conf;
	conf.setToDefault();

	conf.set( el::Level::Info, el::ConfigurationType::Format, "%datetime{%Y-%M-%dT%H:%m:%s.%gZ} [%logger] %fbase:%line %func - %msg" );
	conf.set( el::Level::Info, el::ConfigurationType::Filename, filename );
	conf.set( el::Level::Info, el::ConfigurationType::ToFile, "true" );
	conf.set( el::Level::Info, el::ConfigurationType::ToStandardOutput, "false" );

	el::Loggers::reconfigureLogger( "default", conf );
}

static void BM_easylogging_File_Sync( benchmark::State& state )
{
	state.SetLabel( "easylogging++ Sync" );

	std::string filename = "easylogging_sync_" + std::to_string( state.range( 0 ) ) + ".log";
	configureEasyLogging( filename );

	for ( auto _ : state )
	{
		int a = 0;
		int b = 0;
		LOG( INFO ) << "EasyLogging++ sync MT file output test " << a << " " << b;
	}

	el::Loggers::flushAll();
}
BENCHMARK( BM_easylogging_File_Sync )->Arg( 1 )->Arg( 2 )->Arg( 4 )->Arg( 8 )->Threads( 1 )->UseRealTime();

#endif // HAVE_EASYLOGGINGPP

// ============================================================
// Nova Async File Output (multi-threaded fixture)
// ============================================================

struct NovaAsyncTag {};
NOVA_LOGGER_TRAITS( NovaAsyncTag, NOVA.ASYNC, true, kmac::nova::TimestampHelper::systemNanosecs );

class NovaSharedFileBenchmark : public benchmark::Fixture
{
protected:
	static constexpr std::size_t PoolSize = 256 * 1024;
	static constexpr std::size_t QueueSize = 8192;
	using Formatter = kmac::nova::extras::ISO8601Formatter;
	using FileSink = kmac::nova::extras::FormattingFileSink<>;
	using AsyncSink = kmac::nova::extras::MemoryPoolAsyncSink< PoolSize, QueueSize >;

	static FILE* _file;
	static std::shared_ptr< Formatter > _formatter;
	static std::shared_ptr< FileSink > _fileSink;
	static std::shared_ptr< AsyncSink > _asyncSink;

public:
	void SetUp( const ::benchmark::State& state ) override
	{
		if ( state.thread_index() == 0 )
		{
			std::string filename = "nova_async_noflush.log";
			removeFileIfExists( filename );

			_file = std::fopen( filename.c_str(), "wb" );
			std::setvbuf( _file, nullptr, _IOFBF, 128 * 1024 );

			_formatter = std::make_shared< Formatter >();
			_fileSink = std::make_shared< FileSink >( _file, _formatter.get() );
			_asyncSink = std::make_shared< AsyncSink >( *_fileSink.get() );

			kmac::nova::Logger< NovaAsyncTag >::bindSink( _asyncSink.get() );
		}
	}

	void TearDown( const ::benchmark::State& state ) override
	{
		if ( state.thread_index() == 0 )
		{
			kmac::nova::Logger< NovaAsyncTag >::unbindSink();
			_asyncSink.reset();

			_fileSink->flush();
			_fileSink.reset();

			std::fclose( _file );
			_file = nullptr;
		}
	}
};

FILE* NovaSharedFileBenchmark::_file = nullptr;
std::shared_ptr< NovaSharedFileBenchmark::Formatter > NovaSharedFileBenchmark::_formatter;
std::shared_ptr< NovaSharedFileBenchmark::FileSink > NovaSharedFileBenchmark::_fileSink;
std::shared_ptr< NovaSharedFileBenchmark::AsyncSink > NovaSharedFileBenchmark::_asyncSink;

BENCHMARK_DEFINE_F( NovaSharedFileBenchmark, NovaAsyncNoFlush )( benchmark::State& state )
{
	for ( auto _ : state )
	{
		int a = 0;
		int b = 0;
		NOVA_LOG( NovaAsyncTag ) << "Nova async MT file output test " << a << " " << b;
	}

	if ( state.thread_index() == 0 )
	{
		const uint64_t processed = _asyncSink->processedCount();
		const uint64_t dropped = _asyncSink->droppedCount();
		const uint64_t total = processed + dropped;

		state.counters[ "Logged" ] = static_cast< double >( total );
		state.counters[ "Delivered" ] = static_cast< double >( processed );
		state.counters[ "Dropped" ] = static_cast< double >( dropped );
		state.counters[ "DropRate%" ] = total == 0
			? 0.0
			: 100.0 * static_cast< double >( dropped ) / static_cast< double >( total );
	}
}

BENCHMARK_REGISTER_F( NovaSharedFileBenchmark, NovaAsyncNoFlush )
	->Threads( 1 )
	->Threads( 2 )
	->Threads( 4 )
	->Threads( 8 )
	->Threads( 16 )
	->UseRealTime();

// ============================================================
// spdlog Async File Output (multi-threaded fixture)
// ============================================================

#ifdef HAVE_SPDLOG

class SpdlogAsyncFixture : public benchmark::Fixture
{
protected:
	static std::shared_ptr< spdlog::logger > _logger;
	static std::atomic< uint64_t > _totalLogged;
	static int _lastThreadCount;

public:
	void SetUp( const ::benchmark::State& state ) override
	{
		if ( state.thread_index() == 0 )
		{
			// reinitialize when thread count changes
			if ( state.threads() != _lastThreadCount )
			{
				if ( _logger )
				{
					_logger->flush();
					_logger.reset();
					spdlog::shutdown();
				}

				std::remove( "spdlog_async_noflush.log" );
				_totalLogged.store( 0, std::memory_order_relaxed );

				constexpr size_t QueueSize = 8192;
				constexpr size_t WorkerThreads = 1;
				spdlog::init_thread_pool( QueueSize, WorkerThreads );

				auto sink = std::make_shared< spdlog::sinks::basic_file_sink_st >(
					"spdlog_async_noflush.log",
					false
				);

				_logger = std::make_shared< spdlog::async_logger >(
					"SPDLOGASYNCLOGGER",
					sink,
					spdlog::thread_pool(),
					spdlog::async_overflow_policy::overrun_oldest
				);

				_logger->set_pattern( "%Y-%m-%dT%H:%M:%S.%eZ [%n] %s:%# %! - %v", spdlog::pattern_time_type::utc );
				_logger->set_level( spdlog::level::info );
				_lastThreadCount = state.threads();
			}
		}
	}

	void TearDown( const ::benchmark::State& ) override { }
};

std::shared_ptr< spdlog::logger > SpdlogAsyncFixture::_logger;
std::atomic< uint64_t > SpdlogAsyncFixture::_totalLogged{ 0 };
int SpdlogAsyncFixture::_lastThreadCount = -1;

BENCHMARK_DEFINE_F( SpdlogAsyncFixture, SpdlogAsyncNoFlush )( benchmark::State& state )
{
	for ( auto _ : state )
	{
		int a = 0;
		int b = 0;
		_logger->info( "spdlog async MT file output test {} {}", a, b );
		_totalLogged.fetch_add( 1, std::memory_order_relaxed );
	}

	if ( state.thread_index() == 0 )
	{
		auto tp = spdlog::thread_pool();

		const uint64_t dropped = tp ? tp->overrun_counter() : 0;
		const uint64_t logged = _totalLogged.load( std::memory_order_relaxed );
		const uint64_t delivered = logged > dropped ? logged - dropped : 0;

		state.counters[ "Logged" ] = static_cast< double >( logged );
		state.counters[ "Delivered" ] = static_cast< double >( delivered );
		state.counters[ "Dropped" ] = static_cast< double >( dropped );
		state.counters[ "DropRate%" ] = logged == 0
			? 0.0
			: 100.0 * static_cast< double >( dropped ) / static_cast< double >( logged );
	}
}

BENCHMARK_REGISTER_F( SpdlogAsyncFixture, SpdlogAsyncNoFlush )
	->Threads( 1 )
	->Threads( 2 )
	->Threads( 4 )
	->Threads( 8 )
	->Threads( 16 )
	->UseRealTime();

#endif // HAVE_SPDLOG

BENCHMARK_MAIN();
