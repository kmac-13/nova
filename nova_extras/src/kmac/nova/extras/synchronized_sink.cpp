#include "kmac/nova/extras/synchronized_sink.h"

namespace kmac::nova::extras
{

SynchronizedSink::SynchronizedSink( kmac::nova::Sink& downstream ) noexcept
	: _downstream( &downstream )
{
}

void SynchronizedSink::process( const kmac::nova::Record& record ) noexcept
{
	std::lock_guard< std::mutex > lock( _mutex );
	_downstream->process( record );
}

} // namespace kmac::nova::extras
