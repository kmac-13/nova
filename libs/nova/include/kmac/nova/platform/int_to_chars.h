#pragma once
#ifndef KMAC_NOVA_PLATFORM_INT_TO_CHARS_H
#define KMAC_NOVA_PLATFORM_INT_TO_CHARS_H

#include "config.h"
#include <cstddef>
#include <cstdint>
#include <type_traits>

#if NOVA_HAS_CHARCONV
#include <charconv>
#endif

/**
 * @file int_to_chars.h
 * @brief Integer to string conversion for environments where std::to_chars
 * is not available.
 *
 * Provides a uniform intToChars() interface that delegates to std::to_chars
 * on hosted targets and a fallback implementation on bare-metal toolchains
 * where <charconv> is absent (e.g. arm-none-eabi with newlib-nano).
 *
 * Both paths share the same IntToCharsResult return type so callers are
 * unconditional - the #if logic lives entirely inside this header.
 *
 * Overloads are provided for all standard integer types so callers need no
 * explicit casts.  Overloads for types that alias int64_t or uint64_t on the
 * current platform are suppressed via enable_if to avoid redefinition errors
 * (e.g. long == int64_t on LP64 Linux; long long == int64_t on ILP32 ARM).
 * uintptr_t is always covered because it aliases unsigned long or unsigned
 * long long on all supported platforms.
 *
 * Worst-case output widths:
 * - base 10 signed:   20 characters ("-9223372036854775808")
 * - base 10 unsigned: 20 characters ("18446744073709551615")
 * - base 16:          16 characters ("ffffffffffffffff")
 */

namespace kmac::nova::platform
{

/**
 * @brief Result type mirroring std::to_chars_result, without <charconv>.
 *
 * Used as the return type of intToChars() on both hosted and bare-metal
 * paths so callers need no #if logic of their own.
 */
struct IntToCharsResult
{
	char* ptr;  ///< one past the last written character (mirrors to_chars_result::ptr)
	bool  ok;   ///< true on success; false if the buffer was too small
};

// ============================================================================
// CORE IMPLEMENTATIONS (uint64_t and int64_t)
// All other overloads forward to these.
// ============================================================================

/**
 * @brief Convert an unsigned 64-bit integer to its string representation.
 *
 * Writes the result into [first, last) without null termination, mirroring
 * the behaviour of std::to_chars(first, last, value) and
 * std::to_chars(first, last, value, base).
 *
 * On hosted targets (NOVA_HAS_CHARCONV == 1), this delegates directly to
 * std::to_chars.  On bare-metal targets, a fallback implementation is used.
 *
 * @param first start of the output buffer
 * @param last one past the end of the output buffer
 * @param value value to convert
 * @param base numeric base; must be 10 or 16
 * @return IntToCharsResult::ptr points one past the last written character
 *         on success; IntToCharsResult::ok is false if the buffer was too small
 *
 * @note No heap allocation.
 * @note Base 16 output uses lowercase hex digits (a-f).
 */
inline IntToCharsResult intToChars( char* first, char* last, std::uint64_t value, int base = 10 ) noexcept
{
#if NOVA_HAS_CHARCONV

	auto [ ptr, ec ] = std::to_chars( first, last, value, base );
	return { ptr, ec == std::errc{} };

#else

	if ( first >= last )
	{
		return { last, false };
	}

	char tmp[ 20 ];  // max digits for uint64 in base 10 or 16
	int n = 0;

	if ( value == 0 )
	{
		tmp[ n++ ] = '0';
	}
	else if ( base == 16 )
	{
		constexpr char hex[] = "0123456789abcdef";
		while ( value > 0 )
		{
			tmp[ n++ ] = hex[ value & 0xF ];
			value >>= 4;
		}
	}
	else  // base 10
	{
		while ( value > 0 )
		{
			tmp[ n++ ] = static_cast< char >( '0' + value % 10 );
			value /= 10;
		}
	}

	if ( first + n > last )
	{
		return { last, false };
	}

	for ( int i = n - 1; i >= 0; --i )
	{
		*first++ = tmp[ i ];
	}

	return { first, true };

#endif // NOVA_HAS_CHARCONV
}

/**
 * @brief Convert a signed 64-bit integer to its base-10 string representation.
 *
 * Writes the result into [first, last) without null termination, mirroring
 * the behaviour of std::to_chars(first, last, value).
 *
 * On hosted targets (NOVA_HAS_CHARCONV == 1), this delegates directly to
 * std::to_chars.  On bare-metal targets, a fallback implementation is used.
 *
 * @param first start of the output buffer
 * @param last one past the end of the output buffer
 * @param value value to convert
 * @return IntToCharsResult::ptr points one past the last written character
 *         on success; IntToCharsResult::ok is false if the buffer was too small
 *
 * @note No heap allocation.
 */
inline IntToCharsResult intToChars( char* first, char* last, std::int64_t value ) noexcept
{
#if NOVA_HAS_CHARCONV

	auto [ ptr, ec ] = std::to_chars( first, last, value );
	return { ptr, ec == std::errc{} };

#else

	if ( first >= last )
	{
		return { last, false };
	}

	if ( value < 0 )
	{
		*first++ = '-';
		// cast via unsigned to handle INT64_MIN correctly (negating it overflows)
		return intToChars( first, last, static_cast< std::uint64_t >( 0 ) - static_cast< std::uint64_t >( value ) );
	}

	return intToChars( first, last, static_cast< std::uint64_t >( value ) );

#endif // NOVA_HAS_CHARCONV
}

// ============================================================================
// CONVENIENCE OVERLOADS
// Forward to the uint64_t / int64_t core implementations above.
// Provided so callers need no explicit casts.
//
// Each overload is gated by enable_if to suppress it when the type is already
// identical to int64_t or uint64_t on the current platform (e.g. long on
// LP64 Linux, long long on ILP32 ARM).  This avoids redefinition errors
// while still providing the overload on platforms where the types are distinct.
// ============================================================================

/** @brief Convert an int to its base-10 string representation. */
template< typename T = int >
inline typename std::enable_if<
	std::is_same< T, int >::value && ! std::is_same< int, std::int64_t >::value,
	IntToCharsResult >::type
intToChars( char* first, char* last, int value ) noexcept
{
	return intToChars( first, last, static_cast< std::int64_t >( value ) );
}

/** @brief Convert an unsigned int to its base-10 string representation. */
template< typename T = unsigned int >
inline typename std::enable_if<
	std::is_same< T, unsigned int >::value && ! std::is_same< unsigned int, std::uint64_t >::value,
	IntToCharsResult >::type
intToChars( char* first, char* last, unsigned int value ) noexcept
{
	return intToChars( first, last, static_cast< std::uint64_t >( value ) );
}

/** @brief Convert a long to its base-10 string representation. */
template< typename T = long >
inline typename std::enable_if<
	std::is_same< T, long >::value && ! std::is_same< long, std::int64_t >::value,
	IntToCharsResult >::type
intToChars( char* first, char* last, long value ) noexcept
{
	return intToChars( first, last, static_cast< std::int64_t >( value ) );
}

/** @brief Convert an unsigned long to its base-10 string representation. */
template< typename T = unsigned long >
inline typename std::enable_if<
	std::is_same< T, unsigned long >::value && ! std::is_same< unsigned long, std::uint64_t >::value,
	IntToCharsResult >::type
intToChars( char* first, char* last, unsigned long value ) noexcept
{
	return intToChars( first, last, static_cast< std::uint64_t >( value ) );
}

/** @brief Convert a long long to its base-10 string representation. */
template< typename T = long long >
inline typename std::enable_if<
	std::is_same< T, long long >::value && ! std::is_same< long long, std::int64_t >::value,
	IntToCharsResult >::type
intToChars( char* first, char* last, long long value ) noexcept
{
	return intToChars( first, last, static_cast< std::int64_t >( value ) );
}

/** @brief Convert an unsigned long long to its base-10 string representation. */
template< typename T = unsigned long long >
inline typename std::enable_if<
	std::is_same< T, unsigned long long >::value && ! std::is_same< unsigned long long, std::uint64_t >::value,
	IntToCharsResult >::type
intToChars( char* first, char* last, unsigned long long value ) noexcept
{
	return intToChars( first, last, static_cast< std::uint64_t >( value ) );
}

} // namespace kmac::nova::platform

#endif // KMAC_NOVA_PLATFORM_INT_TO_CHARS_H
