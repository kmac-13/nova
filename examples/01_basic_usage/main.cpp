/**
 * @file 01_basic_usage.cpp
 * @brief Basic Nova logging example
 * 
 * This example demonstrates:
 * - defining a simple tag
 * - binding a sink to a logger
 * - writing log messages
 * - using the NOVA_LOG macro
 */

#include "kmac/nova/nova.h"
#include "kmac/nova/scoped_configurator.h"
#include "kmac/nova/extras/ostream_sink.h"

#include <iostream>

// step 1: Define a tag
// tags are just empty types that identify logging categories
struct AppTag {};

// step 2: Specialize logger_traits for the tag
// this is where you configure the tag's behavior,
// which can be done using a traits convenience macro
NOVA_LOGGER_TRAITS( AppTag, AppTag, true, kmac::nova::TimestampHelper::steadyNanosecs );

// define a macro to log directly using the AppTag logger
#define APP_LOG() NOVA_LOG( AppTag )

int main()
{
	std::cout << "=== Basic Nova Logging Example ===\n\n";
	
	// step 3: Create a sink
	// sinks determine where log messages go
	kmac::nova::extras::OStreamSink consoleSink( std::cout );
	
	// step 4: Bind the sink to the logger
	// use ScopedConfigurator for RAII-style binding
	{
		kmac::nova::ScopedConfigurator config;
		config.bind< AppTag >( &consoleSink );
		
		// step 5: Log some messages
		
		// using a Nova macro
		NOVA_LOG( AppTag ) << "Hello from Nova!\n";

		// using a custom macro
		APP_LOG() << "The answer is " << 42 << '\n';
		
		// direct API (less common)
		kmac::nova::Logger< AppTag >::log( __FILE__, __FUNCTION__, __LINE__, "Direct logging API\n" );
		
		std::cout << "\n";
		
	} // sink is automatically unbound here
	
	// after scope exit, logging is disabled
	NOVA_LOG( AppTag ) << "This won't appear (no sink bound)";
	
	std::cout << "\n=== Example Complete ===\n";
	
	return 0;
}

/*
Expected Output:
================

=== Basic Nova Logging Example ===

Hello from Nova!The answer is 42Direct logging API

=== Example Complete ===

Note that the OStreamSink does not log any info outside of the message to log.

Key Takeaways:
==============

1. tags are types (compile-time routing)
2. logger_traits configures tag behavior
3. ScopedConfigurator manages sink lifetime
4. NOVA_LOG macro provides streaming interface
5. unbinding is automatic on scope exit

*/
