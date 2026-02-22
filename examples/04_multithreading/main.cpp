/**
 * @file 04_multithreading.cpp
 * @brief Thread-safe logging with Nova
 * 
 * This example demonstrates:
 * - Using SynchronizedSink for thread-safe logging
 * - Logging from multiple threads
 * - Preventing interleaved output
 */

#include "kmac/nova/nova.h"
#include "kmac/nova/scoped_configurator.h"
#include "kmac/nova/extras/ostream_sink.h"
#include "kmac/nova/extras/synchronized_sink.h"

#include <iostream>
#include <thread>
#include <vector>
#include <chrono>

// Tag for multithreaded logging
struct WorkerTag {};

NOVA_LOGGER_TRAITS( WorkerTag, WorkerTag, true, ::kmac::nova::TimestampHelper::steadyNanosecs );

// simulate some work
void doWork( int workerId, int iterations )
{
	for ( int i = 0; i < iterations; ++i )
	{
		NOVA_LOG( WorkerTag ) 
			<< "Worker " << workerId
			<< " iteration " << i << '\n';
		
		// simulate work
		std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
	}
}

int main()
{
	std::cout << "=== Multithreading Example ===\n\n";
	
	// create base sink
	kmac::nova::extras::OStreamSink consoleSink( std::cout );
	
	// wrap in synchronized sink for thread safety
	kmac::nova::extras::SynchronizedSink threadSafeSink( consoleSink );
	
	{
		kmac::nova::ScopedConfigurator config;
		config.bind< WorkerTag >( &threadSafeSink );
		
		std::cout << "--- Launching 3 worker threads ---\n\n";
		
		// launch multiple worker threads
		std::vector< std::thread > workers;
		
		for ( int i = 0; i < 3; ++i )
		{
			workers.emplace_back( doWork, i, 5 );
		}
		
		// wait for all workers
		for ( auto& worker : workers )
		{
			worker.join();
		}
		
		std::cout << "\n--- All workers complete ---\n";
	}
	
	std::cout << "\n=== Example Complete ===\n";
	std::cout << "\nNotes:\n";
	std::cout << "- SynchronizedSink prevents interleaved output\n";
	std::cout << "- Each log message is atomic\n";
	std::cout << "- No garbled text from concurrent writes\n";
	
	return 0;
}

/*
Expected Output:
================

=== Multithreading Example ===

--- Launching 3 worker threads ---

main.cpp:36 doWork - Worker 0 iteration 0
main.cpp:36 doWork - Worker 1 iteration 0
main.cpp:36 doWork - Worker 2 iteration 0
main.cpp:36 doWork - Worker 0 iteration 1
main.cpp:36 doWork - Worker 1 iteration 1
main.cpp:36 doWork - Worker 2 iteration 1
main.cpp:36 doWork - Worker 0 iteration 2
main.cpp:36 doWork - Worker 1 iteration 2
main.cpp:36 doWork - Worker 2 iteration 2
main.cpp:36 doWork - Worker 0 iteration 3
main.cpp:36 doWork - Worker 1 iteration 3
main.cpp:36 doWork - Worker 2 iteration 3
main.cpp:36 doWork - Worker 0 iteration 4
main.cpp:36 doWork - Worker 1 iteration 4
main.cpp:36 doWork - Worker 2 iteration 4

--- All workers complete ---

=== Example Complete ===

Notes:
- SynchronizedSink prevents interleaved output
- each log message is atomic
- no garbled text from concurrent writes


Without SynchronizedSink (BAD):
================================

WorkWorker er 01 i iterteratation ion 00
Worker 2 iteration 0
Worker 0 iteration 1
WorWorkerker  21 i iteterarattioionn  11

(Output is garbled!)


With SynchronizedSink (GOOD):
==============================

Worker 0 iteration 0
Worker 1 iteration 0
Worker 2 iteration 0
Worker 0 iteration 1

(Output is clean!)


Key Takeaways:
==============

1. raw sinks are NOT thread-safe by default
2. SynchronizedSink wraps any sink with a mutex
3. each process() call is atomic
4. thread safety is opt-in (no hidden overhead)
5. use SynchronizedSink for any multi-threaded logging

*/
