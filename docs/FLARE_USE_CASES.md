# Flare - Use Cases and Examples

This document covers practical usage patterns for Flare.  For architecture, API reference, and writer descriptions see [FLARE_README.md](FLARE_README.md).

Flare's `EmergencySink` always writes binary TLV format.  Nova's regular sinks produce human-readable text.  Both can run simultaneously - see the Human-Readable vs Binary Output section in `FLARE_README.md` for how to route records to both.

---

## Common Setup Pattern

All use cases share the same setup:

```cpp
#include <kmac/nova.h>
#include <kmac/flare.h>
#include <fcntl.h>

struct CrashTag {};
NOVA_LOGGER_TRAITS( CrashTag, CRASH, true, kmac::nova::TimestampHelper::steadyNanosecs );

// static storage - must outlive all log calls and signal handlers
static int _flareFd = open( "/var/log/app.flare", O_WRONLY | O_CREAT | O_APPEND, 0644 );
static kmac::flare::FdWriter _flareWriter( _flareFd );
static kmac::flare::EmergencySink<> _flareSink( &_flareWriter );

void setupFlare()
{
	// bind directly - no ScopedConfigurator; this binding must persist for program lifetime
	kmac::nova::Logger< CrashTag >::bindSink( &_flareSink );
}
```

**Always initialize Flare before installing signal handlers.**  See the Quick Setup in `FLARE_README.md` for a complete example including `SignalHandler<>`.

---

## Use Case 1: Crash Handler Logging

**Scenario**: Your application crashes with SIGSEGV.  You want to know what was happening right before the crash.

### The Problem

```cpp
// DANGEROUS - will likely deadlock or crash worse
void crashHandler( int sig )
{
	std::cerr << "Crash detected!\n";         // unsafe - stdio uses locks internally

	logger->error( "Segfault occurred" );     // unsafe for any mutex-based logger:
	// if the crash occurred while another thread held the logger's write mutex,
	// this call will deadlock waiting for a mutex that will never be released
}
```

### The Solution

```cpp
#include <kmac/nova.h>
#include <kmac/flare.h>
#include <signal.h>
#include <fcntl.h>

struct CrashTag {};
NOVA_LOGGER_TRAITS( CrashTag, CRASH, true, kmac::nova::TimestampHelper::steadyNanosecs );

static int _flareFd = open( "/var/log/crash.flare", O_WRONLY | O_CREAT | O_APPEND, 0644 );
static kmac::flare::FdWriter _flareWriter( _flareFd );
static kmac::flare::EmergencySink<> _flareSink( &_flareWriter );

void crashHandler( int sig, siginfo_t*, void* )
{
	// safe - NOVA_FLARE_LOG is stack-based and _flareSink uses FdWriter
	NOVA_FLARE_LOG( CrashTag ) << "Signal " << sig;
	_Exit( 128 + sig );
}

int main()
{
	// NOTE: make sure to bind the crash sink to the associated Logger
	kmac::nova::Logger< CrashTag >::bindSink( &_flareSink );

	// use SignalHandler<> for automatic register/fault capture, or install manually:
	struct sigaction sa{};
	sa.sa_sigaction = crashHandler;
	sa.sa_flags = SA_SIGINFO;
	sigaction( SIGSEGV, &sa, nullptr );
	sigaction( SIGABRT, &sa, nullptr );

	// log important state before potential crash
	NOVA_FLARE_LOG( CrashTag ) << "Starting critical operation";

	// ... application code ...
	return 0;
}
```

### After the Crash

```bash
python flare_reader.py crash.flare
# [2025-06-01T14:23:45.432Z] CRASH: Starting critical operation
# [2025-06-01T14:23:47.891Z] CRASH: Signal 11
```

For automatic CPU register and fault address capture, use `SignalHandler<>::install( &_flareSink )` instead of installing signal handlers manually (see `FLARE_README.md`).

---

## Use Case 2: Real-Time System Fault Recording

**Scenario**: A timing violation or safety fault occurs in a real-time control loop.  You need deterministic, no-allocation logging that also survives a crash.

```cpp
#include <kmac/nova.h>
#include <kmac/flare.h>
#include <fcntl.h>

struct SafetyTag {};
NOVA_LOGGER_TRAITS( SafetyTag, SAFETY, true, kmac::nova::TimestampHelper::steadyNanosecs );

static int _safetyFd = open( "/var/log/safety.flare", O_WRONLY | O_CREAT | O_APPEND, 0644 );
static kmac::flare::FdWriter _safetyWriter( _safetyFd );
static kmac::flare::EmergencySink<> _safetySink( &_safetyWriter );

void controlLoop()
{
	kmac::nova::Logger< SafetyTag >::bindSink( &_safetySink );

	while ( running )
	{
		auto position = readSensor();
		auto control = calculateControl( position );

		if ( position > SAFE_LIMIT )
		{
			// deterministic: no allocation, no locks, bounded write() call
			NOVA_FLARE_LOG( SafetyTag )
				<< "SAFETY VIOLATION: pos=" << position
				<< " limit=" << SAFE_LIMIT;
		}

		applyControl( control );

		if ( loopElapsed() > DEADLINE_NS )
		{
			NOVA_FLARE_LOG( SafetyTag )
				<< "DEADLINE MISS: " << loopElapsed() << "ns";
		}
	}
}
```

**Why this works**: `NOVA_FLARE_LOG` is stack-allocated with no TLS; `EmergencySink` uses `write()` only - no locks, no allocation, bounded worst-case time.

---

## Use Case 3: Memory Corruption Debugging

**Scenario**: Occasional memory corruption is hard to reproduce.  You want to log the allocation and free sites closest to the corruption.

```cpp
#include <kmac/nova.h>
#include <kmac/flare.h>
#include <fcntl.h>

struct MemDebugTag {};
NOVA_LOGGER_TRAITS( MemDebugTag, MEMDEBUG, true, kmac::nova::TimestampHelper::steadyNanosecs );

static int _memFd = open( "/var/log/mem.flare", O_WRONLY | O_CREAT | O_APPEND, 0644 );
static kmac::flare::FdWriter _memWriter( _memFd );
static kmac::flare::EmergencySink<> _memSink( &_memWriter );

void* trackedMalloc( std::size_t size, const char* file, int line )
{
	void* ptr = ::malloc( size );
	NOVA_FLARE_LOG( MemDebugTag )
		<< "ALLOC " << ptr << " size=" << size << " at " << file << ":" << line;
	installCanaries( ptr, size );
	return ptr;
}

void trackedFree( void* ptr, const char* file, int line )
{
	if ( ! checkCanaries( ptr ) )
	{
		NOVA_FLARE_LOG( MemDebugTag )
			<< "CORRUPTION at " << ptr << " freed from " << file << ":" << line;
		_memSink.flush();  // ensure the record lands before potential crash
	}
	::free( ptr );
}
```

> ⚠️ Logging from inside allocator hooks has re-entrancy risks.  `NOVA_FLARE_LOG` is stack-allocated and `EmergencySink` does not allocate, but ensure the sink's writer does not call back into the allocator.  `FdWriter` is safe; `FileWriter` may allocate internally.

---

## Use Case 4: Embedded System Black Box

**Scenario**: Firmware on an RTOS target.  On reset, you want to read the last records to understand why it crashed.

```cpp
#include <kmac/nova.h>
#include <kmac/flare.h>

struct SystemTag {};
struct FaultTag {};
NOVA_LOGGER_TRAITS( SystemTag, SYS, true, /* hardware timer */ );
NOVA_LOGGER_TRAITS( FaultTag, FAULT, true, /* hardware timer */ );

// fixed RAM region that survives a soft reset (place in .noinit or equivalent)
static std::uint8_t _crashBuffer[ 8192 ];
static kmac::flare::RamWriter _ramWriter( _crashBuffer, sizeof( _crashBuffer ) );
static kmac::flare::EmergencySink<> _emergencySink( &_ramWriter );

void setupEmergencyLogging()
{
	kmac::nova::Logger< SystemTag >::bindSink( &_emergencySink );
	kmac::nova::Logger< FaultTag >::bindSink( &_emergencySink );
}

void mainLoop()
{
	while ( true )
	{
		NOVA_FLARE_LOG( SystemTag ) << "Heartbeat " << tickCount;

		if ( ! performOperation() )
		{
			NOVA_FLARE_LOG( FaultTag ) << "Operation failed at step " << currentStep;
		}
	}
}

// on next boot, before overwriting the buffer
void checkPreviousCrash()
{
	kmac::flare::Scanner scanner;
	scanner.scan( _crashBuffer, sizeof( _crashBuffer ) );

	// last records show what happened before reset
	for ( const auto& span : scanner.records() )
	{
		kmac::flare::Reader reader( span.data, span.size );
		// process TLVs...
	}
}
```

**Why this works**: `RamWriter` has no OS dependency and no allocation - suitable for bare-metal and RTOS targets.  Flare's TLV format tolerates partial writes, so even if power is lost mid-write, all records already written remain intact and readable on the next boot.

---

## Use Case 5: Multi-Process Coordination Failures

**Scenario**: Multiple processes communicate via shared memory or pipes.  When coordination breaks down, you want to see each process's last actions.

```cpp
#include <kmac/nova.h>
#include <kmac/flare.h>
#include <fcntl.h>
#include <cstdio>

struct CoordTag {};
NOVA_LOGGER_TRAITS( CoordTag, COORD, true, kmac::nova::TimestampHelper::steadyNanosecs );

void initProcessLogging( int processId )
{
	char filename[ 64 ];
	std::snprintf( filename, sizeof( filename ), "/var/log/proc_%d.flare", processId );

	// static storage per-process
	static int _fd = open( filename, O_WRONLY | O_CREAT | O_TRUNC, 0644 );
	static kmac::flare::FdWriter _writer( _fd );
	static kmac::flare::EmergencySink<> _sink( &_writer );
	kmac::nova::Logger< CoordTag >::bindSink( &_sink );
}

void waitForPeer( int mutexId )
{
	NOVA_FLARE_LOG( CoordTag ) << "Waiting for mutex " << mutexId;

	auto startNs = getMonotonicNs();
	while ( ! tryAcquireMutex( mutexId ) )
	{
		if ( getMonotonicNs() - startNs > TIMEOUT_NS )
		{
			NOVA_FLARE_LOG( CoordTag )
				<< "TIMEOUT waiting for mutex " << mutexId
				<< " held by process " << getMutexOwner( mutexId );
			std::abort();
		}
	}

	NOVA_FLARE_LOG( CoordTag ) << "Acquired mutex " << mutexId;
}
```

**Post-mortem**: examine all `proc_*.flare` files together to reconstruct the sequence of events across processes.

---

## Use Case 6: Exception Path Logging

**Scenario**: You have deep exception handling and want to trace the exception path during stack unwinding, including if `std::terminate` is called.

```cpp
#include <kmac/nova.h>
#include <kmac/flare.h>

struct ExceptionTag {};
NOVA_LOGGER_TRAITS( ExceptionTag, EXCEPTION, true, kmac::nova::TimestampHelper::steadyNanosecs );

static int _excFd = open( "/var/log/exceptions.flare", O_WRONLY | O_CREAT | O_APPEND, 0644 );
static kmac::flare::FdWriter _excWriter( _excFd );
static kmac::flare::EmergencySink<> _excSink( &_excWriter );

class TracedException : public std::exception
{
private:
	const char* _msg;

public:
	explicit TracedException( const char* msg ) noexcept : _msg( msg )
	{
		NOVA_FLARE_LOG( ExceptionTag ) << "THROW: " << _msg;
	}

	~TracedException() noexcept override
	{
		NOVA_FLARE_LOG( ExceptionTag ) << "DESTROY: " << _msg;
	}

	const char* what() const noexcept override { return _msg; }
};

void riskyOperation()
{
	NOVA_FLARE_LOG( ExceptionTag ) << "Enter riskyOperation";

	try
	{
		throw TracedException( "Database connection failed" );
	}
	catch ( ... )
	{
		NOVA_FLARE_LOG( ExceptionTag ) << "Caught in riskyOperation, rethrowing";
		throw;
	}
}
```

**Why `NOVA_FLARE_LOG` here**: `NOVA_FLARE_LOG` is re-entrant safe and uses `FdWriter` which flushes each record to disk immediately.  If `std::terminate` is called before the destructor completes, all records already written remain intact and readable post-mortem - only the record in progress at the point of termination may be partial.

---

## Common Patterns

### Pattern 1: Two-Tier Logging

Route the same tag to both a human-readable sink (for operational logs) and an `EmergencySink` (for crash survival) using `FixedCompositeSink`:

```cpp
#include <kmac/nova/extras/fixed_composite_sink.h>
#include <kmac/nova/extras/ostream_sink.h>

// human-readable operational logging
kmac::nova::extras::OStreamSink consoleSink( std::cout );

// binary crash-resilient logging
// (_flareSink is the static EmergencySink<> from the setup pattern above)

// route CrashTag to both simultaneously
kmac::nova::Sink* sinks[] = { &consoleSink, &_flareSink };
kmac::nova::extras::FixedCompositeSink composite( sinks, 2 );
kmac::nova::Logger< CrashTag >::bindSink( &composite );

// CrashTag records now appear in human-readable console output during normal operation
// and are also preserved in binary Flare format for post-mortem analysis after a crash
```

### Pattern 2: Pre-Crash Heartbeat

```cpp
// log a periodic heartbeat so the last record's timestamp shows when
// the process was last known to be alive
void heartbeatThread()
{
	while ( running )
	{
		NOVA_FLARE_LOG( CrashTag ) << "Heartbeat " << tick++;
		sleepMs( 1000 );
	}
}
```

---

## Reading Flare Logs

### Using `flare_reader.py`

```bash
# text output
python flare_reader.py crash.flare

# with tag name dictionary (maps tag IDs to human-readable names)
python flare_reader.py crash.flare --dict crash.tags

# JSON output for log aggregation
python flare_reader.py crash.flare --format json

# with symbolization (requires the original binary and addr2line)
python flare_reader.py crash.flare --binary ./myapp --addr2line addr2line
```

### C++ API

```cpp
#include <kmac/flare/scanner.h>
#include <kmac/flare/reader.h>
#include <vector>
#include <fstream>

void analyzeCrashLog( const char* filename )
{
	// read the entire file into memory
	std::ifstream f( filename, std::ios::binary );
	std::vector< char > data(
		( std::istreambuf_iterator< char >( f ) ),  // iterator over raw file bytes
		std::istreambuf_iterator< char >() );       // end-of-stream sentinel

	// scan the buffer for Flare record boundaries
	kmac::flare::Scanner scanner;
	scanner.scan( data.data(), data.size() );

	// decode each located record
	for ( const auto& span : scanner.records() )
	{
		kmac::flare::Reader reader( span.data, span.size );
		// access TLVs via reader methods
	}
}
```

---

## When Not to Use Flare

- **Normal application logging** - use Nova with `SynchronizedSink`, `MemoryPoolAsyncSink`, or similar
- **High-volume logging** - Flare flushes per record; throughput is limited by `write()` syscall rate
- **Human-readable output** - Flare is binary; use `flare_reader.py` for post-mortem analysis
- **Rich formatting or filtering** - apply these at the Nova layer before records reach Flare
