#pragma once
#ifndef KMAC_NOVA_EXTRAS_UART_SINK_H
#define KMAC_NOVA_EXTRAS_UART_SINK_H

/**
 * @file uart_sink.h
 * @brief UART sink for Nova - bare-metal and RTOS targets.
 *
 * ✅ SUITABLE FOR BARE-METAL AND RTOS USE
 * ✅ NO HEAP ALLOCATION
 * ✅ NO STDLIB DEPENDENCY
 *
 * Provides UartSink, a sink that writes formatted log records to a UART by
 * delegating to a user-supplied write function.  The user is responsible for
 * providing a write function that matches the platform's UART driver.
 *
 * UartSink does not perform any formatting - pair it with FormattingSink and
 * a formatter (e.g. ISO8601Formatter) if human-readable output is required.
 * If raw message bytes are sufficient, bind UartSink directly.
 *
 * This sink is distinct from Flare's UartWriter.  Flare's UartWriter is
 * tightly coupled to Flare's TLV binary wire format and is intended for
 * crash-safe forensic logging.  UartSink is a general-purpose sink for
 * normal-path logging with no crash-safety overhead or binary encoding.
 *
 * Write function:
 *   The write function receives a pointer to the data and the byte count to
 *   write.  It must not be null.  A captureless lambda converts implicitly
 *   to a function pointer:
 *
 *   UartSink<> sink( []( const char* data, std::size_t size ) noexcept
 *   {
 *       HAL_UART_Transmit( &huart1,
 *           reinterpret_cast< const uint8_t* >( data ),
 *           static_cast< uint16_t >( size ),
 *           HAL_MAX_DELAY );
 *   } );
 *
 * Thread safety:
 *   UartSink does not provide internal synchronisation.  If multiple threads
 *   or ISRs log to the same sink concurrently, wrap with SynchronizedSink or
 *   ensure the write function itself is thread-safe (e.g. uses a UART DMA
 *   queue with its own locking).
 *
 * Usage - direct (raw message bytes only):
 *
 *   UartSink<> sink( []( const char* data, std::size_t size ) noexcept
 *   {
 *       myUartWrite( data, size );
 *   } );
 *
 *   ScopedConfigurator config;
 *   config.bind< NetworkTag >( &sink );
 *
 *   NOVA_LOG( NetworkTag ) << "connecting";
 *   // UART receives raw message bytes: "connecting"
 *
 * Usage - formatted (with ISO 8601 timestamp, tag, file, line):
 *
 *   UartSink<> uartSink( []( const char* data, std::size_t size ) noexcept
 *   {
 *       myUartWrite( data, size );
 *   } );
 *   ISO8601Formatter formatter;
 *   FormattingSink<> sink( uartSink, formatter );
 *
 *   ScopedConfigurator config;
 *   config.bind< NetworkTag >( &sink );
 *
 *   NOVA_LOG( NetworkTag ) << "connecting";
 *   // UART receives: "2025-02-07T12:34:56.789Z [NETWORK] net.cpp:42 connect - connecting\n"
 */

#include <kmac/nova/platform/config.h>

#include <kmac/nova/record.h>
#include <kmac/nova/sink.h>

#include <cstddef>
#include <cstdint>

namespace kmac {
namespace nova {
namespace extras {

/**
 * @brief Sink that writes log records to a UART via a user-supplied write function.
 *
 * See file-level documentation for usage and threading guidance.
 *
 * @tparam BufferSize not used for writing (records are written directly from
 *     record.message); reserved for future use and kept consistent with other
 *     sink templates.  Default 1024 bytes.
 */
template< std::size_t BufferSize = 1024 >
class UartSink final : public kmac::nova::Sink
{
	static_assert( BufferSize >= 16, "BufferSize must be at least 16 bytes" );
	static_assert( BufferSize <= 64 * 1024, "BufferSize must not exceed 64KB" );

public:
	/**
	 * @brief Function pointer type for the UART write function.
	 *
	 * Called once per log record with a pointer to the data bytes and the
	 * byte count.  Must not be null.  Must not throw.
	 *
	 * @param data pointer to bytes to write
	 * @param size number of bytes to write
	 */
	using WriteFunc = void (*)( const char* data, std::size_t size ) noexcept;

private:
	WriteFunc _writeFunc;  ///< user-supplied UART write function

public:
	/**
	 * @brief Construct with a UART write function.
	 *
	 * @param writeFunc function to call to write bytes to the UART; must not be null
	 */
	explicit UartSink( WriteFunc writeFunc ) noexcept;

	/**
	 * @brief Write log record to the UART.
	 *
	 * Passes record.message and record.messageSize directly to the write
	 * function.  No null termination, newline, or additional formatting is
	 * added - pair with FormattingSink if those are required.
	 *
	 * @param record log record to write
	 */
	void process( const kmac::nova::Record& record ) noexcept override;
};

template< std::size_t BufferSize >
UartSink< BufferSize >::UartSink( WriteFunc writeFunc ) noexcept
	: _writeFunc( writeFunc )
{
	NOVA_ASSERT( writeFunc != nullptr && "UartSink: writeFunc must not be null" );
}

template< std::size_t BufferSize >
void UartSink< BufferSize >::process( const kmac::nova::Record& record ) noexcept
{
	_writeFunc( record.message, record.messageSize );
}

} // namespace extras
} // namespace nova
} // namespace kmac

#endif // KMAC_NOVA_EXTRAS_UART_SINK_H
