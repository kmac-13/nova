#pragma once
#ifndef KMAC_NOVA_RECORD_H
#define KMAC_NOVA_RECORD_H

#include <cstdint>

namespace kmac {
namespace nova {

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
 * Layout (64-bit platform) - Total size: 56 bytes:
 * - timestamp:   8 bytes (offset  0)
 * - tagId:       8 bytes (offset  8)
 * - tag:         8 bytes (offset 16)
 * - file:        8 bytes (offset 24)
 * - function:    8 bytes (offset 32)
 * - line:        4 bytes (offset 40)
 * - messageSize: 4 bytes (offset 44)
 * - message:     8 bytes (offset 48)
 *
 * Layout (32-bit platform) - Total size: 40 bytes:
 * - timestamp:   8 bytes (offset  0)
 * - tagId:       8 bytes (offset  8)
 * - tag:         4 bytes (offset 16)
 * - file:        4 bytes (offset 20)
 * - function:    4 bytes (offset 24)
 * - line:        4 bytes (offset 28)
 * - messageSize: 4 bytes (offset 32)
 * - message:     4 bytes (offset 36)
 *
 * Fields ordered to eliminate padding: 8-byte aligned fields first (timestamp,
 * tagId, then pointers), then line and messageSize packed as a natural pair,
 * then message pointer last so it follows its size field logically.
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
	std::uint64_t timestamp;   ///< timestamp from logger_traits<Tag>::timestamp() (typically nanoseconds)

	std::uint64_t tagId;       ///< unique tag identifier (hash)
	const char* tag;           ///< tag name string (e.g., "ERROR", "DEBUG")

	const char* file;          ///< source filename (e.g. __FILE__)
	const char* function;      ///< function name (e.g. __FUNCTION__)
	std::uint32_t line;        ///< line number (e.g. __LINE__)

	std::uint32_t messageSize; ///< byte length of message
	const char* message;       ///< log message (may NOT be null-terminated)
};

} // namespace nova
} // namespace kmac

#endif // KMAC_NOVA_RECORD_H
