#include "kmac/flare/fd_writer.h"

#if defined( __linux__ ) || defined( __unix__ ) || defined( __APPLE__ ) || defined( __FreeBSD__ ) || defined( __ANDROID__ )

#include <unistd.h>  // write(), fsync()

namespace kmac {
namespace flare {

FdWriter::FdWriter( int fileDescriptor, FlushMode flushMode ) noexcept
	: _fd( fileDescriptor )
	, _flushMode( flushMode )
{
}

std::size_t FdWriter::write( const void* data, std::size_t size ) noexcept
{
	if ( _fd < 0 || data == nullptr || size == 0 )
	{
		return 0;
	}

	// loop to handle short writes (EINTR, partial writes on pipes/sockets)
	const auto* src = static_cast< const char* >( data );
	std::size_t remaining = size;

	while ( remaining > 0 )
	{
		const ::ssize_t written = ::write( _fd, src, remaining );

		if ( written <= 0 )
		{
			// write error or EOF; return however many bytes we managed
			break;
		}

		src += written;
		remaining -= static_cast< std::size_t >( written );
	}

	return size - remaining;
}

void FdWriter::flush() noexcept
{
	if ( _fd >= 0 && _flushMode == FlushMode::Fsync )
	{
		// return value intentionally ignored: if fsync fails in a crash
		// handler there is nothing useful we can do
		(void) ::fsync( _fd );
	}
}

} // namespace flare
} // namespace kmac

#endif // defined( __linux__ ) || ...
