#include "kmac/nova/extras/ostream_sink.h"

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
	if ( _flushOnWrite )
	{
		_stream->flush();
	}
}

} // namespace kmac::nova::extras
