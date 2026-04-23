# Nova C++ Version Compatibility

## Overview

Nova supports C++11, C++14, C++17, and C++20+ with graceful feature degradation.
C++17 or later is recommended for the best experience, but C++11 and C++14 are
fully supported with no functional limitations.

## Version Support Matrix

| C++ Version | Status | Notes |
|-------------|--------|-------|
| **C++17+**  | ✅ Full support     | Recommended. Additional standard library features available. |
| **C++14**   | ✅ Full support     | All Nova features available. |
| **C++11**   | ✅ Full support     | All Nova features available. Manual configuration may be required. |
| **C++03**   | ❌ Not supported    | Missing required features (constexpr, variadic templates, nullptr). |

## Disabled Tag Elimination

In all supported standards, logging statements for compile-time disabled tags
are completely eliminated from the compiled output.  The macros use
`std::conditional` to select between a real builder wrapper and a no-op
`NullBuilderWrapper` at compile time.  This guarantees the disabled branch is
never instantiated - equivalent to `if constexpr` but compatible with C++11.

```cpp
struct DisabledTag {};
NOVA_LOGGER_TRAITS( DisabledTag, DISABLED, false, myTimestamp );

// compiles to nothing on C++11, C++14, C++17, and C++20:
NOVA_LOG( DisabledTag ) << "never instantiated";
```

## Feature Differences by Version

### C++17 and Later (Recommended)

- `std::string_view` used directly (via `platform::StringView`)
- `std::to_chars` used for integer and floating-point formatting
- `__has_include` used for automatic stdlib feature detection
- `NOVA_INLINE_VAR` expands to `inline` for ODR-safe constexpr variables
- `NOVA_IF_CONSTEXPR` expands to `if constexpr` for non-logging compile-time branches

### C++14

- `platform::StringView` fallback used (pointer + length wrapper)
- Integer and floating-point formatting use platform fallback implementations
- `__has_include` available on GCC and Clang; manual `NOVA_NO_*` flags may be
  needed on other compilers
- `NOVA_INLINE_VAR` expands to nothing (constexpr variables have internal linkage)
- `NOVA_IF_CONSTEXPR` expands to plain `if` (dead branch eliminated by optimiser)

### C++11

Same as C++14, with one additional constraint:

- `constexpr` functions in Nova core use recursive single-expression form
  rather than loops, since C++11 restricts `constexpr` functions to a single
  `return` statement.  This affects `fnv1a` (tag hash) and `fileName`
  (`__FILE__` path stripping) in `details.h`.

## Auto-Detection

Nova uses `__has_include` to detect stdlib feature availability automatically.
`__has_include` is a C++17 standard feature but is also supported as a compiler
extension by GCC and Clang in C++11/14 mode.  On compilers that do not support
it, define the appropriate `NOVA_NO_*` flags manually before including any Nova
header:

```cpp
#define NOVA_NO_CHRONO       // if std::chrono is unavailable
#define NOVA_NO_ATOMIC       // if std::atomic is unavailable
#define NOVA_NO_STRING_VIEW  // if std::string_view is unavailable
#include <kmac/nova/nova.h>
```

On bare-metal targets, `NOVA_BARE_METAL` implies all of the above automatically.

## Migration Guide

### Upgrading from C++11/14 to C++17

No code changes are required.  Nova's public API is identical across all
supported standards.  Simply update the compiler standard flag:

```cmake
# CMakeLists.txt
set( CMAKE_CXX_STANDARD 17 )
```

```makefile
# Makefile
CXXFLAGS += -std=c++17
```

Additional standard library features (std::string_view, std::to_chars) will
be enabled automatically via the `NOVA_HAS_*` capability flags in `config.h`.

## Summary

| Feature                      | C++11 | C++14 | C++17+ |
|------------------------------|-------|-------|--------|
| All Nova features available  | ✅    | ✅    | ✅     |
| Zero-cost disabled tags      | ✅    | ✅    | ✅     |
| std::string_view             | ❌    | ❌    | ✅     |
| std::to_chars                | ❌    | ❌    | ✅     |
| __has_include (standard)     | ❌    | ❌    | ✅     |
| __has_include (GCC/Clang)    | ✅    | ✅    | ✅     |
| if constexpr                 | ❌    | ❌    | ✅     |
