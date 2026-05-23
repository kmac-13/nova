# Nova

## Introduction

Nova is a C++ logging core designed for systems where predictability, auditability, and compile-time clarity matter.  It provides a minimal, dependency-free foundation for type-driven logging using type-based routing instead of runtime strings, enums, or global registries.  Nova records log data deterministically at the point of emission and delegates formatting, transport, and policy decisions to sinks layered on top of it.

Nova suits developers who are comfortable with a small amount of explicit setup in exchange for fast, flexible, and predictable logging behavior.  For systems that require post-mortem or crash-resilient logging, Nova integrates naturally with Flare, a separate forensic logging library built on top of Nova's sink interface.

---

## Quick Example

```cpp
#include <kmac/nova.h>
#include <kmac/nova/extras/ostream_sink.h>

namespace nova = kmac::nova;
namespace extras = kmac::nova::extras;

// 1. define a domain
struct AudioDebug {};

// 2. define traits - name, enabled, clock
NOVA_LOGGER_TRAITS( AudioDebug, AUDIODEBUG, true, nova::TimestampHelper::steadyNanosecs );

int main()
{
	// 3. create a sink
	extras::OStreamSink consoleSink( std::cout );

	// 4. bind the sink (RAII - unbound on destruction)
	nova::ScopedConfigurator config;
	config.bind< AudioDebug >( &consoleSink );

	// 5. log
	NOVA_LOG( AudioDebug ) << "Audio buffer underrun, dropped=" << 3;

	return 0;
}
```

---

## Design Goals

- **Zero-cost when disabled**
  - compile-time opt-out using traits
  - no runtime branching when logging is disabled

- **No global registries**
  - no string-based loggers
  - no name lookups
  - no hidden global state

- **Deterministic behavior**
  - no dynamic allocation in the core
  - predictable call paths

- **Separation of concerns**
  - Nova emits records
  - sinks process records
  - formatting is optional and handled by sinks or formatters layered above Nova

- **Composable**
  - multiple sinks
  - runtime binding and unbinding
  - explicit lifetimes

---

## Core Concepts

### Design for Review and Audit

Nova is intentionally designed to make logging behavior explicit, reviewable, and auditable:
- log routing is determined at compile time through types, not strings or runtime registries
- each tagged logger has an explicit sink binding or is fully disabled
- no implicit global fallbacks or hidden inheritance paths exist
- logging configuration can be expressed through scoped, RAII-based binding
- emission paths are deterministic and free of allocation by default

These properties make it feasible to reason about logging behavior during code review, static analysis, and formal audits.

---

### Domains

A **domain** is a user-defined type that represents the context for which logging should be performed.  In Nova, **tags** are the concrete types that represent domains, and they are C++ types resolved entirely at compile time.  Because routing is type-based, logging routes can be fully inspected during code review without runtime instrumentation or configuration tracing.

Domains can represent:
- severity levels (e.g. `Debug`, `Info`)
- subsystems (e.g. `Audio`, `Networking`)
- combinations (e.g. `AudioDebug`)
- any user-defined context

Nova does **not** use strings, enums, or runtime dispatch for routing.

Example:

```cpp
// a tag is any user-defined type - no members required
struct AudioTag {};       // domain
struct DebugTag {};       // severity
struct AudioDebugTag {};  // domain with severity
```

### Record

`nova::Record` is a lightweight, immutable data structure representing a single log event.  It contains the log payload along with metadata captured at the time the record is created, such as:
- tag identifier
- timestamp (user-defined precision - the unit is whatever the clock function returns)
- payload size

Once constructed, a `Record` is passed through the logging pipeline.  Most sinks read records without modification.  Some sinks - such as `FormattingSink` - produce a new `Record` containing formatted output and pass that to a downstream sink, rather than passing the original record unchanged.

Nova does not interpret records, it only emits them.

---

### Timestamps and Clocks

Nova captures timestamps when a log record is created, not when it is written or formatted.  This ensures that timing information reflects the moment of logging rather than downstream processing delays.

Nova provides `TimestampHelper::steadyNanosecs` and `TimestampHelper::systemNanosecs` as commonly used clock functions.  Clock selection is a compile-time customization point specified as the fourth argument to `NOVA_LOGGER_TRAITS`, or by specializing `kmac::nova::LoggerTraits<Tag>` directly - there is no library-wide default.

Nova does not interpret timestamp values.  Their meaning and presentation are entirely the responsibility of sinks and formatters.

---

### Sink

A `nova::Sink` is responsible for consuming records.  This may include:
- writing to a stream, file, network, etc
- forwarding to another logging system or external destination (e.g. network, database)
- buffering
- formatting
- fan-out to other sinks

```cpp
// namespace nova = kmac::nova;
namespace nova
{
	class Sink
	{
	public:
		virtual ~Sink() = default;
		virtual void process( const Record& record ) noexcept = 0;
	};
}
```

Sinks:
- may be stateful
- must outlive any loggers bound to them
- are explicitly managed by the user

---

### Logger

A `nova::Logger<Tag>` represents a logging endpoint for a specific tag.  A logger:
- routes records to its currently bound sink
- emits nothing if no sink is bound
- does not own sinks
- contains minimal state (a sink pointer)

```cpp
// examples use: namespace nova = kmac::nova;

// option 1: streaming builder - the recommended API
NOVA_LOG( AudioDebug ) << "Audio buffer underrun";

// option 2: direct call with source location - lowest overhead, used in benchmarks
nova::Logger< AudioDebug >::log( __FILE__, __func__, __LINE__, "Audio buffer underrun" );

// option 3: log an already-constructed Record
nova::Logger< AudioDebug >::log( record );
```

Key properties:
- a unique Logger is associated with each tag
- there is no global fallback
- a null sink means logging is disabled at runtime

---

### Logger Traits

In addition to routing and behavior customization, `LoggerTraits` defines the timestamp source used for a given tag.  This allows different parts of a system to use different clocks (e.g., monotonic, wall-clock, or platform-specific timers) without introducing runtime configuration or global state.

---

### Compile-Time Enablement

Compile-time enablement is controlled via `LoggerTraits`.  The recommended approach is the `NOVA_LOGGER_TRAITS` macro:

```cpp
NOVA_LOGGER_TRAITS( AudioDebug, AUDIODEBUG, true, kmac::nova::TimestampHelper::steadyNanosecs );
```

Manual specialization is also valid for advanced use, though it requires `tagId`, `name`, `enabled`, and `timestamp` fields plus collision detection boilerplate - using `NOVA_LOGGER_TRAITS` is strongly recommended:

```cpp
template <>
struct kmac::nova::LoggerTraits< AudioDebug >
{
	static constexpr bool enabled = true;
	static std::uint64_t timestamp() noexcept
	{
		return kmac::nova::TimestampHelper::steadyNanosecs();
	}
};
```

If `enabled == false`:
- logging code is compiled out (relies on compiler optimization in C++11/14, language guarantee in C++17+)
- no runtime cost remains

This is independent of runtime enablement.

---

### Runtime Enablement

Runtime enablement is controlled by binding or unbinding sinks.
- bound sink -> logging enabled
- null sink -> logging disabled

No fallback occurs.  If a logger has no sink, it does nothing.

---

### Streaming API

Nova supports streaming-style logging via macros that return a builder object.

```cpp
NOVA_LOG( AudioDebug ) << "Samples dropped: " << dropped;
```

The builder:
- accumulates data into a fixed-size stack buffer
- finalizes into a Record
- emits once at scope end

The Logger itself does not know about builders, so it does not expose builder APIs.

Nova core provides the truncating builder (`NOVA_LOG`, `NOVA_LOG_BUF`, `NOVA_LOG_STACK`).  Nova Extras adds the continuation builder (`NOVA_LOG_CONT`) for lossless long messages, the streaming builder (`NOVA_LOG_STREAM`) for heap-backed flexible formatting, and `builder_stream_std.h` for zero-allocation operator<< support for common std types.  See `NOVA_BUILDERS_README.md` for details.

---

### Scoped Configurator

`nova::ScopedConfigurator` is an RAII helper for managing sink bindings.  It:
- binds sinks to loggers
- tracks which loggers it has modified
- automatically unbinds them on destruction

```cpp
// namespace nova = kmac::nova;
nova::ScopedConfigurator config;

config.bind< AudioDebug >( &sink );
config.bindFrom< OtherTag, AudioDebug >();
```

On destruction:
- all bound loggers are unbound
- no previous state is restored
- teardown order is deterministic

This makes shutdown safe and explicit.

Configuration in Nova is explicit and scoped.  Binding a sink to a tagged logger is a deliberate act, and unbinding is guaranteed when the configuring scope exits.  Nova does not restore or infer previous bindings, avoiding hidden configuration state.

---

### Thread Safety

Nova itself:
- routes records to sinks using an atomic pointer load - Logger routing is thread-safe
- does not impose synchronization on sink processing - that is the sink's responsibility

Nova does not lock internally.

Nova Extras provides several thread-safety options:
- `SynchronizedSink` - wraps any sink with a mutex
- `SpinlockSink` - wraps any sink with a spinlock (lower overhead for low-contention cases)
- `MemoryPoolAsyncSink` - async delivery via a lock-free memory pool queue

See `NOVA_EXTRAS_REFERENCE.md` for details.

---

## Relationship to Flare

Nova and Flare are **independent libraries** with a one-way dependency:
- **Flare depends on Nova**
- **Nova does not know about Flare**

Flare implements sinks that:
- persist records in a binary TLV format
- survive crashes
- support post-mortem analysis

From Nova’s perspective, Flare is just another sink.

---

## What Nova Is Not

Nova is intentionally not a general-purpose, feature-maximal logging framework.  Several common logging patterns are deliberately excluded to preserve determinism, auditability, and architectural clarity.

Nova does not provide:
- global logger registries or runtime lookup tables (instead: compile-time type-based routing)
- string-based categories or name-based routing (instead: user-defined tag types)
- implicit configuration inheritance or fallback behavior (instead: explicit `ScopedConfigurator` bindings or `Logger<Tag>::bindSink()` calls)
- automatic restoration of previous logging state (instead: RAII scoped binding with deterministic teardown)
- runtime configuration via config files (instead: explicit C++ code)
- runtime discovery of logging topology (there is no API to enumerate loggers or query which sinks are bound)

All logging behavior in Nova is established explicitly through compile-time tags and scoped configuration.  If a typed logger is not bound to a sink, it does not emit.  There are no hidden defaults, inferred routes, or implicit global behavior.

These exclusions are intentional.  While such features are common in general-purpose logging libraries, they make it difficult to reason about logging behavior during code review, static analysis, and formal audits - particularly in safety-critical or post-incident environments.

---

## When Nova Is a Good Fit (and When It Isn’t)
Nova is a good fit when:
- logging behavior must be explicit, deterministic, and reviewable
- log routing should be resolved at compile time, not via runtime string matching
- different parts of the system require independent logging control
- logging configuration needs to be scoped and reversible (RAII-style)
- auditability and post-incident analysis are important design considerations
- the system favors clear ownership of logging behavior over convenience defaults
- runtime configuration is not essential


Nova may not be a good fit when:
- logging configuration is expected to be global and implicit
- log routing must be driven by runtime strings, names, or external configuration files
- the application requires loggers to be dynamically registered or discovered by string name at runtime
- minimal integration effort is more important than architectural clarity
- the logging system is primarily used for ad-hoc debugging or quick instrumentation

In these cases, a more traditional logging framework may offer a lower barrier to entry.

---

## Summary

Nova is a minimal, deterministic logging core intended for systems that need predictable behavior and explicit control.  Nova favors compile-time structure and explicit data capture to support long-term maintainability and auditability in safety- and reliability-critical systems.

Nova provides:
- compile-time tagging
- zero-overhead disablement
- explicit runtime control
- deterministic behavior
- clean separation of responsibilities

It is designed to be small, composable, and dependable - especially in systems where traditional logging frameworks are too heavy or unpredictable.
