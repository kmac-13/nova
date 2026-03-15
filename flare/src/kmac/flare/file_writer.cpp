#include "kmac/flare/file_writer.h"

namespace kmac::flare
{

FileWriter::FileWriter( std::FILE* file ) noexcept
	: _file( file )
{
}

std::size_t FileWriter::write( const void* data, std::size_t size ) noexcept
{
	if ( _file == nullptr )
	{
		return 0;
	}
	return std::fwrite( data, 1, size, _file );
}

void FileWriter::flush() noexcept
{
	if ( _file != nullptr )
	{
		// NOTE: silencing disregarded fflush return value
		(void) std::fflush( _file );
	}
}

} // kmac::flare
