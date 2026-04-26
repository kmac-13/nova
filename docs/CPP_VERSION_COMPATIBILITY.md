# Nova C++ Version Compatibility

## Overview

Nova supports C++11, C++14, C++17, and C++20+ with no functional differences
between standards. All Nova features are available on all supported standards.
C++17 or later is recommended for the strongest compile-time guarantees, but
C++11 and C++14 are fully supported with no API changes required.

## Version Support Matrix

| C++ Version | Status | Notes |
|-------------|--------|-------|
| **C++17+**  | ✅ Full support | Recommended. Language-level zero-cost disabled tags. |
| **C++14**   | ✅ Full support | All features available. Optimiser-dependent disabled tag elimination. |
| **C++11**   | ✅ Full support | All features available. Optimiser-dependent disabled tag elimination. |
| **C++03**   | ❌ Not supported | Missing required features (constexpr, variadic templates, nullptr). |

## Disabled Tag Elimination

Disabled tag elimination is the most important correctness property of Nova's
logging macros. When `logger_traits<Tag>::enabled` is `false`, the log
statement should produce no code — including no evaluation of the arguments
passed to `operator<<`.

### C++17 and Later

On C++17, `NOVA_IF_CONSTEXPR` expands to `if constexpr`, which provides a
language-level guarantee that the disabled branch is never instantiated and
arguments are never evaluated:

```cpp
NOVA_IF_CONSTEXPR ( ! logger_traits< Tag >::enabled )
    {}
else
    TlsTruncBuilderWrapper< Tag, Size >( file, func, line ).builder()
```

Given:
```cpp
NOVA_LOG( DisabledTag ) << getSomething();
```

On C++17, `getSomething()` is guaranteed by the language to never be called.

### C++11 and C++14

On C++11/14, `NOVA_IF_CONSTEXPR` expands to plain `if`. Since
`logger_traits<Tag>::enabled` is a `constexpr bool`, the compiler
constant-folds the condition and eliminates the disabled branch — including
argument evaluation — in any optimised build:

```cpp
if ( ! logger_traits< Tag >::enabled )
    {}
else
    TlsTruncBuilderWrapper< Tag, Size >( file, func, line ).builder()
```

This is optimiser-dependent rather than language-guaranteed. At `-O0` (debug
builds) the branch may not be eliminated and `getSomething()` may be called.
At `-O1` and above all major compilers eliminate the branch. For the vast
majority of use cases this is indistinguishable from the C++17 behaviour.

For cases where language-level argument suppression is required on C++11/14,
the `std::conditional` pattern can be used explicitly — see
`null_logging.h` for `NullBuilderWrapper` and usage examples. Note that
`std::conditional` prevents builder instantiation but does not suppress
argument evaluation.

### Dangling Else

The `if/else` structure of the macro correctly handles the dangling else
problem. Given:

```cpp
if ( test )
    NOVA_LOG( Tag ) << "message";
else
    doSomething();
```

The macro's inner `else` attaches to the macro's inner `if`, leaving the
outer `else` to correctly attach to the outer `if (test)`.

## Feature Differences by Version

### C++17 and Later (Recommended)

- `if constexpr` — language-level disabled branch elimination including
  argument evaluation
- `std::string_view` used directly via `platform::StringView`
- `std::to_chars` used for integer and floating-point formatting
- `__has_include` used for automatic stdlib feature detection (standard)
- `NOVA_INLINE_VAR` expands to `inline` for ODR-safe constexpr variables

### C++14

- Plain `if` — optimiser-dependent disabled branch elimination
- `platform::StringView` fallback (pointer + length wrapper)
- Platform fallback implementations for integer and floating-point formatting
- `__has_include` available on GCC and Clang; manual `NOVA_NO_*` flags may
  be needed on other compilers
- `NOVA_INLINE_VAR` expands to nothing (constexpr variables have internal
  linkage in C++14)

### C++11

Same as C++14, with one additional constraint:

- `constexpr` functions use recursive single-expression form rather than
  loops, since C++11 restricts `constexpr` functions to a single `return`
  statement. This affects `fnv1a` (tag hash) and `fileName` (`__FILE__` path
  stripping) in `details.h`.

## Compilation

All supported standards compile cleanly with no warnings:

```bash
# C++11
g++ -std=c++11 -I nova/include your_app.cpp -o your_app

# C++14
g++ -std=c++14 -I nova/include your_app.cpp -o your_app

# C++17 (recommended)
g++ -std=c++17 -I nova/include your_app.cpp -o your_app
```

Note: the test suite requires C++17 for `std::filesystem`. The library itself
compiles at C++11 and above.

## Auto-Detection

Nova uses `__has_include` to detect stdlib feature availability automatically.
`__has_include` is a C++17 standard feature but is also supported as a
compiler extension by GCC and Clang in C++11/14 mode. On compilers that do
not support it, define the appropriate `NOVA_NO_*` flags manually:

```cpp
#define NOVA_NO_CHRONO       // if std::chrono is unavailable
#define NOVA_NO_ATOMIC       // if std::atomic is unavailable
#define NOVA_NO_STRING_VIEW  // if std::string_view is unavailable
#include <kmac/nova/nova.h>
```

On bare-metal targets, `NOVA_BARE_METAL` implies all of the above
automatically.

## Performance

Disabled tag performance is identical across all standards in optimised builds:

| Version | Debug (`-O0`) | Release (`-O1`+) |
|---------|---------------|------------------|
| C++17+  | Zero (language guarantee) | Zero (language guarantee) |
| C++11/14 | Optimiser-dependent | Zero in practice |

Enabled tag performance is identical across all standards — atomic operations,
timestamp calls, and sink processing are the same regardless of C++ version.

## Migration Guide

### Upgrading from C++11/14 to C++17

No code changes are required. Nova's public API is identical across all
supported standards. Simply update the compiler standard flag:

```cmake
# CMakeLists.txt
set( CMAKE_CXX_STANDARD 17 )
```

```makefile
# Makefile
CXXFLAGS += -std=c++17
```

Additional standard library features (`std::string_view`, `std::to_chars`)
are enabled automatically via the `NOVA_HAS_*` capability flags in `config.h`.

## Summary

| Feature                          | C++11 | C++14 | C++17+ |
|----------------------------------|-------|-------|--------|
| All Nova features available      | ✅    | ✅    | ✅     |
| Clean compilation (no warnings)  | ✅    | ✅    | ✅     |
| Zero-cost disabled tags (release)| ✅    | ✅    | ✅     |
| Zero-cost disabled tags (debug)  | ❌    | ❌    | ✅     |
| Language-level argument suppression | ❌ | ❌    | ✅     |
| std::string_view                 | ❌    | ❌    | ✅     |
| std::to_chars                    | ❌    | ❌    | ✅     |
| __has_include (standard)         | ❌    | ❌    | ✅     |
| __has_include (GCC/Clang)        | ✅    | ✅    | ✅     |
