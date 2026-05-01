#include "kmac/nova/extras/quick_start.h"

namespace kmac {
namespace nova {
namespace extras {

QuickStart::QuickStart( Severity minSeverity ) noexcept
	: QuickStart( std::cout, minSeverity )
{
}

QuickStart::QuickStart( std::ostream& stream, Severity minSeverity ) noexcept
	: _ostreamSink( stream )
	, _formatter()
	, _formattingSink( _ostreamSink, _formatter )
	, _synchronizedSink( _formattingSink )
	, _configurator()
{
	bindSeverities( minSeverity );
}

void QuickStart::bindSeverities( Severity minSeverity ) noexcept
{
	// each case falls through to bind all tags at or above minSeverity
	switch ( minSeverity )
	{
	case Severity::Trace:
		_configurator.bind< TraceTag >( &_synchronizedSink );
		// fallthrough intentional
		[[fallthrough]];

	case Severity::Debug:
		_configurator.bind< DebugTag >( &_synchronizedSink );
		// fallthrough intentional
		[[fallthrough]];

	case Severity::Info:
		_configurator.bind< InfoTag >( &_synchronizedSink );
		// fallthrough intentional
		[[fallthrough]];

	case Severity::Warning:
		_configurator.bind< WarningTag >( &_synchronizedSink );
		// fallthrough intentional
		[[fallthrough]];

	case Severity::Error:
		_configurator.bind< ErrorTag >( &_synchronizedSink );
		// fallthrough intentional
		[[fallthrough]];

	case Severity::Fatal:
		_configurator.bind< FatalTag >( &_synchronizedSink );
		break;
	}
}

} // namespace extras
} // namespace nova
} // namespace kmac
