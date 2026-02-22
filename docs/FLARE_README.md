# Flare — Crash-Resilient Forensic Logging for Nova

## Overview

Flare is a forensic logging library designed to capture **high-value diagnostic records during crashes or fatal system failures**.  It integrates with Nova by implementing the `nova::Sink` interface, but remains fully independent of Nova’s internals.

Where Nova focuses on explicit, deterministic log routing during normal execution, Flare focuses on **leaving reliable trace data when normal execution cannot continue**.

---

## Design Goals

Flare is designed to:
- Operate safely in crash or near-crash conditions
- Avoid dynamic allocation during emission
- Avoid locks, exceptions, and complex dependencies
- Produce a forward-compatible binary record format
- Tolerate partial writes and corrupted data
- Enable post-mortem analysis after process termination

---

## Relationship to Nova

- Flare depends on Nova
- Nova has no knowledge of Flare
- Flare implements `nova::Sink`
- Flare can be used as a sink alongside any other Nova sink

Nova handles when and where logging is routed.
Flare handles how records are persisted safely under failure conditions.

## EmergencySink

Flare’s primary sink is `flare::EmergencySink`.

Characteristics:
- Writes records to a preconfigured destination, e.g. a file or memory region
- Uses a simple binary TLV (Type-Length-Value) format
- Does not allocate during emission
- May emit partial or torn records
- Does not attempt recovery during writing

Flare makes no guarantees that records are complete — only that **best-effort data is preserved**.

---

## Record Format (High Level)

Each emergency record consists of:
- A fixed header (magic, version, size)
- A sequence of TLVs
- No footer (by design)

TLVs may include:
- Timestamp
- Tag
- Message payload
- Process or thread identifiers
- Future extensions

Readers must tolerate:
- Unknown TLVs
- Truncated records
- Corrupted payloads

---

## Scanner and Reader

Flare separates concerns:

- **Scanner**
  - Locates candidate records
  - Resynchronizes after corruption
  - Never allocates or throws
- **Reader**
  - Decodes validated records
  - Exposes structured access to TLVs
  - Does not assume completeness

This separation allows robust recovery even from severely damaged logs.

---

## What Flare Is Not

Flare does **not** provide:
- Guaranteed record completeness
- Encryption or compression
- Automatic **repair** of corrupted data
- Symbolication or stack unwinding
- Human-readable output formatting

Flare’s responsibility ends at **preserving raw forensic data**.

---

## When Flare Is a Good Fit

Flare is appropriate when:
- Post-crash diagnostics matter
- Logging must survive undefined behavior
- Allocations and locks are unsafe
- Deterministic behavior is required under failure

Flare is not intended as a replacement for normal application logging.

---

## Versioning and Guarantees (v1)

- Binary record format is append-only
- TLV type IDs will never be repurposed
- Readers must tolerate unknown fields
- Writers may emit partial records
- Readers must never throw during scanning
