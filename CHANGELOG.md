# Changelog

All notable changes to Nova, Nova Extras, and Flare will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [1.0.0] - 2026-05-23

Initial stable release of the Nova logging system.

**Nova** is a domain-based C++ logging core with compile-time routing, zero-cost disabled tags, and no global state.  Requires C++11; C++17 recommended for language-level zero-cost guarantees.

**Nova Extras** is a collection of sinks, formatters, async delivery, and builder extensions that complement Nova core.

**Flare** is an async-signal-safe forensic logging library built on Nova's sink interface, designed for crash-resilient record capture and post-mortem analysis.

### Supported Platforms

| Platform | Status |
|----------|--------|
| Linux | ✅ Tested in CI |
| macOS | ✅ Supported |
| Windows (MSVC / MinGW) | ✅ Tested in CI |
| Android | ✅ Tested in CI |
| ARM Cortex-M3/M4 (bare-metal, QEMU) | ✅ Tested in CI |
| FreeRTOS (QEMU) | ✅ Tested in CI |
| Other RTOS / bare-metal | ⚠️ Compiles in CI; runtime not verified |

### Supported Compilers

C++11 minimum; C++17 recommended.

CI-tested compilers:
- GCC 13 (Linux)
- Clang 18 (Linux)
- Apple Clang (macOS)
- MSVC latest (Windows)

Minimum supported versions: GCC 5.0+, Clang 3.3+, MSVC 2015+, ARM Compiler 6+.  Older versions may work but are not verified in CI.

### Known Limitations

- no built-in configuration file parsing - logging configuration is expressed in C++ code
- no runtime logger discovery - there is no API to enumerate bound tags or sinks
- TLS builders (`NOVA_LOG`, `NOVA_LOG_BUF`) are not re-entrant: nested calls on the same thread assert in debug builds and silently drop in release builds; use `NOVA_LOG_STACK` for re-entrant contexts
- no lossless async delivery - all async sinks drop records when the delivery queue or pool is full

### Documentation

See `docs/` for full documentation.  Key starting points:

- `README.md` - project overview, quick start, and benchmark summary
- `docs/NOVA_README.md` - Nova core concepts
- `docs/NOVA_EXTRAS_REFERENCE.md` - Nova Extras component reference
- `docs/FLARE_README.md` - Flare architecture and setup
- `docs/FAQ.md` - frequently asked questions
- `docs/LIBRARY_MIGRATION.md` - migrating from spdlog, glog, log4cplus, and others
- `docs/BENCHMARKS.md` - performance results

---

## Links

- [Repository](https://github.com/kmac-13/nova)
- [Issues](https://github.com/kmac-13/nova/issues)
- [License](LICENSE) - BSD-3-Clause
