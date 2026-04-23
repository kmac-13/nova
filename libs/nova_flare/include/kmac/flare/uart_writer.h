#pragma once
#ifndef KMAC_FLARE_UART_WRITER_H
#define KMAC_FLARE_UART_WRITER_H

#include "iwriter.h"

#include <cstddef>

namespace kmac {
namespace flare {

/**
 * @file uart_writer.h
 * @brief IWriter implementation that writes Flare records via a UART callback.
 *
 * UartWriter delegates all output to a user-supplied function pointer, making
 * it compatible with any UART hardware abstraction layer without imposing a
 * specific HAL dependency on the Flare library.
 *
 * The write function must be async-signal-safe - it should write directly to
 * a hardware register or a minimal ring buffer, with no heap allocation, no
 * stdio, and no OS calls.
 *
 * Key characteristics:
 * - no heap allocation
 * - no OS or stdlib dependencies
 * - hardware-agnostic via function pointer
 * - synchronous write (no internal buffering)
 * - flush() calls an optional user-supplied flush callback
 *
 * Synchronous vs buffered:
 * For crash safety, synchronous output (blocking until the UART TX register
 * accepts each byte) is preferred over ring-buffer approaches - a crash
 * mid-write cannot corrupt a partially-drained software buffer.  If your HAL
 * provides a blocking write function, use that directly.
 *
 * Usage:
 * @code
 *   // HAL function that writes bytes synchronously to UART
 *   extern "C" void HAL_UART_Transmit( const uint8_t* data, size_t len );
 *
 *   static kmac::flare::UartWriter uartWriter(
 *       []( const void* data, std::size_t len ) noexcept -> std::size_t
 *       {
 *           HAL_UART_Transmit( static_cast< const uint8_t* >( data ), len );
 *           return len;  // assume success; adapt if HAL returns error codes
 *       }
 *   );
 *   static kmac::flare::EmergencySink emergencySink( &uartWriter );
 * @endcode
 *
 * With a flush callback (optional):
 * @code
 *   static kmac::flare::UartWriter uartWriter(
 *       []( const void* data, std::size_t len ) noexcept -> std::size_t {
 *           HAL_UART_Transmit( static_cast< const uint8_t* >( data ), len );
 *           return len;
 *       },
 *       []() noexcept {
 *           HAL_UART_WaitTxComplete();  // block until TX FIFO drains
 *       }
 *   );
 * @endcode
 *
 * @note write() returns the value from the write callback, return fewer than
 *       size bytes to signal an error; EmergencySink will mark the record Truncated.
 * @note flush() is a no-op if no flush callback is provided.
 * @note thread safety: depends on the thread safety of the provided callbacks.
 */
class UartWriter final : public IWriter
{
public:
	/**
	 * @brief Function pointer type for the write callback.
	 *
	 * @param data pointer to data to write
	 * @param size number of bytes to write
	 * @return number of bytes actually written
	 */
	using WriteFn = std::size_t (*)( const void* data, std::size_t size );  // noexcept not allowed before C++17

	/**
	 * @brief Function pointer type for the optional flush callback.
	 */
	using FlushFn = void (*)();  // noexcept not allowed before C++17

private:
	WriteFn _writeFn = nullptr;     ///< required write callback
	FlushFn _flushFn = nullptr;     ///< optional flush callback (nullptr = no-op)

public:
	/**
	 * @brief Construct a UartWriter with a write callback and optional flush callback.
	 *
	 * @param writeFn  function that writes bytes to the UART (must not be nullptr)
	 * @param flushFn  function called on flush() - pass nullptr for no-op (default)
	 */
	explicit UartWriter( WriteFn writeFn, FlushFn flushFn = nullptr ) noexcept;

	/**
	 * @brief Write data via the UART callback.
	 *
	 * @param data pointer to data to write
	 * @param size number of bytes to write
	 * @return number of bytes written as reported by the callback
	 */
	std::size_t write( const void* data, std::size_t size ) noexcept override;

	/**
	 * @brief Flush via the optional flush callback.
	 *
	 * If no flush callback was provided at construction, this is a no-op.
	 * Useful for blocking until a UART TX FIFO drains before allowing
	 * execution to continue after a crash log.
	 */
	void flush() noexcept override;
};

} // namespace flare
} // namespace kmac

#endif // KMAC_FLARE_UART_WRITER_H
