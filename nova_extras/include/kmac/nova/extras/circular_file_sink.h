#pragma once
#ifndef KMAC_NOVA_EXTRAS_CIRCULAR_FILE_SINK_H
#define KMAC_NOVA_EXTRAS_CIRCULAR_FILE_SINK_H

#include "kmac/nova/sink.h"

#include <cstddef>
#include <cstdio>
#include <string>

namespace kmac::nova::extras
{

class Formatter;

/**
 * @brief Single fixed-size file sink with circular overwrite.
 *
 * ✅ SAFE FOR SAFETY-CRITICAL SYSTEMS
 *
 * CircularFileSink writes logs to a single file with a maximum size. When
 * the file reaches its limit, it seeks back to the beginning and overwrites
 * old data. This provides bounded disk usage with no file rotation overhead.
 *
 * Key safety properties:
 * - bounded disk usage (maxFileSize parameter)
 * - single file (no unbounded file creation)
 * - no heap allocation after construction
 * - deterministic behavior (simple seek + write)
 * - no rotation callbacks (simpler failure modes)
 *
 * Trade-offs:
 * - pros: Simple, bounded, deterministic, no file accumulation
 * - cons: Overwrites old data, file may contain partial records
 *
 * File structure after wraparound:
 *   [newer data][wrap point][older data that will be overwritten next]
 *   
 * Reading the file:
 * - file may not be in chronological order
 * - may contain partial records at wrap point
 *
 * Use cases:
 * - embedded systems (bounded resources)
 * - safety-critical systems (DO-178C/IEC 61508)
 * - real-time systems (deterministic behavior)
 * - black box logging (recent N bytes of logs)
 * - diagnostics (always have last N MB of logs)
 *
 * Not suitable for:
 * - long-term log archival (use RollingFileSink)
 * - log analysis requiring complete history
 * - systems where losing old logs is unacceptable
 *
 * Features:
 * - single file (no roll-over to continuation files, use RollingFileSink for this)
 * - fixed maximum size (compile-time or runtime)
 * - automatic wraparound (seeks to start when full)
 * - optional formatting (via Formatter)
 * - thread-safe (if wrapped in SynchronizedSink)
 *
 * Performance:
 * - fast (no rotation overhead)
 * - write buffering for efficiency
 * - single fseek when wrapping
 * - no file creation/deletion during operation
 *
 * Usage:
 *   CircularFileSink sink("app.log", 10*1024*1024);  // 10MB max
 *   
 *   Logger<MyTag>::bindSink(&sink);
 *   
 *   // writes up to 10MB, then wraps to beginning
 *   // always have most recent 10MB of logs
 *
 * With formatting:
 *   CircularFileSink sink("app.log", 10*1024*1024, &formatter);
 *   // formatted output with wraparound
 *
 * Safety-critical example:
 *   // fixed 1MB log for embedded system
 *   CircularFileSink sink("diagnostics.log", 1*1024*1024);
 *   SpinlockSink<CircularFileSink> threadSafe(sink);
 *   
 *   // Guaranteed:
 *   // - never exceeds 1MB disk
 *   // - no file accumulation
 *   // - deterministic behavior
 *   // - always have recent logs for post-mortem
 *
 * DO-178C/IEC 61508/ISO 26262 COMPLIANCE:
 * 
 * This sink is suitable for certification at all levels:
 * - bounded resources (fixed disk usage)
 * - simple implementation (easier to verify)
 * - no dynamic behavior (no rotation)
 * - deterministic (seek + write)
 * - no callbacks (fewer failure modes)
 *
 * Certification considerations:
 * - file I/O still platform-dependent (analyze fopen/fwrite/fseek)
 * - wrap-around creates discontinuity (analyze impact on log analysis)
 * - partial records at wrap point (define acceptability criteria)
 * - disk full handling (what if file creation fails?)
 *
 * @see RollingFileSink for file rotation with preservation
 * @see Flare emergency logging for crash-safe memory-only logging
 */
class CircularFileSink final : public kmac::nova::Sink
{
	// write buffering for improved performance
	static constexpr std::size_t WRITE_BUFFER_SIZE = 256 * 1024;  // 256 KB buffer

private:
	std::string _filename;
	std::size_t _maxFileSize;
	std::size_t _currentSize;    ///< Current position in file (0 to maxFileSize-1)
	std::size_t _totalWritten;   ///< Total bytes written (may exceed maxFileSize)
	bool _hasWrapped;            ///< True if we've wrapped around at least once

	FILE* _file;

	Formatter* _formatter;

	char _writeBuffer[ WRITE_BUFFER_SIZE ];
	std::size_t _bufferOffset;

	using ProcessFunc = void ( CircularFileSink::* )( const kmac::nova::Record& );
	ProcessFunc _process;

public:
	/**
	 * @brief Construct circular file sink.
	 *
	 * Opens or creates the file.  If the file already exists, it is truncated
	 * to maxFileSize (or to zero if smaller).  Writing starts at the beginning.
	 *
	 * @param filename path to log file
	 * @param maxFileSize maximum file size in bytes (file wraps at this size)
	 * @param formatter optional Formatter for structured output (nullptr for raw)
	 */
	explicit CircularFileSink( const std::string& filename, std::size_t maxFileSize, Formatter* formatter = nullptr ) noexcept;

	/**
	 * @brief Destructor - flushes and closes file.
	 */
	~CircularFileSink() noexcept override;

	NO_COPY_NO_MOVE( CircularFileSink );

	/**
	 * @brief Process a record, wrapping to start if needed.
	 *
	 * Writes the record to the file.  If writing would exceed maxFileSize,
	 * flushes the buffer, seeks to the beginning of the file, and continues
	 * writing from there (overwriting old data).
	 *
	 * Wrap-around behavior:
	 * 1. check if write would exceed maxFileSize
	 * 2. if yes: flush buffer, seek to position 0, reset currentSize
	 * 3. write record (may span across old records)
	 *
	 * NOTE: Records may be split at wrap point.  The file may contain:
	 * - partial record at end (truncated by wrap)
	 * - continuation at beginning (rest of wrapped record)
	 *
	 * @param record the record to process
	 */
	void process( const kmac::nova::Record& record ) noexcept override;

	/**
	 * @brief Get current position in file.
	 *
	 * Returns the current write position (0 to maxFileSize-1).
	 * Wraps back to 0 when reaching maxFileSize.
	 *
	 * @return current file position in bytes
	 */
	std::size_t currentPosition() const noexcept;

	/**
	 * @brief Get maximum file size.
	 *
	 * @return maximum file size in bytes
	 */
	std::size_t maxFileSize() const noexcept;

	/**
	 * @brief Get filename.
	 *
	 * @return file path
	 */
	const std::string& filename() const noexcept;

	/**
	 * @brief Check if file has wrapped around.
	 *
	 * Returns true if the file has reached maxFileSize and wrapped back
	 * to the beginning at least once.
	 *
	 * @return true if wrapped, false if still in first pass
	 */
	bool hasWrapped() const noexcept;

	/**
	 * @brief Get total bytes written (including wrapped data).
	 *
	 * This may exceed maxFileSize if data has been overwritten.
	 * Example: maxFileSize=1MB, totalWritten=3MB means wrapped twice.
	 *
	 * @return total bytes written since construction
	 */
	std::size_t totalWritten() const noexcept;

	/**
	 * @brief Flush write buffer to disk.
	 *
	 * Writes any buffered data to the file and calls fflush.
	 * Called automatically when buffer is full or during wraparound.
	 */
	void flush() noexcept;

private:
	void processRaw( const kmac::nova::Record& record ) noexcept;
	void processFormatted( const kmac::nova::Record& record ) noexcept;

	/**
	 * @brief Wrap to beginning of file.
	 *
	 * Flushes buffer, seeks to position 0, resets currentSize to 0.
	 */
	void wrap() noexcept;

	/**
	 * @brief Write data to buffer, wrapping if needed.
	 *
	 * @param data pointer to data
	 * @param size size of data in bytes
	 */
	void write( const char* data, std::size_t size ) noexcept;
};

} // namespace kmac::nova::extras

#endif // KMAC_NOVA_EXTRAS_CIRCULAR_FILE_SINK_H
