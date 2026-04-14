#pragma once
#ifndef KMAC_NOVA_EXTRAS_MULTILINE_FORMATTER_H
#define KMAC_NOVA_EXTRAS_MULTILINE_FORMATTER_H

#include "buffer.h"
#include "multi_record_formatter.h"

#include <cstddef>

namespace kmac::nova
{
struct Record;
class Sink;
} // namespace kmac::nova

namespace kmac::nova::extras
{

/**
 * @brief Splits multi-line messages into separate records, one per line.
 *
 * ✅ SAFE FOR SAFETY-CRITICAL SYSTEMS
 *
 * MultilineFormatter breaks messages containing newlines into individual records,
 * with each line becoming a separate log entry. This is useful for:
 * - stack traces (emit each frame as a separate record)
 * - configuration dumps (emit each setting as a separate record)
 * - log aggregators that don't handle multi-line messages well
 * - making multi-line content searchable per-line
 * - pretty-printing structured data (JSON, XML, etc.)
 *
 * Features:
 * - preserves all record metadata (timestamp, file, line, function) on each line
 * - optionally adds line numbers to show ordering ([1/5], [2/5], ...)
 * - handles both \n and \r\n line endings
 * - empty lines can be preserved or skipped
 * - each line gets the same timestamp (from original record)
 *
 * Output format (with line numbers enabled):
 *   [TAG] file.cpp:42 func - [1/3] First line
 *   [TAG] file.cpp:42 func - [2/3] Second line
 *   [TAG] file.cpp:42 func - [3/3] Third line
 *
 * Output format (without line numbers):
 *   [TAG] file.cpp:42 func - First line
 *   [TAG] file.cpp:42 func - Second line
 *   [TAG] file.cpp:42 func - Third line
 *
 * Use cases:
 * - exception stack traces that span multiple lines
 * - configuration file dumps
 * - multi-line error messages from external tools
 * - pretty-printed JSON/XML for debugging
 *
 * Limitations:
 * - emits N records for N lines (overhead for many lines)
 * - line numbers limited to 9999 (more lines will overflow format)
 * - no automatic indentation or continuation markers
 * - all lines share same timestamp (may lose sub-line timing info)
 *
 * Performance:
 * - O(n) where n = message length
 * - two passes: count lines, then emit lines
 * - multiple sink->process() calls (one per line)
 *
 * Thread safety:
 * - not thread-safe (single-threaded use)
 * - downstream sink must handle rapid succession of records
 *
 * Usage example:
 *   MultilineFormatter formatter(true, false); // Line numbers, skip empty
 *
 *   Record record = makeRecord();
 *   record.message = "Exception caught:\n"
 *                  "  at processData() line 42\n"
 *                  "  at handleRequest() line 123\n"
 *                  "  at main() line 10";
 *
 *   OStreamSink console(std::cout);
 *   formatter.formatAndWrite(record, console);
 *
 * Typical output:
 *   [ERROR] main.cpp:15 (processData) - [1/4] Exception caught:
 *   [ERROR] main.cpp:15 (processData) - [2/4]   at processData() line 42
 *   [ERROR] main.cpp:15 (processData) - [3/4]   at handleRequest() line 123
 *   [ERROR] main.cpp:15 (processData) - [4/4]   at main() line 10
 *
 * Without line numbers:
 *   MultilineFormatter formatter(false, true); // No numbers, keep empty
 */
class MultilineFormatter final : public MultiRecordFormatter
{
private:
	bool _addLineNumbers;       ///< prepend [N/Total] to each line
	bool _preserveEmptyLines;   ///< emit records for empty lines

	static constexpr std::size_t MAX_LINE_NUMBER_PREFIX = 32; // "[9999/9999] "

public:
	/**
	 * @brief Construct a multiline formatter.
	 *
	 * @param addLineNumbers if true, prepends "[N/Total] " to each line
	 * @param preserveEmptyLines if true, emits records for empty lines; if false, skips them
	 *
	 * @note line numbers limited to 9999 (4 digits)
	 * @note empty line preservation affects blank lines only (not whitespace-only)
	 */
	explicit MultilineFormatter( bool addLineNumbers = true, bool preserveEmptyLines = false ) noexcept;

	/**
	 * @brief Get maximum chunk size for line processing.
	 *
	 * @return maximum bytes per line record
	 */
	std::size_t maxChunkSize() const noexcept override;

	/**
	 * @brief Split message into lines and emit separate records.
	 *
	 * Process:
	 * 1. count total number of lines
	 * 2. for each line:
	 *    - skip if empty and !preserveEmptyLines
	 *    - add [N/Total] prefix if addLineNumbers
	 *    - create new record with modified message
	 *    - call downstream.process()
	 *
	 * @param record original record with multi-line message
	 * @param downstream sink to receive per-line records
	 *
	 * @note calls downstream.process() once per line
	 * @note all emitted records share original timestamp
	 * @note handles both \n and \r\n line endings
	 */
	void formatAndWrite( const kmac::nova::Record& record, kmac::nova::Sink& downstream ) noexcept override;

private:
	/**
	 * @brief Count the number of lines in the message.
	 *
	 * @param message message text
	 * @param messageSize message length in bytes
	 * @return number of lines (including trailing line without newline)
	 */
	std::size_t countLines( const char* message, std::size_t messageSize ) const noexcept;

	/**
	 * @brief Write a "[lineNumber/totalLines] " prefix into buffer.
	 *
	 * @param buffer destination buffer
	 * @param lineNumber current line (1-based)
	 * @param totalLines total line count
	 */
	void formatLineNumberPrefix( Buffer& buffer, std::size_t lineNumber, std::size_t totalLines ) const noexcept;

	/**
	 * @brief Find the next line in the message.
	 *
	 * @param start pointer to start of current position
	 * @param end pointer to end of message
	 * @param outLineStart set to start of line
	 * @param outLineLength set to length of line (excluding newline)
	 * @return pointer to start of next line, or nullptr if no more lines
	 *
	 * @note handles both \n and \r\n
	 * @note sets outLineLength to 0 for empty lines
	 */
	const char* findNextLine(
		const char* start,
		const char* end,
		const char*& outLineStart,
		std::size_t& outLineLength
	) const noexcept;
};

} // namespace kmac::nova::extras

#endif // KMAC_NOVA_EXTRAS_MULTILINE_FORMATTER_H
