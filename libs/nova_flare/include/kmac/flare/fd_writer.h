#pragma once
#ifndef KMAC_FLARE_FD_WRITER_H
#define KMAC_FLARE_FD_WRITER_H

// NOTE: MinGW and some Windows SDK configurations provide a <unistd.h> compatibility header,
// declaring a subset of the POSIX functions, so the __has_include test won't work...
//#if __has_include( <unistd.h> )

// ... instead, use explicit environment checks
#if defined( __linux__ ) || defined( __unix__ ) || defined( __APPLE__ ) || defined( __FreeBSD__ ) || defined( __ANDROID__ )

#include "iwriter.h"

#include <cstddef>
#include <cstdint>

namespace kmac::flare
{

/**
 * @brief Async-signal-safe IWriter implementation for POSIX file descriptors.
 *
 * FdWriter writes Flare records via raw POSIX syscalls (write / fsync),
 * making it safe to use from signal handlers where FILE*-based I/O is not.
 * std::fwrite and std::fflush (used by FileWriter) are not async-signal-safe;
 * the raw write() and fsync() syscalls are.
 *
 * This header is only available on platforms with <unistd.h> support
 * (Linux, macOS, Android, FreeBSD).
 *
 * Prefer FdWriter over FileWriter when EmergencySink is used inside a
 * signal handler (SIGSEGV, SIGABRT, etc.).
 *
 * Usage:
 * @code
 *   // open with O_WRONLY | O_CREAT | O_APPEND for append-safe crash logs
 *   int fd = ::open( "crash.flare", O_WRONLY | O_CREAT | O_APPEND, 0644 );
 *   kmac::flare::FdWriter writer( fd );
 *   kmac::flare::EmergencySink<> sink( &writer );
 * @endcode
 *
 * @note The file descriptor is not owned by FdWriter; the caller is
 * responsible for opening and closing it.
 *
 * @note fsync() blocks until the OS has flushed data to the storage
 * device.  This is the strongest available durability guarantee and is
 * appropriate for crash logging.  If fsync latency is unacceptable for
 * non-crash use, pass flushMode=FlushMode::None to the constructor and
 * call fsync manually at process exit.
 *
 * @note Thread safety: not thread-safe.  Each thread should use its own
 * FdWriter, or access must be serialized externally.
 */
class FdWriter final : public IWriter
{
public:
	/**
	 * @brief Controls what flush() does with the underlying file descriptor.
	 *
	 * For crash logging, Fsync is the right choice: it guarantees data
	 * reaches non-volatile storage before the process terminates.
	 *
	 * For non-crash diagnostic use where latency matters more than
	 * durability, None skips the syscall entirely.
	 */
	enum class FlushMode : std::uint8_t
	{
		Fsync,  ///< call fsync() on flush() (strongest durability guarantee)
		None,   ///< no-op flush (data reaches disk only via OS writeback)
	};

private:
	int _fd;
	FlushMode _flushMode;

public:
	/**
	 * @brief Construct FdWriter with a POSIX file descriptor.
	 *
	 * @param fileDescriptor open file descriptor for writing (not owned)
	 * @param flushMode controls flush() behaviour (default: Fsync)
	 *
	 * @note fd must be open in write mode (O_WRONLY or O_RDWR)
	 * @note caller is responsible for opening and closing fd
	 */
	explicit FdWriter( int fileDescriptor, FlushMode flushMode = FlushMode::Fsync ) noexcept;

	/**
	 * @brief Write data via POSIX write().
	 *
	 * Calls write() in a loop to handle short writes (EINTR, partial writes).
	 * Returns the total number of bytes written; a value less than size
	 * indicates a write error.
	 *
	 * @param data pointer to data to write
	 * @param size number of bytes to write
	 * @return number of bytes actually written
	 *
	 * @note async-signal-safe
	 */
	std::size_t write( const void* data, std::size_t size ) noexcept override;

	/**
	 * @brief Flush via fsync() or no-op, depending on FlushMode.
	 *
	 * With FlushMode::Fsync, blocks until the OS has written all buffered
	 * data for this fd to the storage device.  This is the appropriate
	 * behaviour for crash logging where the process may terminate immediately
	 * after the flush.
	 *
	 * @note async-signal-safe (fsync is async-signal-safe on Linux/POSIX)
	 */
	void flush() noexcept override;
};

} // namespace kmac::flare

#endif // defined( __linux__ ) || ...

//#endif // __has_include( <unistd.h> )

#endif // KMAC_FLARE_FD_WRITER_H
