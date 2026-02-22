#include "kmac/nova/extras/null_sink.h"

namespace kmac::nova::extras
{

NullSink& NullSink::instance() noexcept
{
	static NullSink instance;
	return instance;
}

void NullSink::process( const kmac::nova::Record& ) noexcept
{
	// no-op
}

} // namespace kmac::nova::extras
