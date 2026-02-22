# Flare Forensic Logging - Use Cases and Examples

## What is Flare?

Flare is Nova's crash-safe forensic logging component. While Nova handles normal operational logging, Flare is specifically designed to capture diagnostic data **when everything is going wrong** - during crashes, signal handlers, segmentation faults, stack corruption, and other catastrophic failures.

## Key Concept: Async-Signal-Safety

The critical feature of Flare is **async-signal-safety**. This means Flare can be called from signal handlers (SIGSEGV, SIGABRT, etc.) without risk of deadlock, corruption, or undefined behavior. This is extremely rare in logging libraries.

### Why Most Loggers Fail in Crashes

Most logging libraries (including Nova's regular sinks) use:
- Heap allocation (`malloc`, `new`, `std::string`)
- Locks/mutexes
- Standard library I/O (`std::cout`, `std::cerr`)
- Thread-local storage

All of these are **unsafe in signal handlers**. If a signal handler calls malloc() while the process was already inside malloc(), you get deadlock or corruption.

### What Flare Does Differently

Flare's EmergencySink uses only:
- Stack-based buffers (no heap allocation in process())
- Raw POSIX syscalls (`write()`, `fflush()`)
- Fixed-size encoding
- No locks in the write path

This makes it safe to call from signal handlers.

**Important Note:** While EmergencySink itself avoids heap allocation and locks during `process()`, the Nova core infrastructure (Logger, record builders) does use standard C++ features like `<atomic>`, `<chrono>`, and `<vector>`. This means:
- ✅ Safe to call from signal handlers (EmergencySink is async-signal-safe)
- ✅ Safe in real-time systems (deterministic, no allocation in hot path)
- ❌ NOT suitable for bare-metal/no-std environments (requires C++ standard library)

---

## Use Case 1: Crash Handler Logging

**Scenario:** Your application crashes with SIGSEGV. You want to know what was happening right before the crash.

### The Problem

```cpp
// DANGEROUS - Will likely deadlock or crash worse
void crash_handler(int signal) {
    std::cerr << "Crash detected!\n";  // UNSAFE - may deadlock
    spdlog::error("Segfault occurred");  // UNSAFE - uses locks
    // Process dies, no information preserved
}
```

### The Solution with Flare

```cpp
#include <kmac/flare/emergency_sink.h>
#include <kmac/nova/logger.h>
#include <signal.h>

// Tag for crash logs
struct CrashTag {};
NOVA_LOGGER_TRAITS(CrashTag, CRASH, true, kmac::nova::TimestampHelper::steadyNanosecs);

// Global emergency sink (initialized early)
FILE* g_emergency_log = nullptr;
kmac::flare::EmergencySink* g_emergency_sink = nullptr;

void crash_handler(int signal) {
    // SAFE - Flare is async-signal-safe
    NOVA_LOG_TRUNC(CrashTag) << "CRASH: Signal " << signal;
    
    // Manually flush to ensure data is written
    if (g_emergency_sink) {
        g_emergency_sink->flush();
    }
    
    // Re-raise signal for default handling
    signal(signal, SIG_DFL);
    raise(signal);
}

int main() {
    // Initialize emergency logging FIRST
    g_emergency_log = fopen("crash.flare", "wb");
    g_emergency_sink = new kmac::flare::EmergencySink(g_emergency_log);
    
    // Bind crash tag to emergency sink
    kmac::nova::ScopedConfigurator config;
    config.bind<CrashTag>(g_emergency_sink);
    
    // Install signal handlers
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGFPE, crash_handler);
    
    // Log important state before potential crash
    NOVA_LOG_TRUNC(CrashTag) << "Starting critical operation";
    
    // ... your application code ...
    
    return 0;
}
```

### After the Crash

```bash
# Read the crash log
python3 flare_reader.py crash.flare

# Output shows:
# [2025-01-11 14:23:45.123] CRASH: Starting critical operation
# [2025-01-11 14:23:47.892] CRASH: Signal 11
```

**Why this works:** While Nova's record builders use stack buffers, EmergencySink's `process()` method is async-signal-safe - it uses only syscalls and stack operations.

---

## Use Case 2: Real-Time System Fault Recording

**Scenario:** You're writing real-time control software (robotics, automotive, industrial). A timing violation or safety fault occurs, and you need to log it without disrupting the real-time loop.

### The Problem

Normal logging:
- Allocates memory (unpredictable latency)
- Takes locks (priority inversion risk)
- Blocks on I/O (ruins real-time guarantees)

### The Solution

```cpp
struct SafetyTag {};
NOVA_LOGGER_TRAITS(SafetyTag, SAFETY, true, ...);

// Real-time control loop
void control_loop() {
    FILE* safety_log = fopen("safety.flare", "wb");
    kmac::flare::EmergencySink safety_sink(safety_log);
    
    kmac::nova::ScopedConfigurator config;
    config.bind<SafetyTag>(&safety_sink);
    
    while (running) {
        auto start = get_time();
        
        // Perform control calculations
        auto position = read_sensor();
        auto control = calculate_control(position);
        
        // Safety check
        if (position > SAFE_LIMIT) {
            // DETERMINISTIC - No allocation, no locks, bounded time
            NOVA_LOG_TRUNC(SafetyTag) 
                << "SAFETY VIOLATION: pos=" << position 
                << " limit=" << SAFE_LIMIT;
        }
        
        apply_control(control);
        
        auto elapsed = get_time() - start;
        if (elapsed > DEADLINE) {
            // Log timing violation
            NOVA_LOG_TRUNC(SafetyTag) 
                << "DEADLINE MISS: " << elapsed << "ns";
        }
    }
}
```

**Why this works:** 
- TruncatingRecordBuilder uses fixed stack buffer (no heap)
- EmergencySink::process() uses only write() syscall (deterministic)
- All operations have bounded worst-case time

---

## Use Case 3: Memory Corruption Debugging

**Scenario:** Your application has occasional memory corruption that's hard to reproduce. You want to log state right before corruption occurs.

### The Strategy

```cpp
struct MemDebugTag {};
NOVA_LOGGER_TRAITS(MemDebugTag, MEMDEBUG, true, ...);

// Canary checking in allocator
void* tracked_malloc(size_t size, const char* file, int line) {
    void* ptr = malloc(size);
    
    // Log allocation with Flare
    NOVA_LOG_TRUNC(MemDebugTag) 
        << "ALLOC: " << ptr << " size=" << size 
        << " at " << file << ":" << line;
    
    // Install canaries
    install_canaries(ptr, size);
    
    return ptr;
}

void tracked_free(void* ptr, const char* file, int line) {
    // Check canaries before freeing
    if (!check_canaries(ptr)) {
        // CORRUPTION DETECTED - Log it immediately
        NOVA_LOG_TRUNC(MemDebugTag) 
            << "CORRUPTION at " << ptr 
            << " freed from " << file << ":" << line;
        
        // Flush immediately (corruption may be about to crash us)
        g_emergency_sink->flush();
    }
    
    free(ptr);
}
```

**After corruption:** Even if the process crashes immediately after detecting corruption, the Flare log will show exactly which allocation was corrupted.

---

## Use Case 4: Embedded System Black Box

**Scenario:** You're writing firmware for an embedded device with a C++ standard library. When it locks up or resets, you want to know why.

### The Setup

```cpp
// Persistent memory region for crash log
__attribute__((section(".noinit"))) 
static char crash_buffer[16384];

// Use memory-mapped buffer
void setup_emergency_logging() {
    FILE* crash_log = fmemopen(crash_buffer, sizeof(crash_buffer), "wb");
    kmac::flare::EmergencySink* sink = new kmac::flare::EmergencySink(crash_log);
    
    // Bind to all critical tags
    config.bind<SystemTag>(sink);
    config.bind<HardwareTag>(sink);
    config.bind<FaultTag>(sink);
}

// In your main loop
void main_loop() {
    while (true) {
        // Log heartbeat
        NOVA_LOG_TRUNC(SystemTag) << "Heartbeat " << tick_count;
        
        // Log critical operations
        if (perform_risky_operation()) {
            NOVA_LOG_TRUNC(FaultTag) << "Operation failed";
        }
        
        // Flush periodically
        if (tick_count % 100 == 0) {
            g_emergency_sink->flush();
        }
    }
}

// On next boot
void check_previous_crash() {
    // Read the crash buffer
    kmac::flare::Scanner scanner;
    scanner.scan(crash_buffer, sizeof(crash_buffer));
    
    if (scanner.found_records()) {
        // Last few records show what happened before reset
        // Send to monitoring system, log to persistent storage, etc.
    }
}
```

**Why this works:** Flare's TLV format tolerates partial writes, so even if power was lost mid-write, you can still read earlier records.

**Note:** This requires an embedded environment with C++ standard library support (e.g., ARM with newlib, ESP32, etc.). For true bare-metal without std, you'd need a custom minimal logging solution.

---

## Use Case 5: Multi-Process Coordination Failures

**Scenario:** You have multiple processes communicating via shared memory or pipes. When coordination breaks down (deadlock, starvation), you need to see what each process was doing.

### The Setup

```cpp
// Each process writes to its own emergency log
void init_process_emergency_logging(int process_id) {
    char filename[64];
    snprintf(filename, sizeof(filename), "proc_%d.flare", process_id);
    
    FILE* log = fopen(filename, "wb");
    kmac::flare::EmergencySink* sink = new kmac::flare::EmergencySink(log);
    
    config.bind<CoordTag>(sink);
}

// In coordination code
void wait_for_other_process() {
    NOVA_LOG_TRUNC(CoordTag) << "Waiting for mutex " << mutex_id;
    
    auto start = get_time();
    while (!try_acquire_mutex(mutex_id)) {
        auto elapsed = get_time() - start;
        
        if (elapsed > TIMEOUT) {
            // About to timeout - log state
            NOVA_LOG_TRUNC(CoordTag) 
                << "TIMEOUT waiting for mutex " << mutex_id
                << " held by process " << mutex_owner;
            
            g_emergency_sink->flush();
            
            // Abort or handle timeout
            abort();
        }
    }
    
    NOVA_LOG_TRUNC(CoordTag) << "Acquired mutex " << mutex_id;
}
```

**Post-mortem:** Examine all `proc_*.flare` files together to see the sequence of events across processes leading to deadlock.

---

## Use Case 6: Exception Unwinding Logging

**Scenario:** You have complex exception handling and want to trace the exception path during unwinding, even if the process ultimately crashes.

### The Strategy

```cpp
struct ExceptionTag {};
NOVA_LOGGER_TRAITS(ExceptionTag, EXCEPTION, true, ...);

class TracedException : public std::exception {
    std::string msg_;
public:
    TracedException(const char* msg) : msg_(msg) {
        // Log when exception is thrown
        NOVA_LOG_TRUNC(ExceptionTag) << "THROW: " << msg;
    }
    
    ~TracedException() noexcept {
        // Log when exception is destroyed (after catch or terminate)
        NOVA_LOG_TRUNC(ExceptionTag) << "DESTROY: " << msg_;
    }
    
    const char* what() const noexcept override {
        return msg_.c_str();
    }
};

void risky_operation() {
    NOVA_LOG_TRUNC(ExceptionTag) << "Enter risky_operation";
    
    try {
        throw TracedException("Database connection failed");
    } catch (...) {
        NOVA_LOG_TRUNC(ExceptionTag) << "Caught in risky_operation";
        throw;  // Re-throw
    }
}
```

**Why this works:** Even if the exception causes the program to call `std::terminate()`, the Flare log captures the full exception path.

---

## Common Patterns

### Pattern 1: Two-Tier Logging

```cpp
// Normal logging goes to Nova sinks (file, console, etc.)
NOVA_LOG_TRUNC(InfoTag) << "User logged in";

// Critical events ALSO go to emergency sink
NOVA_LOG_TRUNC(CrashTag) << "User logged in";  // Same data, different sink

// In crash handler, only CrashTag records are guaranteed
```

### Pattern 2: Heartbeat + Context

```cpp
// Regular heartbeat
void heartbeat_thread() {
    while (true) {
        NOVA_LOG_TRUNC(CrashTag) << "Heartbeat " << tick;
        sleep(1);
        tick++;
    }
}

// When crash occurs, last heartbeat shows approximately when
```

### Pattern 3: Ring Buffer Mode

```cpp
// Reopen in append mode, or use mmap with manual wrapping
// Flare Scanner can handle wrapped/circular buffers
```

---

## Reading Flare Logs

### Using Python Reader

```bash
# Text output
python3 flare_reader.py crash.flare

# JSON output (for log aggregation)
python3 flare_reader.py --json crash.flare > crash.json
```

### Programmatic Reading

```cpp
#include <kmac/flare/scanner.h>
#include <kmac/flare/reader.h>

void analyze_crash_log(const char* filename) {
    // Read file into buffer
    std::vector<char> data = read_file(filename);
    
    // Scan for records
    kmac::flare::Scanner scanner;
    scanner.scan(data.data(), data.size());
    
    // Process each found record
    for (const auto& record_info : scanner.records()) {
        kmac::flare::Reader reader(record_info.data, record_info.size);
        
        // Extract fields
        if (auto timestamp = reader.getTimestamp()) {
            // Process timestamp
        }
        if (auto message = reader.getMessage()) {
            // Process message
        }
    }
}
```

---

## When NOT to Use Flare

**Don't use Flare for:**
- Normal application logging (use Nova with regular sinks)
- High-volume logging (Flare prioritizes safety over throughput)
- Human-readable logs (Flare is binary, needs decoding)
- Logs that need formatting/filtering (do that in Nova layer)
- Bare-metal/no-std environments (requires C++ standard library)

**Flare is for:**
- Last-ditch forensics when everything is broken
- Data that must survive crashes
- Logging from signal handlers
- Real-time deterministic logging
- Memory-corruption debugging

---

## Requirements and Limitations

### What You Need

Flare requires:
- ✅ C++ standard library (uses `<atomic>`, `<chrono>`, `<vector>` via Nova)
- ✅ POSIX file I/O (`FILE*`, `fopen`, `fwrite`, `fflush`)
- ✅ Basic C library (`memcpy`, `strlen`)

Flare is compatible with:
- Linux, macOS, FreeBSD, Windows (with POSIX layer)
- Embedded systems with C++ std library (ARM/newlib, ESP32, etc.)
- Real-time systems (deterministic, bounded execution time)

Flare is NOT compatible with:
- ❌ Bare-metal/no-std environments (needs C++ std library)
- ❌ Environments without file I/O (but can use memory buffers)

### Async-Signal-Safety Details

**What's safe:**
- `EmergencySink::process()` - Uses only write() syscall
- `EmergencySink::flush()` - Uses only fflush() syscall
- Record builders (TRUNC/CONT) - Stack-only, no allocation

**What's potentially unsafe:**
- Constructing Logger/ScopedConfigurator (uses vector, should be done before signals)
- First log from a tag (atomic initialization, do it before signals)

**Best practice:** Initialize all logging infrastructure **before** installing signal handlers.

---

## Performance Characteristics

- **Latency:** ~1-5 microseconds per log (syscall overhead)
- **Throughput:** ~100K logs/second (limited by `write()` syscalls)
- **Memory:** Zero heap allocation during process()
- **CPU:** Minimal (memcpy + write syscall)
- **Determinism:** Bounded worst-case time (good for real-time)

Compare to normal logging:
- spdlog: 1-2 microseconds (best case, but unsafe in crashes)
- std::cerr: 5-10 microseconds (but deadlocks in crashes)
- Flare: 3-5 microseconds (and SAFE in crashes)

The small performance penalty is the cost of crash-safety.

---

## Summary

**Use Flare when:**
1. ✅ You need to log from signal handlers
2. ✅ Crashes must preserve diagnostic data
3. ✅ Real-time determinism is required
4. ✅ Memory corruption needs tracking
5. ✅ Multi-process debugging is needed
6. ✅ Exception unwinding needs logging

**Don't use Flare when:**
1. ❌ Normal application logging suffices
2. ❌ You need human-readable logs
3. ❌ High throughput is critical
4. ❌ Complex formatting/filtering is needed
5. ❌ Bare-metal/no-std environment (no C++ std library)

**Key Takeaway:** Flare is your **forensic black box** - the last line of defense when everything else fails. It trades some performance for crash-safety and determinism.
