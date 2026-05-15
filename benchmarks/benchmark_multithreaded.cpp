/**
 * @file benchmark_multithreaded.cpp
 * @brief Multi-threaded throughput comparison: Nova, spdlog, and Quill async logging
 *
 * Google Benchmark cannot be used for this comparison because Quill allocates a
 * thread_local ScopedThreadContext (SPSC queue) on first use and destroys it when
 * the OS thread exits.  Google Benchmark tears down and recreates worker threads
 * between each ->Threads(N) configuration, which causes a race in Quill's
 * ThreadContextManager and crashes the process.
 *
 * This executable runs a single thread-count configuration and exits, so each
 * invocation gets a clean process with no cross-run state.  Use the companion
 * Python script (benchmark_multithreaded.py) to loop through thread counts and
 * collect results.
 *
 * Usage:
 *   benchmark_multithreaded <threads> [duration_secs] [warmup_secs] [--nova-only|--spdlog-only|--quill-only]
 *
 *   threads       number of producer threads (required)
 *   duration_secs measurement window in seconds (default: 3)
 *   warmup_secs   warmup period before measurement (default: 1)
 *
 * Methodology:
 *   - all N threads start simultaneously via a barrier
 *   - each thread logs in a tight loop for the full measurement window
 *   - counters are snapshotted immediately when the window ends
 *   - post-benchmark queue contents are discarded, not counted
 *
 * Queue sizing:
 *   - Nova:   single shared MemoryPoolAsyncSink, pool = 256 KB fixed
 *   - spdlog: shared MPMC async queue, 256 KB x N threads
 *   - Quill:  per-thread SPSC queue, 256 KB each, total = 256 KB x N threads
 *
 *   Nova uses a fixed pool regardless of thread count - this is intentional.
 *   It benchmarks Nova as configured for a real system (bounded memory), not
 *   artificially scaled to match competitors.  spdlog and Quill are given
 *   256 KB per producer thread as their optimal configuration for these conditions.
 *
 * Output: header + one result line per library
 */

#include <kmac/nova.h>
#include <kmac/nova/extras/custom_formatter.h>
#include <kmac/nova/extras/formatting_file_sink.h>
#include <kmac/nova/extras/memory_pool_async_sink.h>
#include <kmac/nova/extras/memory_pool_async_batch_sink.h>

#if defined( _WIN32 )
#include <windows.h>
#endif

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined( HAVE_SPDLOG )
#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/basic_file_sink.h>
#endif

#if defined( HAVE_QUILL )
#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/LogMacros.h>
#include <quill/Logger.h>
#include <quill/core/FrontendOptions.h>
#include <quill/sinks/FileSink.h>
#endif

// ============================================================================
// Configuration
// ============================================================================

// Nova pool sizes - multiple instantiations for runtime selection.
// Pool size is a compile-time template parameter so we instantiate each
// size we want to benchmark and select at runtime.
static constexpr std::size_t NOVA_POOL_256K = 256 * 1024;
static constexpr std::size_t NOVA_POOL_512K = 512 * 1024;
static constexpr std::size_t NOVA_POOL_1M = 1024 * 1024;
static constexpr std::size_t NOVA_POOL_4M = 4096 * 1024;
static constexpr std::size_t NOVA_POOL_16M = 16384 * 1024;
static constexpr std::size_t NOVA_QUEUE_SIZE = 8192;

// default pool size - overridden by --pool-kb command-line argument
static std::size_t glob_novaPoolBytes = NOVA_POOL_256K;

#if defined( HAVE_QUILL )
static constexpr std::uint32_t QUILL_QUEUE_BYTES = 256 * 1024;
#endif

// ============================================================================
// Barrier
// ============================================================================

class Barrier
{
private:
	std::mutex _mutex;
	std::condition_variable _cv;
	int _count;
	int _waiting;
	int _generation;

public:
	explicit Barrier( int count )
		: _count( count )
		, _waiting( 0 )
		, _generation( 0 )
	{
	}

	void wait()
	{
		std::unique_lock< std::mutex > lock( _mutex );
		const int gen = _generation;
		if ( ++_waiting == _count )
		{
			++_generation;
			_waiting = 0;
			_cv.notify_all();
		}
		else
		{
			_cv.wait( lock, [ this, gen ]() { return _generation != gen; } );
		}
	}
};

// ============================================================================
// Result
// ============================================================================

struct BenchResult
{
	std::string library;
	int threads = 0;
	double durationSecs = 0.0;
	std::uint64_t totalLogged = 0;
	std::uint64_t delivered = 0;
	std::uint64_t dropped = 0;

	double dropRatePct() const { return totalLogged > 0 ? 100.0 * static_cast< double >( dropped ) / static_cast< double >( totalLogged ) : 0.0; }
	double loggedPerSec() const { return durationSecs > 0.0 ? static_cast< double >( totalLogged ) / durationSecs : 0.0; }
	double deliveredPerSec() const { return durationSecs > 0.0 ? static_cast< double >( delivered ) / durationSecs : 0.0; }
};

// ============================================================================
// Counting sinks - increment an atomic counter per record, no I/O.
// Used in place of null sinks to:
//   1. Prove the backend is genuinely calling process/write_log on every record
//   2. Give accurate delivered counts without file I/O
//   3. Isolate queue and format overhead from disk write overhead
// ============================================================================

// Nova counting sink
class NovaCountingSink final : public kmac::nova::Sink
{
private:
	std::atomic< std::uint64_t > _count{ 0 };

public:
	void process( const kmac::nova::Record& ) noexcept override
	{
		_count.fetch_add( 1, std::memory_order_relaxed );
	}

	std::uint64_t count() const noexcept
	{
		return _count.load( std::memory_order_relaxed );
	}

	void reset() noexcept { _count.store( 0, std::memory_order_relaxed ); }
};

// Batch counting sink - counts newlines in the pre-formatted message buffer.
// The batch sink sends one Record per batch flush containing concatenated
// formatted text; counting increments per process() call undercounts.
// Counting newlines gives the correct individual record count.
class BatchCountingSink final : public kmac::nova::Sink
{
private:
	std::atomic< std::uint64_t > _count{ 0 };

public:
	void process( const kmac::nova::Record& record ) noexcept override
	{
		if ( record.message && record.messageSize > 0 )
		{
			for ( std::uint32_t i = 0; i < record.messageSize; ++i )
			{
				if ( record.message[ i ] == '\n' )
				{
					_count.fetch_add( 1, std::memory_order_relaxed );
				}
			}
		}
	}

	std::uint64_t count() const noexcept { return _count.load( std::memory_order_relaxed ); }
	void reset() noexcept { _count.store( 0, std::memory_order_relaxed ); }
};

#if defined( HAVE_SPDLOG )
class SpdlogCountingSink final : public spdlog::sinks::sink
{
private:
	std::atomic< std::uint64_t > _count{ 0 };

public:
	void log( const spdlog::details::log_msg& ) override
	{
		_count.fetch_add( 1, std::memory_order_relaxed );
	}
	void flush() override {}
	void set_pattern( const std::string& ) override {}
	void set_formatter( std::unique_ptr< spdlog::formatter > ) override {}

	std::uint64_t count() const noexcept { return _count.load( std::memory_order_relaxed ); }
	void reset() noexcept { _count.store( 0, std::memory_order_relaxed ); }
};
#endif // HAVE_SPDLOG

#if defined( HAVE_QUILL )
class QuillCountingSink final : public quill::Sink
{
private:
	std::atomic< std::uint64_t > _count{ 0 };

public:
	void write_log(
		quill::MacroMetadata const*, uint64_t, std::string_view, std::string_view,
		std::string const&, std::string_view, quill::LogLevel, std::string_view,
		std::string_view, std::vector< std::pair< std::string, std::string > > const*,
		std::string_view, std::string_view ) override
	{
		_count.fetch_add( 1, std::memory_order_relaxed );
	}
	void flush_sink() override {}

	std::uint64_t count() const noexcept { return _count.load( std::memory_order_relaxed ); }
	void reset() noexcept { _count.store( 0, std::memory_order_relaxed ); }
};
#endif // HAVE_QUILL

// Count newlines in a file - used as the ground truth for delivered record
// count across all libraries.  Counter-based approaches (processedCount,
// overrun_counter, failure_counter) all have accuracy limitations:
//   - Nova's processedCount() is snapshotted before stopAndDiscard() drains
//     the queue, so it may undercount records written during the drain window
//   - spdlog's overrun_counter counts overwrites not drops, and may miss
//     records that were enqueued but not yet processed at snapshot time
//   - Quill's failure_counter only counts SPSC queue overflows; records
//     enqueued but not yet written by the backend are not counted as dropped
// Counting newlines in the flushed output file is the only reliable measure.

static std::uint64_t countFileLines( const char* path )
{
	std::uint64_t lines = 0;
	FILE* f = std::fopen( path, "rb" );
	if ( ! f )
	{
		return 0;
	}
	char buf[ 65536 ];
	std::size_t n;
	while ( ( n = std::fread( buf, 1, sizeof( buf ), f ) ) > 0 )
	{
		for ( std::size_t k = 0; k < n; ++k )
		{
			if ( buf[ k ] == '\n' ) { ++lines; }
		}
	}
	std::fclose( f );
	return lines;
}

// ============================================================================
// Nova benchmark
// ============================================================================

struct NovaAsyncMTTag {};
NOVA_LOGGER_TRAITS( NovaAsyncMTTag, ASYNC_MT, true, kmac::nova::TimestampHelper::systemNanosecs );

using NovaFormatter = kmac::nova::extras::CustomFormatter<
	kmac::nova::extras::FieldSpec< '\0', kmac::nova::extras::Field::TimestampISO, ' ' >,
	kmac::nova::extras::FieldSpec< '[',  kmac::nova::extras::Field::Tag,      ']'  >,
	kmac::nova::extras::FieldSpec< ' ',  kmac::nova::extras::Field::File,     ':'  >,
	kmac::nova::extras::FieldSpec< '\0', kmac::nova::extras::Field::Line,     ' '  >,
	kmac::nova::extras::FieldSpec< '\0', kmac::nova::extras::Field::Function, '\0' >,
	kmac::nova::extras::FieldSpec< ' ',  kmac::nova::extras::Field::None,     '-'  >,
	kmac::nova::extras::FieldSpec< ' ',  kmac::nova::extras::Field::Message,  '\n' >
>;
using NovaFileSink   = kmac::nova::extras::FormattingFileSink<>;

// async sink variants - one per pool size
using NovaAsyncSink256K = kmac::nova::extras::MemoryPoolAsyncSink< NOVA_POOL_256K, NOVA_QUEUE_SIZE >;
using NovaAsyncSink512K = kmac::nova::extras::MemoryPoolAsyncSink< NOVA_POOL_512K, NOVA_QUEUE_SIZE >;
using NovaAsyncSink1M = kmac::nova::extras::MemoryPoolAsyncSink< NOVA_POOL_1M, NOVA_QUEUE_SIZE >;
using NovaAsyncSink4M = kmac::nova::extras::MemoryPoolAsyncSink< NOVA_POOL_4M, NOVA_QUEUE_SIZE >;
using NovaAsyncSink16M = kmac::nova::extras::MemoryPoolAsyncSink< NOVA_POOL_16M, NOVA_QUEUE_SIZE >;

// batch sink variants
using NovaBatchSink256K = kmac::nova::extras::MemoryPoolAsyncBatchSink< NOVA_POOL_256K, NOVA_QUEUE_SIZE >;
using NovaBatchSink512K = kmac::nova::extras::MemoryPoolAsyncBatchSink< NOVA_POOL_512K, NOVA_QUEUE_SIZE >;
using NovaBatchSink1M = kmac::nova::extras::MemoryPoolAsyncBatchSink< NOVA_POOL_1M, NOVA_QUEUE_SIZE >;
using NovaBatchSink4M = kmac::nova::extras::MemoryPoolAsyncBatchSink< NOVA_POOL_4M, NOVA_QUEUE_SIZE >;
using NovaBatchSink16M = kmac::nova::extras::MemoryPoolAsyncBatchSink< NOVA_POOL_16M, NOVA_QUEUE_SIZE >;

template< typename AsyncSinkT >
static BenchResult runNovaImpl( int numThreads, double durationSecs, int warmupSecs, bool nullSink )
{
	FILE* file = nullptr;
	std::shared_ptr< NovaFileSink > fileSink;

	if ( ! nullSink )
	{
		std::remove( ( "nova_mt_bench_" + std::to_string( numThreads ) + ".log" ).c_str() );
		file = std::fopen( ( "nova_mt_bench_" + std::to_string( numThreads ) + ".log" ).c_str(), "wb" );
		std::setvbuf( file, nullptr, _IOFBF, 128 * 1024 );
	}

	NovaFormatter formatter;
	NovaCountingSink countingSink;

	// async sink wraps either the file sink or counting sink -
	// queue/pool overhead is identical; counting sink proves backend calls process()
	std::shared_ptr< AsyncSinkT > asyncSink = nullSink
		? std::make_shared< AsyncSinkT >( countingSink )
		: std::make_shared< AsyncSinkT >( *( fileSink = std::make_shared< NovaFileSink >( file, &formatter ) ) );
	asyncSink->start();

	kmac::nova::Logger< NovaAsyncMTTag >::bindSink( asyncSink.get() );

	Barrier barrier( numThreads + 1 );
	std::atomic< bool > measure{ false };

	std::vector< std::atomic< std::uint64_t > > logged( static_cast< std::size_t >( numThreads ) );
	for ( auto& a : logged )
	{
		a.store( 0 );
	}

	std::vector< std::thread > threads;
	threads.reserve( static_cast< std::size_t >( numThreads ) );

	for ( int i = 0; i < numThreads; ++i )
	{
		threads.emplace_back( [ &, i ]() {
			barrier.wait();

			std::uint64_t count = 0;
			while ( measure.load( std::memory_order_relaxed ) )
			{
				NOVA_LOG( NovaAsyncMTTag ) << "Nova MT bench " << i;
				++count;
			}

			logged[ static_cast< std::size_t >( i ) ].store( count, std::memory_order_relaxed );
		} );
	}

	// warmup
	measure.store( true, std::memory_order_relaxed );
	barrier.wait();
	std::this_thread::sleep_for( std::chrono::seconds( warmupSecs ) );

	// reset counters at the start of the measurement window
	for ( auto& a : logged )
	{
		a.store( 0 );
	}

	const auto benchStart = std::chrono::steady_clock::now();
	std::this_thread::sleep_for( std::chrono::duration< double >( durationSecs ) );
	measure.store( false, std::memory_order_relaxed );

	for ( auto& t : threads )
	{
		t.join();
	}

	const auto benchEnd = std::chrono::steady_clock::now();
	const double elapsed = std::chrono::duration< double >( benchEnd - benchStart ).count();

	asyncSink->stopAndDiscard();
	kmac::nova::Logger< NovaAsyncMTTag >::unbindSink();

	std::uint64_t totalLogged = 0;
	for ( const auto& a : logged )
	{
		totalLogged += a.load( std::memory_order_relaxed );
	}

	std::uint64_t delivered = 0;
	if ( nullSink )
	{
		delivered = countingSink.count();
	}
	else
	{
		fileSink->flush();
		fileSink.reset();
		std::fclose( file );
		delivered = countFileLines( ( "nova_mt_bench_" + std::to_string( numThreads ) + ".log" ).c_str() );
	}

	const std::uint64_t dropped = totalLogged > delivered ? totalLogged - delivered : 0;

	const std::size_t poolKb = AsyncSinkT::poolCapacity() / 1024;
	const std::string suffix = nullSink ? " (null)" : "";
	const std::string label = ( glob_novaPoolBytes == NOVA_POOL_256K )
		? "Nova" + suffix
		: "Nova " + std::to_string( poolKb ) + "KB" + suffix;

	BenchResult result;
	result.library = label;
	result.threads = numThreads;
	result.durationSecs = elapsed;
	result.totalLogged = totalLogged;
	result.delivered = delivered;
	result.dropped = dropped;
	return result;
}

static BenchResult runNova( int numThreads, double durationSecs, int warmupSecs, bool nullSink = false )
{
	if ( glob_novaPoolBytes <= NOVA_POOL_256K )
	{
		return runNovaImpl< NovaAsyncSink256K >( numThreads, durationSecs, warmupSecs, nullSink );
	}
	else if ( glob_novaPoolBytes <= NOVA_POOL_512K )
	{
		return runNovaImpl< NovaAsyncSink512K >( numThreads, durationSecs, warmupSecs, nullSink );
	}
	else if ( glob_novaPoolBytes <= NOVA_POOL_1M )
	{
		return runNovaImpl< NovaAsyncSink1M >( numThreads, durationSecs, warmupSecs, nullSink );
	}
	else if ( glob_novaPoolBytes <= NOVA_POOL_4M )
	{
		return runNovaImpl< NovaAsyncSink4M >( numThreads, durationSecs, warmupSecs, nullSink );
	}
	else
	{
		return runNovaImpl< NovaAsyncSink16M >( numThreads, durationSecs, warmupSecs, nullSink );
	}
}

// ============================================================================
// Nova batch async benchmark
// ============================================================================
//
// Uses MemoryPoolAsyncBatchSink - consumer thread dequeues a batch of records,
// formats them all into a single buffer, then calls downstream->process() once
// per flush rather than once per record.  The downstream receives a pre-formatted
// Record with tag=nullptr and message pointing to the formatted bytes - it must
// write those bytes directly without re-formatting.

struct NovaBatchMTTag {};
NOVA_LOGGER_TRAITS( NovaBatchMTTag, BATCH_MT, true, kmac::nova::TimestampHelper::systemNanosecs );

// batch sink variants - one per pool size (selected at runtime in runNovaBatch)
using NovaBatchSink256K = NovaBatchSink256K;
// NovaBatchSink1M/4M/16M defined above

// Raw file sink - writes record.message bytes directly, no formatting.
// Required as downstream for MemoryPoolAsyncBatchSink which pre-formats batches.
class RawFileSink final : public kmac::nova::Sink
{
private:
	FILE* _file = nullptr;

public:
	explicit RawFileSink( FILE* file ) noexcept : _file( file ) {}

	void process( const kmac::nova::Record& record ) noexcept override
	{
		if ( _file && record.message && record.messageSize > 0 )
		{
			std::fwrite( record.message, 1, record.messageSize, _file );
		}
	}

	void flush() noexcept
	{
		if ( _file )
		{
			std::fflush( _file );
		}
	}
};

template< typename BatchSinkT >
static BenchResult runNovaBatchImpl( int numThreads, double durationSecs, int warmupSecs, bool nullSink )
{
	FILE* file = nullptr;
	std::unique_ptr< RawFileSink > rawFileSink;

	if ( ! nullSink )
	{
		std::remove( ( "nova_batch_mt_bench_" + std::to_string( numThreads ) + ".log" ).c_str() );
		file = std::fopen( ( "nova_batch_mt_bench_" + std::to_string( numThreads ) + ".log" ).c_str(), "wb" );
		std::setvbuf( file, nullptr, _IOFBF, 128 * 1024 );
		rawFileSink = std::unique_ptr< RawFileSink >( new RawFileSink( file ) );
	}

	NovaFormatter formatter;
	BatchCountingSink batchCountingSink;

	// batch sink formats internally; downstream receives pre-formatted bytes.
	// BatchCountingSink counts newlines to get per-record count rather than
	// per-flush count (the batch sink sends one record per batch to downstream).
	std::shared_ptr< BatchSinkT > batchSink = nullSink
		? std::make_shared< BatchSinkT >( batchCountingSink, &formatter )
		: std::make_shared< BatchSinkT >( *rawFileSink, &formatter );
	batchSink->start();

	kmac::nova::Logger< NovaBatchMTTag >::bindSink( batchSink.get() );

	Barrier barrier( numThreads + 1 );
	std::atomic< bool > measure{ false };

	std::vector< std::atomic< std::uint64_t > > logged( static_cast< std::size_t >( numThreads ) );
	for ( auto& a : logged )
	{
		a.store( 0 );
	}

	std::vector< std::thread > threads;
	threads.reserve( static_cast< std::size_t >( numThreads ) );

	for ( int i = 0; i < numThreads; ++i )
	{
		threads.emplace_back( [ &, i ]() {
			barrier.wait();

			std::uint64_t count = 0;
			while ( measure.load( std::memory_order_relaxed ) )
			{
				NOVA_LOG( NovaBatchMTTag ) << "Nova batch MT bench " << i;
				++count;
			}

			logged[ static_cast< std::size_t >( i ) ].store( count, std::memory_order_relaxed );
		} );
	}

	// warmup
	measure.store( true, std::memory_order_relaxed );
	barrier.wait();
	std::this_thread::sleep_for( std::chrono::seconds( warmupSecs ) );

	for ( auto& a : logged )
	{
		a.store( 0 );
	}

	const auto benchStart = std::chrono::steady_clock::now();
	std::this_thread::sleep_for( std::chrono::duration< double >( durationSecs ) );
	measure.store( false, std::memory_order_relaxed );

	for ( auto& t : threads )
	{
		t.join();
	}

	const auto benchEnd = std::chrono::steady_clock::now();
	const double elapsed = std::chrono::duration< double >( benchEnd - benchStart ).count();

	batchSink->stopAndDrain();
	kmac::nova::Logger< NovaBatchMTTag >::unbindSink();

	std::uint64_t totalLogged = 0;
	for ( const auto& a : logged )
	{
		totalLogged += a.load( std::memory_order_relaxed );
	}

	std::uint64_t delivered = 0;
	if ( nullSink )
	{
		delivered = batchCountingSink.count();
	}
	else
	{
		rawFileSink->flush();
		rawFileSink.reset();
		std::fclose( file );
		delivered = countFileLines( ( "nova_batch_mt_bench_" + std::to_string( numThreads ) + ".log" ).c_str() );
	}

	const std::uint64_t dropped = totalLogged > delivered ? totalLogged - delivered : 0;

	const std::size_t poolKb = BatchSinkT::poolCapacity() / 1024;
	const std::string suffix = nullSink ? " (null)" : "";
	const std::string label = ( glob_novaPoolBytes == NOVA_POOL_256K )
		? "Nova batch" + suffix
		: "Nova batch " + std::to_string( poolKb ) + "KB" + suffix;

	BenchResult result;
	result.library = label;
	result.threads = numThreads;
	result.durationSecs = elapsed;
	result.totalLogged = totalLogged;
	result.delivered = delivered;
	result.dropped = dropped;
	return result;
}

static BenchResult runNovaBatch( int numThreads, double durationSecs, int warmupSecs, bool nullSink = false )
{
	if ( glob_novaPoolBytes <= NOVA_POOL_256K )
	{
		return runNovaBatchImpl< NovaBatchSink256K >( numThreads, durationSecs, warmupSecs, nullSink );
	}
	else if ( glob_novaPoolBytes <= NOVA_POOL_512K )
	{
		return runNovaBatchImpl< NovaBatchSink512K >( numThreads, durationSecs, warmupSecs, nullSink );
	}
	else if ( glob_novaPoolBytes <= NOVA_POOL_1M )
	{
		return runNovaBatchImpl< NovaBatchSink1M >( numThreads, durationSecs, warmupSecs, nullSink );
	}
	else if ( glob_novaPoolBytes <= NOVA_POOL_4M )
	{
		return runNovaBatchImpl< NovaBatchSink4M >( numThreads, durationSecs, warmupSecs, nullSink );
	}
	else
	{
		return runNovaBatchImpl< NovaBatchSink16M  >( numThreads, durationSecs, warmupSecs, nullSink );
	}
}
// ============================================================================
//
// Uses overrun_oldest policy - same as benchmark_formatted_file_output -
// so the queue drops oldest records when full rather than blocking.
// This measures throughput, not guaranteed delivery (see benchmark_delivery_latency
// for the blocking/guaranteed comparison).
// Queue size scaled with thread count to match Quill's per-thread 256 KB.
// spdlog uses a single shared MPMC queue; Quill uses per-thread SPSC queues.
//
#if defined( HAVE_SPDLOG )

static BenchResult runSpdlog( int numThreads, double durationSecs, int warmupSecs, bool nullSink = false )
{
	if ( ! nullSink )
	{
		std::remove( ( "spdlog_mt_bench_" + std::to_string( numThreads ) + ".log" ).c_str() );
	}

	// scale queue with thread count to match Quill's per-thread 256 KB buffer
	const std::size_t spdlogQueueItems = ( 256 * 1024 * static_cast< std::size_t >( numThreads ) ) / 64;
	spdlog::init_thread_pool( spdlogQueueItems, 1 );

	auto spdlogCounting = std::make_shared< SpdlogCountingSink >();
	std::shared_ptr< spdlog::sinks::sink > sink = nullSink
		? std::static_pointer_cast< spdlog::sinks::sink >( spdlogCounting )
		: std::static_pointer_cast< spdlog::sinks::sink >(
			std::make_shared< spdlog::sinks::basic_file_sink_mt >(
				( "spdlog_mt_bench_" + std::to_string( numThreads ) + ".log" ), true ) );

	auto logger = std::make_shared< spdlog::async_logger >(
		( "spdlog_mt_" + std::to_string( numThreads ) ),
		sink,
		spdlog::thread_pool(),
		spdlog::async_overflow_policy::overrun_oldest );
	logger->set_level( spdlog::level::info );
	logger->set_pattern( "%Y-%m-%dT%H:%M:%S.%f [%l] - %v" );

	Barrier barrier( numThreads + 1 );
	std::atomic< bool > measure{ false };
	std::atomic< std::uint64_t > totalLogged{ 0 };

	std::vector< std::thread > threads;
	threads.reserve( static_cast< std::size_t >( numThreads ) );

	for ( int i = 0; i < numThreads; ++i )
	{
		threads.emplace_back( [ &, i ]() {
			barrier.wait();
			std::uint64_t count = 0;
			while ( measure.load( std::memory_order_relaxed ) )
			{
				logger->info( "spdlog MT bench {} {}", i, count );
				++count;
			}
			totalLogged.fetch_add( count, std::memory_order_relaxed );
		} );
	}

	// warmup
	measure.store( true, std::memory_order_relaxed );
	barrier.wait();
	std::this_thread::sleep_for( std::chrono::seconds( warmupSecs ) );

	totalLogged.store( 0, std::memory_order_relaxed );

	const auto benchStart = std::chrono::steady_clock::now();
	std::this_thread::sleep_for( std::chrono::duration< double >( durationSecs ) );
	measure.store( false, std::memory_order_relaxed );

	for ( auto& t : threads )
	{
		t.join();
	}

	const auto benchEnd = std::chrono::steady_clock::now();
	const double elapsed = std::chrono::duration< double >( benchEnd - benchStart ).count();

	const std::uint64_t logged = totalLogged.load( std::memory_order_relaxed );

	logger->flush();
	spdlog::drop( ( "spdlog_mt_" + std::to_string( numThreads ) ) );
	spdlog::shutdown();

	std::uint64_t delivered = 0;
	if ( nullSink )
	{
		// counting sink counter gives exact delivered count - every log() call
		// increments it, proving the backend did real work on each record
		delivered = spdlogCounting->count();
	}
	else
	{
		delivered = countFileLines( ( "spdlog_mt_bench_" + std::to_string( numThreads ) + ".log" ).c_str() );
	}
	const std::uint64_t dropped = logged > delivered ? logged - delivered : 0;

	BenchResult result;
	result.library = nullSink ? "spdlog (null)" : "spdlog";
	result.threads = numThreads;
	result.durationSecs = elapsed;
	result.totalLogged = logged;
	result.delivered = delivered;
	result.dropped = dropped;
	return result;
}

#endif // HAVE_SPDLOG

// ============================================================================
// Quill benchmark
// ============================================================================

// printResult is defined after the benchmark functions - forward-declared here
// so runQuill() can print its result before cleanup crashes
static void printResult( const BenchResult& r );

#if defined( HAVE_QUILL )

struct QuillMTOptions : quill::FrontendOptions
{
	static constexpr quill::QueueType queue_type = quill::QueueType::BoundedDropping;
	static constexpr std::uint32_t initial_queue_capacity = QUILL_QUEUE_BYTES;
};

using QuillMTFrontend = quill::FrontendImpl< QuillMTOptions >;
using QuillMTLogger = quill::LoggerImpl< QuillMTOptions >;

static BenchResult runQuill( int numThreads, double durationSecs, int warmupSecs, bool nullSink = false )
{
	quill::FileSinkConfig cfg;
	cfg.set_open_mode( 'w' );
	cfg.set_write_buffer_size( 128 * 1024 );

	auto quillCounting = std::make_shared< QuillCountingSink >();
	std::shared_ptr< quill::Sink > sink = nullSink
		? std::static_pointer_cast< quill::Sink >( quillCounting )
		: QuillMTFrontend::create_or_get_sink< quill::FileSink >(
			( "quill_mt_bench_" + std::to_string( numThreads ) + ".log" ), cfg );

	QuillMTLogger* logger = QuillMTFrontend::create_or_get_logger(
		( "quill_mt_" + std::to_string( numThreads ) ),
		std::move( sink ),
		quill::PatternFormatterOptions{
			"%(time) [%(log_level_short_code)] %(file_name):%(line_number) %(caller_function) - %(message)",
			"%Y-%m-%dT%H:%M:%S.%Qns" } );

	Barrier barrier( numThreads + 1 );
	Barrier exitBarrier( numThreads + 1 ); // holds threads alive until backend stops
	std::atomic< bool > measure{ false };
	std::atomic< int > completedThreads{ 0 };

	std::vector< std::atomic< std::uint64_t > > logged( static_cast< std::size_t >( numThreads ) );
	for ( auto& a : logged )
	{
		a.store( 0 );
	}

	std::vector< std::thread > threads;
	threads.reserve( static_cast< std::size_t >( numThreads ) );

	for ( int i = 0; i < numThreads; ++i )
	{
		threads.emplace_back( [ &, i ]() {
			barrier.wait();

			std::uint64_t count = 0;
			while ( measure.load( std::memory_order_relaxed ) )
			{
				LOG_INFO( logger, "Quill MT bench {}", i );
				++count;
			}

			logged[ static_cast< std::size_t >( i ) ].store( count, std::memory_order_relaxed );

			// signal that this thread has finished logging
			completedThreads.fetch_add( 1, std::memory_order_release );

			// hold alive until backend stops - prevents ScopedThreadContext
			// destructor from racing with the backend thread
			exitBarrier.wait();
		} );
	}

	// warmup
	measure.store( true, std::memory_order_relaxed );
	barrier.wait();
	std::this_thread::sleep_for( std::chrono::seconds( warmupSecs ) );

	// reset logged counters at the start of the measurement window
	for ( auto& a : logged )
	{
		a.store( 0 );
	}
	completedThreads.store( 0, std::memory_order_relaxed );

	const auto benchStart = std::chrono::steady_clock::now();
	std::this_thread::sleep_for( std::chrono::duration< double >( durationSecs ) );
	measure.store( false, std::memory_order_relaxed );

	// wait for all threads to exit their logging loops before calling Backend::stop -
	// this ensures no thread is mid-enqueue when the backend starts draining
	while ( completedThreads.load( std::memory_order_acquire ) < numThreads )
	{
		std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
	}

	const auto benchEnd = std::chrono::steady_clock::now();
	const double elapsed = std::chrono::duration< double >( benchEnd - benchStart ).count();

	// accumulate logged counts - all threads have stored their counts
	// (completedThreads == numThreads guarantees this via release/acquire)
	std::uint64_t totalLogged = 0;
	for ( const auto& a : logged )
	{
		totalLogged += a.load( std::memory_order_relaxed );
	}

	// flush_log() drains all enqueued records while threads are still alive
	logger->flush_log();

	std::uint64_t delivered = 0;
	if ( nullSink )
	{
		// counting sink counter gives exact delivered count - every write_log() call
		// increments it, proving Quill's backend does full decode+format work per record
		delivered = quillCounting->count();
	}
	else
	{
		delivered = countFileLines( ( "quill_mt_bench_" + std::to_string( numThreads ) + ".log" ).c_str() );
	}
	const std::uint64_t dropped = totalLogged > delivered ? totalLogged - delivered : 0;

	BenchResult result;
	result.library = nullSink ? "Quill (null)" : "Quill";
	result.threads = numThreads;
	result.durationSecs = elapsed;
	result.totalLogged = totalLogged;
	result.delivered = delivered;
	result.dropped = dropped;

	// print result before any cleanup that may crash
	printResult( result );
	std::cout.flush();
	std::fflush( stdout );

	// release threads and join - ScopedThreadContext destructors fire as
	// threads exit; backend thread is still running but has no more records
	// to process (flush_log completed above).  TerminateProcess in main()
	// kills the backend thread hard - no Backend::stop() needed.
	exitBarrier.wait();
	for ( auto& t : threads )
	{
		t.join();
	}

	return result;
}

#endif // HAVE_QUILL

// ============================================================================
// Output
// ============================================================================

static std::string fmtRate( double v )
{
	std::ostringstream oss;
	if ( v >= 1e9 )
	{
		oss << std::fixed << std::setprecision( 2 ) << v / 1e9 << "G/s";
	}
	else if ( v >= 1e6 )
	{
		oss << std::fixed << std::setprecision( 2 ) << v / 1e6 << "M/s";
	}
	else if ( v >= 1e3 )
	{
		oss << std::fixed << std::setprecision( 1 ) << v / 1e3 << "K/s";
	}
	else
	{
		oss << static_cast< int >( v ) << "/s";
	}
	return oss.str();
}

static std::string fmtCount( std::uint64_t v )
{
	std::ostringstream oss;
	if ( v >= 1000000000ULL )
	{
		oss << std::fixed << std::setprecision( 2 ) << v / 1e9 << "G";
	}
	else if ( v >= 1000000ULL )
	{
		oss << std::fixed << std::setprecision( 2 ) << v / 1e6 << "M";
	}
	else if ( v >= 1000ULL )
	{
		oss << std::fixed << std::setprecision( 1 ) << v / 1e3 << "K";
	}
	else
	{
		oss << v;
	}
	return oss.str();
}

static void printHeader()
{
	std::cout
		<< std::left
		<< std::setw( 20 ) << "Library"
		<< std::setw( 9 ) << "Threads"
		<< std::setw( 10 ) << "Dur(s)"
		<< std::setw( 14 ) << "Logged"
		<< std::setw( 14 ) << "Delivered"
		<< std::setw( 12 ) << "Dropped"
		<< std::setw( 10 ) << "Drop%"
		<< std::setw( 14 ) << "Logged/s"
		<< "Delivered/s"
		<< "\n"
		<< std::string( 117, '-' ) << "\n";
}

static void printResult( const BenchResult& r )
{
	std::ostringstream dropPct;
	dropPct << std::fixed << std::setprecision( 1 ) << r.dropRatePct() << "%";

	std::cout
		<< std::left
		<< std::setw( 20 ) << r.library
		<< std::setw( 9 ) << r.threads
		<< std::setw( 10 ) << std::fixed << std::setprecision( 2 ) << r.durationSecs
		<< std::setw( 14 ) << fmtCount( r.totalLogged )
		<< std::setw( 14 ) << fmtCount( r.delivered )
		<< std::setw( 12 ) << fmtCount( r.dropped )
		<< std::setw( 10 ) << dropPct.str()
		<< std::setw( 14 ) << fmtRate( r.loggedPerSec() )
		<< fmtRate( r.deliveredPerSec() )
		<< "\n";
}

// ============================================================================
// Main
// ============================================================================

int main( int argc, char* argv[] )
{
	if ( argc < 2 )
	{
		std::cerr
			<< "Usage: " << argv[ 0 ]
			<< " <threads> [duration_secs] [warmup_secs] [--nova-only|--quill-only]\n";
		return 1;
	}

	const int numThreads = std::atoi( argv[ 1 ] );
	const double durationSec = argc >= 3 ? std::atof( argv[ 2 ] ) : 3.0;
	const int warmupSecs = argc >= 4 ? std::atoi( argv[ 3 ] ) : 1;

	bool novaOnly = false;
	bool novaBatchOnly = false;
	bool quillOnly = false;
	bool spdlogOnly = false;
	bool nullSink = false;
	for ( int i = 1; i < argc; ++i )
	{
		std::string arg( argv[ i ] );
		if ( arg == "--nova-only" )
		{
			novaOnly = true;
		}
		if ( arg == "--nova-batch-only" )
		{
			novaBatchOnly = true;
		}
		if ( arg == "--quill-only" )
		{
			quillOnly = true;
		}
		if ( arg == "--spdlog-only" )
		{
			spdlogOnly = true;
		}
		if ( arg == "--null-sink" )
		{
			nullSink = true;
		}
		if ( arg == "--pool-kb" && i + 1 < argc )
		{
			const std::size_t kb = static_cast< std::size_t >( std::atoll( argv[ ++i ] ) );
			if ( kb <= 256 )
			{
				glob_novaPoolBytes = NOVA_POOL_256K;
			}
			else if ( kb <= 512 )
			{
				glob_novaPoolBytes = NOVA_POOL_512K;
			}
			else if ( kb <= 1024 )
			{
				glob_novaPoolBytes = NOVA_POOL_1M;
			}
			else if ( kb <= 4096 )
			{
				glob_novaPoolBytes = NOVA_POOL_4M;
			}
			else
			{
				glob_novaPoolBytes = NOVA_POOL_16M;
			}
		}
	}

	const bool anyOnly = novaOnly || novaBatchOnly || quillOnly || spdlogOnly;
	const bool doNova = novaOnly || ! anyOnly;
	const bool doNovaBatch = novaBatchOnly || ! anyOnly;
	const bool doSpdlog = spdlogOnly || ! anyOnly;
	const bool doQuill = quillOnly || ! anyOnly;

	if ( numThreads < 1 )
	{
		std::cerr << "threads must be >= 1\n";
		return 1;
	}

	std::cerr
		<< "benchmark_multithreaded"
		<< "  threads=" << numThreads
		<< "  duration=" << durationSec << "s"
		<< "  warmup=" << warmupSecs << "s"
		<< ( nullSink ? "  counting-sink" : "  file-sink" )
		<< "  nova_pool=" << ( glob_novaPoolBytes / 1024 ) << "KB"
#if defined( HAVE_SPDLOG )
		<< "  spdlog_queue=" << ( 256 * numThreads ) << "KB"
#endif
#if defined( HAVE_QUILL )
		<< "  quill_queue_per_thread=256KB"
#endif
		<< "\n";

#if defined( HAVE_QUILL )
	if ( doQuill )
	{
		quill::BackendOptions backendOptions;
		quill::Backend::start( backendOptions );
	}
#endif

	printHeader();
	std::cout.flush();

	if ( doNova )
	{
		printResult( runNova( numThreads, durationSec, warmupSecs, nullSink ) );
		std::cout.flush();
	}

	if ( doNovaBatch )
	{
		printResult( runNovaBatch( numThreads, durationSec, warmupSecs, nullSink ) );
		std::cout.flush();
		std::fflush( stdout );
	}

#if defined( HAVE_SPDLOG )
	if ( doSpdlog )
	{
		printResult( runSpdlog( numThreads, durationSec, warmupSecs, nullSink ) );
		std::cout.flush();
		std::fflush( stdout );
	}
#endif

#if defined( HAVE_QUILL )
	if ( doQuill )
	{
		runQuill( numThreads, durationSec, warmupSecs, nullSink );
		// result already printed inside runQuill() before cleanup
	}
#endif

	std::cout.flush();
	std::cerr.flush();
	std::fflush( stdout );
	std::fflush( stderr );

#if defined( _WIN32 )
	// hard-terminate to avoid crashes during process shutdown on MinGW.
	// Backend::stop() and full thread cleanup are skipped - all records have
	// been flushed and results printed.  The exact cause of the shutdown crash
	// was not definitively diagnosed: stack traces pointed to
	// ScopedThreadContext::~ScopedThreadContext racing with LdrShutdownThread,
	// consistent with a MinGW pthread TLS destructor ordering issue, but
	// multiple attempted fixes (exitBarrier, completedThreads polling,
	// varying flush_log/Backend::stop ordering) gave inconsistent results
	// across thread counts suggesting a timing-dependent race whose root
	// cause remains uncertain.  TerminateProcess bypasses all user-mode
	// cleanup and reliably avoids the crash.
	SetErrorMode( SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX );
	TerminateProcess( GetCurrentProcess(), 0 );
#else
	_Exit( 0 );
#endif
}
