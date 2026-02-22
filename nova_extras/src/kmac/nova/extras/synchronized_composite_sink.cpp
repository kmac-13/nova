#include "kmac/nova/extras/synchronized_composite_sink.h"

namespace kmac::nova::extras
{

SynchronizedCompositeSink::SynchronizedCompositeSink( CompositeSink& composite ) noexcept
	: _composite( &composite )
{
}

void SynchronizedCompositeSink::addSink( kmac::nova::Sink& sink ) noexcept
{
	std::lock_guard< std::mutex > lock( _mutex );
	_composite->add( sink );
}

void SynchronizedCompositeSink::clearSinks() noexcept
{
	std::lock_guard< std::mutex > lock( _mutex );
	_composite->clear();
}

void SynchronizedCompositeSink::process( const kmac::nova::Record& record ) noexcept
{
	std::lock_guard< std::mutex > lock( _mutex );
	_composite->process( record );
}

} // namespace kmac::nova::extras
