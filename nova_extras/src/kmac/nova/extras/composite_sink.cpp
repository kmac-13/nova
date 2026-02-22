#include "kmac/nova/extras/composite_sink.h"

namespace kmac::nova::extras
{

void CompositeSink::add( kmac::nova::Sink& sink ) noexcept
{
	_sinks.push_back( &sink );
}

void CompositeSink::clear() noexcept
{
	_sinks.clear();
}

void CompositeSink::process( const kmac::nova::Record& record ) noexcept
{
	for ( kmac::nova::Sink* sink : _sinks )
	{
		if ( sink != nullptr )
		{
			sink->process( record );
		}
	}
}

} // namespace kmac::nova::extras
