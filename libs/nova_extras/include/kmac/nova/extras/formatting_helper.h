#pragma once
#ifndef KMAC_NOVA_EXTRAS_FORMATTING_HELPER_H
#define KMAC_NOVA_EXTRAS_FORMATTING_HELPER_H

/**
 * @file formatting_helper.h
 * @brief Shared formatting utilities for Nova formatters.
 *
 * ✅ SAFE FOR SAFETY-CRITICAL SYSTEMS
 * ✅ NO HEAP ALLOCATION
 * ✅ NO STDLIB DEPENDENCY
 *
 * FormattingHelper provides static methods that format common Record fields
 * into caller-supplied char buffers.  All methods write into a buffer pointed
 * to by @p out and return a pointer to the byte immediately after the last
 * byte written, allowing call sites to chain writes without length arithmetic:
 *
 *   char* out = buf;
 *   out = FormattingHelper::formatTimestamp( out, record.timestamp );
 *   out = FormattingHelper::formatTagId( out, record.tagId );
 *   std::size_t len = static_cast< std::size_t >( out - buf );
 *
 * The caller is responsible for ensuring the destination buffer is large
 * enough.  Required sizes per method are documented on each method.
 *
 * Lookup tables:
 *   DIGITS_2 and DIGITS_3 are declared in the detail namespace and defined
 *   in formatting_helper.cpp.  They are shared across all formatters that
 *   include this header, eliminating the per-formatter duplicates that
 *   previously existed in iso8601_formatter.cpp, custom_formatter.h,
 *   json_formatter.cpp, xml_formatter.cpp, and csv_formatter.cpp.
 */

#include <cstddef>
#include <cstdint>

namespace kmac {
namespace nova {
namespace extras {

namespace detail {

/**
 * @brief Lookup table for two-digit decimal strings "00".."99".
 *
 * Declared here and defined in formatting_helper.cpp.  All formatters that
 * previously defined their own copy should use this instead.
 *
 * NOLINT NOTE: 2D string literal lookup table; converting to
 * std::array<std::array> would require replacing all string literal
 * initialisers with explicit char lists, adding significant noise.
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
// extern const char DIGITS_2[ 100 ][ 3 ];

/**
 * @brief Lookup table for three-digit decimal strings "000".."999".
 *
 * Declared here and defined in formatting_helper.cpp.
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
// extern const char DIGITS_3[ 1000 ][ 4 ];

/**
 * @brief Lowercase hex digit lookup table.
 *
 * Declared here and defined in formatting_helper.cpp.
 */
// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
// extern const char HEX_CHARS[ 16 ];

} // namespace detail

/**
 * @brief Static formatting utilities shared by Nova formatters.
 *
 * All methods write into a caller-supplied buffer and return a pointer past
 * the last byte written.  The caller must ensure sufficient buffer capacity;
 * required sizes are documented on each method.
 */
class FormattingHelper
{
public:
	FormattingHelper() = delete;

	/**
	 * @brief Format a nanosecond UTC timestamp as ISO 8601 with millisecond
	 * precision.
	 *
	 * Writes exactly 24 bytes in the form "YYYY-MM-DDTHH:MM:SS.mmmZ" with
	 * no null terminator, no trailing space, and no surrounding delimiters.
	 * The caller adds any surrounding characters (spaces, quotes, tags) after
	 * the returned pointer.
	 *
	 * Minimum buffer requirement: 24 bytes.
	 *
	 * @param out destination buffer; must have at least 24 bytes available
	 * @param timestamp nanoseconds since the Unix epoch (UTC)
	 * @return pointer to the byte immediately after the last byte written
	 */
	static char* formatTimestamp( char* out, std::uint64_t timestamp ) noexcept;

	/**
	 * @brief Format a 64-bit tag identifier as a 16-character lowercase hex string.
	 *
	 * Writes exactly 16 bytes with no null terminator and no surrounding
	 * delimiters.
	 *
	 * Minimum buffer requirement: 16 bytes.
	 *
	 * @param out destination buffer; must have at least 16 bytes available
	 * @param tagId 64-bit tag identifier
	 * @return pointer to the byte immediately after the last byte written
	 */
	static char* formatTagId( char* out, std::uint64_t tagId ) noexcept;

	/**
	 * @brief Format a uint32_t line number as a decimal string.
	 *
	 * Writes 1-10 bytes (no leading zeros, no null terminator, no trailing
	 * space).  The number of bytes written depends on the value.
	 *
	 * Minimum buffer requirement: 10 bytes.
	 *
	 * @param out destination buffer; must have at least 10 bytes available
	 * @param line source line number
	 * @return pointer to the byte immediately after the last byte written
	 */
	static char* formatLine( char* out, std::uint32_t line ) noexcept;
};

} // namespace extras
} // namespace nova
} // namespace kmac

#endif // KMAC_NOVA_EXTRAS_FORMATTING_HELPER_H
