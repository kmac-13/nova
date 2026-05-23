# Flare - Crash-Resilient Forensic Logging for Nova

## Overview

Flare is a forensic logging library designed to capture **high-value diagnostic records during crashes or fatal system failures**.  It integrates with Nova by implementing the `nova::Sink` interface, but remains fully independent of Nova's internals.

Where Nova focuses on explicit, deterministic log routing during normal execution, Flare focuses on **leaving reliable trace data when normal execution cannot continue**.

---

## Terminology

| Term | Meaning |
|------|---------|
| **TLV** | Type-Length-Value - a binary encoding format where each field carries its own type tag and byte length, allowing readers to skip unknown fields and tolerate partial records |
| **ASLR** | Address Space Layout Randomization - a security feature that randomizes where the OS loads executables in memory; Flare records the ASLR offset so post-mortem tools can convert runtime addresses to static binary addresses without needing the running process |
| **Async-signal-safe** | A function is async-signal-safe if it is safe to call from a POSIX signal handler without risk of deadlock or data corruption, i.e. it uses no locks, no heap allocation, and no non-reentrant library calls |

---

## Design Goals

Flare is designed to:
- operate safely in crash or near-crash conditions
- avoid dynamic allocation during emission
- avoid locks, exceptions, and complex dependencies
- produce a forward-compatible binary record format
- tolerate partial writes and corrupted data
- enable post-mortem analysis after process termination

---

## Relationship to Nova

- Flare depends on Nova
- Nova has no knowledge of Flare
- Flare implements `nova::Sink`
- Flare's custom sink can be used as a sink alongside any other Nova sink

Nova handles when and where logging is routed.
Flare handles how records are persisted safely under failure conditions.

---

## Quick Setup

```cpp
#include <kmac/nova.h>
#include <kmac/flare.h>
#include <fcntl.h>

struct CrashTag {};
NOVA_LOGGER_TRAITS( CrashTag, CRASH, true, kmac::nova::TimestampHelper::steadyNanosecs );

// static storage - must outlive any log calls
static int _flareFd = open( "/var/log/crash.flare", O_WRONLY | O_CREAT | O_APPEND, 0644 );
static kmac::flare::FdWriter _flareWriter( _flareFd );
static kmac::flare::EmergencySink<> _flareSink( &_flareWriter );

// bind before installing signal handlers
void setupFlare()
{
	kmac::nova::Logger< CrashTag >::bindSink( &_flareSink );
}

// in a POSIX signal handler - safe to call
void onSignal( int sig )
{
	NOVA_FLARE_LOG( CrashTag ) << "Signal " << sig;
	_Exit( 128 + sig );
}
```

All Flare state must be initialized **before** installing signal handlers.  See `FLARE_USE_CASES.md` for detailed usage patterns.

---

## EmergencySink

Flare's primary sink is `flare::EmergencySink`.

Characteristics:
- writes records to a preconfigured `IWriter` destination
- uses a simple binary TLV (Type-Length-Value) format
- does not allocate during emission
- may emit partial or torn records
- does not attempt to repair torn or partial writes - it emits what it can and stops
- does not drain Nova's async queues (`MemoryPoolAsyncSink` etc.) - records queued but not yet delivered when a crash occurs will be lost

Flare makes no guarantees that records are complete - only that **best-effort data is preserved**.

---

## Writers

Flare separates record encoding (`EmergencySink`) from output destination (`IWriter`).  Four writers are currently available:

| Writer | Transport | Async-Signal-Safe | Platform |
|--------|-----------|-------------------|----------|
| `FdWriter` | POSIX file descriptor (`write`/`fsync`) | ✅ Yes | Linux, macOS, Android, FreeBSD |
| `FileWriter` | C `FILE*` (`fwrite`/`fflush`) | ❌ No | All hosted |
| `RamWriter` | Fixed-size RAM buffer | ✅ Yes | All (no OS dependency) |
| `UartWriter` | User-supplied write callback | Depends on callback | Bare-metal / RTOS |

**`FdWriter` is strongly recommended for signal handlers and crash contexts** - it uses only `write()` and `fsync()`, which are POSIX async-signal-safe functions.  `FileWriter` uses `fwrite()`/`fflush()` which are not async-signal-safe and may deadlock if the crash occurs while the process is inside the C stdio library.

---

## Human-Readable vs Binary Output

Flare's `EmergencySink` always writes binary TLV - it does not produce human-readable text directly.  Human-readable output requires post-mortem decoding via `flare_reader.py` or the C++ `Reader` API.

Nova's regular sinks (`OStreamSink`, `FormattingFileSink` with a formatter, etc.) produce human-readable text during normal operation.  Both can run simultaneously on the same or different tags.  To route the same tag to both a human-readable sink and an `EmergencySink`, use a compositing sink such as `FixedCompositeSink`:

```cpp
#include <kmac/nova/extras/fixed_composite_sink.h>
#include <kmac/nova/extras/ostream_sink.h>

kmac::nova::extras::OStreamSink consoleSink( std::cout );

kmac::nova::Sink* sinks[] = { &consoleSink, &_flareSink };
kmac::nova::extras::FixedCompositeSink composite( sinks, 2 );
kmac::nova::Logger< CrashTag >::bindSink( &composite );

// CrashTag records now go to both human-readable console (through OStreamSink) and binary Flare file (through EmergencySink)
```

---

## Macros

Flare provides two macros as aliases for the **stack-based** Nova builder macros:

```cpp
// include <kmac/flare.h>

NOVA_FLARE_LOG( Tag )              // alias for NOVA_LOG_STACK - safe in signal handlers
NOVA_FLARE_LOG_BUF( Tag, Size )    // alias for NOVA_LOG_BUF_STACK - custom buffer size
```

Both are stack-allocated and re-entrant safe.  They do not use TLS and will not trigger the nested-logging assertion.

> ⚠️ Only `NOVA_FLARE_LOG` and `NOVA_FLARE_LOG_BUF` paired with an async-signal-safe sink (e.g. `EmergencySink` with `FdWriter`) are safe to call from signal handlers.  Any Nova logging that reaches a sink using locks, heap allocation, or non-reentrant library calls will be unsafe regardless of which builder macro is used.  Avoid calling into code from signal handlers that may perform logging through non-signal-safe sinks.

---

## Record Format

Each Flare record consists of:
- a fixed header (magic, version, size)
- a sequence of TLVs
- no footer (by design - truncated records remain parseable)

### TLV Types

| Category | TLV | Contents |
|----------|-----|----------|
| Framing | `RecordBegin`, `RecordSize`, `RecordStatus`, `SequenceNumber` | Record boundaries and ordering |
| Metadata | `TimestampNs`, `TagId`, `FileName`, `LineNumber`, `FunctionName`, `ProcessId`, `ThreadId` | Log context |
| Payload | `MessageBytes`, `MessageTruncated` | Log message content |
| Crash context (address) | `FaultAddress`, `LoadBaseAddress`, `AslrOffset`, `StackFrames` | Signal fault address and call stack for symbolization |
| Crash context (registers) | `RegisterLayout`, `CpuRegisters` | CPU register snapshot at point of fault |

Readers must tolerate:
- unknown TLVs (forward compatibility)
- truncated records
- corrupted payloads

---

## Signal Handler Integration

### POSIX: `SignalHandler<>`

`SignalHandler<>` installs POSIX signal handlers using `sigaltstack` and `SA_SIGINFO | SA_ONSTACK`.  On a fatal signal it captures:
- fault address (from `siginfo_t::si_addr`)
- ASLR (Address Space Layout Randomization) offset and load base address - needed to convert runtime stack frame addresses back to static binary addresses for symbolization with tools like `addr2line`
- CPU registers (`CpuRegisters` TLV)

```cpp
#include <kmac/flare.h>

// install after EmergencySink is set up
kmac::flare::SignalHandler<>::install( &_flareSink );
```

`SignalHandler<>` is POSIX-only (Linux, macOS, Android, FreeBSD).

### Bare-Metal: `BareMetalFaultHandler`

`BareMetalFaultHandler` provides C-linkage vector table entry points for ARM Cortex-M (HardFault, BusFault, MemManage, UsageFault) and RISC-V trap handlers.  On fault it reads MMFAR/BFAR or `mtval`, populates a `FaultContext`, and calls `processWithFaultContext` on the bound `EmergencySink`.

---

## Scanner and Reader

Flare separates concerns:

- **Scanner**
  - locates candidate records in a raw byte stream
  - resynchronizes after corruption or partial writes
  - never allocates or throws
- **Reader**
  - decodes validated records
  - exposes structured access to TLVs
  - does not assume completeness

This separation allows robust recovery even from severely damaged logs.

---

## Post-Mortem Analysis

### `flare_reader.py`

The Python reader (`libs/nova_flare/scripts/flare_reader.py`) decodes binary Flare files:

```bash
# basic text output
python3 flare_reader.py crash.flare

# with tag name dictionary
python3 flare_reader.py crash.flare --dict crash.tags

# JSON output
python3 flare_reader.py crash.flare --format json

# with symbolization (requires addr2line and the original binary)
python3 flare_reader.py crash.flare --binary ./myapp --addr2line addr2line
```

### C++ API

```cpp
#include <kmac/flare/scanner.h>
#include <kmac/flare/reader.h>

// scan a buffer for records
kmac::flare::Scanner scanner;
scanner.scan( data, size );

// read each record
for ( const auto& span : scanner.records() )
{
	kmac::flare::Reader reader( span.data, span.size );
	// access TLVs via reader.getTimestamp(), reader.getMessage(), etc.
}
```

---

## What Flare Is Not

Flare does **not** provide:
- guaranteed record completeness
- encryption or compression
- automatic repair of corrupted data
- symbolication or stack unwinding (but captures data needed for both)
- human-readable output formatting

Flare's responsibility ends at **preserving raw forensic data**.

---

## When Flare Is a Good Fit

Flare is appropriate when:
- post-crash diagnostics matter
- logging must survive undefined behavior
- allocations and locks are unsafe
- deterministic behavior is required under failure

Flare is not intended as a replacement for normal application logging.

---

## Versioning and Guarantees (v1)

- binary record format is append-only
- TLV type IDs will never be repurposed
- readers must tolerate unknown fields
- writers may emit partial records
- readers must never throw during scanning
