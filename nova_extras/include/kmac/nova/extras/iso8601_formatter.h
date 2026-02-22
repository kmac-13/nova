#pragma once
#ifndef KMAC_NOVA_EXTRAS_ISO8601_FORMATTER_H
#define KMAC_NOVA_EXTRAS_ISO8601_FORMATTER_H

#include "formatter.h"
#include "buffer.h"

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
 * The timestamp in the Record is assumed to be in nanoseconds — the caller
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
 *  1. **ultra-fast passthrough** — if tag, file, and function are all empty
 *     (pre-formatted message), the raw message bytes are appended directly
 *     with no framing.
 *
 *  2. **fast path** — if the complete formatted record fits within 480 bytes
 *     and the output buffer has sufficient remaining space, the entire record
 *     is assembled into a 512-byte stack buffer and copied to the output in a
 *     single append.  This eliminates per-field boundary checks.
 *
 *  3. **slow path** — a resumable state machine that writes one field at a
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
	enum class Stage
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

	Stage _stage;         ///< current slow-path position; reset to TimestampWithSpace by begin()
	std::size_t _offset;  ///< byte offset within the field currently being written

	// pre-formatted timestamp built once in begin() and reused across format() calls
	char _timestampBuf[ 32 ];   ///< "YYYY-MM-DDTHH:MM:SS.mmmZ " (25 bytes used)
	std::size_t _timestampLen;  ///< actual byte count in _timestampBuf

	// pre-formatted line number built once in begin(); includes a trailing space
	// so Stage::LineWithSpace writes "42 " as a single append
	char _lineBuf[ 16 ];
	std::size_t _lineLen;

	// string lengths cached in begin() to avoid repeated strlen() in format()
	std::size_t _tagNameLen;
	std::size_t _fileNameLen;
	std::size_t _funcNameLen;

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
