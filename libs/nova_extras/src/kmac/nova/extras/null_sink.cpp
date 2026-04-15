#include "kmac/nova/extras/null_sink.h"

namespace kmac {
namespace nova {
namespace extras {

NullSink& NullSink::instance() noexcept
{
	static NullSink instance;
	return instance;
}

void NullSink::process( const kmac::nova::Record& ) noexcept
{
	// no-op
}

} // namespace extras
} // namespace nova
} // namespace kmac
