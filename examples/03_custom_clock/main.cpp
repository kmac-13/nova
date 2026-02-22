/**
 * @file 03_custom_clock.cpp
 * @brief Using custom time sources for timestamps
 * 
 * This example demonstrates:
 * - Customizing the timestamp source per tag
 * - Using different clocks (steady, system, custom)
 * - Platform-specific timing implementations
 */

#include "kmac/nova/nova.h"
#include "kmac/nova/scoped_configurator.h"

#include <chrono>
#include <ctime>
#include <iostream>
#include <iomanip>
#include <thread>

// tags with different timing requirements
struct MonotonicTag {};   // Uses steady_clock (default)
struct WallClockTag {};   // Uses system_clock (wall time)
struct CustomTag {};      // Uses custom time source

// MonotonicTag: uses default (steady_clock)
NOVA_LOGGER_TRAITS( MonotonicTag, MONOTONIC, true, TimestampHelper::steadyNanosecs );

// WallClockTag: uses system_clock for wall time
NOVA_LOGGER_TRAITS( WallClockTag, WALLCLOCK, true, TimestampHelper::systemNanosecs );

// CustomTag: uses custom time source (e.g., frame counter)
std::uint64_t frameCount()
{
	// example: use frame counter instead of real time
	// in a real game/app, this might be your simulation time
	static std::uint64_t frameCounter = 0;
	return frameCounter++;
}
NOVA_LOGGER_TRAITS( CustomTag, CUSTOM, true, frameCount );

// helper to format timestamp as human-readable time
std::string formatTimestamp( std::uint64_t timestampNs )
{
	// Convert to system_clock time_point
	auto duration = std::chrono::nanoseconds( timestampNs );
	auto timePoint = std::chrono::system_clock::time_point( 
		std::chrono::duration_cast< std::chrono::system_clock::duration >( duration )
	);
	
	std::time_t time = std::chrono::system_clock::to_time_t( timePoint );
	std::tm tm;
	
#ifdef _WIN32
	localtime_s( &tm, &time );
#else
	localtime_r( &time, &tm );
#endif
	
	std::ostringstream oss;
	oss << std::put_time( &tm, "%Y-%m-%d %H:%M:%S" );
	return oss.str();
}

// custom sink that shows timestamps
class TimestampingSink : public kmac::nova::Sink
{
private:
	std::ostream* _stream;

public:
	explicit TimestampingSink( std::ostream& stream )
		: _stream( &stream )
	{}
	
	void process( const kmac::nova::Record& record ) noexcept override
	{
		( *_stream )
			<< "[" << record.timestamp << "] "
			<< record.tag << ": "
			<< record.message << "\n";
	}
};

int main()
{
	std::cout << "=== Custom Clock Example ===\n\n";
	
	TimestampingSink sink( std::cout );
	
	{
		kmac::nova::ScopedConfigurator config;
		
		config.bind< MonotonicTag >( &sink );
		config.bind< WallClockTag >( &sink );
		config.bind< CustomTag >( &sink );
		
		std::cout << "--- Monotonic time (steady_clock) ---\n";
		NOVA_LOG( MonotonicTag ) << "Event 1";
		std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
		NOVA_LOG( MonotonicTag ) << "Event 2";
		
		std::cout << "\n--- Wall clock time (system_clock) ---\n";
		// auto start = std::chrono::system_clock::now();
		NOVA_LOG( WallClockTag ) << "Started at wall time";
		
		// show formatted wall time
		std::uint64_t wallTimeNs;
		{
			const auto now = std::chrono::system_clock::now().time_since_epoch();
			wallTimeNs = std::chrono::duration_cast< std::chrono::nanoseconds >( now ).count();
		}
		std::cout << "  (Human readable: " << formatTimestamp( wallTimeNs ) << ")\n";
		
		std::cout << "\n--- Custom time source (frame counter) ---\n";
		NOVA_LOG( CustomTag ) << "Frame 0";
		NOVA_LOG( CustomTag ) << "Frame 1";
		NOVA_LOG( CustomTag ) << "Frame 2";
		NOVA_LOG( CustomTag ) << "Frame 3";
		
		std::cout << "\n";
	}
	
	std::cout << "\n=== Example Complete ===\n";
	std::cout << "\nNotes:\n";
	std::cout << "- Monotonic timestamps are in nanoseconds since arbitrary epoch\n";
	std::cout << "- Wall clock timestamps can be converted to calendar time\n";
	std::cout << "- Custom timestamps can be anything (frames, ticks, etc.)\n";
	
	return 0;
}

/*
Expected Output (example):
===========================

=== Custom Clock Example ===

--- monotonic time (steady_clock) ---
[1234567890123456] MONOTONIC: Event 1
[1234567990123456] MONOTONIC: Event 2

--- wall clock time (system_clock) ---
[1703001234567890123] WALLCLOCK: Started at wall time
  (Human readable: 2024-12-19 15:30:45)

--- custom time source (frame counter) ---
[0] CUSTOM: Frame 0
[1] CUSTOM: Frame 1
[2] CUSTOM: Frame 2
[3] CUSTOM: Frame 3


=== Example Complete ===

Notes:
- monotonic timestamps are in nanoseconds since arbitrary epoch
- wall clock timestamps can be converted to calendar time
- custom timestamps can be anything (frames, ticks, etc.)


Key Takeaways:
==============

1. each tag can have its own time source
2. monotonic time (steady_clock) is default and recommended
3. wall clock time (system_clock) for human-readable timestamps
4. custom time sources for domain-specific needs
5. all timestamp customization is compile-time per tag

*/
