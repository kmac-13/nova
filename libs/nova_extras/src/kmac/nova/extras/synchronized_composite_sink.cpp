#include "kmac/nova/extras/synchronized_composite_sink.h"

#include <kmac/nova/record.h>
#include <kmac/nova/sink.h>
#include <kmac/nova/extras/composite_sink.h>

#include <mutex>

namespace kmac {
namespace nova {
namespace extras {

SynchronizedCompositeSink::SynchronizedCompositeSink( CompositeSink& composite ) noexcept
	: _composite( &composite )
{
}

void SynchronizedCompositeSink::addSink( kmac::nova::Sink& sink ) noexcept
{
	const std::lock_guard< std::mutex > lock( _mutex );
	_composite->add( sink );
}

void SynchronizedCompositeSink::clearSinks() noexcept
{
	const std::lock_guard< std::mutex > lock( _mutex );
	_composite->clear();
}

void SynchronizedCompositeSink::process( const kmac::nova::Record& record ) noexcept
{
	const std::lock_guard< std::mutex > lock( _mutex );
	_composite->process( record );
}

} // namespace extras
} // namespace nova
} // namespace kmac
