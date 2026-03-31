#include "kmac/nova/extras/fixed_composite_sink.h"

namespace kmac::nova::extras
{

FixedCompositeSink::FixedCompositeSink( Sink** sinks, std::size_t count ) noexcept
	: _sinks( sinks )
	, _count( count )
{
}

void FixedCompositeSink::process( const kmac::nova::Record& record ) noexcept
{
	for ( std::size_t i = 0; i < _count; ++i )
	{
		kmac::nova::Sink* sink = _sinks[ i ];
		if ( sink != nullptr )
		{
			sink->process( record );
		}
	}
}

} // namespace kmac::nova::extras
