/**
 * @file 02_multiple_sinks.cpp
 * @brief Using multiple sinks with composite sink
 *
 * This example demonstrates:
 * - Binding multiple sinks to a single logger
 * - Using CompositeSink for fan-out
 * - Different tags with different sink configurations
 */

#include "kmac/nova/nova.h"
#include "kmac/nova/scoped_configurator.h"
#include "kmac/nova/extras/composite_sink.h"
#include "kmac/nova/extras/null_sink.h"
#include "kmac/nova/extras/ostream_sink.h"

#include <iostream>
#include <fstream>

// Define tags for different subsystems
struct AppTag {};
struct DebugTag {};
struct ErrorTag {};

// tag configuration
NOVA_LOGGER_TRAITS( AppTag, APP, true, ::kmac::nova::TimestampHelper::steadyNanosecs );
NOVA_LOGGER_TRAITS( DebugTag, DEBUG, true, ::kmac::nova::TimestampHelper::steadyNanosecs );
NOVA_LOGGER_TRAITS( ErrorTag, ERROR, true, ::kmac::nova::TimestampHelper::steadyNanosecs );

int main()
{
	std::cout << "=== Multiple Sinks Example ===\n\n";

	// create a file for error logs
	std::ofstream errorFile( "errors.log" );

	// create individual sinks
	kmac::nova::extras::OStreamSink consoleSink( std::cout );
	kmac::nova::extras::OStreamSink errorFileSink( errorFile );
	kmac::nova::extras::NullSink& nullSink = kmac::nova::extras::NullSink::instance();

	// create composite sink (fan-out to multiple destinations)
	kmac::nova::extras::CompositeSink composite;
	composite.add( consoleSink );
	composite.add( errorFileSink );

	{
		kmac::nova::ScopedConfigurator config;

		// AppTag: Goes to console only
		config.bind< AppTag >( &consoleSink );

		// DebugTag: Goes nowhere (disabled at runtime)
		config.bind< DebugTag >( &nullSink );

		// ErrorTag: Goes to both console and file (using composite)
		config.bind< ErrorTag >( &composite );

		// Log messages
		std::cout << "--- App logs (console only) ---\n";
		NOVA_LOG( AppTag ) << "Application started" << '\n';
		NOVA_LOG( AppTag ) << "Processing request" << '\n';

		std::cout << "\n--- Debug logs (discarded) ---\n";
		NOVA_LOG( DebugTag ) << "This debug message is discarded" << '\n';
		NOVA_LOG( DebugTag ) << "Another debug message" << '\n';
		std::cout << "(No debug output - bound to NullSink)\n";

		std::cout << "\n--- Error logs (console + file) ---\n";
		NOVA_LOG( ErrorTag ) << "Connection failed" << '\n';
		NOVA_LOG( ErrorTag ) << "Retry limit exceeded" << '\n';

		std::cout << "\n";
	}

	errorFile.close();

	// Check what was written to error file
	std::cout << "\n--- Contents of errors.log ---\n";
	std::ifstream readErrors( "errors.log" );
	std::string line;
	while ( std::getline( readErrors, line ) )
	{
		std::cout << line << "\n";
	}

	std::cout << "\n=== Example Complete ===\n";

	return 0;
}

/*
Expected Output:
================

=== Multiple Sinks Example ===

--- app logs (console only) ---
Application started
Processing request

--- debug logs (discarded) ---
(No debug output - bound to NullSink)

--- error logs (console + file) ---
Connection failed
Retry limit exceeded


--- Contents of errors.log ---
Connection failed
Retry limit exceeded

=== Example Complete ===


Key Takeaways:
==============

1. each tag can have its own sink configuration
2. CompositeSink enables fan-out to multiple destinations
3. NullSink discards messages (runtime disable)
4. different routing strategies per tag
5. all binding is explicit and visible

*/
