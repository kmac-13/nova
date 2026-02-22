#pragma once
#ifndef KMAC_NOVA_EXTRAS_LARGE_PAYLOAD_FORMATTER_H
#define KMAC_NOVA_EXTRAS_LARGE_PAYLOAD_FORMATTER_H

#include "streaming_formatter.h"

#include <cstddef>

namespace kmac::nova
{
struct Record;
class Sink;
} // namespace kmac::nova

namespace kmac::nova::extras
{

/**
 * @brief Chunks large payloads into multiple records for safe transmission.
 *
 * ✅ SAFE FOR SAFETY-CRITICAL SYSTEMS
 *
 * LargePayloadFormatter breaks oversized messages into fixed-size chunks,
 * emitting multiple records with BEGIN_PAYLOAD and END_PAYLOAD markers.
 * This is useful when:
 * - message content exceeds downstream sink buffer limits
 * - network protocols impose message size limits
 * - log aggregators have per-record size constraints
 * - file systems have line length limits
 *
 * Output format:
 *   [TAG] BEGIN_PAYLOAD
 *   [chunk 1 data]
 *   [chunk 2 data]
 *   ...
 *   [chunk N data]
 *   END_PAYLOAD
 *
 * Features:
 * - automatically chunks data into manageable sizes
 * - preserves all payload data (no truncation)
 * - clear begin/end markers for reassembly
 * - works with StreamingFormatter interface
 *
 * Limitations:
 * - emits multiple log records (one per chunk + header/footer)
 * - downstream must handle multi-record payloads
 * - no automatic reassembly (consumer must handle markers)
 * - payload must remain valid during formatAndWrite call
 *
 * Performance:
 * - O(n) where n = payload size
 * - multiple sink->process() calls
 * - chunk size affects number of records emitted
 *
 * Thread safety:
 * - not thread-safe (single-threaded use)
 * - payload pointer must remain valid during formatAndWrite
 *
 * Usage example:
 *   // large binary data that needs logging
 *   std::vector<char> largeData(50000);
 *   // ... fill with data ...
 *
 *   LargePayloadFormatter formatter(largeData.data(), largeData.size());
 *
 *   Record record = { ... };
 *   OStreamSink console(std::cout);
 *
 *   // Emits multiple records: BEGIN + chunks + END
 *   formatter.formatAndWrite(record, console);
 *
 * Typical output:
 *   [INFO] BEGIN_PAYLOAD
 *   [binary chunk 1... 1024 bytes]
 *   [binary chunk 2... 1024 bytes]
 *   ...
 *   [binary chunk 49... 1024 bytes]
 *   [binary chunk 50... remaining bytes]
 *   END_PAYLOAD
 *
 * Chunk size control:
 * Default chunk size is determined by maxChunkSize() override.
 * Adjust if needed based on downstream sink limits.
 */
class LargePayloadFormatter final : public StreamingFormatter
{
private:
	const char* _payload;      ///< payload data (not owned)
	std::size_t _payloadSize;  ///< payload size in bytes

public:
	/**
	 * @brief Construct formatter for a large payload.
	 *
	 * @param payload pointer to payload data (must remain valid during formatAndWrite)
	 * @param payloadSize size of payload in bytes
	 *
	 * @note payload is not copied - pointer must remain valid
	 * @note no ownership transfer - caller manages lifetime
	 */
	explicit LargePayloadFormatter( const char* payload, std::size_t payloadSize ) noexcept;

	/**
	 * @brief Get maximum chunk size for splitting payload.
	 *
	 * @return maximum bytes per chunk
	 *
	 * @note override to customize chunk size based on downstream limits
	 */
	std::size_t maxChunkSize() const noexcept override;

	/**
	 * @brief Format payload and write chunks to downstream sink.
	 *
	 * Emits:
	 * 1. BEGIN_PAYLOAD marker record
	 * 2. N chunk records (payload split into maxChunkSize() pieces)
	 * 3. END_PAYLOAD marker record
	 *
	 * @param record original record (provides context like tag, timestamp)
	 * @param downstream sink to receive chunked records
	 *
	 * @note calls downstream.process() multiple times (N+2 calls)
	 * @note payload must remain valid for duration of this call
	 */
	void formatAndWrite( const kmac::nova::Record& record, kmac::nova::Sink& downstream ) noexcept override;
};

} // namespace kmac::nova::extras

#endif // KMAC_NOVA_EXTRAS_LARGE_PAYLOAD_FORMATTER_H
