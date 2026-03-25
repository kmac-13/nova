#pragma once
#ifndef KMAC_FLARE_EMERGENCY_SINK_H
#define KMAC_FLARE_EMERGENCY_SINK_H

#include "iwriter.h"

#include <kmac/nova/sink.h>

#include <cstddef>
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
	std::uint64_t _sequenceNumber;   // monotonic sequence counter
	std::uint64_t _loadBaseAddress;  // captured once at construction; 0 if unavailable
	bool _captureProcessInfo;        // capture PID (and TID on Linux)
	bool _captureStackTrace;         // capture raw stack frames via backtrace()

public:
	/**
	 * @brief Construct emergency sink.
	 *
	 * @param writer output writer (must remain valid during lifetime)
	 * @param captureProcessInfo if true, capture process ID (and thread ID depending on platform)
	 * @param captureStackTrace if true, capture a raw stack trace on every record
	 *
	 * @note captureStackTrace is disabled by default because the underlying
	 * unwinding APIs are not formally async-signal-safe on all implementations;
	 * enable explicitly in crash handlers where the trade-off is acceptable.
	 * Requires FLARE_ENABLE_STACK_TRACE to be defined at compile time (CMake
	 * option FLARE_STACK_TRACE).  Has no effect when the macro is absent.
	 *
	 * The load base address of the main executable is always captured at
	 * construction time (when available) and written as a TLV on every record.
	 * This is required to convert runtime frame addresses to static addresses
	 * for symbolization with addr2line / llvm-symbolizer on PIE binaries.
	 *
	 * @note Stack trace platform support:
	 *  - Android            :  _Unwind_Backtrace  (<unwind.h>)
	 *  - Linux / macOS / BSD:  backtrace()        (<execinfo.h>)
	 *  - Windows            :  RtlCaptureStackBackTrace
	 *  - other platforms    :  TLV silently omitted
	 *
	 * @note Load base address platform support:
	 *  - Linux / Android:  first PT_LOAD segment via dl_iterate_phdr()
	 *  - macOS          :  _dyld_get_image_vmaddr_slide() + text segment base
	 *  - Windows        :  GetModuleInformation() on the main module
	 *  - other platforms:  0 (TLV written with value 0; reader will warn)
	 */
	explicit EmergencySink(
		IWriter* writer,
		bool captureProcessInfo = true,
		bool captureStackTrace = false
	) noexcept;

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

	/**
	 * @brief Capture the runtime load base address of the main executable.
	 *
	 * Called once during construction.  Returns 0 if the platform is not
	 * supported or the address cannot be determined.
	 *
	 * @note Not async-signal-safe, must only be called from a normal context
	 * (i.e. at construction time, not from a signal handler).
	 */
	static std::uint64_t captureLoadBaseAddress() noexcept;

	/**
	 * @brief Capture raw return addresses into a caller-supplied array.
	 *
	 * @param frames destination array of raw return addresses
	 * @param maxFrames capacity of the array
	 * @return number of frames written (may be 0 on unsupported platforms)
	 *
	 * @note The unwinding APIs used here are not formally async-signal-safe,
	 * but are widely used in crash handlers.  Only called when the
	 * FLARE_ENABLE_STACK_TRACE macro is defined and captureStackTrace=true.
	 */
	static std::size_t captureStackFrames( void** frames, std::size_t maxFrames ) noexcept;
};

} // namespace kmac::flare

#endif // KMAC_FLARE_EMERGENCY_SINK_H
