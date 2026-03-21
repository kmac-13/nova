#pragma once
#ifndef KMAC_FLARE_RAM_WRITER_H
#define KMAC_FLARE_RAM_WRITER_H

#include "iwriter.h"

#include <cstdint>

namespace kmac::flare
{

/**
 * @file ram_writer.h
 * @brief IWriter implementation that writes Flare records to a fixed RAM buffer.
 *
 * RamWriter stores the Flare binary stream in a caller-supplied memory region.
 * It requires no OS, no file system, and no hardware peripherals - making it
 * suitable for any bare-metal or RTOS target.
 *
 * After a crash the buffer contents can be retrieved via:
 * - JTAG/SWD debugger memory read
 * - bootloader upload on next boot (copy from known RAM address to flash/UART)
 * - dedicated crash-reporting routine that drains the buffer before reset
 *
 * Key characteristics:
 * - no heap allocation
 * - no OS or peripheral dependencies
 * - async-signal-safe (write is a memcpy, flush is a no-op)
 * - write() returns 0 when the buffer is full (EmergencySink marks record Truncated)
 * - bytesWritten() and isFull() allow the application to check buffer state
 *
 * Usage:
 * @code
 *   // static storage - survives reset on most Cortex-M targets if placed in
 *   // a no-init RAM section (e.g. __attribute__((section(".noinit"))))
 *   static std::uint8_t crashBuf[ 4096 ];
 *   static kmac::flare::RamWriter ramWriter( crashBuf, sizeof( crashBuf ) );
 *   static kmac::flare::EmergencySink emergencySink( &ramWriter );
 *
 *   // bind to a tag during startup
 *   kmac::nova::Logger< CrashTag >::bindSink( &emergencySink );
 * @endcode
 *
 * Linker section placement for post-reset retrieval (ARM Cortex-M example):
 * @code
 *   // linker script: add a .noinit section that is not zeroed on startup
 *   .noinit (NOLOAD) : { *(.noinit) } > RAM
 *
 *   // application:
 *   __attribute__( ( section( ".noinit" ) ) )
 *   static std::uint8_t crashBuf[ 4096 ];
 * @endcode
 *
 * @note flush() is a no-op - RAM writes are immediately visible.
 * @note thread safety: not thread-safe; use a single writer per crash context.
 */
class RamWriter final : public IWriter
{
private:
	std::uint8_t* const _buf;         ///< caller-supplied buffer
	const std::size_t _capacity = 0;  ///< total buffer capacity in bytes
	std::size_t _offset = 0;          ///< current write position

public:
	/**
	 * @brief Construct a RamWriter over a caller-supplied buffer.
	 *
	 * @param buf pointer to the buffer (must remain valid for the writer's lifetime)
	 * @param capacity size of the buffer in bytes
	 *
	 * @note the buffer is not zeroed on construction, call reset() to clear it
	 */
	RamWriter( void* buf, std::size_t capacity ) noexcept;

	/**
	 * @brief Number of bytes written so far.
	 */
	std::size_t bytesWritten() const noexcept;

	/**
	 * @brief Whether the buffer has been completely filled.
	 */
	bool isFull() const noexcept;

	/**
	 * @brief Pointer to the start of the buffer.
	 *
	 * Use with bytesWritten() to access the written data, e.g. to drain
	 * via UART on next boot:
	 * @code
	 *   uartSend( writer.data(), writer.bytesWritten() );
	 * @endcode
	 */
	const std::uint8_t* data() const noexcept;

	/**
	 * @brief Write data into the RAM buffer.
	 *
	 * Copies as many bytes as fit in the remaining space.  Returns the
	 * number of bytes actually written.  EmergencySink treats a short
	 * write as a truncation and marks the record accordingly.
	 *
	 * @param data pointer to data to write
	 * @param size number of bytes to write
	 * @return number of bytes written (0 if buffer is full)
	 */
	std::size_t write( const void* data, std::size_t size ) noexcept override;

	/**
	 * @brief No-op - RAM writes are immediately visible.
	 */
	void flush() noexcept override;

	/**
	 * @brief Reset the write position to zero.
	 *
	 * Call this at startup if the buffer may contain stale data from a
	 * previous run and you are not using a no-init section.
	 */
	void reset() noexcept;
};

} // namespace kmac::flare

#endif // KMAC_FLARE_RAM_WRITER_H
