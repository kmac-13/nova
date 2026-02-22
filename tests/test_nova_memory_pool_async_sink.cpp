// test_nova_memory_pool_async_sink.cpp
// Comprehensive tests for MemoryPool and MemoryPoolAsyncSink

#include "kmac/nova/extras/memory_pool.h"
#include "kmac/nova/extras/memory_pool_async_sink.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

// ============================================================================
// Helper: Capture Sink
// ============================================================================

class CaptureSink : public kmac::nova::Sink
{
private:
	mutable std::mutex _mutex;
	std::vector< std::string > _messages;

public:
	void process( const kmac::nova::Record& record ) override
	{
		std::lock_guard< std::mutex > lock( _mutex );
		_messages.emplace_back( record.message, record.messageSize );
	}

	std::vector< std::string > getMessages()
	{
		std::lock_guard< std::mutex > lock( _mutex );
		return _messages;
	}

	std::size_t count() const
	{
		std::lock_guard< std::mutex > lock( _mutex );
		return _messages.size();
	}

	void clear()
	{
		std::lock_guard< std::mutex > lock( _mutex );
		_messages.clear();
	}
};

// ============================================================================
// MemoryPool Tests
// ============================================================================

TEST( MemoryPoolTest, BasicAllocation )
{
	kmac::nova::extras::MemoryPool< 4096 > pool;

	uint8_t* ptr1 = pool.allocate( 100 );
	ASSERT_NE( ptr1, nullptr );
	ASSERT_EQ( pool.pointerToOffset( ptr1 ), 0 );

	uint8_t* ptr2 = pool.allocate( 200 );
	ASSERT_NE( ptr2, nullptr );
	// 100 aligned to 104 (8-byte boundary)
	ASSERT_EQ( pool.pointerToOffset( ptr2 ), 104 );
}

TEST( MemoryPoolTest, Alignment )
{
	kmac::nova::extras::MemoryPool< 4096 > pool;

	// allocate odd size
	uint8_t* ptr1 = pool.allocate( 99 );
	ASSERT_NE( ptr1, nullptr );

	// next allocation should be aligned
	uint8_t* ptr2 = pool.allocate( 50 );
	ASSERT_NE( ptr2, nullptr );

	// 99 rounded up to 104 (8-byte boundary)
	std::size_t diff = ptr2 - ptr1;
	ASSERT_EQ( diff % 8, 0 );
	ASSERT_EQ( diff, 104 );
}

TEST( MemoryPoolTest, PoolFull )
{
	kmac::nova::extras::MemoryPool< 1024 > pool;

	// allocate most of pool
	uint8_t* ptr1 = pool.allocate( 900 );
	ASSERT_NE( ptr1, nullptr );

	// try to allocate more than remaining
	uint8_t* ptr2 = pool.allocate( 200 );
	ASSERT_EQ( ptr2, nullptr ); // should fail
}

TEST( MemoryPoolTest, WrapAround )
{
	kmac::nova::extras::MemoryPool< 1024 > pool;

	// fill most of pool
	uint8_t* ptr1 = pool.allocate( 900 );
	ASSERT_NE( ptr1, nullptr );

	// release the first allocation to free space at the start
	pool.release( 900 );

	// this should trigger wrap-around
	// remaining space: 1024 - 904 (900 aligned) = 120 bytes
	// request: 200 bytes (won't fit)
	// should wrap and allocate from start
	uint8_t* ptr2 = pool.allocate( 200 );
	ASSERT_NE( ptr2, nullptr );
	ASSERT_EQ( pool.pointerToOffset( ptr2 ), 0 ); // wrapped to start
}

TEST( MemoryPoolTest, AllocateAndRelease )
{
	kmac::nova::extras::MemoryPool< 4096 > pool;

	// allocate
	uint8_t* ptr1 = pool.allocate( 100 );
	ASSERT_NE( ptr1, nullptr );

	// pool should have less available
	std::size_t available1 = pool.available();
	ASSERT_LT( available1, 4096 );

	// release
	pool.release( 100 );

	// pool should have more available
	std::size_t available2 = pool.available();
	ASSERT_GT( available2, available1 );
}

TEST( MemoryPoolTest, StackAllocation )
{
	// test stack-allocated pool
	kmac::nova::extras::MemoryPool< 16 * 1024, kmac::nova::extras::PoolAllocator::Stack > stackPool;

	uint8_t* ptr = stackPool.allocate( 100 );
	ASSERT_NE( ptr, nullptr );

	// verify it's actually on the stack (approximate check)
	// stack memory should be in a different range than heap
	// this is platform-dependent, so just verify it works
	stackPool.release( 100 );
}

TEST( MemoryPoolTest, OffsetPointerConversion )
{
	kmac::nova::extras::MemoryPool< 4096 > pool;

	// allocate some entries
	uint8_t* ptr1 = pool.allocate( 100 );
	uint8_t* ptr2 = pool.allocate( 200 );
	uint8_t* ptr3 = pool.allocate( 300 );

	ASSERT_NE( ptr1, nullptr );
	ASSERT_NE( ptr2, nullptr );
	ASSERT_NE( ptr3, nullptr );

	// convert pointers to offsets
	std::size_t offset1 = pool.pointerToOffset( ptr1 );
	std::size_t offset2 = pool.pointerToOffset( ptr2 );
	std::size_t offset3 = pool.pointerToOffset( ptr3 );

	// verify offsets are sensible
	ASSERT_EQ( offset1, 0 );         // first allocation at start
	ASSERT_EQ( offset2, 104 );       // 100 aligned to 104
	ASSERT_EQ( offset3, 104 + 200 ); // 200 bytes after ptr2

	// convert offsets back to pointers
	uint8_t* recovered1 = pool.offsetToPointer( offset1 );
	uint8_t* recovered2 = pool.offsetToPointer( offset2 );
	uint8_t* recovered3 = pool.offsetToPointer( offset3 );

	// verify round-trip conversion
	ASSERT_EQ( recovered1, ptr1 );
	ASSERT_EQ( recovered2, ptr2 );
	ASSERT_EQ( recovered3, ptr3 );

	// verify we can write to and read from converted pointers
	*recovered1 = 42;
	*recovered2 = 84;
	*recovered3 = 126;

	ASSERT_EQ( *ptr1, 42 );
	ASSERT_EQ( *ptr2, 84 );
	ASSERT_EQ( *ptr3, 126 );
}

// ============================================================================
// EntryIndex Tests
// ============================================================================

TEST( EntryIndexTest, SizeCheck )
{
	// Verify sizes for different offset types
	ASSERT_EQ( sizeof( kmac::nova::extras::EntryIndex< uint16_t > ), 2 );
	ASSERT_EQ( sizeof( kmac::nova::extras::EntryIndex< uint32_t > ), 4 );
	ASSERT_EQ( sizeof( kmac::nova::extras::EntryIndex< uint64_t > ), 8 );
}

// ============================================================================
// MemoryPoolAsyncSink Tests
// ============================================================================

TEST( MemoryPoolAsyncSinkTest, BasicLogging )
{
	CaptureSink capture;
	kmac::nova::extras::MemoryPoolAsyncSink<> sink( capture );

	// create a simple record
	kmac::nova::Record record{};
	record.tag = "INFO";
	record.tagId = 1;
	record.file = "test.cpp";
	record.function = "test_function";
	record.line = 42;
	record.timestamp = 12345;

	std::string msg = "Hello, World!";
	record.message = msg.c_str();
	record.messageSize = msg.size();

	// process record
	sink.process( record );

	// wait for async processing
	std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );

	// verify
	auto messages = capture.getMessages();
	ASSERT_EQ( messages.size(), 1 );
	ASSERT_EQ( messages[ 0 ], "Hello, World!" );
	ASSERT_EQ( sink.processedCount(), 1 );
	ASSERT_EQ( sink.droppedCount(), 0 );
}

TEST( MemoryPoolAsyncSinkTest, MultipleMessages )
{
	CaptureSink capture;
	kmac::nova::extras::MemoryPoolAsyncSink<> sink( capture );

	// log 100 messages
	for ( int i = 0; i < 100; ++i )
	{
		kmac::nova::Record record{};
		record.tag = "INFO";
		record.tagId = 1;
		record.file = "test.cpp";
		record.function = "test_function";
		record.line = 42;
		record.timestamp = i;

		std::string msg = "Message " + std::to_string( i );
		record.message = msg.c_str();
		record.messageSize = msg.size();

		sink.process( record );
	}

	// wait for processing
	std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

	// verify all messages received
	auto messages = capture.getMessages();
	ASSERT_EQ( messages.size(), 100 );

	for ( int i = 0; i < 100; ++i )
	{
		ASSERT_EQ( messages[ i ], "Message " + std::to_string( i ) );
	}

	ASSERT_EQ( sink.processedCount(), 100 );
	ASSERT_EQ( sink.droppedCount(), 0 );
}

TEST( MemoryPoolAsyncSinkTest, VariableMessageSizes )
{
	CaptureSink capture;
	kmac::nova::extras::MemoryPoolAsyncSink<> sink( capture );

	// lg messages of varying sizes
	std::vector< std::string > testMessages {
		"A",
		"Short message",
		"Medium length message here",
		std::string( 500, 'X' ), // large message
		"",                      // empty message
		std::string( 1000, 'Y' ) // very large message
	};

	for ( const auto& msg : testMessages )
	{
		kmac::nova::Record record{};
		record.tag = "INFO";
		record.tagId = 1;
		record.file = "test.cpp";
		record.function = "test_function";
		record.line = 42;
		record.timestamp = 0;
		record.message = msg.c_str();
		record.messageSize = msg.size();

		sink.process( record );
	}

	// wait for processing
	std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

	// verify
	auto messages = capture.getMessages();
	ASSERT_EQ( messages.size(), testMessages.size() );

	for ( std::size_t i = 0; i < testMessages.size(); ++i )
	{
		ASSERT_EQ( messages[ i ], testMessages[ i ] );
	}
}

TEST( MemoryPoolAsyncSinkTest, NoMessageCorruption )
{
	CaptureSink capture;
	kmac::nova::extras::MemoryPoolAsyncSink<> sink( capture );

	// log messages, then immediately invalidate the source buffer
	// this tests that the message is copied into the pool
	for ( int i = 0; i < 50; ++i )
	{
		char buffer[ 256 ];
		std::snprintf( buffer, sizeof( buffer ), "Message %d with data", i );

		kmac::nova::Record record{};
		record.tag = "INFO";
		record.tagId = 1;
		record.file = "test.cpp";
		record.function = "test_function";
		record.line = 42;
		record.timestamp = i;
		record.message = buffer;
		record.messageSize = std::strlen( buffer );

		sink.process( record );

		// immediately overwrite buffer (simulating stack reuse)
		std::memset( buffer, 'Z', sizeof( buffer ) );
	}

	// wait for processing
	std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

	// verify no corruption
	auto messages = capture.getMessages();
	ASSERT_EQ( messages.size(), 50 );

	for ( int i = 0; i < 50; ++i )
	{
		std::string expected = "Message " + std::to_string( i ) + " with data";
		ASSERT_EQ( messages[ i ], expected ) << "Message " << i << " was corrupted";
	}
}

TEST( MemoryPoolAsyncSinkTest, ConcurrentProducers )
{
	CaptureSink capture;
	kmac::nova::extras::MemoryPoolAsyncSink< 2 * 1024 * 1024, 16384 > sink( capture );

	constexpr int NUM_THREADS = 4;
	constexpr int MSGS_PER_THREAD = 100;

	std::vector< std::thread > threads;

	// launch multiple producer threads
	for ( int t = 0; t < NUM_THREADS; ++t )
	{
		threads.emplace_back( [ &sink, t ]() {
			for ( int i = 0; i < MSGS_PER_THREAD; ++i )
			{
				kmac::nova::Record record{};
				record.tag = "INFO";
				record.tagId = 1;
				record.file = "test.cpp";
				record.function = "test_function";
				record.line = 42;
				record.timestamp = t * 1000 + i;

				std::string msg = "Thread " + std::to_string( t ) + " Message " + std::to_string( i );
				record.message = msg.c_str();
				record.messageSize = msg.size();

				sink.process( record );
			}
		} );
	}

	// wait for all threads
	for ( auto& thread : threads )
	{
		thread.join();
	}

	// wait for processing - actively wait until queue is drained
	constexpr int MAX_WAIT_MS = 2000;
	constexpr int POLL_INTERVAL_MS = 10;
	int waited = 0;

	while ( waited < MAX_WAIT_MS )
	{
		std::this_thread::sleep_for( std::chrono::milliseconds( POLL_INTERVAL_MS ) );
		waited += POLL_INTERVAL_MS;

		// check if processing is complete
		if ( sink.queueSize() == 0 && sink.processedCount() >= NUM_THREADS * MSGS_PER_THREAD )
		{
			break;
		}
	}

	// verify total count
	auto messages = capture.getMessages();

	// check if any messages were dropped
	std::size_t dropped = sink.droppedCount();
	std::size_t processed = sink.processedCount();
	std::size_t received = messages.size();

	// for debugging if test fails
	if ( received != NUM_THREADS * MSGS_PER_THREAD || dropped > 0 )
	{
		std::cout << "Expected: " << NUM_THREADS * MSGS_PER_THREAD << " messages\n";
		std::cout << "Received: " << received << " messages\n";
		std::cout << "Processed: " << processed << " messages\n";
		std::cout << "Dropped: " << dropped << " messages\n";
		std::cout << "Queue size: " << sink.queueSize() << "\n";
		std::cout << "Pool available: " << sink.poolAvailable() << " bytes\n";
		std::cout << "Pool used: " << sink.poolUsed() << " bytes\n";
	}

	ASSERT_EQ( received, NUM_THREADS * MSGS_PER_THREAD );
	ASSERT_EQ( processed, NUM_THREADS * MSGS_PER_THREAD );
	ASSERT_EQ( dropped, 0 );
}

TEST( MemoryPoolAsyncSinkTest, SmallPool_DropMessages )
{
	CaptureSink capture;
	// very small pool that will fill quickly
	kmac::nova::extras::MemoryPoolAsyncSink< 4096, 128, uint16_t > sink( capture );

	// log many large messages to fill pool
	for ( int i = 0; i < 100; ++i )
	{
		kmac::nova::Record record{};
		record.tag = "INFO";
		record.tagId = 1;
		record.file = "test.cpp";
		record.function = "test_function";
		record.line = 42;
		record.timestamp = i;

		std::string msg = std::string( 500, 'X' ) + std::to_string( i );
		record.message = msg.c_str();
		record.messageSize = msg.size();

		sink.process( record );
	}

	// wait for processing
	std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

	// some messages should be dropped due to small pool
	ASSERT_GT( sink.droppedCount(), 0 );
	ASSERT_LT( capture.count(), 100 );
}

TEST( MemoryPoolAsyncSinkTest, StackAllocatedPool )
{
	CaptureSink capture;
	// use stack-allocated pool
	kmac::nova::extras::MemoryPoolAsyncSink< 64 * 1024, 512, uint64_t, kmac::nova::extras::PoolAllocator::Stack > sink( capture );

	// log some messages
	for ( int i = 0; i < 50; ++i )
	{
		kmac::nova::Record record{};
		record.tag = "INFO";
		record.tagId = 1;
		record.file = "test.cpp";
		record.function = "test_function";
		record.line = 42;
		record.timestamp = i;

		std::string msg = "Message " + std::to_string( i );
		record.message = msg.c_str();
		record.messageSize = msg.size();

		sink.process( record );
	}

	// wait for processing
	std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );

	// Verify
	auto messages = capture.getMessages();
	ASSERT_EQ( messages.size(), 50 );
	ASSERT_EQ( sink.droppedCount(), 0 );
}

TEST( MemoryPoolAsyncSinkTest, PoolMetrics )
{
	CaptureSink capture;
	kmac::nova::extras::MemoryPoolAsyncSink< 64 * 1024 > sink( capture );

	// check initial state
	ASSERT_EQ( sink.poolCapacity(), 64 * 1024 );
	ASSERT_EQ( sink.indexCapacity(), 8192 );
	ASSERT_GT( sink.poolAvailable(), 60 * 1024 ); // should be nearly full
	ASSERT_LT( sink.poolUsed(), 4 * 1024 );       // should be nearly empty

	// log a message
	kmac::nova::Record record{};
	record.tag = "INFO";
	record.tagId = 1;
	record.file = "test.cpp";
	record.function = "test_function";
	record.line = 42;
	record.timestamp = 0;

	std::string msg = "Test message";
	record.message = msg.c_str();
	record.messageSize = msg.size();

	sink.process( record );

	// queue size should be greater than 0 immediately after enqueuing
	// (check before background thread has time to process)
	std::size_t queueSizeAfterEnqueue = sink.queueSize();
	ASSERT_GT( queueSizeAfterEnqueue, 0 );

	// wait for processing
	std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );

	// after waiting, message should be processed and queue should be empty
	ASSERT_EQ( sink.queueSize(), 0 );
	ASSERT_EQ( sink.processedCount(), 1 );
}

// ============================================================================
// Main
// ============================================================================

int main( int argc, char** argv )
{
	::testing::InitGoogleTest( &argc, argv );
	return RUN_ALL_TESTS();
}
