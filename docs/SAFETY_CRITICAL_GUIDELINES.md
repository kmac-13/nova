# Safety-Critical Systems Guidelines for Nova/Flare

**Version**: 1.0
**Date**: February 2026
**Audience**: Engineers integrating Nova/Flare into safety-critical systems requiring certification

---

## Table of Contents

1. [Overview](#overview)
2. [Applicable Standards](#applicable-standards)
3. [Feature Qualification Matrix](#feature-qualification-matrix)
4. [Recommended Subsets by Safety Level](#recommended-subsets-by-safety-level)
5. [Implementation Guidelines](#implementation-guidelines)
6. [Verification Requirements](#verification-requirements)
7. [Documentation Requirements](#documentation-requirements)
8. [Hazard Analysis](#hazard-analysis)
9. [Example Configurations](#example-configurations)
10. [Checklist](#checklist)

---

## Overview

This document provides guidelines for using Nova, Nova Extras, and Flare in safety-critical systems that require certification under standards such as DO-178C, IEC 61508, ISO 26262, or IEC 62304.

### Key Principles

1. **Qualified Subset Approach**: Use only verified, deterministic features
2. **Explicit Configuration**: Document all configuration decisions
3. **Traceability**: Maintain requirements-to-code-to-test traceability
4. **Determinism**: Ensure predictable behavior and bounded execution
5. **Defense in Depth**: Add safety barriers around logging operations

### Safety Philosophy

**Logging failures should never cause system failures.** Logging is typically a diagnostic feature, not a safety function. Design your logging architecture such that:

- Logging errors are contained (don't propagate)
- Critical functions continue if logging fails
- Logging doesn't interfere with timing requirements
- Resource exhaustion in logging is impossible

---

## Applicable Standards

### Aviation (DO-178C)

**Relevance**: Nova/Flare would typically be classified as:
- **Software Level**: Depends on what is being logged
  - Level A/B: If logging failure could lead to catastrophic/hazardous conditions
  - Level C/D/E: For diagnostic logging that doesn't affect safety

**Key Requirements**:
- Source code review
- Static analysis (e.g., MISRA C++)
- Structural coverage analysis
- Traceability from requirements through verification
- Configuration management

### Industrial (IEC 61508)

**Relevance**: Typically used at SIL 1-3 for diagnostic logging

**Key Requirements**:
- Failure modes analysis
- Systematic capability evaluation
- Evidence of proven-in-use (if claiming)
- Diverse redundancy (if needed)

### Automotive (ISO 26262)

**Relevance**: ASIL A-D depending on safety goals

**Key Requirements**:
- ASIL decomposition (if applicable)
- Fault injection testing
- MISRA C++ compliance
- Safety analysis (FMEA, FTA)

### Medical (IEC 62304)

**Relevance**: Class A/B/C software depending on risk

**Key Requirements**:
- Risk analysis
- Software unit testing
- Integration testing
- Verification and validation

---

## Feature Qualification Matrix

### Nova Core Features

| Feature | Complexity | Allocation | Deterministic | Recommended Safety Level |
|---------|-----------|------------|---------------|-------------------------|
| **Tags & Traits** | Low | None | Yes | All levels |
| **Logger<Tag>** | Low | None | Yes | All levels |
| **TruncatingRecordBuilder** | Low | None (stack) | Yes | All levels |
| **ContinuationRecordBuilder** | Low | None (stack) | Yes | All levels |
| **NullRecordBuilder** (extras/null_logging.h) | Minimal | None | Yes | All levels |
| **ScopedConfigurator** | Low | Heap (vector) | Yes* | A-D (with restrictions) |
| **Macros (NOVA_LOG)** | Low | None | Yes | All levels |

*Note: ScopedConfigurator uses std::vector which allocates on heap. For highest safety levels, consider static configuration.

### Nova Extras Features

| Feature | Complexity | Allocation | Deterministic | Recommended Safety Level |
|---------|-----------|------------|---------------|-------------------------|
| **NullSink** | Minimal | None | Yes | All levels |
| **HierarchicalTag** | Low | None | Yes | All levels |
| **Severities (tags)** | Minimal | None | Yes | All levels |
| **SpinlockSink** | Low | None | Yes | B-D |
| **BoundedCompositeSink** | Low | None (array) | Yes | B-D |
| **OStreamSink** | Low | None | Yes** | C-D (depends on stream) |
| **FormattingSink** | Medium | None | Yes** | C-D |
| **FilterSink** | Low | None | Yes*** | C-D |
| **SynchronizedSink** | Low | None | Yes**** | C-D |
| **FormattingFileSink / ISO8601** | Medium | None (stack buffer) | Yes | C-D |
| **CompositeSink** | Low | Heap (vector) | Yes | D only |
| **MemoryPoolAsyncSink** | Medium | None (pool) | Yes | B-D |
| **MemoryPoolAsyncBatchSink** | Medium | None (pool) | Yes | B-D |
| **RollingFileSink** | High | Heap (file I/O) | No | D only |
| **StreamingRecordBuilder** | Medium | Heap (ostringstream) | No | D only |
| **MultilineFormatter** | Medium | Heap | No | D only |

**Determinism notes:
- **: Depends on underlying std::ostream behavior
- ***: Depends on filter function complexity
- ****: Mutex operations have bounded but non-constant time
Note: Both MemoryPoolAsync* sinks pre-allocate their pools at construction (zero runtime allocation). Consumer thread scheduling is non-deterministic so they are excluded from Level A/B hard real-time calling contexts, but the producer call itself is bounded and safe.

### Flare Features

| Feature | Complexity | Allocation | Deterministic | Recommended Safety Level |
|---------|-----------|------------|---------------|-------------------------|
| **EmergencySink** | Low | None (stack) | Yes | All levels |
| **FileWriter** | Low | None | No***** | C-D |
| **Reader** | Medium | None | Yes | All levels |
| **Scanner** | Low | None | Yes | All levels |
| **TLV encoding** | Low | None | Yes | All levels |

*****: FileWriter uses fwrite/fflush which are not async-signal-safe. For highest safety, use custom IWriter with raw syscalls.

---

## Recommended Subsets by Safety Level

### Level A / SIL 3 / ASIL D (Catastrophic/Hazardous)

**Philosophy**: Minimal, fully verifiable subset only.

**Recommended Components**:
```cpp
// Nova Core Only
- Tags and logger_traits
- Logger<Tag>::bindSink() / unbindSink()
- TruncatingRecordBuilder (stack-based, bounded)
- NullSink (for disabling)
- Static sink configuration (no ScopedConfigurator)

// Flare (Emergency Only)
- EmergencySink with custom IWriter using raw syscalls
- Fixed-size emergency buffer
```

**Prohibited**:
- All heap allocation
- Dynamic sink reconfiguration
- All Nova Extras except NullSink
- Unbounded operations
- Complex formatters

**Example Configuration**:
```cpp
// Static configuration - no runtime changes
namespace SafetyCritical
{
    struct CriticalErrorTag {};
    
    // Compile-time verification
    static_assert(logger_traits<CriticalErrorTag>::enabled, "Must be enabled");
    
    // Fixed buffer sink - no allocation
    class FixedNVRAMSink : public Sink
    {
        static constexpr size_t BUFFER_SIZE = 4096;
        uint8_t buffer[BUFFER_SIZE];
        size_t writePos = 0;
        
    public:
        void process(const Record& record) noexcept override
        {
            // Bounded copy to fixed buffer
            size_t available = BUFFER_SIZE - writePos;
            size_t toCopy = std::min(record.messageSize, available);
            
            if (toCopy > 0)
            {
                std::memcpy(&buffer[writePos], record.message, toCopy);
                writePos += toCopy;
            }
            
            // Never fail - just stop logging when buffer full
        }
    };
    
    // Static instance - initialized at startup
    static FixedNVRAMSink criticalSink;
    
    // Static binding - no runtime changes
    void initializeLogging() noexcept
    {
        Logger<CriticalErrorTag>::bindSink(&criticalSink);
    }
}
```

### Level B / SIL 2 / ASIL C (Severe/Major)

**Philosophy**: Deterministic subset with bounded heap usage.

**Recommended Components**:
```cpp
// Nova Core
- All Level A components
+ ScopedConfigurator (pre-allocate vector to known size)
+ ContinuationRecordBuilder

// Nova Extras
+ NullSink
+ SpinlockSink (preferred for hard real-time; bounded, no kernel calls)
+ SynchronizedSink (acceptable where mutex latency is budgeted)
+ BoundedCompositeSink (fixed-capacity, no heap)
+ MemoryPoolAsyncSink / MemoryPoolAsyncBatchSink (pre-allocated pool, zero runtime alloc;
  producer call is bounded — consumer scheduling is not, so exclude from hard-RT call sites)
+ OStreamSink (to fixed buffers only)
+ FormattingSink / FormattingFileSink (with bounded formatting)

// Flare
+ FileWriter (with restrictions)
```

**Restrictions**:
- Pre-allocate all containers
- Bound all operations
- Use only stack-based buffers
- Verify all formatting paths

**Example Configuration**:
```cpp
namespace SafetyImportant
{
    // Pre-allocated sink vector
    class BoundedConfigurator
    {
        static constexpr size_t MAX_BINDINGS = 10;
        std::array<std::function<void()>, MAX_BINDINGS> unbindFns;
        size_t count = 0;
        
    public:
        template<typename Tag>
        void bind(Sink* sink) noexcept
        {
            if (count < MAX_BINDINGS)
            {
                Logger<Tag>::bindSink(sink);
                unbindFns[count++] = []() { Logger<Tag>::unbindSink(); };
            }
            // else: logged elsewhere as configuration error
        }
        
        ~BoundedConfigurator() noexcept
        {
            for (size_t i = 0; i < count; ++i)
            {
                unbindFns[i]();
            }
        }
    };
    
    // Fixed-size composite sink
    class BoundedCompositeSink : public Sink
    {
        static constexpr size_t MAX_SINKS = 5;
        std::array<Sink*, MAX_SINKS> sinks{};
        size_t count = 0;
        
    public:
        void add(Sink& sink) noexcept
        {
            if (count < MAX_SINKS)
            {
                sinks[count++] = &sink;
            }
        }
        
        void process(const Record& record) noexcept override
        {
            for (size_t i = 0; i < count; ++i)
            {
                if (sinks[i])
                {
                    sinks[i]->process(record);
                }
            }
        }
    };
}
```

### Level C / SIL 1 / ASIL B (Minor)

**Philosophy**: Most features allowed with verification.

**Recommended Components**:
```cpp
// Nova Core
- All components

// Nova Extras
- NullSink, HierarchicalTag, Severities
- SpinlockSink, SynchronizedSink        // thread safety
- BoundedCompositeSink                  // fixed-capacity fan-out
- OStreamSink, FormattingSink, FormattingFileSink / ISO8601, FilterSink

// Flare
- All components
```

**Prohibited at this level**:
- `RollingFileSink` — file I/O duration is unbounded
- `CompositeSink` (heap variant) — prefer `BoundedCompositeSink`
- `StreamingRecordBuilder`, `MultilineFormatter` — heap allocation

**Note on async sinks**: `MemoryPoolAsyncSink` and `MemoryPoolAsyncBatchSink` use
pre-allocated pools and are acceptable at Level C.  The producer call is bounded;
only the consumer thread scheduling is non-deterministic, which is acceptable at
this level.  Dropped records (pool/queue full) must be monitored via `droppedCount()`.

**Restrictions**:
- Verify all execution paths
- Bound all resources
- Document timing characteristics

### Level D-E / QM / ASIL A (Negligible)

**Philosophy**: All features available for diagnostic use.

**Allowed**:
- Full Nova, Nova Extras, and Flare
- Dynamic allocation acceptable
- Complex operations allowed

**Guidelines**:
- Still follow coding standards
- Still verify basic functionality
- Document configuration

---

## Implementation Guidelines

### 1. Resource Management

#### Memory Allocation

**Level A/B**:
```cpp
// DO NOT: Dynamic allocation
config.bind<Tag>(&sink);  // Uses std::vector internally

// DO: Static allocation
static MySink sink;
Logger<Tag>::bindSink(&sink);
```

**Level C/D**:
```cpp
// Acceptable: Dynamic allocation with bounds
CompositeSink composite;
composite.add(sink1);  // Bounded by number of sinks
composite.add(sink2);
```

#### Buffer Sizing

**Always use fixed-size buffers**:
```cpp
// Truncating builder with known buffer size
static constexpr size_t BUFFER_SIZE = 256;
TruncatingRecordBuilder<Tag, BUFFER_SIZE> builder(__FILE__, __FUNCTION__, __LINE__);
builder << "Message";
```

**Verify buffer adequacy**:
```cpp
// Static assertion for buffer size
static_assert(BUFFER_SIZE >= MAX_EXPECTED_MESSAGE, "Buffer too small");
```

### 2. Error Handling

**Logging must never fail the system**:

```cpp
class SafeLoggingSink : public Sink
{
    Sink* downstream;
    std::atomic<uint64_t> errorCount{0};
    
public:
    void process(const Record& record) noexcept override
    {
        try
        {
            if (downstream)
            {
                downstream->process(record);
            }
        }
        catch (...)
        {
            // Log failure is not a system failure
            errorCount.fetch_add(1, std::memory_order_relaxed);
            // Continue - don't propagate
        }
    }
    
    uint64_t getErrorCount() const noexcept
    {
        return errorCount.load(std::memory_order_relaxed);
    }
};
```

### 3. Timing Constraints

**Measure and bound execution time**:

```cpp
// Compile-time timing requirement
template<typename Tag>
void logWithTiming(const char* message)
{
    auto start = TimestampHelper::steadyNanosecs();
    
    TruncatingRecordBuilder<Tag> builder(__FILE__, __FUNCTION__, __LINE__);
    builder << message;
    
    auto elapsed = TimestampHelper::steadyNanosecs() - start;
    
    // Static check or runtime verification
    static constexpr uint64_t MAX_LOG_TIME_NS = 10'000;  // 10 microseconds
    
    if (elapsed > MAX_LOG_TIME_NS)
    {
        // Report timing violation through separate channel
    }
}
```

**Use WCET (Worst Case Execution Time) analysis**:
- Identify all code paths
- Measure maximum execution time
- Document timing budgets
- Verify during testing

### 4. Thread Safety

**Level A/B**:
```cpp
// Option 1: No threading (simplest)
// Each task has own logger/sink

// Option 2: Lock-free (if needed)
class PerThreadSink : public Sink
{
    thread_local static MySink localSink;
    
    void process(const Record& record) noexcept override
    {
        localSink.process(record);
    }
};
```

**Level C/D**:
```cpp
// Mutex-based synchronization acceptable
SynchronizedSink threadSafe(baseSink);
```

### 5. Configuration Management

**Baseline configuration**:
```cpp
// configuration.h - under version control
namespace LoggingConfig
{
    constexpr size_t MESSAGE_BUFFER_SIZE = 256;
    constexpr size_t MAX_SINKS = 5;
    constexpr uint64_t MAX_LOG_TIME_NS = 10'000;
    
    // Compile-time feature selection
    constexpr bool ENABLE_FILTERING = false;
    constexpr bool ENABLE_FORMATTING = true;
    constexpr bool ENABLE_THREAD_SAFETY = true;
}
```

**Version tracking**:
```cpp
// Embed version information
namespace LoggingVersion
{
    constexpr const char* NOVA_VERSION = "1.0.0";
    constexpr const char* CONFIG_VERSION = "2024-01-15";
    constexpr uint32_t CONFIG_HASH = 0x12345678;  // Build-time hash
}
```

### 6. Defensive Programming

**Add safety checks**:
```cpp
class DefensiveSink : public Sink
{
    Sink* downstream;
    
    void process(const Record& record) noexcept override
    {
        // Verify record integrity
        if (!record.message || record.messageSize == 0)
        {
            return;  // Invalid record - don't process
        }
        
        if (record.messageSize > MAX_REASONABLE_SIZE)
        {
            return;  // Suspiciously large - potential corruption
        }
        
        if (!downstream)
        {
            return;  // No downstream - fail safe
        }
        
        // Process only if all checks pass
        downstream->process(record);
    }
};
```

**Sanitize inputs**:
```cpp
template<typename Tag>
void safeLog(const char* message)
{
    // Verify message is valid before logging
    if (!message)
    {
        return;  // Null message - ignore
    }
    
    size_t len = strnlen(message, MAX_MESSAGE_SIZE);
    if (len == 0 || len >= MAX_MESSAGE_SIZE)
    {
        return;  // Invalid length
    }
    
    TruncatingRecordBuilder<Tag> builder(__FILE__, __FUNCTION__, __LINE__);
    builder << message;
}
```

---

## Verification Requirements

### 1. Unit Testing

**Test Coverage Requirements**:

| Safety Level | Statement Coverage | Branch Coverage | MC/DC Coverage |
|--------------|-------------------|-----------------|----------------|
| Level A | 100% | 100% | Required |
| Level B | 100% | 100% | Recommended |
| Level C | 100% | 100% | Not required |
| Level D | As designed | As designed | Not required |

**Required Tests**:

```cpp
// Test 1: Basic functionality
TEST(SafetyLogging, BasicOperation)
{
    CounterSink counter;
    Logger<TestTag>::bindSink(&counter);
    
    NOVA_LOG(TestTag) << "Test";
    
    ASSERT_EQ(counter.count, 1);
}

// Test 2: Buffer overflow protection
TEST(SafetyLogging, BufferOverflow)
{
    std::string largeMessage(10000, 'X');
    
    // Should not crash or allocate
    TruncatingRecordBuilder<TestTag> builder(__FILE__, __FUNCTION__, __LINE__);
    builder << largeMessage;
    
    // Verify truncation occurred safely
}

// Test 3: Null pointer handling
TEST(SafetyLogging, NullHandling)
{
    Logger<TestTag>::bindSink(nullptr);
    
    // Should not crash
    NOVA_LOG(TestTag) << "Test";
}

// Test 4: Concurrent access (if multi-threaded)
TEST(SafetyLogging, ThreadSafety)
{
    const int NUM_THREADS = 10;
    const int LOGS_PER_THREAD = 1000;
    
    CounterSink counter;
    SynchronizedSink sync(counter);
    Logger<TestTag>::bindSink(&sync);
    
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i)
    {
        threads.emplace_back([&]() {
            for (int j = 0; j < LOGS_PER_THREAD; ++j)
            {
                NOVA_LOG(TestTag) << "Thread log";
            }
        });
    }
    
    for (auto& t : threads)
    {
        t.join();
    }
    
    ASSERT_EQ(counter.count, NUM_THREADS * LOGS_PER_THREAD);
}

// Test 5: Timing verification
TEST(SafetyLogging, TimingBounds)
{
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < 1000; ++i)
    {
        NOVA_LOG(TestTag) << "Timing test " << i;
    }
    
    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    auto avgNs = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count() / 1000;
    
    // Verify average time is within bounds
    ASSERT_LT(avgNs, MAX_LOG_TIME_NS);
}
```

### 2. Integration Testing

**Test logging integration with real system**:

```cpp
// Test logging doesn't interfere with timing
TEST(Integration, RealTimePerformance)
{
    // Configure logging as in real system
    setupProductionLogging();
    
    // Run critical task with logging
    auto start = getHighResTime();
    
    criticalTask();  // Includes logging calls
    
    auto elapsed = getHighResTime() - start;
    
    // Verify task still meets timing requirements
    ASSERT_LT(elapsed, TASK_DEADLINE);
}

// Test logging under stress
TEST(Integration, StressConditions)
{
    setupProductionLogging();
    
    // Simulate high load
    for (int i = 0; i < 100000; ++i)
    {
        NOVA_LOG(Tag) << "Stress test " << i;
    }
    
    // Verify system remains stable
    ASSERT_TRUE(systemHealthCheck());
}
```

### 3. Static Analysis

**Required Checks**:

```bash
# MISRA C++ compliance
# Use tools like:
# - PC-lint Plus
# - Coverity
# - Clang-Tidy with MISRA rules

clang-tidy \
    --checks='-*,misra-*,cert-*,cppcoreguidelines-*' \
    --warnings-as-errors='*' \
    your_logging_code.cpp

# Memory safety
# Use tools like:
# - Valgrind
# - AddressSanitizer
# - MemorySanitizer

g++ -fsanitize=address,undefined \
    -fno-omit-frame-pointer \
    your_logging_code.cpp

./a.out  # Should show no errors
```

**Acceptable Deviations**:
- Document any MISRA violations
- Provide justification
- Get approval from safety authority

### 4. Performance Testing

**Measure and document**:

```cpp
// Performance benchmark
void benchmarkLogging()
{
    constexpr int ITERATIONS = 1000000;
    
    // Measure disabled tag (should be ~0)
    auto start = now();
    for (int i = 0; i < ITERATIONS; ++i)
    {
        NOVA_LOG(DisabledTag) << "Should be compiled out";
    }
    auto disabledTime = now() - start;
    
    // Measure NullSink (builder overhead only)
    NullSink null;
    Logger<TestTag>::bindSink(&null);
    
    start = now();
    for (int i = 0; i < ITERATIONS; ++i)
    {
        NOVA_LOG(TestTag) << "Null sink";
    }
    auto nullTime = now() - start;
    
    // Measure real sink
    OStreamSink sink(devNull);
    Logger<TestTag>::bindSink(&sink);
    
    start = now();
    for (int i = 0; i < ITERATIONS; ++i)
    {
        NOVA_LOG(TestTag) << "Real sink";
    }
    auto realTime = now() - start;
    
    // Document results
    std::cout << "Disabled: " << disabledTime / ITERATIONS << " ns/log\n";
    std::cout << "NullSink: " << nullTime / ITERATIONS << " ns/log\n";
    std::cout << "RealSink: " << realTime / ITERATIONS << " ns/log\n";
}
```

---

## Documentation Requirements

### 1. Software Requirements Specification

Document what logging must do:

```
REQ-LOG-001: The system shall log all safety-critical errors
    Trace: HAZARD-005, TEST-LOG-001
    
REQ-LOG-002: Logging shall not interfere with real-time tasks
    Trace: PERF-001, TEST-LOG-010
    
REQ-LOG-003: Log buffer exhaustion shall not cause system failure
    Trace: FAULT-012, TEST-LOG-015
    
REQ-LOG-004: Logging shall be disabled if initialization fails
    Trace: SAFE-008, TEST-LOG-020
```

### 2. Software Design Description

Document how logging is implemented:

```
DESIGN-LOG-001: Tag-based routing
    Implementation: Nova Logger<Tag> template
    Files: nova/include/kmac/nova/logger.h
    Rationale: Compile-time routing eliminates runtime overhead
    
DESIGN-LOG-002: Fixed buffer strategy
    Implementation: TruncatingRecordBuilder<Tag, SIZE>
    Files: nova/include/kmac/nova/truncating_logging.h
    Rationale: Bounded memory usage, no heap allocation
    
DESIGN-LOG-003: Error containment
    Implementation: DefensiveSink wrapper
    Files: safety/defensive_sink.h
    Rationale: Logging failures don't propagate to system
```

### 3. Traceability Matrix

| Requirement | Design | Implementation | Test | Verification |
|-------------|--------|----------------|------|--------------|
| REQ-LOG-001 | DESIGN-LOG-001 | logger.h:45 | TEST-LOG-001 | PASS |
| REQ-LOG-002 | DESIGN-LOG-002 | truncating_logging.h:100 | TEST-LOG-010 | PASS |
| REQ-LOG-003 | DESIGN-LOG-003 | defensive_sink.h:25 | TEST-LOG-015 | PASS |

### 4. Configuration Documentation

```cpp
/**
 * @file logging_config.h
 * @brief Safety-critical logging configuration
 * 
 * Version: 1.0
 * Date: 2024-01-15
 * Author: Safety Team
 * Approved: Chief Engineer
 * 
 * Configuration Rationale:
 * - MESSAGE_BUFFER_SIZE = 256: Sufficient for all expected messages
 * - MAX_SINKS = 3: NVRAM sink, diagnostic sink, emergency sink
 * - ENABLE_FILTERING = false: Filtering adds complexity, not needed
 * - ENABLE_FORMATTING = true: Required for diagnostic output
 * 
 * Deviations from defaults:
 * - Using static configuration instead of ScopedConfigurator (Level A)
 * - Using MemoryPoolAsyncBatchSink for async output (pool pre-allocated, no runtime allocation)
 * 
 * Dependencies:
 * - Nova core v1.0.0
 * - Nova Extras v1.0.0 (subset only)
 * - Flare v1.0.0
 */
```

### 5. Verification Report

```
Test Summary for Logging Subsystem
===================================

Statement Coverage: 100% (1247/1247 statements)
Branch Coverage: 100% (312/312 branches)
MC/DC Coverage: 100% (189/189 decisions)

Unit Tests: 45 tests, all PASS
Integration Tests: 12 tests, all PASS
Stress Tests: 5 tests, all PASS

Static Analysis: 0 errors, 0 warnings (MISRA C++)
Memory Analysis: 0 leaks, 0 invalid accesses (Valgrind)

Performance:
- Disabled tag: 0 ns (compiled out)
- Enabled tag with NullSink: 47 ns average, 92 ns max
- Enabled tag with NVRAMSink: 385 ns average, 1240 ns max
- All within budgeted time of 2000 ns

Timing Jitter Analysis:
- Standard deviation: 15 ns
- 99.9th percentile: 580 ns
- Meets real-time requirements

Resource Usage:
- ROM: 12 KB (code)
- RAM: 8 KB (static buffers)
- Stack: 512 bytes per log call (verified)

Deviations: None

Conclusion: Logging subsystem verified for Level B use
```

---

## Hazard Analysis

### Logging-Related Hazards

| Hazard ID | Description | Severity | Mitigation | Residual Risk |
|-----------|-------------|----------|------------|---------------|
| HAZ-LOG-001 | Buffer overflow corrupts memory | Catastrophic | Fixed-size buffers, bounds checking | Acceptable |
| HAZ-LOG-002 | Logging blocks critical task | Hazardous | Bounded execution time, async logging | Acceptable |
| HAZ-LOG-003 | Heap exhaustion from logging | Hazardous | No dynamic allocation in critical paths | Acceptable |
| HAZ-LOG-004 | Infinite loop in formatter | Severe | Code review, static analysis | Acceptable |
| HAZ-LOG-005 | Deadlock in synchronized sink | Severe | Lock-free alternative or timeout | Acceptable |
| HAZ-LOG-006 | Log file fills disk | Major | Bounded file size, rotation | Acceptable |

### Failure Modes Analysis

```
Component: TruncatingRecordBuilder
Failure Mode: Message truncation
Effect: Loss of diagnostic information
Severity: Minor
Detection: Truncation flag set
Mitigation: Adequate buffer sizing, verification
Residual: Acceptable (diagnostic only)

Component: EmergencySink
Failure Mode: File write failure
Effect: Loss of emergency log
Severity: Major
Detection: Write return value checked
Mitigation: Redundant logging, NVRAM backup
Residual: Acceptable with mitigation

Component: SynchronizedSink
Failure Mode: Mutex deadlock
Effect: Logging hangs
Severity: Severe
Detection: Watchdog timeout
Mitigation: Bounded wait, fallback path
Residual: Acceptable with mitigation
```

---

## Example Configurations

### Example 1: DO-178C Level A (Catastrophic)

```cpp
// Absolute minimal configuration
namespace LevelA
{
    // Only critical errors logged
    struct CriticalError {};
    
    NOVA_LOGGER_TRAITS(CriticalError, CRITICAL, true, TimestampHelper::steadyNanosecs);
    
    // Fixed NVRAM sink - no allocation
    class NVRAMSink : public Sink
    {
        static constexpr size_t BUFFER_SIZE = 2048;
        uint8_t nvramBuffer[BUFFER_SIZE] __attribute__((section(".nvram")));
        std::atomic<size_t> writePos{0};
        
    public:
        void process(const Record& record) noexcept override
        {
            size_t pos = writePos.load(std::memory_order_relaxed);
            size_t available = BUFFER_SIZE - pos;
            size_t toCopy = std::min(record.messageSize, available);
            
            if (toCopy > 0)
            {
                std::memcpy(&nvramBuffer[pos], record.message, toCopy);
                writePos.store(pos + toCopy, std::memory_order_release);
            }
        }
    };
    
    static NVRAMSink sink;
    
    void initialize() noexcept
    {
        Logger<CriticalError>::bindSink(&sink);
    }
}
```

### Example 2: IEC 61508 SIL 2 (Industrial Safety)

```cpp
namespace SIL2
{
    // Multiple severity levels
    using CriticalTag = kmac::nova::extras::Critical;
    using ErrorTag = kmac::nova::extras::Error;
    using WarningTag = kmac::nova::extras::Warning;
    
    // Defensive composite sink
    class BoundedCompositeSink : public Sink
    {
        std::array<Sink*, 3> sinks{};
        size_t count = 0;
        std::atomic<uint64_t> errorCount{0};
        
    public:
        void add(Sink& sink) noexcept
        {
            if (count < sinks.size())
            {
                sinks[count++] = &sink;
            }
        }
        
        void process(const Record& record) noexcept override
        {
            for (size_t i = 0; i < count; ++i)
            {
                try
                {
                    if (sinks[i])
                    {
                        sinks[i]->process(record);
                    }
                }
                catch (...)
                {
                    errorCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
        
        uint64_t getErrorCount() const noexcept
        {
            return errorCount.load(std::memory_order_relaxed);
        }
    };
    
    // Fixed buffer formatting sink
    class FixedFormattingSink : public Sink
    {
        static constexpr size_t BUFFER_SIZE = 512;
        char buffer[BUFFER_SIZE];
        Sink* downstream;
        
    public:
        FixedFormattingSink(Sink& ds) : downstream(&ds) {}
        
        void process(const Record& record) noexcept override
        {
            // Simple fixed-format: [TAG] message
            int written = std::snprintf(buffer, BUFFER_SIZE,
                "[%s] %.*s\n",
                record.tag,
                static_cast<int>(record.messageSize),
                record.message);
            
            if (written > 0 && written < static_cast<int>(BUFFER_SIZE))
            {
                Record formatted = record;
                formatted.message = buffer;
                formatted.messageSize = written;
                downstream->process(formatted);
            }
        }
    };
    
    // Configuration
    static std::array<uint8_t, 4096> logBuffer;
    static OStreamSink bufferSink(/* ... */);
    static FixedFormattingSink formatter(bufferSink);
    static SpinlockSink threadSafe(formatter);
    
    void initialize() noexcept
    {
        Logger<CriticalTag>::bindSink(&threadSafe);
        Logger<ErrorTag>::bindSink(&threadSafe);
        Logger<WarningTag>::bindSink(&threadSafe);
    }
}
```

### Example 3: ISO 26262 ASIL B (Automotive)

```cpp
namespace ASILB
{
    // Subsystem-based logging
    struct PowertrainTag {};
    struct BrakingTag {};
    struct SteeringTag {};
    
    // Emergency logging for safety events
    class EmergencyLogger
    {
        static constexpr size_t EMERGENCY_BUFFER_SIZE = 1024;
        
        FILE* emergencyFile;
        FileWriter writer;
        EmergencySink sink;
        
    public:
        EmergencyLogger()
            : emergencyFile(fopen("/nvram/emergency.flare", "ab"))
            , writer(emergencyFile)
            , sink(&writer, true)  // Capture process info
        {
        }
        
        ~EmergencyLogger()
        {
            if (emergencyFile)
            {
                sink.flush();
                fclose(emergencyFile);
            }
        }
        
        void logCriticalEvent(const char* subsystem, const char* message)
        {
            TruncatingRecordBuilder<PowertrainTag> builder(__FILE__, __FUNCTION__, __LINE__);
            builder << "[CRITICAL] " << subsystem << ": " << message;
        }
    };
    
    static EmergencyLogger emergency;
    
    // Diagnostics to CAN bus
    class CANDiagnosticSink : public Sink
    {
        void process(const Record& record) noexcept override
        {
            // Send truncated message to CAN diagnostic channel
            CANMessage msg;
            msg.id = DIAGNOSTIC_MESSAGE_ID;
            msg.length = std::min(record.messageSize, size_t(8));
            std::memcpy(msg.data, record.message, msg.length);
            
            // Non-blocking send
            can_send_nonblocking(&msg);
        }
    };
    
    static CANDiagnosticSink canSink;
    static SpinlockSink threadSafe(canSink);
    
    void initialize()
    {
        Logger<PowertrainTag>::bindSink(&threadSafe);
        Logger<BrakingTag>::bindSink(&threadSafe);
        Logger<SteeringTag>::bindSink(&threadSafe);
    }
}
```

---

## Checklist

Use this checklist to verify compliance with safety-critical guidelines:

### Design Phase

- [ ] Identified applicable safety standard (DO-178C, IEC 61508, etc.)
- [ ] Determined safety level/classification
- [ ] Selected appropriate Nova/Flare subset
- [ ] Documented all configuration decisions
- [ ] Created traceability matrix (requirements to design)
- [ ] Performed hazard analysis for logging subsystem
- [ ] Defined resource budgets (memory, timing)
- [ ] Established coding standards (MISRA, etc.)
- [ ] Created verification plan

### Implementation Phase

- [ ] Used only approved subset of features
- [ ] Followed coding standards
- [ ] Added defensive checks
- [ ] Bounded all resources (buffers, execution time)
- [ ] Avoided heap allocation (or justified if used)
- [ ] Implemented error containment
- [ ] Added configuration version tracking
- [ ] Commented all safety-critical code
- [ ] Reviewed by independent party

### Verification Phase

- [ ] Achieved required code coverage
- [ ] All unit tests passing
- [ ] All integration tests passing
- [ ] Static analysis clean (0 errors)
- [ ] Memory safety verified (Valgrind, etc.)
- [ ] Timing requirements verified
- [ ] Stress testing completed
- [ ] Fault injection testing completed (if required)
- [ ] Traceability complete (requirements to tests)

### Documentation Phase

- [ ] Software Requirements Specification complete
- [ ] Software Design Description complete
- [ ] Traceability matrix complete
- [ ] Verification report complete
- [ ] Hazard analysis complete
- [ ] Configuration documentation complete
- [ ] User manual/integration guide complete
- [ ] All deviations justified and approved

### Review Phase

- [ ] Peer review completed
- [ ] Safety authority review completed
- [ ] All findings addressed
- [ ] Documentation approved
- [ ] Configuration baselined

---

## Conclusion

Following these guidelines will help ensure that Nova, Nova Extras, and Flare are used appropriately in safety-critical systems. Key principles:

1. **Use appropriate subset** for your safety level
2. **Document everything** - decisions, rationale, deviations
3. **Verify thoroughly** - testing, analysis, review
4. **Bound all resources** - memory, time, complexity
5. **Fail safe** - logging errors don't propagate

Remember: **These are guidelines, not requirements.** Always consult your safety authority and follow your specific standards and regulations.

For questions or clarifications, consult:
- Your safety authority
- DO-178C/IEC 61508/ISO 26262 standards
- Nova/Flare documentation
- Safety engineering resources

---

**Document Control**

- Version: 1.0
- Date: January 2025
- Status: Initial Release
- Next Review: Annually or upon major Nova/Flare updates
