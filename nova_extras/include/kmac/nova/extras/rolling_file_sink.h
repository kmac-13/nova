#pragma once
#ifndef KMAC_NOVA_EXTRAS_ROLLING_FILE_SINK_H
#define KMAC_NOVA_EXTRAS_ROLLING_FILE_SINK_H

/**
 * ⚠️ WARNING ⚠️
 *
 * REQUIRES CAREFUL CONFIGURATION FOR SAFETY-CRITICAL SYSTEMS
 *
 * This sink creates numbered log files (app.log.1, app.log.2, ...) with
 * an unbounded file index.  It requires proper callback configuration to
 * prevent unbounded resource usage.
 *
 * SAFETY REQUIREMENTS:
 *
 * 1. MANDATORY CLEANUP CALLBACK:
 *    - MUST register rollover callback
 *    - MUST delete/compress old files to bound disk usage
 *    - MUST handle callback errors gracefully
 *
 * 2. BOUNDED FILE COUNT:
 *    Example callback keeping only last 10 files:
 *
 *    sink.setRolloverCallback( []( const std::string& oldFile, const std::string& newFile ) {
 *        static constexpr size_t MAX_FILES = 10;
 *
 *        // extract index from filename
 *        size_t oldIndex = extractIndexFromFilename( oldFile );
 *
 *        // delete files beyond retention limit
 *        if ( oldIndex > MAX_FILES ) {
 *            for ( size_t i = 1; i <= oldIndex - MAX_FILES; i++ ) {
 *                std::filesystem::remove( makeFilename( i ) );
 *            }
 *        }
 *    } );
 *
 * 3. STARTUP CLEANUP:
 *    On startup, clean up orphaned files from crashed runs:
 *
 *    void startupCleanup( const std::string& baseFilename ) {
 *        // Find all app.log.N files
 *        // Keep only most recent MAX_FILES
 *        // Delete the rest
 *    }
 *
 * 4. DISK USAGE MONITORING:
 *    - monitor disk space externally
 *    - alert if disk usage exceeds threshold
 *    - have fallback if disk fills
 *
 * REMAINING SAFETY ISSUES:
 *
 * 1. UNBOUNDED FILE INDEX:
 *    - _currentIndex increments forever
 *    - after ~4 billion rotations (32-bit): overflow
 *    - after ~18 quintillion rotations (64-bit): overflow
 *    - mitigation: Use 64-bit size_t (81,693 years @ 6 rotations/hour)
 *
 * 2. CALLBACK FAILURE:
 *    - if callback throws/crashes, files accumulate
 *    - if callback is slow, rotation is delayed
 *    - mitigation: Robust error handling in callback
 *
 * 3. CRASH BEFORE CALLBACK:
 *    - if process crashes after rotation but before callback
 *    - old file not deleted, accumulates over time
 *    - mitigation: Startup cleanup script
 *
 * 4. NON-DETERMINISTIC BEHAVIOR:
 *    - file I/O timing varies (disk speed, fragmentation)
 *    - callback execution time varies
 *    - not suitable for hard real-time systems
 *
 * 5. FILESYSTEM DEPENDENCIES:
 *    - relies on std::filesystem (C++17)
 *    - relies on fopen/fclose (platform-specific behavior)
 *    - file creation may fail (permissions, disk full, etc.)
 *
 * CERTIFICATION CONSIDERATIONS:
 *
 * DO-178C/IEC 61508/ISO 26262:
 * - Level C/D: Acceptable with proper callback and monitoring
 * - Level A/B: Requires extensive analysis and testing:
 *   * proof of bounded disk usage (callback analysis)
 *   * worst-case execution time analysis (WCET)
 *   * failure mode analysis (callback failures, disk full, etc.)
 *   * file system behavior analysis (platform-specific)
 *
 * ALTERNATIVES FOR HIGHER SAFETY LEVELS:
 *
 * 1. Single Fixed-Size File (Circular):
 *    - open file once at startup
 *    - seek to beginning when full
 *    - overwrite old data
 *    - no rotation, bounded disk usage
 *
 * 2. Memory-Only Logging:
 *    - log to fixed-size circular buffer in RAM
 *    - dump to file only on shutdown/crash
 *    - use Flare emergency logging
 *
 * 3. External Log Daemon:
 *    - application writes to fixed file
 *    - separate process handles rotation
 *    - clearer separation of concerns
 *
 * SAFE CONFIGURATION EXAMPLE:
 *
 *   constexpr size_t MAX_LOG_FILES = 5;
 *   constexpr size_t MAX_FILE_SIZE = 10*1024*1024;  // 10MB
 *
 *   RollingFileSink sink( "app.log", MAX_FILE_SIZE );
 *
 *   sink.setRolloverCallback( []( const std::string& oldFile, const std::string& newFile ) {
 *       try {
 *           size_t oldIndex = extractIndex( oldFile );
 *
 *           // delete files beyond retention
 *           if ( oldIndex > MAX_LOG_FILES ) {
 *               for ( size_t i = 1; i <= oldIndex - MAX_LOG_FILES; i++ ) {
 *                   std::string toDelete = "app.log." + std::to_string( i );
 *                   std::filesystem::remove( toDelete );
 *               }
 *           }
 *
 *           // optional: compress old file
 *           if ( oldIndex > 1 ) {
 *               std::system( ( "gzip " + oldFile ).c_str());
 *           }
 *       } catch ( const std::exception& e ) {
 *           // log error but don't throw
 *           std::cerr << "Rollover callback failed: " << e.what() << std::endl;
 *       }
 *   } );
 *
 * SUMMARY:
 *
 * RollingFileSink CAN be used safely in soft real-time and safety-critical
 * systems (Level C/D) IF proper callback and monitoring are implemented.
 *
 * For Level A/B certification or hard real-time systems, consider simpler
 * alternatives with more deterministic behavior.
 */

#include "kmac/nova/sink.h"

#include <cstddef>
#include <cstdio>
#include <functional>
#include <string>

namespace kmac::nova::extras
{

class Formatter;

/**
 * @brief File sink with size-based log rotation and callback support.
 *
 * RollingFileSink writes logs to numbered files and automatically rotates when
 * the current file reaches maximum size. The current file is always the one
 * with the highest numeric suffix, avoiding the need to rename multiple files.
 *
 * IMPORTANT: Rotation happens BEFORE writing to ensure maxFileSize is respected.
 * This guarantees no file exceeds the maximum size.
 *
 * File naming pattern:
 *   app.log.1
 *   app.log.2
 *   app.log.3
 *   app.log.4     (current - highest number, always < maxFileSize)
 *
 * When writing would exceed maxFileSize:
 *   1. close app.log.4
 *   2. create app.log.5 (new current)
 *   3. invoke rollover callback (if registered)
 *   4. write to app.log.5
 *
 * File management (deletion, compression, archival) is handled via the
 * callback mechanism, allowing flexible external policies.
 *
 * Benefits:
 * - no file renaming needed (fast rotation)
 * - single file operation (create new)
 * - atomic rotation (no intermediate state)
 * - better for SSDs (fewer writes)
 * - external file management via callback
 * - guaranteed max file size (rotation before write)
 *
 * Features:
 * - configurable max file size
 * - rollover callback support
 * - reuses OStreamSink for actual writing
 * - thread-safe (if wrapped in SynchronizedSink)
 * - works with FormattingSink for formatted output
 *
 * Usage:
 *   RollingFileSink sink("app.log", 10*1024*1024);  // 10MB max size
 *
 *   sink.setRolloverCallback([](const std::string& closedFile, const std::string& newFile) {
 *       // compress old file
 *       std::system(("gzip " + closedFile).c_str());
 *
 *       // delete files older than 30 days
 *       cleanupOldFiles("app.log", 30);
 *   });
 *
 *   Logger<MyTag>::bindSink(&sink);
 */
class RollingFileSink final : public kmac::nova::Sink
{
	// write buffering for improved performance
	static constexpr std::size_t WRITE_BUFFER_SIZE = 256 * 1024;  // # KB buffer

public:
	/**
	 * @brief Rollover callback type.
	 *
	 * Called after rotation completes.
	 *
	 * Use this to:
	 * - compress old files (gzip, bzip2, xz, etc.)
	 * - delete files beyond retention limit
	 * - upload to remote storage (S3, Azure, etc.)
	 * - send notifications
	 * - update metrics
	 * - trigger log analysis
	 *
	 * @param closedFile path to the file that was just closed
	 * @param newFile path to the new current file
	 */
	using RolloverCallback = std::function< void( const std::string& closedFile, const std::string& newFile ) >;

private:
	std::string _baseFilename;
	std::size_t _maxFileSize;
	std::size_t _currentIndex;
	std::size_t _currentSize;

	FILE* _currentFile;

	RolloverCallback _rolloverCallback;

	Formatter* _formatter;

	char _writeBuffer[ WRITE_BUFFER_SIZE ];
	std::size_t _bufferOffset;
	std::size_t _remaining;

	using ProcessFunc = void (RollingFileSink::*)( const kmac::nova::Record& );
	ProcessFunc _process;

public:
	/**
	 * @brief Construct rolling file sink.
	 *
	 * Finds existing log files and opens/creates the current file
	 * (highest numbered file).
	 *
	 * @param baseFilename base filename without suffix (e.g., "app.log")
	 * @param maxFileSizem maximum file size in bytes before rotation
	 */
	explicit RollingFileSink(
		const std::string& baseFilename,
		std::size_t maxFileSize = 10 * 1024 * 1024,  // 10 MB default
		Formatter* formatter = nullptr
	) noexcept;

	/**
	 * @brief Destructor - closes current file.
	 */
	~RollingFileSink() noexcept override;

	/**
	 * @brief Set rollover callback.
	 *
	 * The callback is invoked after rotation completes but before the first
	 * write to the new file.
	 *
	 * @param callback function to call on rollover (nullptr to clear)
	 */
	void setRolloverCallback( RolloverCallback callback ) noexcept;

	/**
	 * @brief Process a record, with automatic rotation if needed.
	 *
	 * Checks if write would exceed maxFileSize. If so, rotates BEFORE writing.
	 * This guarantees no file exceeds the maximum size.
	 *
	 * @param record the record to process
	 */
	void process( const kmac::nova::Record& record ) noexcept override;

	/**
	 * @brief Get current file size.
	 */
	std::size_t currentFileSize() const noexcept;

	/**
	 * @brief Get base filename.
	 */
	const std::string& baseFilename() const noexcept;

	/**
	 * @brief Get maximum file size.
	 */
	std::size_t maxFileSize() const noexcept;

	/**
	 * @brief Get current file index.
	 */
	std::size_t currentIndex() const noexcept;

	/**
	 * @brief Get current filename.
	 */
	std::string currentFilename() const noexcept;

	/**
	 * @brief Force rotation now.
	 *
	 * Useful for testing or manual rotation (e.g., at midnight).
	 */
	void forceRotate() noexcept;

	/**
	 * @brief Flush write buffer to disk.
	 *
	 * Writes any buffered data to the file and flushes the stream.
	 * Called automatically when buffer is full or during rotation.
	 */
	void flush() noexcept;

private:
	void processRaw( const kmac::nova::Record& record ) noexcept;
	void processFormatted( const kmac::nova::Record& record ) noexcept;

	/**
	 * @brief Initialize by finding existing files that match the specified
	 * base name and opening current.
	 */
	void initialize() noexcept;

	/**
	 * @brief Find highest index of existing log file indices.
	 */
	std::size_t findHighestIndex() const noexcept;

	/**
	 * @brief Make filename for given index.
	 */
	std::string makeFilename( std::size_t index ) const noexcept;

	/**
	 * @brief Open current file for appending.
	 */
	void openCurrentFile() noexcept;

	/**
	 * @brief Close current file.
	 */
	void closeCurrentFile() noexcept;

	/**
	 * @brief Rotate to next file.
	 *
	 * Fast rotation:
	 * 1. close current file
	 * 2. increment index and create new file
	 * 3. invoke callback (which handles file management)
	 */
	void rotate() noexcept;
};

} // namespace kmac::nova::extras

#endif // KMAC_NOVA_EXTRAS_ROLLING_FILE_SINK_H
