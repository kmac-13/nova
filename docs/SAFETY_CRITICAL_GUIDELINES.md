# Safety-Critical Systems Guidelines for Nova/Flare

**Audience**: Engineers integrating Nova/Flare into systems requiring certification under DO-178C, IEC 61508, ISO 26262, IEC 62304, or similar standards.

> These are guidelines, not requirements.  Always follow your applicable standard and consult your safety authority.

---

## Table of Contents

1. [Key Principles](#key-principles)
2. [Feature Qualification Matrix](#feature-qualification-matrix)
3. [Standard Mapping](#standard-mapping)
4. [Recommended Subsets](#recommended-subsets)
5. [Implementation Notes](#implementation-notes)
6. [Example Configurations](#example-configurations)

---

## Key Principles

**Logging failures should never cause system failures.**  Design your logging architecture so that:

- logging errors are contained and do not propagate to system functions
- critical functions continue if logging fails or is disabled
- logging does not interfere with timing requirements
- resource exhaustion in logging is bounded and handled gracefully

---

## Feature Qualification Matrix

Components are grouped by their suitability for safety-certified code paths.

### All Configurations

Safe to use in any safety-critical context.  Zero heap allocation, deterministic execution, no OS dependencies beyond what the target already requires.

| Component | Notes |
|-----------|-------|
| **Nova core** | Tags, `LoggerTraits`, `Logger<Tag>`, `bindSink()`/`unbindSink()` |
| **`NOVA_LOG`**, **`NOVA_LOG_BUF`**, **`NOVA_LOG_STACK`**, **`NOVA_LOG_BUF_STACK`** | Stack-based macros; `NOVA_LOG`/`NOVA_LOG_BUF` use TLS (falls back to stack if `NOVA_NO_TLS`) |
| **`ScopedConfigurator<MaxBindings = 16>`** | Stack-allocated internal array; reduce `MaxBindings` if stack is constrained |
| **Nova Extras: `NullSink`** | No-op sink for runtime disablement (compile-time disablement is done via `enabled = false` in `LoggerTraits`) |
| **Nova Extras: `HierarchicalTag`**, **`severities.h`** | Zero-cost tag composition |
| **Nova Extras: `ContinuationRecordBuilder`** | Stack-based, no heap |
| **Nova Extras: `SpinlockSink`** | Bounded, no kernel calls - preferred for hard real-time |
| **Nova Extras: `BoundedCompositeSink`** | Fixed-capacity fan-out, no heap |
| **Flare: `EmergencySink`** | Stack buffer, no heap |
| **Flare: `RamWriter`** | Fixed RAM buffer, no OS dependency |
| **Flare: `UartWriter`** | User-supplied callback, no OS dependency |
| **Flare: `Scanner`**, **`Reader`**, **`TLV encoding`** | No heap, suitable for post-mortem analysis |

### Hosted / Non-Hard-RT Contexts

Acceptable where bounded non-determinism is tolerable (e.g. diagnostic paths, non-safety partitions, or where timing budgets are generous).

| Component | Constraint |
|-----------|------------|
| **Nova Extras: `SynchronizedSink`** | Mutex operations have bounded but non-constant latency |
| **Nova Extras: `OStreamSink`** | Determinism depends on underlying `std::ostream` implementation |
| **Nova Extras: `FormattingSink`** | Formatting uses a stack buffer (deterministic); downstream sink's determinism depends on the wrapped sink |
| **Nova Extras: `FormattingFileSink`** | File I/O determinism is platform-dependent |
| **Nova Extras: `FilterSink`** | Determinism depends on filter function complexity |
| **Nova Extras: `MemoryPoolAsyncSink`**, **`MemoryPoolAsyncBatchSink`** | Pool pre-allocated at construction - zero runtime allocation; producer call is bounded but consumer thread scheduling is not (see note below) |
| **Flare: `FdWriter`** | `write()`/`fsync()` syscall latency is bounded but not constant |
| **Flare: `FileWriter`** | `fwrite()`/`fflush()` not async-signal-safe; not suitable for bare-metal |

> **`MemoryPoolAsyncSink` note**: The producer-side enqueue call is bounded and lock-free - safe to call from real-time contexts.  The consumer thread scheduling is non-deterministic.  Monitor `droppedCount()` to detect pool exhaustion.

### Avoid in Certified Paths

These components use heap allocation, unbounded I/O, or non-deterministic operations.  Acceptable only in uncertified diagnostic partitions.

| Component | Reason |
|-----------|--------|
| **Nova Extras: `CompositeSink`** | Heap allocation (`std::vector` internally) |
| **Nova Extras: `RollingFileSink`** | Unbounded file I/O, heap allocation |
| **Nova Extras: `StreamingRecordBuilder`** | Heap allocation via `std::ostringstream` |
| **Nova Extras: `MultilineFormatter`**, **`LargePayloadFormatter`** | Heap allocation |

---

## Standard Mapping

The three categories above correspond roughly to the following safety levels.  Actual applicability depends on your system architecture, safety goals, and the judgment of your safety authority.

| Standard | "All Configurations" | "Hosted / Non-Hard-RT" | "Avoid in Certified Paths" |
|----------|---------------------|------------------------|---------------------------|
| **DO-178C** | Levels A–E | Levels C–E | Level E (or excluded) |
| **IEC 61508** | SIL 1–4 | SIL 1–2 | Not recommended for SIL paths |
| **ISO 26262** | ASIL A–D | ASIL A–B | QM / non-safety partitions only |
| **IEC 62304** | Class A–C | Class A–B | Class A or excluded |

---

## Recommended Subsets

### Maximum Restriction (e.g. DO-178C Level A, IEC 61508 SIL 4, ISO 26262 ASIL D)

Use only components from the **All Configurations** category above.  A null (unbound) sink is also acceptable where logging is intentionally disabled.

- static sink binding (`Logger<Tag>::bindSink()`) rather than `ScopedConfigurator`
- `RamWriter` or `UartWriter` for Flare (bare-metal) or a custom `IWriter` using raw syscalls (hosted)
- no dynamic reconfiguration after initialization

### Moderate Restriction (e.g. DO-178C Level B/C, IEC 61508 SIL 2/3, ISO 26262 ASIL B/C)

All "All Configurations" components plus the "Hosted / Non-Hard-RT" components, subject to timing budget verification.

- `ScopedConfigurator` acceptable; size `MaxBindings` to your needs
- `SpinlockSink` preferred over `SynchronizedSink` for lower jitter
- `MemoryPoolAsyncSink` acceptable for non-hard-RT producer call sites

### Minimal Restriction (e.g. DO-178C Level D/E, ISO 26262 ASIL A / QM)

All components available for diagnostic use.  Follow your project's coding standards.

---

## Implementation Notes

### Memory

Pre-allocate everything at initialization.  No heap allocation should occur on any certified path after startup.

```cpp
// static storage - initialized once at startup
static kmac::flare::RamWriter _ramWriter( _buffer, sizeof( _buffer ) );
static kmac::flare::EmergencySink<> _sink( &_ramWriter );

void initLogging() noexcept
{
	kmac::nova::Logger< CriticalTag >::bindSink( &_sink );
}
```

### Timing

Measure worst-case execution time for your logging path under your target conditions.  Key factors:
- buffer size (affects copy cost)
- sink type (`SpinlockSink` < `SynchronizedSink` < file I/O)
- formatting complexity

### Error Containment

`Sink::process()` is `noexcept` by interface contract.  Ensure your sink implementations honour this - do not let exceptions escape.  If a downstream sink can throw (e.g. a legacy library), wrap it:

```cpp
void process( const kmac::nova::Record& record ) noexcept override
{
	try { downstream_->process( record ); }
	catch ( ... ) { /* count error, continue */ }
}
```

### TLS Builders

`NOVA_LOG` and `NOVA_LOG_BUF` use thread-local storage by default.  On targets without TLS (`NOVA_NO_TLS`), they fall back to stack allocation automatically.  Use `NOVA_LOG_STACK` / `NOVA_LOG_BUF_STACK` explicitly when re-entrancy is required (e.g. signal handlers, fault handlers, logging from a function being called from a logging statement).

---

## Example Configurations

### DO-178C Level A / IEC 61508 SIL 4 - Bare-Metal Emergency Logging

```cpp
#include <kmac/nova.h>
#include <kmac/flare.h>

struct CriticalErrorTag {};
NOVA_LOGGER_TRAITS( CriticalErrorTag, CRITICAL, true, /* hardware timer */ );

// place in .noinit section to survive soft reset; drain on next boot
static std::uint8_t _nvramBuffer[ 2048 ] __attribute__( ( section( ".noinit" ) ) );
static kmac::flare::RamWriter _ramWriter( _nvramBuffer, sizeof( _nvramBuffer ) );
static kmac::flare::EmergencySink<> _emergencySink( &_ramWriter );

void initLogging() noexcept
{
	kmac::nova::Logger< CriticalErrorTag >::bindSink( &_emergencySink );
}
```

### ISO 26262 ASIL B - Hosted, Multi-Subsystem with Async Delivery

```cpp
#include <kmac/nova.h>
#include <kmac/nova/extras/formatting_sink.h>
#include <kmac/nova/extras/iso8601_formatter.h>
#include <kmac/nova/extras/ostream_sink.h>
#include <kmac/nova/extras/spinlock_sink.h>

struct PowertrainTag {};
struct BrakingTag {};
struct SteeringTag {};

NOVA_LOGGER_TRAITS( PowertrainTag, POWERTRAIN, true, kmac::nova::TimestampHelper::steadyNanosecs );
NOVA_LOGGER_TRAITS( BrakingTag, BRAKING, true, kmac::nova::TimestampHelper::steadyNanosecs );
NOVA_LOGGER_TRAITS( SteeringTag, STEERING, true, kmac::nova::TimestampHelper::steadyNanosecs );

static std::ofstream _logFile( "/var/log/vehicle.log" );
static kmac::nova::extras::OStreamSink _fileSink( _logFile );
static kmac::nova::extras::ISO8601Formatter _formatter;
static kmac::nova::extras::FormattingSink<> _formattingSink( _fileSink, _formatter );
static kmac::nova::extras::SpinlockSink _threadSafeSink( _formattingSink );

void initLogging() noexcept
{
	kmac::nova::Logger< PowertrainTag >::bindSink( &_threadSafeSink );
	kmac::nova::Logger< BrakingTag >::bindSink( &_threadSafeSink );
	kmac::nova::Logger< SteeringTag >::bindSink( &_threadSafeSink );
}
```
