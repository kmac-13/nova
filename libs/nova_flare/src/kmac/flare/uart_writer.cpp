#include "kmac/flare/uart_writer.h"

namespace kmac {
namespace flare {

UartWriter::UartWriter( WriteFn writeFn, FlushFn flushFn ) noexcept
	: _writeFn( writeFn )
	, _flushFn( flushFn )
{
}

std::size_t UartWriter::write( const void* data, std::size_t size ) noexcept
{
	if ( _writeFn == nullptr || size == 0 )
	{
		return 0;
	}

	return _writeFn( data, size );
}

void UartWriter::flush() noexcept
{
	if ( _flushFn != nullptr )
	{
		_flushFn();
	}
}

} // namespace flare
} // namespace kmac
