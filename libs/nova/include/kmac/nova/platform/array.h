#pragma once
#ifndef KMAC_NOVA_PLATFORM_ARRAY_H
#define KMAC_NOVA_PLATFORM_ARRAY_H

#include "config.h"
#include <cstddef>
#include <cstdint>

// include std::array outside namespace to avoid pollution
#if NOVA_HAS_STD_ARRAY
#include <array>
#endif

/**
 * @file array.h
 * @brief Fixed-size array abstraction for bare-metal environments
 *
 * Provides std::array-like interface with two implementations:
 * 1. std::array (default, fully featured)
 * 2. C-style array wrapper (bare-metal, minimal overhead)
 *
 * Both implementations provide:
 * - size known at compile time
 * - no heap allocation
 * - bounds checking in debug builds
 * - iterator support
 */

namespace kmac {
namespace nova {
namespace platform {

// ============================================================================
// IMPLEMENTATION 1: STANDARD LIBRARY (Default)
// ============================================================================

#if NOVA_HAS_STD_ARRAY

template< typename T, std::size_t N >
using Array = std::array< T, N >;

// ============================================================================
// IMPLEMENTATION 2: BARE-METAL C-STYLE ARRAY WRAPPER
// ============================================================================

#else // ! NOVA_HAS_STD_ARRAY

/**
 * @brief Bare-metal fixed-size array wrapper
 *
 * Provides std::array-compatible interface using C-style arrays.
 * Zero overhead compared to raw C arrays.
 * Bounds checking only in debug builds (NOVA_ASSERT).
 */
template< typename T, std::size_t N >
struct Array
{
public:
	using value_type = T;
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;
	using reference = T&;
	using const_reference = const T&;
	using pointer = T*;
	using const_pointer = const T*;
	using iterator = T*;
	using const_iterator = const T*;

	T _data[ N ];  // similar to std::array, public for aggregate initialization compatibility

	// ========================================================================
	// ELEMENT ACCESS
	// ========================================================================

	constexpr reference operator[]( size_type pos ) noexcept
	{
		return _data[ pos ];
	}

	constexpr const_reference operator[]( size_type pos ) const noexcept
	{
		return _data[ pos ];
	}

	constexpr reference at( size_type pos ) noexcept
	{
		NOVA_ASSERT( pos < N && "Array::at: index out of bounds" );
		return _data[ pos ];
	}

	constexpr const_reference at( size_type pos ) const noexcept
	{
		NOVA_ASSERT( pos < N && "Array::at: index out of bounds" );
		return _data[ pos ];
	}

	constexpr reference front() noexcept
	{
		return _data[ 0 ];
	}

	constexpr const_reference front() const noexcept
	{
		return _data[ 0 ];
	}

	constexpr reference back() noexcept
	{
		return _data[ N - 1 ];
	}

	constexpr const_reference back() const noexcept
	{
		return _data[ N - 1 ];
	}

	constexpr pointer data() noexcept
	{
		return _data;
	}

	constexpr const_pointer data() const noexcept
	{
		return _data;
	}

	// ========================================================================
	// ITERATORS
	// ========================================================================

	constexpr iterator begin() noexcept
	{
		return _data;
	}

	constexpr const_iterator begin() const noexcept
	{
		return _data;
	}

	constexpr const_iterator cbegin() const noexcept
	{
		return _data;
	}

	constexpr iterator end() noexcept
	{
		return _data + N;
	}

	constexpr const_iterator end() const noexcept
	{
		return _data + N;
	}

	constexpr const_iterator cend() const noexcept
	{
		return _data + N;
	}

	// ========================================================================
	// CAPACITY
	// ========================================================================

	constexpr bool empty() const noexcept
	{
		return N == 0;
	}

	constexpr size_type size() const noexcept
	{
		return N;
	}

	constexpr size_type max_size() const noexcept
	{
		return N;
	}

	// ========================================================================
	// OPERATIONS
	// ========================================================================

	void fill( const T& value ) noexcept
	{
		for ( size_type i = 0; i < N; ++i )
		{
			_data[ i ] = value;
		}
	}
};

// ============================================================================
// ZERO-SIZE SPECIALIZATION
// ============================================================================

template< typename T >
struct Array< T, 0 >
{
	using value_type = T;
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;
	using reference = T&;
	using const_reference = const T&;
	using pointer = T*;
	using const_pointer = const T*;
	using iterator = T*;
	using const_iterator = const T*;

	constexpr pointer data() noexcept { return nullptr; }
	constexpr const_pointer data() const noexcept { return nullptr; }
	constexpr iterator begin() noexcept { return nullptr; }
	constexpr const_iterator begin() const noexcept { return nullptr; }
	constexpr const_iterator cbegin() const noexcept { return nullptr; }
	constexpr iterator end() noexcept { return nullptr; }
	constexpr const_iterator end() const noexcept { return nullptr; }
	constexpr const_iterator cend() const noexcept { return nullptr; }
	constexpr bool empty() const noexcept { return true; }
	constexpr size_type size() const noexcept { return 0; }
	constexpr size_type max_size() const noexcept { return 0; }
};

#endif // NOVA_HAS_STD_ARRAY

} // namespace platform
} // namespace nova
} // namespace kmac

#endif // KMAC_NOVA_PLATFORM_ARRAY_H
