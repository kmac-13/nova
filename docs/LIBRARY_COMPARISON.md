# Logging Library Comparison: Nova/Flare vs. Popular C++ Logging Libraries

## Executive Summary

This document compares Nova, Nova Extras, and Flare against popular C++ logging libraries including spdlog, Quill, log4cplus, Boost.Log, glog, and NanoLog.  The comparison focuses on design philosophy, performance characteristics, safety guarantees, and use case suitability.  log4cpp, the predecessor to log4cplus, is noted where relevant but is not recommended for new projects (LGPL 2.1 license, not thread-safe by default).

---

## Comparison Tables

Nova targets a different design space from most C++ logging libraries.  The following tables are split by design intent: safety-critical and embedded criteria first, then general-purpose hosted criteria.  Nova appears in both
for reference.

---

## Safety-Critical & Embedded Criteria

Compared to hosted/server-side C++ logging ecosystems, the landscape of dedicated safety-critical and embedded logging libraries is relatively sparse.  Most embedded teams use a minimal C logger (often a printf wrapper), a custom implementation, or no logging at all.  Libraries like P7/μP7 offer more features but come with ecosystem dependencies (binary format requiring Baical viewer for decoding) and license constraints (LGPL 3.0).  Pure string-based C loggers (log.c, microlog) are widely used in embedded contexts but provide no type safety and vary in bare-metal suitability.

This comparison covers the most commonly encountered options.  For reference, several widely used general-purpose C++ logging libraries (spdlog, Quill, etc.) are covered separately - they were not designed for these environments and are omitted here.

| Criterion | Nova/Flare | NanoLog (Stanford) | P7 / μP7 | log.c | microlog |
|-----------|-----------|---------|----------|-------|---------|
| **Zero heap in hot path** | ✅ Always | ✅ Always | ⚠️ Pre-allocated at init; μP7: ✅ | ⚠️ Stack-heavy | ✅ Configurable |
| **Deterministic / bounded** | ✅ Yes | ✅ Yes | ⚠️ Configurable; μP7: ✅ | ⚠️ Output-dependent | ⚠️ Configurable |
| **No exceptions in logging path** | ✅ Yes | ✅ Yes | ✅ Yes (C-based) | ✅ Yes (C) | ✅ Yes (C) |
| **Async-signal-safe crash logging** | ✅ Flare | ❌ No | ⚠️ Crash handler (not async-signal-safe) | ❌ No | ❌ No |
| **Compile-time log elimination** | ✅ Per-tag compiled out (optimizer pre-C++17) | ❌ No | ✅ Format strings compiled out | ❌ No | ✅ Feature stripping |
| **Runtime domain filtering** | ✅ Per-tag (sink bind/unbind) | ❌ No | ✅ Named channels | ❌ No | ✅ Per-topic |
| **Runtime level filtering** | ⚠️ Via FilterSink | ❌ No | ✅ Yes | ✅ Yes | ✅ Yes |
| **Bare-metal / RTOS support** | ✅ Yes | ❌ Hosted only | ✅ μP7 variant | ⚠️ Requires stdio | ⚠️ Requires stdio |
| **Offline log processing required** | ❌ No | ✅ Yes (decoder tool) | ✅ Yes (Baical viewer; text mode available) | ❌ No | ❌ No |
| **Language** | C++11/17 | C++17 | C/C++ (μP7: C only) | C99 | C99/C11 |
| **Type safety** | ✅ Compile-time types | ✅ Printf-style (type-safe) | ❌ Printf-style | ❌ Printf-style | ❌ Printf-style |
| **Certifiability guidance** | ✅ Documented | ❌ No | ❌ No | ❌ No | ❌ No |
| **License** | BSD-3 | BSD-3 | LGPL 3.0 ⚠️ | MIT | MIT |

**Notes:**

- **Async-signal-safe** here refers specifically to operation from POSIX fatal signal handlers without allocator, mutex, or non-signal-safe libc dependencies.
- **P7 / μP7**: Two variants - P7 targets hosted systems (Linux/Windows/ARM), μP7 targets bare-metal/RTOS microcontrollers.  Binary format requires the Baical viewer for decoding; a plain-text sink is available at the cost of performance.  LGPL 3.0 requires disclosure of modifications, which may be a constraint for proprietary safety-critical products.
- **log.c**: Minimal two-file C99 library (~150 lines).  Widely copied into embedded projects.  Uses stdio - not suitable for strict bare-metal without a custom output backend.
- **microlog**: Evolved fork of log.c (~2,500 lines) with compile-time feature stripping and per-topic filtering.  More capable than log.c but still printf-style and stdio-dependent by default.
- **NanoLog**: Targets hosted Linux only.  Single global logger with no per-subsystem routing.  Offline post-processing required with the provided decoder tool.

---

## General-Purpose C++ Criteria

This table covers libraries intended for hosted application logging.  Even though Nova was designed for safety-critical systems, it's included here to highlight its viability as a general-purpose logging system.

| Criterion | Nova/Extras | spdlog | Quill | log4cplus | Boost.Log | glog | NanoLog |
|-----------|------------|--------|-------|-----------|-----------|------|---------|
| **Language** | C++11/17 | C++11 | C++17 | C++11 (v1.x, v2.x) / C++23 (v3.x) | C++11 | C++11 | C++17 |
| **License** | BSD-3 | MIT | MIT | Apache 2.0 / BSD-2 | Boost | BSD-3 | BSD-3 |
| **Learning curve** | ⚠️ Medium | ✅ Low | ⚠️ Medium | ❌ High | ❌ High | ✅ Low | ⚠️ Medium |
| **Modern formatting syntax (e.g. `{fmt}`)** | ⚠️ Possible (user-supplied) | ✅ Yes | ✅ Yes | ❌ PatternLayout | ✅ Yes | ❌ No | ❌ No |
| **Compile-time filtering** | ✅ Per-tag | ⚠️ Global level | ⚠️ Global level | ❌ No | ❌ No | ⚠️ Via GOOGLE_STRIP_LOG | ❌ No |
| **Runtime domain filtering** | ✅ Per-tag (sink bind/unbind) | ✅ Per-logger | ✅ Per-logger | ✅ Per-category | ✅ Per-sink attribute filters | ✅ Per-severity file output | ❌ Single global |
| **Runtime level filtering** | ⚠️ Via FilterSink | ✅ Per-logger | ✅ Per-logger | ✅ Per-category | ✅ Advanced | ✅ Yes | ⚠️ Global only |
| **Async logging** | ✅ Yes | ✅ Yes | ✅ Yes | ✅ Yes | ✅ Yes | ❌ No | ✅ Yes |
| **Log rotation** | ✅ Yes | ✅ Yes | ✅ Yes | ✅ Yes | ✅ Yes | ✅ Yes | ❌ No |
| **Structured / JSON output** | ✅ Yes | ✅ Yes | ✅ Yes | ❌ No | ✅ Yes | ❌ No | ❌ No |
| **Independent sink routing** | ✅ Per-tag | ✅ Per-logger | ✅ Per-logger | ✅ Per-category | ✅ Per-sink attribute filters | ⚠️ Per-severity file output | ❌ Single global |
| **Header-only option** | ✅ Core only | ✅ Yes | ❌ No | ❌ No | ❌ No | ❌ No | ❌ No |
| **Syslog output** | ✅ Yes | ✅ Yes | ⚠️ Custom | ✅ Yes | ✅ Yes | ✅ Yes | ❌ No |
| **Runtime config file** | ❌ No | ❌ No | ❌ No | ✅ Yes | ✅ Yes | ⚠️ Flags only | ❌ No |

**Notes:**

- **log4cplus v3.x requires C++23** - users on older compilers should use the v1.x / v2.x series, which support C++11.
- **Nova runtime level filtering**: Nova does not have a built-in severity hierarchy.  Level-based filtering can be achieved via `FilterSink` (content-based) or by structuring tags to represent severity explicitly.
- **Nova** is strongest when compile-time routing, zero heap allocation, or embedded constraints are also present.  For purely hosted general-purpose logging, spdlog or Quill may be simpler starting points.
- **easylogging++** has been archived and is omitted.
- **LGPL 3.0** (P7, in the safety-critical table) requires disclosure of modifications; this may be a constraint for proprietary or safety-critical products.  Consult legal counsel before use in those contexts.  Apache 2.0 (log4cplus) and BSD variants (all others) are permissive and commercial-friendly.

## Library Overview

### Nova / Nova Extras / Flare

**Characteristics**: compile-time determinism, zero-allocation safety, explicit configuration

**Core Strengths**:
- **Nova (core)**: header-only, domain-based routing with compile-time sink binding, `TruncatingRecordBuilder` (zero heap, fixed stack buffer), no runtime name-based logger lookup - routing is resolved entirely at compile time via types
- **Nova Extras**: `ContinuationRecordBuilder` and `StreamingRecordBuilder`, modular sinks (OStream, CircularFile, RollingFile, Composite, Synchronized, Asynchronous, Formatting, Filter), extensible architecture
- **Flare**: async-signal-safe emergency logging with TLV encoding, crash-resilient design

**Target Use Cases**: safety-critical systems, real-time applications, embedded systems, crash forensics

---

### spdlog

**Characteristics**: fast, header-only, modern C++ logging with sensible defaults

**Strengths**:
- flexible and straightforward asynchronous logging
- header-only option
- excellent formatting (uses fmt library)
- easy to use, low learning curve
- active development and community

**Weaknesses**:
- no compile-time routing (all runtime)
- uses heap allocation by default
- not async-signal-safe
- larger binary size due to templates

**Best For**: general-purpose applications, server applications, when ease-of-use matters more than determinism

**Code Example**:
```cpp
#include <spdlog/spdlog.h>

auto logger = spdlog::stdout_color_mt("console");
logger->info("Hello {}", "world");
logger->warn("Easy padding in numbers like {:08d}", 12);
```

---

### Quill

**Characteristics**: ultra-low-latency async logging using per-thread SPSC queues with backend formatting via `{fmt}`

**Strengths**:
- extremely low frontend latency - enqueue to per-thread queue is near-zero contention
- per-thread SPSC queues eliminate producer-side lock contention
- backend thread handles all formatting, keeping the hot path minimal
- bounded drop-on-full queue option for predictable behavior
- active development
- optional built-in signal handler (`with_signal_handler`) attempts best-effort backend flush on crash (not async-signal-safe)

**Weaknesses**:
- thread reuse and shutdown ordering require care - threads with pre-existing contexts that are reused across logging sessions can trigger internal assertions and crashes
- not header-only (requires compilation)
- not async-signal-safe


**Best For**: high-throughput async logging where frontend enqueue latency dominates, and thread lifecycle can be managed carefully

**Code Example**:
```cpp
#include <quill/Backend.h>
#include <quill/Frontend.h>
#include <quill/LogMacros.h>
#include <quill/Logger.h>
#include <quill/sinks/FileSink.h>

int main()
{
	quill::Backend::start();

	quill::FileSinkConfig cfg;
	cfg.set_open_mode('w');
	auto sink = quill::Frontend::create_or_get_sink<quill::FileSink>("app.log", cfg);

	quill::Logger* logger = quill::Frontend::create_or_get_logger("root", std::move(sink));

	LOG_INFO(logger, "Hello {}", "world");
	LOG_ERROR(logger, "Error code {}", 42);

	return 0;
}
```

---

### log4cplus

**Characteristics**: actively maintained C++ port of log4j, hierarchical logging with categories and appenders

**Strengths**:
- thread-safe by default
- active development and maintenance
- hierarchical logger/category system familiar to log4j users
- runtime configuration via property files
- flexible appender system (file, rolling file, syslog, console, async)
- Apache 2.0 / BSD-2 license - permissive and commercial-friendly

**Weaknesses**:
- high learning curve (Java log4j heritage)
- extensive heap allocation
- not suitable for embedded or bare-metal targets
- v3.x requires C++23; use v1.x / v2.x for C++11 compatibility
- no compile-time routing or zero-cost disabled logging

**Best For**: hosted applications requiring hierarchical log4j-style configuration, runtime property file reconfiguration, or teams with existing log4j familiarity

**Code Example**:
```cpp
#include <log4cplus/logger.h>
#include <log4cplus/loggingmacros.h>
#include <log4cplus/consoleappender.h>
#include <log4cplus/initializer.h>

int main()
{
	log4cplus::Initializer initializer;
	auto appender = log4cplus::SharedAppenderPtr(new log4cplus::ConsoleAppender());
	log4cplus::Logger logger = log4cplus::Logger::getInstance("App");
	logger.addAppender(appender);
	logger.setLogLevel(log4cplus::INFO_LOG_LEVEL);

	LOG4CPLUS_INFO(logger, "Application started");
	LOG4CPLUS_WARN(logger, "Disk space below 10%");

	return 0;
}
```

---

### easylogging++

**Characteristics**: single-header logging with minimal setup

**Strengths**:
- single header + single `.cc` file, no other dependencies
- very easy to get started
- good default formatting
- cross-platform

**Weaknesses**:
- performance not optimized for high-throughput
- uses heap allocation extensively
- thread safety has overhead
- limited customization compared to alternatives
- project archived (no more development)

**Best For**: small to medium projects, rapid prototyping, when simplicity matters more than performance

**Code Example**:
```cpp
#include "easylogging++.h"

INITIALIZE_EASYLOGGINGPP

LOG(INFO) << "My first log message";
LOG(WARNING) << "This is a warning";
```

---

### Boost.Log

**Characteristics**: feature-complete, industrial-strength logging framework

**Strengths**:
- most feature-rich (filters, formatters, sinks, attributes)
- excellent documentation
- part of Boost ecosystem
- highly configurable

**Weaknesses**:
- very large compile-time and binary size impact
- steep learning curve
- heavy runtime overhead
- requires Boost dependencies
- slow compilation

**Best For**: large enterprise applications, when you need every possible feature, when compile time doesn't matter

**Code Example**:
```cpp
#include <boost/log/trivial.hpp>

BOOST_LOG_TRIVIAL(trace) << "A trace message";
BOOST_LOG_TRIVIAL(debug) << "A debug message";
BOOST_LOG_TRIVIAL(info) << "An info message";
```

---

### glog (Google Logging)

**Characteristics**: Google's internal logging library, simple and fast

**Strengths**:
- battle-tested at Google scale
- very simple API
- good performance
- conditional/verbose logging built-in
- stack trace on crash

**Weaknesses**:
- limited customization
- not header-only
- crash handling not async-signal-safe

**Best For**: applications (not libraries), when you want Google's proven approach, simple logging needs

**Code Example**:
```cpp
#include <glog/logging.h>

LOG(INFO) << "Found " << num_cookies << " cookies";
CHECK_NE(1, 2) << ": The world must be ending!";
VLOG(1) << "I'm printed when you run with --v=1 or higher";
```

---

### NanoLog (Stanford)

**Characteristics**: ultra-low latency logging via compile-time format string processing

**Strengths**:
- compile-time format string extraction - format strings are stripped from binaries and replaced with numeric IDs, minimizing runtime work and binary size
- zero-allocation in hot path
- very small runtime footprint

**Weaknesses**:
- requires offline post-processing to read logs
- single global logger - cannot create multiple instances, route subsystems independently, or use as a library without affecting application-wide logging state
- limited sink options
- not async-signal-safe
- requires code generation step
- less mature ecosystem

**Best For**: ultra-low-latency trading systems, high-frequency data collection, when nanoseconds matter

**Code Example**:
```cpp
#include "NanoLog.hpp"

NANO_LOG(NOTICE, "Simple log message with 0 parameters");
NANO_LOG(DEBUG, "This is a %s with %d parameters", "message", 2);
```

---

> **Why consider Nova instead**: If compile-time routing, zero heap allocation, async-signal-safe crash logging, or predictable deterministic behavior are requirements - or if you are developing for embedded or safety-critical environments - none of the above libraries address all of these.  Nova was designed specifically for these constraints.

---

## Detailed Feature Comparison

### 1. Performance & Overhead

#### Runtime Performance

Measured benchmarks for Nova, spdlog, and Quill are available in [docs/BENCHMARKS.md](BENCHMARKS.md), which covers guaranteed delivery latency and async throughput across 1–8 threads.

Key results (Ryzen 9 6900HX, MinGW GCC, Release, C++17):
- **Nova** (`SynchronizedSink`): 190 ns/msg at 1 thread - 2.7× faster than spdlog sync, 3.6× faster than Quill async for guaranteed delivery
- **Quill**: ~13–50 ns frontend enqueue (backend formatting excluded); backend-bound throughput degrades under multi-thread load
- **spdlog** async: degrades significantly under multi-thread load (up to 8,514 ns/msg at 8 threads)

Performance for log4cplus, easylogging++, Boost.Log, glog, and NanoLog was not measured in the benchmark suite. log4cpp figures are estimates based on its C++98-era implementation and are not verified.

#### Compile-Time Performance

**Fast**:
- **glog**: minimal template usage
- **log4cplus**: traditional C++

**Medium**:
- **spdlog**: header-only but optimized
- **easylogging++**: single header
- **Nova/Flare**: moderate template usage

**Slow**:
- **Boost.Log**: very slow, extensive templates
- **NanoLog**: requires code generation step

#### Binary Size Impact

Binary size impact varies significantly by compiler, optimization level, and which components are used.  Nova core is header-only and benefits from dead-code elimination of disabled tags.  No verified measurements are available for the other libraries.

---

### 2. Safety Guarantees

#### Async-Signal Safety

**Fully Safe**:
- **Flare**: designed specifically for signal handlers and crash contexts
  - no heap allocation
  - no locks
  - no system calls except write()
  - TLV encoding ensures partial records are recoverable

**Partially Safe**:
- **glog**: attempts crash handling but uses non-safe functions
- **Boost.Log**: can configure safe sinks but not guaranteed

**Not Safe**:
- **spdlog**: uses mutexes, heap allocation
- **log4cplus**: heavy runtime, locks
- **easylogging++**: not designed for signal handlers
- **NanoLog**: uses queues, not signal-safe
- **Nova (core)**: not designed for signal handlers (use Flare instead)
- **Nova Extras**: OStreamSink and other sinks use non-safe operations

#### Thread Safety

**Lock-Free Options**:
- **Nova Extras**: MemoryPoolAsyncSink, SpinlockSink
- **Quill**: per-thread SPSC queues (lock-free frontend, not signal-safe)
- **NanoLog**: lock-free queue (not signal-safe though)

**Thread-Safe with Locks**:
- **spdlog**: thread-safe sinks available
- **Boost.Log**: configurable thread safety
- **glog**: thread-safe by default
- **Nova Extras**: SynchronizedSink wrapper

**Requires Manual Synchronization**:
- **log4cplus**: thread-safe by default (unlike log4cpp)
- **easylogging++**: thread-safe with overhead

#### Memory Safety

**Zero Heap Allocation**:
- **Nova (core)**: TruncatingRecordBuilder and pipeline avoid heap
- **Nova Extras**: StreamingRecordBuilder uses heap (by design for flexibility, but builder not required)
- **Flare**: completely heap-free
- **NanoLog**: zero allocation in hot path

**Controlled Allocation**:
- **Quill**: pre-allocated per-thread SPSC queues
- **spdlog**: can use ring buffer
- **glog**: controlled buffering

**Dynamic Allocation**:
- **log4cplus**: extensive heap usage
- **easylogging++**: per-log allocation
- **Boost.Log**: heavy allocation

---

### 3. Architecture & Design

#### Configuration Style

**Compile-Time**:
- **Nova**: domain-based routing determined at compile time
- **NanoLog**: format strings compiled out

**Runtime**:
- **Quill**: runtime sink/logger configuration
- **log4cplus**: property file configuration
- **Boost.Log**: runtime filter chains
- **spdlog**: runtime sink configuration
- **glog**: command-line flags
- **easylogging++**: configuration file or code

**Hybrid**:
- **Nova Extras**: compile-time routing, runtime sink configuration
- **Flare**: compile-time structure, runtime file descriptor

#### Extensibility

**Highly Extensible**:
- **Boost.Log**: attributes, filters, formatters, sinks all pluggable
- **spdlog**: custom sinks require subclassing `spdlog::sinks::base_sink<Mutex>` and implementing `sink_it_()` and `flush_()`
- **Nova (core)**: minimal `Sink` interface (implement a single `process(const Record&)` method) - no mutex template parameter, no threading policy baked in; thread safety is a separate composable concern via `SynchronizedSink`
- **Nova Extras**: many ready-to-use sink implementations

**Moderately Extensible**:
- **Quill**: custom sinks (handlers) supported
- **log4cplus**: appender system
- **easylogging++**: custom handlers possible

**Limited Extensibility**:
- **glog**: limited sink customization
- **NanoLog**: fixed architecture
- **Flare**: fixed format (by design for crash safety)

---

### 4. Use Case Analysis

#### Embedded Systems / Real-Time

**Best Choices**:
1. **Nova + Flare**: zero allocation, deterministic, small footprint
2. **NanoLog**: ultra-fast, but requires post-processing infrastructure
3. **spdlog**: if async logging is acceptable and you have memory

**Avoid**:
- Boost.Log (too heavy)
- log4cpp or log4cplus (too much overhead)
- Quill (thread lifecycle complexity, not bare-metal compatible)

#### Safety-Critical Systems

**Best Choices**:
1. **Flare**: only async-signal-safe option for crash logging
2. **Nova**: deterministic, explicit, testable
3. **glog**: proven at scale, but not signal-safe

**Avoid**:
- anything with non-deterministic behavior
- libraries with extensive heap allocation in hot path

#### High-Performance / Low-Latency

**Best Choices**:
1. **NanoLog**: absolute lowest latency
2. **Quill**: ultra-low frontend enqueue latency, per-thread queues
3. **Nova**: very low overhead, compile-time routing
4. **spdlog**: straightforward async setup with good ecosystem support

**Consider**:
- benchmark your specific workload
- measure end-to-end latency, not just logging overhead

#### General Application Development

**Best Choices**:
1. **spdlog**: best balance of features, performance, ease-of-use
2. **Quill**: if async throughput is a priority
3. **glog**: if you like Google's approach

**Consider**:
- **Boost.Log** if you need advanced features and already use Boost
- **Nova** if you want explicit control and modern C++

#### Library Development

**Best Choices**:
1. **Nova**: designed for library use, no global state per domain, namespace-scoped types prevent naming collisions
2. **spdlog**: can create isolated logger instances

**Avoid**:
- **glog**: global state not ideal for libraries
- **easylogging++**: global initialization

---

### 5. Crash Logging & Forensics

This is where Flare truly shines compared to all alternatives.

#### Crash Log Analysis Comparison

| Feature | Flare | glog | Boost.Log | Others |
|---------|-------|------|-----------|--------|
| Async-signal-safe | ✅ Yes | ❌ No | ❌ No | ❌ No |
| Partial record recovery | ✅ TLV format | ❌ No | ❌ No | ❌ No |
| Torn write detection | ✅ END markers | ❌ No | ❌ No | ❌ No |
| No heap in crash path | ✅ Yes | ❌ No | ❌ No | ❌ No |
| No locks in crash path | ✅ Yes | ❌ No | ❌ No | ❌ No |
| Record status tracking | ✅ Yes | ❌ No | ❌ No | ❌ No |
| Scanner for corruption | ✅ Yes | ⚠️ Basic | ❌ No | ❌ No |

**Flare's Unique Advantages**:
- can log from signal handlers (SIGSEGV, SIGABRT, etc.)
- records survive even if process crashes mid-write
- TLV encoding allows parsing partial/corrupted logs
- sequence numbers for ordering across crashes
- process/thread ID capture for multi-process debugging

**What Others Do**:
- **glog**: attempts to flush on crash, but not signal-safe
- **Boost.Log**: can configure sinks for crash scenarios, but not guaranteed safe
- **Others**: generally just flush buffers, not designed for crash contexts

---

### 6. Code Examples: Same Task in Each Library

**Task**: Log a message with metadata (tag, file, line) to both console and file, with filtering.

#### Nova + Extras

```cpp
#include <kmac/nova.h>
#include <kmac/nova/extras/circular_file_sink.h>
#include <kmac/nova/extras/filter_sink.h>
#include <kmac/nova/extras/fixed_composite_sink.h>
#include <kmac/nova/extras/ostream_sink.h>

struct AppTag {};
NOVA_LOGGER_TRAITS(AppTag, APP, true, kmac::nova::TimestampHelper::steadyNanosecs);

int main()
{
	// fan-out: all records to console, only errors to file
	kmac::nova::extras::OStreamSink consoleSink(std::cout);
	kmac::nova::extras::CircularFileSink fileSink("app.log", 1024 * 1024);

	auto errorFilter = [](const kmac::nova::Record& r) {
	   return strstr(r.message, "ERROR") != nullptr;
	};

	kmac::nova::extras::FilterSink filteredFile(fileSink, errorFilter);

	kmac::nova::Sink* sinks[] = { &consoleSink, &filteredFile };
	kmac::nova::extras::FixedCompositeSink composite(sinks, 2);

	kmac::nova::ScopedConfigurator config;
	config.bind<AppTag>(&composite);

	NOVA_LOG(AppTag) << "All records go to console";
	NOVA_LOG(AppTag) << "ERROR: only this goes to file too";

	return 0;
}
```

#### spdlog

```cpp
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

int main()
{
	auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
	auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("app.log");

	spdlog::sinks_init_list sink_list = { console_sink, file_sink };
	auto logger = std::make_shared<spdlog::logger>("app", sink_list);

	// Filter errors only
	logger->set_level(spdlog::level::err);

	logger->info("This is filtered out");
	logger->error("This shows up");

	return 0;
}
```

#### Boost.Log

```cpp
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>

namespace logging = boost::log;

int main()
{
	logging::add_file_log("app.log");
	logging::add_console_log();

	logging::core::get()->set_filter(logging::trivial::severity >= logging::trivial::error);

	BOOST_LOG_TRIVIAL(info) << "Filtered out";
	BOOST_LOG_TRIVIAL(error) << "This shows up";

	return 0;
}
```

#### glog

```cpp
#include <glog/logging.h>

int main(int argc, char* argv[])
{
	google::InitGoogleLogging(argv[0]);

	// glog always logs to file, filter via severity
	FLAGS_minloglevel = google::ERROR;

	LOG(INFO) << "Filtered out";
	LOG(ERROR) << "This shows up";

	return 0;
}
```

---

## Design Philosophy Comparison

### Nova/Flare Philosophy

**Core Principles**:
1. **Explicitness over convenience**: no hidden allocations, no global state per tag
2. **Compile-time determinism**: routing decisions at compile time
3. **Zero-cost abstractions**: pay only for what you use
4. **Safety first**: async-signal-safety (Flare), deterministic behavior
5. **Composability**: small, focused components (Sink interface)

**When to Choose**:
- safety-critical or real-time systems
- embedded systems with tight resource constraints
- you want complete control over behavior
- crash forensics is critical
- you value explicitness and predictability

---

### spdlog Design Characteristics

**Core Principles**:
1. **Ease of use**: sensible defaults, minimal configuration
2. **Modern C++**: uses C++11/14/17 features
3. **Minimal dependencies**: header-only option, uses fmt

**When to Choose**:
- general-purpose applications
- you want fast logging with minimal setup
- header-only is valuable
- active development and community matter

---

### Boost.Log Philosophy

**Core Principles**:
1. **Feature completeness**: every possible logging feature
2. **Industrial strength**: designed for large enterprises
3. **Configurability**: everything is customizable
4. **Integration**: part of Boost ecosystem

**When to Choose**:
- large enterprise applications
- you need every advanced feature
- already using Boost
- compile time and binary size don't matter

---

## Migration Guide

### From spdlog to Nova

**Conceptual Mapping**:
- spdlog logger → Nova tag
- spdlog sink → Nova Sink
- spdlog async → Nova MemoryPoolAsyncSink
- spdlog pattern → Nova FormattingSink

**Key Differences**:
1. tags are compile-time types, not runtime strings
2. binding is explicit via ScopedConfigurator
3. record builders chosen at log site (TRUNC/CONT/STREAM)
4. no global registry of loggers

---

### From log4cpp to Nova

**Conceptual Mapping**:
- log4cpp Category → Nova tag
- log4cpp Appender → Nova Sink
- log4cpp Priority → Tag filtering via FilterSink
- log4cpp Layout → FormattingSink

**Key Differences**:
1. No hierarchical categories (use separate tags)
2. No runtime configuration files (explicit C++ code)
3. No priority levels (implement via filtering if needed)

---

### From glog to Nova

**Conceptual Mapping**:
- LOG(INFO) → NOVA_LOG(Tag)
- VLOG(n) → Separate tags or FilterSink
- CHECK macros → Not provided (use assertions)

**Key Differences**:
1. no global state (per-tag configuration)
2. no automatic file logging (explicit sink setup)
3. no built-in flag parsing (explicit configuration)

---

## Performance

For measured benchmark results covering Nova, spdlog, and Quill across guaranteed delivery latency and async throughput at 1–8 threads, see [docs/BENCHMARKS.md](BENCHMARKS.md).  Performance figures for other libraries in this document were not measured and are omitted.

---

## Licensing Considerations

| Library | License | Commercial Use | Static Linking | Attribution Required |
|---------|---------|----------------|----------------|---------------------|
| Nova/Flare | BSD-3 | ✅ Free | ✅ Yes | ⚠️ Yes (copyright notice) |
| Quill | MIT | ✅ Free | ✅ Yes | ⚠️ Yes (copyright notice) |
| spdlog | MIT | ✅ Free | ✅ Yes | ⚠️ Yes (copyright notice) |
| log4cpp | LGPL 2.1 | ⚠️ Restricted | ❌ Problematic (static linking) | ✅ Yes |
| log4cplus | Apache 2.0 / BSD-2 | ✅ Free | ✅ Yes | ✅ Yes |
| easylogging++ | MIT | ✅ Free | ✅ Yes | ⚠️ Yes (copyright notice) |
| Boost.Log | Boost | ✅ Free | ✅ Yes | ⚠️ Yes (copyright notice) |
| glog | BSD-3 | ✅ Free | ✅ Yes | ⚠️ Yes (copyright notice) |
| NanoLog | BSD-3 | ✅ Free | ✅ Yes | ⚠️ Yes (copyright notice) |

**LGPL Note**: log4cpp (LGPL 2.1) and P7 (LGPL 3.0) require that if you statically link, you must provide object files to allow users to relink with a different version.  This is often impractical for commercial software.  log4cplus (Apache 2.0 / BSD-2) does not have this restriction.

---

## Unique Selling Points

### What Nova/Flare Does That Others Don't

1. **Async-Signal-Safe Crash Logging** (Flare)
   - only library truly safe for signal handlers
   - TLV encoding for partial record recovery
   - torn write detection

2. **Compile-Time Tag Routing**
   - zero runtime overhead for disabled tags
   - type-safe log routing
   - library-friendly (no global state per tag)

3. **Explicit Record Builder Choice**
   - TRUNC, CONT, STREAM variants
   - choose performance vs. completeness per call site
   - no hidden buffering behavior

4. **Zero-Allocation Guarantee**
   - TRUNC and CONT record builders avoid heap (STREAM uses std::ostringstream)
   - deterministic memory usage
   - real-time system suitable

5. **Philosophy of Explicitness**
   - no magic, no hidden costs
   - every decision is visible
   - testable, predictable

---

## Recommendations by Use Case

### Choose Nova + Flare if:
- ✅ building safety-critical systems
- ✅ real-time or embedded systems
- ✅ need crash forensics
- ✅ want zero heap allocation
- ✅ value explicitness and determinism
- ✅ building a library (avoid global state)
- ✅ prefer not to categorize logs by severity

### Choose Quill if:
- ✅ ultra-low frontend enqueue latency is critical
- ✅ high-throughput multi-producer async logging
- ✅ `{fmt}` formatting is desired
- ✅ single backend thread model fits your architecture
- ✅ thread lifecycle complexity is manageable

---

### Choose spdlog if:
- ✅ general-purpose application logging
- ✅ want ease of use with good performance
- ✅ like header-only libraries
- ✅ `{fmt}` formatting
- ✅ want an active community

### Choose NanoLog if:
- ✅ ultra-low latency is critical (< 100ns)
- ✅ can tolerate offline log processing
- ✅ high-frequency trading or similar
- ✅ logging overhead dominates your profile

### Choose glog if:
- ✅ like Google's proven approach
- ✅ need simple, battle-tested logging
- ✅ want built-in crash handling (though not signal-safe)
- ✅ building applications (not libraries)

### Choose Boost.Log if:
- ✅ need every advanced feature
- ✅ already using Boost extensively
- ✅ building large enterprise systems
- ✅ compile time and binary size are not concerns

### Choose easylogging++ if:
- ✅ want absolute simplest setup (single header)
- ✅ small to medium project
- ✅ performance is not critical
- ✅ rapid prototyping
- ✅ archive status not a concern

### Avoid log4cpp:
log4cpp is not recommended for new projects.  If you are maintaining an existing log4cpp codebase, consider migrating to log4cplus (compatible API, actively maintained) or to Nova.  See the migration guide for mapping log4cpp concepts to Nova equivalents.

---

## Feature Matrix

The following tables provide additional feature detail for reference. The summary comparison tables near the top of this document are better suited for library selection decisions.

### Logging Capabilities

| Feature | Nova/Flare | spdlog | Quill | log4cplus | easylog++ | Boost.Log | glog | NanoLog |
|---------|-----------|--------|-------|---------|-----------|-----------|------|---------|
| Severity levels | Custom | ✅ 6 levels | ✅ Custom | ✅ 10 levels | ✅ 9 levels | ✅ Custom | ✅ 4 levels | ✅ 5 levels |
| Custom tags | ✅ Compile-time | ⚠️ Runtime | ❌ No | ⚠️ Categories | ⚠️ Runtime | ✅ Attributes | ❌ No | ❌ No |
| Structured logging (JSON) | ✅ JsonFormatter | ⚠️ Via custom | ✅ JsonSink | ❌ No | ❌ No | ✅ Yes | ❌ No | ❌ No |
| Conditional logging | ✅ Yes | ✅ Yes | ✅ Yes | ✅ Yes | ✅ Yes | ✅ Yes | ✅ Yes | ❌ No |
| Async logging | ✅ AsyncQueue | ✅ Yes | ✅ Always | ✅ Yes | ❌ No | ✅ Yes | ❌ No | ✅ Always |
| Message formatting | `<<` operators; Formatter interface | `{fmt}` placeholders | `{fmt}` placeholders (backend) | `<<` / printf-style | `<<` operators | `<<` operators + attributes | `<<` operators | Printf-style; format strings as compile-time IDs |
| Format strings | ⚠️ Via sink | ✅ fmt | ✅ fmt | ⚠️ Basic | ✅ Good | ✅ Excellent | ⚠️ Basic | ✅ Printf-style |

### Output Destinations

| Destination | Nova/Flare | spdlog | Quill | log4cplus | easylog++ | Boost.Log | glog | NanoLog |
|-------------|-----------|--------|-------|---------|-----------|-----------|------|---------|
| Console | ✅ Yes | ✅ Yes | ✅ Yes | ✅ Yes | ✅ Yes | ✅ Yes | ✅ Yes | ❌ No |
| File | ✅ Yes | ✅ Yes | ✅ Yes | ✅ Yes | ✅ Yes | ✅ Yes | ✅ Yes | ✅ Yes |
| Rotating files | ✅ Yes | ✅ Yes | ✅ Yes | ✅ Yes | ✅ Yes | ✅ Yes | ✅ Yes | ❌ No |
| Syslog | ✅ Yes | ✅ Yes | ⚠️ Custom | ✅ Yes | ❌ No | ✅ Yes | ✅ Yes | ❌ No |
| Network | ⚠️ Custom | ⚠️ Custom | ⚠️ Custom | ✅ Yes | ❌ No | ✅ Yes | ❌ No | ❌ No |
| Custom sinks | ✅ Yes | ✅ Yes | ✅ Yes | ✅ Yes | ⚠️ Limited | ✅ Yes | ⚠️ Limited | ❌ No |

### Advanced Features

| Feature | Nova/Flare | spdlog | Quill | log4cplus | easylog++ | Boost.Log | glog | NanoLog |
|---------|-----------|--------|-------|---------|-----------|-----------|------|---------|
| Compile-time disable | ✅ Per-tag (zero binary footprint) | ⚠️ Global (`SPDLOG_ACTIVE_LEVEL`) | ⚠️ Global (`QUILL_COMPILE_ACTIVE_LOG_LEVEL`) | ❌ No | ⚠️ Global (`ELPP_DISABLE_*`) | ❌ No | ⚠️ Via `GOOGLE_STRIP_LOG` | ❌ No |
| Runtime disable | ✅ Per-tag (unbind sink) | ✅ Per-logger level | ✅ Per-logger level | ✅ Per-category level | ✅ Yes | ✅ Filter chains | ✅ Per-severity level | ⚠️ Global level only (`SILENT_LOG_LEVEL`) |
| Heap usage | ✅ None in core path | ⚠️ Yes ({fmt} + queue) | ⚠️ Frontend minimal; backend uses {fmt} | ❌ Extensive | ❌ Extensive | ❌ Extensive | ⚠️ Moderate | ✅ None in hot path |
| Exceptions | ✅ None | ⚠️ Optional (`SPDLOG_NO_EXCEPTIONS`) | ⚠️ Compiler-controlled (adapts to `-fno-exceptions`) | ❌ Uses exceptions | ⚠️ Limited | ❌ Uses exceptions | ✅ None in hot path | ⚠️ File operations only |
| Filtering | ✅ FilterSink | ✅ Yes | ⚠️ Basic | ✅ Yes | ⚠️ Basic | ✅ Advanced | ⚠️ VLOG | ❌ No |
| Custom formatting | ✅ FormattingSink | ✅ Yes | ✅ Yes ({fmt} patterns) | ⚠️ Limited | ⚠️ Limited | ✅ Excellent | ❌ No | ❌ No |
| Context/MDC | ⚠️ Custom | ⚠️ Custom | ⚠️ Custom | ✅ Yes | ❌ No | ✅ Yes | ❌ No | ❌ No |
| Crash safety | ✅ Flare | ❌ No | ⚠️ Optional signal handler | ❌ No | ❌ No | ⚠️ Partial | ⚠️ Partial | ❌ No |
| Binary logging | ⚠️ Flare TLV (crash forensics only) | ❌ No | ❌ No | ❌ No | ❌ No | ⚠️ Custom | ❌ No | ✅ Yes |
| Log sampling | ⚠️ Custom | ⚠️ Custom | ❌ No | ❌ No | ❌ No | ⚠️ Custom | ❌ No | ❌ No |

---

## Conclusion

**Nova + Flare occupy a unique niche** in the C++ logging ecosystem:

**Strengths**:
- unmatched crash safety with Flare's async-signal-safe design
- zero-allocation guarantees for real-time systems
- compile-time routing for library development
- explicit, predictable behavior
- small footprint

**Trade-offs**:
- more verbose than some alternatives (explicit configuration)
- fewer out-of-box formatting options (by design)
- smaller community and ecosystem (newer project)
- learning curve for compile-time tag system

**Best Alternative Depending on Priorities**:
- **For ease of use**: spdlog
- **For ultra-low frontend latency**: Quill
- **For ultra-low latency**: NanoLog
- **For feature completeness**: Boost.Log
- **For simplicity**: easylogging++ or glog
- **For crash safety**: no real alternative to Flare

**The Bottom Line**: 
If you're building systems where **safety, determinism, and crash logging** matter more than convenience features, Nova/Flare are the best choice.  For general application logging where ease-of-use and community support are priorities, spdlog is hard to beat.  For everything else, evaluate based on your specific requirements.

---

## References & Links

- **Nova/Flare**: https://github.com/kmac-13/nova
- **Quill**: https://github.com/odygrd/quill
- **spdlog**: https://github.com/gabime/spdlog
- **log4cpp**: http://log4cpp.sourceforge.net/ (legacy, not recommended for new projects)
- **log4cplus**: https://github.com/log4cplus/log4cplus
- **easylogging++**: https://github.com/amrayn/easyloggingpp
- **Boost.Log**: https://www.boost.org/doc/libs/release/libs/log/
- **glog**: https://github.com/google/glog
- **NanoLog**: https://github.com/PlatformLab/NanoLog
