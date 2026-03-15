#include "kmac/nova/extras/ostream_sink.h"

#include "kmac/nova/record.h"

#include <ostream>

namespace kmac::nova::extras
{

OStreamSink::OStreamSink( std::ostream& stream, bool flushOnWrite ) noexcept
	: _stream( &stream )
	, _flushOnWrite( flushOnWrite )
{
}

void OStreamSink::process( const kmac::nova::Record& record ) noexcept
{
	_stream->write( record.message, record.messageSize );
	_stream->write( "\n", 1 );
	if ( _flushOnWrite )
	{
		_stream->flush();
	}
}

} // namespace kmac::nova::extras
