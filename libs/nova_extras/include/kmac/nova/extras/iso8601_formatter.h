#pragma once
#ifndef KMAC_NOVA_EXTRAS_ISO8601_FORMATTER_H
#define KMAC_NOVA_EXTRAS_ISO8601_FORMATTER_H

#include "formatter.h"
#include "buffer.h"

#include <kmac/nova/platform/array.h>

#include <cstdint>
#include <ctime>

namespace kmac::nova::extras
{

/**
 * @brief Formats log records as ISO 8601 timestamped lines.
 *
 * ✅ SAFE FOR SAFETY-CRITICAL SYSTEMS
 *
 * This is NOT a configurable formatter.  Output from this formatter takes
 * the form:
 * @code
 *   2025-02-07T12:34:56.789Z [TAG] file.cpp:42 functionName - message text
 * @endcode
 *
 * Timestamp precision is milliseconds, always expressed in UTC (Z suffix).
 * The timestamp in the Record is assumed to be in nanoseconds - the caller
 * controls the clock source via logger_traits<Tag>::timestamp().
 *
 * ### Design
 *
 * All metadata (timestamp, line number, string lengths) is pre-computed in
 * begin() so that format() only performs memcpy operations with no runtime
 * arithmetic.  This keeps format() on the hot path as cheap as possible.
 *
 * format() has three execution paths ordered from fastest to slowest:
 *
 *  1. **ultra-fast passthrough** - if tag, file, and function are all empty
 *     (pre-formatted message), the raw message bytes are appended directly
 *     with no framing.
 *
 *  2. **fast path** - if the complete formatted record fits within 480 bytes
 *     and the output buffer has sufficient remaining space, the entire record
 *     is assembled into a 512-byte stack buffer and copied to the output in a
 *     single append.  This eliminates per-field boundary checks.
 *
 *  3. **slow path** - a resumable state machine that writes one field at a
 *     time.  Used for large records (> 480 bytes total) or when the output
 *     buffer fills mid-record and format() must be called again to continue.
 *
 * ### Thread safety
 *
 * Not thread-safe.  Each thread must own its own instance of ISO8601Formatter.
 */
class ISO8601Formatter final : public Formatter
{
private:
	// fields written to the output buffer in this order by the slow path
	enum class Stage : std::uint8_t
	{
		TimestampWithSpace,  ///< "2025-02-07T12:34:56.789Z "
		OpenBracket,         ///< "["
		Tag,                 ///< tag name string
		CloseBracket,        ///< "]"
		SpaceAfterTag,       ///< " "
		File,                ///< source file name
		Colon,               ///< ":"
		LineWithSpace,       ///< line number followed by a space
		Function,            ///< function name
		Separator,           ///< " - "
		Message,             ///< message body
		Newline,             ///< "\n"
		Done                 ///< record fully written
	};

	Stage _stage = Stage::Done;  ///< current slow-path position; reset to TimestampWithSpace by begin()
	std::size_t _offset = 0;     ///< byte offset within the field currently being written

	// pre-formatted timestamp built once in begin() and reused across format() calls
	platform::Array< char, 32 > _timestampBuf { };  ///< "YYYY-MM-DDTHH:MM:SS.mmmZ " (25 bytes used)
	std::size_t _timestampLen = 0;                  ///< actual byte count in _timestampBuf

	// pre-formatted line number built once in begin(); includes a trailing space
	// so Stage::LineWithSpace writes "42 " as a single append
	platform::Array< char, 16 > _lineBuf { };
	std::size_t _lineLen = 0;

	// string lengths cached in begin() to avoid repeated strlen() in format()
	std::size_t _tagNameLen = 0;
	std::size_t _fileNameLen = 0;
	std::size_t _funcNameLen = 0;

public:
	ISO8601Formatter() noexcept;

	/**
	 * @brief Pre-computes all per-record metadata before format() is called.
	 *
	 * Converts the record's nanosecond timestamp to an ISO 8601 string,
	 * converts the line number to decimal with a trailing space, and caches
	 * the byte lengths of the tag, file, and function name strings.
	 *
	 * Must be called exactly once per record before any format() calls.
	 *
	 * @param record the record about to be formatted
	 */
	void begin( const kmac::nova::Record& record ) noexcept override;

	/**
	 * @brief Writes as much of the formatted record as fits into @p buffer.
	 *
	 * Returns true when the record is complete, false if the buffer filled
	 * before all fields could be written.  In the false case, the formatter
	 * suspends at its current Stage and resumes from that point on the next
	 * call with a fresh buffer.
	 *
	 * @param record the record being formatted (same instance passed to begin())
	 * @param buffer destination buffer; may be partially full on entry
	 * @return true if the record is fully written, false if more buffer space is needed
	 */
	bool format( const kmac::nova::Record& record, Buffer& buffer ) noexcept override;

private:
	/**
	 * @brief Check if the record qualifies for passthrough formatting.
	 *
	 * Returns true when the record has no tag, file, or function name,
	 * indicating a pre-formatted message that should be written directly
	 * to the buffer without any prefix or decoration.
	 *
	 * @return true if passthrough formatting should be used
	 */
	bool isPassthrough() const noexcept;

	/**
	 * @brief Write a pre-formatted message directly to the buffer.
	 *
	 * Used when the record has no tag, file, or function name.  Appends
	 * the raw message bytes without any timestamp prefix or source location.
	 *
	 * @param record record containing the pre-formatted message
	 * @param buffer destination buffer
	 * @return true if formatting is complete, false if buffer is full
	 */
	bool formatPassthrough( const kmac::nova::Record& record, Buffer& buffer ) noexcept;

	/**
	 * @brief Attempt to format a complete record in a single pass.
	 *
	 * Formats the full record into a 512-byte stack-allocated temporary buffer
	 * and copies it to the destination in one append call, avoiding the overhead
	 * of the stage machine for the common case.  Only attempted when the record
	 * fits within 480 bytes and sufficient buffer space is available.
	 *
	 * @param record record to format
	 * @param buffer destination buffer
	 * @return true if the record was formatted and written successfully,
	 *   false if the record is too large or buffer space is insufficient
	 *   (caller should fall through to formatSlow)
	 */
	bool tryFormatFast( const kmac::nova::Record& record, Buffer& buffer ) noexcept;

	/**
	 * @brief Format a record incrementally using the stage machine.
	 *
	 * Handles records that are too large for the fast path, or resumes
	 * formatting after a partial write.  Each stage appends one field to
	 * the buffer and suspends if the buffer is full, allowing the caller
	 * to flush and retry.  Stages fall through in sequence:
	 * TimestampWithSpace -> OpenBracket -> Tag -> CloseBracket -> SpaceAfterTag
	 * -> File -> Colon -> LineWithSpace -> Function -> Separator -> Message
	 * -> Newline -> Done
	 *
	 * @param record record to format
	 * @param buffer destination buffer
	 * @return true if formatting reached Stage::Done, false if buffer is full
	 */
	bool formatSlow( const kmac::nova::Record& record, Buffer& buffer ) noexcept;

	/**
	 * @brief Converts a nanosecond timestamp to "YYYY-MM-DDTHH:MM:SS.mmmZ " in
	 * _timestampBuf and sets _timestampLen.
	 *
	 * Uses lookup tables (DIGITS_2, DIGITS_3) to avoid division in the hot path.
	 * The trailing space is included in the formatted output.
	 *
	 * @param timestamp nanoseconds since the Unix epoch (UTC)
	 */
	void buildTimestamp( std::uint64_t timestamp ) noexcept;
};

} // namespace kmac::nova::extras

#endif // KMAC_NOVA_EXTRAS_ISO8601_FORMATTER_H
