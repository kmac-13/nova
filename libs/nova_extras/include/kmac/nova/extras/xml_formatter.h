#pragma once
#ifndef KMAC_NOVA_EXTRAS_XML_FORMATTER_H
#define KMAC_NOVA_EXTRAS_XML_FORMATTER_H

/**
 * @file xml_formatter.h
 * @brief Single-record-per-line XML formatter for Nova.
 *
 * ✅ SAFE FOR SAFETY-CRITICAL SYSTEMS
 * ✅ NO HEAP ALLOCATION
 * ✅ NO STDLIB DEPENDENCY
 *
 * Provides XmlFormatter, which formats each log record as a single XML
 * element followed by a newline.  One element per line makes the output
 * streamable without requiring a wrapping document structure.
 *
 * No XML declaration or root element is emitted.  If a valid XML document
 * is required, wrap the output in a root element and add an XML declaration:
 *
 *   <?xml version="1.0" encoding="UTF-8"?>
 *   <log>
 *   [XmlFormatter output here]
 *   </log>
 *
 * Child elements vs attributes:
 *   All fields are represented as child elements rather than XML attributes.
 *   The XML 1.0 specification requires conforming parsers to normalize
 *   attribute values by replacing newlines and other whitespace characters
 *   with spaces.  This would silently corrupt log field values that contain
 *   newlines or tabs.  Furthermore, XML provides no backslash-style escape
 *   sequences equivalent to JSON - there is no way to preserve a literal
 *   newline in an attribute value through a conforming parser.  Child element
 *   content has no such normalization constraint; all legal XML characters
 *   survive a parse round-trip intact, making child elements the correct
 *   choice for fields that may contain arbitrary string data.
 *
 * Output format (one <record> element per line):
 *
 *   <record><ts>2025-02-07T12:34:56.789Z</ts><tagId>a1b2c3d4e5f60718</tagId><tag>INFO</tag><file>main.cpp</file><function>main</function><line>42</line><message>hello world</message></record>\n
 *
 * Fields (always emitted in this order):
 *   ts        - ISO 8601 UTC timestamp with millisecond precision; always emitted
 *   tagId     - 16-character lowercase hex tag hash; always emitted
 *   tag       - tag name string, XML-escaped; omitted if null, <tag/> if empty
 *   file      - source file name, XML-escaped; omitted if null, <file/> if empty
 *   function  - function name, XML-escaped; omitted if null, <function/> if empty
 *   line      - source line number as decimal integer; always emitted
 *   message   - log message body, XML-escaped; omitted if null, <message/> if empty
 *
 * Null handling:
 *   String fields whose pointer is null are omitted entirely since XML has no
 *   native null concept.  Empty strings (non-null pointer, zero length) are
 *   emitted as self-closing elements (e.g. <tag/>).  This applies to tag,
 *   file, function, and message.  The ts, tagId, and line fields are always
 *   emitted since they have no null state.
 *
 * String escaping:
 *   All string fields are XML-escaped per the XML 1.0 specification.  The
 *   following substitutions are made in element content:
 *     &   ->  &amp;
 *     <   ->  &lt;
 *     >   ->  &gt;
 *   Control characters U+0000-U+0008, U+000B-U+000C, U+000E-U+001F are
 *   illegal in XML 1.0 and cannot be represented even as numeric character
 *   references; these bytes are silently dropped.  U+0009 (tab), U+000A
 *   (LF), and U+000D (CR) are legal in XML 1.0 element content and are
 *   passed through unmodified.  Non-ASCII bytes are passed through
 *   unmodified - the formatter does not perform UTF-8 validation.
 *
 * Thread safety:
 *   Not thread-safe.  Each sink using this formatter must be protected with
 *   (e.g.) a SynchronizedSink in projects using multi-threaded logging.
 *
 * Usage:
 *
 *   XmlFormatter formatter;
 *   OStreamSink osSink( std::cout );
 *   FormattingSink<> sink( osSink, formatter );
 *
 *   ScopedConfigurator config;
 *   config.bind< NetworkTag >( &sink );
 *
 *   NOVA_LOG( NetworkTag ) << "connected to " << host;
 *   // stdout: <record><ts>2025-02-07T12:34:56.789Z</ts><tagId>a1b2c3d4e5f60718</tagId><tag>NETWORK</tag><file>net.cpp</file><function>connect</function><line>42</line><message>connected to 192.168.1.1</message></record>\n
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
 * @brief Formats log records as single-record-per-line XML with child elements.
 *
 * See file-level documentation for output format, child element rationale,
 * and escaping rules.
 */
class XmlFormatter final : public Formatter
{
private:
	// top-level stage - one per record field, in output order
	enum class Stage : std::uint8_t
	{
		OpenRecord,   ///< <record>
		Ts,           ///< <ts>2025-02-07T12:34:56.789Z</ts>  (pre-formatted, one shot)
		TagId,        ///< <tagId>a1b2c3d4e5f60718</tagId>    (pre-formatted, one shot)
		Tag,          ///< <tag>...</tag> or <tag/>; skipped if null
		File,         ///< <file>...</file> or <file/>; skipped if null
		Function,     ///< <function>...</function> or <function/>; skipped if null
		Line,         ///< <line>42</line>  (pre-formatted, one shot)
		Message,      ///< <message>...</message> or <message/>; skipped if null
		CloseRecord,  ///< </record>\n
		Done
	};

	// sub-stage for variable-length fields - tracks progress through
	// open tag, escaped content, and close tag
	enum class FieldStage : std::uint8_t
	{
		OpenTag,      ///< writing element open tag e.g. <tag>
		Content,      ///< writing XML-escaped content bytes (resumable)
		CloseTag      ///< writing element close tag e.g. </tag>
	};

	Stage _stage = Stage::Done;
	FieldStage _field = FieldStage::OpenTag;
	std::size_t _offset = 0;  ///< byte offset within content currently being written

	// pre-formatted complete elements built in begin() - written in one shot
	platform::Array< char, 40 > _tsBuf {};     ///< <ts>YYYY-MM-DDTHH:MM:SS.mmmZ</ts>
	std::size_t _tsLen = 0;

	platform::Array< char, 32 > _tagIdBuf {};  ///< <tagId>a1b2c3d4e5f60718</tagId>
	std::size_t _tagIdLen = 0;

	platform::Array< char, 24 > _lineBuf {};   ///< <line>decimal</line>
	std::size_t _lineLen = 0;

	// string lengths and null flags cached in begin()
	std::size_t _tagLen = 0;
	std::size_t _fileLen = 0;
	std::size_t _funcLen = 0;
	std::size_t _msgLen = 0;

	bool _tagIsNull = false;   ///< true when record.tag is null - element omitted
	bool _fileIsNull = false;  ///< true when record.file is null - element omitted
	bool _funcIsNull = false;  ///< true when record.function is null - element omitted
	bool _msgIsNull = false;   ///< true when record.message is null - element omitted

public:
	XmlFormatter() noexcept = default;

	/**
	 * @brief Pre-computes all per-record metadata before format() is called.
	 *
	 * Pre-formats the ts, tagId, and line elements and caches string lengths
	 * and null flags for variable-length fields.  Must be called exactly once
	 * per record before any format() calls.
	 *
	 * @param record the record about to be formatted
	 */
	void begin( const kmac::nova::Record& record ) noexcept override;

	/**
	 * @brief Writes as much of the formatted XML record as fits into buffer.
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
	 * @brief Pre-format the complete <ts>...</ts> element into _tsBuf.
	 *
	 * @param timestamp nanoseconds since the Unix epoch (UTC)
	 */
	void buildTs( std::uint64_t timestamp ) noexcept;

	/**
	 * @brief Pre-format the complete <tagId>...</tagId> element into _tagIdBuf.
	 *
	 * @param tagId 64-bit tag identifier
	 */
	void buildTagId( std::uint64_t tagId ) noexcept;

	/**
	 * @brief Pre-format the complete <line>...</line> element into _lineBuf.
	 *
	 * @param line source line number
	 */
	void buildLine( std::uint32_t line ) noexcept;

	/**
	 * @brief Write a variable-length XML element to the buffer, resuming as needed.
	 *
	 * Writes <openTag>content</closeTag>, or selfClose if content is empty.
	 * Resumes from _field and _offset if the buffer filled on a previous call.
	 * Updates _field and _offset on return.  Returns true when the element is
	 * fully written.  Content is XML-escaped; illegal XML 1.0 control characters
	 * are silently dropped.
	 *
	 * @param openTag    element open tag, e.g. "<tag>"
	 * @param openTagLen byte length of openTag
	 * @param closeTag   element close tag, e.g. "</tag>"
	 * @param closeTagLen byte length of closeTag
	 * @param selfClose  self-closing form, e.g. "<tag/>"
	 * @param selfCloseLen byte length of selfClose
	 * @param content    pointer to content bytes (need not be null-terminated)
	 * @param len        byte length of content
	 * @param buffer     destination buffer
	 * @return true if the element was fully written
	 */
	bool appendElement(
		const char* openTag,
		std::size_t openTagLen,
		const char* closeTag,
		std::size_t closeTagLen,
		const char* selfClose,
		std::size_t selfCloseLen,
		const char* content,
		std::size_t len,
		Buffer& buffer ) noexcept;

	/**
	 * @brief Append XML-escaped content bytes to the buffer, resuming from offset.
	 *
	 * Writes bytes from content[ offset ] onward, substituting XML entity
	 * references for &, <, and >.  Illegal XML 1.0 control characters
	 * (U+0000-U+0008, U+000B-U+000C, U+000E-U+001F) are silently dropped.
	 * Tab (U+0009), LF (U+000A), and CR (U+000D) are passed through unmodified.
	 * Updates offset to reflect progress.
	 *
	 * @param content pointer to content bytes
	 * @param len     byte length of content
	 * @param offset  current write position; updated on return
	 * @param buffer  destination buffer
	 * @return true if all content bytes were processed
	 */
	static bool appendEscaped(
		const char* content,
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

} // namespace extras
} // namespace nova
} // namespace kmac

#endif // KMAC_NOVA_EXTRAS_XML_FORMATTER_H
