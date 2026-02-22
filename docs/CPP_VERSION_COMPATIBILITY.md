# Nova C++ Version Compatibility

## Overview

Nova now supports C++11, C++14, C++17, and C++20+ with graceful feature degradation.

## Version Support Matrix

| C++ Version | Status | Notes |
|-------------|--------|-------|
| **C++17+** | ✅ Full Support | Recommended. Zero-cost disabled logging with `if constexpr` |
| **C++14** | ✅ Supported | Works with minimal overhead. `if constexpr` becomes runtime `if` |
| **C++11** | ✅ Supported | Works with minimal overhead. `if constexpr` becomes runtime `if` |
| **C++03** | ❌ Not Supported | Missing required features (constexpr, variadic templates) |

## Feature Differences by Version

### C++17 and Later (Recommended)

**Benefits:**
- ✅ Zero-cost disabled logging (`if constexpr` eliminates code)
- ✅ Automatic detection with `__has_include`
- ✅ Best optimization and smallest code size
- ✅ No compiler warnings

**Example:**
```cpp
#include <kmac/nova/logger.h>

struct DisabledTag {};
NOVA_LOGGER_TRAITS(DisabledTag, DISABLED, false, ...);

// This line compiles to NOTHING in C++17:
NOVA_LOG_TRUNC(DisabledTag) << "Never executed";
```

### C++14

**Benefits:**
- ✅ Full API compatibility
- ✅ Works with all Nova features
- ⚠️ May have `__has_include` support (compiler-dependent)

**Limitations:**
- ⚠️ Disabled logging uses runtime check (very fast, but not zero-cost)
- ⚠️ Compiler warnings about `if constexpr` (can be ignored)

**Example:**
```cpp
#include <kmac/nova/logger.h>

struct DisabledTag {};
NOVA_LOGGER_TRAITS(DisabledTag, DISABLED, false, ...);

// In C++14, this compiles to a runtime if-statement:
NOVA_LOG_TRUNC(DisabledTag) << "Checked at runtime";
// Still very fast (branch prediction), just not completely free
```

**Expected warnings:**
```
warning: 'if constexpr' only available with '-std=c++17'
```

These warnings are harmless - the code falls back to runtime `if`.

### C++11

**Benefits:**
- ✅ Full API compatibility
- ✅ Works with all Nova features

**Limitations:**
- ⚠️ Disabled logging uses runtime check
- ⚠️ Compiler warnings about `if constexpr`
- ⚠️ No `__has_include` - must configure manually

**Example:**
```cpp
// Manual configuration required for C++11
#define NOVA_NO_CHRONO  // If std::chrono unavailable

#include <kmac/nova/logger.h>

// Implement timestamp
namespace kmac { namespace nova { namespace platform {
    std::uint64_t steadyNanosecs() noexcept {
        return /* your implementation */;
    }
}}}
```

## Performance Impact

### Disabled Tags

| Version | Overhead | Code Generated |
|---------|----------|----------------|
| C++17+ | **Zero** | No code at all |
| C++14 | **~1-2 ns** | Runtime branch (predicted) |
| C++11 | **~1-2 ns** | Runtime branch (predicted) |

### Enabled Tags

All versions have identical performance for enabled tags:
- Atomic operations: Same
- Timestamp calls: Same
- Sink processing: Same

## Compilation Differences

### C++17 - Clean Compilation

```bash
g++ -std=c++17 -Inova/include test.cpp
# No warnings, optimal code generation
```

### C++14 - Warnings Expected

```bash
g++ -std=c++14 -Inova/include test.cpp
# Warning: 'if constexpr' only available with '-std=c++17'
# Code works perfectly, just warns about the fallback
```

**Suppress warnings (if desired):**
```bash
g++ -std=c++14 -Wno-c++17-extensions -Inova/include test.cpp
```

### C++11 - Manual Configuration

```bash
# Define features manually
g++ -std=c++11 -DNOVA_NO_CHRONO -Inova/include test.cpp
```

## Diagnostic Mode by Version

Enable diagnostics to see what's happening:

```cpp
#define NOVA_DIAGNOSTICS
#include <kmac/nova/logger.h>
```

**C++17 Output:**
```
Nova Platform Configuration:
  C++ version: C++17
  std::atomic: available
  std::chrono: available
  std::array: available
  Platform: POSIX
  __has_include: supported
```

**C++14 Output:**
```
Nova Platform Configuration:
  C++ version: C++14
  WARNING: Pre-C++17 detected. Disabled tags will have minimal runtime overhead.
  std::atomic: available
  std::chrono: available
  std::array: available
  Platform: POSIX
  __has_include: supported
```

**C++11 Output:**
```
Nova Platform Configuration:
  C++ version: C++11
  WARNING: Pre-C++17 detected. Disabled tags will have minimal runtime overhead.
  Note: __has_include not available, assuming std::chrono is present
        Define NOVA_NO_CHRONO manually if std::chrono is unavailable
  std::atomic: available
  std::chrono: available (assumed)
  std::array: available (assumed)
  Platform: POSIX
  __has_include: NOT supported (manual configuration required)
```

## Recommendations by Use Case

### New Projects
**Use C++17 or later**
- Best performance
- Cleanest compilation
- Automatic detection
- Future-proof

### Legacy Codebases (C++11/14)
**Nova works fine, but:**
- Consider upgrading to C++17 if possible
- Accept minimal runtime overhead for disabled tags
- Manually configure NOVA_NO_* flags on C++11

### Safety-Critical Projects
**Use C++17 or later**
- Zero-cost disabled logging
- More predictable code generation
- Better for static analysis

### Embedded Systems (C++11/14)
**Nova fully supports you**
- Define `NOVA_BARE_METAL` explicitly
- Implement `platform::steadyNanosecs()`
- Accept ~1-2ns overhead for disabled tags

## Migration Guide

### From C++11/14 to C++17

**Before (C++14):**
```cpp
#define NOVA_NO_CHRONO
#include <kmac/nova/logger.h>

// Warnings about if constexpr
// Runtime checks for disabled tags
```

**After (C++17):**
```cpp
#include <kmac/nova/logger.h>

// No warnings
// Zero-cost disabled tags
// Auto-detection works
```

**Build system change:**
```bash
# CMakeLists.txt
- set(CMAKE_CXX_STANDARD 14)
+ set(CMAKE_CXX_STANDARD 17)

# Makefile
- CXXFLAGS += -std=c++14
+ CXXFLAGS += -std=c++17
```

## Known Issues

### C++11/14: if constexpr Warnings

**Issue:** Compiler warns about `if constexpr` being C++17-only

**Impact:** Cosmetic only - code works perfectly

**Solutions:**
1. Upgrade to C++17 (recommended)
2. Suppress warning: `-Wno-c++17-extensions` (GCC/Clang)
3. Ignore - warning is harmless

### C++11: No Auto-Detection

**Issue:** `__has_include` not available in C++11

**Impact:** Must manually define NOVA_NO_* flags

**Solution:**
```cpp
#define NOVA_NO_CHRONO  // If std::chrono missing
#define NOVA_NO_ATOMIC  // If std::atomic missing
#include <kmac/nova/logger.h>
```

## Testing

All versions tested and verified:

✅ **C++17:** Full zero-cost, auto-detection
✅ **C++14:** Runtime fallback, may have auto-detection
✅ **C++11:** Runtime fallback, manual configuration

## Summary

Nova supports C++11 through C++20+ with these trade-offs:

| Feature | C++11 | C++14 | C++17+ |
|---------|-------|-------|---------|
| Works | ✅ | ✅ | ✅ |
| Zero-cost disabled tags | ❌ | ❌ | ✅ |
| Auto-detection | ❌ | Maybe | ✅ |
| Clean compilation | ⚠️ | ⚠️ | ✅ |
| Performance impact | ~1-2ns | ~1-2ns | 0ns |

**Recommendation:** Use C++17 or later for best experience, but C++11/14 work fine if needed.
