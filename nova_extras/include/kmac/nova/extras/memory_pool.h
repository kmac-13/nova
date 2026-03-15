#pragma once
#ifndef KMAC_NOVA_EXTRAS_MEMORY_POOL_H
#define KMAC_NOVA_EXTRAS_MEMORY_POOL_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

namespace kmac::nova::extras
{

/**
 * @brief Memory allocation strategy for pool storage.
 */
enum class PoolAllocator
{
	Heap,  // use std::unique_ptr (default, flexible size)
	Stack  // use std::array (faster, but limited by stack constraints)
};

/**
 * @brief Lock-free ring buffer memory pool for variable-size allocations.
 *
 * ✅ SAFE FOR SAFETY-CRITICAL SYSTEMS
 *
 * Thread-safe memory pool using atomic operations for allocation/deallocation.
 * Designed for single-reader/multiple-writer scenarios (async logging).
 *
 * Memory layout:
 * ┌─────────────────────────────────────────────────────────┐
 * │ [Entry1][Entry2][Entry3]...[Free Space]...[EntryN]      │
 * │  ^read                      ^write                      │
 * └─────────────────────────────────────────────────────────┘
 *
 * Wrap-around behavior:
 * When an entry won't fit in the remaining space before the pool end, we
 * advance the write pointer to the next pool boundary (wasting the remaining
 * bytes as padding). This is simpler than split allocations and the waste
 * is minimal in practice (average: entry_size/2 per wrap).
 *
 * Thread safety:
 * - multiple producers: lock-free atomic allocation
 * - single consumer: simple atomic release
 * - no ABA problem: monotonically increasing offsets
 *
 * @tparam Capacity total pool size in bytes (must be power of 2)
 * @tparam Allocator allocation strategy (Heap or Stack)
 */
template< std::size_t Capacity, PoolAllocator Allocator = PoolAllocator::Heap >
class MemoryPool
{
	// ensure power of 2 for efficient modulo via bitwise AND
	static_assert( ( Capacity & ( Capacity - 1 ) ) == 0, "Capacity must be power of 2" );
	static_assert( Capacity >= 1024, "Minimum capacity is 1KB" );
	static_assert( Capacity <= 256 * 1024 * 1024, "Maximum capacity is 256MB" );

	// stack allocation safety limits
	static_assert( Allocator != PoolAllocator::Stack || Capacity <= 256 * 1024,
		"Stack allocation limited to 256KB for portability" );

private:
	// cache line alignment to avoid false sharing
	alignas( 64 ) std::atomic< std::size_t > _writeOffset;
	alignas( 64 ) std::atomic< std::size_t > _readOffset;

	// conditional storage based on allocator type
	using HeapStorage = std::unique_ptr< uint8_t[] >;
	using StackStorage = std::array< uint8_t, Capacity >;

	std::conditional_t< Allocator == PoolAllocator::Heap, HeapStorage, StackStorage > _pool;

public:
	/**
	 * @brief Get pool capacity in bytes.
	 *
	 * @return Pool capacity
	 */
	static constexpr std::size_t capacity() noexcept;

	/**
	 * @brief Construct memory pool.
	 *
	 * Heap allocator: allocates memory dynamically
	 * Stack allocator: uses stack-allocated array
	 */
	MemoryPool() noexcept;

	/**
	 * @brief Gets the pointer represented by the offset from the base of
	 * the memory pool.
	 *
	 * @param offset offset to get pointer for
	 * @return pointer found at offset
	 */
	uint8_t* offsetToPointer( std::size_t offset ) const;

	/**
	 * @brief Gets the offset difference between the specified pointer and
	 * the base of the memory pool.
	 *
	 * @param ptr point to calculate offset for
	 * @return offset representing pointer
	 */
	std::size_t pointerToOffset( const uint8_t* ptr ) const;

	/**
	 * @brief Get approximate bytes available for allocation.
	 *
	 * This is an approximation because the value may change between checking
	 * and allocating (concurrent producers).
	 *
	 * @return Approximate available bytes
	 */
	std::size_t available() const noexcept;

	/**
	 * @brief Get approximate bytes currently in use.
	 *
	 * This is an approximation because the value may change between checking
	 * and accessing (concurrent producers/consumer).
	 *
	 * @return Approximate used bytes
	 */
	std::size_t used() const noexcept;

	/**
	 * @brief Allocate contiguous space for an entry.
	 *
	 * Thread-safe allocation using atomic compare-exchange.
	 * Returns nullptr if insufficient space available.
	 *
	 * Allocation algorithm:
	 * 1. check if requested size fits in available space
	 * 2. if not enough space before wrap, advance to next boundary
	 * 3. atomically claim the space
	 * 4. return pointer or nullptr if failed
	 *
	 * @param size number of bytes to allocate
	 * @return pointer to allocated space, or nullptr if full
	 */
	uint8_t* allocate( std::size_t size ) noexcept;

	/**
	 * @brief Release consumed space (called by consumer).
	 *
	 * Must be called with same alignment as allocate().
	 * Consumer must release entries in the same order they were allocated.
	 *
	 * @param size number of bytes to release
	 */
	void release( std::size_t size ) noexcept;

	/**
	 * @brief Get current read offset.
	 *
	 * Note: This is a monotonically increasing offset, not a physical position.
	 * Use modulo Capacity to get physical position.
	 *
	 * @return current read offset
	 */
	std::size_t readOffset() const noexcept;

	/**
	 * @brief Get current write offset.
	 *
	 * Note: This is a monotonically increasing offset, not a physical position.
	 * Use modulo Capacity to get physical position.
	 *
	 * @return current write offset
	 */
	std::size_t writeOffset() const noexcept;

private:
	/**
	 * @brief Get base address of pool.
	 *
	 * Used for calculating offsets and pointer arithmetic.
	 *
	 * @return pointer to start of pool
	 */
	uint8_t* base() const noexcept;

	/**
	 * @brief Initialize pool storage based on allocator type.
	 *
	 * @return initialized storage (heap or stack)
	 */
	auto initializePool() noexcept;
};

template< std::size_t Capacity, PoolAllocator Allocator >
constexpr std::size_t MemoryPool< Capacity, Allocator >::capacity() noexcept
{
	return Capacity;
}

template< std::size_t Capacity, PoolAllocator Allocator >
MemoryPool< Capacity, Allocator >::MemoryPool() noexcept
	: _writeOffset( 0 )
	, _readOffset( 0 )
	, _pool( initializePool() )
{
}

template< std::size_t Capacity, PoolAllocator Allocator >
uint8_t* MemoryPool< Capacity, Allocator >::offsetToPointer( std::size_t offset ) const
{
	return base() + offset;
}

template< std::size_t Capacity, PoolAllocator Allocator >
std::size_t MemoryPool< Capacity, Allocator >::pointerToOffset( const uint8_t* ptr ) const
{
	return ptr - base();
}

template< std::size_t Capacity, PoolAllocator Allocator >
std::size_t MemoryPool< Capacity, Allocator >::available() const noexcept
{
	std::size_t write = _writeOffset.load( std::memory_order_acquire );
	std::size_t read = _readOffset.load( std::memory_order_acquire );
	return Capacity - ( write - read );
}

template< std::size_t Capacity, PoolAllocator Allocator >
std::size_t MemoryPool< Capacity, Allocator >::used() const noexcept
{
	std::size_t write = _writeOffset.load( std::memory_order_acquire );
	std::size_t read = _readOffset.load( std::memory_order_acquire );
	return write - read;
}

template< std::size_t Capacity, PoolAllocator Allocator >
uint8_t* MemoryPool< Capacity, Allocator >::allocate( std::size_t size ) noexcept
{
	// align to 8-byte boundary for Record struct alignment
	size = ( size + 7 ) & ~std::size_t{ 7 };

	// wrap retries and allocation retries are bounded independently so
	// wrap contention doesn't consume allocation attempts (and vice versa)
	constexpr int MAX_WRAP_RETRIES = 8;
	constexpr int MAX_ALLOC_RETRIES = 8;

	int wrapRetries = 0;

	while ( wrapRetries < MAX_WRAP_RETRIES )
	{
		std::size_t write = _writeOffset.load( std::memory_order_acquire );
		std::size_t read = _readOffset.load( std::memory_order_acquire );

		// check if we have enough total space
		if ( size > ( Capacity - ( write - read ) ) )
		{
			// pool is full
			return nullptr;
		}

		// calculate physical positions (handle overflow via modulo)
		std::size_t writePos = write & ( Capacity - 1 );

		if ( size > ( Capacity - writePos ) )
		{
			// need to wrap, compare-and-swap failure means another thread already wrapped,
			// which is fine, just reload and retry without burning an alloc attempt
			std::size_t nextBoundary = ( ( write / Capacity ) + 1 ) * Capacity;
			_writeOffset.compare_exchange_weak( write, nextBoundary, std::memory_order_release, std::memory_order_relaxed );
			++wrapRetries;
			continue;
		}

		// normal allocation attempt
		for ( int allocRetries = 0; allocRetries < MAX_ALLOC_RETRIES; ++allocRetries )
		{
			if ( _writeOffset.compare_exchange_weak( write, write + size, std::memory_order_release, std::memory_order_relaxed ) )
			{
				return base() + writePos;
			}

			// write is now updated to the current _writeOffset value, and now writePos
			// needs to be recomputed so we don't return a stale/claimed address
			writePos = write & ( Capacity - 1 );

			// if the updated write position requires a wrap, break to the
			// outer loop rather than retrying with an unusable writePos
			if ( size > ( Capacity - writePos ) )
			{
				break;
			}

			// re-check pool fullness with the updated read/write positions
			read = _readOffset.load( std::memory_order_acquire );
			if ( size > ( Capacity - ( write - read ) ) )
			{
				return nullptr;
			}
		}

		// counts outer iterations regardless of why inner exited
		++wrapRetries;
	}

	// wrap contention exhausted - treat as full
	return nullptr;
}

template< std::size_t Capacity, PoolAllocator Allocator >
void MemoryPool< Capacity, Allocator >::release( std::size_t size ) noexcept
{
	// ensure same alignment as allocate
	size = ( size + 7 ) & ~std::size_t{ 7 };
	_readOffset.fetch_add( size, std::memory_order_release );
}

template< std::size_t Capacity, PoolAllocator Allocator >
std::size_t MemoryPool< Capacity, Allocator >::readOffset() const noexcept
{
	return _readOffset.load( std::memory_order_acquire );
}

template< std::size_t Capacity, PoolAllocator Allocator >
std::size_t MemoryPool< Capacity, Allocator >::writeOffset() const noexcept
{
	return _writeOffset.load( std::memory_order_acquire );
}

template< std::size_t Capacity, PoolAllocator Allocator >
uint8_t* MemoryPool< Capacity, Allocator >::base() const noexcept
{
	if constexpr ( Allocator == PoolAllocator::Heap )
	{
		return _pool.get();
	}
	else
	{
		return const_cast< uint8_t* >( _pool.data() );
	}
}

template< std::size_t Capacity, PoolAllocator Allocator >
auto MemoryPool< Capacity, Allocator >::initializePool() noexcept
{
	if constexpr ( Allocator == PoolAllocator::Heap )
	{
		return std::make_unique< uint8_t[] >( Capacity );
	}
	else
	{
		return StackStorage{}; // Zero-initialized
	}
}

} // namespace kmac::nova::extras

#endif // KMAC_NOVA_EXTRAS_MEMORY_POOL_H
