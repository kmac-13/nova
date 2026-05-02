#include "kmac/flare/emergency_sink.h"
#include "kmac/flare/file_writer.h"
#include "kmac/flare/reader.h"
#include "kmac/flare/record.h"
#include "kmac/flare/scanner.h"

#include "kmac/nova/nova.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>

#if defined( FLARE_HAVE_FAULT_CONTEXT )
#include "kmac/flare/fd_writer.h"
#include "kmac/flare/signal_handler.h"

#include <fcntl.h>
#include <unistd.h>
#endif

// ============================================================================
// This example has two modes depending on whether a crash log already exists:
//
// * First run:  test_signal.flare absent
//   - installs SignalHandler
//   - deliberately dereferences a null pointer (SIGSEGV)
//   - handler writes a crash record to test_signal.flare
//   - process re-raises and exits with signal status
//
// * Second run: test_signal.flare present
//   - reads and displays the crash record from the first run
//   - deletes the file
//   - runs the remaining Flare tests (write, read, scanner, etc.)
//   - exits 0
//
// During the second run, the test_signal.flare is automatically deleted, so
// the next run will start over at the first run again.
// ============================================================================

// test tag
struct CrashTag {};

std::uint64_t fixedTimestamp()
{
	return 1704067200000000000ULL; // 2024-01-01 00:00:00 UTC
}

NOVA_LOGGER_TRAITS( CrashTag, CRASH, true, fixedTimestamp );

// ============================================================================
// Crash record display
// ============================================================================

#if defined( FLARE_HAVE_FAULT_CONTEXT )

static void printCrashRecord( const kmac::flare::Record& record )
{
	std::cout << "  Status:         " << record.statusString() << "\n";
	std::cout << "  Sequence:       " << record.sequenceNumber << "\n";
	std::cout << "  Timestamp:      " << record.timestampNs << " ns\n";

	if ( record.processId != 0 )
	{
		std::cout << "  Process:        " << record.processId << "\n";
	}
	if ( record.threadId != 0 )
	{
		std::cout << "  Thread:         " << record.threadId << "\n";
	}

	std::cout << "  Message:        " << record.message.data() << "\n";

	if ( record.hasFaultAddress )
	{
		std::cout << "  Fault address:  0x" << std::hex << record.faultAddress << std::dec << "\n";
	}

	if ( record.loadBaseAddress != 0 )
	{
		std::cout << "  Load base:      0x" << std::hex << record.loadBaseAddress << std::dec << "\n";
	}

	if ( record.aslrOffset != 0 )
	{
		std::cout << "  ASLR offset:    0x" << std::hex << record.aslrOffset << std::dec << "\n";
	}

	if ( record.stackFrameCount > 0 )
	{
		std::cout << "  Stack frames:   " << record.stackFrameCount << "\n";
		for ( std::size_t i = 0; i < record.stackFrameCount; ++i )
		{
			const std::uint64_t runtime = record.stackFrames.at( i );
			const std::uint64_t staticAddr = runtime - record.aslrOffset;
			std::cout << "    [" << i << "]  0x" << std::hex << runtime
				<< "  ->  static 0x" << staticAddr << std::dec << "\n";
		}
		std::cout << "  (pass --binary <path> --addr2line addr2line to flare_reader.py"
			" to resolve symbols)\n";
	}

	if ( record.registerCount > 0 )
	{
		// register names for the most common layout (x86-64)
		static const char* const X86_64_NAMES[] = {
			"rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp",
			"r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15",
			"rip", "rflags"
		};
		static const char* const ARM64_NAMES[] = {
			"x0",  "x1",  "x2",  "x3",  "x4",  "x5",  "x6",  "x7",
			"x8",  "x9",  "x10", "x11", "x12", "x13", "x14", "x15",
			"x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
			"x24", "x25", "x26", "x27", "x28", "fp",  "lr",  "sp",
			"pc",  "pstate"
		};
		static const char* const ARM32_NAMES[] = {
			"r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",
			"r8",  "r9",  "r10", "fp",  "ip",  "sp",  "lr",  "pc",
			"cpsr"
		};

		const char* const* names = nullptr;
		std::size_t nameCount = 0;
		const char* layoutName = "unknown";

		switch ( record.registerLayout )
		{
		case 1:
			names = X86_64_NAMES;
			nameCount = 18;
			layoutName = "x86-64";
			break;
		case 2:
			names = ARM64_NAMES;
			nameCount = 34;
			layoutName = "ARM64";
			break;
		case 3:
			names = ARM32_NAMES;
			nameCount = 17;
			layoutName = "ARM32";
			break;
		default:
			break;
		}

		std::cout << "  Registers (" << layoutName << "):\n";
		for ( std::size_t i = 0; i < record.registerCount; ++i )
		{
			const char* name = ( names != nullptr && i < nameCount ) ? names[ i ] : "?";
			std::cout << "    " << name << "\t0x"
				<< std::hex << record.registers.at( i ) << std::dec << "\n";
		}
	}
}

#endif // FLARE_HAVE_FAULT_CONTEXT

// ============================================================================
// main
// ============================================================================

int main()
{
	std::cout << "=== Flare Library Example ===\n\n";

	// -----------------------------------------------------------------------
	// Crash demo (POSIX only)
	// -----------------------------------------------------------------------
#if defined( FLARE_HAVE_FAULT_CONTEXT )
	{
		static constexpr const char* CRASH_LOG = "test_signal.flare";

		std::ifstream existingLog( CRASH_LOG, std::ios::binary );
		const bool crashLogExists = existingLog.good();
		existingLog.close();

		if ( ! crashLogExists )
		{
			// first run: install handler and deliberately crash
			std::cout << "No crash log found - demonstrating crash capture.\n";
			std::cout << "The process will crash with SIGSEGV, write a Flare record,\n";
			std::cout << "and terminate.  Run again to see the captured crash data.\n\n";

			// FdWriter + EmergencySink must have static storage - they must
			// outlive the signal handler and survive the crash itself
			// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
			static int fd = ::open( CRASH_LOG, O_WRONLY | O_CREAT | O_TRUNC, 0644 );
			if ( fd < 0 )
			{
				std::cerr << "ERROR: failed to open crash log file\n";
				return 1;
			}

			// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
			static kmac::flare::FdWriter fdWriter( fd );

			// captureStackTrace=true so we get a full stack trace in the record
			// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
			static kmac::flare::EmergencySink<> signalSink( &fdWriter, true, true );

			kmac::flare::SignalHandler<>::install( &signalSink );

			std::cout << "Signal handlers installed. Crashing now...\n";
			std::cout.flush();

			// deliberately dereference null to trigger SIGSEGV
			// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,cppcoreguidelines-pro-bounds-pointer-arithmetic)
			volatile int* nullPtr = nullptr;
			*nullPtr = 42; // SIGSEGV here - handler fires, writes record, re-raises

			// unreachable
			return 1;
		}
		else
		{
			// second run: read and display the crash record from the first run
			std::cout << "Crash log found from previous run - displaying crash record:\n";
			std::cout << "-------------------------------------------------------------\n";

			std::ifstream file( CRASH_LOG, std::ios::binary );
			std::vector< std::uint8_t > data {
				std::istreambuf_iterator< char >( file ),
				std::istreambuf_iterator< char >()
			};

			kmac::flare::Reader reader;
			kmac::flare::Record record;
			int count = 0;

			while ( reader.parseNext( data.data(), data.size(), record ) )
			{
				++count;
				std::cout << "\nCrash record " << count << ":\n";
				printCrashRecord( record );
			}

			if ( count == 0 )
			{
				std::cerr << "ERROR: crash log exists but contains no parseable records\n";
				return 1;
			}

			// remove the crash log so the demo resets for the next run
			std::remove( CRASH_LOG );

			std::cout << "\n(crash log deleted - run again to repeat the demo)\n\n";
		}
	}
#else
	std::cout << "SignalHandler not available on this platform (POSIX only).\n\n";
#endif

	// test 1: write emergency records
	std::cout << "Test 1: writing emergency records to file\n";
	std::cout << "-------------------------------------------\n";
	{
		std::FILE* emergencyFile = std::fopen( "test_crash.flare", "wb" );
		if ( ! emergencyFile )
		{
			std::cerr << "failed to open emergency log file\n";
			return 1;
		}

		kmac::flare::FileWriter fileWriter( emergencyFile );
		kmac::flare::EmergencySink<> sink( &fileWriter );
		kmac::nova::Logger< CrashTag >::bindSink( &sink );

		// write several test records
		{
			kmac::nova::TruncatingRecordBuilder<> builder;
			builder.setContext< CrashTag >( __FILE__, __FUNCTION__, __LINE__ );
			builder << "Test record 1: basic message";
			builder.commit();
		}

		{
			kmac::nova::TruncatingRecordBuilder<> builder;
			builder.setContext< CrashTag >( __FILE__, __FUNCTION__, __LINE__ );
			builder << "Test record 2: with numbers: " << 42 << " and " << 3.14;
			builder.commit();
		}

		{
			// use stack-based streaming macro
			NOVA_LOG_STACK( CrashTag ) << "Test record 3: multi-line message\nLine 2\nLine 3";
		}

		// manually create a record to test direct API
		const char* msg = "SIGSEGV: Segmentation fault at 0xdeadbeef";
		kmac::nova::Record manualRecord {
			1704067200000000000ULL,
			kmac::nova::LoggerTraits< CrashTag >::tagId,
			"CRASH",
			"crash_handler.cpp",
			"signal_handler",
			99,
			static_cast< std::uint32_t >( std::strlen( msg ) ),
			msg
		};
		sink.process( manualRecord );

		sink.flush();
		std::fclose( emergencyFile );

		std::cout << "successfully wrote 4 emergency records\n\n";
	}

	// test 2: read back the records
	std::cout << "Test 2: reading emergency records from file\n";
	std::cout << "--------------------------------------------\n";
	{
		// read the entire file into memory
		std::ifstream file( "test_crash.flare", std::ios::binary );
		if ( ! file )
		{
			std::cerr << "failed to open emergency log file for reading\n";
			return 1;
		}

		std::vector< std::uint8_t > data {
			std::istreambuf_iterator< char >( file ),
			std::istreambuf_iterator< char >()
		};

		std::cout << "read " << data.size() << " bytes from file\n";

		// parse records
		kmac::flare::Reader reader;
		kmac::flare::Record record;
		int recordCount = 0;

		while ( reader.parseNext( data.data(), data.size(), record ) )
		{
			++recordCount;
			std::cout << "\nRecord " << recordCount << ":\n";
			std::cout << "  Sequence:  " << record.sequenceNumber << "\n";
			std::cout << "  Status:    " << record.statusString() << "\n";
			std::cout << "  Timestamp: " << record.timestampNs << " ns\n";
			std::cout << "  Tag ID:    0x" << std::hex << record.tagId << std::dec << "\n";

			// show process/thread info if available
			if ( record.processId != 0 )
			{
				std::cout << "  Process:   " << record.processId << "\n";
			}
			if ( record.threadId != 0 )
			{
				std::cout << "  Thread:    " << record.threadId << "\n";
			}

			std::cout << "  File:      " << record.file.data() << "\n";
			std::cout << "  Line:      " << record.line << "\n";
			std::cout << "  Function:  " << record.function.data() << "\n";
			std::cout << "  Message:   " << record.message.data();
			if ( record.messageTruncated )
			{
				std::cout << " [TRUNCATED]";
			}
			std::cout << "\n";
		}

		std::cout << "\nsuccessfully parsed " << recordCount << " records\n\n";

		if ( recordCount != 4 )
		{
			std::cerr << "ERROR: expected 4 records, got " << recordCount << "\n";
			return 1;
		}
	}

	// test 3: scanner edge cases
	std::cout << "Test 3: testing Scanner with edge cases\n";
	std::cout << "----------------------------------------\n";
	{
		kmac::flare::Scanner scanner;

		// Test: empty data
		// NOTE: zero-size arrays are not supported by all compilers...
		// std::uint8_t empty[] = {};
		// ... so just use a non-array value
		std::uint8_t empty;
		if ( scanner.scan( &empty, 0 ) )
		{
			std::cerr << "ERROR: scanner should fail on empty data\n";
			return 1;
		}
		std::cout << "empty data correctly rejected\n";

		// Test: partial magic
		std::uint8_t partial[] = { 0x4B, 0x4D, 0x41 };
		if ( scanner.scan( partial, sizeof( partial ) ) )
		{
			std::cerr << "ERROR: scanner should fail on partial magic\n";
			return 1;
		}
		std::cout << "partial magic correctly rejected\n";

		// Test: valid magic but no size
		std::uint8_t noSize[] = {
			0x52, 0x4C, 0x46, 0x5F, 0x43, 0x41, 0x4D, 0x4B  // FLARE_MAGIC
		};
		if ( scanner.scan( noSize, sizeof( noSize ) ) )
		{
			std::cerr << "ERROR: scanner should fail when size field is missing\n";
			return 1;
		}
		std::cout << "missing size field correctly rejected\n\n";
	}

	// test 4: EmergencySink with large message
	std::cout << "Test 4: testing with large message (truncation)\n";
	std::cout << "------------------------------------------------\n";
	{
		std::FILE* emergencyFile = std::fopen( "test_large.flare", "wb" );
		kmac::flare::FileWriter fileWriter( emergencyFile );
		kmac::flare::EmergencySink<> sink( &fileWriter );

		// create a very large message (larger than UINT16_MAX)
		std::string largeMsg( 70000, 'X' );  // 70KB of 'X'

		kmac::nova::Record largeRecord {
			1704067200000000000ULL,
			kmac::nova::LoggerTraits< CrashTag >::tagId,
			"CRASH",
			"test.cpp",
			"testLarge",
			100,
			static_cast< std::uint32_t >( largeMsg.size() ),
			largeMsg.c_str()
		};

		sink.process( largeRecord );
		sink.flush();
		std::fclose( emergencyFile );

		// read it back
		std::ifstream file( "test_large.flare", std::ios::binary );
		std::vector< std::uint8_t > data {
			std::istreambuf_iterator< char >( file ),
			std::istreambuf_iterator< char >()
		};

		kmac::flare::Reader reader;
		kmac::flare::Record readBack;

		if ( reader.parseNext( data.data(), data.size(), readBack ) )
		{
			std::cout << "large message written and read back\n";
			std::cout << "  Status:         " << readBack.statusString() << "\n";
			std::cout << "  Original size:  " << largeMsg.size() << " bytes\n";
			std::cout << "  Read back size: " << readBack.messageLen << " bytes\n";
			std::cout << "  Truncated flag: " << ( readBack.messageTruncated ? "Yes" : "No" ) << "\n";
		}
		else
		{
			std::cerr << "ERROR: failed to read back large message\n";
			return 1;
		}

		std::cout << "\n";
	}

	// test 5: tag hash consistency
	std::cout << "Test 5: testing tag hash consistency\n";
	std::cout << "-------------------------------------\n";
	{
		// write with tag
		std::FILE* f1 = std::fopen( "test_hash1.flare", "wb" );
		kmac::flare::FileWriter f1Writer( f1 );
		kmac::flare::EmergencySink<> sink1( &f1Writer );
		kmac::nova::Record rec1 { 12345ULL, 0, "TestTag", "file.cpp",  "func",  1, 9, "Message 1" };
		sink1.process( rec1 );
		sink1.flush();
		std::fclose( f1 );

		// write same tag again
		std::FILE* f2 = std::fopen( "test_hash2.flare", "wb" );
		kmac::flare::FileWriter f2Writer( f2 );
		kmac::flare::EmergencySink<> sink2( &f2Writer );
		kmac::nova::Record rec2 { 67890ULL, 0, "TestTag", "file2.cpp", "func2", 2, 9, "Message 2" };
		sink2.process( rec2 );
		sink2.flush();
		std::fclose( f2 );

		// read both and compare tag hashes
		std::ifstream file1( "test_hash1.flare", std::ios::binary );
		std::vector< std::uint8_t > data1 {
			std::istreambuf_iterator< char >( file1 ),
			std::istreambuf_iterator< char >()
		};

		std::ifstream file2( "test_hash2.flare", std::ios::binary );
		std::vector< std::uint8_t > data2 {
			std::istreambuf_iterator< char >( file2 ),
			std::istreambuf_iterator< char >()
		};

		kmac::flare::Reader reader1, reader2;
		kmac::flare::Record read1, read2;

		if ( reader1.parseNext( data1.data(), data1.size(), read1 )
			&& reader2.parseNext( data2.data(), data2.size(), read2 ) )
		{
			std::cout << "read both records\n";
			std::cout << "  record 1 tag hash: 0x" << std::hex << read1.tagId << std::dec << "\n";
			std::cout << "  record 2 tag hash: 0x" << std::hex << read2.tagId << std::dec << "\n";

			if ( read1.tagId == read2.tagId )
			{
				std::cout << "  tag hashes match (consistent hashing)\n";
			}
			else
			{
				std::cerr << "  ERROR: tag hashes don't match!\n";
				return 1;
			}
		}
		else
		{
			std::cerr << "ERROR: failed to read records\n";
			return 1;
		}

		std::cout << "\n";
	}

	// test 6: sequence number ordering
	std::cout << "Test 6: testing sequence number ordering\n";
	std::cout << "-----------------------------------------\n";
	{
		std::FILE* seqFile = std::fopen( "test_sequence.flare", "wb" );
		kmac::flare::FileWriter seqWriter( seqFile );
		kmac::flare::EmergencySink<> seqSink( &seqWriter );

		// write multiple records
		for ( int i = 0; i < 5; ++i )
		{
			std::string msg = "Record " + std::to_string( i );
			kmac::nova::Record rec {
				std::uint64_t( 1000 + i ),
				1,
				"SEQ",
				"test.cpp",
				"testSeq",
				std::uint32_t( 100 + i ),
				static_cast< std::uint32_t >( msg.size() ),
				msg.c_str()
			};
			seqSink.process( rec );
		}
		seqSink.flush();
		std::fclose( seqFile );

		// read back and verify sequence
		std::ifstream file( "test_sequence.flare", std::ios::binary );
		std::vector< std::uint8_t > data {
			std::istreambuf_iterator< char >( file ),
			std::istreambuf_iterator< char >()
		};

		kmac::flare::Reader reader;
		kmac::flare::Record record;
		std::uint64_t expectedSeq = 0;
		int count = 0;

		while ( reader.parseNext( data.data(), data.size(), record ) )
		{
			if ( record.sequenceNumber != expectedSeq )
			{
				std::cerr
					<< "ERROR: sequence mismatch! Expected " << expectedSeq
					<< ", got " << record.sequenceNumber << "\n";
				return 1;
			}
			++expectedSeq;
			++count;
		}

		std::cout << "read " << count << " records\n";
		std::cout << "sequence numbers 0-" << ( count - 1 ) << " in order\n\n";
	}

	std::cout << "=== all Flare tests passed! ===\n";

	return 0;
}

/*
Expected Output:
================

--- First run (no test_signal.flare present) ---

=== Flare Library Example ===

No crash log found - demonstrating crash capture.
The process will crash with SIGSEGV, write a Flare record,
and terminate.  Run again to see the captured crash data.

Signal handlers installed. Crashing now...
[process exits with SIGSEGV]

--- Second run (test_signal.flare present) ---

=== Flare Library Example ===

Crash log found from previous run - displaying crash record:
-------------------------------------------------------------

Crash record 1:
  Status:         Complete
  Sequence:       0
  Timestamp:      [ns]
  Process:        [pid]
  Thread:         [tid]
  Message:        SIGSEGV
  Fault address:  0x0000000000000000
  Load base:      0x[addr]
  ASLR offset:    0x[addr]
  Stack frames:   [N]
    [0]  0x[runtime]  ->  static 0x[static]
    [1]  0x[runtime]  ->  static 0x[static]
    ...
  (pass --binary <path> --addr2line addr2line to flare_reader.py to resolve symbols)
  Registers (x86-64):
    rax     0x[value]
    rbx     0x[value]
    ...
    rip     0x[value]
    rflags  0x[value]

(crash log deleted - run again to repeat the demo)

Test 1: writing emergency records to file
-------------------------------------------
successfully wrote 4 emergency records

Test 2: reading emergency records from file
...

=== all Flare tests passed! ===

The crash demo resets automatically - each odd run crashes, each even run
displays the captured data and runs the remaining tests.
To symbolicate stack frames:
  python3 flare_reader.py test_signal.flare --binary ./07_flare --addr2line addr2line
*/
