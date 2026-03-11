#include "kmac/flare/file_writer.h"

namespace kmac::flare
{

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
	// NOTE: silencing disregarded fflush return value
	if ( _file ) (void) std::fflush( _file );
}

} // kmac::flare
