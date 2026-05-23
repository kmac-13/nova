# CI Workflows

All workflows run on push to `main`/`develop` and on every pull request.  LibFuzzer additionally runs nightly for extended fuzzing sessions.

[![License](https://img.shields.io/badge/license-BSD--3-blue.svg)](LICENSE)
[![C++11+](https://img.shields.io/badge/C%2B%2B-11%2B-blue.svg)](https://en.cppreference.com/w/cpp/11)
[![Version](https://img.shields.io/badge/version-1.0.0-blue.svg)](libs/nova/include/kmac/nova/version.h)

[![clang-tidy](https://github.com/kmac-13/nova/actions/workflows/clang-tidy.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/clang-tidy.yml)
[![cppcheck](https://github.com/kmac-13/nova/actions/workflows/cppcheck.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/cppcheck.yml)
[![lizard](https://github.com/kmac-13/nova/actions/workflows/lizard.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/lizard.yml)

[![ASan](https://github.com/kmac-13/nova/actions/workflows/sanitizers-asan.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/sanitizers-asan.yml)
[![DFSan](https://github.com/kmac-13/nova/actions/workflows/sanitizers-dfsan.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/sanitizers-dfsan.yml)
[![LSan](https://github.com/kmac-13/nova/actions/workflows/sanitizers-lsan.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/sanitizers-lsan.yml)
[![MSan](https://github.com/kmac-13/nova/actions/workflows/sanitizers-msan.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/sanitizers-msan.yml)
[![TSan](https://github.com/kmac-13/nova/actions/workflows/sanitizers-tsan.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/sanitizers-tsan.yml)
[![UBSan](https://github.com/kmac-13/nova/actions/workflows/sanitizers-ubsan.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/sanitizers-ubsan.yml)
[![LibFuzzer](https://github.com/kmac-13/nova/actions/workflows/sanitizers-libfuzzer.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/sanitizers-libfuzzer.yml)

[![Build / Bare-Metal](https://github.com/kmac-13/nova/actions/workflows/build-bare-metal.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/build-bare-metal.yml)
[![Build / Bare-Metal (Test)](https://github.com/kmac-13/nova/actions/workflows/build-bare-metal-test.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/build-bare-metal-test.yml)

[![Build / RTOS](https://github.com/kmac-13/nova/actions/workflows/build-rtos.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/build-rtos.yml)
[![Build / RTOS (Test)](https://github.com/kmac-13/nova/actions/workflows/build-rtos-test.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/build-rtos-test.yml)

[![Build / Hosted](https://github.com/kmac-13/nova/actions/workflows/build-hosted.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/build-hosted.yml)
[![Build / Android (Test)](https://github.com/kmac-13/nova/actions/workflows/build-android-test.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/build-android-test.yml)


---

## Build workflows

### Build / Bare-Metal

[![Build / Bare-Metal](https://github.com/kmac-13/nova/actions/workflows/build-bare-metal.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/build-bare-metal.yml)

Confirms that the bare-metal example and library compile cleanly across four configurations without requiring a physical target or QEMU execution.

| Job | Description |
|---|---|
| Bare-Metal / Hosted | Compiles the bare-metal example on a hosted Linux target with `NOVA_BARE_METAL` defined - fast build-time sanity check |
| Bare-Metal / No-Stdlib-Headers Hosted | Same as above but with `NOVA_NO_CHARCONV` and `NOVA_NO_STRING_VIEW` forced, catching anything that depends on headers which may be absent on strict freestanding targets |
| Bare-Metal / ARM Cortex-M4 | Cross-compiled with `arm-none-eabi-g++` for Cortex-M4 |
| Bare-Metal / ARM Cortex-M4 (newlib-nano) | Same cross-compile with `--specs=nano.specs` to test compatibility with the size-optimised newlib-nano C runtime |

---

### Build / Bare-Metal (Test)

[![Build / Bare-Metal (Test)](https://github.com/kmac-13/nova/actions/workflows/build-bare-metal-test.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/build-bare-metal-test.yml)

Builds `test_nova_bare_metal` and executes it, verifying that all runtime checks pass under actual bare-metal constraints.  See `tests/bare_metal/test_nova_bare_metal.cpp` for the full list.

| Job | Description |
|---|---|
| Bare-Metal / Hosted / Test | Compiled with `NOVA_BARE_METAL` and run natively - validates the bare-metal code paths (`NOVA_NO_STD`, `NOVA_NO_ATOMIC`, `NOVA_NO_CHRONO`) in a hosted environment |
| Bare-Metal / ARM Cortex-M3 / QEMU / Test | Cross-compiled for Cortex-M3 (`mps2-an385` machine) and executed under QEMU with semihosting; QEMU translates the semihosting exit to a host process exit code so the CI step succeeds or fails correctly |

---

### Build / RTOS

[![Build / RTOS](https://github.com/kmac-13/nova/actions/workflows/build-rtos.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/build-rtos.yml)

Confirms that the RTOS example compiles cleanly against a FreeRTOS port without requiring execution.

| Job | Description |
|---|---|
| RTOS / FreeRTOS Hosted | Compiles against the FreeRTOS GCC\_POSIX port on hosted Linux with `NOVA_PLATFORM_FREERTOS` defined |
| RTOS / FreeRTOS ARM Cortex-M3 | Cross-compiled for Cortex-M3 using `arm-none-eabi-g++` |

---

### Build / RTOS (Test)

[![Build / RTOS (Test)](https://github.com/kmac-13/nova/actions/workflows/build-rtos-test.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/build-rtos-test.yml)

Builds `test_nova_rtos` and executes it under FreeRTOS.  See `tests/rtos/test_nova_rtos.cpp` for details.

| Job | Description |
|---|---|
| RTOS / FreeRTOS POSIX / Test | Built against the FreeRTOS GCC\_POSIX port and run natively on hosted Linux |
| RTOS / FreeRTOS ARM Cortex-M3 / QEMU / Test | Cross-compiled for Cortex-M3 (`mps2-an385`) and executed under QEMU with semihosting |

---

### Build / Hosted

[![Build / Hosted](https://github.com/kmac-13/nova/actions/workflows/build-hosted.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/build-hosted.yml)

Builds and runs the full test suite across the primary hosted toolchain and platform matrix.

| Job | Compiler | OS |
|---|---|---|
| Linux / GCC-13 | GCC 13 | ubuntu-24.04 |
| Linux / Clang-18 | Clang 18 | ubuntu-24.04 |
| macOS / Apple Clang | Apple Clang | macos-latest |
| Windows / MSVC | MSVC | windows-latest |
| Android / arm64-v8a | Android NDK 27 | ubuntu-24.04 |
| Android / armeabi-v7a | Android NDK 27 | ubuntu-24.04 |
| Android / x86\_64 | Android NDK 27 | ubuntu-24.04 |

---

### Build / Android (Test)

[![Build / Android (Test)](https://github.com/kmac-13/nova/actions/workflows/build-android-test.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/build-android-test.yml)

Cross-compiles the test suite for Android x86\_64 (API 26) using the NDK toolchain and executes it on a hardware-accelerated Android emulator via `reactivecircus/android-emulator-runner`.  Complements the build-only Android jobs in the hosted workflow by confirming that tests actually pass at runtime on the Android platform.

---

## Static analysis workflows

### clang-tidy

[![clang-tidy](https://github.com/kmac-13/nova/actions/workflows/clang-tidy.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/clang-tidy.yml)

Runs `clang-tidy-18` with `--warnings-as-errors` against the full source tree.  The enabled check set includes `cppcoreguidelines-*`, `modernize-*`, `readability-*`, `performance-*`, and `bugprone-*` groups, with a small number of checks disabled where they conflict with deliberate design decisions (documented in the workflow file).  A cognitive complexity threshold is enforced via `readability-function-cognitive-complexity` (see `.github/workflows/clang-tidy.yml` for the current value); functions that legitimately exceed this must carry a `// NOLINT(readability-function-cognitive-complexity)` suppression with a justification comment in source.

---

### cppcheck

[![cppcheck](https://github.com/kmac-13/nova/actions/workflows/cppcheck.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/cppcheck.yml)

Runs `cppcheck` with `--std=c++17` across the full source tree and uploads the report as a workflow artifact.

---

### lizard

[![lizard](https://github.com/kmac-13/nova/actions/workflows/lizard.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/lizard.yml)

Measures cyclomatic complexity using `lizard` and enforces a per-function threshold.  Functions that legitimately exceed the threshold (e.g. large switch-based state machines in the formatters) are whitelisted in `.github/lizard_whitelist.txt` with documented justifications.  The full per-function report is uploaded as a workflow artifact.

---

## Sanitizer workflows

All sanitizer workflows compile the full test suite with the relevant instrumentation and run it, failing if any issue is detected.

### AddressSanitizer (ASan)

[![ASan](https://github.com/kmac-13/nova/actions/workflows/sanitizers-asan.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/sanitizers-asan.yml)

`-fsanitize=address -fno-omit-frame-pointer`.  Detects heap buffer overflows, stack buffer overflows, use-after-free, use-after-return, and use-after-scope errors.

---

### LeakSanitizer (LSan)

[![LSan](https://github.com/kmac-13/nova/actions/workflows/sanitizers-lsan.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/sanitizers-lsan.yml)

`-fsanitize=leak`.  Detects memory leaks at process exit.  Complements ASan's leak detection with a standalone run that is less likely to be masked by other instrumentation.

---

### MemorySanitizer (MSan)

[![MSan](https://github.com/kmac-13/nova/actions/workflows/sanitizers-msan.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/sanitizers-msan.yml)

`-fsanitize=memory`.  Detects reads of uninitialised memory.  Requires building libc++ and libc++abi from LLVM source with MSan instrumentation (cached between runs); the instrumented standard library is linked statically to avoid false positives from uninstrumented system libraries.

---

### ThreadSanitizer (TSan)

[![TSan](https://github.com/kmac-13/nova/actions/workflows/sanitizers-tsan.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/sanitizers-tsan.yml)

`-fsanitize=thread`.  Detects data races and lock-order violations.  Particularly relevant for the `SynchronizedSink`, `SpinlockSink`, `MemoryPoolAsyncSink`, and `MemoryPoolAsyncBatchSink` components.

---

### UndefinedBehaviorSanitizer (UBSan)

[![UBSan](https://github.com/kmac-13/nova/actions/workflows/sanitizers-ubsan.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/sanitizers-ubsan.yml)

`-fsanitize=undefined`.  Detects signed integer overflow, null pointer dereference, misaligned access, invalid enum values, and other forms of undefined behavior.

---

### DataFlowSanitizer (DFSan)

[![DFSan](https://github.com/kmac-13/nova/actions/workflows/sanitizers-dfsan.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/sanitizers-dfsan.yml)

`-fsanitize=dataflow`.  Tracks the flow of data through the logging pipeline to verify that log records carry only the expected data and that no unintended data paths exist.  An ABI list (`.github/dfsan_abi_list.txt`) marks external functions as uninstrumented.

---

### LibFuzzer

[![LibFuzzer](https://github.com/kmac-13/nova/actions/workflows/sanitizers-libfuzzer.yml/badge.svg)](https://github.com/kmac-13/nova/actions/workflows/sanitizers-libfuzzer.yml)

Runs the fuzzing targets in `fuzz/` with seed corpora, failing if any crash or sanitizer violation is found within the time budget.  Runs nightly with an extended budget in addition to the standard per-PR run.

| Target | What it fuzzes |
|---|---|
| `fuzz_record_builder` | `TruncatingRecordBuilder` - arbitrary byte sequences as message content |
| `fuzz_continuation_builder` | `ContinuationRecordBuilder` - same as above, targeting continuation logic |
| `fuzz_flare_scanner` | `Scanner::scan()` - arbitrary bytes as a Flare binary stream; uses `fuzz/flare.dict` to help the fuzzer generate valid-looking TLV magic numbers |
| `fuzz_flare_reader` | `Reader::parseNext()` - arbitrary bytes as a Flare record; same dictionary |
