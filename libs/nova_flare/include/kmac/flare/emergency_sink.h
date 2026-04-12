#pragma once
#ifndef KMAC_FLARE_EMERGENCY_SINK_H
#define KMAC_FLARE_EMERGENCY_SINK_H

#include "iwriter.h"

#include <kmac/nova/record.h>
#include <kmac/nova/sink.h>
#include <kmac/nova/platform/array.h>

#include <cstddef>
#include <cstdint>

namespace kmac::flare
{

// forward declarations
struct FaultContext;  // full definition in signal_handler.h

/**
 * @brief Non-templated base for BasicEmergencySink.
 *
 * EmergencySinkBase holds all state and implements everything that does not
 * depend on the encoding buffer size: construction, flush(), and the full
 * TLV encoding logic (encodeRecordTlv).  The only thing left to the derived
 * template is process(), which allocates the stack buffer and calls
 * encodeRecordTlv.
 *
 * This class is not intended to be used directly.  Use BasicEmergencySink<N>
 * or the EmergencySink alias instead.
 *
 * @note EmergencySinkBase must be constructed in a normal (non-signal)
 * context.  dl_iterate_phdr and equivalent APIs acquire locks that are not
 * safe to call from a signal handler.  Construct once at startup before
 * installing signal handlers.
 */
class EmergencySinkBase : public kmac::nova::Sink
{
private:
	IWriter* _writer;
	std::uint64_t _loadBaseAddress;  // captured once at construction; 0 if unavailable
	bool _captureProcessInfo;        // capture PID (and TID on Linux/Android)
	bool _captureStackTrace;         // capture raw stack frames

public:
	/**
	 * @brief Construct emergency sink base.
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
	explicit EmergencySinkBase(
		IWriter* writer,
		bool captureProcessInfo = true,
		bool captureStackTrace = false
	) noexcept;

	/**
	 * @brief Flush buffered data to the writer.
	 *
	 * Call this before process termination to ensure all data is written.
	 */
	void flush() noexcept;

protected:
	/**
	 * @brief Get the underlying IWriter.
	 */
	IWriter* writer() const noexcept;

	/**
	 * @brief Get the load base address captured at construction.
	 *
	 * Used by SignalHandler to populate FaultContext::aslrOffset without
	 * needing to re-query the dynamic linker from a signal handler.
	 */
	std::uint64_t loadBaseAddress() const noexcept;

	/**
	 * @brief Encode a Nova record into a caller-supplied TLV buffer.
	 *
	 * @param record the Nova record to encode
	 * @param buffer destination buffer
	 * @param bufferSize size of destination buffer in bytes
	 * @param sequenceNumber monotonic counter for this record
	 * @param faultContext optional fault context from a signal handler (may be nullptr)
	 * @return number of bytes written, or 0 on error
	 *
	 * @note The caller is responsible for maintaining and incrementing the
	 * sequenceNumber.  See EmergencySink::_sequenceNumber for an example.
	 */
	std::size_t encodeRecordTlv(
		const kmac::nova::Record& record,
		char* buffer,
		std::size_t bufferSize,
		std::size_t sequenceNumber,
		const FaultContext* faultContext = nullptr
	) noexcept;

private:
	/**
	 * @brief FNV-1a hash for tag strings.
	 *
	 * Used to convert tag strings to compact uint64_t IDs.
	 */
	// TODO: move to cpp
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
	// TODO: move to cpp
	static std::uint64_t captureLoadBaseAddress() noexcept;

	/**
	 * @brief Capture raw return addresses into a caller-supplied array.
	 *
	 * @param frames destination array of raw return addresses
	 * @param maxFrames capacity of the array
	 * @return        umber of frames written (may be 0 on unsupported platforms).
	 *
	 * @note The unwinding APIs used here are not formally async-signal-safe,
	 * but are widely used in crash handlers.  Only called when the
	 * FLARE_ENABLE_STACK_TRACE macro is defined and captureStackTrace=true.
	 */
	// TODO: move to cpp
	static std::size_t captureStackFrames( void** frames, std::size_t maxFrames ) noexcept;
};


/**
 * @brief Crash-safe forensic logging sink for Nova.
 *
 * BasicEmergencySink writes Nova log records to a binary stream in TLV format.
 * Designed for crash handlers and emergency logging scenarios where:
 * - heap allocation is unsafe
 * - exceptions cannot be used
 * - partial writes must be tolerated
 * - speed matters more than completeness
 *
 * Key characteristics:
 * - no heap allocation during process()
 * - uses a fixed stack buffer of BufferSize bytes
 * - single write() per record (more atomic)
 * - flushes after each record (crash safety)
 * - truncates messages that don't fit in the buffer
 *
 * @tparam BufferSize size of the per-record encoding buffer in bytes
 *   The default (4096) comfortably holds typical log messages, source
 *   location strings, and a full 32-frame stack trace.  Reduce for
 *   bare-metal targets with limited interrupt-stack space; increase if
 *   you need longer messages without truncation.
 *
 *   For bare-metal crash capture considerations (fault vectors, register
 *   extraction, RamWriter usage) see the TODO in signal_handler.h.
 *
 *   Minimum useful size is roughly:
 *     8  (magic) + 4 (size) + ~60 (fixed TLVs) + message + stack frames
 *   A static_assert enforces a 256-byte floor.
 *
 * TLV Format:
 * - MAGIC (8 bytes)
 * - size (4 bytes)
 * - TLVs for status, sequence, timestamp, tag, file, line, function,
 *   process info, fault address (signal records only), load base address,
 *   ASLR offset, stack frames, register layout + CPU registers, message
 * - END marker
 *
 * Usage (signal handler):
 * @code
 *   // construct once at startup, before installing signal handlers
 *   int fd = ::open( "crash.flare", O_WRONLY | O_CREAT | O_APPEND, 0644 );
 *   static kmac::flare::FdWriter writer( fd );
 *   static kmac::flare::EmergencySink<> sink( &writer, true, true );
 *   kmac::nova::Logger< CrashTag >::bindSink( &sink );
 * @endcode
 *
 * Bare-metal usage (smaller buffer):
 * @code
 *   static std::uint8_t crashBuf[ 2048 ];
 *   static kmac::flare::RamWriter ramWriter( crashBuf, sizeof( crashBuf ) );
 *   static kmac::flare::EmergencySink< 512 > sink( &ramWriter );
 * @endcode
 */
template < std::size_t BufferSize = 4096 >
class EmergencySink final : public EmergencySinkBase
{
private:
	static_assert( BufferSize >= 256, "BufferSize must be at least 256 bytes" );

	std::uint64_t _sequenceNumber = 0;   // monotonic sequence counter

public:
	using EmergencySinkBase::EmergencySinkBase;

	/**
	 * @brief Encode and write a Nova record.
	 *
	 * @param record the Record to process
	 */
	void process( const kmac::nova::Record& record ) noexcept override;

	/**
	 * @brief Encode and write a Nova record with fault context from a signal handler.
	 *
	 * Called by SignalHandler on crash signal delivery.  Includes fault address,
	 * ASLR offset, and CPU register snapshot alongside the standard TLVs.
	 *
	 * @param record the Record to process
	 * @param faultContext fault context captured from siginfo_t / ucontext_t
	 */
	void processWithFaultContext(
		const kmac::nova::Record& record,
		const FaultContext& faultContext
	) noexcept;

private:
	/**
	 * @brief Encode and write a record, optionally with fault context.
	 *
	 * Common implementation for process() and processWithFaultContext().
	 * Allocates the stack buffer, encodes, writes, flushes, and advances
	 * the sequence number.
	 *
	 * encodedSize == 0 means the buffer was too small to fit the mandatory
	 * fixed-size header fields - a silent no-op is the correct response.
	 * There is nothing meaningful to write, no partial record to flush, and
	 * in a crash handler there is no way to surface an error.  The sequence
	 * number is intentionally not incremented: a gap could mislead a reader
	 * into believing a record was lost in transit, whereas leaving it
	 * unchanged means the next successful record is numbered as if this
	 * call never happened.
	 *
	 * @param record the Record to encode
	 * @param faultContext optional fault context (nullptr for normal records)
	 */
	void writeRecord( const kmac::nova::Record& record, const FaultContext* faultContext ) noexcept;
};

template < std::size_t BufferSize >
void EmergencySink< BufferSize >::process( const kmac::nova::Record& record ) noexcept
{
	writeRecord( record, nullptr );
}

template < std::size_t BufferSize >
void EmergencySink< BufferSize >::processWithFaultContext(
	const kmac::nova::Record& record,
	const FaultContext& faultContext
) noexcept
{
	writeRecord( record, &faultContext );
}

template < std::size_t BufferSize >
void EmergencySink< BufferSize >::writeRecord( const kmac::nova::Record& record, const FaultContext* faultContext ) noexcept
{
	if ( writer() == nullptr )
	{
		return;
	}

	// fixed-size stack buffer, no heap allocation
	kmac::nova::platform::Array< char, BufferSize > buffer {};
	const std::size_t encodedSize = encodeRecordTlv(
		record,
		buffer.data(),
		buffer.size(),
		_sequenceNumber,
		faultContext
	);

	if ( encodedSize > 0 )
	{
		// single atomic write
		writer()->write( buffer.data(), encodedSize );

		// flush for crash safety
		writer()->flush();

		// increment sequence number for next record
		++_sequenceNumber;
	}
}

} // namespace kmac::flare

#endif // KMAC_FLARE_EMERGENCY_SINK_H
