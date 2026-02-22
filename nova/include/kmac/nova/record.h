#pragma once
#ifndef KMAC_NOVA_RECORD_H
#define KMAC_NOVA_RECORD_H

#include <cstddef>
#include <cstdint>

namespace kmac::nova
{

/**
 * @brief Log record structure passed to Sink::process().
 *
 * Record contains all metadata and message content for a single log entry.
 * Fields are populated before being passed to Sinks for processing.
 * Sinks are free to modify the message field (e.g., FormattingSink) before
 * passing to downstream sinks.
 *
 * Memory ownership:
 * - all pointers are non-owning (const char*)
 * - message might not be null-terminated (use messageSize)
 * - record lifetime is limited to Sink::process() call
 * - do not store pointers beyond process() call
 *
 * Layout (64-bit platform) - Total size: 64 bytes:
 * - tag: 8 bytes (offset 0)
 * - tagId: 8 bytes (offset 8)
 * - file: 8 bytes (offset 16)
 * - function: 8 bytes (offset 24)
 * - line: 4 bytes (offset 32)
 * - [padding: 4 bytes]
 * - timestamp: 8 bytes (offset 40)
 * - message: 8 bytes (offset 48)
 * - messageSize: 8 bytes (offset 56)
 *
 * Note: Members are ordered for clarity, not optimally packed.
 * The 4-byte padding after 'line' could be eliminated by reordering.
 *
 * Usage:
 *   void MySink::process(const Record& record) {
 *       // access message safely
 *       std::string_view msg(record.message, record.messageSize);
 *
 *       // format timestamp
 *       auto ts = record.timestamp;
 *
 *       // use tag information
 *       std::cout << record.tag << ", " << ts << ": " << msg << "\n";
 *   }
 */
struct Record
{
	const char* tag;         ///< tag name string (e.g., "ERROR", "DEBUG")
	std::uintptr_t tagId;    ///< unique tag identifier (address-based)

	const char* file;        ///< source filename (e.g. __FILE__)
	const char* function;    ///< function name (e.g. __FUNCTION__)
	uint32_t line;           ///< line number (e.g. __LINE__)

	std::uint64_t timestamp; ///< timestamp from logger_traits<Tag>::timestamp() (typically nanoseconds)
	const char* message;     ///< log message (may NOT be null-terminated)
	std::size_t messageSize; ///< byte length of message
};

} // namespace kmac::nova

#endif // KMAC_NOVA_RECORD_H
