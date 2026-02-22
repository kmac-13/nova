/**
 * @file 05_filtering.cpp
 * @brief Using FilterSink for runtime filtering
 * 
 * This example demonstrates:
 * - Runtime filtering of log messages
 * - Filter by tag, message content, or source
 * - Composing multiple filters
 * - Dynamic filter adjustment
 */

#include "kmac/nova/nova.h"
#include "kmac/nova/scoped_configurator.h"
#include "kmac/nova/extras/filter_sink.h"
#include "kmac/nova/extras/ostream_sink.h"

#include <iostream>
#include <cstring>

// Define multiple tags
struct AudioTag {};
struct NetworkTag {};
struct RenderingTag {};

NOVA_LOGGER_TRAITS( AudioTag, Audio, true, ::kmac::nova::TimestampHelper::steadyNanosecs );
NOVA_LOGGER_TRAITS( NetworkTag, Network, true, ::kmac::nova::TimestampHelper::steadyNanosecs );
NOVA_LOGGER_TRAITS( RenderingTag, Rendering, true, ::kmac::nova::TimestampHelper::steadyNanosecs );

int main()
{
	std::cout << "=== Filtering Example ===\n\n";
	
	kmac::nova::extras::OStreamSink consoleSink( std::cout );
	
	// example 1: filter by tag
	{
		std::cout << "--- Filter by tag (Audio only) ---\n";
		
		auto audioFilter = []( const kmac::nova::Record& record ) {
			return std::strstr( record.tag, "Audio" ) != nullptr;
		};
		
		kmac::nova::extras::FilterSink audioOnly( consoleSink, audioFilter );
		
		kmac::nova::ScopedConfigurator config;
		config.bind< AudioTag >( &audioOnly );
		config.bind< NetworkTag >( &audioOnly );
		config.bind< RenderingTag >( &audioOnly );
		
		NOVA_LOG( AudioTag ) << "Audio buffer ready\n";
		NOVA_LOG( NetworkTag ) << "Packet received\n";
		NOVA_LOG( RenderingTag ) << "Frame rendered\n";
		NOVA_LOG( AudioTag ) << "Audio processing complete\n";
		
		std::cout << "(Only Audio messages appear)\n\n";
	}
	
	// example 2: filter by message content
	{
		std::cout << "--- Filter by message content (errors only) ---\n";
		
		auto errorFilter = []( const kmac::nova::Record& record ) {
			return std::strstr( record.message, "ERROR" ) != nullptr ||
				std::strstr( record.message, "FAILED" ) != nullptr;
		};
		
		kmac::nova::extras::FilterSink errorsOnly( consoleSink, errorFilter );
		
		kmac::nova::ScopedConfigurator config;
		config.bind< AudioTag >( &errorsOnly );
		config.bind< NetworkTag >( &errorsOnly );
		
		NOVA_LOG( AudioTag ) << "Processing frame\n";
		NOVA_LOG( AudioTag ) << "ERROR: Buffer underrun\n";
		NOVA_LOG( NetworkTag ) << "Connection established\n";
		NOVA_LOG( NetworkTag ) << "FAILED: Timeout\n";
		
		std::cout << "(Only error messages appear)\n\n";
	}
	
	// example 3: composite filter (tag AND content)
	{
		std::cout << "--- Composite filter (Audio errors only) ---\n";
		
		auto audioErrorFilter = []( const kmac::nova::Record& record ) {
			bool isAudio = std::strstr( record.tag, "Audio" ) != nullptr;
			bool isError = std::strstr( record.message, "ERROR" ) != nullptr;
			return isAudio && isError;
		};
		
		kmac::nova::extras::FilterSink audioErrors( consoleSink, audioErrorFilter );
		
		kmac::nova::ScopedConfigurator config;
		config.bind< AudioTag >( &audioErrors );
		config.bind< NetworkTag >( &audioErrors );
		
		NOVA_LOG( AudioTag ) << "Audio initialized\n";
		NOVA_LOG( AudioTag ) << "ERROR: Sample rate mismatch\n";
		NOVA_LOG( NetworkTag ) << "ERROR: Connection lost\n";
		NOVA_LOG( AudioTag ) << "ERROR: Device not found\n";
		
		std::cout << "(Only Audio errors appear)\n\n";
	}
	
	// example 4: filter chain
	{
		std::cout << "--- Filter chain (Audio -> Errors) ---\n";
		
		auto audioFilter = []( const kmac::nova::Record& record ) {
			return std::strstr( record.tag, "Audio" ) != nullptr;
		};
		
		auto errorFilter = []( const kmac::nova::Record& record ) {
			return std::strstr( record.message, "ERROR" ) != nullptr;
		};
		
		// chain: All messages -> Audio filter -> Error filter -> Console
		kmac::nova::extras::FilterSink errorOnly( consoleSink, errorFilter );
		kmac::nova::extras::FilterSink audioErrors( errorOnly, audioFilter );
		
		kmac::nova::ScopedConfigurator config;
		config.bind< AudioTag >( &audioErrors );
		config.bind< NetworkTag >( &audioErrors );
		config.bind< RenderingTag >( &audioErrors );
		
		NOVA_LOG( AudioTag ) << "Processing\n";
		NOVA_LOG( AudioTag ) << "ERROR: Overflow\n";
		NOVA_LOG( NetworkTag ) << "ERROR: Timeout\n";
		NOVA_LOG( RenderingTag ) << "Frame done\n";
		
		std::cout << "(Only Audio errors pass through both filters)\n\n";
	}
	
	std::cout << "=== Example Complete ===\n";
	std::cout << "\nKey Points:\n";
	std::cout << "- Filters run at runtime (no recompilation)\n";
	std::cout << "- Can filter by tag, message, file, function, etc.\n";
	std::cout << "- Filters can be chained for complex logic\n";
	std::cout << "- Filtering happens before sink processes message\n";
	
	return 0;
}

/*
Expected Output:
================

=== Filtering Example ===

--- Filter by tag (Audio only) ---
Audio buffer ready
Audio processing complete
(Only Audio messages appear)

--- Filter by message content (errors only) ---
ERROR: Buffer underrun
FAILED: Timeout
(Only error messages appear)

--- Composite filter (Audio errors only) ---
ERROR: Sample rate mismatch
ERROR: Device not found
(Only Audio errors appear)

--- Filter chain (Audio -> Errors) ---
ERROR: Overflow
(Only Audio errors pass through both filters)

=== Example Complete ===

Key Points:
- filters run at runtime (no recompilation)
- can filter by tag, message, file, function, etc.
- filters can be chained for complex logic
- filtering happens before sink processes message


Key Takeaways:
==============

1. FilterSink enables runtime filtering without recompilation
2. filters are just predicates (functions returning bool)
3. can filter on any Record field (tag, message, file, etc.)
4. multiple filters can be chained
5. filtering is composable (like all Nova sinks)
6. zero cost if not used (opt-in feature)

*/
