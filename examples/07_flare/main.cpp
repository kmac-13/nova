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

// test tag
struct CrashTag {};

std::uint64_t fixedTimestamp()
{
	return 1704067200000000000ULL; // 2024-01-01 00:00:00 UTC
}

NOVA_LOGGER_TRAITS( CrashTag, CRASH, true, fixedTimestamp );

int main()
{
	std::cout << "=== Flare Library Test ===\n\n";

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
		kmac::flare::EmergencySink sink( &fileWriter );
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
		kmac::nova::Record manualRecord {
			"CRASH",
			kmac::nova::logger_traits< CrashTag >::tagId,
			"crash_handler.cpp",
			"signal_handler",
			99,
			1704067200000000000ULL,
			"SIGSEGV: Segmentation fault at 0xdeadbeef",
			std::strlen( "SIGSEGV: Segmentation fault at 0xdeadbeef" )
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
		std::cout << "missing size field correctly rejected\n";

		std::cout << "\n";
	}

	// test 4: EmergencySink with large message
	std::cout << "Test 4: testing with large message (truncation)\n";
	std::cout << "------------------------------------------------\n";
	{
		std::FILE* emergencyFile = std::fopen( "test_large.flare", "wb" );
		kmac::flare::FileWriter fileWriter( emergencyFile );
		kmac::flare::EmergencySink sink( &fileWriter );

		// create a very large message (larger than UINT16_MAX)
		std::string largeMsg( 70000, 'X' );  // 70KB of 'X'

		kmac::nova::Record largeRecord {
			"CRASH",
			kmac::nova::logger_traits< CrashTag >::tagId,
			"test.cpp",
			"testLarge",
			100,
			1704067200000000000ULL,
			largeMsg.c_str(),
			largeMsg.size()
		};

		sink.process( largeRecord );
		sink.flush();
		std::fclose( emergencyFile );

		// Read it back
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
			std::cout << "  Status: " << readBack.statusString() << "\n";
			std::cout << "  Original size: " << largeMsg.size() << " bytes\n";
			std::cout << "  Read back size: " << readBack.messageLen << " bytes\n";
			std::cout << "  Truncated flag: " << ( readBack.messageTruncated ? "Yes" : "No" ) << "\n";

			if ( readBack.messageTruncated )
			{
				std::cout << "  truncation correctly flagged\n";
			}

			if ( readBack.messageLen < largeMsg.size() )
			{
				std::cout << "  message was truncated (expected)\n";
			}
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
		kmac::flare::EmergencySink sink1( &f1Writer );

		kmac::nova::Record rec1 {
			"TestTag",
			0,
			"file.cpp",
			"func",
			1,
			12345ULL,
			"Message 1",
			9
		};
		sink1.process( rec1 );
		sink1.flush();
		std::fclose( f1 );

		// write same tag again
		std::FILE* f2 = std::fopen( "test_hash2.flare", "wb" );
		kmac::flare::FileWriter f2Writer( f2 );
		kmac::flare::EmergencySink sink2( &f2Writer );

		kmac::nova::Record rec2 {
			"TestTag",
			0,
			"file2.cpp",
			"func2",
			2,
			67890ULL,
			"Message 2",
			9
		};
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
		kmac::flare::EmergencySink seqSink( &seqWriter );

		// write multiple records
		for ( int i = 0; i < 5; ++i )
		{
			std::string msg = "Record " + std::to_string( i );
			kmac::nova::Record rec {
				"SEQ",
				1,
				"test.cpp",
				"testSeq",
				uint32_t( 100 + i ),
				std::uint64_t( 1000 + i ),
				msg.c_str(),
				msg.size()
			};
			seqSink.process( rec );
		}
		seqSink.flush();
		std::fclose( seqFile );

		// read back and verify sequence
		std::ifstream file( "test_sequence.flare", std::ios::binary );
		std::vector< std::uint8_t > data{
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
		std::cout << "sequence numbers 0-" << ( count - 1 ) << " in order\n";
		std::cout << "\n";
	}

	std::cout << "=== all Flare tests passed! ===\n";

	return 0;
}

/*
Expected Output:
================

=== Flare Library Test ===

Test 1: writing emergency records to file
-------------------------------------------
successfully wrote 4 emergency records

Test 2: reading emergency records from file
--------------------------------------------
read 727 bytes from file

Record 1:
  Sequence:  0
  Status:    Complete
  Timestamp: 1704067200000000000 ns
  Tag ID:    0x[hex value]
  Process:   [pid]
  Thread:    [tid]
  File:      main.cpp
  Line:      47
  Function:  main
  Message:   Test record 1: basic message

Record 2:
  Sequence:  1
  Status:    Complete
  Timestamp: 1704067200000000000 ns
  Tag ID:    0x[hex value]
  Process:   [pid]
  Thread:    [tid]
  File:      main.cpp
  Line:      52
  Function:  main
  Message:   Test record 2: with numbers: 42 and 3.14

Record 3:
  Sequence:  2
  Status:    Complete
  Timestamp: 1704067200000000000 ns
  Tag ID:    0x[hex value]
  Process:   [pid]
  Thread:    [tid]
  File:      main.cpp
  Line:      57
  Function:  main
  Message:   Test record 3: multi-line message
Line 2
Line 3

Record 4:
  Sequence:  3
  Status:    Complete
  Timestamp: 1704067200000000000 ns
  Tag ID:    0x[hex value]
  Process:   [pid]
  Thread:    [tid]
  File:      crash_handler.cpp
  Line:      99
  Function:  signal_handler
  Message:   SIGSEGV: Segmentation fault at 0xdeadbeef

successfully parsed 4 records

Test 3: testing Scanner with edge cases
----------------------------------------
empty data correctly rejected
partial magic correctly rejected
missing size field correctly rejected

Test 4: testing with large message (truncation)
------------------------------------------------
large message written and read back
  Status: Truncated
  Original size: 70000 bytes
  Read back size: [bytes read]
  Truncated flag: Yes
  truncation correctly flagged
  message was truncated (expected)

Test 5: testing tag hash consistency
-------------------------------------
read both records
  record 1 tag hash: 0x[hex]
  record 2 tag hash: 0x[hex]
  tag hashes match (consistent hashing)

Test 6: testing sequence number ordering
-----------------------------------------
read 5 records
sequence numbers 0-4 in order

=== all Flare tests passed! ===


Key Takeaways:
==============

1. **Flare Emergency Logging**: Designed for crash-safe forensic logging in emergency scenarios
2. **FileWriter Pattern**: EmergencySink requires an IWriter implementation (FileWriter for FILE*)
3. **Binary TLV Format**: Records stored in compact Type-Length-Value format for reliability
4. **Automatic Metadata**: Process ID, thread ID, sequence numbers added automatically
5. **Graceful Truncation**: Large messages are truncated with appropriate status flags
6. **Reader Resilience**: Can parse partial/corrupted records from crash dumps
7. **Hash Consistency**: Tag strings consistently hash to same ID across writes
8. **No Heap Allocation**: Emergency logging uses only stack buffers (4KB default)


What This Example Demonstrates:
================================

**Basic Workflow**:
- opening a binary .flare file for emergency logging
- creating FileWriter to wrap FILE* handle
- binding EmergencySink to Nova logger
- writing records via RecordBuilder (automatic) or direct API (manual)
- flushing to ensure data reaches disk
- reading back records with Reader
- parsing TLV-encoded binary format

**Advanced Features**:
- multi-line message handling
- large message truncation (70KB → buffer limit)
- scanner edge case handling (partial data, missing fields)
- tag hash consistency verification
- sequence number monotonicity
- process/thread ID capture

**Error Handling**:
- scanner rejects malformed data gracefully
- reader extracts partial information from corrupted records
- truncation is flagged but doesn't prevent parsing
- all operations are async-signal-safe (except FILE* functions)


Real-World Usage Pattern:
==========================

// crash handler setup (called once at startup)
void setupCrashLogging()
{
	static FILE* crashLog = fopen("/var/log/app_crash.flare", "ab");
	static FileWriter writer(crashLog);
	static EmergencySink sink(&writer, true);  // capture process info

	Logger<CrashTag>::bindSink(&sink);

	// install signal handlers that log via CrashTag before terminating
}

// in-signal handler (must be async-signal-safe)
void signalHandler(int sig)
{
	// this is safe in signal handlers (no malloc, no locks beyond atomic)
	TruncatingRecordBuilder<CrashTag> builder(__FILE__, __FUNCTION__, __LINE__);
	builder << "Signal " << sig << " received at " << getCurrentAddress();

	// record is automatically flushed when builder destructs
	// (though in real crash handler, process may terminate immediately)
}

// post-crash analysis
void analyzeCrashLog()
{
	ifstream file("/var/log/app_crash.flare", ios::binary);
	vector<uint8_t> data(istreambuf_iterator<char>(file), {});

	Reader reader;
	Record record;

	while (reader.parseNext(data.data(), data.size(), record))
	{
		cout << record.statusString() << ": " << record.message << "\n";

		// can filter by sequence, timestamp, tag, etc.
		// can detect torn writes via status field
		// can reconstruct timeline from timestamps
	}
}


Comparison with Nova Examples:
===============================

**Nova Examples**:
- general-purpose logging with flexible routing
- multiple sinks (console, file, composite)
- thread-safe wrappers (SynchronizedSink)
- runtime filtering (FilterSink)
- custom timestamps and formatting

**Flare Example**:
- emergency/forensic logging only
- single purpose: crash-safe binary logging
- no formatting (raw TLV encoding)
- minimal overhead (stack-only, no heap)
- designed for signal handlers and crash scenarios


When to Use Flare:
===================
- signal handlers (SIGSEGV, SIGABRT, etc.)
- crash dump forensics
- embedded systems with limited resources
- real-time systems (deterministic, lock-free writes)
- scenarios where process may terminate unexpectedly
- post-mortem debugging

When NOT to Use Flare:
======================
- regular application logging (use Nova + OStreamSink instead)
- human-readable logs (use Nova + FormattingSink instead)
- interactive debugging (use standard debugger)


Advanced Usage:
===============

**Custom IWriter for Raw Syscalls**:
Instead of FileWriter (which uses FILE and fwrite), implement IWriter
with raw syscalls for true async-signal-safety:

class SyscallWriter : public IWriter
{
	int _fd;
public:
	SyscallWriter(int fd) : _fd(fd) {}

	size_t write(const void* data, size_t size) noexcept override
	{
		return ::write(_fd, data, size);  // POSIX syscall (async-signal-safe)
	}

	void flush() noexcept override
	{
		::fsync(_fd);  // Force to disk
	}
};

// Usage:
int fd = open("/var/log/crash.flare", O_WRONLY | O_CREAT | O_APPEND, 0644);
SyscallWriter writer(fd);
EmergencySink sink(&writer);


**Analyzing Crash Logs with Python**:
Flare includes Python tools in scripts/ for parsing .flare files:

	python scripts/analyze_crash.py crash.flare --json output.json

This generates human-readable JSON with all parsed records.


**Integration with Core Dumps**:
Flare logs complement core dumps:
- core dump: memory state at crash
- Flare log: event sequence leading to crash
- together: complete forensic picture


File Format Details:
====================

Each record in .flare file:
- MAGIC (8 bytes): 0x52 0x4C 0x46 0x5F 0x43 0x41 0x4D 0x4B ("FLARE_KMAC")
- SIZE (4 bytes): Total record size in bytes
- TLVs: Type-Length-Value triplets
  - RecordStatus (0x03): Complete/Truncated/InProgress
  - SequenceNumber (0x04): Monotonic counter
  - TimestampNs (0x0A): Nanosecond timestamp
  - TagId (0x0B): FNV-1a hash of tag string
  - FileName (0x0C): Source file
  - LineNumber (0x0D): Source line
  - FunctionName (0x0E): Function name
  - ProcessId (0x0F): Process ID
  - ThreadId (0x10): Thread ID
  - MessageBytes (0x14): Log message
  - MessageTruncated (0x15): Truncation flag
- END (2 bytes): 0xFF 0xFF marker

Reader is forward-compatible: unknown TLV types are skipped.


Thread Safety Notes:
=====================

- EmergencySink itself is NOT thread-safe
- Multiple threads should use separate EmergencySink instances
- OR wrap in SynchronizedSink (adds mutex, loses async-signal-safety)
- OR use lock-free techniques (one sink per thread, thread-local)

For signal handlers:
- EmergencySink operations are async-signal-safe
- EXCEPT when using FileWriter (fwrite/fflush are not async-signal-safe)
- Use SyscallWriter with raw write() for true async-signal-safety


Common Pitfalls:
================

1. **forgetting to flush**: call sink.flush() before fclose()
2. **FILE* lifetime**: FileWriter doesn't own FILE*, caller must manage
3. **large messages**: messages >4KB are truncated (check messageTruncated flag)
4. **tag hashing**: different tag strings may hash to same ID (very rare)
5. **binary format**: Don't try to cat/grep .flare files (use Reader or Python tools)
6. **concurrent writes**: multiple threads writing to same sink needs synchronization


Next Steps:
===========

1. **Try the Nova examples**: Learn the general-purpose logging features
2. **Read flare/README.md**: Understand design philosophy and use cases
3. **Explore Python tools**: scripts/analyze_crash.py for post-mortem analysis
4. **Review emergency_sink.h**: See all TLV types and encoding details
5. **Check reader.h**: Understand corruption handling and resilience
6. **Integrate with crash handler**: Use Flare in your signal handlers

*/
