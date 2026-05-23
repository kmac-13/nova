# Benchmark Comparison

All figures are from the `benchmarks/` suite run on the following hardware:

- **CPU**: AMD Ryzen 9 6900HX (16 logical cores)
- **OS**: Windows 11 (10.0.26200)
- **Storage**: Crucial P3 Plus 1TB NVMe SSD (CT1000P3PSSD8)
- **Build**: MinGW GCC, Release, C++17

### Benchmark configuration

**Queue sizing.** All three libraries use a single consumer/backend thread. Memory allocations per library:

- **Nova**: single shared `MemoryPoolAsyncSink`, fixed at 256 KB regardless of thread count. This is the memory-conservative default appropriate for embedded and RTOS targets.  Nova's TLS builders additionally allocate one fixed-size buffer per producer thread (1 KB by default, configurable via `NOVA_DEFAULT_BUFFER_SIZE_OVERRIDE`) - at 8 threads this is 8 KB of TLS buffer memory.
- **Quill**: one 256 KB SPSC queue per producer thread - total buffer scales with thread count.
- **spdlog async**: single shared MPMC queue, sized to 256 KB × N threads to match Quill's total buffer capacity. This is the optimal configuration for multi-thread conditions; the default fixed queue would put spdlog at a structural disadvantage.

The guaranteed delivery benchmarks use the same sizing. Nova `SynchronizedSink` and spdlog sync have no async queue - those modes pay the full cost synchronously on the calling thread.

**Counting sinks.** Several benchmarks are run twice: once writing to a real file and once with a counting sink - a downstream that atomically increments a counter per record and does nothing else. The counting sink proves the backend is doing real work on every record (the call cannot be optimized away through a virtual dispatch) while removing file I/O from the measurement. The difference between file-sink and counting-sink timings isolates I/O cost.

### Guaranteed delivery latency

Time to write 1,000,000 records to a file with no drops allowed - producers block if the queue is full. Each figure is the average of 5 independent runs.

| Library | Mode | 1 thread | 2 threads | 4 threads | 8 threads |
|---|---|---|---|---|---|
| **Nova** | synchronized sink | **190 ns/msg** | **309 ns/msg** | **301 ns/msg** | **291 ns/msg** |
| spdlog | sync (mutex) | 508 ns/msg | 828 ns/msg | 845 ns/msg | 849 ns/msg |
| Quill | async, blocking queue | 693 ns/msg | 734 ns/msg | 845 ns/msg | 954 ns/msg |
| spdlog | async, blocking queue | 819 ns/msg | 1,304 ns/msg | 4,396 ns/msg | 8,514 ns/msg |

Nova delivers 1M records **2.7–3.0× faster** than spdlog sync and **3.6–4.7× faster** than Quill async.

The same benchmark with a counting sink, averaged across 3 runs, isolates formatting and synchronisation cost from file I/O:

| Library | Mode | 1 thread | 2 threads | 4 threads | 8 threads |
|---|---|---|---|---|---|
| **Nova** | synchronized sink | **46 ns** | **76 ns** | **79 ns** | **105 ns** |
| spdlog | sync (mutex) | 155 ns | 91 ns | 48 ns | 33 ns |
| Quill | async, blocking queue | 364 ns | 363 ns | 410 ns | 483 ns |
| spdlog | async, blocking queue | 575 ns | 578 ns | 3,054 ns | 7,046 ns |

Key observations:

- **File I/O accounts for 74–76% of Nova's per-message cost** at 1–4 threads. The counting-sink baseline of 47 ns is the formatter plus mutex. At 8 threads it rises to 105 ns as threads compete for the single mutex.
- **spdlog sync counting-sink numbers decrease at higher thread counts** - this is a wall-clock artifact of dividing 1M messages across N threads, but there is also a genuine concurrency effect. spdlog sync's critical section is ~155 ns (mutex + `{fmt}` + counting sink). At 8 threads, threads spread their lock attempts naturally - by the time one thread releases and re-attempts, others have had time to proceed. Nova's ~46 ns critical section is so short that threads immediately contend again after release, creating a hot-lock pattern. Counterintuitively, Nova's faster formatter causes more relative mutex contention at high thread counts: Nova degrades from 46 ns to 105 ns at 8T while spdlog sync's wall-clock time falls due to the combined effect of parallelism and natural backoff.
- **Quill's ~370–490 ns counting-sink cost is blocking queue and backend formatting overhead.** spdlog and Quill both use `{fmt}` for record formatting. Quill's backend dequeues records and runs `{fmt}` on each before calling the sink; spdlog async does the same in its backend thread. Since the counting-sink's own cost (~1 ns) is negligible, the measured time is essentially blocking queue wait plus `{fmt}` formatting - no I/O.
- **spdlog async degrades sharply under multi-thread load even without file I/O** - from 572 ns at 1 thread to 6,895 ns at 8 threads with a counting sink. This strongly implicates MPMC queue contention rather than I/O as the primary cause, since I/O is absent from this measurement.

### Async throughput (drop-on-full)

Sustained delivery rate to a file over a 5-second window. Figures are averages of 8 independent runs. Attempted/s is the producer call rate (logged/s from the benchmark output).

| Library | Metric | 1 thread | 2 threads | 4 threads | 8 threads |
|---|---|---|---|---|---|
| **Nova** | attempted/s | 25.8M/s | 37.7M/s | 46.1M/s | 39.1M/s |
| **Nova** | delivered/s | **2.98M/s** (12%) | **2.98M/s** (8%) | **2.98M/s** (6%) | **2.97M/s** (8%) |
| Quill | attempted/s | 89.2M/s | 176.7M/s | 327.8M/s | 535.7M/s |
| Quill | delivered/s | 2.39M/s (3%) | 2.15M/s (1%) | 1.94M/s (<1%) | 1.52M/s (<1%) |
| spdlog | attempted/s | 3.3M/s | 2.2M/s | 1.8M/s | 1.5M/s |
| spdlog | delivered/s | 1.60M/s (49%) | 729K/s (33%) | 353K/s (20%) | 167K/s (11%) |

Nova's delivered rate is flat across all thread counts; Quill and spdlog degrade under multi-thread load. Quill's high attempted rate (535M/s at 8 threads) reflects its ~13 ns frontend enqueue cost - records enter a per-thread SPSC queue almost instantly - but 99.7% are dropped because the single backend thread formats and writes at ~1.5M/s.

#### Counting sink - isolating I/O from queue overhead

The same benchmark with a counting sink, varied by Nova pool size. With a 256 KB pool the pool fills immediately under load and ~87% of records are dropped - the consumer is slowed by competing with producers for the small pool. Larger pools reveal the consumer's true processing rate.

| Configuration | 1 thread | 2 threads | 4 threads | 8 threads | Drop% at 1T |
|---|---|---|---|---|---|
| Nova 256 KB pool | 2.98M/s | 2.97M/s | 2.98M/s | 2.97M/s | 87% |
| Nova 512 KB pool | 9.08M/s | 5.49M/s | 4.71M/s | 4.42M/s | 0% |
| Nova 1 MB pool | 8.89M/s | 5.74M/s | 4.82M/s | 4.16M/s | 0% |
| Quill 256 KB/thread | 6.79M/s | 6.57M/s | 5.41M/s | 3.91M/s | 92% |
| spdlog 256 KB×N | 2.18M/s | 1.99M/s | 588K/s | 244K/s | 0% |

A 512 KB pool eliminates all drops and reaches the same delivered rate as 1 MB and larger. **The consumer's true throughput ceiling on this hardware is ~9M/s at 1 thread**, degrading under multi-thread load due to atomic queue contention between producers and the single consumer. Pool sizes beyond 512 KB show no further improvement, meaning capacity is not the bottleneck beyond that point.

The Quill and spdlog rows use the same counting-sink multithreaded benchmark with each library's standard configuration - the same benchmark used for the file-sink throughput figures above.

The 256 KB default is appropriate for memory-constrained embedded and RTOS targets. For hosted Linux/Windows deployments, a 512 KB pool delivers approximately 3× the async throughput of the default:

```cpp
// memory-conservative default (embedded/RTOS)
MemoryPoolAsyncSink<256 * 1024> asyncSink{ fileSink };

// higher throughput for hosted platforms
MemoryPoolAsyncSink<512 * 1024> asyncSink{ fileSink };
```

The optimal pool size is workload and platform dependent - profile on the target hardware.

### Running the benchmarks

```bash
cd benchmarks && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .

# guaranteed delivery - file sink (5+ runs recommended)
python ../benchmark_delivery_latency.py --exe ./benchmark_delivery_latency \
    --threads 1 2 4 8 --messages 1000000 --out delivery_file.txt

# guaranteed delivery - counting sink
python ../benchmark_delivery_latency.py --exe ./benchmark_delivery_latency \
    --threads 1 2 4 8 --messages 1000000 --null-sink --out delivery_null.txt

# async throughput - file sink (5+ runs recommended)
python ../benchmark_multithreaded.py --exe ./benchmark_multithreaded \
    --threads 1 2 4 8 --duration 5 --out multithreaded_file.txt

# async throughput - counting sink, Nova pool size investigation
python ../benchmark_multithreaded.py --exe ./benchmark_multithreaded \
    --threads 1 2 4 8 --duration 5 --nova-only --null-sink --pool-kb 256
python ../benchmark_multithreaded.py --exe ./benchmark_multithreaded \
    --threads 1 2 4 8 --duration 5 --nova-only --null-sink --pool-kb 512
```

Third-party libraries (spdlog, Quill) are fetched automatically via FetchContent at configure time.
