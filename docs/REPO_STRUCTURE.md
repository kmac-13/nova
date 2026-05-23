# Repository Structure

```
.
├── libs/
│   ├── nova/                   # Nova core (header-only)
│   │   └── include/kmac/nova/
│   │       └── platform/       # platform abstraction headers
│   ├── nova_extras/            # sinks, formatters, async queue, builders
│   │   ├── include/
│   │   └── src/
│   └── nova_flare/             # crash/forensic logging
│       ├── include/
│       ├── src/
│       └── scripts/            # flare_reader.py, test_flare_reader.py
│
├── examples/                   # numbered working examples
│   ├── 01_basic_usage/
│   ├── 02_multiple_sinks/
│   ├── 03_custom_clock/
│   ├── 04_multithreading/
│   ├── 05_filtering/
│   ├── 06_hierarchical_tags/
│   ├── 07_flare/
│   ├── 08_bare_metal/
│   └── 09_diagnostics/
│
├── tests/                      # Google Test suite
│   ├── bare_metal/             # bare-metal runtime checks (run under QEMU)
│   └── rtos/                   # RTOS runtime checks (run under QEMU/FreeRTOS)
│
├── benchmarks/                 # Google Benchmark suite + third-party comparisons
├── fuzz/                       # LibFuzzer targets
├── cmake/                      # CMake modules and cross-compile toolchains
│   ├── NovaConfig.cmake.in     # find_package support
│   ├── Sanitizers.cmake        # sanitizer flag helpers
│   └── toolchains/
│       ├── arm-none-eabi-cortex-m3.cmake
│       ├── arm-none-eabi-cortex-m4.cmake
│       └── android-ndk.cmake
│
└── docs/

    # Core library
    ├── NOVA_README.md              # Nova core design, tags, records, sinks, configurator
    ├── NOVA_BUILDERS_README.md     # record builder variants compared
    ├── NOVA_EXTRAS_REFERENCE.md    # full Nova Extras component reference

    # Flare
    ├── FLARE_README.md             # Flare architecture, writers, signal handler setup, TLV format
    ├── FLARE_USE_CASES.md          # crash forensics use cases and patterns

    # Platform & integration
    ├── BARE_METAL_GUIDE.md         # bare-metal and RTOS porting
    ├── CMAKE_INTEGRATION.md        # CMake integration patterns and build options
    ├── CONFIG_AUTO_DETECTION_GUIDE.md  # platform auto-detection and NOVA_ENABLE_DIAGNOSTICS
    ├── CPP_VERSION_COMPATIBILITY.md    # C++11/14/17 compatibility notes

    # Performance & comparison
    ├── BENCHMARKS.md               # methodology, results, and comparisons
    └── LIBRARY_COMPARISON.md       # feature matrix vs spdlog, Quill, Boost.Log, glog, etc.

    # Project & operations
    ├── CI.md                       # all CI workflows — build targets, sanitizers, fuzzing
    ├── CODING_GUIDELINES.md        # coding standards and style conventions
    ├── FAQ.md                      # frequently asked questions
    ├── LIBRARY_MIGRATION.md        # migration guides from spdlog, glog, and others
    ├── REPO_STRUCTURE.md           # this file
    └── SAFETY_CRITICAL_GUIDELINES.md  # per-component guidance for DO-178C / IEC 61508 / ISO 26262
```
