#pragma once
#ifndef KMAC_FLARE_IWRITER_H
#define KMAC_FLARE_IWRITER_H

#include <kmac/nova/immovable.h>

#include <cstddef>

namespace kmac::flare
{

/**
 * @brief Abstract interface for writing Flare log data.
 *
 * The IWriter interface allows users to provide custom I/O implementations
 * for platforms without FILE* support (bare-metal, RTOS, etc.).
 *
 * Requirements:
 * - must be async-signal-safe (no heap allocation, locks, etc.)
 * - write() must return number of bytes actually written
 * - flush() must ensure data is persisted (or no-op if not applicable)
 *
 * Thread safety:
 * - implementation must be thread-safe if used from multiple threads
 * - EmergencySink does not provide synchronization
 *
 * Usage:
 *   class CustomWriter : public IWriter {
 *       size_t write(const void* data, size_t size) noexcept override {
 *           // custom write implementation
 *           return size;
 *       }
 *       void flush() noexcept override {
 *           // custom flush implementation
 *       }
 *   };
 */
class IWriter : private kmac::nova::Immovable
{
public:
	virtual ~IWriter() = default;

	/**
	* @brief Write data to the output.
	*
	* @param data pointer to data to write
	* @param size number of bytes to write
	* @return number of bytes actually written (may be less than size on error)
	*
	* @note must be async-signal-safe (no heap allocation, locks, etc.)
	* @note should not throw exceptions
	* @note return value < size indicates error (data may be truncated)
	*/
	virtual std::size_t write( const void* data, std::size_t size ) noexcept = 0;

	/**
	* @brief Flush any buffered data to persistent storage.
	*
	* For crash-safety, this should ensure data is written to non-volatile
	* storage (disk, flash, etc.). For volatile storage (RAM, UART), this
	* can be a no-op.
	*
	* @note must be async-signal-safe
	* @note should not throw exceptions
	*/
	virtual void flush() noexcept = 0;
};

} // namespace kmac::flare

#endif // KMAC_FLARE_IWRITER_H
