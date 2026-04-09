#pragma once
#ifndef KMAC_NOVA_EXTRAS_MEMORY_POOL_ASYNC_BATCHED_SINK_H
#define KMAC_NOVA_EXTRAS_MEMORY_POOL_ASYNC_BATCHED_SINK_H

#include "buffer.h"
#include "formatter.h"
#include "memory_pool.h"
#include "mpsc_queue.h"

#include <kmac/nova/record.h>
#include <kmac/nova/sink.h>
#include <kmac/nova/platform/array.h>

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <mutex>
#include <thread>

namespace kmac::nova::extras
{

/**
 * @brief Asynchronous sink with batch formatting using Formatter interface.
 *
 * ✅ SAFE FOR SAFETY-CRITICAL SYSTEMS
 *
 * Architecture:
 * - producer threads: store raw Record + message in memory pool (fast path)
 * - consumer thread: dequeues batches, formats using Formatter, sends to downstream
 *
 * This eliminates formatting overhead from the hot path and allows batch processing
 * for better cache locality and reduced downstream calls.
 *
 * @tparam PoolSize size of memory pool in bytes
 * @tparam IndexQueueCapacity max pending entries
 * @tparam IndexType type for pool offsets
 * @tparam Allocator pool allocation strategy
 */
template<
	std::size_t PoolSize = 1024UL * 1024UL,
	std::size_t IndexQueueCapacity = 8192UL,
	typename IndexType = uint32_t,
	PoolAllocator Allocator = PoolAllocator::Heap
>
class MemoryPoolAsyncBatchSink final : public kmac::nova::Sink
{
	static_assert( PoolSize <= std::numeric_limits< IndexType >::max(),
	    "PoolSize exceeds maximum value representable by IndexType" );

private:
	// entry in the index queue
	struct EntryIndex
	{
		IndexType offset;
	};

	// memory pool for storing Record + message data
	MemoryPool< PoolSize, Allocator > _pool;

	// queue of offsets into the pool
	MPSCQueue< EntryIndex, IndexQueueCapacity > _indexQueue;

	// downstream sink (typically RollingFileSink)
	kmac::nova::Sink* _downstream = nullptr;

	// formatter for records (must remain valid for sink's lifetime)
	Formatter* _formatter = nullptr;

	// fixed-size formatting buffer (256KB for batch formatting)
	static constexpr std::size_t FORMAT_BUFFER_SIZE = 256UL * 1024UL;
	kmac::nova::platform::Array< char, FORMAT_BUFFER_SIZE > _formatBuffer { };
	std::size_t _formatOffset = 0;

	// consumer thread
	std::thread _consumerThread;
	std::atomic< bool > _shutdown{ false };

	// notification
	std::mutex _mutex;
	std::condition_variable _cv;

	// statistics
	std::atomic< std::size_t > _dropped{ 0 };
	std::atomic< std::size_t > _processed{ 0 };

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
	 * @brief Construct async sink with batch formatting.
	 *
	 * @param downstream sink to receive formatted batches (typically RollingFileSink)
	 * @param formatter formatter object (must remain valid for sink's lifetime)
	 */
	explicit MemoryPoolAsyncBatchSink( kmac::nova::Sink& downstream, Formatter* formatter ) noexcept;

	~MemoryPoolAsyncBatchSink() noexcept override;

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

	/**
	 * @brief Get approximate number of entries in index queue.
	 *
	 * @return queue size
	 */
	std::size_t queueSize() const noexcept;

	/**
	 * @brief Get number of processed messages, i.e. those that ended up in the queue.
	 *
	 * The total number of messages passed through process is processedCount() + droppedCount().
	 *
	 * @return processed message count
	 */
	std::size_t processedCount() const noexcept;

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
	 * @brief Process a record (producer hot path).
	 *
	 * Stores raw record in pool - NO formatting on hot path.
	 */
	void process( const kmac::nova::Record& record ) noexcept override;

private:
	/**
	 * @brief Consumer thread - dequeues and formats batches.
	 */
	void processLoop() noexcept;

	/**
	 * @brief Format batch of records using streaming Formatter interface.
	 *
	 * Uses Formatter::begin() and Formatter::format() to handle large records
	 * that may not fit in the buffer all at once.
	 */
	void formatBatch( kmac::nova::Record* const* records, std::size_t count ) noexcept;

	/**
	 * @brief Send formatted buffer to downstream sink.
	 *
	 * Creates a Record with the formatted data and sends to downstream.
	 * The downstream RollingFileSink will write this directly without re-formatting.
	 */
	void flushFormatBuffer() noexcept;

	/**
	 * @brief Format a single record using the Formatter interface.
	 *
	 * Formats in chunks, flushing when the format buffer fills.
	 *
	 * @param record record to format
	 */
	void formatRecordWithFormatter( const kmac::nova::Record& record ) noexcept;

	/**
	 * @brief Copy raw message bytes from a record into the format buffer.
	 *
	 * Flushes before copying if insufficient space remains.
	 *
	 * @param record record whose message is to be copied
	 */
	void copyRawMessage( const kmac::nova::Record& record ) noexcept;
};

template< std::size_t PoolSize, std::size_t IndexQueueCapacity, typename IndexType, PoolAllocator Allocator >
constexpr std::size_t MemoryPoolAsyncBatchSink< PoolSize, IndexQueueCapacity, IndexType, Allocator >::poolCapacity() noexcept
{
	return PoolSize;
}

template< std::size_t PoolSize, std::size_t IndexQueueCapacity, typename IndexType, PoolAllocator Allocator >
constexpr std::size_t MemoryPoolAsyncBatchSink< PoolSize, IndexQueueCapacity, IndexType, Allocator >::indexCapacity() noexcept
{
	return IndexQueueCapacity;
}

template< std::size_t PoolSize, std::size_t IndexQueueCapacity, typename IndexType, PoolAllocator Allocator >
MemoryPoolAsyncBatchSink< PoolSize, IndexQueueCapacity, IndexType, Allocator >::MemoryPoolAsyncBatchSink(
	kmac::nova::Sink& downstream,
	Formatter* formatter
) noexcept
	: _downstream( &downstream )
	, _formatter( formatter )
{
	_consumerThread = std::thread( &MemoryPoolAsyncBatchSink::processLoop, this );
}

template< std::size_t PoolSize, std::size_t IndexQueueCapacity, typename IndexType, PoolAllocator Allocator >
MemoryPoolAsyncBatchSink< PoolSize, IndexQueueCapacity, IndexType, Allocator >::~MemoryPoolAsyncBatchSink() noexcept
{
	_shutdown.store( true, std::memory_order_release );
	_cv.notify_all();

	if ( _consumerThread.joinable() )
	{
		_consumerThread.join();
	}
}

template< std::size_t PoolSize, std::size_t IndexQueueCapacity, typename IndexType, PoolAllocator Allocator >
std::size_t MemoryPoolAsyncBatchSink< PoolSize, IndexQueueCapacity, IndexType, Allocator >::poolAvailable() const noexcept
{
	return _pool.available();
}

template< std::size_t PoolSize, std::size_t IndexQueueCapacity, typename IndexType, PoolAllocator Allocator >
std::size_t MemoryPoolAsyncBatchSink< PoolSize, IndexQueueCapacity, IndexType, Allocator >::poolUsed() const noexcept
{
	return _pool.used();
}

template< std::size_t PoolSize, std::size_t IndexQueueCapacity, typename IndexType, PoolAllocator Allocator >
std::size_t MemoryPoolAsyncBatchSink< PoolSize, IndexQueueCapacity, IndexType, Allocator >::queueSize() const noexcept
{
	return _indexQueue.size();
}

template< std::size_t PoolSize, std::size_t IndexQueueCapacity, typename IndexType, PoolAllocator Allocator >
std::size_t MemoryPoolAsyncBatchSink< PoolSize, IndexQueueCapacity, IndexType, Allocator >::processedCount() const noexcept
{
	return _processed.load( std::memory_order_relaxed );
}

template< std::size_t PoolSize, std::size_t IndexQueueCapacity, typename IndexType, PoolAllocator Allocator >
std::size_t MemoryPoolAsyncBatchSink< PoolSize, IndexQueueCapacity, IndexType, Allocator >::droppedCount() const noexcept
{
	return _dropped.load( std::memory_order_relaxed );
}

template< std::size_t PoolSize, std::size_t IndexQueueCapacity, typename IndexType, PoolAllocator Allocator >
void MemoryPoolAsyncBatchSink< PoolSize, IndexQueueCapacity, IndexType, Allocator >::process( const kmac::nova::Record& record ) noexcept
{
	// calculate entry size
	const std::size_t entrySize = sizeof( kmac::nova::Record ) + record.messageSize;

	// allocate from pool
	uint8_t* entryPtr = _pool.allocate( entrySize );
	if ( entryPtr == nullptr )
	{
		_dropped.fetch_add( 1, std::memory_order_relaxed );
		return;
	}

	// copy Record struct
	// NOLINT NOTE: placement of trivially-copyable Record into raw pool buffer; well-defined for standard-layout types, no C++17 alternative
	kmac::nova::Record* storedRecord = reinterpret_cast< kmac::nova::Record* >( entryPtr );  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
	*storedRecord = record;

	// copy message data
	std::memcpy( entryPtr + sizeof( kmac::nova::Record ), record.message, record.messageSize );

	// update message pointer to point into pool
	// NOLINT NOTE: pointer into pool buffer after Record storage; well-defined for standard-layout types
	storedRecord->message = reinterpret_cast< const char* >( entryPtr + sizeof( kmac::nova::Record ) );  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)

	// enqueue index
	EntryIndex entry;
	entry.offset = _pool.pointerToOffset( entryPtr );

	if ( ! _indexQueue.push( entry ) )
	{
		_pool.release( entrySize );
		_dropped.fetch_add( 1, std::memory_order_relaxed );
		return;
	}

	// always notify consumer (prevents deadlock in multi-threaded scenarios)
	{
		std::lock_guard< std::mutex > lock( _mutex );
		_cv.notify_one();
	}
}

template< std::size_t PoolSize, std::size_t IndexQueueCapacity, typename IndexType, PoolAllocator Allocator >
void MemoryPoolAsyncBatchSink< PoolSize, IndexQueueCapacity, IndexType, Allocator >::processLoop() noexcept
{
	constexpr std::size_t BATCH_SIZE = 64;
	kmac::nova::platform::Array< EntryIndex, BATCH_SIZE > indexBatch { };
	kmac::nova::platform::Array< kmac::nova::Record*, BATCH_SIZE > recordBatch { };

	while ( true )
	{
		bool shutdown = _shutdown.load( std::memory_order_acquire );

		// dequeue batch
		std::size_t batchSize = _indexQueue.popBatch( indexBatch.data(), BATCH_SIZE );

		if ( batchSize > 0 )
		{
			// get record pointers
			for ( std::size_t i = 0; i < batchSize; ++i )
			{
				uint8_t* entryPtr = _pool.offsetToPointer( indexBatch.data()[ i ].offset );
				// NOLINT NOTE: retrieving Record from pool buffer; same justification as process()
				recordBatch.data()[ i ] = reinterpret_cast< kmac::nova::Record* >( entryPtr );  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
			}

			// format and send batch
			formatBatch( recordBatch.data(), batchSize );

			// release pool entries
			for ( std::size_t i = 0; i < batchSize; ++i )
			{
				std::size_t entrySize = sizeof( kmac::nova::Record ) + recordBatch.data()[ i ]->messageSize;
				_pool.release( entrySize );
			}

			_processed.fetch_add( batchSize, std::memory_order_relaxed );
		}

		if ( shutdown && batchSize == 0 )
		{
			break;
		}

		if ( batchSize == 0 )
		{
			std::unique_lock< std::mutex > lock( _mutex );
			_cv.wait( lock, [ this ]() {
				return ! _indexQueue.empty() || _shutdown.load( std::memory_order_acquire );
			} );
		}
	}
}

template< std::size_t PoolSize, std::size_t IndexQueueCapacity, typename IndexType, PoolAllocator Allocator >
void MemoryPoolAsyncBatchSink< PoolSize, IndexQueueCapacity, IndexType, Allocator >::formatRecordWithFormatter( const kmac::nova::Record& record ) noexcept
{
	_formatter->begin( record );

	bool done = false;
	while ( ! done )
	{
		Buffer buf( _formatBuffer.data() + _formatOffset, FORMAT_BUFFER_SIZE - _formatOffset );

		done = _formatter->format( record, buf );
		_formatOffset += buf.size();

		if ( ! done )
		{
			flushFormatBuffer();
			// formatter maintains state across chunks; begin() is not called again
		}
	}
}

template< std::size_t PoolSize, std::size_t IndexQueueCapacity, typename IndexType, PoolAllocator Allocator >
void MemoryPoolAsyncBatchSink< PoolSize, IndexQueueCapacity, IndexType, Allocator >::copyRawMessage( const kmac::nova::Record& record ) noexcept
{
	if ( _formatOffset + record.messageSize > FORMAT_BUFFER_SIZE )
	{
		flushFormatBuffer();
	}

	if ( record.messageSize <= FORMAT_BUFFER_SIZE - _formatOffset )
	{
		std::memcpy( _formatBuffer.data() + _formatOffset, record.message, record.messageSize );
		_formatOffset += record.messageSize;
	}
}

template< std::size_t PoolSize, std::size_t IndexQueueCapacity, typename IndexType, PoolAllocator Allocator >
void MemoryPoolAsyncBatchSink< PoolSize, IndexQueueCapacity, IndexType, Allocator >::formatBatch( kmac::nova::Record* const* records, std::size_t count ) noexcept
{
	_formatOffset = 0;

	for ( std::size_t i = 0; i < count; ++i )
	{
		if ( _formatter != nullptr )
		{
			formatRecordWithFormatter( *records[ i ] );
		}
		else
		{
			copyRawMessage( *records[ i ] );
		}
	}

	// flush any remaining formatted data
	if ( _formatOffset > 0 )
	{
		flushFormatBuffer();
	}
}

template< std::size_t PoolSize, std::size_t IndexQueueCapacity, typename IndexType, PoolAllocator Allocator >
void MemoryPoolAsyncBatchSink< PoolSize, IndexQueueCapacity, IndexType, Allocator >::flushFormatBuffer() noexcept
{
	if ( _formatOffset == 0 )
	{
		return;
	}

	// create Record pointing to formatted buffer
	kmac::nova::Record formattedRecord{};
	formattedRecord.tag = nullptr;
	formattedRecord.tagId = 0;
	formattedRecord.file = nullptr;
	formattedRecord.function = nullptr;
	formattedRecord.line = 0;
	formattedRecord.timestamp = 0;
	formattedRecord.messageSize = static_cast< std::uint32_t >( _formatOffset );
	formattedRecord.message = _formatBuffer.data();

	// send to downstream (RollingFileSink will write directly)
	_downstream->process( formattedRecord );

	_formatOffset = 0;
}

} // namespace kmac::nova::extras

#endif // KMAC_NOVA_EXTRAS_MEMORY_POOL_ASYNC_BATCHED_SINK_H
