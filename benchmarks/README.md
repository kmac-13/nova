# Nova/Flare Benchmark Suite

The benchmarks directory contains comprehensive benchmarks for Nova and Flare logging systems, including comparisons with popular C++ logging libraries.

---

## Benchmark Files Overview

### Core Performance Benchmarks

#### `benchmark_throughput.cpp`
**Purpose:** Measures maximum sustainable logging throughput using null sinks (library overhead only, no I/O).

**Tests:**
- `BM_Throughput_Nova_SingleThread_*` - single-threaded throughput for Nova's three builder types (Truncating, Continuation, Streaming)
- `BM_Throughput_Nova_MultiThread_*` - multi-threaded throughput with different synchronization strategies (Mutex, Spinlock, Async Pool, Direct)
- `BM_Throughput_Nova_MessageSize` - throughput scaling with message size (8-4096 bytes)
- `BM_Throughput_Nova_Optimized_*` - latency-optimized Nova variants (NoTimestamp, Direct API, Minimal)
- `BM_Throughput_Nova_RuntimeDisabledTag` - runtime disabled logging overhead (no sink bound)
- `BM_Throughput_Nova_CompiletimeDisabledTag` - compile-time disabled logging overhead (zero cost)
- `BM_Throughput_Spdlog_*` - spdlog sync/async throughput (single and multi-threaded)
- `BM_Throughput_Quill_*` - Quill throughput (single-threaded and message size scaling)
- `BM_Throughput_Easylogging_SingleThread` - easylogging++ throughput
- `BM_Throughput_Glog_*` - glog throughput (single and multi-threaded)
- `BM_Throughput_BoostLog_*` - Boost.Log throughput (single and multi-threaded)
- `BM_Throughput_Log4cpp_SingleThread` - log4cpp throughput
- `BM_Throughput_NanoLog_SingleThread` - NanoLog throughput

**Key Metrics:** messages per second, latency per message, scaling efficiency

---

#### `benchmark_latency.cpp`
**Purpose:** Measures per-message latency characteristics including percentile analysis.

**Tests:**
- `BM_Latency_Nova_Truncating` - latency for truncating builder (null sink)
- `BM_Latency_Nova_Continuation` - latency for continuation builder (null sink)
- `BM_Latency_Nova_Streaming` - latency for streaming builder (null sink)
- `BM_Latency_Nova_Synchronized_Mutex` - latency with mutex synchronization
- `BM_Latency_Nova_Synchronized_Spinlock` - latency with spinlock synchronization
- `BM_Latency_Nova_MemoryPoolAsyncQueue` - latency with async queue (enqueue time)
- `BM_Latency_Nova_FileSync` - latency for synchronous file I/O
- `BM_Latency_UnderLoad_Mutex` - latency under multi-threaded contention (1/2/4 threads)
- `BM_Latency_MessageSize` - latency vs message size (8-4096 bytes)
- `BM_Latency_Spdlog` - spdlog latency comparison
- `BM_Latency_Glog` - glog latency comparison
- `BM_Latency_BoostLog` - Boost.Log latency comparison
- `BM_Latency_Easylogging` - easylogging++ latency comparison
- `BM_Latency_Log4cpp` - log4cpp latency comparison
- `BM_Latency_NanoLog` - NanoLog latency comparison

**Key Metrics:** mean, median, p99, p999, max latency

---

#### `benchmark_formatted_file_output.cpp`
**Purpose:** Apples-to-apples comparison of formatted log output to actual files across Nova, spdlog, and easylogging++.  Covers disabled-tag overhead, sync/async file I/O under various flush strategies, and multi-threaded scaling.

**Tests:**

*Disabled-tag overhead (Nova vs spdlog equivalents):*
- `BM_Nova_DisabledTag_NullptrSink` - Nova runtime-disabled via nullptr sink (1/2/4/8 threads)
- `BM_Nova_DisabledTag_NullSink` - Nova runtime-disabled via NullSink binding (1/2/4/8 threads)
- `BM_Nova_DisabledTag_CompileTime` - Nova compile-time disabled tag (1/2/4/8 threads)
- `BM_Spdlog_NullSinkDisabled` - spdlog null sink (1/2/4/8 threads)
- `BM_Spdlog_LevelDisabled` - spdlog level-filtered at runtime (1/2/4/8 threads)
- `BM_Spdlog_CompileTimeDisabled` - spdlog SPDLOG_ACTIVE_LEVEL compile-time disabled (1/2/4/8 threads)

*Nova sync file output (single-threaded, varies flush strategy):*
- `BM_Nova_File_Sync_Flush` - SynchronizedSink + fflush() per call (arg 1/2/4/8)
- `BM_Nova_File_Sync_NoFlush` - SynchronizedSink, OS-buffered (arg 1/2/4/8)
- `BM_Nova_File_NoSync_Flush` - no lock + fflush() per call (arg 1/2/4/8)
- `BM_Nova_File_NoSync_NoFlush` - no lock, OS-buffered (arg 1/2/4/8)

*spdlog sync file output (single-threaded):*
- `BM_spdlog_File_Sync_Flush` - basic_file_sink_mt + flush() per call (arg 1/2/4/8)
- `BM_spdlog_File_Sync_NoFlush` - basic_file_sink_mt, OS-buffered (arg 1/2/4/8)

*easylogging++ sync file output (single-threaded):*
- `BM_easylogging_File_Sync` - default sync file output (arg 1/2/4/8)

*Nova async file output (multi-threaded fixture):*
- `NovaSharedFileBenchmark/NovaAsyncNoFlush` - MemoryPoolAsyncSink (256KB pool) to file (1/2/4/8/16 threads)

*spdlog async file output (multi-threaded fixture):*
- `SpdlogAsyncFixture/SpdlogAsyncNoFlush` - async_logger with overrun_oldest policy (1/2/4/8/16 threads)

**Key Metrics:** producer latency, delivered messages/sec, drop rate (async), flush overhead

**Design notes:**
- sync benchmarks use `ScopedConfigurator` with stack-local sinks — each invocation is fully isolated with a clean file and fresh sink binding
- async benchmarks use Google Benchmark fixtures (`SetUp`/`TearDown`) so each thread-count variant gets a clean sink and counters
- the Arg parameter in sync benchmarks is informational (thread count label); all sync tests run single-threaded

---

### Nova-Specific Benchmarks

#### `benchmark_nova_builders.cpp`
**Purpose:** Detailed comparison of Nova's three RecordBuilder variants.

**Tests:**
- `BM_Builder_Truncating_*` - truncating builder performance (Simple, Complex, LongMessage, HighFrequency, ConstructionOverhead, Correctness)
- `BM_Builder_Continuation_*` - continuation builder performance (same test variants)
- `BM_Builder_Streaming_*` - streaming builder performance (same test variants)

**Key Metrics:** builder overhead, message construction time, formatting performance, buffer handling

---

#### `benchmark_nova_sinks.cpp`
**Purpose:** Benchmark different Nova sink implementations.

**Tests:**
- `BM_NullSink_Baseline` - baseline (no-op sink)
- `BM_OStreamSink_StringStream` - output to std::stringstream
- `BM_OStreamSink_FileStream` - output to std::ofstream
- `BM_OStreamSink_HighVolume` - OStreamSink under high load
- `BM_RollingFileSink_NoRotation` - rolling file without rotation
- `BM_RollingFileSink_SmallFile` - rolling file with frequent rotation
- `BM_CompositeSink_TwoSinks` - composite with 2 destinations
- `BM_CompositeSink_FiveSinks` - composite with 5 destinations
- `BM_SynchronizedSink_Mutex_SingleThread` - mutex-synchronized sink (single-threaded)
- `BM_SynchronizedSink_Mutex_MultiThread` - mutex-synchronized sink (1/2/4/8 threads)
- `BM_SynchronizedSink_Spinlock_SingleThread` - spinlock-synchronized sink (single-threaded)
- `BM_SynchronizedSink_Spinlock_MultiThread` - spinlock-synchronized sink (1/2/4/8 threads)
- `BM_AsynchSink_MemoryPool_SingleThread` - memory pool asynchronous sink (single-threaded)
- `BM_AsynchSink_MemoryPool_MultiThread` - memory pool asynchronous sink (1/2/4/8 threads)
- `BM_FilterSink_AllPass` - filter that passes all messages
- `BM_FilterSink_AllBlock` - filter that blocks all messages
- `BM_FilterSink_ComplexPredicate` - filter with complex condition
- `BM_FormattingSink_SimpleFormatter` - simple formatting wrapper
- `BM_FormattingSink_ComplexFormatter` - complex formatting wrapper
- `BM_Combined_Sync_Composite` - combined synchronization + composite
- `BM_Combined_Filter_Format_File` - combined filter + format + file

**Key Metrics:** sink overhead, synchronization cost, composition performance

---

#### `benchmark_memory_pool_async.cpp`
**Purpose:** Benchmark MemoryPoolAsyncSink with various configurations.

**Tests:**
- `BM_MemoryPoolAsyncSink_Default` - default configuration (128KB pool, 4096 capacity)
- `BM_MemoryPoolAsyncSink_256KB` - 256KB pool
- `BM_MemoryPoolAsyncSink_1MB` - 1MB pool
- `BM_MemoryPoolAsyncSink_4MB` - 4MB pool
- `BM_MemoryPoolAsyncSink_Uint16Offset` - using uint16_t offsets (smaller index)
- `BM_MemoryPoolAsyncSink_Uint32Offset` - using uint32_t offsets (larger index)
- `BM_MemoryPoolAsyncSink_Heap` - heap-allocated pool
- `BM_MemoryPoolAsyncSink_Stack` - stack-allocated pool
- `BM_MemoryPoolAsyncSink_ShortMessages` - performance with short messages
- `BM_MemoryPoolAsyncSink_MediumMessages` - performance with medium messages
- `BM_MemoryPoolAsyncSink_LargeMessages` - performance with large messages
- `BM_MemoryPoolAsyncSink_Embedded` - embedded-friendly configuration
- `BM_MemoryPoolAsyncSink_Optimized` - optimized configuration

**Key Metrics:** pool utilization, allocation overhead, throughput, drop rate

---

### Flare-Specific Benchmarks

#### `benchmark_flare_emergency.cpp`
**Purpose:** Benchmark Flare's async-signal-safe emergency logging system.

**Tests:**
- `BM_Flare_EmergencySink_SimpleMessage` - basic emergency logging
- `BM_Flare_EmergencySink_WithMetadata` - emergency logging with full metadata
- `BM_Flare_MessageSize` - emergency logging message size scaling (8-4096 bytes)
- `BM_Flare_CrashScenario` - simulated crash scenario
- `BM_Flare_MultiThreaded` - multi-threaded emergency logging (1/2/4 threads)
- `BM_Flare_ReadBack` - reading emergency logs from buffer
- `BM_Flare_FlushOverhead` - flush operation overhead
- `BM_Comparison_NovaRegular` - Nova regular logging for comparison
- `BM_Comparison_FlareEmergency` - Flare emergency logging for comparison

**Key Metrics:** async-signal-safe overhead, TLV encoding cost, buffer management

---

## Running Benchmarks

### Build All Benchmarks
```bash
cd benchmarks/build
cmake ..
cmake --build .
```

### Run Specific Benchmark Suite
```bash
./benchmark_throughput                  # throughput tests
./benchmark_latency                     # latency tests
./benchmark_formatted_file_output       # file output and delivery tests
./benchmark_nova_builders               # builder comparison
./benchmark_memory_pool_async           # memory pool async tests
```

### Run Specific Test
```bash
# run only Nova single-threaded throughput
./benchmark_throughput --benchmark_filter=Nova_SingleThread

# run only async file output tests
./benchmark_formatted_file_output --benchmark_filter=Async

# run only 8-thread tests
./benchmark_throughput --benchmark_filter=/threads:8
```

### Export Results
```bash
# JSON output
./benchmark_throughput --benchmark_out=results.json --benchmark_out_format=json

# CSV output
./benchmark_throughput --benchmark_out=results.csv --benchmark_out_format=csv
```

---

## Key Benchmark Categories

### 1. Library Overhead (Null Sink)
**Files:** `benchmark_throughput.cpp`, `benchmark_latency.cpp`
**Purpose:** measure pure library performance without I/O
**Use Case:** understanding fundamental performance limits

### 2. Real-World Performance (File I/O)
**Files:** `benchmark_formatted_file_output.cpp`
**Purpose:** measure actual formatted log output to disk, including sync/async strategies, flush overhead, and disabled-tag cost
**Use Case:** production performance estimation and configuration decisions

### 3. Component-Level Analysis
**Files:** `benchmark_nova_builders.cpp`, `benchmark_nova_sinks.cpp`
**Purpose:** understand individual component performance
**Use Case:** optimization and configuration decisions

### 4. Async Pool Tuning
**Files:** `benchmark_memory_pool_async.cpp`, `benchmark_multi_queue_async.cpp`
**Purpose:** find optimal async configuration
**Use Case:** performance tuning for specific workloads

### 5. Competitive Analysis
**Files:** `benchmark_nova_vs_others.cpp`, `benchmark_formatted_file_output.cpp`
**Purpose:** compare against other logging libraries
**Use Case:** library selection decisions

### 6. Emergency Logging
**Files:** `benchmark_flare_emergency.cpp`
**Purpose:** validate async-signal-safe performance
**Use Case:** crash logging and signal handlers

---

## Interpreting Results

### Throughput Benchmarks
- **items_per_second:** messages logged per second
- **Higher is better**
- look for scaling efficiency in multi-threaded tests

### Latency Benchmarks
- **Time per operation:** nanoseconds per log message
- **Lower is better**
- check p99/p999 for tail latency

### File Output Benchmarks
- **Time per operation:** producer latency including formatting and I/O
- **Delivered:** messages confirmed written to disk (async fixture benchmarks)
- **Dropped:** messages discarded due to pool/queue saturation (async only)
- **DropRate%:** percentage lost under sustained load — expected behaviour for bounded async sinks, not a bug
- **DeliveredPerSec:** real throughput to disk

### Key Performance Indicators
- **Single-thread latency:** library overhead baseline
- **Multi-thread scaling:** contention and lock performance
- **Drop rate:** queue saturation under load — Nova's bounded pool trades drop rate for predictable memory usage
- **Tail latency:** worst-case performance

---

## Dependencies

Required:
- Google Benchmark
- Nova logging library
- Flare emergency logging library

Optional (for comparison benchmarks):
- spdlog
- glog
- Boost.Log
- log4cpp
- easylogging++
- NanoLog
- Quill

Configure with CMake options:
```bash
cmake -DHAVE_SPDLOG=ON -DHAVE_GLOG=ON ..
```

---

## Notes

- all benchmarks use Google Benchmark framework
- compile with optimizations: `-O3 -DNDEBUG`
- run on idle system for consistent results
- file output benchmarks write actual formatted log files to disk
- Quill multi-threaded benchmarks are not included: Quill uses unbounded SPSC queues that grow without limit under sustained load, eventually exhausting memory and crashing the process.  Single-threaded Quill benchmarks remain in `benchmark_throughput.cpp`
- Nova's async drop rate under maximum pressure reflects intentional bounded-pool backpressure, not data loss in normal operation
