#pragma once
#ifndef KMAC_NOVA_EXTRAS_RAM_SINK_H
#define KMAC_NOVA_EXTRAS_RAM_SINK_H

/**
 * @file ram_sink.h
 * @brief RAM sink for Nova - bare-metal and RTOS targets.
 *
 * ✅ SUITABLE FOR BARE-METAL AND RTOS USE
 * ✅ NO HEAP ALLOCATION
 * ✅ NO STDLIB DEPENDENCY
 *
 * Provides RamSink, a sink that writes formatted log records into a
 * user-supplied fixed-size buffer.  Intended for targets without a UART,
 * file system, or console - log output accumulates in RAM and can be read
 * out via JTAG, a debugger, or a custom protocol.
 *
 * This sink is distinct from Flare's RamWriter.  Flare's RamWriter is
 * tightly coupled to Flare's TLV binary wire format and EmergencySink, and
 * is intended for crash-safe forensic logging.  RamSink is a general-purpose
 * sink for normal-path logging with no crash-safety overhead or binary
 * encoding.
 *
 * Buffer layout:
 *   Records are written sequentially into the buffer.  When the buffer is
 *   full, new records are dropped and the overflow count is incremented.
 *   The buffer is NOT circular - call reset() to clear it and begin again.
 *
 *   [ record 0 bytes ][ record 1 bytes ][ record 2 bytes ][ unused... ]
 *    ^                                                     ^
 *    buf                                                   buf + bytesWritten()
 *
 * Overflow:
 *   Records that do not fit in the remaining buffer space are silently
 *   dropped.  Call overflowCount() to detect dropped records.  Consider
 *   sizing the buffer generously or flushing (reading and resetting) before
 *   it fills.
 *
 * Thread safety:
 *   RamSink does not provide internal synchronisation.  Wrap with
 *   SynchronizedSink if concurrent log calls are possible.
 *
 * Usage - basic (raw message bytes only):
 *
 *   static char logBuf[ 4096 ];
 *   RamSink<> sink( logBuf, sizeof( logBuf ) );
 *
 *   ScopedConfigurator config;
 *   config.bind< SensorTag >( &sink );
 *
 *   NOVA_LOG( SensorTag ) << "reading=" << val;
 *
 *   // later - read via JTAG or pass to a transmission function:
 *   myTransmit( sink.data(), sink.bytesWritten() );
 *   sink.reset();
 *
 * Usage - formatted (with ISO 8601 timestamp, tag, file, line):
 *
 *   static char logBuf[ 4096 ];
 *   RamSink<> ramSink( logBuf, sizeof( logBuf ) );
 *   ISO8601Formatter formatter;
 *   FormattingSink<> sink( ramSink, formatter );
 *
 *   ScopedConfigurator config;
 *   config.bind< SensorTag >( &sink );
 *
 *   NOVA_LOG( SensorTag ) << "reading=" << val;
 *   // buffer contains: "2025-02-07T12:34:56.789Z [SENSOR] sensor.cpp:42 read - reading=123\n"
 *
 * Usage - statically allocated buffer inside the sink (template parameter):
 *
 *   RamSink< 4096 > sink;
 *   // sink owns its own 4096-byte internal buffer
 *   // no external buffer required
 *
 *   ScopedConfigurator config;
 *   config.bind< SensorTag >( &sink );
 */

#include <kmac/nova/platform/config.h>

#include <kmac/nova/platform/array.h>
#include <kmac/nova/record.h>
#include <kmac/nova/sink.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace kmac {
namespace nova {
namespace extras {

/**
 * @brief Sink that writes log records into a fixed-size RAM buffer.
 *
 * See file-level documentation for usage, buffer layout, and overflow
 * behaviour.
 *
 * Two construction modes are supported:
 *
 * External buffer (InternalSize == 0, default):
 *   The sink writes into a user-supplied buffer.  The buffer pointer and
 *   size are passed at construction and must remain valid for the sink
 *   lifetime.
 *
 *   static char buf[ 4096 ];
 *   RamSink<> sink( buf, sizeof( buf ) );
 *
 * Internal buffer (InternalSize > 0):
 *   The sink owns a statically-allocated internal buffer of InternalSize
 *   bytes.  No external buffer is required.
 *
 *   RamSink< 4096 > sink;
 *
 * @tparam InternalSize if > 0, the sink owns an internal buffer of this size
 *     and the external-buffer constructor is disabled.  If 0 (default), the
 *     sink uses an external user-supplied buffer.
 */
template< std::size_t InternalSize = 0 >
class RamSink final : public kmac::nova::Sink
{
private:
	kmac::nova::platform::Array< char, InternalSize > _internalBuf {};  ///< internal buffer (zero-size array when unused)
	char* _buf = nullptr;            ///< pointer to active buffer (internal or external)
	std::size_t _capacity = 0;       ///< total buffer capacity in bytes
	std::size_t _bytesWritten = 0;   ///< bytes written since last reset
	std::size_t _overflowCount = 0;  ///< records dropped due to full buffer

public:
	/**
	 * @brief Construct with an internally-owned buffer.
	 *
	 * Only available when InternalSize > 0.
	 */
	RamSink() noexcept;

	/**
	 * @brief Pointer to the start of the buffer.
	 *
	 * Valid bytes are [ data(), data() + bytesWritten() ).
	 *
	 * @return pointer to buffer start
	 */
	const char* data() const noexcept;

	/**
	 * @brief Total capacity of the buffer in bytes.
	 *
	 * @return buffer capacity
	 */
	std::size_t capacity() const noexcept;

	/**
	 * @brief Number of bytes written since construction or last reset().
	 *
	 * @return bytes written
	 */
	std::size_t bytesWritten() const noexcept;

	/**
	 * @brief Number of records dropped due to insufficient buffer space.
	 *
	 * @return overflow count
	 */
	std::size_t overflowCount() const noexcept;

	/**
	 * @brief Write log record into the RAM buffer.
	 *
	 * Appends record.message bytes to the buffer.  If the record does not fit
	 * in the remaining space, it is dropped and overflowCount() is incremented.
	 * No null termination or newline is added - pair with FormattingSink if
	 * those are required.
	 *
	 * @param record log record to write
	 */
	void process( const kmac::nova::Record& record ) noexcept override;

	/**
	 * @brief Reset the sink - clear written bytes and overflow count.
	 *
	 * Does not zero the buffer contents - only the write position and
	 * overflow counter are reset.  Existing bytes remain in memory until
	 * overwritten by subsequent log calls.
	 */
	void reset() noexcept;
};

// ----- external buffer specialisation (InternalSize == 0) -------------------

template<>
class RamSink< 0 > final : public kmac::nova::Sink
{
private:
	char* _buf = nullptr;
	std::size_t _capacity = 0;
	std::size_t _bytesWritten = 0;
	std::size_t _overflowCount = 0;

public:
	/**
	 * @brief Construct with a user-supplied external buffer.
	 *
	 * @param buf pointer to buffer (must remain valid for sink lifetime)
	 * @param capacity total size of the buffer in bytes
	 */
	RamSink( char* buf, std::size_t capacity ) noexcept;

	const char* data() const noexcept;
	std::size_t capacity() const noexcept;
	std::size_t bytesWritten() const noexcept;
	std::size_t overflowCount() const noexcept;

	void process( const kmac::nova::Record& record ) noexcept override;

	void reset() noexcept;
};

inline RamSink< 0 >::RamSink( char* buf, std::size_t capacity ) noexcept
	: _buf( buf )
	, _capacity( capacity )
{
	NOVA_ASSERT( buf != nullptr && "RamSink: buf must not be null" );
	NOVA_ASSERT( capacity > 0 && "RamSink: capacity must be greater than zero" );
}

inline const char* RamSink< 0 >::data() const noexcept
{
	return _buf;
}

inline std::size_t RamSink< 0 >::capacity() const noexcept
{
	return _capacity;
}

inline std::size_t RamSink< 0 >::bytesWritten() const noexcept
{
	return _bytesWritten;
}

inline std::size_t RamSink< 0 >::overflowCount() const noexcept
{
	return _overflowCount;
}

inline void RamSink< 0 >::process( const kmac::nova::Record& record ) noexcept
{
	const std::size_t remaining = _capacity - _bytesWritten;

	if ( record.messageSize > remaining )
	{
		++_overflowCount;
		return;
	}

	std::memcpy( _buf + _bytesWritten, record.message, record.messageSize );
	_bytesWritten += record.messageSize;
}

inline void RamSink< 0 >::reset() noexcept
{
	_bytesWritten = 0;
	_overflowCount = 0;
}

// ----- internal buffer primary template (InternalSize > 0) ------------------

template< std::size_t InternalSize >
RamSink< InternalSize >::RamSink() noexcept
	: _buf( _internalBuf.data() )
	, _capacity( InternalSize )
{
	static_assert( InternalSize >= 16, "InternalSize must be at least 16 bytes" );
	static_assert( InternalSize <= std::size_t( 1024 ) * 1024, "InternalSize must not exceed 1MB (stack safety)" );
}

template< std::size_t InternalSize >
const char* RamSink< InternalSize >::data() const noexcept
{
	return _buf;
}

template< std::size_t InternalSize >
std::size_t RamSink< InternalSize >::capacity() const noexcept
{
	return _capacity;
}

template< std::size_t InternalSize >
std::size_t RamSink< InternalSize >::bytesWritten() const noexcept
{
	return _bytesWritten;
}

template< std::size_t InternalSize >
std::size_t RamSink< InternalSize >::overflowCount() const noexcept
{
	return _overflowCount;
}

template< std::size_t InternalSize >
void RamSink< InternalSize >::process( const kmac::nova::Record& record ) noexcept
{
	const std::size_t remaining = _capacity - _bytesWritten;

	if ( record.messageSize > remaining )
	{
		++_overflowCount;
		return;
	}

	std::memcpy( _buf + _bytesWritten, record.message, record.messageSize );
	_bytesWritten += record.messageSize;
}

template< std::size_t InternalSize >
void RamSink< InternalSize >::reset() noexcept
{
	_bytesWritten = 0;
	_overflowCount = 0;
}

} // namespace extras
} // namespace nova
} // namespace kmac

#endif // KMAC_NOVA_EXTRAS_RAM_SINK_H
