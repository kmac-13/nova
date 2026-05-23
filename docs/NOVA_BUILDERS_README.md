# Nova RecordBuilder Variants

Nova provides three RecordBuilder implementations to suit different use cases and requirements.

## Quick Comparison

| Feature | Truncating | Continuation | Streaming |
|---------|------------|--------------|-----------|
| **Heap Allocation** | ❌ None | ❌ None | ⚠️ Yes (multiple) |
| **Deterministic** | ✅ Yes | ✅ Yes | ❌ No |
| **Max Message Length** | Buffer size | Unlimited | Unlimited |
| **Records per Statement** | 1 | 1+ | 1 |
| **Data Loss** | Truncates | None | None |
| **Real-Time Safe** | ✅ Yes | ✅ Yes | ❌ No |
| **Crash Safe** | ✅ Yes | ✅ Yes | ❌ No |
| **Speed** | ⚠️ Fast | ⚠️ Fast | ❌ Slower |
| **Type Support** | Limited | Limited | Extensive |
| **Implementation** | Core | Extras | Extras |

---

## 1. TruncatingRecordBuilder (Core, default)

### Overview

**Location**: `kmac/nova/truncating_logging.h` (included transitively via `macros.h`)
**Macros**: `NOVA_LOG(Tag)`, `NOVA_LOG_BUF(Tag, Size)`, `NOVA_LOG_STACK(Tag)`, `NOVA_LOG_BUF_STACK(Tag, Size)`

TruncatingRecordBuilder is the default builder used in Nova core.  Uses a fixed-size stack buffer.  When the buffer fills, additional data is **silently truncated** with a "..." marker added to indicate data loss.

```cpp
NOVA_LOG(Audio) << "Short message";
// output: "Short message"

NOVA_LOG_BUF(Audio, 64) << "This is a very long message that exceeds the buffer";
// output: "This is a very long message that exceeds the bu..."
```

### Characteristics

- **Zero heap allocation**: completely deterministic memory usage
- **One record per statement**: simple, predictable behavior
- **Truncates on overflow**: data loss for very long messages
- **Fast**: no allocations, minimal overhead
- **Buffer size**: exactly `BufferSize` bytes (in TLS or on the stack depending on macro used)

### When to Use

✅ **Use when**:
- real-time performance is critical
- determinism is required
- most messages fit in buffer
- single-record emission is important
- crash safety is needed (Flare integration)

❌ **Avoid when**:
- message lengths are highly unpredictable, maximum length is unknown, or maximum length buffer size is unfeasible
- complete data preservation is critical
- truncation is unacceptable

### Example

```cpp
#include <kmac/nova.h>

struct Audio {};
NOVA_LOGGER_TRAITS(Audio, AUDIO, true, kmac::nova::TimestampHelper::steadyNanosecs);

// default buffer (1 KB)
NOVA_LOG(Audio) << "Processing frame " << frameId;

// custom buffer size
NOVA_LOG_BUF(Audio, 512) << "Quick status update";

// explicit stack allocation - use to support reentrant logging
NOVA_LOG_STACK(Audio) << "Stack-allocated log";

// wasTruncated() is available in TruncatingRecordBuilder<BufferSize> for advanced use
```

---

## 2. ContinuationRecordBuilder (Extras)

### Overview

**Location**: `kmac/nova/extras/continuation_logging.h`
**Macros**: `NOVA_LOG_CONT(Tag)`, `NOVA_LOG_CONT_BUF(Tag, Size)`, `NOVA_LOG_CONT_STACK(Tag)`, `NOVA_LOG_CONT_BUF_STACK(Tag, Size)`

Uses a fixed-size stack buffer.  When the buffer fills, it **emits the current record**, resets the buffer with a "[cont] " prefix, and continues building.

```cpp
NOVA_LOG_CONT_BUF(Diagnostics, 40) << "This is a very long message that requires multiple records";
// emits:
// log 1: "This is a very long message that re"
// log 2: "[cont] quires multiple records"
```

### Characteristics

- **Zero heap allocation**: completely deterministic memory usage
- **Multiple records for long messages**: automatic continuation
- **No data loss**: complete message preserved
- **Same timestamp**: all continuations share original timestamp
- **Stack usage**: exactly `BufferSize` bytes (regardless of message length)

### When to Use

✅ **Use when**:
- complete data is more important than single-record emission
- message lengths are unpredictable
- zero allocation is still required
- log analysis tools can handle continuations

⚠️ **Consider**:
- continuations may interleave in multi-threaded scenarios
- need tools to reassemble messages for analysis

### Example

```cpp
#include <kmac/nova.h>
#include <kmac/nova/extras/continuation_logging.h>

struct Diagnostics {};
NOVA_LOGGER_TRAITS(Diagnostics, DIAG, true, kmac::nova::TimestampHelper::steadyNanosecs);

// default 1 KB buffer with continuations
NOVA_LOG_CONT(Diagnostics) << generateLargeReport();

// larger buffer = fewer continuations
NOVA_LOG_CONT_BUF(Diagnostics, 16384) << generateVeryLargeReport();

// continuationCount() is available in ContinuationRecordBuilder<BufferSize> for advanced use
```

### Multi-Threading Considerations

In multi-threaded scenarios, continuations can interleave:

```
Thread 1: [Audio] Long message part 1
Thread 2: [Network] Packet received
Thread 1: [Audio] [cont] part 2
Thread 2: [Network] Processing
Thread 1: [Audio] [cont] part 3
```

**Solutions**:
- use `SynchronizedSink` to make emission atomic
- use larger buffers to reduce continuations

---

## 3. StreamingRecordBuilder (Extras)

### Overview

**Location**: `kmac/nova/extras/streaming_logging.h`
**Macro**: `NOVA_LOG_STREAM(Tag)`

Uses `std::ostringstream` for maximum flexibility.  Messages can be any length and support all types with `operator<<`.  No TLS involvement — the builder is a temporary object; data is heap-allocated via `std::ostringstream`.

```cpp
NOVA_LOG_STREAM(AppTag) << "Message: " << complexObject;
// works with any streamable type, any length
```

### Characteristics

- **Heap allocation**: uses std::ostringstream (non-deterministic)
- **Unlimited length**: no truncation or continuation
- **Extensive type support**: any type with `operator<<` defined for `std::ostream`
- **Simple to use**: familiar API
- **Slower**: allocation overhead
- **Not crash-safe**: heap may be corrupted during crashes

### When to Use

✅ **Use when**:
- convenience is more important than determinism
- application is not real-time or safety-critical
- working with complex types that have `operator<<`
- rapid development or prototyping

❌ **Avoid when**:
- real-time performance is required
- safety certification is needed
- crash logging (Flare)
- deterministic behavior is important

### Example

```cpp
#include <kmac/nova.h>
#include <kmac/nova/extras/streaming_logging.h>

struct Config {};
NOVA_LOGGER_TRAITS(Config, CONFIG, true, kmac::nova::TimestampHelper::steadyNanosecs);

// types with std::ostream operator<< work directly - no Nova-specific overloads needed
// (examples: std::filesystem::path, std::chrono::duration)
std::filesystem::path logDir = "/var/log/app";
NOVA_LOG_STREAM(Config) << "Log directory: " << logDir;

std::chrono::milliseconds elapsed{142};
NOVA_LOG_STREAM(Config) << "Elapsed: " << elapsed.count() << "ms";
```

**Note**: for common std types such as `std::vector`, `std::pair`, `std::map`, `std::optional`, and others, prefer `builder_stream_std.h` instead - it provides zero-allocation `operator<<` overloads for both `TruncatingRecordBuilder` and `ContinuationRecordBuilder`:

```cpp
#include <kmac/nova.h>
#include <kmac/nova/extras/builder_stream_std.h>

std::vector<int> data{1, 2, 3};
NOVA_LOG(Config) << "values: " << data;        // output: values: [1, 2, 3]

std::pair<int, int> coords{10, 20};
NOVA_LOG(Config) << "position: " << coords;    // output: position: (10, 20)
```

### ⚠️ Warning

```cpp
// DO NOT use in crash handlers!
void crashHandler(int signal) {
	// Heap may be corrupted - this could crash again!
	NOVA_LOG_STREAM(Emergency) << "Crash: " << signal;  // ❌ BAD

	// Use NOVA_LOG_STACK for crash/signal handler contexts:
	NOVA_LOG_STACK(Emergency) << "Crash: " << signal;   // ✅ GOOD
}
```

---

## Choosing the Right Builder

### Decision Tree

```
Is determinism/real-time safety required?
├─ Yes
│  └─ Can messages be truncated if extremely long?
│     ├─ Yes → use TruncatingRecordBuilder (NOVA_LOG)
│     └─ No  → use ContinuationRecordBuilder (NOVA_LOG_CONT)
└─ No
   └─ Is convenience more important than performance?
      ├─ Yes → use StreamingRecordBuilder (NOVA_LOG_STREAM)
      └─ No  → use TruncatingRecordBuilder anyway (it's fast!)
```

### By Use Case

**Real-Time Audio Processing**:
```cpp
NOVA_LOG_BUF(Audio, 512) << "Frame " << id;  // fast, small stack
```

**System Diagnostics**:
```cpp
NOVA_LOG_CONT_BUF(Diagnostics, 16384) << report;  // complete data, large buffer
```

**Crash/Signal Handler** - use Flare's macros, which are built on `NOVA_LOG_STACK*` and add async-signal-safe guarantees:
```cpp
// include <kmac/flare.h>
NOVA_FLARE_LOG(Emergency) << "Signal " << sig;
```

**Signal-handler-safe general logging** - when Flare is not available:
```cpp
NOVA_LOG_STACK(Emergency) << "Signal " << sig;  // always stack-based
```

**Development/Debugging**:
```cpp
NOVA_LOG_STREAM(Debug) << complexObject;  // convenient, flexible
```

---

## Macro Reference

### Core Macros (Truncating)

```cpp
// default buffer (respects NOVA_DEFAULT_BUFFER_SIZE_OVERRIDE)
NOVA_LOG(Tag)                     // TLS-backed (falls back to stack if NOVA_NO_TLS)
NOVA_LOG_BUF(Tag, Size)           // custom buffer size, TLS-backed

// always stack-allocated - use in signal handlers and bare-metal contexts
NOVA_LOG_STACK(Tag)               // default buffer, stack-only
NOVA_LOG_BUF_STACK(Tag, Size)     // custom buffer, stack-only

// convenience sizes (use default builder)
NOVA_LOG_SMALL(Tag)               // 512 bytes
NOVA_LOG_MEDIUM(Tag)              // 4 KB
NOVA_LOG_LARGE(Tag)               // 16 KB
NOVA_LOG_HUGE(Tag)                // 64 KB
```

### Extras Macros (Continuation)

```cpp
// include <kmac/nova/extras/continuation_logging.h>
NOVA_LOG_CONT(Tag)                  // default buffer, TLS-backed
NOVA_LOG_CONT_BUF(Tag, Size)        // custom buffer, TLS-backed
NOVA_LOG_CONT_STACK(Tag)            // default buffer, stack-only
NOVA_LOG_CONT_BUF_STACK(Tag, Size)  // custom buffer, stack-only
```

### Extras Macros (Streaming)

```cpp
// include <kmac/nova/extras/streaming_logging.h>
NOVA_LOG_STREAM(Tag)              // std::ostringstream (heap allocation)
```

---

## Configuration

### Changing Default Buffer Size

```cpp
// Before including Nova headers:
#define NOVA_DEFAULT_BUFFER_SIZE_OVERRIDE 8192
#include <kmac/nova.h>

// now all default macros use 8 KB buffer
NOVA_LOG(Tag) << "message";
```

### Project-Specific Macro Conventions

```cpp
// define project-specific shorthand macros at global scope
// (macros are preprocessor - they cannot be namespaced)
#define APP_LOG_FAST(Tag)  NOVA_LOG_BUF(Tag, 256)     // high-frequency, small buffer
#define APP_LOG_DIAG(Tag)  NOVA_LOG_CONT_BUF(Tag, 16384)  // diagnostic, full data

#ifdef APP_DEBUG
	#define APP_LOG_DEV(Tag) NOVA_LOG_STREAM(Tag)
#else
	#define APP_LOG_DEV(Tag) NOVA_LOG(Tag)
#endif
```

---

## Type Support

### Truncating & Continuation Builders

Built-in support for:
- `const char*`, `char`
- `int`, `unsigned int`, `long`, `unsigned long`
- `float`, `double`
- `bool`
- `void*` (formatted as hex)
- `std::string_view` (and `std::string` implicitly via cast)

**Standard library containers and types** - include `builder_stream_std.h` for zero-allocation `operator<<` support for `std::vector`, `std::array`, `std::list`, `std::map`, `std::unordered_map`, `std::set`, `std::unordered_set`, `std::pair`, and `std::optional`:

```cpp
#include <kmac/nova.h>
#include <kmac/nova/extras/builder_stream_std.h>

std::vector<int> values{1, 2, 3};
NOVA_LOG(AppTag) << "values: " << values;  // output: values: [1, 2, 3]

std::pair<int, int> p{10, 20};
NOVA_LOG(AppTag) << "pair: " << p;         // output: pair: (10, 20)
```

**Custom types** - define `operator<<` for both builder types alongside your type:

```cpp
namespace App {
	struct Point { int x; int y; };

	template< std::size_t N >
	kmac::nova::TruncatingRecordBuilder< N >& operator<<(
		kmac::nova::TruncatingRecordBuilder< N >& builder, const Point& p )
	{
		return builder << '(' << p.x << ", " << p.y << ')';
	}

	template< std::size_t N >
	kmac::nova::extras::ContinuationRecordBuilder< N >& operator<<(
		kmac::nova::extras::ContinuationRecordBuilder< N >& builder, const Point& p )
	{
		return builder << '(' << p.x << ", " << p.y << ')';
	}
}

// now usable with both builders:
NOVA_LOG(AppTag) << "position: " << point;
NOVA_LOG_CONT(AppTag) << "position: " << point;
```

### Streaming Builder

The streaming builder's primary advantage is working with any type that already has `operator<<` defined for `std::ostream` - types from the wider C++ ecosystem and third-party libraries - without needing Nova-specific overloads:

```cpp
// std::filesystem::path, std::chrono types, and other ostream-compatible types
// work directly without writing any Nova-specific operator<< overloads
NOVA_LOG_STREAM(AppTag) << "path: " << someFilesystemPath;
NOVA_LOG_STREAM(AppTag) << "elapsed: " << someDuration;
```

Note that `builder_stream_std.h` is generally preferable to the streaming builder for `std::vector`, `std::pair`, and other common std types, since it provides zero-allocation support via the truncating and continuation builders.

---

## Performance

For measured benchmark results covering synchronous and async delivery latency, see [docs/BENCHMARKS.md](BENCHMARKS.md).

General characteristics:
- **Truncating and Continuation builders**: same zero-heap per-character cost during buffer building; continuation emits additional records when the buffer fills
- **Streaming builder**: allocates via `std::ostringstream`; cost depends on message size and allocator

Continuation has the same per-call cost as truncating, but long messages require multiple sink calls.

---

## Best Practices

### 1. Choose Appropriate Buffer Sizes

```cpp
// TOO SMALL - will truncate/continue frequently
NOVA_LOG_BUF(Audio, 32) << "Processing frame " << frameId;

// GOOD - right-sized for typical message
NOVA_LOG_BUF(Audio, 256) << "Processing frame " << frameId;

// TOO LARGE - unnecessarily large buffer
NOVA_LOG_BUF(Audio, 65536) << "Done";
```

### 2. Use Correct Builder for Context

```cpp
// real-time audio callback
void audioCallback() {
	NOVA_LOG(Audio) << "Processing";       // ✅ deterministic
	// NOT: NOVA_LOG_STREAM(Audio) << ...; // ❌ allocates!
}

// diagnostic report generation
void generateReport() {
	NOVA_LOG_CONT(Diagnostics) << fullReport;  // ✅ complete data
}
```

### 3. TLS vs Stack-Based Builders

Nova provides two storage strategies for the log buffer, with different trade-offs:

**TLS builders** (`NOVA_LOG`, `NOVA_LOG_BUF`, `NOVA_LOG_CONT`, `NOVA_LOG_CONT_BUF`):
- buffer lives in thread-local storage, allocated once per thread and reused across calls
- no per-call stack cost - safe to use in deeply recursive functions
- when `NOVA_NO_TLS` is defined (bare-metal, some RTOS targets), `NOVA_LOG` falls back to stack-based automatically
- **not re-entrant**: if a TLS log call is in progress when another begins on the same thread - for example from a signal handler or from a sink that calls back into the logging system - the nested call is detected via a `_busy` flag:
  - **debug builds**: `NOVA_ASSERT` fires with note about nested logging detection
  - **release builds**: the nested call is silently dropped

**Stack builders** (`NOVA_LOG_STACK`, `NOVA_LOG_BUF_STACK`, `NOVA_LOG_CONT_STACK`, `NOVA_LOG_CONT_BUF_STACK`):
- allocate `BufferSize` bytes per call frame — in the worst case (e.g. deep recursion or signal interrupts) each active call holds its own buffer simultaneously
- **re-entrant safe**: each call has its own independent buffer
- correct choice for signal handlers and any context where nested logging may occur

```cpp
// TLS builder - no per-call stack cost, safe for deep recursion
void recursiveProcess(int depth) {
	NOVA_LOG(Debug) << "Depth: " << depth;  // TLS buffer, reused each call
	if (depth > 0) {
		recursiveProcess(depth - 1);
	}
}

// stack builder - re-entrant safe, but adds BufferSize bytes per call frame
void signalHandler(int sig) {
	NOVA_LOG_STACK(Emergency) << "Signal: " << sig;  // safe: own buffer per call
}

// avoid: NOVA_LOG_STACK in deep recursion
void deepRecursion(int depth) {
	NOVA_LOG_STACK(Debug) << "Depth: " << depth;  // ⚠️ 1 KB per call frame
	if (depth < 1000) {
		deepRecursion(depth - 1);                // ⚠️ ~1 MB total stack
	}
}
```

---

## Migration Guide

### From Streaming to Truncation/Continuation Builders

```cpp
// old code:
NOVA_LOG_STREAM(AppTag) << "Value: " << complexObject;

// new code:
NOVA_LOG(AppTag) << "Value: " << simpleValue;
// note: for custom types, define operator<< for the builder — see Type Support above
```

### From Other Logging Libraries

```cpp
// spdlog / Boost.Log:
logger->info("Value: {}", value);

// Nova (streaming - easiest migration):
NOVA_LOG_STREAM(Info) << "Value: " << value;

// Nova (core - best performance):
NOVA_LOG(Info) << "Value: " << value;
```

---

## Recommendations Summary

**Default choice**: `NOVA_LOG(Tag)`
- fast, deterministic, suitable for 99% of cases
- 1 KB buffer handles most messages
- explicit about trade-offs

**For unpredictable message sizes**: `NOVA_LOG_CONT(Tag)`
- no data loss
- still deterministic and crash-safe
- accept multi-record emission

**Only when necessary**: `NOVA_LOG_STREAM(Tag)`
- rapid development
- complex type formatting
- non-critical code paths
- accept heap allocation cost

When in doubt, start with `NOVA_LOG` and switch to `NOVA_LOG_CONT` only if truncation becomes a problem.
