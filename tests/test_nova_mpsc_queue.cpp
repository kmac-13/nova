/**
 * @file test_nova_mpsc_queue.cpp
 * @brief Unit tests for MPSCQueue
 *
 * Covers: basic push/pop, popBatch correctness, full/empty boundary
 * conditions, pop* memory ordering, sequence-slot recycling, and
 * multi-producer concurrency.
 */

#include "kmac/nova/extras/mpsc_queue.h"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

using namespace kmac::nova::extras;

// ============================================================================
// Single-threaded functional tests
// ============================================================================

class MPSCQueueST : public ::testing::Test
{
};

TEST_F( MPSCQueueST, DefaultStateIsEmpty )
{
	MPSCQueue< int, 8 > q;
	EXPECT_TRUE( q.empty() );
	EXPECT_EQ( q.size(), 0u );
}

TEST_F( MPSCQueueST, PushOnePopOne )
{
	MPSCQueue< int, 8 > q;
	EXPECT_TRUE( q.push( 42 ) );
	EXPECT_FALSE( q.empty() );
	EXPECT_EQ( q.size(), 1u );

	int val = 0;
	EXPECT_TRUE( q.pop( val ) );
	EXPECT_EQ( val, 42 );
	EXPECT_TRUE( q.empty() );
}

TEST_F( MPSCQueueST, PopFromEmptyReturnsFalse )
{
	MPSCQueue< int, 4 > q;
	int val = -1;
	EXPECT_FALSE( q.pop( val ) );
	EXPECT_EQ( val, -1 );  // output unchanged on failure
}

TEST_F( MPSCQueueST, FillToCapacity )
{
	constexpr std::size_t CAP = 4;
	MPSCQueue< int, CAP > q;

	for ( int i = 0; i < static_cast< int >( CAP ); ++i )
	{
		EXPECT_TRUE( q.push( i ) ) << "push " << i << " should succeed";
	}
	EXPECT_EQ( q.size(), CAP );
}

TEST_F( MPSCQueueST, PushWhenFullReturnsFalse )
{
	constexpr std::size_t CAP = 4;
	MPSCQueue< int, CAP > q;

	for ( int i = 0; i < static_cast< int >( CAP ); ++i )
	{
		q.push( i );
	}

	EXPECT_FALSE( q.push( 999 ) );
}

TEST_F( MPSCQueueST, FIFOOrdering )
{
	MPSCQueue< int, 8 > q;
	for ( int i = 0; i < 5; ++i )
	{
		q.push( i );
	}

	for ( int i = 0; i < 5; ++i )
	{
		int val = -1;
		ASSERT_TRUE( q.pop( val ) );
		EXPECT_EQ( val, i );
	}
}

TEST_F( MPSCQueueST, SlotRecyclingAfterDrain )
{
	// push/pop past capacity boundary to verify sequence numbers wrap correctly
	MPSCQueue< int, 4 > q;

	for ( int round = 0; round < 3; ++round )
	{
		for ( int i = 0; i < 4; ++i )
		{
			EXPECT_TRUE( q.push( round * 10 + i ) ) << "round " << round << " push " << i;
		}
		for ( int i = 0; i < 4; ++i )
		{
			int val = -1;
			ASSERT_TRUE( q.pop( val ) );
			EXPECT_EQ( val, round * 10 + i );
		}
	}
}

// ============================================================================
// popBatch tests
// ============================================================================

class MPSCQueueBatch : public ::testing::Test
{
};

TEST_F( MPSCQueueBatch, PopBatchOnEmptyQueueReturnsZero )
{
	MPSCQueue< int, 8 > q;
	int buf[ 8 ];
	EXPECT_EQ( q.popBatch( buf, 8 ), 0u );
}

TEST_F( MPSCQueueBatch, PopBatchDrainsEntireQueue )
{
	MPSCQueue< int, 8 > q;
	for ( int i = 0; i < 6; ++i )
	{
		q.push( i );
	}

	int buf[ 8 ] = {};
	std::size_t popped = q.popBatch( buf, 8 );

	EXPECT_EQ( popped, 6u );
	for ( int i = 0; i < 6; ++i )
	{
		EXPECT_EQ( buf[ i ], i );
	}
	EXPECT_TRUE( q.empty() );
}

TEST_F( MPSCQueueBatch, PopBatchRespectsMaxCount )
{
	MPSCQueue< int, 8 > q;
	for ( int i = 0; i < 6; ++i )
	{
		q.push( i );
	}

	int buf[ 8 ] = {};
	std::size_t popped = q.popBatch( buf, 3 );

	EXPECT_EQ( popped, 3u );
	for ( int i = 0; i < 3; ++i )
	{
		EXPECT_EQ( buf[ i ], i );
	}

	// remaining 3 items should still be in the queue
	EXPECT_EQ( q.size(), 3u );
}

TEST_F( MPSCQueueBatch, PopBatchThenPushFillsCorrectly )
{
	// verify slots are properly recycled after popBatch
	MPSCQueue< int, 4 > q;
	for ( int i = 0; i < 4; ++i )
	{
		q.push( i );
	}

	int buf[ 4 ];
	EXPECT_EQ( q.popBatch( buf, 4 ), 4u );
	EXPECT_TRUE( q.empty() );

	// should be able to refill
	for ( int i = 10; i < 14; ++i )
	{
		EXPECT_TRUE( q.push( i ) );
	}

	std::size_t popped = q.popBatch( buf, 4 );
	EXPECT_EQ( popped, 4u );
	for ( int i = 0; i < 4; ++i )
	{
		EXPECT_EQ( buf[ i ], 10 + i );
	}
}

TEST_F( MPSCQueueBatch, PopBatchMaxCountZeroReturnsZero )
{
	MPSCQueue< int, 8 > q;
	q.push( 1 );

	int buf[ 8 ];
	EXPECT_EQ( q.popBatch( buf, 0 ), 0u );

	// item should still be in the queue
	EXPECT_EQ( q.size(), 1u );
}

TEST_F( MPSCQueueBatch, PopBatchSlotsMarkedAvailableForProducers )
{
	// after popBatch the freed slots must be visible to producers;
	// if sequence numbers aren't advanced this push will return false
	constexpr std::size_t CAP = 4;
	MPSCQueue< int, CAP > q;

	for ( int i = 0; i < static_cast< int >( CAP ); ++i )
	{
		q.push( i );
	}

	int buf[ CAP ];
	q.popBatch( buf, CAP );

	// all slots freed - a full re-fill must succeed
	for ( int i = 0; i < static_cast< int >( CAP ); ++i )
	{
		EXPECT_TRUE( q.push( 100 + i ) ) << "push after popBatch slot " << i << " should succeed";
	}
}

TEST_F( MPSCQueueBatch, PopBatchPartialThenPopRestViaScalar )
{
	MPSCQueue< int, 8 > q;
	for ( int i = 0; i < 5; ++i )
	{
		q.push( i );
	}

	// drain 3 via batch
	int buf[ 3 ];
	ASSERT_EQ( q.popBatch( buf, 3 ), 3u );
	for ( int i = 0; i < 3; ++i )
	{
		EXPECT_EQ( buf[ i ], i );
	}

	// drain remaining 2 via scalar pop
	for ( int i = 3; i < 5; ++i )
	{
		int val = -1;
		ASSERT_TRUE( q.pop( val ) );
		EXPECT_EQ( val, i );
	}

	EXPECT_TRUE( q.empty() );
}

TEST_F( MPSCQueueBatch, InterleavedBatchAndScalarPops )
{
	MPSCQueue< int, 16 > q;
	for ( int i = 0; i < 8; ++i )
	{
		q.push( i );
	}

	// batch pop 3
	int buf[ 8 ];
	ASSERT_EQ( q.popBatch( buf, 3 ), 3u );

	// scalar pop 2
	int val;
	ASSERT_TRUE( q.pop( val ) );
	EXPECT_EQ( val, 3 );
	ASSERT_TRUE( q.pop( val ) );
	EXPECT_EQ( val, 4 );

	// batch pop remaining 3
	ASSERT_EQ( q.popBatch( buf, 8 ), 3u );
	EXPECT_EQ( buf[ 0 ], 5 );
	EXPECT_EQ( buf[ 1 ], 6 );
	EXPECT_EQ( buf[ 2 ], 7 );

	EXPECT_TRUE( q.empty() );
}

// ============================================================================
// Multi-producer concurrency tests
// ============================================================================

class MPSCQueueMT : public ::testing::Test
{
};

TEST_F( MPSCQueueMT, MultipleProducersSingleConsumer )
{
	constexpr std::size_t CAPACITY   = 4096;
	constexpr int PRODUCERS  = 4;
	constexpr int PER_THREAD = 500;

	MPSCQueue< int, CAPACITY > q;
	std::atomic< int > pushCount{ 0 };

	auto producer = [&]( int threadId ) {
		for ( int i = 0; i < PER_THREAD; ++i )
		{
			// spin until push succeeds (queue may be briefly full)
			while ( ! q.push( threadId * 10000 + i ) )
			{
				std::this_thread::yield();
			}
			pushCount.fetch_add( 1, std::memory_order_relaxed );
		}
	};

	std::vector< std::thread > threads;
	for ( int t = 0; t < PRODUCERS; ++t )
	{
		threads.emplace_back( producer, t );
	}

	// consume until all expected items are drained
	int totalConsumed = 0;
	const int expected = PRODUCERS * PER_THREAD;
	int buf[ 64 ];
	while ( totalConsumed < expected )
	{
		std::size_t n = q.popBatch( buf, 64 );
		totalConsumed += static_cast< int >( n );
		if ( n == 0 )
		{
			std::this_thread::yield();
		}
	}

	for ( auto& t : threads )
	{
		t.join();
	}

	EXPECT_EQ( totalConsumed, expected );
	EXPECT_TRUE( q.empty() );
}

TEST_F( MPSCQueueMT, NoItemsLostOrDuplicated )
{
	// each item is a unique integer; verify exact set is consumed
	constexpr std::size_t CAPACITY   = 2048;
	constexpr int PRODUCERS  = 3;
	constexpr int PER_THREAD = 300;

	MPSCQueue< int, CAPACITY > q;
	std::atomic< bool > done{ false };

	std::vector< int > consumed;
	consumed.reserve( static_cast< std::size_t >( PRODUCERS * PER_THREAD ) );

	auto producer = [&]( int base )
	{
		for ( int i = 0; i < PER_THREAD; ++i )
		{
			while ( ! q.push( base + i ) )
			{
				std::this_thread::yield();
			}
		}
	};

	std::vector< std::thread > threads;
	for ( int t = 0; t < PRODUCERS; ++t )
	{
		threads.emplace_back( producer, t * 100000 );
	}

	// consumer thread
	auto consumer = std::thread( [&]()
	{
		int buf[ 32 ];
		while ( ! done.load( std::memory_order_acquire ) || ! q.empty() )
		{
			std::size_t n = q.popBatch( buf, 32 );
			for ( std::size_t i = 0; i < n; ++i )
			{
				consumed.push_back( buf[ i ] );
			}
			if ( n == 0 )
			{
				std::this_thread::yield();
			}
		}
	} );

	for ( auto& t : threads )
	{
		t.join();
	}
	done.store( true, std::memory_order_release );
	consumer.join();

	// drain any residual items after done flag
	{
		int buf[ 64 ];
		std::size_t n;
		while ( ( n = q.popBatch( buf, 64 ) ) > 0 )
		{
			for ( std::size_t i = 0; i < n; ++i )
			{
				consumed.push_back( buf[ i ] );
			}
		}
	}

	EXPECT_EQ( consumed.size(), static_cast< std::size_t >( PRODUCERS * PER_THREAD ) );

	// no duplicates: sort and check adjacent elements
	std::sort( consumed.begin(), consumed.end() );
	for ( std::size_t i = 1; i < consumed.size(); ++i )
	{
		EXPECT_NE( consumed[ i ], consumed[ i - 1 ] ) << "duplicate at index " << i;
	}
}

TEST_F( MPSCQueueMT, ProducerConsumerWithScalarPopOnly )
{
	constexpr std::size_t CAPACITY   = 512;
	constexpr int PRODUCERS  = 2;
	constexpr int PER_THREAD = 200;

	MPSCQueue< int, CAPACITY > q;
	std::atomic< int > consumed{ 0 };
	std::atomic< bool > stop{ false };

	auto producer = [&]( int base )
	{
		for ( int i = 0; i < PER_THREAD; ++i )
		{
			while ( ! q.push( base + i ) )
			{
				std::this_thread::yield();
			}
		}
	};

	auto consumer = std::thread( [&]()
	{
		int val;
		while ( ! stop.load( std::memory_order_acquire ) || ! q.empty() )
		{
			if ( q.pop( val ) )
			{
				consumed.fetch_add( 1, std::memory_order_relaxed );
			}
			else
			{
				std::this_thread::yield();
			}
		}
	} );

	std::vector< std::thread > threads;
	for ( int t = 0; t < PRODUCERS; ++t )
	{
		threads.emplace_back( producer, t * 100000 );
	}
	for ( auto& t : threads )
	{
		t.join();
	}

	stop.store( true, std::memory_order_release );
	consumer.join();

	// drain residual
	int val;
	while ( q.pop( val ) )
	{
		consumed.fetch_add( 1, std::memory_order_relaxed );
	}

	EXPECT_EQ( consumed.load(), PRODUCERS * PER_THREAD );
}
