# Nova - Domain-Based Logging System

[![License](https://img.shields.io/badge/license-BSD--3-blue.svg)](LICENSE)
[![C++11+](https://img.shields.io/badge/C%2B%2B-11%2B-blue.svg)](https://en.cppreference.com/w/cpp/11)
[![Version](https://img.shields.io/badge/version-1.0.0-blue.svg)](libs/nova/include/kmac/nova/version.h)
[![Sanitizers](https://github.com/kmac-13/nova/actions/workflows/sanitizers-asan.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/sanitizers-asan.yml)
[![Bare-Metal](https://github.com/kmac-13/nova/actions/workflows/build-bare-metal-test.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/build-bare-metal-test.yml)
[![RTOS](https://github.com/kmac-13/nova/actions/workflows/build-rtos-test.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/build-rtos-test.yml)
[![Hosted Build](https://github.com/kmac-13/nova/actions/workflows/build-hosted.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/build-hosted.yml)
[![Android](https://github.com/kmac-13/nova/actions/workflows/build-android-test.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/build-android-test.yml)
[→ all CI workflows](docs/CI.md)

Nova is a domain-based C++ logging system built for **safety-critical, real-time, and embedded environments**.  It uses compile-time type-based routing instead of runtime string registries, enabling zero-cost disabled logs and deterministic resource usage.

---

## Quick Start

The fastest path to console logging is `QuickStart`, which configures a synchronized console logger with ISO 8601 formatted timestamps and severity-level tags:

```cpp
#include <kmac/nova/extras/quick_start.h>

int main()
{
    kmac::nova::extras::QuickStart logging;

    NOVA_LOG_INFO()  << "Application started";
    NOVA_LOG_WARN()  << "Disk space below 10%";
    NOVA_LOG_ERROR() << "Connection refused on port " << 8080;
}
```

Output:
```
2025-06-01T14:22:01.432Z [INFO] main.cpp:8 main - Application started
2025-06-01T14:22:01.432Z [WARN] main.cpp:9 main - Disk space below 10%
2025-06-01T14:22:01.432Z [ERROR] main.cpp:10 main - Connection refused on port 8080
```

`QuickStart` is intended for prototypes and early-stage product development.  Production systems should define explicit domain tags and bind them to application-specific sinks.  For a detailed example of defining and logging non-severity domains, see: Domain-Based Routing.

---

## Domain-Based Routing

Nova treats logging domains (subsystems, features, or phases) as first-class C++ types. This allows disabled logging domains to be completely eliminated at compile time.

Severity is just one possible domain.  Nova's routing is not constrained to any particular taxonomy — tags can represent subsystems, execution phases, transaction types, hardware peripherals, or anything else that makes sense for your architecture.

```cpp
struct MotionPlanner {};  // domain
NOVA_LOGGER_TRAITS( MotionPlanner, MOTION, true, kmac::nova::TimestampHelper::steadyNanosecs );  // true indicates enabled

int main()
{
    ...
    kmac::nova::ScopedConfigurator config;
    config.bind<MotionPlanner>(&mySink);

    NOVA_LOG(MotionPlanner) << "Planning trajectory...";
}
```

Each domain routes to an independent sink.  Disabling a `SensorFusion` domain at compile time removes all logging calls for that domain from the binary — no branches, no string literals, no sink lookups.

Because domains are C++ types, the compiler catches misspelled domain names — a typo in a string-based logger silently succeeds at runtime; a typo in a type name fails at compile time.

Libraries can define their own logging domains without interfering with application or third-party logging configuration. Each domain is an independent type with its own sink binding, so there is no shared global registry for domains to collide in.

### Combining domains with severity

If severity is important, it can be included in the type, e.g.:
```cpp
struct MotionPlannerDebug {};
```

Additionally, `HierarchicalTag` in Nova Extras can be used when you want both subsystem identity and severity in a single tag:
```cpp
struct Debug {};
struct Info  {};

struct MotionPlanner {};

using MotionPlannerDebug = HierarchicalTag< MotionPlanner, Debug >;
using MotionPlannerInfo  = HierarchicalTag< MotionPlanner, Info  >;
```

---

## Why Nova?

Most logging libraries organize behavior around runtime severity levels (e.g. DEBUG, INFO, WARN, ERROR) and string-named loggers.  Nova, instead, treats logging domains as **first-class C++ types**.

That architectural choice enables capabilities that are difficult or impossible in traditional logging systems:

| **Feature** | **Nova** | **Traditional logging libraries** |
|---|---|---|
| **Routing** | **Compile-time** (Type-based) | Runtime (string-based) |
| **Disabled Logs** | **Zero-cost** (Optimized out) | Runtime branching/filtering |
| **Memory** | **Zero heap** (Fixed stack buffers) | Often allocator-dependent |
| **Crash Safety** | **Async-signal-safe** (Flare) | Rare or flush only |

Nova is designed for systems where logging behavior must remain analyzable, bounded, and predictable under failure conditions - including embedded, RTOS, and safety-critical environments.

---

## Design Philosophy

| Principle | Description |
|---|---|
| **Domain-based routing** | Logging should reflect application structure rather than forcing subsystems into global severity categories. |
| **Deterministic behavior** | No heap allocation or exceptions in core logging paths. |
| **Explicit tradeoffs** | Buffer and queue sizes, synchronous vs asynchronous delivery, blocking, and fan-out behavior are visible and configurable at the call site. |
| **Compile-time configuration** | Logging configuration is resolved at compile time wherever practical — disabled domains produce no binary footprint. |
| **Production-oriented design** | Optimised for real workloads and operational predictability, not synthetic benchmarks. |
| **Modern C++** | C++11 minimum; C++17 recommended for `if constexpr` language guarantees on disabled tags. |
| **Performance-conscious** | Logging overhead remains predictable and low, especially in latency-sensitive and multi-threaded systems. |

---

## Components

Nova is split into three independent libraries.  Include only what your project needs.

| **Library** | **Purpose** |
|---|---|
| **Nova core** | Compile-time routing, tags, builders, sink interface, platform abstraction |
| **Nova Extras** | Utility builders, formatters, buffers, composable sinks |
| **Nova Flare** | Crash-safe emergency logging and forensics tooling |

### Highlights

* compile-time domain routing
* zero-cost disabled logs
* fixed-memory operation
* async logging
* structured formatting
* bare-metal + RTOS support
* async-signal-safe crash logging
* crash forensics tooling

---

## Safety-Critical Design

Nova was designed for deterministic and analyzable logging behavior in embedded and safety-critical systems.

Key properties include:
* compile-time elimination of disabled domains
* fixed-size stack-buffer logging paths
* bounded async queues with predictable backpressure
* no global string-based logger registry
* async-signal-safe crash logging (Flare)
* partial-record recovery after crashes
* bare-metal and RTOS support
* explicit configuration with no hidden runtime behavior

These properties support use in environments where traceability and bounded behavior matter, including:
* DO-178C
* IEC 61508
* ISO 26262

See:
* docs/SAFETY_CRITICAL_GUIDELINES.md
* docs/FLARE_README.md
* docs/BARE_METAL_GUIDE.md

---

## Performance

On the reference hardware (Ryzen 9 6900HX, MinGW GCC Release, C++17):

- **Guaranteed delivery**: Nova `SynchronizedSink` writes 1M records to disk in **190 ns/msg** at 1 thread — 2.7× faster than spdlog sync (508 ns) and 3.6× faster than Quill async (693 ns).
- **Async throughput**: Nova delivers **2.98M records/s** to a file flat across 1–8 threads with a 256 KB pool. spdlog degrades from 1.60M/s at 1 thread to 167K/s at 8 threads; Quill degrades from 2.39M/s to 1.52M/s.
- **No-I/O async throughput**: in counting-sink tests with pool memory matched to Quill's per-thread queue allocation, Nova's backend delivered **~9M/s at 1 thread** vs Quill's ~6.8M/s. Quill's per-thread SPSC queues give it an advantage at 2–4 threads; Nova recovers at 8 threads (4.4M/s vs 3.9M/s). Nova maintains **zero per-record heap allocation** across all configurations. See [docs/BENCHMARKS.md](docs/BENCHMARKS.md) for the full multi-thread breakdown.

For full methodology, queue sizing rationale, counting-sink isolation results, and pool size analysis:

* [docs/BENCHMARKS.md](docs/BENCHMARKS.md)
* [docs/NOVA_VS_ALTERNATIVES.md](docs/NOVA_VS_ALTERNATIVES.md)

---

## Platform and Compiler Support

| Platform | Status |
|---|---|
| Linux (hosted) | CI-verified |
| Windows (MinGW) | CI-verified |
| Android | CI-verified |
| FreeRTOS / QEMU (ARM Cortex-M3) | CI-verified |
| Bare-metal ARM Cortex-M3 / QEMU | CI-verified |

| Compiler | Minimum |
|---|---|
| GCC | 5.0 |
| Clang | 3.3 |
| MSVC | 2015 |
| ARM Compiler | 6 |

C++11 is the minimum required standard.  C++17 is recommended for the `if constexpr` language guarantee on disabled tags and for `std::to_chars` availability.

---

## CMake Integration

### add_subdirectory

```cmake
set( NOVA_BUILD_EXTRAS ON  CACHE BOOL "" FORCE )
set( NOVA_BUILD_FLARE  ON  CACHE BOOL "" FORCE )
set( NOVA_BUILD_TESTS  OFF CACHE BOOL "" FORCE )
add_subdirectory( external/nova )

target_link_libraries( myapp PRIVATE Nova::Core Nova::Extras Nova::Flare )
```

### FetchContent

```cmake
include( FetchContent )
FetchContent_Declare( nova
    GIT_REPOSITORY https://github.com/kmac-13/nova.git
    GIT_TAG        main
)
set( NOVA_BUILD_EXTRAS ON  CACHE BOOL "" FORCE )
set( NOVA_BUILD_FLARE  ON  CACHE BOOL "" FORCE )
set( NOVA_BUILD_TESTS  OFF CACHE BOOL "" FORCE )
FetchContent_MakeAvailable( nova )

target_link_libraries( myapp PRIVATE Nova::Core Nova::Extras Nova::Flare )
```

Nova core is header-only; no compilation is required if you use only `Nova::Core`.  See `docs/CMAKE_INTEGRATION.md` for `find_package`, bare-metal cross-compilation toolchain usage, and build option reference.

---

## Documentation

| Document | Contents |
|---|---|
| [`docs/REPO_STRUCTURE.md`](docs/REPO_STRUCTURE.md) | Repository organization |
| [`docs/CI.md`](docs/CI.md) | All 16 CI workflows — build targets, sanitizers, static analysis, fuzzing |
| [`docs/NOVA_README.md`](docs/NOVA_README.md) | Nova core design, tags, records, sinks, configurator |
| [`docs/NOVA_BUILDERS_README.md`](docs/NOVA_BUILDERS_README.md) | Truncating vs continuation vs streaming builders |
| [`docs/NOVA_EXTRAS_REFERENCE.md`](docs/NOVA_EXTRAS_REFERENCE.md) | Full Nova Extras component reference |
| [`docs/FLARE_README.md`](docs/FLARE_README.md) | Flare architecture, signal handler setup, TLV format |
| [`docs/FLARE_USE_CASES.md`](docs/FLARE_USE_CASES.md) | Crash forensics use cases and patterns |
| [`docs/BARE_METAL_GUIDE.md`](docs/BARE_METAL_GUIDE.md) | Bare-metal and RTOS porting |
| [`docs/NOVA_VS_ALTERNATIVES.md`](docs/NOVA_VS_ALTERNATIVES.md) | Detailed per-library comparison with code examples |
| [`docs/LIBRARY_COMPARISON.md`](docs/LIBRARY_COMPARISON.md) | Feature matrix vs spdlog, Quill, Boost.Log, glog, etc. |
| [`docs/CMAKE_INTEGRATION.md`](docs/CMAKE_INTEGRATION.md) | CMake integration patterns and build options |
| [`docs/SAFETY_CRITICAL_GUIDELINES.md`](docs/SAFETY_CRITICAL_GUIDELINES.md) | Per-component guidance for DO-178C / IEC 61508 / ISO 26262 |
| [`docs/LIBRARY_MIGRATION.md`](docs/LIBRARY_MIGRATION.md) | Migration guides from spdlog, glog, and others |
| [`docs/CPP_VERSION_COMPATIBILITY.md`](docs/CPP_VERSION_COMPATIBILITY.md) | C++11/14/17 compatibility notes |
| [`docs/CONFIG_AUTO_DETECTION_GUIDE.md`](docs/CONFIG_AUTO_DETECTION_GUIDE.md) | Platform auto-detection and `NOVA_DIAGNOSTICS` |
| [`docs/BENCHMARKS.md`](docs/BENCHMARKS.md) | Performance and comparisons with other popular libraries |

---

## License

BSD 3-Clause — see [LICENSE](LICENSE).
