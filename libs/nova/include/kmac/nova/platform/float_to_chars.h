#pragma once
#ifndef KMAC_NOVA_PLATFORM_FLOAT_TO_CHARS_H
#define KMAC_NOVA_PLATFORM_FLOAT_TO_CHARS_H

#include "config.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

#if NOVA_HAS_CHARCONV
#include <charconv>
#endif

/**
 * @file float_to_chars.h
 * @brief Floating-point to string conversion for environments where std::to_chars
 * is not available.
 *
 * Provides a uniform floatToChars() interface that delegates to
 * std::to_chars on hosted targets and a fixed-point fallback on bare-metal
 * toolchains where the floating-point std::to_chars symbols are absent
 * (e.g. arm-none-eabi with newlib-nano).
 *
 * Both paths share the same FloatToCharsResult return type so callers are
 * unconditional - the #if logic lives entirely inside this header.
 *
 * Fallback algorithm: fixed-point decimal, 6 decimal places.
 * Sufficient for human-readable logging; round-trip accuracy is not a goal.
 * Special values handled: NaN, +/-Inf, -0 (printed as "0.000000").
 * Values whose integer part exceeds uint64 max (~1.84e19) are clamped.
 * Carry from rounding (e.g. 1.9999999 -> 2.000000) is handled correctly.
 *
 * Worst-case output width: 28 characters ("-" + 20 integer digits + "." +
 * 6 fractional digits).  Callers should reserve at least 32 bytes.
 */

namespace kmac {
namespace nova {
namespace platform {

/**
 * @brief Result type mirroring std::to_chars_result, without <charconv>.
 *
 * Used as the return type of floatToChars() on both hosted and bare-metal
 * paths so callers need no #if logic of their own.
 */
struct FloatToCharsResult
{
	char* ptr;  ///< one past the last written character (mirrors to_chars_result::ptr)
	bool  ok;   ///< true on success; false if the buffer was too small
};

// ============================================================================
// IMPLEMENTATION DETAIL (bare-metal fallback only)
// ============================================================================

#if ! NOVA_HAS_CHARCONV

namespace details
{

/**
 * @brief Write a uint64_t into [buf, end) with no null terminator.
 *
 * @param buf destination buffer start
 * @param end one past the last writable byte
 * @param value value to convert
 * @return pointer one past the last written char, or nullptr if buf too small
 */
inline char* writeUInt( char* buf, char* end, std::uint64_t value ) noexcept
{
	char tmp[ 20 ];  // uint64 max is 20 decimal digits
	int n = 0;

	if ( value == 0 )
	{
		tmp[ n++ ] = '0';
	}
	else
	{
		while ( value > 0 )
		{
			tmp[ n++ ] = static_cast< char >( '0' + value % 10 );
			value /= 10;
		}
	}

	if ( buf + n > end )
	{
		return nullptr;
	}

	for ( int i = n - 1; i >= 0; --i )
	{
		*buf++ = tmp[ i ];
	}

	return buf;
}

} // namespace details

#endif // ! NOVA_HAS_CHARCONV

// ============================================================================
// PUBLIC INTERFACE
// ============================================================================

/**
 * @brief Convert a double to its decimal string representation.
 *
 * Writes the result into [first, last) without null termination, mirroring
 * the behaviour of std::to_chars(first, last, value).
 *
 * On hosted targets (NOVA_HAS_CHARCONV == 1) this delegates directly
 * to std::to_chars.  On bare-metal targets it uses a fixed-point fallback
 * with 6 decimal places.
 *
 * float callers should promote to double before calling:
 * @code
 *   floatToChars( buf, end, static_cast< double >( floatValue ) );
 * @endcode
 *
 * @param first start of the output buffer
 * @param last one past the end of the output buffer; callers should
 *             reserve at least 32 bytes to cover the worst case
 * @param value value to convert
 * @return FloatToCharsResult::ptr points one past the last written character
 *         on success; FloatToCharsResult::ok is false if the buffer was too
 *         small (ptr is set to last in that case)
 *
 * @note No heap allocation.
 * @note Not locale-sensitive; always uses '.' as the decimal separator.
 * @note Negative zero is formatted as "0.000000".
 */
inline FloatToCharsResult floatToChars( char* first, char* last, double value ) noexcept
{
#if NOVA_HAS_CHARCONV

	const auto result = std::to_chars( first, last, value );
	return { result.ptr, result.ec == std::errc{} };

#else // bare-metal fixed-point fallback

	char* p = first;

	// -------------------------------------------------------------------------
	// Special values: inspect IEEE 754 bits via memcpy to avoid <cmath>
	// -------------------------------------------------------------------------

	std::uint64_t bits = 0;
	std::memcpy( &bits, &value, sizeof( bits ) );

	const std::uint64_t exponentMask = 0x7FF0000000000000ULL;
	const std::uint64_t mantissaMask = 0x000FFFFFFFFFFFFFULL;
	const bool signBit = ( bits >> 63 ) != 0;
	const bool expAllOnes = ( bits & exponentMask ) == exponentMask;
	const bool mantZero = ( bits & mantissaMask ) == 0;

	if ( expAllOnes )
	{
		if ( ! mantZero )
		{
			// NaN
			if ( p + 3 > last )
			{
				return { last, false };
			}
			*p++ = 'n'; *p++ = 'a'; *p++ = 'n';
			return { p, true };
		}

		// +/-Inf
		if ( signBit )
		{
			if ( p + 4 > last )
			{
				return { last, false };
			}
			*p++ = '-'; *p++ = 'i'; *p++ = 'n'; *p++ = 'f';
		}
		else
		{
			if ( p + 3 > last )
			{
				return { last, false };
			}
			*p++ = 'i'; *p++ = 'n'; *p++ = 'f';
		}
		return { p, true };
	}

	// -------------------------------------------------------------------------
	// Sign (negative zero is printed as "0.000000")
	// -------------------------------------------------------------------------

	if ( signBit && value != 0.0 )
	{
		if ( p >= last )
		{
			return { last, false };
		}
		*p++ = '-';
		value = -value;
	}

	// -------------------------------------------------------------------------
	// Split into integer and fractional parts.
	//
	// Split before scaling to avoid uint64 overflow for large values.
	// Values whose integer part exceeds uint64 max (~1.84e19) are clamped.
	// The fractional part is always in [0.0, 1.0) so scaling by 1e6 always
	// fits in uint64.  Rounding at 6dp may carry into the integer part
	// (e.g. 1.9999999 -> intPart=2, fracPart=0) which is handled explicitly.
	// -------------------------------------------------------------------------

	constexpr double UINT64_MAX_D = 18446744073709551615.0;
	constexpr double FRAC_SCALE = 1000000.0;
	constexpr std::uint64_t FRAC_MOD = 1000000ULL;

	const std::uint64_t intPart = value >= UINT64_MAX_D
		? 0xFFFFFFFFFFFFFFFFULL
		: static_cast< std::uint64_t >( value );

	const double frac = value - static_cast< double >( intPart );
	std::uint64_t fracPart = static_cast< std::uint64_t >( frac * FRAC_SCALE + 0.5 );

	// handle carry from rounding
	std::uint64_t adjustedInt = intPart;
	if ( fracPart >= FRAC_MOD )
	{
		fracPart -= FRAC_MOD;
		adjustedInt = intPart < 0xFFFFFFFFFFFFFFFFULL ? intPart + 1 : intPart;
	}

	// -------------------------------------------------------------------------
	// Integer part
	// -------------------------------------------------------------------------

	p = details::writeUInt( p, last, adjustedInt );
	if ( p == nullptr )
	{
		return { last, false };
	}

	// -------------------------------------------------------------------------
	// Decimal point + 6 fractional digits (zero-padded)
	// -------------------------------------------------------------------------

	if ( p + 7 > last )
	{
		// '.' + 6 digits
		return { last, false };
	}

	*p++ = '.';

	char fracBuf[ 6 ];
	std::uint64_t rem = fracPart;
	for ( int i = 5; i >= 0; --i )
	{
		fracBuf[ i ] = static_cast< char >( '0' + rem % 10 );
		rem /= 10;
	}
	for ( int i = 0; i < 6; ++i )
	{
		*p++ = fracBuf[ i ];
	}

	return { p, true };

#endif // NOVA_HAS_CHARCONV
}

/**
 * @brief Convert a float to its decimal string representation.
 *
 * Promotes to double and delegates to the double overload.  Provided so
 * callers need no explicit cast.
 *
 * @param first start of the output buffer
 * @param last one past the end of the output buffer; reserve at least
 *             32 bytes to cover the worst case
 * @param value value to convert
 * @return FloatToCharsResult - see floatToChars(double) for details
 */
inline FloatToCharsResult floatToChars( char* first, char* last, float value ) noexcept
{
	return floatToChars( first, last, static_cast< double >( value ) );
}

} // namespace platform
} // namespace nova
} // namespace kmac

#endif // KMAC_NOVA_PLATFORM_FLOAT_TO_CHARS_H
