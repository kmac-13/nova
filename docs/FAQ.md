# Nova/Flare Frequently Asked Questions

---

## General

### Is Nova only for safety-critical systems?

No.  Nova was designed with safety-critical and embedded systems in mind, but it works well for any project where explicit, predictable logging behavior is valued.  Its zero-heap allocation, compile-time routing, and RAII sink binding make it a good choice for performance-sensitive applications, embedded systems, real-time audio and video, games, and general hosted applications alike.

The main trade-off compared to libraries like spdlog is that Nova requires a small amount of explicit setup (defining tags, binding sinks) rather than providing implicit global defaults.  If you prefer that setup over hidden global state and runtime string lookups, Nova is worth considering regardless of whether your system requires certification.

---

### Why do I need to define a struct just to log a message?

The struct *is* the domain.  Nova uses C++ types - not strings, enums, or integer IDs - to identify logging domains at compile time.  This means:

- routing is resolved by the compiler, not at runtime
- disabled domains have zero binary footprint (in optimised builds)
- two domains with the same name in different namespaces are genuinely different domains

In practice this is two lines:

```cpp
struct AudioDebug {};
NOVA_LOGGER_TRAITS( AudioDebug, AUDIODEBUG, true, kmac::nova::TimestampHelper::steadyNanosecs );
```

For severity-based logging without domain tags, `severities.h` in Nova Extras provides pre-defined tags (`InfoTag`, `ErrorTag`, etc.) and convenience macros (`NOVA_LOG_INFO()`, `NOVA_LOG_ERROR()`) so you can start logging without defining any types yourself.

Additionally, you do not need to define a new struct for every domain.  If you want to log activity in an existing class, that class can serve as the domain - you only need to define logger traits for it and bind a sink.

---

### What happens if I forget to bind a sink?

Nothing.  If no sink is bound to a tag, `Logger<Tag>` does nothing when a log statement is reached.  There is no fallback, no warning, and no error.  The log statement is silently discarded.

This is intentional.  Nova has no hidden global defaults or inherited fallback sinks.  If a domain has no sink, it is effectively disabled at runtime.

During development, `QuickStart` (from Nova Extras) can be used to easily wire up console output for all severity tags with a single declaration, making it easy to see output without explicit configuration.

---

### Can I use Nova in a library without affecting the application's logging?

Yes - this follows naturally from Nova's architecture.  Because logging configuration is explicit and scoped, a library can define its own tags and leave them unbound.  The application then decides whether to bind a sink to those tags or leave them silent.

A library should:
- define its own tag types in its own namespace
- never bind sinks itself (leave that to the application)
- document which tags it uses so the application can opt in

```cpp
// in library header
namespace acme {
	struct DiagTag {};
}
NOVA_LOGGER_TRAITS( acme::DiagTag, ACME_DIAG, true, kmac::nova::TimestampHelper::steadyNanosecs );

// in library code
NOVA_LOG( acme::DiagTag ) << "Internal diagnostic";  // silent unless app binds a sink

// in application code - opt in explicitly
kmac::nova::Logger< acme::DiagTag >::bindSink( &consoleSink );
```

There is no global logger registry for a library to accidentally pollute.

---

## Runtime Control

### Can I change the sink at runtime without recompiling?

Yes.  Sink binding is a runtime operation.  You can rebind a tag to a different sink, unbind it entirely, or swap in a `NullSink` at any point:

```cpp
// swap to a file sink at runtime
kmac::nova::Logger< AudioDebug >::bindSink( &fileSink );

// silence a tag at runtime
kmac::nova::Logger< AudioDebug >::unbindSink();

// or bind to a NullSink to keep the binding infrastructure intact
kmac::nova::Logger< AudioDebug >::bindSink( &kmac::nova::extras::NullSink::instance() );
```

Compile-time enablement (`enabled = false` in `LoggerTraits`) is a separate, recompile-required mechanism that eliminates all logging code for a tag, including argument evaluation.

---

### How do I dynamically enable/disable verbose logging at runtime?

Bind or unbind the sink for a verbose tag:

```cpp
struct VerboseAudio {};
NOVA_LOGGER_TRAITS( VerboseAudio, VERBOSE_AUDIO, true, kmac::nova::TimestampHelper::steadyNanosecs );  // always enabled

// enable verbose logging
kmac::nova::Logger< VerboseAudio >::bindSink( &consoleSink );  // runtime enabled

// disable verbose logging
kmac::nova::Logger< VerboseAudio >::unbindSink();  // runtime disabled
```

For severity-based filtering, use `FilterSink` to route only records above a threshold, or bind/unbind individual severity tags from `severities.h`:

```cpp
// enable debug and above - bind each severity tag you want
kmac::nova::Logger< kmac::nova::extras::DebugTag >::bindSink( &sink );
kmac::nova::Logger< kmac::nova::extras::InfoTag >::bindSink( &sink );

// disable debug - unbind it
kmac::nova::Logger< kmac::nova::extras::DebugTag >::unbindSink();
```

---

### How do I filter logs by severity?

Nova does not have a built-in severity hierarchy - severity is just one way to use tags, not a core concept.  There are several approaches:

**Option 1 - bind only the severity tags you want** (simplest):
```cpp
// using pre-defined severity tags in Nova Extras, bind only warnings and errors; info and debug are unbound (silent)
kmac::nova::Logger< kmac::nova::extras::WarningTag >::bindSink( &sink );
kmac::nova::Logger< kmac::nova::extras::ErrorTag >::bindSink( &sink );
```

**Option 2 - `FilterSink` for runtime predicate filtering**:
```cpp
auto severityFilter = []( const kmac::nova::Record& r ) noexcept {
	return r.tagId == kmac::nova::LoggerTraits< kmac::nova::extras::ErrorTag >::tagId
		|| r.tagId == kmac::nova::LoggerTraits< kmac::nova::extras::FatalTag >::tagId;
};
kmac::nova::extras::FilterSink< decltype( severityFilter ) > errorsOnly( sink, severityFilter );
```

**Option 3 - compile-time disable** via `NOVA_LOGGER_TRAITS` with `enabled = false` (zero binary footprint, requires recompile).

---

## Thread Safety

### Is Nova thread-safe?

Nova's `Logger<Tag>` routing is thread-safe - the sink pointer is stored atomically, so concurrent log calls and sink rebinding are safe.

Thread-safe *processing* of records is the sink's responsibility.  Nova Extras provides:
- `SynchronizedSink` - mutex-based, general purpose
- `SpinlockSink` - spinlock, lower latency under low contention
- `MemoryPoolAsyncSink` - lock-free producer enqueue, async consumer thread

TLS builders (`NOVA_LOG`, `NOVA_LOG_BUF`) are not re-entrant within a single thread - a nested log call on the same thread while a TLS log is in progress will assert in debug builds and silently drop in release builds.  Use `NOVA_LOG_STACK` / `NOVA_LOG_BUF_STACK` for re-entrant contexts such as signal handlers or when a function used as a log argument itself calls into the logging system.

---

## Platform Support

### Does Nova work on Windows, Android, and bare-metal?

Yes.  Nova core targets C++11 and above with no OS dependencies.  Some Nova Extras components require a hosted environment (e.g. `OStreamSink`, `FormattingSink`, `RollingFileSink`); others work on bare-metal and RTOS targets (e.g. `SpinlockSink`, `NullSink`, `HierarchicalTag`, `ContinuationRecordBuilder`).  Flare has POSIX and bare-metal variants.

| Target | Nova core | Nova Extras | Flare |
|--------|-----------|-------------|-------|
| Linux / macOS | ✅ | ✅ | ✅ `FdWriter`, `SignalHandler<>` |
| Windows (MSVC / MinGW) | ✅ | ✅ | ⚠️ `FileWriter` only (no POSIX signal handler) |
| Android | ✅ | ✅ | ✅ `FdWriter`, `AndroidLogSink` in Extras |
| RTOS (FreeRTOS etc.) | ✅ | ⚠️ subset | ✅ `RamWriter`, `UartWriter` |
| Bare-metal (Cortex-M, RISC-V) | ✅ (`NOVA_BARE_METAL`) | ⚠️ subset | ✅ `BareMetalFaultHandler`, `RamWriter` |

See `BARE_METAL_GUIDE.md` for bare-metal and RTOS porting details.

---

### Do I have to use all of Nova Extras?

No - Nova Extras is a collection of independent components.  Include only the headers and sources you need; each component has its own header and no component forces you to use another.

```cpp
// use only what you need
#include <kmac/nova/extras/iso8601_formatter.h>    // ISO8601Formatter
#include <kmac/nova/extras/ostream_sink.h>         // OStreamSink
#include <kmac/nova/extras/synchronized_sink.h>    // SynchronizedSink
// no need to include formatters, async sinks, or builders you are not using
```

Nova Extras is also split across two compilation modes - some components are header-only, others require a `.cpp` to be compiled.

---

## Flare

### Can I use Flare in an application that already uses spdlog (or another logger)?

Yes.  Flare integrates through Nova's sink interface, which is independent of spdlog or any other logging library.  Your application can continue using spdlog for operational logging while Flare handles crash/forensic records on a separate Nova tag:

```cpp
#include <kmac/nova.h>
#include <kmac/flare.h>
#include <spdlog/spdlog.h>

// define a tag used only for crash logging
struct CrashTag {};
NOVA_LOGGER_TRAITS( CrashTag, CRASH, true, kmac::nova::TimestampHelper::steadyNanosecs );

// static Flare storage
static int _flareFd = open( "/var/log/crash.flare", O_WRONLY | O_CREAT | O_APPEND, 0644 );
static kmac::flare::FdWriter _flareWriter( _flareFd );
static kmac::flare::EmergencySink<> _flareSink( &_flareWriter );

void setup()
{
	// spdlog continues handling operational logging as before
	spdlog::info( "Application starting" );

	// Flare is wired up independently on a dedicated tag
	kmac::nova::Logger< CrashTag >::bindSink( &_flareSink );
	kmac::flare::SignalHandler<>::install( &_flareSink );
}

void onCrash( int sig, siginfo_t*, void* )
{
	NOVA_FLARE_LOG( CrashTag ) << "Signal " << sig;
	_Exit( 128 + sig );
}
```

spdlog and Flare operate entirely independently - Flare has no knowledge of spdlog's state, and spdlog has no knowledge of Flare.

---

### Can I use Flare alongside Nova logging in the same application?

Yes, and this is the typical pattern.  Flare integrates as a Nova sink - route a crash-specific tag to `EmergencySink` and route other tags to your normal sinks:

```cpp
// operational logging - goes to console/file via normal Nova sinks
kmac::nova::Logger< AudioTag >::bindSink( &consoleSink );
kmac::nova::Logger< NetworkTag >::bindSink( &fileSink );

// crash logging - goes to Flare on a dedicated tag
kmac::nova::Logger< CrashTag >::bindSink( &_flareSink );

// or route the same tag to both for operational + crash-resilient coverage
kmac::nova::Sink* sinks[] = { &consoleSink, &_flareSink };
kmac::nova::extras::FixedCompositeSink composite( sinks, 2 );
kmac::nova::Logger< CrashTag >::bindSink( &composite );

// use NOVA_FLARE_LOG( CrashTag ) to log crash events safely from signal handlers
```

See `FLARE_README.md` and `FLARE_USE_CASES.md` for detailed setup and patterns.
