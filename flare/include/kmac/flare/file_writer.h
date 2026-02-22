#pragma once
#ifndef KMAC_FLARE_FILE_WRITER_H
#define KMAC_FLARE_FILE_WRITER_H

#if __has_include( <cstdio> )

// #include "emergency_sink.h"
#include "iwriter.h"

#include <cstdio>

namespace kmac::flare
{

/**
 * @brief IWriter interface adapter for FILE*.
 * 
 * Allows using EmergencySink with standard C FILE* handles.
 * This header is only available on platforms with <cstdio> support.
 * 
 * On platforms without FILE*, use EmergencySink directly with
 * a custom WriteInterface implementation.
 * 
 * Thread safety:
 * - not thread-safe (FILE* operations are not thread-safe)
 * - wrap with SynchronizedSink if needed
 * 
 * Example:
 *   FILE* f = fopen("crash.flare", "wb");
 *   FileWriter writer(f);
 *   EmergencySink sink(&writer);
 */
class FileWriter final : public IWriter
{
private:
	std::FILE* _file;

public:
	/**
	 * @brief Construct FileWriter with FILE* handle.
	 * 
	 * @param file open FILE* for writing (not owned, must remain valid)
	 * 
	 * @note FILE* must be opened in binary write mode ("wb" or "ab")
	 * @note caller is responsible for opening/closing FILE*
	 */
	explicit FileWriter( std::FILE* file ) noexcept;

	/**
	 * @brief Write data to FILE*.
	 * 
	 * @param data pointer to data to write
	 * @param size number of bytes to write
	 * @return number of bytes actually written
	 * 
	 * @note uses std::fwrite (not async-signal-safe)
	 * @note for truly async-signal-safe operation, use a syscall-based writer
	 */
	inline std::size_t write( const void* data, std::size_t size ) noexcept override;

	/**
	 * @brief Flush FILE* buffer.
	 * 
	 * For crash-safety, this ensures data is written to the OS.
	 * For full persistence to disk, consider calling fsync() on the
	 * underlying file descriptor.
	 * 
	 * @note uses std::fflush (not async-signal-safe)
	 */
	inline void flush() noexcept override;
};

FileWriter::FileWriter( std::FILE* file ) noexcept
	: _file( file )
{
}

std::size_t FileWriter::write( const void* data, std::size_t size ) noexcept
{
	if ( !_file ) return 0;
	return std::fwrite( data, 1, size, _file );
}

void FileWriter::flush() noexcept
{
	if ( _file ) std::fflush( _file );
}


/**
 * @brief Convenience typedef for FILE*-based emergency sink.
 * 
 * Usage:
 *   FILE* f = fopen( "crash.flare", "wb" );
 *   FileWriter writer( f );
 *   EmergencySink sink( &writer );
 * 
 * Or use the helper function:
 *   auto sink = makeFileSink( f );
 *
 * @note this doesn't work due to single instance of FileWriter per thread
 */
// inline EmergencySink makeEmergencyFileSink( std::FILE* file, bool captureProcessInfo = false )
// {
//	static thread_local FileWriter writer( file );
//	return EmergencySink( &writer, captureProcessInfo );
// }

} // namespace kmac::flare

#endif // __has_include <cstdio>

#endif // KMAC_FLARE_FILE_SINK_H
