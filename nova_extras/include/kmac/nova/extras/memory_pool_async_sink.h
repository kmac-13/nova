#pragma once
#ifndef KMAC_NOVA_EXTRAS_MEMORY_POOL_ASYNC_SINK_H
#define KMAC_NOVA_EXTRAS_MEMORY_POOL_ASYNC_SINK_H

#include "memory_pool.h"
#include "mpsc_queue.h"

#include "kmac/nova/record.h"
#include "kmac/nova/sink.h"

#include <atomic>
#include <cstring>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>
#include <type_traits>

namespace kmac::nova::extras
{

/**
 * @brief Index entry for locating records in the memory pool.
 *
 * Stores only the offset into the pool. The Record at that offset contains
 * the messageSize field, which allows calculating the total entry size.
 *
 * @tparam OffsetType type for offset storage (uint16_t, uint32_t, uint64_t)
 */
template< typename OffsetType = uint32_t >
struct EntryIndex
{
	static_assert( std::is_unsigned_v< OffsetType >, "OffsetType must be unsigned" );
	static_assert( std::is_integral_v< OffsetType >, "OffsetType must be integral" );

	OffsetType offset; ///< offset into memory pool where Record begins
};

/**
 * @brief Async sink using memory pool for safe message storage.
 *
 * ✅ SAFE FOR SAFETY-CRITICAL SYSTEMS
 *
 * MemoryPoolAsyncSink allocates space in a memory pool for both the Record
 * metadata AND the message data, eliminating dangling pointer issues.
 *
 * Unlike unsafe designs that only copy the Record struct (leaving message
 * pointers dangling), this implementation ensures all data remains valid
 * until processed.
 *
 * Memory pool layout:
 * [Record1 (64B)][Message1 (N1 bytes)][Record2 (64B)][Message2 (N2 bytes)]...
 *
 * The Record.message pointer is updated to point within the pool, so it
 * remains valid until the background thread processes and releases the entry.
 *
 * Thread safety:
 * - multiple producers: lock-free allocation from pool
 * - single consumer: background thread processes entries
 * - no dangling pointers: message data lives in pool
 *
 * Memory usage:
 * - Pool: Configurable (default 256KB, stores Record + message data)
 * - Index queue: 4 bytes/entry * queue capacity
 * - Example: 256KB pool + 8192 entries = ~288KB total
 *
 * Trade-offs:
 * - pros: no dangling pointers, zero heap allocation, safe for async logging
 * - cons: higher memory usage (Record + message), slightly more complex, potential wrap overhead
 *
 * Use when:
 * - need async logging without dangling pointer risk
 * - zero heap allocation required (safety-critical, real-time)
 * - can afford ~1MB memory for pool
 * - message loss acceptable (if pool/queue fills)
 *
 * Avoid when:
 * - memory extremely constrained (<1MB available)
 * - guaranteed delivery required (use SynchronizedSink)
 * - synchronous logging acceptable (use SpinlockSink)
 *
 * @tparam PoolSize size of memory pool in bytes (must be power of 2)
 * @tparam IndexQueueCapacity max number of pending entries in index queue
 * @tparam IndexType type for pool offsets (uint16_t/uint32_t/uint64_t)
 * @tparam Allocator pool allocation strategy (Heap or Stack)
 */
template<
	std::size_t PoolSize = 256UL * 1024UL,
	std::size_t IndexQueueCapacity = 8192UL,
	typename IndexType = uint32_t,
	PoolAllocator Allocator = PoolAllocator::Heap
>
class MemoryPoolAsyncSink final : public kmac::nova::Sink
{
	// ensure pool size doesn't exceed what IndexType can represent
	static_assert( PoolSize <= std::numeric_limits< IndexType >::max(),
		"PoolSize exceeds maximum value representable by IndexType" );

private:
	MemoryPool< PoolSize, Allocator > _pool;
	MPSCQueue< EntryIndex< IndexType >, IndexQueueCapacity > _indexQueue{ };

	kmac::nova::Sink* _downstream = nullptr;
	std::atomic< bool > _shutdown{ false };
	std::atomic< std::size_t > _dropped{ 0 };
	std::atomic< std::size_t > _processed{ 0 };

	std::mutex _mutex;
	std::condition_variable _cv;
	std::thread _workerThread;

public:
	/**
	 * @brief Get pool capacity in bytes.
	 *
	 * @return Pool capacity
	 */
	static constexpr std::size_t poolCapacity() noexcept;

	/**
	 * @brief Get index queue capacity.
	 *
	 * @return Index queue capacity
	 */
	static constexpr std::size_t indexCapacity() noexcept;

	/**
	 * @brief Construct async sink with memory pool.
	 *
	 * Starts a background thread that waits on condition variable for entries.
	 * The pool is allocated according to the Allocator template parameter.
	 *
	 * @param downstream sink to forward records to (must outlive this sink)
	 */
	explicit MemoryPoolAsyncSink( kmac::nova::Sink& downstream ) noexcept;

	/**
	 * @brief Destructor - shuts down background thread.
	 *
	 * Signals shutdown and waits for all queued records to be processed.
	 * Remaining entries in the pool are processed before thread exits.
	 */
	~MemoryPoolAsyncSink() noexcept override;

	/**
	 * @brief Process a record by copying it and its message into the pool.
	 *
	 * Steps:
	 * 1. calculate total entry size (sizeof(Record) + messageSize)
	 * 2. allocate space from pool (lock-free atomic operation)
	 * 3. copy Record struct into pool
	 * 4. copy message data into pool (immediately after Record)
	 * 5. update Record.message to point to copied message in pool
	 * 6. enqueue index to this entry (lock-free push)
	 * 7. notify worker thread if queue was empty
	 *
	 * Thread-safe for multiple producers. Non-blocking unless pool is full.
	 *
	 * If pool or index queue is full, the message is dropped and the dropped
	 * counter is incremented.
	 *
	 * @param record the record to process
	 */
	void process( const kmac::nova::Record& record ) noexcept override;

	/**
	 * @brief Get number of dropped messages.
	 *
	 * Messages are dropped when pool or index queue is full.
	 * The total number of messages passed through process is processedCount() + droppedCount().
	 *
	 * @return dropped message count
	 */
	std::size_t droppedCount() const noexcept;

	/**
	 * @brief Get number of processed messages, i.e. those that ended up in the queue.
	 *
	 * The total number of messages passed through process is processedCount() + droppedCount().
	 *
	 * @return processed message count
	 */
	std::size_t processedCount() const noexcept;

	/**
	 * @brief Get approximate number of entries in index queue.
	 *
	 * @return queue size
	 */
	std::size_t queueSize() const noexcept;

	/**
	 * @brief Get approximate bytes available in pool.
	 *
	 * @return available bytes
	 */
	std::size_t poolAvailable() const noexcept;

	/**
	 * @brief Get approximate bytes in use in pool.
	 *
	 * @return used bytes
	 */
	std::size_t poolUsed() const noexcept;

private:
	/**
	 * @brief Background thread processing loop.
	 *
	 * Waits on condition variable when queue is empty.
	 * Processes entries in batches for efficiency.
	 *
	 * Loop:
	 * 1. pop batch of indices from queue
	 * 2. for each index:
	 *    a. get Record pointer from pool
	 *    b. process record (message pointer is valid)
	 *    c. calculate entry size from Record.messageSize
	 *    d. release pool space
	 * 3. update processed counter
	 * 4. if shutdown and queue empty, exit
	 * 5. if no entries, wait on condition variable
	 */
	void processLoop() noexcept;
};

template< std::size_t PoolSize, std::size_t IndexQueueCapacity, typename IndexType, PoolAllocator Allocator >
constexpr std::size_t MemoryPoolAsyncSink< PoolSize, IndexQueueCapacity, IndexType, Allocator >::poolCapacity() noexcept
{
	return PoolSize;
}

template< std::size_t PoolSize, std::size_t IndexQueueCapacity, typename IndexType, PoolAllocator Allocator >
constexpr std::size_t MemoryPoolAsyncSink< PoolSize, IndexQueueCapacity, IndexType, Allocator >::indexCapacity() noexcept
{
	return IndexQueueCapacity;
}

template< std::size_t PoolSize, std::size_t IndexQueueCapacity, typename IndexType, PoolAllocator Allocator >
MemoryPoolAsyncSink< PoolSize, IndexQueueCapacity, IndexType, Allocator >::MemoryPoolAsyncSink( kmac::nova::Sink& downstream ) noexcept
	: _downstream( &downstream )
	, _shutdown( false )
	, _dropped( 0 )
	, _processed( 0 )
	, _workerThread( [ this ]() { processLoop(); } )
{
}

template< std::size_t PoolSize, std::size_t IndexQueueCapacity, typename IndexType, PoolAllocator Allocator >
MemoryPoolAsyncSink< PoolSize, IndexQueueCapacity, IndexType, Allocator >::~MemoryPoolAsyncSink() noexcept
{
	// signal shutdown
	{
		std::lock_guard< std::mutex > lock( _mutex );
		_shutdown.store( true, std::memory_order_release );
		_cv.notify_one();
	}

	// wait for worker thread to finish
	if ( _workerThread.joinable() )
	{
		_workerThread.join();
	}
}

template< std::size_t PoolSize, std::size_t IndexQueueCapacity, typename IndexType, PoolAllocator Allocator >
void MemoryPoolAsyncSink< PoolSize, IndexQueueCapacity, IndexType, Allocator >::process( const kmac::nova::Record& record ) noexcept
{
	// calculate space needed: Record struct + message data
	std::size_t entrySize = sizeof( kmac::nova::Record ) + record.messageSize;

	// allocate from pool (lock-free)
	uint8_t* entryPtr = _pool.allocate( entrySize );
	if ( entryPtr == nullptr )
	{
		// pool is full, so drop the record/message
		_dropped.fetch_add( 1, std::memory_order_relaxed );
		return;
	}

	// copy Record struct to pool
	// NOLINT NOTE: placement of trivially-copyable Record into raw pool buffer; well-defined for standard-layout types, no C++17 alternative
	kmac::nova::Record* storedRecord = reinterpret_cast< kmac::nova::Record* >( entryPtr );  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
	std::memcpy( storedRecord, &record, sizeof( kmac::nova::Record ) );

	// copy message data immediately after Record
	// NOLINT NOTE: pointer into pool buffer after Record storage; well-defined for standard-layout types
	char* messagePtr = reinterpret_cast< char* >( entryPtr + sizeof( kmac::nova::Record ) );  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
	std::memcpy( messagePtr, record.message, record.messageSize );

	// update Record's message pointer to point to copied data in pool
	storedRecord->message = messagePtr;

	// calculate offset from pool base
	std::size_t offset = _pool.pointerToOffset( entryPtr );

	// enqueue the index (lock-free push)
	EntryIndex< IndexType > index{ static_cast< IndexType >( offset ) };
	if ( ! _indexQueue.push( index ) )
	{
		// index queue is full, so drop the record/message
		// (this shouldn't happen if queue is sized correctly relative to pool)
		_dropped.fetch_add( 1, std::memory_order_relaxed );

		// release the pool space we allocated
		_pool.release( entrySize );
		return;
	}

	// notify worker thread
	{
		std::lock_guard< std::mutex > lock( _mutex );
		_cv.notify_one();
	}
}

template< std::size_t PoolSize, std::size_t IndexQueueCapacity, typename IndexType, PoolAllocator Allocator >
std::size_t MemoryPoolAsyncSink< PoolSize, IndexQueueCapacity, IndexType, Allocator >::droppedCount() const noexcept
{
	return _dropped.load( std::memory_order_relaxed );
}

template< std::size_t PoolSize, std::size_t IndexQueueCapacity, typename IndexType, PoolAllocator Allocator >
std::size_t MemoryPoolAsyncSink< PoolSize, IndexQueueCapacity, IndexType, Allocator >::processedCount() const noexcept
{
	return _processed.load( std::memory_order_relaxed );
}

template< std::size_t PoolSize, std::size_t IndexQueueCapacity, typename IndexType, PoolAllocator Allocator >
std::size_t MemoryPoolAsyncSink< PoolSize, IndexQueueCapacity, IndexType, Allocator >::queueSize() const noexcept
{
	return _indexQueue.size();
}

template< std::size_t PoolSize, std::size_t IndexQueueCapacity, typename IndexType, PoolAllocator Allocator >
std::size_t MemoryPoolAsyncSink< PoolSize, IndexQueueCapacity, IndexType, Allocator >::poolAvailable() const noexcept
{
	return _pool.available();
}

template< std::size_t PoolSize, std::size_t IndexQueueCapacity, typename IndexType, PoolAllocator Allocator >
std::size_t MemoryPoolAsyncSink< PoolSize, IndexQueueCapacity, IndexType, Allocator >::poolUsed() const noexcept
{
	return _pool.used();
}

template< std::size_t PoolSize, std::size_t IndexQueueCapacity, typename IndexType, PoolAllocator Allocator >
void MemoryPoolAsyncSink< PoolSize, IndexQueueCapacity, IndexType, Allocator >::processLoop() noexcept
{
	constexpr std::size_t BATCH_SIZE = 64;
	std::array< EntryIndex< IndexType >, BATCH_SIZE > indexBatch{};

	while ( true )
	{
		// check shutdown flag
		bool shutdown = _shutdown.load( std::memory_order_acquire );

		// dequeue batch of indices (lock-free pop)
		std::size_t batchSize = _indexQueue.popBatch( indexBatch.data(), BATCH_SIZE );

		// process each entry in batch
		for ( std::size_t i = 0; i < batchSize; ++i )
		{
			// get pointer to Record in pool
			uint8_t* entryPtr = _pool.offsetToPointer( indexBatch[ i ].offset );
			// NOLINT NOTE: placement of trivially-copyable Record into raw pool buffer; well-defined for standard-layout types, no C++17 alternative
			kmac::nova::Record* record = reinterpret_cast< kmac::nova::Record* >( entryPtr );  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)

			// process the record (message pointer is valid - points into pool)
			_downstream->process( *record );

			// calculate entry size from Record
			// total size = Record struct + message data
			std::size_t entrySize = sizeof( kmac::nova::Record ) + record->messageSize;

			// release pool space (advances read offset)
			_pool.release( entrySize );
		}

		// update processed count
		_processed.fetch_add( batchSize, std::memory_order_relaxed );

		// exit if shutting down and queue is empty
		if ( shutdown && batchSize == 0 )
		{
			break;
		}

		// wait for more entries if we got nothing
		if ( batchSize == 0 )
		{
			std::unique_lock< std::mutex > lock( _mutex );

			// wait until queue has data or shutdown signaled
			_cv.wait( lock, [ this ]() {
				return !_indexQueue.empty() || _shutdown.load( std::memory_order_acquire );
			} );
		}
	}
}

} // namespace kmac::nova::extras

#endif // KMAC_NOVA_EXTRAS_MEMORY_POOL_ASYNC_SINK_H
