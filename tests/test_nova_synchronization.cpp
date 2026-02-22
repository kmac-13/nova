/**
 * @file test_nova_synchronization.cpp
 * @brief Google Test unit tests for Nova synchronization primitives
 *
 * Each test uses its own unique tag to ensure Logger<Tag>::_sink is never
 * shared between tests.  Sharing a single SyncTag across tests would create
 * a window between the ScopedConfigurator bind in one test and the unbind at
 * test exit where a worker thread from a concurrent or repeated test could
 * read a stale or null sink pointer and silently drop a record, causing
 * intermittent count failures.
 */

#include "kmac/nova/nova.h"
#include "kmac/nova/scoped_configurator.h"
#include "kmac/nova/extras/ostream_sink.h"
#include "kmac/nova/extras/spinlock_sink.h"
#include "kmac/nova/extras/synchronized_sink.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// shared timestamp function
std::uint64_t syncTestTimestamp() noexcept { return 77777ULL; }

// unique tag per test -- each tag has its own Logger<Tag>::_sink atomic, so
// a ScopedConfigurator in one test can never interfere with another test
struct SyncSingleThreadTag {};
NOVA_LOGGER_TRAITS( SyncSingleThreadTag, SYNC.SINGLE, true, syncTestTimestamp );

struct SyncMultiThreadTag {};
NOVA_LOGGER_TRAITS( SyncMultiThreadTag, SYNC.MULTI, true, syncTestTimestamp );

struct SpinSingleThreadTag {};
NOVA_LOGGER_TRAITS( SpinSingleThreadTag, SPIN.SINGLE, true, syncTestTimestamp );

struct SpinMultiThreadTag {};
NOVA_LOGGER_TRAITS( SpinMultiThreadTag, SPIN.MULTI, true, syncTestTimestamp );

struct SyncNoRaceTag {};
NOVA_LOGGER_TRAITS( SyncNoRaceTag, SYNC.NORACE, true, syncTestTimestamp );

struct SpinNoRaceTag {};
NOVA_LOGGER_TRAITS( SpinNoRaceTag, SPIN.NORACE, true, syncTestTimestamp );

struct SyncStressTag {};
NOVA_LOGGER_TRAITS( SyncStressTag, SYNC.STRESS, true, syncTestTimestamp );

struct MixedMutexTag {};
NOVA_LOGGER_TRAITS( MixedMutexTag, MIXED.MUTEX, true, syncTestTimestamp );

struct MixedSpinTag {};
NOVA_LOGGER_TRAITS( MixedSpinTag, MIXED.SPIN, true, syncTestTimestamp );

// distinct buffer sizes so MixedMutexTag and MixedSpinTag threads each get
// their own TlsTruncBuilderStorage<N>::builder TLS instance with zero overlap
static constexpr std::size_t MIXED_MUTEX_BUF = 512;
static constexpr std::size_t MIXED_SPIN_BUF  = 768;

struct SyncOrderTag {};
NOVA_LOGGER_TRAITS( SyncOrderTag, SYNC.ORDER, true, syncTestTimestamp );


// counter sink for testing
class CounterSink : public kmac::nova::Sink
{
public:
	std::atomic< size_t > count { 0 };

	void process( const kmac::nova::Record& ) override
	{
		++count;
	}
};

class NovaSynchronization : public ::testing::Test
{
protected:
	void SetUp() override { }
	void TearDown() override { }
};

TEST_F( NovaSynchronization, SynchronizedSinkSingleThread )
{
	CounterSink counter;
	kmac::nova::extras::SynchronizedSink syncSink( counter );

	kmac::nova::ScopedConfigurator config;
	config.bind< SyncSingleThreadTag >( &syncSink );

	NOVA_LOG( SyncSingleThreadTag ) << "test 1";
	NOVA_LOG( SyncSingleThreadTag ) << "test 2";
	NOVA_LOG( SyncSingleThreadTag ) << "test 3";

	EXPECT_EQ( counter.count.load(), 3u );
}

TEST_F( NovaSynchronization, SynchronizedSinkMultiThread )
{
	CounterSink counter;
	kmac::nova::extras::SynchronizedSink syncSink( counter );

	kmac::nova::ScopedConfigurator config;
	config.bind< SyncMultiThreadTag >( &syncSink );

	const int numThreads = 10;
	const int logsPerThread = 100;

	std::vector< std::thread > threads;
	for ( int i = 0; i < numThreads; ++i )
	{
		threads.emplace_back( [ logsPerThread ]() {
			for ( int j = 0; j < logsPerThread; ++j )
			{
				NOVA_LOG( SyncMultiThreadTag ) << "thread log " << j;
			}
		} );
	}

	for ( auto& t : threads )
	{
		t.join();
	}

	EXPECT_EQ( counter.count.load(), size_t( numThreads * logsPerThread ) );
}

TEST_F( NovaSynchronization, SpinlockSinkSingleThread )
{
	CounterSink counter;
	kmac::nova::extras::SpinlockSink spinSink( counter );

	kmac::nova::ScopedConfigurator config;
	config.bind< SpinSingleThreadTag >( &spinSink );

	NOVA_LOG( SpinSingleThreadTag ) << "test 1";
	NOVA_LOG( SpinSingleThreadTag ) << "test 2";

	EXPECT_EQ( counter.count.load(), 2u );
}

TEST_F( NovaSynchronization, SpinlockSinkMultiThread )
{
	CounterSink counter;
	kmac::nova::extras::SpinlockSink spinSink( counter );

	kmac::nova::ScopedConfigurator config;
	config.bind< SpinMultiThreadTag >( &spinSink );

	const int numThreads = 10;
	const int logsPerThread = 100;

	std::vector< std::thread > threads;
	for ( int i = 0; i < numThreads; ++i )
	{
		threads.emplace_back( [ logsPerThread ]() {
			for ( int j = 0; j < logsPerThread; ++j )
			{
				NOVA_LOG( SpinMultiThreadTag ) << "spinlock log " << j;
			}
		} );
	}

	for ( auto& t : threads )
	{
		t.join();
	}

	EXPECT_EQ( counter.count.load(), size_t( numThreads * logsPerThread ) );
}

TEST_F( NovaSynchronization, SynchronizedSinkNoRaceConditions )
{
	// use a vector accumulator rather than ostringstream -- ostringstream on MSVC
	// acquires an internal locale lock on every write() which can interact with
	// external synchronization under contention and produce intermittent lost writes
	std::vector< std::string > messages;
	messages.reserve( 250 );

	class VectorSink : public kmac::nova::Sink
	{
	public:
		std::vector< std::string >& out_;
		explicit VectorSink( std::vector< std::string >& v ) : out_( v ) {}
		void process( const kmac::nova::Record& record ) noexcept override
		{
			// called under SynchronizedSink's mutex -- single-writer, no extra lock needed
			out_.emplace_back( record.message, record.messageSize );
		}
	};

	VectorSink baseSink( messages );
	kmac::nova::extras::SynchronizedSink syncSink( baseSink );

	kmac::nova::ScopedConfigurator config;
	config.bind< SyncNoRaceTag >( &syncSink );

	const int numThreads = 5;
	const int logsPerThread = 50;

	std::vector< std::thread > threads;
	for ( int i = 0; i < numThreads; ++i )
	{
		threads.emplace_back( [ i, logsPerThread ]() {
			for ( int j = 0; j < logsPerThread; ++j )
			{
				NOVA_LOG( SyncNoRaceTag ) << "Thread " << i << " log " << j;
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

TEST_F( NovaSynchronization, SpinlockSinkNoRaceConditions )
{
	// same approach as SynchronizedSinkNoRaceConditions: vector accumulator avoids
	// ostringstream's internal MSVC locale lock which causes intermittent lost writes
	std::vector< std::string > messages;
	messages.reserve( 250 );

	class VectorSink : public kmac::nova::Sink
	{
	public:
		std::vector< std::string >& out_;
		explicit VectorSink( std::vector< std::string >& v ) : out_( v ) {}
		void process( const kmac::nova::Record& record ) noexcept override
		{
			// called under SpinlockSink's spinlock -- single-writer, no extra lock needed
			out_.emplace_back( record.message, record.messageSize );
		}
	};

	VectorSink baseSink( messages );
	kmac::nova::extras::SpinlockSink spinSink( baseSink );

	kmac::nova::ScopedConfigurator config;
	config.bind< SpinNoRaceTag >( &spinSink );

	const int numThreads = 5;
	const int logsPerThread = 50;

	std::vector< std::thread > threads;
	for ( int i = 0; i < numThreads; ++i )
	{
		threads.emplace_back( [ i, logsPerThread ]() {
			for ( int j = 0; j < logsPerThread; ++j )
			{
				NOVA_LOG( SpinNoRaceTag ) << "Thread " << i << " log " << j;
			}
		} );
	}

	for ( auto& t : threads )
	{
		t.join();
	}

	// count check first: if this fails, the spinlock is dropping records
	ASSERT_EQ( messages.size(), static_cast< size_t >( numThreads * logsPerThread ) ) << "SpinlockSink dropped records";

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

TEST_F( NovaSynchronization, SynchronizedSinkStressTest )
{
	CounterSink counter;
	kmac::nova::extras::SynchronizedSink syncSink( counter );

	kmac::nova::ScopedConfigurator config;
	config.bind< SyncStressTag >( &syncSink );

	const int numThreads = 20;
	const int logsPerThread = 1000;

	std::vector< std::thread > threads;
	for ( int i = 0; i < numThreads; ++i )
	{
		threads.emplace_back( [ logsPerThread ]() {
			for ( int j = 0; j < logsPerThread; ++j )
			{
				NOVA_LOG( SyncStressTag ) << "stress " << j;
			}
		} );
	}

	for ( auto& t : threads )
	{
		t.join();
	}

	EXPECT_EQ( counter.count.load(), size_t( numThreads * logsPerThread ) );
}

TEST_F( NovaSynchronization, MixedSyncPrimitives )
{
	CounterSink counter1;
	CounterSink counter2;

	kmac::nova::extras::SynchronizedSink mutexSink( counter1 );
	kmac::nova::extras::SpinlockSink spinSink( counter2 );

	kmac::nova::ScopedConfigurator config;
	config.bind< MixedMutexTag >( &mutexSink );
	config.bind< MixedSpinTag >( &spinSink );

	const int numThreads = 10;
	const int logsPerThread = 100;

	std::vector< std::thread > threads;
	for ( int i = 0; i < numThreads; ++i )
	{
		threads.emplace_back( [ logsPerThread, i ]() {
			// even threads use MixedMutexTag with a dedicated buffer size,
			// odd threads use MixedSpinTag with a different buffer size;
			// distinct sizes give each group its own TLS builder instance so
			// there is zero possibility of cross-tag state in the TLS builder
			if ( i % 2 == 0 )
			{
				for ( int j = 0; j < logsPerThread; ++j )
				{
					NOVA_LOG_BUF( MixedMutexTag, MIXED_MUTEX_BUF ) << "mixed " << j;
				}
			}
			else
			{
				for ( int j = 0; j < logsPerThread; ++j )
				{
					NOVA_LOG_BUF( MixedSpinTag, MIXED_SPIN_BUF ) << "mixed " << j;
				}
			}
		} );
	}

	for ( auto& t : threads )
	{
		t.join();
	}

	// each sink should have received logs from half the threads
	size_t expectedPerSink = ( numThreads / 2 ) * logsPerThread;
	EXPECT_EQ( counter1.count.load(), expectedPerSink );
	EXPECT_EQ( counter2.count.load(), expectedPerSink );
}

TEST_F( NovaSynchronization, SynchronizedSinkPreservesOrdering )
{
	std::ostringstream oss;
	kmac::nova::extras::OStreamSink baseSink( oss );
	kmac::nova::extras::SynchronizedSink syncSink( baseSink );

	kmac::nova::ScopedConfigurator config;
	config.bind< SyncOrderTag >( &syncSink );

	// single-threaded ordering should be preserved
	for ( int i = 0; i < 10; ++i )
	{
		NOVA_LOG( SyncOrderTag ) << "message " << i;
	}

	std::string output = oss.str();

	// find positions of each message
	std::vector< size_t > positions;
	for ( int i = 0; i < 10; ++i )
	{
		std::string msg = "message " + std::to_string( i );
		size_t pos = output.find( msg );
		EXPECT_NE( pos, std::string::npos ) << "Message not found: " << msg;
		positions.push_back( pos );
	}

	// verify positions are in order
	for ( size_t i = 1; i < positions.size(); ++i )
	{
		EXPECT_GT( positions[ i ], positions[ i - 1 ] ) << "Message " << i << " appeared before message " << ( i - 1 );
	}
}
