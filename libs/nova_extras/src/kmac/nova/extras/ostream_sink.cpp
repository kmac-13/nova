#include "kmac/nova/extras/ostream_sink.h"

#include <kmac/nova/record.h>

#include <ostream>

namespace kmac {
namespace nova {
namespace extras {

OStreamSink::OStreamSink( std::ostream& stream, bool flushOnWrite ) noexcept
	: _stream( &stream )
	, _flushOnWrite( flushOnWrite )
{
}

void OStreamSink::process( const kmac::nova::Record& record ) noexcept
{
	_stream->write( record.message, static_cast< std::streamsize >( record.messageSize ) );
	_stream->write( "\n", 1 );
	if ( _flushOnWrite )
	{
		_stream->flush();
	}
}

} // namespace extras
} // namespace nova
} // namespace kmac
