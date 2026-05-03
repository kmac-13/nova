#pragma once
#ifndef KMAC_NOVA_EXTRAS_JSON_FORMATTER_H
#define KMAC_NOVA_EXTRAS_JSON_FORMATTER_H

/**
 * @file json_formatter.h
 * @brief Newline-delimited JSON (NDJSON) formatter for Nova.
 *
 * ✅ SAFE FOR SAFETY-CRITICAL SYSTEMS
 * ✅ NO HEAP ALLOCATION
 * ✅ NO STDLIB DEPENDENCY
 *
 * Provides JsonFormatter, which formats each log record as a single JSON
 * object followed by a newline (NDJSON / JSON Lines format).  One object
 * per line makes the output streamable and compatible with tools such as
 * jq, Elasticsearch, Loki, and Grafana.
 *
 * Output format:
 *
 *   {"ts":"2025-02-07T12:34:56.789Z","tagId":"a1b2c3d4e5f60718","tag":"INFO","file":"main.cpp","function":"main","line":42,"message":"hello world"}\n
 *
 * Fields:
 *   ts        - ISO 8601 UTC timestamp with millisecond precision
 *   tagId     - 16-character lowercase hex representation of the 64-bit tag hash
 *   tag       - tag name string (JSON-escaped); null if record.tag is null
 *   file      - source file name (JSON-escaped); null if record.file is null
 *   function  - function name (JSON-escaped); null if record.function is null
 *   line      - source line number (integer); always emitted
 *   message   - log message body (JSON-escaped); null if record.message is null
 *
 * Null handling:
 *   String fields whose pointer is null are emitted as JSON null rather than
 *   an empty string.  Empty strings (non-null pointer, zero length) are emitted
 *   as "".  This applies to tag, file, function, and message.  The line field
 *   is always emitted as an integer since it has no null state.
 *
 * String escaping:
 *   All string fields are JSON-escaped per RFC 8259.  The following
 *   characters are escaped:
 *     "               ->  \"
 *     \               ->  \\
 *     backspace       ->  \b
 *     form feed       ->  \f
 *     newline         ->  \n
 *     carriage return ->  \r
 *     tab             ->  \t
 *   Control characters U+0000-U+001F (other than the above) are emitted as
 *   \uXXXX sequences.  Non-ASCII bytes are passed through unmodified - the
 *   formatter does not perform UTF-8 validation.
 *
 * Thread safety:
 *   Not thread-safe.  Each sink using this formatter must be protected with
 *   (e.g.) a SynchronizedSink in projects using multi-threaded logging.
 *
 * Usage:
 *
 *   JsonFormatter formatter;
 *   OStreamSink osSink( std::cout );
 *   FormattingSink<> sink( osSink, formatter );
 *
 *   ScopedConfigurator config;
 *   config.bind< NetworkTag >( &sink );
 *
 *   NOVA_LOG( NetworkTag ) << "connected to " << host;
 *   // stdout: {"ts":"2025-02-07T12:34:56.789Z","tagId":"a1b2c3d4e5f60718","tag":"NETWORK","file":"net.cpp","function":"connect","line":42,"message":"connected to 192.168.1.1"}\n
 */

#include "buffer.h"
#include "formatter.h"

#include <kmac/nova/platform/array.h>
#include <kmac/nova/record.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>

namespace kmac {
namespace nova {
namespace extras {

/**
 * @brief Formats log records as newline-delimited JSON (NDJSON).
 *
 * See file-level documentation for output format and escaping rules.
 */
class JsonFormatter final : public Formatter
{
private:
	// top-level stage - one per record field, in output order
	enum class Stage : std::uint8_t
	{
		Timestamp,  ///< {"ts":"2025-02-07T12:34:56.789Z" (pre-formatted, one shot)
		TagId,      ///< ,"tagId":"a1b2c3d4e5f60718" (pre-formatted, one shot)
		Tag,        ///< ,"tag":"..."  or  ,"tag":null
		File,       ///< ,"file":"..."  or  ,"file":null
		Function,   ///< ,"function":"..."  or  ,"function":null
		Line,       ///< ,"line":42  (pre-formatted, one shot)
		Message,    ///< ,"message":"..."  or  ,"message":null
		Suffix,     ///< }\n
		Done
	};

	// sub-stage for variable-length string fields - tracks progress through
	// the key, optional opening quote, escaped content, and closing quote
	enum class FieldStage : std::uint8_t
	{
		Key,        ///< writing the JSON key + either null or opening quote
		Content,    ///< writing JSON-escaped content bytes (resumable)
		CloseQuote  ///< writing the closing '"'
	};

	Stage _stage = Stage::Done;
	FieldStage _field = FieldStage::Key;
	std::size_t _offset = 0;  ///< byte offset within content currently being written

	// pre-formatted complete field strings built in begin() - written in one shot
	platform::Array< char, 40 > _tsBuf {};     ///< {"ts":"YYYY-MM-DDTHH:MM:SS.mmmZ"
	std::size_t _tsLen = 0;

	platform::Array< char, 32 > _tagIdBuf {};  ///< ,"tagId":"a1b2c3d4e5f60718"
	std::size_t _tagIdLen = 0;

	platform::Array< char, 20 > _lineBuf {};   ///< ,"line":decimal
	std::size_t _lineLen = 0;

	// string lengths and null flags cached in begin()
	std::size_t _tagLen = 0;
	std::size_t _fileLen = 0;
	std::size_t _funcLen = 0;
	std::size_t _msgLen = 0;

	bool _tagIsNull = false;   ///< true when record.tag is null
	bool _fileIsNull = false;  ///< true when record.file is null
	bool _funcIsNull = false;  ///< true when record.function is null
	bool _msgIsNull = false;   ///< true when record.message is null

public:
	JsonFormatter() noexcept = default;

	/**
	 * @brief Pre-computes all per-record metadata before format() is called.
	 *
	 * Pre-formats the ts, tagId, and line fields and caches string lengths
	 * and null flags for variable-length fields.  Must be called exactly once
	 * per record before any format() calls.
	 *
	 * @param record the record about to be formatted
	 */
	void begin( const kmac::nova::Record& record ) noexcept override;

	/**
	 * @brief Writes as much of the formatted JSON record as fits into buffer.
	 *
	 * Returns true when the record is complete, false if the buffer filled
	 * before all fields were written.  The formatter resumes from its current
	 * stage on the next call.
	 *
	 * @param record the record being formatted (same instance passed to begin())
	 * @param buffer destination buffer
	 * @return true if the record is fully written, false if more space is needed
	 */
	bool format( const kmac::nova::Record& record, Buffer& buffer ) noexcept override;

private:
	/**
	 * @brief Pre-format the opening brace and complete ts field into _tsBuf.
	 *
	 * Produces: {"ts":"YYYY-MM-DDTHH:MM:SS.mmmZ"
	 *
	 * @param timestamp nanoseconds since the Unix epoch (UTC)
	 */
	void buildTs( std::uint64_t timestamp ) noexcept;

	/**
	 * @brief Pre-format the complete tagId field into _tagIdBuf.
	 *
	 * Produces: ,"tagId":"a1b2c3d4e5f60718"
	 *
	 * @param tagId 64-bit tag identifier
	 */
	void buildTagId( std::uint64_t tagId ) noexcept;

	/**
	 * @brief Pre-format the complete line field into _lineBuf.
	 *
	 * Produces: ,"line":decimal
	 *
	 * @param line source line number
	 */
	void buildLine( std::uint32_t line ) noexcept;

	/**
	 * @brief Append a JSON string field to the buffer, resuming from _field/_offset.
	 *
	 * Writes the key prefix, then either the JSON null literal (if isNull) or
	 * the JSON-escaped string value wrapped in double quotes.  Uses _field and
	 * _offset to track progress across calls when the buffer fills mid-field.
	 * Returns true when the field is fully written.
	 *
	 * The key and nullKey lengths are deduced from the string literal array
	 * sizes at compile time, eliminating the risk of hardcoded length errors.
	 *
	 * @tparam KeyN size of key array (deduced)
	 * @tparam NullN size of nullKey array (deduced)
	 * @param key JSON key prefix including opening quote, e.g. ",\"tag\":\""
	 * @param nullKey JSON key + null value, e.g. ",\"tag\":null"
	 * @param str pointer to string content; ignored when isNull is true
	 * @param len byte length of str
	 * @param isNull true if str is null - emit null literal
	 * @param buffer destination buffer
	 * @return true if the field was fully written
	 */
	template< std::size_t KeyN, std::size_t NullN >
	bool appendField(
		const char ( &key )[ KeyN ],       // NOLINT(cppcoreguidelines-avoid-c-arrays)
		const char ( &nullKey )[ NullN ],  // NOLINT(cppcoreguidelines-avoid-c-arrays)
		const char* str,
		std::size_t len,
		bool isNull,
		Buffer& buffer ) noexcept;

	/**
	 * @brief Append a JSON-escaped string to the buffer, resuming from offset.
	 *
	 * Writes bytes from str[ offset ] onward, applying JSON escape sequences.
	 * Updates offset to reflect progress.  Returns true when the full string
	 * has been written.
	 *
	 * @param str pointer to string bytes (need not be null-terminated)
	 * @param len byte length of the string
	 * @param offset current write position within str; updated on return
	 * @param buffer destination buffer
	 * @return true if the string was fully written
	 */
	static bool appendEscaped(
		const char* str,
		std::size_t len,
		std::size_t& offset,
		Buffer& buffer ) noexcept;

	/**
	 * @brief Incremental formatter - drives the stage machine.
	 *
	 * Suspends and returns false if the buffer fills mid-record, resuming
	 * from _stage on the next call.
	 *
	 * @param record record being formatted
	 * @param buffer destination buffer
	 * @return true if Stage::Done was reached
	 */
	bool formatSlow( const kmac::nova::Record& record, Buffer& buffer ) noexcept;
};

template< std::size_t KeyN, std::size_t NullN >
inline bool JsonFormatter::appendField(
	const char ( &key )[ KeyN ],       // NOLINT(cppcoreguidelines-avoid-c-arrays)
	const char ( &nullKey )[ NullN ],  // NOLINT(cppcoreguidelines-avoid-c-arrays)
	const char* str,
	std::size_t len,
	bool isNull,
	Buffer& buffer ) noexcept
{
	static_assert( KeyN > 0 );
	static_assert( NullN > 0 );

	switch ( _field )
	{
	case FieldStage::Key:
		if ( isNull )
		{
			return buffer.append( nullKey, NullN - 1 );
		}
		if ( ! buffer.append( key, KeyN - 1 ) )
		{
			return false;
		}
		_field  = FieldStage::Content;
		_offset = 0;
		// [[fallthrough]];

	case FieldStage::Content:
		if ( ! appendEscaped( str, len, _offset, buffer ) )
		{
			return false;
		}
		_field = FieldStage::CloseQuote;
		// [[fallthrough]];

	case FieldStage::CloseQuote:
		if ( ! buffer.appendChar( '"' ) )
		{
			return false;
		}
		break;
	}

	return true;
}

} // namespace extras
} // namespace nova
} // namespace kmac

#endif // KMAC_NOVA_EXTRAS_JSON_FORMATTER_H
