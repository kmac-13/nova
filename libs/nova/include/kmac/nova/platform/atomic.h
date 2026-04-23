#pragma once
#ifndef KMAC_NOVA_PLATFORM_ATOMIC_H
#define KMAC_NOVA_PLATFORM_ATOMIC_H

#include "config.h"
#include <cstdint>

// Include std::atomic outside of any namespace to avoid pollution
#if NOVA_HAS_STD_ATOMIC
#include <atomic>
#endif

/**
 * @file atomic.h
 * @brief Atomic operations abstraction for bare-metal and RTOS environments
 *
 * Provides atomic pointer operations with multiple backend implementations:
 * 1. std::atomic (default, thread-safe)
 * 2. Volatile pointers (bare-metal single-core, not thread-safe)
 * 3. RTOS-specific primitives (interrupt-safe)
 * 4. Compiler intrinsics (platform-specific atomics)
 *
 * Trade-offs:
 * - std::atomic: Full thread safety, TSAN compatible
 * - volatile: Single-core only, minimal overhead, NOT thread-safe
 * - RTOS primitives: Interrupt-safe, RTOS-dependent
 * - Intrinsics: Platform-specific, may require manual porting
 *
 * Safety Note:
 * For safety-critical systems (DO-178C Level A, IEC 61508 SIL 3/4):
 * - Use std::atomic where available and qualified
 * - For bare-metal single-core: volatile is acceptable with proper analysis
 * - For bare-metal multi-core: must provide qualified atomic implementation
 * - Document assumptions about interrupt/thread safety in safety case
 */

namespace kmac {
namespace nova {
namespace platform {

// ============================================================================
// IMPLEMENTATION 1: STANDARD LIBRARY (Default)
// ============================================================================

#if NOVA_HAS_STD_ATOMIC

template< typename T >
class AtomicPtr
{
private:
	std::atomic< T* > _ptr;

public:
	AtomicPtr() noexcept : _ptr( nullptr ) {}
	explicit AtomicPtr( T* ptr ) noexcept : _ptr( ptr ) {}

	// load with acquire semantics
	T* load( std::memory_order order = std::memory_order_acquire ) const noexcept
	{
		return _ptr.load( order );
	}

	// store with release semantics
	void store( T* ptr, std::memory_order order = std::memory_order_release ) noexcept
	{
		_ptr.store( ptr, order );
	}

	// exchange (swap)
	T* exchange( T* ptr, std::memory_order order = std::memory_order_acq_rel ) noexcept
	{
		return _ptr.exchange( ptr, order );
	}

	// compare-and-swap
	bool compare_exchange_weak( T*& expected, T* desired,
		std::memory_order success = std::memory_order_acq_rel,
		std::memory_order failure = std::memory_order_acquire ) noexcept
	{
		return _ptr.compare_exchange_weak( expected, desired, success, failure );
	}

	bool compare_exchange_strong( T*& expected, T* desired,
		std::memory_order success = std::memory_order_acq_rel,
		std::memory_order failure = std::memory_order_acquire ) noexcept
	{
		return _ptr.compare_exchange_strong( expected, desired, success, failure );
	}
};

// ============================================================================
// IMPLEMENTATION 2: BARE-METAL VOLATILE (Single-core only, NOT thread-safe)
// ============================================================================

#elif defined( NOVA_BARE_METAL ) || defined( NOVA_NO_ATOMIC )

/**
 * WARNING: This implementation is NOT thread-safe and NOT interrupt-safe!
 *
 * Safe usage scenarios:
 * - single-core bare-metal with interrupts disabled during sink binding
 * - single-threaded applications
 * - logging from single execution context only
 *
 * Unsafe scenarios:
 * - multi-core systems
 * - multi-threaded RTOS
 * - logging from interrupts AND normal code
 * - any concurrent access to Logger<Tag>::bindSink()/unbindSink()
 *
 * For multi-core or concurrent systems, you MUST provide a proper atomic
 * implementation using platform-specific intrinsics or RTOS primitives.
 */

template< typename T >
class AtomicPtr
{
private:
	T* volatile _ptr;  // volatile prevents compiler optimizations

public:
	AtomicPtr() noexcept : _ptr( nullptr ) {}
	explicit AtomicPtr( T* ptr ) noexcept : _ptr( ptr ) {}

	// volatile load (NOT atomic on multi-core!)
	T* load() const noexcept
	{
		return _ptr;
	}

	// volatile store (NOT atomic on multi-core!)
	void store( T* ptr ) noexcept
	{
		_ptr = ptr;
	}

	// exchange - simple swap (NOT atomic!)
	T* exchange( T* ptr ) noexcept
	{
		T* old = _ptr;
		_ptr = ptr;
		return old;
	}

	// compare-and-swap (NOT atomic!)
	bool compare_exchange_weak( T*& expected, T* desired ) noexcept
	{
		if ( _ptr == expected )
		{
			_ptr = desired;
			return true;
		}
		else
		{
			expected = _ptr;
			return false;
		}
	}

	bool compare_exchange_strong( T*& expected, T* desired ) noexcept
	{
		return compare_exchange_weak( expected, desired );
	}

	// overloads to match std::atomic interface (ignore memory_order)
	template< typename... Args >
	T* load( Args&&... ) const noexcept { return load(); }

	template< typename... Args >
	void store( T* ptr, Args&&... ) noexcept { store( ptr ); }

	template< typename... Args >
	T* exchange( T* ptr, Args&&... ) noexcept { return exchange( ptr ); }

	template< typename... Args >
	bool compare_exchange_weak( T*& expected, T* desired, Args&&... ) noexcept
	{
		return compare_exchange_weak( expected, desired );
	}

	template< typename... Args >
	bool compare_exchange_strong( T*& expected, T* desired, Args&&... ) noexcept
	{
		return compare_exchange_strong( expected, desired );
	}
};

#endif

// ============================================================================
// USER-PROVIDED ATOMIC IMPLEMENTATION
// ============================================================================

/**
 * Platform-Specific Atomic Implementations
 *
 * Users can provide their own atomic implementation by defining
 * NOVA_CUSTOM_ATOMIC before including this header:
 *
 * Example: ARM Cortex-M with LDREX/STREX
 *
 * template< typename T >
 * class AtomicPtr
 * {
 * private:
 *	T* _ptr;
 *
 * public:
 *	T* load() const noexcept
 *	{
 *		T* value;
 *		__asm__ volatile( "ldr %0, [%1]" : "=r"( value ) : "r"( &_ptr ) : "memory" );
 *		__asm__ volatile( "dmb" ::: "memory" );
 *		return value;
 *	}
 *
 *	void store( T* ptr ) noexcept
 *	{
 *		__asm__ volatile( "dmb" ::: "memory" );
 *		__asm__ volatile( "str %1, [%0]" :: "r"( &_ptr ), "r"( ptr ) : "memory" );
 *	}
 *
 *	T* exchange( T* desired ) noexcept
 *	{
 *		T* old;
 *		uint32_t success;
 *		do
 *		{
 *			__asm__ volatile( "ldrex %0, [%2]" : "=r"( old ) : "r"( &_ptr ) : "memory");
 *			__asm__ volatile( "strex %0, %2, [%1]" : "=&r"( success ) : "r"( &_ptr ), "r"( desired ) : "memory" );
 *		}
 *		while ( success != 0 );
 *		return old;
 *	}
 * };
 *
 * Example: FreeRTOS critical sections
 *
 * template< typename T >
 * class AtomicPtr
 * {
 * private:
 *	T* _ptr;
 *
 * public:
 *	T* load() const noexcept
 *	{
 *		taskENTER_CRITICAL();
 *		T* value = _ptr;
 *		taskEXIT_CRITICAL();
 *		return value;
 *	}
 *
 *	void store( T* ptr ) noexcept
 *	{
 *		taskENTER_CRITICAL();
 *		_ptr = ptr;
 *		taskEXIT_CRITICAL();
 *	}
 * };
 */

} // namespace platform
} // namespace nova
} // namespace kmac

#endif // KMAC_NOVA_PLATFORM_ATOMIC_H
