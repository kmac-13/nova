# Nova Extras Library - Complete Class Reference

## Overview

The Nova Extras library provides optional components that extend Nova's core functionality.  Components are divided into two broad categories:

- **Safety-critical capable** - zero heap allocation, deterministic, marked ✅ SAFE FOR SAFETY-CRITICAL SYSTEMS in their headers: `NullSink`, `SpinlockSink`, `SynchronizedSink`, `BoundedCompositeSink`, `MemoryPoolAsyncSink`, `MemoryPoolAsyncBatchSink`, and the formatter/file-sink pipeline.
- **General-purpose only** - use heap allocation or have unbounded timing: `CompositeSink`, `RollingFileSink`, `StreamingRecordBuilder`, `MultilineFormatter`.

See `docs/SAFETY_CRITICAL_GUIDELINES.md` for per-component safety level recommendations.

**Organization:**
- **Sinks** - output destinations for log records
- **Formatters** - transform records before output
- **Builders** - alternative record construction methods
- **Utilities** - supporting components (queues, buffers, tags)

> Nova Extras is not a monolithic library — include only the headers you need.  Each component has its own header and no component forces you to use another.

> All examples assume `using namespace kmac::nova::extras;` and `using namespace kmac::nova;` for brevity unless otherwise noted.

---

## Sinks

### NullSink

**Purpose**: Discards all log records (no-op sink).

**Use When:**
- ✅ routing records to a no-op sink while keeping the binding intact (use `Logger<Tag>::unbindSink()` instead to remove the binding entirely and skip `process()` calls)
- ✅ testing/benchmarking logger overhead
- ✅ placeholder during development
- ✅ swapping logging on/off by replacing the bound sink at runtime (alternatively, bind/unbind directly)

**Avoid When:**
- ❌ you actually want to log something

**Key Features:**
- singleton pattern available (`NullSink::instance()`)
- zero delivery overhead - record building (the `<<` chain) still executes, but `process()` is a no-op
- useful for measuring logging infrastructure cost

**Example:**
```cpp
// discard all VerboseDebug records
Logger< VerboseDebug >::bindSink( &NullSink::instance() );

// benchmark record builder overhead
auto start = std::chrono::steady_clock::now();
for ( int i = 0; i < 1000000; ++i ) {
	NOVA_LOG( VerboseDebug ) << "Test";
}
auto elapsed = std::chrono::steady_clock::now() - start;
// measures builder overhead only - process() is a no-op
```

---

### QuickStart

**Purpose**: Zero-configuration console logging for prototyping and early development.

**Use When:**
- ✅ getting Nova working immediately without any setup
- ✅ examples, prototypes, quick experiments

**Avoid When:**
- ❌ production use requiring zero-cost disabling - severity tags are always enabled
- ❌ embedded or real-time targets (uses `std::cout`, `std::mutex`)
- ❌ you need domain-based tags (QuickStart binds only the six severity tags from `severities.h`)

**Key Features:**
- wires up `SynchronizedSink` → `FormattingSink` → `ISO8601Formatter` → `OStreamSink(std::cout)` in one declaration
- binds all six severity tags (`TraceTag` through `FatalTag`)
- RAII - all bindings released on destruction
- accepts a custom `std::ostream` for output redirection

**Example:**
```cpp
#include <kmac/nova/extras/quick_start.h>

int main()
{
	// default: std::cout, minimum severity = Info (binds InfoTag through FatalTag)
	kmac::nova::extras::QuickStart logging;

	NOVA_LOG_INFO()  << "Application started";
	NOVA_LOG_WARN()  << "Configuration file not found, using defaults";
	NOVA_LOG_ERROR() << "Failed to connect to database";

	return 0;
}  // all bindings released here
```

**Optional constructor arguments:**
```cpp
using QS = kmac::nova::extras::QuickStart;

// lower threshold to include debug and trace output
QS logging( QS::Severity::Debug );

// redirect to file, default threshold (Info)
std::ofstream logFile( "debug.log" );
QS fileLogging( logFile );

// custom stream + custom threshold
QS customLogging( logFile, QS::Severity::Warning );
```

**Redirect to file:**
```cpp
std::ofstream logFile("debug.log");
kmac::nova::extras::QuickStart logging( logFile );
```

---

### OStreamSink

**Purpose**: Writes records to any C++ ostream (cout, cerr, fstream, etc.).

**Use When:**
- ✅ console logging (stdout/stderr)
- ✅ file logging (ofstream)
- ✅ string stream capture (ostringstream)
- ✅ simple applications

**Avoid When:**
- ❌ high-throughput logging (not buffered/async)
- ❌ need file rotation
- ❌ multi-threaded without synchronization

**Key Features:**
- **writes raw message content only** (no newlines, timestamps, or formatting)
- works with any std::ostream
- simple, straightforward implementation
- no built-in thread safety (wrap in SynchronizedSink if needed)

**Important**: OStreamSink outputs the exact message text with no additional formatting:
- no automatic newlines (messages will concatenate)
- no timestamps, tags, or metadata
- no contextual information, e.g. file/function/line

For formatted output with metadata, use `FormattingSink` instead.

**Example:**
```cpp
// console logging
OStreamSink console( std::cout );
Logger< Info >::bindSink( &console );

NOVA_LOG( Info ) << "First";
NOVA_LOG( Info ) << "Second";
// outputs: "FirstSecond" (no newline between messages)

// file logging
std::ofstream logFile( "app.log" );
OStreamSink fileSink( logFile );
Logger< Error >::bindSink( &fileSink );

// capture to string
std::ostringstream oss;
OStreamSink capture( oss );
Logger< Test >::bindSink( &capture );
// ... later ...
std::string logs = oss.str();

// for formatted output with timestamps and newlines:
OStreamSink base( std::cout );
ISO8601Formatter formatter;
FormattingSink<> formatted( base, formatter );  // format: timestamp [TAG] file:line func - message\n
Logger< Info >::bindSink( &formatted );
```

---

### RollingFileSink

**Purpose**: Writes to files with automatic size-based rotation.

**Use When:**
- ✅ long-running applications
- ✅ need to limit disk usage
- ✅ want automatic log rotation
- ✅ production logging

**Avoid When:**
- ❌ real-time systems (involves file I/O)
- ❌ need time-based rotation (only size-based)
- ❌ multi-threaded without synchronization

**Key Features:**
- size-based rotation (configurable max file size)
- no file renaming (highest number is current)
- rollover callback for custom actions
- uses C FILE* interface for high performance

**File Naming Pattern:**
```
app.log.1
app.log.2
app.log.3
app.log.4  ← current file (highest number)
```

**Example:**
```cpp
// 10MB per file
RollingFileSink sink( "app.log", 10 * 1024 * 1024 );

// optional: compress/cleanup on rotation
sink.setRolloverCallback( []( const std::string& closed, const std::string& newFile ) {
	// compress old file
	std::system( ( "gzip " + closed ).c_str() );

	// delete files older than 30 days
	cleanupOldLogs( "app.log", 30 );

	// upload to S3
	uploadToCloud( closed + ".gz" );
} );

Logger< App >::bindSink( &sink );
```

---


### SynchronizedSink

**Purpose**: Thread-safe wrapper using std::mutex.

**Use When:**
- ✅ multiple threads logging to same sink
- ✅ downstream sink is not thread-safe
- ✅ correct mutex overhead acceptable

**Avoid When:**
- ❌ real-time systems (mutex can block)
- ❌ high contention (consider MemoryPoolAsyncSink or SpinlockSink)
- ❌ downstream is already thread-safe

**Key Features:**
- uses std::mutex for synchronization
- wraps any sink
- simple, correct implementation

**Example:**
```cpp
OStreamSink console( std::cout );
SynchronizedSink sync( console );  // now thread-safe

Logger< ThreadedTag >::bindSink( &sync );

// safe from multiple threads
std::thread t1( []{ NOVA_LOG( ThreadedTag ) << "Thread 1"; } );
std::thread t2( []{ NOVA_LOG( ThreadedTag ) << "Thread 2"; } );
```

---

### SpinlockSink

**Purpose**: Thread-safe wrapper using spinlock (busy-wait).

**Use When:**
- ✅ very short critical sections
- ✅ low contention expected
- ✅ cannot use mutex (e.g., signal handlers on some platforms)

**Avoid When:**
- ❌ high contention (wastes CPU cycles)
- ❌ long-held locks
- ❌ general-purpose multi-threading (use SynchronizedSink)

> For high contention from many threads, prefer `MemoryPoolAsyncSink` - its producer enqueue is lock-free and adds no contention regardless of thread count.

**Key Features:**
- spinlock instead of mutex
- lower latency under low contention
- can waste CPU under high contention

**Example:**
```cpp
OStreamSink console( std::cout );
SpinlockSink spin( console );

Logger< FastTag >::bindSink( &spin );

// better for occasional concurrent access
std::thread t1( []{ NOVA_LOG( FastTag ) << "Thread 1"; } );
std::thread t2( []{ NOVA_LOG( FastTag ) << "Thread 2"; } );
```

---

### MemoryPoolAsyncSink

**Purpose**: Async sink that copies records into a fixed memory pool for processing by a background thread.

One of the fundamental problems with naive async logging: a background thread holds a `Record` whose `message` pointer refers to a buffer that the logging thread has already reused or destroyed.  `MemoryPoolAsyncSink` solves this by copying both the `Record` struct **and** the message bytes into a pre-allocated pool.  The `Record.message` pointer is then rewritten to point into the pool, so it remains valid until the consumer thread processes and releases the entry.

**Note**: The `file`, `function`, and `tag` fields in a `Record` are `const char*` pointers to static storage (string literals compiled into the binary).  These are **not** copied into the pool — they are expected to remain valid for the lifetime of the application, which is always the case when using `NOVA_LOG` macros.

**Use When:**
- ✅ need async logging without blocking the calling thread
- ✅ zero heap allocation required (pool is pre-allocated at construction)
- ✅ message loss under pool exhaustion is acceptable

**Avoid When:**
- ❌ guaranteed delivery required — records are dropped when pool is full (use synchronous delivery with `SynchronizedSink` instead)
- ❌ memory extremely constrained — pool + index queue adds ~288KB at default sizes
- ❌ synchronous logging is acceptable (simpler: `SpinlockSink` or `SynchronizedSink`)

**Key Features:**
- pool stores `Record` metadata + message bytes contiguously; no pointers escape
- lock-free MPSC queue for producer→consumer handoff
- `droppedCount()` tracks records lost to pool/queue exhaustion
- thread lifecycle is explicit: call `start()` before use, `stopAndDrain()` or `stopAndDiscard()` before destruction (supports fork-safe patterns)

**Template Parameters:**
```cpp
template<
    std::size_t PoolSize           = 256 * 1024,  // bytes; record + message storage
    std::size_t IndexQueueCapacity = 8192,        // max pending entries
    typename    IndexType          = uint32_t,    // offset type (uint16_t saves memory)
    PoolAllocator Allocator        = PoolAllocator::Heap
>
```

**Example:**
```cpp
RollingFileSink rolling( "app.log", 100 * 1024 * 1024 );
MemoryPoolAsyncSink<> async( rolling );
async.start();  // start consumer thread before binding

Logger< HighFreq >::bindSink( &async );

// check for drops periodically
if ( async.droppedCount() > 0 ) {
	// pool or queue was full; increase PoolSize or IndexQueueCapacity
}

// on shutdown
async.stopAndDrain();  // wait for all queued records to be delivered
```

---

### MemoryPoolAsyncBatchSink

**Purpose**: Async sink that defers formatting to the consumer thread, keeping the producer hot path as cheap as possible.

Extends the memory-pool approach of `MemoryPoolAsyncSink` by also moving the `Formatter` call off the calling thread and performing the formatting in batches.  Producers store raw records in the pool; the consumer dequeues batches, formats them into a 256KB stack buffer, and flushes to the downstream sink in large writes.  This improves cache locality and reduces the number of downstream calls compared to formatting per-record on the producer.

**Note**: Same static pointer contract as `MemoryPoolAsyncSink` — `file`, `function`, and `tag` are not copied and must remain valid (always true when using `NOVA_LOG` macros).

**Use When:**
- ✅ high-throughput logging where formatting overhead on the hot path matters
- ✅ using an ISO 8601 or other stateful `Formatter` (formatter is single-threaded on consumer)
- ✅ downstream sink benefits from large batched writes (e.g. `RollingFileSink`)

**Avoid When:**
- ❌ formatting must appear in real-time — consumer processes asynchronously
- ❌ guaranteed delivery required — full pool drops records
- ❌ no formatter needed — use `MemoryPoolAsyncSink` directly

**Key Features:**
- same safe memory pool design as `MemoryPoolAsyncSink`
- consumer formats a batch into a 256KB stack buffer before flushing
- formatter instance is owned by the caller and accessed only from the consumer thread
- thread lifecycle is explicit: call `start()` before use, `stopAndDrain()` or `stopAndDiscard()` before destruction

**Template Parameters:**
```cpp
template<
    std::size_t PoolSize           = 1024 * 1024,  // bytes; larger default for batch accumulation
    std::size_t IndexQueueCapacity = 8192,
    typename    IndexType          = uint32_t,
    PoolAllocator Allocator        = PoolAllocator::Heap
>
```

**Example:**
```cpp
ISO8601Formatter formatter;                        // owned by caller, used only on consumer thread
RollingFileSink rolling( "app.log", 100 * 1024 * 1024 );
MemoryPoolAsyncBatchSink<> async( rolling, &formatter );
async.start();  // start consumer thread before binding

Logger< HighFreq >::bindSink( &async );

// on shutdown
async.stopAndDrain();  // wait for all queued records to be formatted and delivered
```

---

### CompositeSink / BoundedCompositeSink / FixedCompositeSink

**Purpose**: Fan-out to multiple downstream sinks.

**Variants:**

| Variant | Storage | Capacity | Use Case |
|---------|---------|----------|----------|
| **CompositeSink** | std::vector (heap) | dynamic | general purpose |
| **BoundedCompositeSink** | std::array (stack) | fixed (template param) | no heap allocation |
| **FixedCompositeSink** | array pointer | fixed (runtime param) | pre-allocated arrays |

**Use When:**
- ✅ fan-out to multiple downstream sinks
- ✅ different formatting per sink
- ✅ conditional output (console + file)

**Avoid When:**
- ❌ only one destination needed
- ❌ complex routing logic (use FilterSink instead)

**Example:**
```cpp
// console + file simultaneously
OStreamSink console( std::cout );
RollingFileSink file( "app.log", 10 * 1024 * 1024 );

CompositeSink composite;
composite.add( &console );
composite.add( &file );

Logger< App >::bindSink( &composite );

NOVA_LOG( App ) << "Goes to both console and file";

// bounded variant (no heap)
BoundedCompositeSink< 2 > bounded;
bounded.addSink( console );
bounded.addSink( file );
```

---

### FilterSink

**Purpose**: Runtime filtering based on predicate function.

**Use When:**
- ✅ dynamic filtering (by message content, tag, etc.)
- ✅ temporary debugging filters
- ✅ runtime log level control
- ✅ selective forwarding

**Avoid When:**
- ❌ compile-time filtering sufficient (use `LoggerTraits< Tag >::enabled`)
- ❌ runtime filtering sufficient (use `Logger< Tag >::bindSink( nullptr )`)
- ❌ performance-critical path (adds overhead)

**Key Features:**
- template-based (any callable predicate)
- lambda-friendly
- runtime configurable

**Example - filter by tagId (preferred for domain routing):**
```cpp
OStreamSink console( std::cout );

// route only ErrorTag and FatalTag records to a separate sink
OStreamSink errFile( errorLogFile );
auto errorFilter = []( const kmac::nova::Record& r ) noexcept {
	return r.tagId == kmac::nova::LoggerTraits< ErrorTag >::tagId
		|| r.tagId == kmac::nova::LoggerTraits< FatalTag >::tagId;
};
FilterSink< decltype( errorFilter ) > errorsOnly( errFile, errorFilter );
Logger< ErrorTag >::bindSink( &errorsOnly );
Logger< FatalTag >::bindSink( &errorsOnly );

// filter by message content (runtime predicate on message string)
auto keywordFilter = []( const kmac::nova::Record& r ) noexcept {
	return std::strstr( r.message, "timeout" ) != nullptr;
};
FilterSink< decltype( keywordFilter ) > timeouts( console, keywordFilter );
Logger< NetworkTag >::bindSink( &timeouts );
```

---

## Formatters

### FormattingSink

**Purpose**: Applies a `Formatter` to records and writes the result to a downstream `Sink`.

**Use When:**
- ✅ want timestamps, tag, source location in output
- ✅ need a specific output format (ISO 8601, JSON, CSV, custom)
- ✅ human-readable or machine-parseable logs

**Avoid When:**
- ❌ message already contains all needed info (e.g. use `OStreamSink` directly)

**Constructor:** `FormattingSink< BufferSize = 256KB >( Sink& downstream, Formatter& formatter )`

**Output format is entirely determined by the `Formatter` passed in.**

**Note:** `Formatter`s and `FormattingSink` are not thread-safe.  Either wrap in a `SyncrhonizedSink`, use `MemoryPoolAsyncBatchSink` (for backend formatting), or ensure each `FormattingSink` instance has its own formatter.

**Example:**
```cpp
OStreamSink console( std::cout );
ISO8601Formatter formatter;  // one instance per thread
FormattingSink<> formatted( console, formatter );

Logger< App >::bindSink( &formatted );
NOVA_LOG( App ) << "Hello";
// output: 2025-06-01T14:23:45.432Z [APP] main.cpp:15 main - Hello
```

---

### Available Formatters

Formatters implement the `Formatter` interface and are passed to `FormattingSink`.

| Formatter | Output | Thread-Safe |
|-----------|--------|-------------|
| `ISO8601Formatter` | `2025-06-01T14:23:45.432Z [TAG] file.cpp:42 fn - msg\n` | ❌ one instance per thread |
| `JSONFormatter` | `{"ts":"...","tag":"...","msg":"...","file":"...","line":42}\n` | ❌ one instance per thread |
| `CSVFormatter` | `timestamp,tag,file,line,function,message\n` | ❌ one instance per thread |
| `XMLFormatter` | `<record><ts>...</ts><tag>...</tag><msg>...</msg></record>\n` | ❌ one instance per thread |
| `CustomFormatter` | user-defined formatting | ❌ not thread-safe |
| User-defined Formatter | any type implementing `Formatter` | depends on implementation |

**Example - ISO8601:**
```cpp
OStreamSink fileSink( logFile );
ISO8601Formatter formatter;
FormattingSink<> sink( fileSink, formatter );
Logger< Production >::bindSink( &sink );
NOVA_LOG( Production ) << "Server started";
// output: 2025-06-01T14:23:45.432Z [PRODUCTION] server.cpp:89 main - Server started
```

**Example - JSON:**
```cpp
OStreamSink fileSink( logFile );
JSONFormatter jsonFmt;
FormattingSink<> sink( fileSink, jsonFmt );
Logger< App >::bindSink( &sink );
NOVA_LOG( App ) << "User logged in";
// output: {"ts":"2025-06-01T14:23:45.432Z","tag":"APP","msg":"User logged in","file":"main.cpp","line":42}
```

**User-defined formatter example** (`CustomFormatter` is a Nova-provided type; the example below shows the pattern for writing your own):
```cpp
class LogFormatter final : public kmac::nova::extras::Formatter
{
public:
	void begin( const kmac::nova::Record& record ) noexcept override
	{
		// called once before any format() calls for a new record
		// reset any per-record state here
	}

	bool format( const kmac::nova::Record& record, kmac::nova::extras::Buffer& buf ) noexcept override
	{
		buf.append( record.tag, std::strlen( record.tag ) );
		buf.appendChar( ':' );
		buf.append( record.message, record.messageSize );
		buf.appendChar( '\n' );
		return true;  // return false if more format() calls needed (multi-chunk output)
	}
};
```

---

### LargePayloadFormatter (MultiRecordFormatter)

**Purpose**: Breaks large payloads into fixed-size chunks (chunk size is required at construction).

**Use When:**
- ✅ binary dumps too large for single record
- ✅ downstream has size limits
- ✅ need to preserve all data
- ✅ multi-KB messages

**Avoid When:**
- ❌ messages fit in builder buffer
- ❌ don't need chunking
- ❌ want line-based splitting (use MultilineFormatter)

**Output Format:**
```
[TAG] BEGIN_PAYLOAD
[1024 bytes of data]
[1024 bytes of data]
...
END_PAYLOAD
```

**Example:**
```cpp
// large crash dump
const char* crashDump = readCrashDump();  // 5MB
std::size_t dumpSize = getCrashDumpSize();

LargePayloadFormatter formatter( crashDump, dumpSize );

Record record = /* ... */;
formatter.formatAndWrite( record, sink );
// emits records similar to:
// Record 1 (header): "BEGIN 5242880 bytes"
// Record 2..N (chunks): 1024 bytes of raw payload data each
// Record N+1 (footer): "END"
```

---

### MultilineFormatter (MultiRecordFormatter)

**Purpose**: Splits multi-line messages into separate records.

**Use When:**
- ✅ stack traces (one frame per record)
- ✅ multi-line error messages
- ✅ config dumps
- ✅ log aggregators that parse line-by-line

**Avoid When:**
- ❌ single-line messages
- ❌ want to preserve multi-line structure
- ❌ message is already one logical unit

**Output Format (with line numbers):**
```
[1/4] First line
[2/4] Second line
[3/4] Third line
[4/4] Fourth line
```

**Example:**
```cpp
MultilineFormatter formatter( true, false );  // line numbers, skip empty

Record record;
record.message = 
	"Exception: Null pointer\n"
	"Stack:\n"
	"  at func1() db.cpp:100\n"
	"  at func2() server.cpp:200";
record.messageSize = std::strlen( record.message );

formatter.formatAndWrite( record, sink );
// emits 4 separate records:
// Record 1: "[1/4] Exception: Null pointer"
// Record 2: "[2/4] Stack:"
// Record 3: "[3/4]   at func1() db.cpp:100"
// Record 4: "[4/4]   at func2() server.cpp:200"
```

---

## Builders

### StreamingRecordBuilder

**Purpose**: Heap-allocated record builder using std::ostringstream.

**Use When:**
- ✅ convenience more important than determinism
- ✅ heap allocation acceptable
- ✅ variable message lengths
- ✅ not real-time or safety-critical

**Avoid When:**
- ❌ real-time systems
- ❌ safety-critical applications
- ❌ deterministic behavior required
- ❌ crash handlers

**Key Features:**
- unlimited message length
- supports all types with operator<<
- heap allocation for flexibility

**Builder Comparison** (Truncating is in Nova core; Continuation and Streaming are in Nova Extras):

| Feature | Truncating | Continuation | Streaming |
|---------|-----------|--------------|-----------|
| Allocation | stack | stack | **heap** |
| Overflow | truncate | multi-record | **grows** |
| Deterministic | ✅ yes | ✅ yes | ❌ no |
| Real-time safe | ✅ yes | ✅ yes | ❌ no |
| Use case | most | unpredictable size | convenience |

**Example:**
```cpp
// define streaming macros
NOVA_LOG_STREAM( AppTag ) << "Very long message: " << largeData;

// heap allocation allows unlimited size
NOVA_LOG_STREAM( AppTag ) << generateHugeReport();  // 100KB? No problem
```

---

## Utilities

### Buffer

**Purpose**: Fixed-size view over an externally-owned char array for safe, bounded output building.

`Buffer` wraps a caller-supplied array; it does **not** own or allocate memory.  All `append` operations are all-or-nothing - if the remaining space is insufficient, the call returns `false` and the buffer is left unchanged.  There is no truncation flag; callers handle partial-write situations by checking the return value and either flushing the underlying storage or discarding the record.  This is the contract used by `ISO8601Formatter`
and the other built-in formatters when writing to `FormattingFileSink`.

**Use When:**
- ✅ implementing a `Formatter` (the `format()` API requires it)
- ✅ building fixed-size output in safety-critical or real-time code

**Avoid When:**
- ❌ need dynamic sizing (use `std::string` or `std::ostringstream`)
- ❌ need automatic truncation with a marker (use `TruncatingRecordBuilder`)

**Key Features:**
- wraps external storage - zero allocation
- all-or-nothing append semantics prevent partial writes
- returns `false` on overflow so callers can react (flush and retry)

**Example:**
```cpp
char storage[ 1024 ];
Buffer buf( storage, sizeof( storage ) );

// each append either succeeds fully or returns false
if ( ! buf.append( "Hello", 5 ) ) {
	// no space - handle accordingly (flush, skip, etc.)
}
buf.appendChar( ' ' );
buf.append( "World", 5 );

// inspect what was written
const char* data = buf.data();     // not null-terminated
std::size_t written = buf.size();
std::size_t left = buf.remaining();
```

---

### MPSCQueue

**Purpose**: Lock-free Multi-Producer-Single-Consumer queue.

**Use When:**
- ✅ implementing async sinks
- ✅ multiple producers, one consumer
- ✅ need lock-free performance

**Avoid When:**
- ❌ need multi-consumer (MPMC)
- ❌ single-threaded
- ❌ don't need lock-free

**Key Features:**
- lock-free push/pop
- fixed capacity (template parameter)
- approximate size() method

**Example:**
```cpp
MPSCQueue< int, 1024 > queue;

// producer threads (lock-free)
bool pushed = queue.push( 42 );

// consumer thread (single)
int value;
if ( queue.pop( value ) ) {
	// process value
}

std::size_t approxSize = queue.size();
```

---

### HierarchicalTag

**Purpose**: Combines different types, e.g. subsystem + severity, into single tag type.

**Use When:**
- ✅ traditional logging model desired (subsystem + level)
- ✅ want compile-time type safety
- ✅ familiar with Log4j/spdlog style

**Avoid When:**
- ❌ simple tagging sufficient
- ❌ don't need hierarchical organization

**Example:**
```cpp
// define subsystems
struct Audio {};
struct Network {};

// define severity levels - Nova imposes no restrictions here; define what
// makes sense for your system.  severities.h provides a convenient set but
// you are free to define your own, add levels not found there, etc.
struct Debug {};
struct Info {};
struct Warning {};
struct Error {};
struct Diagnostic {};  // example: a custom level not in severities.h

// combine subsystem + severity into a single tag type
using AudioDebug      = HierarchicalTag< Audio, Debug >;
using AudioWarning    = HierarchicalTag< Audio, Warning >;
using AudioDiagnostic = HierarchicalTag< Audio, Diagnostic >;
using NetworkInfo     = HierarchicalTag< Network, Info >;
using NetworkError    = HierarchicalTag< Network, Error >;

// register each combined tag with its name and clock
NOVA_LOGGER_TRAITS( AudioDebug,      AUDIO_DEBUG, true, kmac::nova::TimestampHelper::steadyNanosecs );
NOVA_LOGGER_TRAITS( AudioWarning,    AUDIO_WARN,  true, kmac::nova::TimestampHelper::steadyNanosecs );
NOVA_LOGGER_TRAITS( AudioDiagnostic, AUDIO_DIAG,  true, kmac::nova::TimestampHelper::steadyNanosecs );
NOVA_LOGGER_TRAITS( NetworkInfo,     NET_INFO,    true, kmac::nova::TimestampHelper::steadyNanosecs );
NOVA_LOGGER_TRAITS( NetworkError,    NET_ERROR,   true, kmac::nova::TimestampHelper::steadyNanosecs );

// bind subsystem tags independently
Logger< AudioDebug      >::bindSink( &audioSink );
Logger< AudioWarning    >::bindSink( &audioSink );
Logger< AudioDiagnostic >::bindSink( &audioDiagSink );  // different sink for diagnostics
Logger< NetworkInfo     >::bindSink( &networkSink );
Logger< NetworkError    >::bindSink( &errorSink );      // sink designated for errors

// use with any Nova macro
NOVA_LOG( AudioDebug )      << "Buffer underrun detected";
NOVA_LOG( AudioDiagnostic ) << "Latency spike: " << latencyNs << "ns";
NOVA_LOG( NetworkError )    << "Connection refused";
```

---

## Common Patterns

### Pattern 1: Console + File with Formatting

```cpp
// console: formatted with timestamp
OStreamSink console( std::cout );
ISO8601Formatter consoleFormatter;
FormattingSink<> consoleFormatted( console, consoleFormatter );

// file: also formatted with timestamp (each FormattingSink needs its own formatter instance)
std::ofstream logFile( "app.log" );
OStreamSink fileSink( logFile );
ISO8601Formatter fileFormatter;
FormattingSink<> fileFormatted( fileSink, fileFormatter );

// composite
CompositeSink composite;
composite.add( &consoleFormatted );
composite.add( &fileFormatted );

Logger< App >::bindSink( &composite );
```

### Pattern 2: High-Throughput Async to Rolling File

`MemoryPoolAsyncSink` copies both the `Record` metadata and the message bytes into a fixed pool so the consumer thread never holds dangling pointers.  `MemoryPoolAsyncBatchSink` additionally defers formatting to the consumer for better cache locality and fewer downstream calls.

```cpp
// option A: store raw records, format synchronously on consumer
RollingFileSink rolling( "app.log", 100 * 1024 * 1024 );
MemoryPoolAsyncSink<> async( rolling );       // default 256KB pool, 8192-entry queue
Logger< HighFreq >::bindSink( &async );

// option B: store raw records, batch-format on consumer (preferred for high throughput)
RollingFileSink rollingB( "app.log", 100 * 1024 * 1024 );
ISO8601Formatter formatter;
MemoryPoolAsyncBatchSink<> asyncBatch( rollingB, &formatter );  // formatting off hot path
Logger< HighFreq >::bindSink( &asyncBatch );
```

### Pattern 3: Multi-Threaded with Filtering

```cpp
OStreamSink console( std::cout );
ISO8601Formatter formatter;
FormattingSink<> formatted( console, formatter );

// only errors and fatals to console
auto errorFilter = []( const kmac::nova::Record& r ) noexcept {
	return r.tagId == kmac::nova::LoggerTraits< ErrorTag >::tagId
		|| r.tagId == kmac::nova::LoggerTraits< FatalTag >::tagId;
};
FilterSink< decltype( errorFilter ) > filtered( formatted, errorFilter );

// thread-safe
SynchronizedSink sync( filtered );

Logger< Shared >::bindSink( &sync );
```

### Pattern 4: Development vs Production

```cpp
#ifdef NDEBUG
    // production: batch-async + rolling with timestamps (formatting off hot path)
    ISO8601Formatter formatter;
    RollingFileSink rolling( "production.log", 50 * 1024 * 1024 );
    MemoryPoolAsyncBatchSink<> async( rolling, &formatter );
    Logger< App >::bindSink( &async );
#else
    // development: console with timestamp
    OStreamSink console( std::cout );
    ISO8601Formatter formatter;
    FormattingSink<> formatted( console, formatter );
    Logger< App >::bindSink( &formatted );
#endif
```

---

## Summary Decision Tree

**Need async logging?**
- → yes, need batch/formatted output → `MemoryPoolAsyncBatchSink`
- → yes, basic async delivery → `MemoryPoolAsyncSink`
- → no → Continue...

**Multiple threads?**
- → yes → `SynchronizedSink` or `SpinlockSink`
- → no → Continue...

**Multiple destinations?**
- → yes → `CompositeSink` (or bounded variant)
- → no → Continue...

**Need file rotation?**
- → yes → `RollingFileSink`
- → no → Continue...

**Need formatting?**
- → with timestamps → `FormattingSink + ISO8601Formatter`
- → custom format → `FormattingSink + JSONFormatter / CSVFormatter / CustomFormatter`
- → none → Continue...

**Basic output:**
- → console → `OStreamSink( std::cout )`
- → file → `OStreamSink( std::ofstream )`
- → nowhere → `NullSink` or null pointer for runtime disabling, `LoggerTraits<Tag>::enabled = false` for compile-time disabling

---

## Best Practices

1. **Compose sinks** - combine simple sinks for complex behavior
2. **Use synchronization** - wrap non-thread-safe sinks with SpinlockSink or SynchronizedSink
3. **Consider async** - MemoryPoolAsyncSink or MemoryPoolAsyncBatchSink for high-throughput
4. **Format once** - add formatting sink before composite (not after) if all composited sinks should log the same format
5. **Prefer batch async** - MemoryPoolAsyncBatchSink moves formatting off the hot path
6. **Monitor drops** - check droppedCount() on async sinks periodically
7. **Set rotation callbacks** - use RollingFileSink callbacks for cleanup

---

Each component is designed to be composable — combine simple sinks for complex logging pipelines.
