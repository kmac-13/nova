/**
 * @file benchmark_delivery_latency.cpp
 * @brief Sustained delivery latency: time to write N records to a file guaranteed
 *
 * Measures wall-clock time from first log call to last record confirmed written.
 * Unlike the throughput benchmarks (which accept drops), this benchmark
 * guarantees every record is delivered - the producer blocks if the sink is full.
 *
 * This answers: "if I need N records in my log file, how long do I wait?"
 *
 * Nova:  SynchronizedSink wrapping FormattingFileSink - mutex-serialised,
 *        synchronous format + write on the calling thread.  All threads contend
 *        on a single mutex; scaling is limited by lock contention.
 *
 * spdlog sync: basic_file_sink_mt with mutex - equivalent to Nova's approach,
 *        formats and writes synchronously on the calling thread.
 *
 * spdlog async: thread pool with block overflow policy - producer blocks when
 *        queue is full.  Queue scaled to 256 KB x N threads (optimal for
 *        multi-thread conditions; matches Quill's total buffer capacity).
 *        Backend formats via {fmt} and writes.
 *
 * Quill: BoundedBlocking queue - producer blocks when queue is full until the
 *        backend drains space.  Per-thread SPSC queues, 256 KB each.
 *        Backend formats via {fmt}.
 *
 * NanoLog: excluded - Linux-only (uses __rdtsc and glibc-specific features),
 *        does not build on MinGW/Windows.  Also uses binary compressed logs
 *        requiring offline decoding, making it architecturally incomparable.
 *
 * Note that Nova currently does not have a blocking async approach and Quill does
 * not have a sync approach.
 *
 * Usage:
 *   benchmark_delivery_latency [messages] [threads] [--nova-only|--quill-only|--spdlog-only]
 *
 *   messages   total records to deliver across all threads (default: 1000000)
 *   threads    number of producer threads (default: 1)
 */

#include <kmac/nova.h>
#include <kmac/nova/extras/custom_formatter.h>
#include <kmac/nova/extras/formatting_file_sink.h>
#include <kmac/nova/extras/synchronized_sink.h>

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

#if defined( _WIN32 )
#include <windows.h>
#endif

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
		: _count( count ), _waiting( 0 ), _generation( 0 )
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

struct DeliveryResult
{
	std::string library;
	int threads = 0;
	std::uint64_t totalMessages = 0;
	double elapsedSecs = 0.0;

	double msgsPerSec() const
	{
		return elapsedSecs > 0.0
			? static_cast< double >( totalMessages ) / elapsedSecs
			: 0.0;
	}

	double nsPerMsg() const
	{
		return totalMessages > 0
			? elapsedSecs * 1e9 / static_cast< double >( totalMessages )
			: 0.0;
	}
};

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
		oss << std::fixed << std::setprecision( 2 ) << v / 1e9  << "G";
	}
	else if ( v >= 1000000ULL )
	{
		oss << std::fixed << std::setprecision( 2 ) << v / 1e6  << "M";
	}
	else if ( v >= 1000ULL )
	{
		oss << std::fixed << std::setprecision( 1 ) << v / 1e3  << "K";
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
		<< std::setw( 22 ) << "Library"
		<< std::setw( 9 ) << "Threads"
		<< std::setw( 12 ) << "Messages"
		<< std::setw( 12 ) << "Elapsed(s)"
		<< std::setw( 14 ) << "Msgs/s"
		<< "ns/msg"
		<< "\n"
		<< std::string( 82, '-' ) << "\n";
}

static void printResult( const DeliveryResult& r )
{
	std::ostringstream nsOss;
	nsOss << std::fixed << std::setprecision( 1 ) << r.nsPerMsg() << " ns";

	std::cout
		<< std::left
		<< std::setw( 22 ) << r.library
		<< std::setw( 9 ) << r.threads
		<< std::setw( 12 ) << fmtCount( r.totalMessages )
		<< std::setw( 12 ) << std::fixed << std::setprecision( 3 ) << r.elapsedSecs
		<< std::setw( 14 ) << fmtRate( r.msgsPerSec() )
		<< nsOss.str()
		<< "\n";
}

// ============================================================================
// Helpers
// ============================================================================

static std::uint64_t messagesForThread( std::uint64_t total, int numThreads, int threadIdx )
{
	const std::uint64_t perThread = total / static_cast< std::uint64_t >( numThreads );
	return ( threadIdx == numThreads - 1 )
		? total - perThread * static_cast< std::uint64_t >( numThreads - 1 )
		: perThread;
}

// ============================================================================
// Counting sinks - increment an atomic counter per record, no I/O.
// Replaces null sinks to prove the backend does real work on each record.
// ============================================================================

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
	std::uint64_t count() const noexcept
	{
		return _count.load( std::memory_order_relaxed );
	}
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
	std::uint64_t count() const noexcept
	{
		return _count.load( std::memory_order_relaxed );
	}
};
#endif // HAVE_QUILL

// ============================================================================
// Nova benchmark
// ============================================================================

struct NovaDeliveryTag {};
NOVA_LOGGER_TRAITS( NovaDeliveryTag, DELIVERY, true, kmac::nova::TimestampHelper::systemNanosecs );

using NovaFormatter = kmac::nova::extras::CustomFormatter<
	kmac::nova::extras::FieldSpec< '\0', kmac::nova::extras::Field::TimestampISO, ' '  >,
	kmac::nova::extras::FieldSpec< '[',  kmac::nova::extras::Field::Tag,      ']'  >,
	kmac::nova::extras::FieldSpec< ' ',  kmac::nova::extras::Field::File,     ':'  >,
	kmac::nova::extras::FieldSpec< '\0', kmac::nova::extras::Field::Line,     ' '  >,
	kmac::nova::extras::FieldSpec< '\0', kmac::nova::extras::Field::Function, '\0' >,
	kmac::nova::extras::FieldSpec< ' ',  kmac::nova::extras::Field::None,     '-'  >,
	kmac::nova::extras::FieldSpec< ' ',  kmac::nova::extras::Field::Message,  '\n' >
>;
using NovaFileSink = kmac::nova::extras::FormattingFileSink<>;
using NovaSyncSink = kmac::nova::extras::SynchronizedSink;

static DeliveryResult runNova( std::uint64_t totalMessages, int numThreads, bool nullSink = false )
{
	FILE* file = nullptr;
	NovaFormatter formatter;
	NovaCountingSink countingSink;
	std::unique_ptr< NovaFileSink > fileSinkPtr;
	std::unique_ptr< NovaSyncSink > syncSinkPtr;

	if ( nullSink )
	{
		syncSinkPtr = std::unique_ptr< NovaSyncSink >( new NovaSyncSink( countingSink ) );
	}
	else
	{
		std::remove( ( "nova_delivery_" + std::to_string( numThreads ) + ".log" ).c_str() );
		file = std::fopen( ( "nova_delivery_" + std::to_string( numThreads ) + ".log" ).c_str(), "wb" );
		std::setvbuf( file, nullptr, _IOFBF, 128 * 1024 );
		fileSinkPtr = std::unique_ptr< NovaFileSink >( new NovaFileSink( file, &formatter ) );
		syncSinkPtr = std::unique_ptr< NovaSyncSink >( new NovaSyncSink( *fileSinkPtr ) );
	}

	kmac::nova::Logger< NovaDeliveryTag >::bindSink( syncSinkPtr.get() );

	Barrier barrier( numThreads + 1 );
	std::vector< std::thread > threads;
	threads.reserve( static_cast< std::size_t >( numThreads ) );

	for ( int i = 0; i < numThreads; ++i )
	{
		threads.emplace_back( [ &, i ]() {
			const std::uint64_t count = messagesForThread( totalMessages, numThreads, i );
			barrier.wait();
			for ( std::uint64_t j = 0; j < count; ++j )
			{
				NOVA_LOG( NovaDeliveryTag ) << "Nova delivery bench " << i << " " << j;
			}
		} );
	}

	barrier.wait();
	const auto start = std::chrono::steady_clock::now();

	for ( auto& t : threads )
	{
		t.join();
	}
	if ( ! nullSink )
	{
		std::fflush( file );
	}

	const auto end = std::chrono::steady_clock::now();

	kmac::nova::Logger< NovaDeliveryTag >::unbindSink();
	if ( file )
	{
		std::fclose( file );
	}

	DeliveryResult result;
	result.library = nullSink ? "Nova sync (null)" : "Nova sync";
	result.threads = numThreads;
	result.totalMessages = totalMessages;
	result.elapsedSecs = std::chrono::duration< double >( end - start ).count();
	return result;
}

// ============================================================================
// spdlog benchmarks
// ============================================================================

#if defined( HAVE_SPDLOG )

static DeliveryResult runSpdlogSync( std::uint64_t totalMessages, int numThreads, bool nullSink = false )
{
	auto spdlogCounting = std::make_shared< SpdlogCountingSink >();
	std::shared_ptr< spdlog::sinks::sink > sink = nullSink
		? std::static_pointer_cast< spdlog::sinks::sink >( spdlogCounting )
		: std::static_pointer_cast< spdlog::sinks::sink >(
			[ & ]() {
				std::remove( ( "spdlog_sync_delivery_" + std::to_string( numThreads ) + ".log" ).c_str() );
				return std::make_shared< spdlog::sinks::basic_file_sink_mt >(
					( "spdlog_sync_delivery_" + std::to_string( numThreads ) + ".log" ), true );
			}() );
	auto logger = std::make_shared< spdlog::logger >(
		( "spdlog_sync_delivery_" + std::to_string( numThreads ) ),
		sink );
	logger->set_level( spdlog::level::info );
	logger->set_pattern( "%Y-%m-%dT%H:%M:%S.%f [%l] - %v" );
	logger->flush_on( spdlog::level::off );  // manual flush at end

	Barrier barrier( numThreads + 1 );
	std::vector< std::thread > threads;
	threads.reserve( static_cast< std::size_t >( numThreads ) );

	for ( int i = 0; i < numThreads; ++i )
	{
		threads.emplace_back( [ &, i ]() {
			const std::uint64_t count = messagesForThread( totalMessages, numThreads, i );
			barrier.wait();
			for ( std::uint64_t j = 0; j < count; ++j )
			{
				logger->info( "spdlog sync delivery bench {} {}", i, j );
			}
		} );
	}

	barrier.wait();
	const auto start = std::chrono::steady_clock::now();

	for ( auto& t : threads )
	{
		t.join();
	}
	logger->flush();

	const auto end = std::chrono::steady_clock::now();

	spdlog::drop( ( "spdlog_sync_delivery_" + std::to_string( numThreads ) ) );

	DeliveryResult result;
	result.library = nullSink ? "spdlog sync (null)" : "spdlog sync";
	result.threads = numThreads;
	result.totalMessages = totalMessages;
	result.elapsedSecs = std::chrono::duration< double >( end - start ).count();
	return result;
}

static DeliveryResult runSpdlogAsync( std::uint64_t totalMessages, int numThreads, bool nullSink = false )
{
	// queue size in items - use 256 KB worth assuming ~64 bytes per item,
	// fixed regardless of thread count to match Nova's fixed 256 KB pool;
	// NOTE: Quill uses 256 KB *per thread* so its total buffer scales with
	// thread count while spdlog and Nova use a single fixed-size queue.
	// scale queue with thread count to match Quill's per-thread 256 KB buffer
	const std::size_t queueItems = ( 256 * 1024 * static_cast< std::size_t >( numThreads ) ) / 64;
	spdlog::init_thread_pool( queueItems, 1 );

	auto spdlogAsyncCounting = std::make_shared< SpdlogCountingSink >();
	std::shared_ptr< spdlog::sinks::sink > sink = nullSink
		? std::static_pointer_cast< spdlog::sinks::sink >( spdlogAsyncCounting )
		: std::static_pointer_cast< spdlog::sinks::sink >(
			[ & ]() {
				std::remove( ( "spdlog_async_delivery_" + std::to_string( numThreads ) + ".log" ).c_str() );
				return std::make_shared< spdlog::sinks::basic_file_sink_mt >(
					( "spdlog_async_delivery_" + std::to_string( numThreads ) + ".log" ), true );
			}() );
	auto logger = std::make_shared< spdlog::async_logger >(
		( "spdlog_async_delivery_" + std::to_string( numThreads ) ),
		sink,
		spdlog::thread_pool(),
		spdlog::async_overflow_policy::block ); // block, never drop
	logger->set_level( spdlog::level::info );
	logger->set_pattern( "%Y-%m-%dT%H:%M:%S.%f [%l] - %v" );

	Barrier barrier( numThreads + 1 );
	std::vector< std::thread > threads;
	threads.reserve( static_cast< std::size_t >( numThreads ) );

	for ( int i = 0; i < numThreads; ++i )
	{
		threads.emplace_back( [ &, i ]() {
			const std::uint64_t count = messagesForThread( totalMessages, numThreads, i );
			barrier.wait();
			for ( std::uint64_t j = 0; j < count; ++j )
			{
				logger->info( "spdlog async delivery bench {} {}", i, j );
			}
		} );
	}

	barrier.wait();
	const auto start = std::chrono::steady_clock::now();

	for ( auto& t : threads )
	{
		t.join();
	}
	logger->flush();  // blocks until backend writes everything

	const auto end = std::chrono::steady_clock::now();

	spdlog::drop( ( "spdlog_async_delivery_" + std::to_string( numThreads ) ) );
	spdlog::shutdown();

	DeliveryResult result;
	result.library = nullSink ? "spdlog async (null)" : "spdlog async";
	result.threads = numThreads;
	result.totalMessages = totalMessages;
	result.elapsedSecs = std::chrono::duration< double >( end - start ).count();
	return result;
}

#endif // HAVE_SPDLOG

// ============================================================================
// Quill benchmark
// ============================================================================

#if defined( HAVE_QUILL )

struct QuillBlockingOptions : quill::FrontendOptions
{
	// BoundedBlocking: producer blocks when queue is full, never drops or grows
	static constexpr quill::QueueType queue_type = quill::QueueType::BoundedBlocking;
	static constexpr std::uint32_t initial_queue_capacity = 256 * 1024; // 256 KB per thread
};

using QuillBlockingFrontend = quill::FrontendImpl< QuillBlockingOptions >;
using QuillBlockingLogger = quill::LoggerImpl< QuillBlockingOptions >;

static DeliveryResult runQuill( std::uint64_t totalMessages, int numThreads, bool nullSink = false )
{
	quill::FileSinkConfig cfg;
	cfg.set_open_mode( 'w' );
	cfg.set_write_buffer_size( 128 * 1024 );

	auto quillCounting = std::make_shared< QuillCountingSink >();
	std::shared_ptr< quill::Sink > sink = nullSink
		? std::static_pointer_cast< quill::Sink >( quillCounting )
		: QuillBlockingFrontend::create_or_get_sink< quill::FileSink >(
			( "quill_delivery_" + std::to_string( numThreads ) + ".log" ), cfg );

	QuillBlockingLogger* logger = QuillBlockingFrontend::create_or_get_logger(
		( "quill_delivery_" + std::to_string( numThreads ) ),
		std::move( sink ),
		quill::PatternFormatterOptions{
			"%(time) [%(log_level_short_code)] %(file_name):%(line_number) %(caller_function) - %(message)",
			"%Y-%m-%dT%H:%M:%S.%Qns" } );

	std::atomic< int > completedThreads{ 0 };
	Barrier barrier( numThreads + 1 );
	Barrier exitBarrier( numThreads + 1 );  // holds threads alive until backend stops

	std::vector< std::thread > threads;
	threads.reserve( static_cast< std::size_t >( numThreads ) );

	for ( int i = 0; i < numThreads; ++i )
	{
		threads.emplace_back( [ &, i ]() {
			const std::uint64_t count = messagesForThread( totalMessages, numThreads, i );
			barrier.wait();
			for ( std::uint64_t j = 0; j < count; ++j )
			{
				LOG_INFO( logger, "Quill delivery bench {} {}", i, j );
			}
			completedThreads.fetch_add( 1, std::memory_order_release );
			exitBarrier.wait(); // hold alive until backend stops
		} );
	}

	barrier.wait();
	const auto start = std::chrono::steady_clock::now();

	// wait for all producers to finish enqueueing
	while ( completedThreads.load( std::memory_order_acquire ) < numThreads )
	{
		std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
	}

	// flush blocks until backend has written everything
	logger->flush_log();

	const auto end = std::chrono::steady_clock::now();

	DeliveryResult result;
	result.library = nullSink ? "Quill async (null)" : "Quill async";
	result.threads = numThreads;
	result.totalMessages = totalMessages;
	result.elapsedSecs = std::chrono::duration< double >( end - start ).count();

	// print result before cleanup - cleanup may crash on MinGW
	printResult( result );
	std::cout.flush();
	std::fflush( stdout );

	quill::Backend::stop();
	exitBarrier.wait();
	for ( auto& t : threads )
	{
		t.join();
	}

	return result;
}

#endif // HAVE_QUILL

// ============================================================================
// Main
// ============================================================================

int main( int argc, char* argv[] )
{
	std::uint64_t totalMessages = 1000000ULL;
	int numThreads = 1;
	bool novaOnly = false;
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
		else if ( arg == "--quill-only" )
		{
			quillOnly = true;
		}
		else if ( arg == "--spdlog-only" )
		{
			spdlogOnly = true;
		}
		else if ( arg == "--null-sink" )
		{
			nullSink = true;
		}
		else if ( i == 1 )
		{
			totalMessages = static_cast< std::uint64_t >( std::atoll( argv[ i ] ) );
		}
		else if ( i == 2 )
		{
			numThreads = std::atoi( argv[ i ] );
		}
	}

	if ( numThreads < 1 || totalMessages < 1 )
	{
		std::cerr
			<< "Usage: " << argv[ 0 ]
			<< " [messages] [threads] [--nova-only|--quill-only|--spdlog-only]\n";
		return 1;
	}

	// if a specific --only flag is set, run only that library
	// if no --only flag is set, run all libraries
	const bool anyOnly = novaOnly || quillOnly || spdlogOnly;
	const bool doNova = novaOnly || ! anyOnly;
	const bool doSpdlog = spdlogOnly || ! anyOnly;
	const bool doQuill = quillOnly || ! anyOnly;

	std::cerr
		<< "benchmark_delivery_latency"
		<< "  messages=" << fmtCount( totalMessages )
		<< "  threads=" << numThreads
		<< ( nullSink ? "  null-sink" : "  file-sink" )
		<< "\n";

#if defined( HAVE_QUILL )
	if ( doQuill )
	{
		quill::BackendOptions backendOptions;
		quill::Backend::start( backendOptions );
	}
#endif

	printHeader();

	if ( doNova )
	{
		printResult( runNova( totalMessages, numThreads, nullSink ) );
		std::cout.flush();
		std::fflush( stdout );
	}

#if defined( HAVE_SPDLOG )
	if ( doSpdlog )
	{
		printResult( runSpdlogSync( totalMessages, numThreads, nullSink ) );
		std::cout.flush();
		std::fflush( stdout );
		printResult( runSpdlogAsync( totalMessages, numThreads, nullSink ) );
		std::cout.flush();
		std::fflush( stdout );
	}
#endif

#if defined( HAVE_QUILL )
	if ( doQuill )
	{
		runQuill( totalMessages, numThreads, nullSink );
		// result printed inside runQuill before cleanup
	}
#endif

	std::cout.flush();
	std::fflush( stdout );

#if defined( _WIN32 )
	TerminateProcess( GetCurrentProcess(), 0 );
#else
	_Exit( 0 );
#endif
}
