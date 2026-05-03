#pragma once
#ifndef KMAC_NOVA_EXTRAS_CSV_FORMATTER_H
#define KMAC_NOVA_EXTRAS_CSV_FORMATTER_H

/**
 * @file csv_formatter.h
 * @brief RFC 4180 CSV formatter for Nova.
 *
 * ✅ SAFE FOR SAFETY-CRITICAL SYSTEMS
 * ✅ NO HEAP ALLOCATION
 * ✅ NO STDLIB DEPENDENCY
 *
 * Provides CsvFormatter, which formats each log record as a single RFC 4180
 * CSV row followed by CRLF.  Intended for tooling consumption - import into
 * spreadsheets, pandas, R, or similar tools.
 *
 * Header row:
 *   CsvFormatter does not emit a header row.  If one is required, write it
 *   directly to the output destination before binding any tags, to avoid
 *   interleaving with log output:
 *
 *   std::ofstream file( "log.csv" );
 *   file << "ts,tagId,tag,file,function,line,message\r\n";
 *
 *   CsvFormatter formatter;
 *   OStreamSink osSink( file );
 *   FormattingSink<> sink( osSink, formatter );
 *
 *   ScopedConfigurator<> config;
 *   config.bind< MyTag >( &sink );  // bind after writing header
 *
 * Output format (one row per record, CRLF line ending per RFC 4180):
 *
 *   2025-02-07T12:34:56.789Z,a1b2c3d4e5f60718,INFO,main.cpp,main,42,hello world\r\n
 *
 * Columns (fixed order, always emitted):
 *   ts        - ISO 8601 UTC timestamp with millisecond precision
 *   tagId     - 16-character lowercase hex representation of the 64-bit tag hash
 *   tag       - tag name string
 *   file      - source file name
 *   function  - function name
 *   line      - source line number as decimal integer
 *   message   - log message body
 *
 * All seven columns are always emitted so that CSV consumers can rely on a
 * fixed column count.
 *
 * Null handling:
 *   String fields whose pointer is null are emitted as an unquoted empty field
 *   (e.g. ,,).  Empty strings (non-null pointer, zero length) are emitted as
 *   quoted empty fields (e.g. ,"",).  This distinction preserves the semantic
 *   difference between null and empty string for downstream tools that care.
 *   The line field is always emitted as a decimal integer since it has no null
 *   state.
 *
 * Quoting (RFC 4180):
 *   A field is quoted with double quotes if it contains the delimiter, a
 *   double quote, a carriage return, or a line feed.  Embedded double quotes
 *   are escaped by doubling them ("").  Newlines inside quoted fields are
 *   preserved as-is per RFC 4180 - note that some CSV parsers do not support
 *   embedded newlines; if this is a concern, ensure message content does not
 *   contain newlines.
 *
 * Delimiter:
 *   Defaults to comma (',').  A custom single-character delimiter can be
 *   supplied at construction - for example '\t' for tab-delimited output.
 *   The delimiter character itself triggers quoting if present in a field.
 *
 * Thread safety:
 *   Not thread-safe.  Each sink using this formatter must be protected with
 *   (e.g.) a SynchronizedSink in projects using multi-threaded logging.
 *
 * Usage - default comma delimiter:
 *
 *   CsvFormatter formatter;
 *   OStreamSink osSink( std::cout );
 *   FormattingSink<> sink( osSink, formatter );
 *
 *   ScopedConfigurator config;
 *   config.bind< NetworkTag >( &sink );
 *
 *   NOVA_LOG( NetworkTag ) << "connected";
 *   // stdout: 2025-02-07T12:34:56.789Z,a1b2c3d4e5f60718,NETWORK,net.cpp,connect,42,connected\r\n
 *
 * Usage - tab delimiter:
 *
 *   CsvFormatter formatter( '\t' );
 */

#include "buffer.h"
#include "formatter.h"

#include <kmac/nova/platform/array.h>
#include <kmac/nova/record.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace kmac {
namespace nova {
namespace extras {

/**
 * @brief Formats log records as RFC 4180 CSV rows.
 *
 * See file-level documentation for output format, quoting rules, and
 * delimiter configuration.
 */
class CsvFormatter final : public Formatter
{
private:
	// top-level stage - one per record field, in output order
	enum class Stage : std::uint8_t
	{
		Timestamp,  ///< 2025-02-07T12:34:56.789Z (no preceding delimiter)
		TagId,      ///< ,a1b2c3d4e5f60718
		Tag,        ///< ,<field>  or  ,  if null
		File,       ///< ,<field>  or  ,  if null
		Function,   ///< ,<field>  or  ,  if null
		Line,       ///< ,<decimal>
		Message,    ///< ,<field>  or  ,  if null
		Crlf,       ///< \r\n
		Done
	};

	// sub-stage for variable-length quoted fields - tracks progress through
	// the delimiter, optional opening quote, content bytes, and closing quote
	enum class FieldStage : std::uint8_t
	{
		Delimiter,  ///< writing the preceding delimiter and optional opening quote
		Content,    ///< writing field content bytes (resumable)
		CloseQuote  ///< writing the closing '"' (quoted fields only)
	};

	Stage _stage = Stage::Done;
	FieldStage _field = FieldStage::Delimiter;
	std::size_t _offset = 0;  ///< byte offset within Content currently being written

	char _delimiter;          ///< field delimiter character

	// pre-formatted fixed fields built in begin() - written in one shot
	platform::Array< char, 32 > _timestampBuf {};  ///< "YYYY-MM-DDTHH:MM:SS.mmmZ" (24 bytes)
	std::size_t _timestampLen = 0;

	platform::Array< char, 17 > _tagIdBuf {};  ///< 16 hex chars
	std::size_t _tagIdLen = 0;

	platform::Array< char, 12 > _lineBuf {};  ///< decimal line number
	std::size_t _lineLen = 0;

	// string lengths, null flags, and quoting flags cached in begin()
	std::size_t _tagLen = 0;
	std::size_t _fileLen = 0;
	std::size_t _funcLen = 0;
	std::size_t _msgLen = 0;

	bool _tagIsNull = false;   ///< true when record.tag is null - emit empty unquoted field
	bool _fileIsNull = false;  ///< true when record.file is null - emit empty unquoted field
	bool _funcIsNull = false;  ///< true when record.function is null - emit empty unquoted field
	bool _msgIsNull = false;   ///< true when record.message is null - emit empty unquoted field

	bool _tagNeedsQuoting = false;
	bool _fileNeedsQuoting = false;
	bool _funcNeedsQuoting = false;
	bool _msgNeedsQuoting = false;

public:
	/**
	 * @brief Construct with a field delimiter.
	 *
	 * @param delimiter single character separating fields (default: ',')
	 */
	explicit CsvFormatter( char delimiter = ',' ) noexcept;

	/**
	 * @brief Pre-computes all per-record metadata before format() is called.
	 *
	 * Formats the timestamp, tag ID hex string, and line number.  Scans all
	 * string fields to determine which require quoting.  Must be called
	 * exactly once per record before any format() calls.
	 *
	 * @param record the record about to be formatted
	 */
	void begin( const kmac::nova::Record& record ) noexcept override;

	/**
	 * @brief Writes as much of the formatted CSV row as fits into buffer.
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
	 * @brief Format the ISO 8601 UTC timestamp into _timestampBuf.
	 *
	 * @param timestamp nanoseconds since the Unix epoch (UTC)
	 */
	void buildTimestamp( std::uint64_t timestamp ) noexcept;

	/**
	 * @brief Format the tagId as a 16-character lowercase hex string into _tagIdBuf.
	 *
	 * @param tagId 64-bit tag identifier
	 */
	void buildTagId( std::uint64_t tagId ) noexcept;

	/**
	 * @brief Format the line number as a decimal string into _lineBuf.
	 *
	 * @param line source line number
	 */
	void buildLine( std::uint32_t line ) noexcept;

	/**
	 * @brief Return true if the string requires RFC 4180 quoting.
	 *
	 * A field must be quoted if it contains the delimiter, a double quote,
	 * a carriage return, or a line feed.
	 *
	 * @param str pointer to string bytes
	 * @param len byte length of the string
	 * @return true if the field must be quoted
	 */
	bool needsQuoting( const char* str, std::size_t len ) const noexcept;

	/**
	 * @brief Append a delimited CSV field to the buffer, resuming from _field/_offset.
	 *
	 * Writes the delimiter, then the field content.  If the field is null,
	 * only the delimiter is written (unquoted empty field).  If the field is
	 * empty (non-null, zero length), writes the delimiter followed by "".  If
	 * the field needs quoting, wraps the content in double quotes and escapes
	 * embedded double quotes by doubling them.
	 *
	 * Uses _field and _offset to track progress across calls when the buffer
	 * fills mid-field.  Returns true when the field is fully written.
	 *
	 * @param str pointer to field bytes; null means emit unquoted empty field
	 * @param len byte length of the field
	 * @param isNull true if str is null - emit delimiter only
	 * @param quoting true if the field must be quoted
	 * @param buffer destination buffer
	 * @return true if the field was fully written
	 */
	bool appendField(
		const char* str,
		std::size_t len,
		bool isNull,
		bool quoting,
		Buffer& buffer ) noexcept;

	/**
	 * @brief Write quoted field content bytes, doubling embedded double quotes.
	 *
	 * Resumes from _offset.  Returns true when all content bytes have been
	 * written.  The opening and closing quotes are written by appendField.
	 *
	 * @param str pointer to field content bytes
	 * @param len byte length of the field
	 * @param buffer destination buffer
	 * @return true if all content bytes were written
	 */
	bool writeQuotedContent(
		const char* str,
		std::size_t len,
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

} // namespace extras
} // namespace nova
} // namespace kmac

#endif // KMAC_NOVA_EXTRAS_CSV_FORMATTER_H
