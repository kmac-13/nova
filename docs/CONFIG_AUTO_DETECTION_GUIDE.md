# Nova Auto-Detection Feature Guide

## Overview

Nova can **automatically detect** which standard library features are available on your platform using the C++17 `__has_include` preprocessor feature.  This means you often don't need to manually define `NOVA_NO_*` flags - Nova will figure it out for you!

## How It Works

### Automatic Detection (C++17+)

If your compiler supports `__has_include` (most modern compilers do), Nova will automatically detect:

```cpp
// you write:
#include <kmac/nova.h>

// Nova automatically detects:
// ✓ is <atomic> available?
// ✓ is <chrono> available?
// ✓ is <array> available?
// ✓ is <string_view> available?
// ✓ is std::to_chars (integers) available?
// ✓ is std::to_chars (floats) available?
// ✓ what platform am I on?
```

### Detection Logic

1. **Check for explicit flags first** - if you define `NOVA_BARE_METAL` or `NOVA_NO_*`, those take precedence
2. **Use __has_include** - if available, check for each standard library header
3. **Platform detection** - identify ARM, FreeRTOS, Zephyr, VxWorks, QNX, ThreadX, embOS, POSIX, Windows
4. **Set NOVA_HAS_*** - enable features based on detection results

## Diagnostic Mode

### Enable Diagnostics

See exactly what Nova detected at compile time:

```cpp
#define NOVA_ENABLE_DIAGNOSTICS
#include <kmac/nova.h>
```

### Example Output

**Standard Linux System:**
```
Nova Platform Configuration:
  std::atomic: available
  std::chrono: available
  std::array: available
  Platform: POSIX
  __has_include: supported
```

**Bare-Metal ARM:**
```
Nova Platform Configuration:
  NOVA_BARE_METAL: enabled
  NOVA_NO_STD: enabled
  std::atomic: NOT available (using volatile)
  std::chrono: NOT available (supply timestamp callable to NOVA_LOGGER_TRAITS)
  std::array: NOT available (using C array wrapper)
  Platform: ARM bare-metal
  __has_include: supported
```

**FreeRTOS with CMSIS:**
```
Nova Platform Configuration:
  std::atomic: available
  std::chrono: NOT available (supply timestamp callable to NOVA_LOGGER_TRAITS)
  std::array: available
  Platform: FreeRTOS
  __has_include: supported
```

## Configuration Methods

### Method 1: Full Automatic (Recommended)

Let Nova detect everything:

```cpp
#include <kmac/nova.h>
// that's it - Nova handles the rest
```

**Best for:**
- standard desktop/server environments
- platforms with full or partial stdlib
- initial prototyping

### Method 2: Explicit Bare-Metal

Force bare-metal mode:

```cpp
#define NOVA_BARE_METAL
#include <kmac/nova.h>

// implement timestamp in any namespace and pass to NOVA_LOGGER_TRAITS:
namespace bsp {
	std::uint64_t steadyNanosecs() noexcept {
		return /* your hardware timer */;
	}
}

NOVA_LOGGER_TRAITS(Tag, NAME, true, bsp::steadyNanosecs);
```

**Best for:**
- embedded systems without stdlib
- situations where you want full control
- safety-critical systems with controlled configuration

### Method 3: Fine-Grained Control

Override specific features:

```cpp
// example: RTOS with atomic but no chrono
#define NOVA_NO_CHRONO
#include <kmac/nova.h>

// implement timestamp in any namespace and pass to NOVA_LOGGER_TRAITS:
namespace app {
	std::uint64_t osTimestampNs() noexcept {
		return xTaskGetTickCount() * 1000000ULL;
	}
}

NOVA_LOGGER_TRAITS(Tag, NAME, true, app::osTimestampNs);
```

**Best for:**
- RTOS environments
- platforms with partial stdlib
- optimizing specific features

### Method 4: Debug Your Configuration

Check what Nova thinks:

```cpp
#define NOVA_ENABLE_DIAGNOSTICS
#include <kmac/nova.h>

int main() {
	#if NOVA_HAS_STD_ATOMIC
	std::cout << "Atomic available\n";
	#endif

	#if NOVA_HAS_STD_CHRONO
	std::cout << "Chrono available\n";
	#endif

	return 0;
}
```

**Best for:**
- troubleshooting
- CI/CD verification
- cross-compilation debugging

## Platform-Specific Behavior

### Desktop (Linux/Windows/macOS)

**Auto-detected:**
- ✅ std::atomic
- ✅ std::chrono
- ✅ std::array
- ✅ threading support

**Required defines:** None

### FreeRTOS (with CMSIS)

**Auto-detected:**
- ✅ std::atomic (via CMSIS)
- ❌ std::chrono
- ✅ std::array

**Required defines:**
```cpp
// usually auto-detected, but can explicitly set:
#define NOVA_NO_CHRONO

namespace app {
	std::uint64_t osTimestampNs() noexcept {
		return xTaskGetTickCount() * 1000000000ULL / configTICK_RATE_HZ;
	}
}

NOVA_LOGGER_TRAITS(Tag, NAME, true, app::osTimestampNs);
```

### Zephyr RTOS

**Auto-detected:**
- ✅ std::atomic
- ❌ std::chrono
- ✅ std::array
- ✅ platform: Zephyr

**Required defines:**
```cpp
// usually auto-detected, but can explicitly set:
#define NOVA_NO_CHRONO

namespace app {
	std::uint64_t osTimestampNs() noexcept {
		return k_uptime_get() * 1000000ULL;
	}
}

NOVA_LOGGER_TRAITS(Tag, NAME, true, app::osTimestampNs);
```

### ARM Cortex-M Bare-Metal

**Auto-detected:**
- ❌ std::atomic
- ❌ std::chrono
- ❌ std::array
- ✅ platform: ARM bare-metal

**Required defines:**
```cpp
#define NOVA_BARE_METAL  // or let __has_include detect missing headers

namespace bsp {
	std::uint64_t steadyNanosecs() noexcept {
		return DWT->CYCCNT * (1000000000ULL / SystemCoreClock);
	}
}

NOVA_LOGGER_TRAITS(Tag, NAME, true, bsp::steadyNanosecs);
```

## Compiler Support

### Compilers with __has_include (Auto-detection works)

- ✅ GCC 5.0+
- ✅ Clang 3.8+
- ✅ MSVC 2019+ (19.20+)
- ✅ ARM Compiler 6+

### Older Compilers (Manual configuration required)

If `__has_include` is not available, you must manually define flags:

```cpp
// for older compilers without __has_include:
#define NOVA_BARE_METAL  // if no stdlib
// OR
#define NOVA_NO_CHRONO   // if specific features missing

#include <kmac/nova.h>
```

## Programmatic Feature Checking

You can check what Nova detected in your code:

```cpp
#include <kmac/nova.h>

// at compile time:
#if NOVA_HAS_STD_ATOMIC
	// std::atomic is available
	use_atomic_impl();
#else
	// volatile fallback
	use_volatile_impl();
#endif

// runtime is not needed - everything is compile-time!
```

## Available Macros

### Detection Result Macros

| Macro | Value | Meaning |
|-------|-------|---------|
| `NOVA_HAS_STD_ARRAY` | 0 or 1 | std::array available |
| `NOVA_HAS_STD_ATOMIC` | 0 or 1 | std::atomic available |
| `NOVA_HAS_STD_CHRONO` | 0 or 1 | std::chrono available |
| `NOVA_HAS_INT_CHARCONV` | 0 or 1 | std::to_chars (integers) available |
| `NOVA_HAS_FLOAT_CHARCONV` | 0 or 1 | std::to_chars (floats) available |
| `NOVA_HAS_STD_STRING_VIEW` | 0 or 1 | std::string_view available |
| `NOVA_HAS_THREADING` | 0 or 1 | threading support detected |

### Platform Detection Macros

| Macro | Defined If |
|-------|-----------|
| `NOVA_PLATFORM_ARM_BAREMETAL` | ARM without OS |
| `NOVA_PLATFORM_FREERTOS` | FreeRTOS detected |
| `NOVA_PLATFORM_ZEPHYR` | Zephyr RTOS detected |
| `NOVA_PLATFORM_VXWORKS` | VxWorks detected |
| `NOVA_PLATFORM_QNX` | QNX detected |
| `NOVA_PLATFORM_THREADX` | ThreadX detected |
| `NOVA_PLATFORM_EMBOS` | embOS detected |
| `NOVA_PLATFORM_POSIX` | POSIX system |
| `NOVA_PLATFORM_WINDOWS` | Windows |

## Best Practices

### 1. Start with Automatic Detection

```cpp
// try this first:
#include <kmac/nova.h>
```

If it works, you're done!

### 2. Enable Diagnostics When Porting

```cpp
#define NOVA_ENABLE_DIAGNOSTICS
#include <kmac/nova.h>
```

Check the compiler output to verify detection.

### 3. Be Explicit in Safety-Critical Code

```cpp
// for DO-178C/IEC 61508 projects:
#define NOVA_BARE_METAL  // explicit is safer than automatic
#include <kmac/nova.h>
```

Document the choice in your safety case.

### 4. Use CI to Verify Configuration

```bash
# in your CI script:
g++ -DNOVA_ENABLE_DIAGNOSTICS -std=c++17 -c test.cpp 2>&1 | grep "Nova Platform"

# parse output to verify expected configuration
```

## Troubleshooting

### Problem: Auto-detection picked wrong configuration

**Solution 1:** Override with explicit defines
```cpp
#define NOVA_NO_CHRONO  // force specific configuration
#include <kmac/nova.h>
```

**Solution 2:** Check compiler version
```bash
g++ --version  # ensure __has_include is supported
```

### Problem: Can't see diagnostic messages

**Solution:** Make sure NOVA_ENABLE_DIAGNOSTICS is defined BEFORE including Nova:
```cpp
#define NOVA_ENABLE_DIAGNOSTICS  // MUST be before includes
#include <kmac/nova.h>
```

### Problem: Different behavior on different build machines

**Solution:** Enable diagnostics and compare output:
```bash
# machine 1:
g++ -DNOVA_ENABLE_DIAGNOSTICS -c test.cpp 2>&1 | tee config1.txt

# machine 2:
g++ -DNOVA_ENABLE_DIAGNOSTICS -c test.cpp 2>&1 | tee config2.txt

# compare:
diff config1.txt config2.txt
```

## Examples

See complete working examples in:
- `examples/01_basic_usage/` - standard automatic detection
- `examples/08_bare_metal/`  - explicit bare-metal configuration
- `examples/09_diagnostics/` - diagnostic mode demonstration

## Summary

Nova's auto-detection makes it easier to use across different platforms:

✅ **Zero configuration** for standard environments
✅ **Minimal configuration** for RTOS
✅ **Explicit control** when needed
✅ **Diagnostic mode** for verification
✅ **Backward compatible** - manual configuration still works

The auto-detection feature follows the principle of "convention over configuration" while still allowing explicit control when you need it.
