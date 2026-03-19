# Nova Examples

This directory contains complete working examples demonstrating Nova's features.

## Building and Running

Each example is a standalone program. To build:

```bash
# Example 1: Basic Usage
g++ -std=c++17 -I../../include 01_basic_usage/main.cpp -o basic_usage
./basic_usage

# Example 2: Multiple Sinks
g++ -std=c++17 -I../../include 02_multiple_sinks/main.cpp -o multiple_sinks
./multiple_sinks

# Example 3: Custom Clock
g++ -std=c++17 -I../../include 03_custom_clock/main.cpp -o custom_clock
./custom_clock

# Example 4: Multithreading
g++ -std=c++17 -I../../include -pthread 04_multithreading/main.cpp -o multithreading
./multithreading

# Example 5: Filtering
g++ -std=c++17 -I../../include 05_filtering/main.cpp -o filtering
./filtering

# Example 6: Hierarchical Tags
g++ -std=c++17 -I../../include 06_hierarchical_tags/main.cpp -o hierarchical_tags
./hierarchical_tags

# Example 7: Flare Emergency Logging
g++ -std=c++17 -I../../include -I../../flare/include 07_flare/main.cpp -o flare_example
./flare_example

# Example 8: Bare-Metal
g++ -std=c++17 -I../../include 08_bare_metal/main.cpp -o bare_metal
./bare_metal

# Example 9: Diagnostics
g++ -std=c++17 -I../../include 09_diagnostics/main.cpp -o diagnostics
./diagnostics
```

## Examples Overview

### 01_basic_usage
**Demonstrates**: core concepts
- defining a tag
- specializing logger_traits
- binding a sink
- writing log messages with NOVA_LOG macro

**Best for**: first-time users, understanding fundamentals

### 02_multiple_sinks
**Demonstrates**: routing flexibility
- binding different sinks to different tags
- using CompositeSink for fan-out
- using NullSink to disable at runtime
- writing to both console and file

**Best for**: understanding sink routing strategies

### 03_custom_clock
**Demonstrates**: timestamp customization
- using different clocks (steady, system, custom)
- per-tag time source configuration
- custom time sources (frame counters, etc.)

**Best for**: games, simulations, systems needing custom timing

### 04_multithreading
**Demonstrates**: thread safety
- using SynchronizedSink for thread-safe logging
- logging from multiple threads
- preventing interleaved output

**Best for**: multi-threaded applications

### 05_filtering
**Demonstrates**: runtime filtering
- FilterSink for dynamic message filtering
- filter by tag, message content, or source
- chaining multiple filters
- composing filter logic

**Best for**: applications needing runtime log control

### 06_hierarchical_tags
**Demonstrates**: traditional logging model
- using HierarchicalTag template
- organizing by subsystem + severity
- familiar Debug/Info/Warning/Error model
- type-safe severity checking

**Best for**: teams familiar with traditional loggers

### 07_flare
**Demonstrates**: emergency crash logging
- using Flare's async-signal-safe EmergencySink
- binary serialization for crash dumps
- tag dictionary generation
- reading crash logs with Python tools
- logging from signal handlers

**Best for**: applications requiring crash forensics, safety-critical systems

### 08_bare_metal
**Demonstrates**: bare-metal/embedded usage
- NOVA_BARE_METAL configuration
- custom timestamp implementation
- custom UART sink
- zero heap allocation
- no standard library dependencies

**Best for**: embedded systems, RTOS environments, bare-metal ARM

### 09_diagnostics
**Demonstrates**: platform auto-detection
- NOVA_ENABLE_DIAGNOSTICS mode
- automatic feature detection
- runtime configuration checking
- platform identification
- verifying detected capabilities

**Best for**: porting to new platforms, troubleshooting compilation issues

## Common Patterns

### Pattern 1: Single Sink to Console
```cpp
OStreamSink console(std::cout);
ScopedConfigurator config;
config.bind<MyTag>(&console);
NOVA_LOG(MyTag) << "Message";
```

### Pattern 2: Multiple Tags, Multiple Sinks
```cpp
OStreamSink console(std::cout);
OStreamSink errorFile(errorStream);

config.bind<DebugTag>(&console);
config.bind<ErrorTag>(&errorFile);
```

### Pattern 3: Fan-Out with CompositeSink
```cpp
CompositeSink composite;
composite.add(&console);
composite.add(&file);

config.bind<MyTag>(&composite);  // Goes to both
```

### Pattern 4: Thread-Safe Logging
```cpp
OStreamSink base(std::cout);
SynchronizedSink threadSafe(base);

config.bind<MyTag>(&threadSafe);
// Now safe from multiple threads
```

### Pattern 5: Runtime Filtering
```cpp
auto filter = [](const Record& r) {
    return strstr(r.message, "ERROR") != nullptr;
};

FilterSink filtered(console, filter);
config.bind<MyTag>(&filtered);  // Only errors
```

### Pattern 6: Bare-Metal Configuration
```cpp
#define NOVA_BARE_METAL
#define NOVA_ASSERT(x) do { if (!(x)) { halt(); } } while(0)

namespace kmac { namespace nova { namespace platform {
    std::uint64_t steadyNanosecs() noexcept {
        return DWT->CYCCNT * (1000000000ULL / SystemCoreClock);
    }
}}}

#include <kmac/nova/logger.h>

// Custom sink (e.g., UART)
class UartSink : public Sink { /* ... */ };
```

### Pattern 7: Emergency Crash Logging
```cpp
#include <kmac/flare/emergency_sink.h>

kmac::flare::EmergencySink emergency("crash.bin", tagDict, tagCount);
config.bind<CrashTag>(&emergency);

// Safe to call from signal handlers
signal(SIGSEGV, crash_handler);

void crash_handler(int sig) {
    NOVA_LOG_TRUNC(CrashTag) << "Crash: signal " << sig;
    // Emergency sink is async-signal-safe
}
```

## Next Steps

After working through these examples:

1. **Read the main README** - understand Nova's design philosophy
2. **Explore the samples directory** - see additional sink implementations
3. **Review performance_metrics.h** - verify zero-cost abstractions
4. **Check out Flare** - learn about crash-safe emergency logging

## Tips

- start with example 01 and work through in order
- each example is self-contained and fully documented
- examples show both what to do and what NOT to do
- all examples compile cleanly with -Wall -Wextra -Werror
- examples demonstrate best practices, not just features
- example 07 (Flare) requires building Flare library first
- example 08 (Bare-Metal) demonstrates embedded concepts on desktop
- example 09 (Diagnostics) is useful when porting to new platforms
