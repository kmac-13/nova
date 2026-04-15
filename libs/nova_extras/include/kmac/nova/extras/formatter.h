#pragma once
#ifndef KMAC_NOVA_EXTRAS_FORMATTER_H
#define KMAC_NOVA_EXTRAS_FORMATTER_H

// #include <cstddef>

namespace kmac {
namespace nova {

struct Record;

namespace extras {

class Buffer;

/**
 * @brief Interface for formatting a Record into a byte stream.
 *
 * ✅ SAFE FOR SAFETY-CRITICAL SYSTEMS
 *
 * A Formatter converts a Record into formatted output using a caller-provided
 * buffer. Formatters must not allocate memory and must not modify the Record.
 *
 * Formatting may be incremental: a formatter can emit output across multiple
 * write() calls to support large payloads and fixed-size buffers.
 */
class Formatter
{
public:
	virtual ~Formatter() = default;

	/**
	 * @brief Prepare the formatter to format a new record.
	 *
	 * Called exactly once before any write() calls for a record.
	 *
	 * @param record
	 */
	virtual void begin( const kmac::nova::Record& record ) noexcept = 0;

	/**
	 * @brief Write formatted output into a buffer.
	 *
	 * @param outBuffer destination buffer
	 * @param bufferSize size of destination buffer
	 * @param bytesWritten number of bytes written
	 *
	 * @return true if formatting is complete, false if more data remains
	 */
	virtual bool format( const kmac::nova::Record& record, Buffer& buffer ) noexcept = 0;
};

} // namespace extras
} // namespace nova
} // namespace kmac

#endif  // KMAC_NOVA_EXTRAS_FORMATTER_H
