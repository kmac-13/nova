#pragma once
#ifndef KMAC_NOVA_PLATFORM_CHRONO_H
#define KMAC_NOVA_PLATFORM_CHRONO_H

#include "config.h"

// cstdint is required even in bare-metal mode for uint64_t
#include <cstdint>

// ============================================================================
// IMPLEMENTATION 1: STANDARD LIBRARY (Default)
// ============================================================================

#if NOVA_HAS_STD_CHRONO
#include <chrono>
#endif

/**
 * @file chrono.h
 * @brief Timestamp abstraction for bare-metal and RTOS environments
 *
 * Provides timestamp sources with multiple backend implementations:
 * 1. std::chrono (default, portable)
 * 2. hardware timers (bare-metal)
 * 3. RTOS tick counters
 * 4. custom user-provided timestamp sources
 *
 * For safety-critical systems:
 * - use monotonic clocks only (never wall clock for intervals)
 * - document timestamp resolution in safety case
 * - verify timestamp source meets timing requirements
 * - consider overflow behavior for long-running systems
 */

namespace kmac::nova::platform {

#if NOVA_HAS_STD_CHRONO

/**
 * @brief Get steady (monotonic) clock timestamp in nanoseconds
 *
 * Uses std::chrono::steady_clock which is guaranteed to be monotonic.
 * Will not jump backwards due to NTP adjustments or daylight saving time.
 *
 * @return nanoseconds since arbitrary epoch (not wall clock time)
 */
inline std::uint64_t steadyNanosecs() noexcept
{
	auto now = std::chrono::steady_clock::now();
	auto duration = now.time_since_epoch();
	return static_cast< std::uint64_t >(
		std::chrono::duration_cast< std::chrono::nanoseconds >( duration ).count()
	);
}

/**
 * @brief Get system (wall) clock timestamp in nanoseconds
 *
 * WARNING: Not suitable for measuring intervals!
 * Can jump backwards due to NTP, timezone changes, etc.
 * Only use for human-readable timestamps.
 *
 * @return nanoseconds since Unix epoch (1970-01-01)
 */
inline std::uint64_t systemNanosecs() noexcept
{
	auto now = std::chrono::system_clock::now();
	auto duration = now.time_since_epoch();
	return static_cast< std::uint64_t >(
		std::chrono::duration_cast< std::chrono::nanoseconds >( duration ).count()
	);
}

/**
 * @brief Get high-resolution timestamp in nanoseconds
 *
 * Uses highest available clock precision. May be steady or system clock
 * depending on platform. Prefer steadyNanosecs() for interval measurement.
 *
 * @return nanoseconds since arbitrary epoch
 */
inline std::uint64_t highResNanosecs() noexcept
{
	auto now = std::chrono::high_resolution_clock::now();
	auto duration = now.time_since_epoch();
	return static_cast< std::uint64_t >(
		std::chrono::duration_cast< std::chrono::nanoseconds >( duration ).count()
	);
}

// ============================================================================
// IMPLEMENTATION 2: BARE-METAL / NO-STD
// ============================================================================

#else // !NOVA_HAS_STD_CHRONO

/**
 * USER MUST IMPLEMENT: Timestamp source for bare-metal systems
 *
 * You must provide implementations of these functions before using Nova.
 * The functions should be noexcept and thread/interrupt-safe if applicable.
 *
 * Example implementations below for common platforms.
 */

/**
 * @brief Get steady (monotonic) timestamp in nanoseconds
 *
 * MUST BE IMPLEMENTED by user for bare-metal systems.
 * Should use a monotonic timer that doesn't wrap frequently.
 *
 * Implementation requirements:
 * - monotonic (never goes backwards)
 * - thread-safe (if multi-threaded)
 * - interrupt-safe (if logging from ISRs)
 * - known overflow behavior
 *
 * Example (ARM Cortex-M with SysTick):
 *   extern volatile uint64_t g_systick_count_ns;
 *   return g_systick_count_ns;
 *
 * Example (with DWT cycle counter):
 *   uint32_t cycles = DWT->CYCCNT;
 *   return (uint64_t)cycles * ( 1000000000ULL / SystemCoreClock );
 *
 * Example (with hardware timer):
 *   return HAL_GetTick() * 1000000ULL;  // milliseconds to nanoseconds
 */
NOVA_USER_IMPL std::uint64_t steadyNanosecs() noexcept;

/**
 * @brief Get system (wall) timestamp in nanoseconds
 *
 * OPTIONAL: Can be same as steadyNanosecs() for bare-metal.
 * Only needed if you want actual wall-clock time in logs.
 *
 * Example (RTC):
 *   time_t rtc_time = RTC_GetTime();
 *   return (uint64_t)rtc_time * 1000000000ULL;
 */
inline std::uint64_t systemNanosecs() noexcept
{
	return steadyNanosecs();  // default: same as steady
}

/**
 * @brief Get high-resolution timestamp in nanoseconds
 *
 * OPTIONAL: Can be same as steadyNanosecs() for bare-metal.
 * Use highest resolution timer available on your platform.
 */
inline std::uint64_t highResNanosecs() noexcept
{
	return steadyNanosecs();  // default: same as steady
}

#endif // NOVA_HAS_STD_CHRONO

// ============================================================================
// PLATFORM-SPECIFIC IMPLEMENTATIONS (Examples)
// ============================================================================

#ifdef NOVA_PLATFORM_ARM_BAREMETAL

/**
 * Example: ARM Cortex-M with DWT Cycle Counter
 *
 * Enable DWT cycle counter in startup code:
 *   CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
 *   DWT->CYCCNT = 0;
 *   DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
 *
 * Note: 32-bit counter wraps frequently! Consider extending to 64-bit
 * with overflow interrupt or using SysTick instead.
 */
#if 0  // example only, not compiled by default
inline std::uint64_t steadyNanosecs() noexcept
{
	extern uint32_t SystemCoreClock;  // from CMSIS
	uint32_t cycles = DWT->CYCCNT;
	// convert to nanoseconds (avoid overflow with careful ordering)
	return ( (uint64_t)cycles * 1000000000ULL ) / SystemCoreClock;
}
#endif

#endif // NOVA_PLATFORM_ARM_BAREMETAL

#ifdef NOVA_PLATFORM_FREERTOS

/**
 * Example: FreeRTOS tick counter
 *
 * Note: Limited resolution (typically 1ms). Consider using hardware
 * timer for higher precision if needed.
 */
#if 0  // example only, not compiled by default
#include "FreeRTOS.h"
#include "task.h"

inline std::uint64_t steadyNanosecs() noexcept
{
	TickType_t ticks = xTaskGetTickCount();
	// convert ticks to nanoseconds (configTICK_RATE_HZ from FreeRTOSConfig.h)
	return ( (uint64_t)ticks * 1000000000ULL ) / configTICK_RATE_HZ;
}
#endif

#endif // NOVA_PLATFORM_FREERTOS

#ifdef NOVA_PLATFORM_ZEPHYR

/**
 * Example: Zephyr RTOS uptime
 */
#if 0  // example only, not compiled by default
#include <zephyr/kernel.h>

inline std::uint64_t steadyNanosecs() noexcept
{
	return k_uptime_get() * 1000000ULL;  // milliseconds to nanoseconds
}
#endif

#endif // NOVA_PLATFORM_ZEPHYR

#ifdef NOVA_PLATFORM_VXWORKS

/**
 * Example: VxWorks clock_gettime
 */
#if 0  // example only, not compiled by default
#include <time.h>

inline std::uint64_t steadyNanosecs() noexcept
{
	struct timespec ts;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}
#endif

#endif // NOVA_PLATFORM_VXWORKS

// ============================================================================
// ALTERNATIVE TIMESTAMP SOURCES
// ============================================================================

/**
 * @brief Frame counter timestamp (for game engines / real-time systems)
 *
 * Instead of time, use frame number as timestamp. Useful for deterministic
 * replay and debugging in games or simulation systems.
 *
 * Opt-in only: define NOVA_ENABLE_FRAME_COUNTER before including Nova headers to
 * enable this function.  Not compiled by default because the extern declaration
 * of g_frameCounter causes link errors on bare-metal toolchains that resolve
 * all extern symbols regardless of whether the function is called.
 *
 * Usage:
 *   #define NOVA_ENABLE_FRAME_COUNTER
 *   #include <kmac/nova/logger.h>
 *
 *   // g_frameCounter must be in the kmac::nova::platform namespace,
 *   // because the extern declaration inside frameCounter() is namespace-scoped
 *   namespace kmac::nova::platform { std::uint64_t g_frameCounter = 0; }
 *   NOVA_LOGGER_TRAITS( GameTag, GAME, true, ::kmac::nova::platform::frameCounter );
 */
#ifdef NOVA_ENABLE_FRAME_COUNTER
inline std::uint64_t frameCounter() noexcept
{
	// user must provide global frame counter
	extern std::uint64_t g_frameCounter;
	return g_frameCounter;
}
#endif // NOVA_ENABLE_FRAME_COUNTER

/**
 * @brief Zero timestamp (disable timestamps completely)
 *
 * For systems where timing is not relevant or to save log space.
 *
 * Usage:
 *   NOVA_LOGGER_TRAITS( Tag, NAME, true, []() { return 0ULL; } );
 */
inline std::uint64_t noTimestamp() noexcept
{
	return 0;
}

} // namespace kmac::nova::platform

#endif // KMAC_NOVA_PLATFORM_CHRONO_H
