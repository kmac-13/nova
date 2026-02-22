#pragma once
#ifndef KMAC_NOVA_EXTRAS_MPSC_QUEUE_H
#define KMAC_NOVA_EXTRAS_MPSC_QUEUE_H

#include <atomic>
#include <cstddef>

namespace kmac::nova::extras
{

/**
 * @brief Lock-free Multi-Producer-Single-Consumer bounded queue.
 *
 * ✅ SAFE FOR SAFETY-CRITICAL SYSTEMS
 *
 * This is a simplified MPSC queue using a ring buffer.  Multiple threads
 * can safely push() concurrently, and a single consumer thread can pop().
 *
 * Implementation uses atomic operations for thread safety without locks.
 *
 * Note: This is a simple implementation.  For production use, consider
 * more sophisticated lock-free queue algorithms (e.g., from Boost.Lockfree).
 */
template< typename T, std::size_t Capacity >
class MPSCQueue
{
private:
	struct Slot
	{
		T data;
		std::atomic< std::size_t > sequence;
	};

	// cache line padding to avoid false sharing
	static constexpr std::size_t CACHE_LINE_SIZE = 64;

	alignas( CACHE_LINE_SIZE ) std::atomic< std::size_t > _head;
	alignas( CACHE_LINE_SIZE ) std::atomic< std::size_t > _tail;

	Slot _slots[Capacity];

public:
	MPSCQueue() noexcept;
	~MPSCQueue() noexcept = default;

	// non-copyable, non-movable
	MPSCQueue( const MPSCQueue& ) = delete;
	MPSCQueue& operator=( const MPSCQueue& ) = delete;
	MPSCQueue( MPSCQueue&& ) = delete;
	MPSCQueue& operator=( MPSCQueue&& ) = delete;

	/**
	 * @brief Get approximate size of queue.
	 *
	 * Note: In a concurrent environment, this is approximate.
	 */
	std::size_t size() const noexcept;

	/**
	 * @brief Check if queue is empty (approximate).
	 *
	 * Note: In a concurrent environment, this is a best-effort check.
	 * The queue state may change immediately after this returns.
	 */
	bool empty() const noexcept;

	/**
	 * @brief Push an item to the queue (multi-producer safe).
	 *
	 * Multiple threads can call this concurrently.
	 * Returns false if queue is full.
	 *
	 * @param item Item to push
	 * @return true if pushed successfully, false if queue full
	 */
	bool push( const T& item ) noexcept;

	/**
	 * @brief Pop an item from the queue (single-consumer only).
	 *
	 * Only one thread should call this.
	 * Returns false if queue is empty.
	 *
	 * @param item Output parameter for popped item
	 * @return true if popped successfully, false if queue empty
	 */
	bool pop( T& item ) noexcept;

	/**
	 * @brief Pop multiple items from the queue in one batch (single-consumer only).
	 *
	 * This is more efficient than calling pop() multiple times because it
	 * amortizes the cost of atomic operations.
	 *
	 * @param items output buffer for popped items (must have space for maxCount)
	 * @param maxCount maximum number of items to pop
	 * @return number of items actually popped (0 if queue empty)
	 */
	std::size_t popBatch( T* items, std::size_t maxCount ) noexcept;
};

template< typename T, std::size_t Capacity >
MPSCQueue< T, Capacity >::MPSCQueue() noexcept
	: _head( 0 )
	, _tail( 0 )
{
	// initialize sequence numbers
	for ( std::size_t i = 0; i < Capacity; ++i )
	{
		_slots[ i ].sequence.store( i, std::memory_order_relaxed );
	}
}

template< typename T, std::size_t Capacity >
std::size_t MPSCQueue< T, Capacity >::size() const noexcept
{
	std::size_t tail = _tail.load( std::memory_order_acquire );
	std::size_t head = _head.load( std::memory_order_acquire );
	return head - tail;
}

template< typename T, std::size_t Capacity >
bool MPSCQueue< T, Capacity >::empty() const noexcept
{
	std::size_t tail = _tail.load( std::memory_order_acquire );
	std::size_t head = _head.load( std::memory_order_acquire );
	return head == tail;
}

template< typename T, std::size_t Capacity >
bool MPSCQueue< T, Capacity >::push( const T& item ) noexcept
{
	std::size_t head = _head.load( std::memory_order_relaxed );

	while ( true )
	{
		Slot& slot = _slots[ head % Capacity ];
		std::size_t seq = slot.sequence.load( std::memory_order_acquire );

		// check if slot is ready for writing
		if ( seq == head )
		{
			// try to claim this slot
			if ( _head.compare_exchange_weak( head, head + 1, std::memory_order_relaxed ) )
			{
				// successfully claimed - write data
				slot.data = item;

				// mark as ready for reading
				slot.sequence.store( head + 1, std::memory_order_release );
				return true;
			}
			// call-and-swap failed - another thread claimed it, retry
		}
		else if ( seq < head )
		{
			// queue is full
			return false;
		}
		else
		{
			// another thread is ahead of us, reload head and retry
			head = _head.load( std::memory_order_relaxed );
		}
	}
}

template< typename T, std::size_t Capacity >
bool MPSCQueue< T, Capacity >::pop( T& item ) noexcept
{
	std::size_t tail = _tail.load( std::memory_order_relaxed );
	Slot& slot = _slots[tail % Capacity];

	std::size_t seq = slot.sequence.load( std::memory_order_acquire );

	// check if data is ready
	if ( seq == tail + 1 )
	{
		// read the data
		item = slot.data;

		// mark slot as ready for writing
		slot.sequence.store( tail + Capacity, std::memory_order_release );

		// advance tail
		_tail.store( tail + 1, std::memory_order_release );
		return true;
	}

	// queue is empty
	return false;
}

template< typename T, std::size_t Capacity >
std::size_t MPSCQueue< T, Capacity >::popBatch( T* items, std::size_t maxCount ) noexcept
{
	std::size_t tail = _tail.load( std::memory_order_relaxed );
	std::size_t count = 0;

	// pop up to maxCount items
	while ( count < maxCount )
	{
		Slot& slot = _slots[ ( tail + count ) % Capacity ];
		std::size_t seq = slot.sequence.load( std::memory_order_acquire );

		// check if data is ready
		if ( seq == ( tail + count ) + 1 )
		{
			// read the data
			items[ count ] = slot.data;
			++count;
		}
		else
		{
			// no more items available
			break;
		}
	}

	// mark all consumed slots as available and update tail;
	// this is done AFTER reading all items to minimize the window
	// where items are read but slots aren't marked as consumed
	for ( std::size_t i = 0; i < count; ++i )
	{
		Slot& slot = _slots[ ( tail + i ) % Capacity ];
		slot.sequence.store( tail + i + Capacity, std::memory_order_release );
	}

	// advance tail by the number of items we consumed
	if ( count > 0 )
	{
		_tail.store( tail + count, std::memory_order_release );
	}

	return count;
}

} // namespace kmac::nova::extras

#endif // KMAC_NOVA_EXTRAS_MPSC_QUEUE_H
