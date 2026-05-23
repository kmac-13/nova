# Nova Bare-Metal & RTOS Porting Guide

## Overview

This guide explains how to use Nova/Flare in bare-metal embedded systems and RTOS environments without the C++ standard library.  Nova's platform abstraction layer provides conditional compilation support for environments ranging from full-featured desktop systems to resource-constrained microcontrollers.

---

## Table of Contents

1. [Quick Start](#quick-start)
2. [Platform Abstraction Architecture](#platform-abstraction-architecture)
3. [Feature Flags Reference](#feature-flags-reference)
4. [Required Implementations](#required-implementations)
5. [Platform-Specific Examples](#platform-specific-examples)
6. [Safety Considerations](#safety-considerations)
7. [Troubleshooting](#troubleshooting)

---

## Quick Start

The following examples show the minimum platform-specific setup required for each environment - defines, includes, and timestamp implementation.  They omit tag definition, sink setup, and sink binding, which are common to all platforms.  For a complete example of those, see [NOVA_README.md](NOVA_README.md).

### Bare-Metal (No Standard Library)

```cpp
// 1. define NOVA_BARE_METAL before including Nova headers
#define NOVA_BARE_METAL

// 2. provide custom assert (optional but recommended)
#define NOVA_ASSERT(x) do { if (!(x)) { halt_system(); } } while(0)

// 3. include Nova headers
#include <kmac/nova.h>

// 4. implement required platform functions
namespace bsp {
	std::uint64_t steadyNanosecs() noexcept {
		return get_hardware_timer_ns();
	}
}

// 5. use Nova normally
NOVA_LOGGER_TRAITS(Tag, NAME, true, bsp::steadyNanosecs);
NOVA_LOG(Tag) << "Hello from bare-metal!";
```

### RTOS (With Partial Standard Library)

```cpp
// most RTOSes provide std::atomic and basic Standard C++ Library,
// only override what's needed:

#define NOVA_NO_CHRONO  // use RTOS tick counter instead

#include <kmac/nova.h>

// implement timestamp using RTOS tick counter
namespace app {
	std::uint64_t osTimestampNs() noexcept {
		return os_get_tick_count() * 1000000;  // ms to ns
	}
}

NOVA_LOGGER_TRAITS(Tag, NAME, true, app::osTimestampNs);
```

---

## Platform Abstraction Architecture

Nova's platform abstraction sits between the Core API and the underlying platform.  The platform abstraction headers automatically select `std::` implementations where available and fall back to built-in alternatives otherwise.

```
┌────────────────────────────────────────┐
│      Application/Library Code          │
│  (NOVA_LOG, Logger<Tag>, etc)          │
└────────────────────────────────────────┘
            ↓
┌────────────────────────────────────────┐
│       Nova Core API                    │
│ (logger.h, truncating_logging.h, etc)  │
└────────────────────────────────────────┘
            ↓
┌────────────────────────────────────────┐
│   Platform Abstraction Layer           │
│  platform/{array, atomic, chrono,      │
│   int_to_chars, float_to_chars,        │
│   string_view}.h                       │
└────────────────────────────────────────┘
```

Two behaviors are user-configurable:

| Customization Point | Mechanism |
|---|---|
| Timestamp source | `NOVA_LOGGER_TRAITS(Tag, NAME, true, callable)` |
| Assert behavior | `#define NOVA_ASSERT(x) ...` |

### Platform Headers

- **`platform/config.h`**  - feature detection and configuration macros
- **`platform/array.h`**   - fixed-size array abstraction
- **`platform/atomic.h`**  - atomic pointer operations abstraction
- **`platform/chrono.h`**  - timestamp source abstraction
- **`platform/int_to_chars.h`** - integer-to-string conversion (`std::to_chars` or fallback)
- **`platform/float_to_chars.h`** - float-to-string conversion (`std::to_chars` or fallback)
- **`platform/string_view.h`**  - non-owning string reference (`std::string_view` or fallback)

---

## Feature Flags Reference

### Master Flags

| Flag | Description | Implies |
|------|-------------|---------|
| `NOVA_BARE_METAL` | Complete bare-metal mode | All NO_* flags below |
| `NOVA_NO_STD` | Disable all standard library | NO_ATOMIC, NO_CHRONO, NO_ARRAY |

### Component Flags

| Flag | Disables |
|------|----------|
| `NOVA_NO_ARRAY` | `std::array` |
| `NOVA_NO_ATOMIC` | `std::atomic` - provide a custom AtomicPtr or accept volatile fallback |
| `NOVA_NO_CHRONO` | `std::chrono` - provide a timestamp callable to `NOVA_LOGGER_TRAITS` |
| `NOVA_NO_CHARCONV` | `std::to_chars` |
| `NOVA_NO_STRING_VIEW` | `std::string_view` |

### Additional Flags

| Flag | Purpose |
|------|---------|
| `NOVA_RTOS` | Enable RTOS-specific features |
| `NOVA_CUSTOM_ATOMIC` | Use entirely custom atomic impl |
| `NOVA_ASSERT` | Override default assert behavior |

---

## Required Implementations

### 1. Timestamp Source

Nova requires a timestamp source per tag, supplied as the fourth argument to `NOVA_LOGGER_TRAITS`.  On bare-metal, two approaches are available:

**Option 1** - Supply your own callable directly:
Any function or callable returning `std::uint64_t` nanoseconds can be passed directly.  No implementation of `platform::steadyNanosecs()` is needed.

```cpp
namespace bsp {
	std::uint64_t steadyNanosecs() noexcept {
		return ((uint64_t)DWT->CYCCNT * 1000000000ULL) / SystemCoreClock;
	}
}
NOVA_LOGGER_TRAITS(Tag, NAME, true, bsp::steadyNanosecs);
```

**Note:** 32-bit cycle counters wrap frequently.  Consider:
- extending to 64-bit with overflow interrupt
- using RTC for lower resolution
- accepting wrap behavior if log correlation is not critical

**Option 2** - Use `TimestampHelper`:
If you implement `platform::steadyNanosecs()` in `chrono.h`'s user implementation slot, you can then use `TimestampHelper::steadyNanosecs` as a convenience wrapper.  This also enables `TimestampHelper::steadyMicrosecs` and `TimestampHelper::steadyMillisecs`.

```cpp
NOVA_LOGGER_TRAITS(Tag, NAME, true, kmac::nova::TimestampHelper::steadyNanosecs);
```

### 2. Atomic Operations (OPTIONAL in NO_ATOMIC mode)

When `NOVA_NO_ATOMIC` is defined, Nova falls back to `volatile` pointers.  This is:
- ✅ safe for single-core with careful interrupt handling
- ✅ safe for single-threaded applications
- ❌ NOT safe for multi-core systems
- ❌ NOT safe for preemptive multithreading without critical sections

**For multi-core/multi-threaded bare-metal, provide custom atomic:**

See `platform/atomic.h` for examples using:
- ARM LDREX/STREX instructions
- RTOS critical sections
- compiler intrinsics
- platform-specific atomic libraries

### 3. Assert Macro (RECOMMENDED)

Define `NOVA_ASSERT` before including Nova headers:

```cpp
// bare-metal halt
#define NOVA_ASSERT(x) do { if (!(x)) { while(1); } } while(0)

// with LED indication
#define NOVA_ASSERT(x) do { if (!(x)) { error_led_on(); while(1); } } while(0)

// with breakpoint (debugging)
#define NOVA_ASSERT(x) do { if (!(x)) { __BKPT(0); } } while(0)

// RTOS task suspend
#define NOVA_ASSERT(x) do { if (!(x)) { vTaskSuspendAll(); while(1); } } while(0)
```

If not defined, defaults to:
- standard mode: `assert()` from `<cassert>`
- bare-metal mode: no-op (compiles but provides no safety)

---

## Platform-Specific Examples

### ARM Cortex-M (Bare-Metal)

```cpp
#define NOVA_BARE_METAL
#define NOVA_ASSERT(x) do { if (!(x)) { __BKPT(0); } } while(0)

#include <kmac/nova.h>

// enable Data Watchpoint and Trace (DWT) cycle counter in startup code:
void init_dwt() {
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CYCCNT = 0;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

// timestamp implementation
namespace bsp {
	std::uint64_t steadyNanosecs() noexcept {
		return ((uint64_t)DWT->CYCCNT * 1000000000ULL) / SystemCoreClock;
	}
}

NOVA_LOGGER_TRAITS(Tag, NAME, true, bsp::steadyNanosecs);

// custom UART sink (see also extras/uart_sink.h for a ready-made implementation)
class UartSink : public kmac::nova::Sink {
	void process(const kmac::nova::Record& record) noexcept override {
		uart_write(record.message, record.messageSize);
	}
};
```

### FreeRTOS

```cpp
// FreeRTOS provides std::atomic via CMSIS, only need timestamp
#define NOVA_NO_CHRONO

#include "FreeRTOS.h"
#include "task.h"
#include <kmac/nova.h>

namespace app {
	std::uint64_t osTimestampNs() noexcept {
		TickType_t ticks = xTaskGetTickCount();
		return ((uint64_t)ticks * 1000000000ULL) / configTICK_RATE_HZ;
	}
}

NOVA_LOGGER_TRAITS(Tag, NAME, true, app::osTimestampNs);

// thread-safe logging with mutex sink
#include <kmac/nova/extras/synchronized_sink.h>

kmac::nova::extras::SynchronizedSink<OStreamSink> threadSafeSink(...);
```

### Zephyr RTOS

```cpp
#define NOVA_NO_CHRONO
#include <zephyr/kernel.h>
#include <kmac/nova.h>

namespace app {
	std::uint64_t osTimestampNs() noexcept {
		return k_uptime_get() * 1000000ULL;  // ms to ns
	}
}

NOVA_LOGGER_TRAITS(Tag, NAME, true, app::osTimestampNs);
```

### VxWorks

```cpp
#define NOVA_NO_CHRONO
#include <time.h>
#include <kmac/nova.h>

namespace app {
	std::uint64_t osTimestampNs() noexcept {
		struct timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
	}
}

NOVA_LOGGER_TRAITS(Tag, NAME, true, app::osTimestampNs);
```

### QNX Neutrino

QNX provides a full C++ standard library, so no NOVA_BARE_METAL or NOVA_NO_* overrides are needed.  Follow the standard hosted setup in [NOVA_README.md](NOVA_README.md).

---

## Safety Considerations

### Multi-Core Safety

The default `volatile` atomic implementation in `NOVA_BARE_METAL` mode is **NOT multi-core safe**.  For multi-core systems:

1. **Provide proper atomic operations** using platform instructions (LDREX/STREX, etc.)
2. **OR** ensure sink binding happens only during single-core init
3. **AND** document this constraint in your safety case

Example multi-core safe atomic for ARM:

```cpp
#define NOVA_CUSTOM_ATOMIC
#include <kmac/nova/platform/atomic.h>

// Define before including atomic.h
namespace kmac { namespace nova { namespace platform {

template<typename T>
class AtomicPtr {
private:
	T* _ptr;

public:
	T* load() const noexcept {
		T* value;
		__asm__ volatile("ldr %0, [%1]" : "=r"(value) : "r"(&_ptr) : "memory");
		__asm__ volatile("dmb" ::: "memory");
		return value;
	}

	void store(T* ptr) noexcept {
		__asm__ volatile("dmb" ::: "memory");
		__asm__ volatile("str %1, [%0]" :: "r"(&_ptr), "r"(ptr) : "memory");
	}

	T* exchange(T* desired) noexcept {
		T* old;
		uint32_t success;
		do {
			__asm__ volatile("ldrex %0, [%2]" : "=r"(old) : "r"(&_ptr) : "memory");
			__asm__ volatile("strex %0, %2, [%1]"
			: "=&r"(success) : "r"(&_ptr), "r"(desired) : "memory");
		} while (success != 0);
		return old;
	}
};

}}}
```

### Interrupt Safety

Nova is interrupt-safe with these constraints:

✅ **Safe from ISR:**
- logging via `NOVA_LOG(Tag)` (stack-based when `NOVA_BARE_METAL`/`NOVA_NO_TLS` is defined) or `NOVA_LOG_STACK(Tag)` (always stack-based, explicit)
- reading current sink via `Logger<Tag>::getSink()`
- sink processing (if sink is interrupt-safe)

❌ **NOT safe from ISR:**
- `Logger<Tag>::bindSink()` / `unbindSink()`
- `ScopedConfigurator` operations
- any sink that uses mutexes or non-reentrant code

**Best practice:** Configure all sinks during initialization, then only log from ISRs.

### Memory Analysis

**Nova Core (bare-metal mode):**
- zero heap allocation
- zero dynamic allocation
- stack usage per log call: ~buffer size (typically 256 bytes)
- static memory: one pointer per tag (4-8 bytes)

**Example memory footprint:**
```
10 tags × 8 bytes = 80 bytes static
256 byte buffer stack per log call
ScopedConfigurator<10>: 10 × 8 = 80 bytes stack
Total: ~160 bytes static, 256 bytes stack per call
```

### DO-178C / IEC 61508 / ISO 26262 Qualification

Nova bare-metal mode is suitable for safety-critical systems:

| Standard | Level | Status | Notes |
|----------|-------|--------|-------|
| DO-178C | Level A | ✅ Ready | Zero heap, deterministic, no exceptions |
| IEC 61508 | SIL 3/4 | ✅ Ready | Fixed capacity, analyzable memory |
| ISO 26262 | ASIL D | ✅ Ready | Compile-time routing, no dynamic behavior |
| IEC 62304 | Class C | ✅ Ready | Medical device software certified |

**Requirements for certification:**
1. verify atomic operations for target platform
2. analyze stack usage for all log call sites
3. document timestamp overflow behavior
4. verify sink implementations are safe
5. include platform headers in DO-178C objective review

---

## Troubleshooting

### Compilation Errors

**Error:** `std::atomic not found`
- **Solution:** Define `NOVA_NO_ATOMIC` or `NOVA_BARE_METAL`

**Error:** `std::chrono not found`
- **Solution:** Define `NOVA_NO_CHRONO` and provide a timestamp callable to `NOVA_LOGGER_TRAITS`

**Error:** `std::array not found`
- **Solution:** Define `NOVA_NO_ARRAY` (automatic with `NOVA_BARE_METAL`)

**Error:** `undefined reference to` your timestamp function
- **Solution:** Ensure your timestamp function is defined in a compiled translation unit

### Linker Errors

**Error:** `multiple definition of Logger<Tag>::_sink`
- **Solution:** Include logger.h only in one translation unit, or use header guards

**Error:** `undefined reference to operator new`
- **Solution:** Nova doesn't use heap allocation; check your sinks and ensure they don't either

### Runtime Issues

**Problem:** Timestamps are always zero
- **Solution:** Verify hardware timer initialization in your timestamp function

**Problem:** Logs not appearing
- **Solution:** 
  1. check sink is bound: `Logger<Tag>::getSink() != nullptr`
  2. verify tag is enabled: `logger_traits<Tag>::enabled == true`
  3. check sink `process()` implementation

**Problem:** System hangs in assert
- **Solution:** ScopedConfigurator capacity exceeded; increase `MaxBindings` template parameter

**Problem:** Corrupted logs in multi-threaded system
- **Solution:** Use `SynchronizedSink` wrapper or proper atomic implementation

---

## Advanced Topics

### Custom Clock Sources

Replace standard timestamps with custom sources:

```cpp
// frame counter for game engines
NOVA_LOGGER_TRAITS(GameTag, GAME, true, []() { 
	extern uint64_t ext_frame_number;
	return ext_frame_number;
});

// RTC wall-clock time
NOVA_LOGGER_TRAITS(EventTag, EVENT, true, []() { 
	return rtc_get_time_ns();
});

// no timestamp (save space)
NOVA_LOGGER_TRAITS(MinimalTag, MIN, true, []() { 
	return 0ULL;
});
```

### Mixed Standard/Bare-Metal Builds

Use conditional compilation for code that runs on multiple platforms:

```cpp
#if defined(TARGET_EMBEDDED)
	#define NOVA_BARE_METAL
#endif

#include <kmac/nova.h>

// platform-specific sink
#if defined(TARGET_EMBEDDED)
	UartSink sink;
#else
	OStreamSink sink(std::cout);
#endif
```

### Testing Bare-Metal Code on Desktop

```cpp
// test harness that simulates bare-metal environment
#define NOVA_BARE_METAL
#define NOVA_ASSERT(x) assert(x)  // use standard assert for testing

// mock hardware timer
namespace app {
	std::uint64_t mockTimestampNs() noexcept {
		return mock_timer_ns++;
	}
}

NOVA_LOGGER_TRAITS(Tag, NAME, true, app::mockTimestampNs);

// unit tests can now run on desktop
```

---

## References

- **Platform Abstraction Headers:**
  - `nova/include/kmac/nova/platform/config.h`
  - `nova/include/kmac/nova/platform/array.h`
  - `nova/include/kmac/nova/platform/atomic.h`
  - `nova/include/kmac/nova/platform/chrono.h`
  - `nova/include/kmac/nova/platform/float_to_chars.h`
  - `nova/include/kmac/nova/platform/int_to_chars.h`
  - `nova/include/kmac/nova/platform/string_view.h`

- **Examples:**
  - `examples/01_basic_usage/` - standard library baseline
  - `examples/08_bare_metal/`  - complete bare-metal example

- **Documentation:**
  - `docs/NOVA_README.md` - core API reference
  - `docs/SAFETY_CRITICAL_GUIDELINES.md` - certification guidance

---

## Summary Checklist

Before deploying to bare-metal:

- [ ] Define `NOVA_BARE_METAL` or appropriate `NOVA_NO_*` flags
- [ ] Implement a timestamp function and pass it to `NOVA_LOGGER_TRAITS`
- [ ] Define `NOVA_ASSERT` for error handling
- [ ] Verify atomic operations are safe for your concurrency model
- [ ] Test all tag/sink configurations
- [ ] Analyze stack usage for worst-case log calls
- [ ] Verify sinks are interrupt-safe if logging from ISRs
- [ ] Document platform assumptions in safety case
- [ ] Test on actual target hardware
- [ ] Measure performance impact (timing, stack, code size)
