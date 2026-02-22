/**
 * @file benchmark_launcher.cpp
 * @brief Simple launcher to run all benchmarks or choose specific ones
 * 
 * This provides a menu-driven interface for running benchmarks in Qt Creator
 */

#include <iostream>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#define EXEC_EXT ".exe"
#define CLEAR_CMD "cls"
#else
#define EXEC_EXT ""
#define CLEAR_CMD "clear"
#endif

void runBenchmark( const std::string& name )
{
	std::string cmd = "./" + name + EXEC_EXT;
	std::cout << "\n========================================\n";
	std::cout << "Running: " << name << "\n";
	std::cout << "========================================\n\n";
	
	int result = std::system( cmd.c_str() );
	
	if ( result != 0 )
	{
		std::cerr << "\nERROR: Benchmark failed with code " << result << "\n";
	}
	
	std::cout << "\n\nPress Enter to continue...";
	std::cin.ignore();
	std::cin.get();
}

void runAllBenchmarks()
{
	const char* benchmarks[] = {
		"benchmark_nova_builders",
		"benchmark_nova_sinks",
		"benchmark_throughput",
		"benchmark_latency",
		"benchmark_formatted_file_output",
		"benchmark_memory_pool_async",
		"benchmark_flare_emergency"
	};
	
	std::cout << "\n========================================\n";
	std::cout << "Running ALL Benchmarks\n";
	std::cout << "========================================\n";
	std::cout << "\nThis could take several minutes.\n";
	std::cout << "Press Enter to start...";
	std::cin.ignore();
	std::cin.get();
	
	for ( const auto& bench : benchmarks )
	{
		runBenchmark( bench );
	}
	
	std::cout << "\n========================================\n";
	std::cout << "All Benchmarks Complete!\n";
	std::cout << "========================================\n\n";
}

void showMenu()
{
	std::system( CLEAR_CMD );
	
	std::cout << "========================================\n";
	std::cout << "Nova/Flare Benchmark Launcher\n";
	std::cout << "========================================\n\n";
	
	std::cout << "Select benchmark to run:\n\n";
	std::cout << "  1. Nova Builders (Truncating vs Continuation vs Streaming)\n";
	std::cout << "  2. Nova Sinks (NullSink, OStreamSink, Async, etc.)\n";
	std::cout << "  3. Throughput (messages per second)\n";
	std::cout << "  4. Latency (nanoseconds per message)\n";
	std::cout << "  5. File Output (head-to-head comparison of formatted delivery)\n";
	std::cout << "  6. Async Memory Pool (performance of various configurations of MemoryPoolAsyncSink)\n";
	std::cout << "  7. Flare Emergency (Async-signal-safe logging)\n";
	std::cout << "\n";
	std::cout << "  A. Run ALL benchmarks (could take several minutes)\n";
	std::cout << "\n";
	std::cout << "  Q. Quit\n";
	std::cout << "\n";
	std::cout << "Choice: ";
}

int main()
{
	while ( true )
	{
		showMenu();
		
		std::string choice;
		std::getline( std::cin, choice );
		
		if ( choice.empty() )
		{
			continue;
		}
		
		char c = std::tolower( choice[0] );
		
		switch ( c )
		{
			case '1':
				runBenchmark( "benchmark_nova_builders" );
				break;
			
			case '2':
				runBenchmark( "benchmark_nova_sinks" );
				break;
			
			case '3':
				runBenchmark( "benchmark_throughput" );
				break;
			
			case '4':
				runBenchmark( "benchmark_latency" );
				break;
			
			case '5':
				runBenchmark( "benchmark_formatted_file_output" );
				break;

			case '6':
				runBenchmark( "benchmark_memory_pool_async" );
				break;
			
			case '7':
				runBenchmark( "benchmark_flare_emergency" );
				break;
			
			case 'a':
				runAllBenchmarks();
				break;
			
			case 'q':
				std::cout << "\nGoodbye!\n";
				return 0;
			
			default:
				std::cout << "\nInvalid choice. Press Enter to continue...";
				std::cin.get();
				break;
		}
	}
	
	return 0;
}
