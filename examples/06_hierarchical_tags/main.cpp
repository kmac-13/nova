/**
 * @file 06_hierarchical_tags.cpp
 * @brief Using HierarchicalTag for subsystem + severity organization
 *
 * This example demonstrates:
 * - Organizing logs by subsystem and severity
 * - Traditional severity model with Nova's tag system
 * - Binding by subsystem or severity level
 */

#include "kmac/nova/nova.h"
#include "kmac/nova/scoped_configurator.h"
#include "kmac/nova/extras/ostream_sink.h"
#include "kmac/nova/extras/hierarchical_tag.h"

#include <iostream>

// Define subsystems
struct Audio {};
struct Network {};
struct Rendering {};

// Use built-in severity levels from hierarchical_tag.h
namespace nova_extras = kmac::nova::extras;  // Debug, Info, Warning, Error

// Create hierarchical tag types
using AudioDebug   = nova_extras::HierarchicalTag< Audio, nova_extras::Debug >;
using AudioInfo    = nova_extras::HierarchicalTag< Audio, nova_extras::Info >;
using AudioError   = nova_extras::HierarchicalTag< Audio, nova_extras::Error >;

using NetworkInfo  = nova_extras::HierarchicalTag< Network, nova_extras::Info >;
using NetworkError = nova_extras::HierarchicalTag< Network, nova_extras::Error >;

using RenderDebug  = nova_extras::HierarchicalTag< Rendering, nova_extras::Debug >;
using RenderError  = nova_extras::HierarchicalTag< Rendering, nova_extras::Error >;

// Specialize logger_traits with clean names
NOVA_LOGGER_TRAITS( AudioDebug, Audio.Debug, true, ::kmac::nova::TimestampHelper::steadyNanosecs );
NOVA_LOGGER_TRAITS( AudioInfo, Audio.Info, true, ::kmac::nova::TimestampHelper::steadyNanosecs );
NOVA_LOGGER_TRAITS( AudioError, Audio.Error, true, ::kmac::nova::TimestampHelper::steadyNanosecs );

NOVA_LOGGER_TRAITS( NetworkInfo, Network.Info, true, ::kmac::nova::TimestampHelper::steadyNanosecs );
NOVA_LOGGER_TRAITS( NetworkError, Network.Error, true, ::kmac::nova::TimestampHelper::steadyNanosecs );

NOVA_LOGGER_TRAITS( RenderDebug, Render.Debug, true, ::kmac::nova::TimestampHelper::steadyNanosecs );
NOVA_LOGGER_TRAITS( RenderError, Render.Error, true, ::kmac::nova::TimestampHelper::steadyNanosecs );

int main()
{
	std::cout << "=== Hierarchical Tags Example ===\n\n";

	nova_extras::OStreamSink consoleSink( std::cout );

	{
		kmac::nova::ScopedConfigurator config;

		// bind all our hierarchical tags
		config.bind< AudioDebug >( &consoleSink );
		config.bind< AudioInfo >( &consoleSink );
		config.bind< AudioError >( &consoleSink );
		config.bind< NetworkInfo >( &consoleSink );
		config.bind< NetworkError >( &consoleSink );
		config.bind< RenderDebug >( &consoleSink );
		config.bind< RenderError >( &consoleSink );

		std::cout << "--- Audio subsystem logs ---\n";
		NOVA_LOG( AudioDebug ) << "Initializing audio engine\n";
		NOVA_LOG( AudioInfo ) << "Sample rate: 44100 Hz\n";
		NOVA_LOG( AudioError ) << "Buffer underrun detected\n";

		std::cout << "\n--- Network subsystem logs ---\n";
		NOVA_LOG( NetworkInfo ) << "Connecting to server\n";
		NOVA_LOG( NetworkError ) << "Connection timeout\n";

		std::cout << "\n--- Rendering subsystem logs ---\n";
		NOVA_LOG( RenderDebug ) << "Drawing 1000 triangles\n";
		NOVA_LOG( RenderError ) << "Shader compilation failed\n";

		std::cout << "\n";
	}

	std::cout << "\n=== Example Complete ===\n";
	std::cout << "\nNotes:\n";
	std::cout << "- Hierarchical tags combine Subsystem + Severity\n";
	std::cout << "- Tags appear as 'Subsystem.Severity' in logs\n";
	std::cout << "- Still compile-time routing (type-based)\n";
	std::cout << "- Familiar model for traditional logger users\n";

	return 0;
}

/*
Expected Output:
================

=== Hierarchical Tags Example ===

--- audio subsystem logs ---
Initializing audio engine
Sample rate: 44100 Hz
Buffer underrun detected

--- network subsystem logs ---
Connecting to server
Connection timeout

--- rendering subsystem logs ---
Drawing 1000 triangles
Shader compilation failed


=== Example Complete ===

Notes:
- hierarchical tags combine Subsystem + Severity
- tags appear as 'Subsystem.Severity' in logs
- still compile-time routing (type-based)
- familiar model for traditional logger users


Advanced Usage:
===============

// bind all Audio tags to same sink
template< typename Severity >
void bindAllAudioTags( Sink& sink )
{
	Logger< HierarchicalTag< Audio, Severity > >::bindSink( &sink );
}

// disable all Debug tags in release builds
#ifdef NDEBUG
	template< typename Subsystem >
	struct logger_traits< HierarchicalTag< Subsystem, Debug > >
	{
		static constexpr bool enabled = false;
	};
#endif

// filter by severity
auto errorFilter = []( const Record& r ) {
	return std::strstr( r.tag, "Error" ) != nullptr;
};

FilterSink errorsOnly( console, errorFilter );


Key Takeaways:
==============

1. HierarchicalTag provides traditional logging model
2. still type-based (compile-time routing)
3. subsystem and Severity are separate types
4. easy to bind by subsystem or severity
5. works with all Nova features (filtering, etc.)
6. optional pattern - use if it fits your needs

*/
