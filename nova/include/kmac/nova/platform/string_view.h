#pragma once
#ifndef KMAC_NOVA_PLATFORM_STRING_VIEW_H
#define KMAC_NOVA_PLATFORM_STRING_VIEW_H

#include "config.h"

#include <cstddef>
#include <cstring>

// include std::string_view outside namespace to avoid pollution
#if NOVA_HAS_STD_STRING_VIEW
#include <string_view>
#endif

/**
 * @file string_view.h
 * @brief Non-owning string reference for environments that don't include
 * std::string_view.
 *
 * Provides a uniform StringView type that aliases std::string_view on
 * hosted targets and a minimal (pointer, length) substitute on bare-metal
 * toolchains where <string_view> is absent (e.g. arm-none-eabi with
 * newlib-nano).
 *
 * Only the interface used by Nova's builders is provided in the fallback:
 * - construction from (const char*, std::size_t)
 * - construction from a null-terminated const char* (computes length via strlen)
 * - data()   - pointer to the first character
 * - length() - number of characters (not including any null terminator)
 * - empty()  - true if length() == 0
 *
 * No heap allocation.  C++14 compatible.
 */

namespace kmac::nova::platform
{

#if NOVA_HAS_STD_STRING_VIEW

using StringView = std::string_view;

#else // bare-metal fallback

/**
 * @brief Minimal non-owning string_view reference for bare-metal targets.
 *
 * Provides the subset of the std::string_view interface used by Nova's
 * record builders.  Does not own the pointed-to data; the caller is
 * responsible for ensuring the referenced memory remains valid.
 */
class StringView
{
private:
	const char* _data;
	std::size_t _length;

public:
	/**
	 * @brief Construct an empty StringView.
	 */
	constexpr StringView() noexcept;

	/**
	 * @brief Construct from a pointer and explicit length.
	 *
	 * @param data    pointer to the first character (need not be null-terminated)
	 * @param length  number of characters
	 */
	constexpr StringView( const char* data, std::size_t length ) noexcept;

	/**
	 * @brief Construct from a null-terminated string.
	 *
	 * @param str  null-terminated string; must not be nullptr
	 */
	StringView( const char* str ) noexcept;  // NOLINT(google-explicit-constructor)

	/**
	 * @brief Pointer to the first character.
	 *
	 * @note The string is not guaranteed to be null-terminated.
	 */
	constexpr const char* data() const noexcept;

	/**
	 * @brief Number of characters in the view.
	 */
	constexpr std::size_t length() const noexcept;

	/**
	 * @brief Number of characters in the view (alias for length()).
	 */
	constexpr std::size_t size() const noexcept;

	/**
	 * @brief True if the view has zero length.
	 */
	constexpr bool empty() const noexcept;
};

constexpr StringView::StringView() noexcept
	: _data( nullptr )
	, _length( 0 )
{
}

constexpr StringView::StringView( const char* data, std::size_t length ) noexcept
	: _data( data )
	, _length( length )
{
}

StringView::StringView( const char* str ) noexcept  // NOLINT(google-explicit-constructor)
	: _data( str )
	, _length( str != nullptr ? std::strlen( str ) : 0 )
{
}

constexpr const char* StringView::data() const noexcept
{
	return _data;
}

constexpr std::size_t StringView::length() const noexcept
{
	return _length;
}

constexpr std::size_t StringView::size() const noexcept
{
	return _length;
}

constexpr bool StringView::empty() const noexcept
{
	return _length == 0;
}

#endif // NOVA_HAS_STD_STRING_VIEW

} // namespace kmac::nova::platform

#endif // KMAC_NOVA_PLATFORM_STRING_VIEW_H
