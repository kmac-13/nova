# Nova/Flare Logging Ecosystem

A modern domain-based C++ logging framework emphasizing compile-time routing, explicit configuration, and zero-cost abstractions, designed for safety-critical, real-time, and embedded systems where deterministic behavior and performance are paramount.

[![License](https://img.shields.io/badge/license-BSD--3-blue.svg)](LICENSE)
[![C++11+](https://img.shields.io/badge/C%2B%2B-11%2B-blue.svg)](https://en.cppreference.com/w/cpp/11)

## Overview

The Nova/Flare ecosystem consists of three complementary libraries:

- **Nova** - core compile-time log routing with type-safe tags representing logging domains
- **Nova Extras** - optional sinks, formatters, and utilities
- **Flare** - emergency forensic logging for crash scenarios

### Why Nova?

Traditional logging frameworks organize logs by severity levels (DEBUG, INFO, WARN, ERROR), often offering the ability to disable logging based on severity threshold.

Nova recognizes that **not all logging needs to be categorized by severities** - sometimes it's more natural or even convenient to log against **subsystems, modules, or functional areas**.

```cpp
// Traditional approach - everything is severity-based
LOG_INFO("Network") << "Connection established";
LOG_DEBUG("Database") << "Query executed";
LOG_ERROR("Renderer") << "Texture load failed";

// Nova approach - log by domain (subsystem, module, etc) directly
NOVA_LOG(NetworkTag) << "Connection established";
NOVA_LOG(DatabaseTag) << "Query executed"; 
NOVA_LOG(RendererTag) << "Texture load failed";
```

Nova's distinguishing approach:

**Type-based routing instead of string lookups**
- logging domains are represented by tags, which are C++ types (e.g. struct NetworkTag {};), not runtime strings
- Logger<NetworkTag> resolved at compile time - no runtime map lookups or string comparisons
- disabled tags optimized to zero overhead in release builds; on C++17 this is a language guarantee via `if constexpr`, on C++11/14 it is optimiser-dependent

**Severity is optional, not mandatory**
- core Nova has no concept of DEBUG/INFO/ERROR levels
- tags can represent any domain: subsystems, components, execution phases, transaction types, and, yes, even severity level

With Nova:
- **Tags are compile-time types** - tag selection and enablement determined at compile time
- **Tags can represent anything** - severity, subsystem, feature, class, combinations, etc
- **No runtime overhead** - disabled tags compile to nothing
- **Explicit configuration** - no hidden global state or magic

Of course, if you prefer traditional severity-based logging, Nova supports that, too.  There are different ways to support this:
- **Pre-defined severity tags in `severities.h`** - if the Nova Extras `severities.h` meets the application's severity-level needs, include this header
- **Custom severity tags** - implement custom severity level tags needed for the application

Additionally, severities can be combined with other logging contexts:
- **Custom context+severity tags** - NetworkDebug tag or DebugNetwork tag
- **Combine subsystem+severity with HierarchicalTag** - implement subsystem and severity tags separately, combining them with HierarchicalTag<Network, Debug>

## Key Features

### Nova

- **Compile-Time Routing**: tags are types, selection determined at compile time
- **Zero-Cost Abstractions**: disabled tags vanish completely (verified with performance_metrics.h)
- **Type-Safe Tags**: tags are compile-time types, eliminating runtime issues associated with typos
- **Advanced Builders**: truncating and continuation record builders using stream operators
- **Explicit Over Implicit**: no hidden global configuration, everything is visible
- **Header-Only**: no linking required for basic functionality
- **No Exceptions**: suitable for exception-free environments
- **Minimal Dependencies**: only standard C++ library (C++11 or later), configurable for bare-metal/no-std environments

### Nova Extras

- **Rich Sink Collection**: OStream, file rotation, filtering, composition
- **Flexible Formatters**: ISO8601 timestamps, custom formats, multi-line
- **Synchronization Primitives**: mutex-based and spinlock-based thread safety
- **Advanced Builder**: streaming record builder
- **Utility Components**: buffers, async queues, hierarchical tags

### Flare

- **Crash-Safe Logging**: designed for signal handlers and crash scenarios
- **No Heap Allocation**: uses only stack buffers (4KB default)
- **Async-Signal-Safe**: safe to use in signal handlers (with raw syscalls)
- **Binary TLV Format**: compact, parsable even when corrupted
- **Forensic Analysis**: Python tools for post-mortem investigation
- **Graceful Degradation**: extracts partial data from corrupted records

## Quick Start

### Basic Usage (Nova)

```cpp
#include "kmac/nova/nova.h"
#include "kmac/nova/scoped_configurator.h"
#include "kmac/nova/extras/ostream_sink.h"

// define a tag for your subsystem
struct NetworkTag {};
NOVA_LOGGER_TRAITS( NetworkTag, NETWORK, true, kmac::nova::TimestampHelper::steadyNanosecs );

int main()
{
	// create and configure sink
	kmac::nova::extras::OStreamSink consoleSink( std::cout );

	// for RAII clean-up of sinks registration
	kmac::nova::ScopedConfigurator config;
	config.bind< NetworkTag >( &consoleSink );

	// log messages
	NOVA_LOG( NetworkTag ) << "Server started on port " << 8080 << "\n";

	return 0;
}
```

**Output:**
```
Server started on port 8080
```

### Emergency Logging (Flare)

```cpp
#include "kmac/flare/emergency_sink.h"
#include "kmac/flare/file_writer.h"

// in signal handler or crash scenario
void crashHandler( int signal )
{
	static FILE* crashLog = fopen( "/var/log/crash.flare", "ab" );
	static kmac::flare::FileWriter writer( crashLog );
	static kmac::flare::EmergencySink sink( &writer );

	// this is async-signal-safe (no malloc, no locks)
	NOVA_LOG_STACK( CrashTag ) << "SIGSEGV at address " << faultingAddress;

	// flushed automatically when builder destructs
}
```

## Repository Structure

```
.
├── libs/
│   ├── nova/                      # Nova core (header-only)
│   │   └── include/
│   │       └── kmac/nova/
│   │           └── platform/      # platform abstraction headers
│   │
│   ├── nova_extras/               # Nova Extras (sinks, formatters, async)
│   │   ├── include/
│   │   └── src/
│   │
│   └── nova_flare/                # Flare emergency/crash logging
│       ├── include/
│       ├── src/
│       └── scripts/               # flare_reader.py, test_flare_reader.py
│
├── examples/                      # complete working examples
│   ├── 01_basic_usage/
│   ├── 02_multiple_sinks/
│   ├── 03_custom_clock/
│   ├── 04_multithreading/
│   ├── 05_filtering/
│   ├── 06_hierarchical_tags/
│   ├── 07_flare/                  # Flare crash capture demo
│   ├── 08_bare_metal/             # ARM Cortex-M bare-metal example
│   └── 09_diagnostics/            # NOVA_ENABLE_DIAGNOSTICS example
│
├── tests/                         # unit and integration tests
│   ├── test_nova_*.cpp            # Nova core and Extras tests
│   ├── test_flare_*.cpp           # Flare tests
│   ├── test_integration.cpp       # integration tests
│   ├── nova_c11_check.cpp         # strict C++11 conformance check
│   ├── bare_metal/                # bare-metal QEMU test harness
│   └── rtos/                      # FreeRTOS QEMU test harness
│
├── benchmarks/                    # performance benchmarks
│   └── benchmark_*.cpp            # Nova, Extras, Flare, and comparisons
│
├── fuzz/                          # LibFuzzer targets
│   ├── fuzz_record_builder.cpp
│   ├── fuzz_continuation_builder.cpp
│   ├── fuzz_flare_reader.cpp
│   ├── fuzz_flare_scanner.cpp
│   ├── flare.dict                 # Flare binary format dictionary
│   └── corpus/                    # seed corpus per target
│
├── cmake/                         # CMake modules and toolchains
│   ├── Sanitizers.cmake
│   ├── NovaConfig.cmake.in
│   └── toolchains/                # cross-compile toolchain files
│       ├── arm-none-eabi-cortex-m3.cmake
│       ├── arm-none-eabi-cortex-m4.cmake
│       └── android-ndk.cmake
│
├── docs/                          # additional documentation
│   ├── CPP_VERSION_COMPATIBILITY.md
│   ├── FLARE_README.md
│   ├── FLARE_USE_CASES.md
│   ├── LIBRARY_MIGRATION.md
│   └── SAFETY_CRITICAL_GUIDELINES.md
│
├── .github/
│   └── workflows/                 # CI workflows (sanitizers, static analysis,
│                                  # bare-metal, RTOS, fuzzing, multi-platform)
│
├── README.md
└── LICENSE                        # BSD-3-Clause
```

## Architecture

### Compile-Time Routing

Nova's distinguishing feature is type-based routing via tagged types:

```cpp
// define different tags for different purposes
struct NetworkTag {};      // subsystem
struct DatabaseTag {};     // another subsystem
struct CriticalTag {};     // severity
struct DebugNetworkTag {}; // combination: debug + network

// each tag can route to a different sink
config.bind< NetworkTag >( &networkFileSink );
config.bind< DatabaseTag >( &dbFileSink );
config.bind< CriticalTag >( &alertingSink );
config.bind< DebugNetworkTag >( &verboseConsoleSink );
```

The compiler knows at each call site which tag is being logged to, enabling:
- **Compile-time elimination** of disabled tags
- **No runtime tag lookups** or string comparisons
- **Type safety** - can't accidentally mix tags
- **Zero overhead** - same performance as handwritten code

### Logger Traits

Each tag's behaviour is customized via `logger_traits< Tag >`. The recommended
way is the `NOVA_LOGGER_TRAITS` macro:

```cpp
// timestamp function (must match signature: std::uint64_t() noexcept)
std::uint64_t gameTimestamp() noexcept
{
    return GameEngine::getFrameNanoseconds();
}

// defines all properties: name, enabled flag, and timestamp source
NOVA_LOGGER_TRAITS( GameTag, GAME, true, gameTimestamp );

// disabled tag - all logging compiles to nothing
NOVA_LOGGER_TRAITS( DebugTag, DEBUG, false, gameTimestamp );
```

Or as a manual specialization:

```cpp
template<>
struct kmac::nova::logger_traits< GameTag >
{
    static constexpr const char* tagName = "GAME";
    static constexpr std::uint64_t tagId = kmac::nova::details::fnv1a( "GameTag" );
    static constexpr bool enabled = true;

    static std::uint64_t timestamp() noexcept
    {
        return GameEngine::getFrameNanoseconds();
    }
};
```

Tag IDs are generated as FNV-1a 64-bit hashes of the fully-qualified tag type
name string passed to `NOVA_LOGGER_TRAITS`.  Collisions between tags in the
same translation unit are detected at compile time via a `TagIdVal` struct
specialization guard - a duplicate tag ID produces a redefinition error.


### Sink Architecture

Sinks implement a simple interface:

```cpp
class CustomSink : public kmac::nova::Sink
{
public:
	void process( const kmac::nova::Record& record ) noexcept override
	{
		// handle the record
	}
};
```

Sinks can be:
- **Composed** (CompositeSink, FilterSink)
- **Synchronized** (SynchronizedSink, SpinlockSink)
- **Formatted** (FormattingSink)
- **Specialized** (RollingFileSink, MemoryPoolAsyncSink)

### Record Builder Pattern

Records are constructed with type-safe builders:

```cpp
// truncating builder (Nova core - zero allocation)
TruncatingRecordBuilder<> builder;
builder.setContext< Tag >( __FILE__, __FUNCTION__, __LINE__ );
builder << "Value: " << 42;
builder.commit();

// continuation builder (Nova core - zero allocation)
ContinuationRecordBuilder<> builder;
builder.setContext< Tag >( __FILE__, __FUNCTION__, __LINE__ );
builder << "Part 1";
builder << "Part 2";
builder.commit();
```

## Design Philosophy

### Explicit Over Implicit

Nova values explicitness:

```cpp
// explicit - clear what's happening
config.bind< NetworkTag >( &networkSink );
NOVA_LOG( NetworkTag ) << "Message";

// vs implicit (traditional loggers)
LOG_INFO( "Message" );  // where does this go?  who configured it?
```

### Compile-Time Over Runtime

Prefer compile-time decisions:

```cpp
// compile-time: disabled tags compile to nothing
template<>
struct logger_traits< VerboseDebug >
{
	static constexpr bool enabled = false;  // completely eliminated
};

// runtime: tag still processes, just discarded
config.bind< VerboseInfo >( nullptr );
config.bind< VerboseDebug >( &NullSink::instance() );
```

### Determinism Over Convenience

Nova prioritizes predictable behavior over hidden convenience features:

**Compile-time decisions**
```cpp
// disabled tags: completely eliminated at compile time
template<>
struct logger_traits< VerboseTag > {
	static constexpr bool enabled = false;  // 0 ns overhead
};

NOVA_LOG( VerboseTag ) << "Never executed";  // compiled out entirely
```

**Explicit costs**
```cpp
// traditional loggers: hidden string building
logger->info( "Message {}", longString );  // may allocate, format, lock

// Nova: explicit buffer size, visible in code
NOVA_LOG( Tag ) << "Message";  // 1024-byte stack buffer (configurable)
```

**No hidden global state**
```cpp
// traditional: performance depends on runtime configuration
logger->info( "Message" );  // what sinks are registered?  what filters?  what level?

// Nova: performance determined by tag and bound sink
NOVA_LOG( NetworkTag ) << "Message";  // NetworkTag's sink determines cost
```

The key difference: Nova makes costs **visible and controllable**.  You choose:
- which tags exist, which are enabled (compile-time)
- which sink is bound for each tag (explicit binding)
- which record builder to use (truncating=stack, continuation=stack, streaming=heap, custom=?)
- buffer sizes (template parameters, no hidden defaults)

Traditional loggers optimize for ease of use with defaults and global configuration.  Nova optimizes for explicit control where you decide the tradeoffs.

### Safety Over Features

No surprises in critical scenarios:

```cpp
// Flare: async-signal-safe emergency logging
EmergencySink sink( &writer );  // no malloc, no locks
TruncatingRecordBuilder< CrashTag > builder;  // stack only, noexcept
```

## Use Cases

### Nova vs String-Based Loggers - When to Choose Which

Nova's type-based approach and string-based loggers (spdlog, log4cxx, glog) optimize for different priorities:

#### Choose Nova When:

**Performance and determinism are critical**
- real-time systems requiring predictable overhead
- embedded systems with tight resource constraints
- games needing zero-cost disabled logging (frame rate sensitive)
- safety-critical systems requiring compile-time verification
- applications where disabled logs must have zero overhead

**You value explicit configuration and type safety**
- compile-time tag validation (typos caught by compiler)
- explicit sink binding (no hidden global state)
- type-based routing (no runtime string lookups)
- visible configuration (all bindings in code)

**You control all code in the system**
- in-house applications with full codebase ownership
- embedded firmware where all components are custom
- applications without dependencies on libraries that expect specific logging frameworks

#### Choose String-Based Loggers When:

**Built-in convenience features matter more than zero-cost performance**
- **Built-in config file parsing:** string-based frameworks parse config files directly; Nova requires implementing a custom config loader that maps strings to tag bindings
- **Built-in admin interfaces:** string-based frameworks often include JMX/HTTP/telnet consoles; Nova requires implementing your own control interface (FilterSink makes this straightforward)
- **Automatic hierarchical inheritance:** string-based loggers derive hierarchy from naming (`com.company.module` inherits from `com.company`); Nova requires explicit configuration per tag

**You need unbounded dynamic contexts**
- multi-tenant SaaS (per-tenant loggers: `"tenant." + userId`)
- plugin systems (per-plugin loggers created at runtime)
- network servers (per-connection loggers for thousands of connections)

**Note:** Nova can handle dynamic contexts using custom routing sinks, but string-based loggers make this pattern more natural.

**Third-party ecosystem integration is important**
- many third-party libraries already use spdlog/log4cxx
- existing infrastructure expects standard logging APIs
- integration with monitoring/logging services designed for string-based loggers

**Note:** Nova can integrate third-party libraries via the Adapter Pattern (see `docs/LIBRARY_MIGRATION.md`).

#### Nova Can Still Handle These With Adapters:

**Runtime configuration:** ✅ Possible
```cpp
// parse config file and bind/unbind sinks at runtime
configLoader.loadConfig( "logging.json", scopedConfig );

// or use FilterSink for dynamic runtime control
DynamicLevelFilter filter( &sink );
config.bind< Tag >( &filter );
filter.setLevel( Level::DEBUG );  // change at runtime
```

**Per-user/per-connection logging:** ✅ Possible
```cpp
// custom sink routes by context
class PerConnectionSink : public Sink {
	thread_local static int currentConnection;
	std::map< int, Sink* > sinks;

	void process( const Record& r ) noexcept override {
		sinks[ currentConnection ]->process( r );
	}
};
```

**Third-party integration:** ✅ Possible via Adapter Pattern
```cpp
// wrap spdlog/log4cxx - Nova logs forward to existing system
class SpdlogAdapterSink : public nova::Sink {
	void process( const Record& r ) noexcept override {
		spdlogLogger->info( "[{}] {}", r.tag, r.message );
	}
};
```

**Hierarchical organization:** ✅ Possible
```cpp
// explicit hierarchy with types
namespace com { namespace company { namespace module {
    struct Tag {};
}}}

// or use HierarchicalTag
using ComCompanyModule = HierarchicalTag< CompanyTag, ModuleTag >;
```

**Trade-off:** Nova approaches require explicit code rather than automatic behavior, and some (like per-connection routing) add runtime overhead.  String-based loggers make these patterns more convenient at the cost of compile-time safety and zero-cost disabled logs.

---

### Nova - General Purpose Logging

✅ **Best for:**
- performance-critical applications (zero-cost disabled tags)
- multi-threaded applications (explicit synchronization via SynchronizedSink)
- embedded systems with C++ standard library
- games and simulations (custom timestamps, subsystem-based logging)
- modular systems needing subsystem-based routing
- safety-critical systems requiring compile-time verification
- applications where explicitness and type safety matter

⚠️ **Consider trade-offs for:**
- systems requiring unbounded dynamic loggers (can use custom routing sink, but might less convenient than string-based)
- applications with many third-party libraries using other logging frameworks (adapter pattern works, but adds integration effort)
- teams preferring configuration-file-driven logging (can implement config loader, but not built-in)

**Note:** Nova supports runtime configuration via sink binding/unbinding and FilterSink, but without built-in config file parsing.  See `docs/LIBRARY_MIGRATION.md` for adapter patterns and integration strategies.

---

### Nova Extras - Rich Logging Features

✅ **Best for:**
- desktop applications
- mobile applications
- web services and servers
- development and debugging tools
- applications needing formatted output (ISO8601, custom formats)
- thread-safe multi-sink architectures (CompositeSink, SynchronizedSink)

⚠️ **Consider trade-offs for:**
- hard real-time systems (some extras use heap allocation - carefully choose appropriate sinks)
- bare-metal embedded (some extras depend on std library features - consider core only or bare-metal builds)
- scenarios requiring deterministic allocation patterns (use Nova core with TruncatingRecordBuilder)

---

### Flare - Emergency/Forensic Logging

✅ **Best for:**
- signal handlers (SIGSEGV, SIGABRT, SIGILL)
- crash dump analysis and post-mortem debugging
- safety-critical systems requiring crash forensics (medical, automotive, aviation)
- real-time systems (deterministic, no heap allocation)
- embedded systems with limited resources
- black-box recording for failure analysis
- regulatory compliance (IEC 62304, ISO 26262, DO-178C)

❌ **Not ideal for:**
- regular application logging (use Nova - Flare is for emergencies)
- human-readable logs during development (binary format requires parsing)
- interactive debugging (post-mortem analysis tool)
- high-volume continuous logging (designed for crash scenarios)

**See also:** `docs/SAFETY_CRITICAL_GUIDELINES.md` for detailed guidance on using Nova/Flare in certified systems.

---

### Comparison Summary

| Scenario | Nova Approach | String-Based Approach | Verdict |
|----------|---------------|----------------------|---------|
| **Zero-cost disabled logs** | ✅ `if constexpr` eliminates code and arguments (C++17); optimiser-dependent in release builds (C++11/14) | ⚠️ Some support compile-time level disabling but not per-named-logger<sup>1</sup> | — |
| **Selective disabling** | ✅ Per-tag compile-time granularity | ⚠️ Per-named-logger runtime only | **Compile-time type-safe control is Nova unique** |
| **Compile-time safety** | ✅ Types catch errors | ❌ Strings fail at runtime | Nova advantage |
| **Runtime config files** | ⚠️ Requires app code | ✅ Built-in | String advantage (convenience) |
| **Dynamic loggers (unbounded)** | ⚠️ Custom sink needed | ✅ Natural | String advantage (convenience) |
| **Configuration isolation** | ✅ Tags are types — third-party code cannot interfere with your sink bindings | ❌ Global registry — any library can reconfigure loggers | Nova advantage |
| **Shared logging protocol** | ✅ Both using Nova: app can configure library tags via public tag types; library cannot affect app tags | ⚠️ Both using same framework: app can configure library loggers by name; library can also affect app loggers via shared registry | Nova advantage when both parties use it |
| **Initialization order safety** | ✅ No static init races; each component can only rebind its own tag types — cannot affect other components' bindings | ⚠️ Global registry subject to static init order issues; any component can reconfigure any named logger | Nova advantage on isolation; both require care during runtime reconfiguration |
| **Hierarchical inheritance** | ⚠️ Explicit per-tag binding — predictable, no implicit inheritance side effects | ✅ Automatic propagation through naming hierarchy | String advantage (convenience) |
| **Logger access at call site** | ✅ Tag type resolved at compile time — no instance to pass or cache | ⚠️ Cached pointer: comparable to Nova; uncached: map lookup + string compare per call | Nova advantage (ergonomics + performance) |
| **Scoped configuration** | ✅ Local lifetime, no global state | ❌ Global registry singleton | Nova advantage |
| **Stale logger references** | ✅ No logger instances held — atomic sink lookup on every call guarantees current configuration | ❌ Cached logger instances can become stale if registry is reconfigured after caching | Nova advantage (correctness) |
| **Type safety** | ✅ Compiler enforced | ❌ Runtime validation | Nova advantage |
| **Performance (enabled logs)** | ✅ Direct compile-time dispatch, atomic sink lookup | ⚠️ Cached pointer: comparable; uncached: map lookup + string compare per call | Nova advantage (uncached); comparable (cached) |
| **Per-user/per-connection** | ✅ Custom sink works | ✅ Native support | String advantage (convenience) |
| **Operational flexibility** | ✅ FilterSink + control interface | ✅ Built-in admin tools | — |

<sup>1</sup> Some string-based loggers (glog, spdlog) support compile-time disabling via preprocessor macros, but only at coarse severity level granularity — removing all DEBUG logs globally. They cannot selectively disable an individual named logger while keeping others at the same severity level enabled.

**Key insight:** The combination of zero-cost disabled logging with per-tag granularity is genuinely unique to Nova - string-based loggers can achieve compile-time elimination only at coarse severity level boundaries, not per individual tag type.

## Performance

Nova is designed around zero-cost abstractions:

- **Disabled tags** compile to nothing on C++17 (language guarantee) or are
  eliminated by the optimiser in release builds on C++11/14
- **Enabled tags** incur only an atomic sink pointer load plus the cost of the
  sink itself — no string formatting, no map lookup, no registry access
- **Async logging** via `MemoryPoolAsyncSink` decouples the hot path from I/O

For measured latency and throughput figures comparing Nova against spdlog,
Quill, and easylogging++, see the `benchmarks/` directory.  Run the benchmark
suite on your target hardware for results relevant to your use case.

## Benchmarks

TBD

## Building

### Nova (Header-Only)

No build required!  Just include headers:

```bash
# minimum supported standard
g++ -std=c++11 -I nova/include your_app.cpp -o your_app

# recommended for best standard library support (std::string_view, std::to_chars)
g++ -std=c++17 -I nova/include your_app.cpp -o your_app
```

### Nova Extras + Flare

Use CMake:

```bash
mkdir build && cd build
cmake ..
make
make test  # Run unit tests
```

Or compile directly:

```bash
g++ -std=c++11 \
    -I nova/include \
    -I nova_extras/include \
    -I flare/include \
    your_app.cpp \
    nova_extras/src/kmac/nova/extras/*.cpp \
    flare/src/kmac/flare/*.cpp \
    -o your_app -pthread
```

### Unit Tests

```bash
# With CMake
cd build
make
ctest

# Or with Google Test directly
# note: tests require C++17 for std::filesystem
g++ -std=c++17 \
    -I nova/include \
    -I nova_extras/include \
    -I flare/include \
    tests/test_*.cpp \
    nova_extras/src/kmac/nova/extras/*.cpp \
    flare/src/kmac/flare/*.cpp \
    -lgtest -lgtest_main -pthread \
    -o test_runner
./test_runner
```

## Examples

The repository includes comprehensive examples:

### Nova Examples

1. **01_basic_usage** - core concepts and fundamentals
2. **02_multiple_sinks** - routing, CompositeSink, NullSink
3. **03_custom_clock** - custom timestamp sources
4. **04_multithreading** - thread-safe logging
5. **05_filtering** - runtime filtering with FilterSink
6. **06_hierarchical_tags** - traditional severity-based logging
7. **07_flare** - comprehensive crash logging demonstration
8. **08_bare_metal** - bare-metal/RTOS usage with custom timestamp and UART sink
9. **09_diagnostics** - platform auto-detection and configuration verification

Build and run:

```bash
cd examples/nova
g++ -std=c++11 -I../../nova/include -I../../nova_extras/include \
    01_basic_usage/main.cpp \
    ../../nova_extras/src/kmac/nova/extras/*.cpp \
    -o basic_usage
./basic_usage
```

## Benchmarks

The repository includes comprehensive benchmarks, including Nova, Nova Extras, Flare, and comparisons with other popular libraries.

## Documentation


## Testing

The repository includes comprehensive unit tests using Google Test:

- **Core Tests**: tag routing, record builders, logger traits
- **Extras Tests**: all sinks, formatters, synchronization
- **Flare Tests**: emergency logging, reader, scanner
- **Integration Tests**: multi-component scenarios

**Test Coverage:**
- Thread safety verification
- Stress testing (1000+ concurrent logs)
- Edge cases and error conditions

Run tests:
```bash
cd build
make test
# or
ctest --verbose
```

## Advanced Topics

### Custom Sinks

Implement the `Sink` interface:

```cpp
class NetworkSink : public kmac::nova::Sink
{
	int _socket;
    
public:
	NetworkSink( int socket ) : _socket( socket ) {}

	void process( const kmac::nova::Record& record ) noexcept override
	{
		// serialize and send over network
		send( _socket, record.message, record.messageSize, 0 );
	}
};
```

### Custom Timestamps

Use custom clock sources:

```cpp
struct GameTag {};

template<>
struct kmac::nova::logger_traits< GameTag >
{
	static constexpr const char* tagName = "GAME";
	static constexpr std::uint64_t tagId = kmac::nova::details::fnv1a( "GameTag" );
	static constexpr bool enabled = true;

	static std::uint64_t timestamp() noexcept
	{
		return GameEngine::getFrameNumber();  // frame counter instead of time
	}
};

// OR convenience macro
NOVA_LOGGER_TRAITS( GameTag, GAME, true, GameEngine::getFrameNumber );

```

### Async-Signal-Safe Logging

For true async-signal-safety, use raw syscalls:

```cpp
class SyscallWriter : public kmac::flare::IWriter
{
	int _fd;
    
public:
	SyscallWriter( int fd ) : _fd( fd ) {}

	size_t write( const void* data, size_t size ) noexcept override
	{
		return ::write( _fd, data, size );  // POSIX syscall
	}

	void flush() noexcept override
	{
		::fsync( _fd );
	}
};

// usage in signal handler
void setupCrashHandler()
{
	int fd = open( "/var/log/crash.flare", O_WRONLY | O_CREAT | O_APPEND, 0644 );
	static SyscallWriter writer( fd );
	static kmac::flare::EmergencySink sink( &writer );

	signal( SIGSEGV, crashHandler );
}
```

### Severity-Based Logging

For those who prefer traditional severity-based logging, Nova provides multiple approaches:

**Option 1: Direct severity tags** (from `severities.h`):
```cpp
#include "kmac/nova/extras/severities.h"

using namespace kmac::nova::extras;

// use pre-defined severity tags directly
config.bind< Debug >( &debugSink );
config.bind< Info >( &infoSink );
config.bind< Warning >( &warnSink );
config.bind< Error >( &errorSink );

NOVA_LOG( Debug ) << "Detailed trace";
NOVA_LOG( Info ) << "Normal operation";
NOVA_LOG( Error ) << "Something failed";
```

**Option 2: Hierarchical tags** (subsystem + severity):
```cpp
#include "kmac/nova/extras/hierarchical_tag.h"
#include "kmac/nova/extras/severities.h"

using namespace kmac::nova::extras;

// combine subsystem with severity
using NetworkDebug = HierarchicalTag<NetworkTag, Debug>;
using NetworkInfo = HierarchicalTag<NetworkTag, Info>;
using NetworkError = HierarchicalTag<NetworkTag, Error>;

// configure minimum severity per subsystem
config.bind<NetworkInfo>(&sink);   // info and above
config.bind<NetworkError>(&sink);  // only errors

// log with both subsystem and severity context
NOVA_LOG( NetworkDebug ) << "Detailed trace";     // may be filtered
NOVA_LOG( NetworkInfo ) << "Connection opened";   // logged
NOVA_LOG( NetworkError ) << "Connection failed";  // logged
```

**Option 3: Custom severity tags**:
```cpp
namespace LogSeverity {
struct D {};
struct I {};
struct W {};
struct E {};
}

NOVA_LOG( LogSeverity::D ) << "custom debug tag";
```

**Option 4: Custom combined tags (explicit context)**:
```cpp
struct NetworkDebug {};

NOVA_LOG( NetworkDebug ) << "networking update";

struct WorkheadMovingState {};

NOVA_LOG( WorkheadMovingState ) << "move direction = " << moveDir;  // log specific to moving state, logging enabled/disabled independant of other states
```

## Platform Support

### Tested Platforms

- **Linux**: Ubuntu 20.04+, RHEL 8+, Debian 11+
- **macOS**: 10.15+
- **Windows**: Windows 10+ (MSVC 2019+, MinGW)
- **FreeBSD**: 12.0+

### Compiler Requirements

- **GCC**: 5.0+ (C++11 support)
- **Clang**: 3.3+ (C++11 support)
- **MSVC**: 2015+ (C++11 support)

### Embedded Platforms

Nova core works on embedded platforms with:
- C++11 or later compiler
- standard library (atomic, chrono, vector)
- threading support (optional)

For bare-metal or RTOS, consider:
- custom no-std version (future work)
- Flare with custom IWriter for hardware-specific I/O

## License

This project is licensed under the BSD 3-Clause License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- inspired by the need for compile-time logging in safety-critical systems
- designed to challenge the severity-centric logging paradigm
- built for developers who value explicitness and determinism

## Getting Help

- **Documentation**: start with [Nova Core Guide](docs/NOVA_README.md)
- **Examples**: see [examples/](examples/) for working code
- **Issues**: report bugs or request features via GitHub issues
- **Discussions**: ask questions in GitHub Discussions

## FAQ

**Q: Why not just use spdlog/glog/Boost.Log?**  
A: Those are excellent runtime-based loggers.  Nova is designed for scenarios where compile-time routing, zero overhead for disabled tags, and deterministic behavior are required.

**Q: Do I need all three libraries?**  
A: No.  Use Nova core for basic logging, add Nova Extras for convenience features, and use Flare only if you need crash-safe emergency logging.

**Q: Can I use Nova without exceptions?**  
A: Yes, Nova core is fully exception-free.  Compile with `-fno-exceptions`.

**Q: Is Nova thread-safe?**  
A: Logger binding is atomic.  Thread safety is per sink, either through wrapping wrapping a sinks with SynchronizedSink or SpinlockSink or by implementing explicit multi-thread handling for custom sinks.

**Q: How do I migrate from another framework?**  
A: See [LIBRARY_MIGRATION.md](docs/LIBRARY_MIGRATION.md) for step-by-step guides.

**Q: What's the performance overhead?**  
A: Disabled tags: 0 ns (compiled out).  See Performance/Benchmark sections above.

**Q: Can I log by both severity AND subsystem?**  
A: Yes!  Combine concepts into a single tag: `NetworkError` OR use HierarchicalTag: `HierarchicalTag<NetworkTag, Error>`.

---

## Author

**[Kleetus MacTavish]**  
<[kleetus.mactavish@gmail.com]>

For questions, bug reports, or contributions, please open an issue on the project repository.

---

**Nova/Flare** - Compile-time logging for modern C++  
*Explicit. Deterministic. Zero-cost. Awesome.*
