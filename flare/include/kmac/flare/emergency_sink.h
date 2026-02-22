#pragma once
#ifndef KMAC_FLARE_EMERGENCY_SINK_H
#define KMAC_FLARE_EMERGENCY_SINK_H

#include "kmac/flare/iwriter.h"

#include <kmac/nova/sink.h>

#include <cstdint>

namespace kmac::flare
{

/**
 * @brief Crash-safe forensic logging sink for Nova.
 * 
 * EmergencySink writes Nova log records to a binary file in TLV format.
 * Designed for crash handlers and emergency logging scenarios where:
 * - heap allocation is unsafe
 * - exceptions cannot be used
 * - partial writes must be tolerated
 * - speed matters more than completeness
 * 
 * Key characteristics:
 * - no heap allocation during process()
 * - uses fixed stack buffer (4KB default)
 * - single fwrite() per record (more atomic)
 * - flushes after each record (crash safety)
 * - truncates messages that don't fit in buffer
 * 
 * TLV Format:
 * - MAGIC (8 bytes)
 * - size (4 bytes)
 * - TLVs for timestamp, tag, file, line, function, message
 * - END marker
 * 
 * Usage:
 *   FILE* emergency = std::fopen( "crash.flare", "wb" );
 *   EmergencySink sink( emergency );
 *   Logger< CrashTag >::bindSink( &sink );
 */
class EmergencySink final : public kmac::nova::Sink
{
private:
	static constexpr std::size_t ENCODING_BUFFER_SIZE = 4096;

	IWriter* _writer;
	std::uint64_t _sequenceNumber;  // monotonic sequence counter
	bool _captureProcessInfo;       // capture PID (and TID on Linux)
	
public:
	/**
	 * @brief Construct emergency sink.
	 * 
	 * @param file output file (must remain open during lifetime)
	 * @param captureProcessInfo If true, capture process ID (and thread ID depending on platform)
	 *
	 * @note platform support:
	 *  - Linux: PID + TID (both async-signal-safe)
	 *  - macOS/FreeBSD/Windows: PID only
	 *  - other platforms: no process info
	 */
	explicit EmergencySink( IWriter* writer, bool captureProcessInfo = true ) noexcept;

	/**
	 * @brief Processes a Nova Record.
	 *
	 * @param record the Record to process
	 */
	void process( const kmac::nova::Record& record ) noexcept override;
	
	/**
	 * @brief Flush buffered data to disk.
	 * 
	 * Call this before process termination to ensure all data is written.
	 */
	void flush() noexcept;

private:
	/**
	 * @brief Encode a Nova record into TLV format.
	 * 
	 * @param record the Nova record to encode
	 * @param buffer destination buffer
	 * @param bufferSize size of destination buffer
	 * @return number of bytes written, or 0 on error
	 */
	std::size_t encodeRecordTlv(
		const kmac::nova::Record& record,
		char* buffer,
		std::size_t bufferSize
	) noexcept;
	
	/**
	 * @brief FNV-1a hash for tag strings.
	 * 
	 * Used to convert tag strings to compact uint64_t IDs.
	 */
	static std::uint64_t hashString( const char* str ) noexcept;
};

} // namespace kmac::flare

#endif // KMAC_FLARE_EMERGENCY_SINK_H
