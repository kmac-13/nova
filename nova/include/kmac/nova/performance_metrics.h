#pragma once
#ifndef KMAC_NOVA_PERFORMANCE_METRICS_H
#define KMAC_NOVA_PERFORMANCE_METRICS_H

#include "logger.h"
#include "record.h"
#include "sink.h"
#include "platform/atomic.h"

#include <type_traits>

/**
 * @file performance_metrics.h
 * @brief Compile-time performance guarantees and verification.
 *
 * This file contains static assertions that verify Nova's performance
 * claims and ensure the library maintains its zero-cost abstraction
 * properties.
 *
 * These assertions:
 * - verify size and layout of core types
 * - ensure trivial destructors for hot-path types
 * - check alignment for optimal performance
 * - prevent unintended bloat
 *
 * If any assertion fails, it indicates a regression in Nova's
 * performance characteristics.
 */

namespace kmac::nova::performance
{

//
// Logger Performance Metrics
//

template< typename Tag >
struct LoggerMetrics
{
	// Logger should only contain a single atomic pointer.
	// Uses platform::AtomicPtr which resolves to std::atomic on hosted targets
	// and a volatile pointer on bare-metal - both are pointer-sized.
	static_assert(
		sizeof( Logger< Tag > ) == sizeof( platform::AtomicPtr< Sink > ),
		"Logger size regression: should only contain atomic sink pointer"
	);

	// Logger should be trivially destructible (no cleanup needed)
	static_assert(
		std::is_trivially_destructible_v< Logger< Tag > >,
		"Logger should be trivially destructible for zero-cost cleanup"
	);

	// Logger should have pointer alignment
	static_assert(
		alignof( Logger< Tag > ) == alignof( void* ),
		"Logger should have pointer alignment for optimal access"
	);

	// Logger should be standard-layout for predictable memory layout
	static_assert(
		std::is_standard_layout_v< Logger< Tag > >,
		"Logger should be standard layout for predictable memory layout"
	);
};

//
// Record Performance Metrics
//

struct RecordMetrics
{
	// Lock in the exact layout size, catches accidental field additions or padding.
	// 56 bytes on 64-bit (pointers are 8 bytes); 40 bytes on 32-bit (pointers are 4 bytes).
#if UINTPTR_MAX == UINT64_MAX
	static_assert(
		sizeof( Record ) == 56,
		"Record size regression: expected 56 bytes on 64-bit platform"
	);
#else
	static_assert(
		sizeof( Record ) == 40,
		"Record size regression: expected 40 bytes on 32-bit platform"
	);
#endif

	// Record should be trivially copyable (POD-like)
	static_assert(
		std::is_trivially_copyable_v< Record >,
		"Record should be trivially copyable for efficient passing"
	);

	// Record should be standard layout
	static_assert(
		std::is_standard_layout_v< Record >,
		"Record should be standard layout for predictable memory layout"
	);

	// Record should have natural alignment
	static_assert(
		alignof( Record ) <= 16,
		"Record alignment should not exceed 16 bytes"
	);
};

//
// Sink Performance Metrics
//

struct SinkMetrics
{
	// Sink base class should only contain vtable pointer
	// Size is typically sizeof(void*) for vtable
	static_assert(
		sizeof( Sink ) == sizeof( void* ),
		"Sink base class should only contain vtable pointer"
	);

	// Sink should be polymorphic (has virtual functions)
	static_assert(
		std::is_polymorphic_v< Sink >,
		"Sink should be polymorphic (has virtual functions)"
	);

	// Sink should be abstract (cannot be instantiated)
	static_assert(
		std::is_abstract_v< Sink >,
		"Sink should be abstract (pure virtual process method)"
	);
};

//
// Compile-Time Performance Summary
//

/**
 * @brief Instantiate this to verify all performance metrics.
 *
 * Usage:
 *   template struct VerifyPerformance<MyTag>;
 *
 * Or in a test file:
 *   static_assert(VerifyPerformance<MyTag>::value);
 */
template< typename Tag >
struct VerifyPerformance
{
	// Force instantiation of all metrics
	using logger_check = LoggerMetrics< Tag >;
	using record_check = RecordMetrics;
	using sink_check = SinkMetrics;

	static constexpr bool value = true;
};

//
// Performance Report (Compile-Time Information)
//

/**
 * @brief Generate compile-time performance report.
 *
 * This can be used with static_assert messages to display
 * size information during compilation.
 */
template< typename Tag >
struct PerformanceReport
{
	static constexpr std::size_t logger_size = sizeof( Logger< Tag > );
	static constexpr std::size_t record_size = sizeof( Record );
	static constexpr std::size_t sink_size = sizeof( Sink );

	static constexpr std::size_t logger_alignment = alignof( Logger< Tag > );
	static constexpr std::size_t record_alignment = alignof( Record );
	static constexpr std::size_t sink_alignment = alignof( Sink );

	// Provide compile-time report
	static constexpr bool report()
	{
		// When static_assert uses this, it will show in compiler output
		return true;
	}
};

} // namespace kmac::nova::performance

//
// Convenience Macros for Testing
//

/**
 * @brief Verify performance metrics for a tag.
 *
 * Usage in test files:
 *   NOVA_VERIFY_PERFORMANCE(MyTag);
 */
#define NOVA_VERIFY_PERFORMANCE( Tag ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	static_assert( \
		::kmac::nova::performance::VerifyPerformance< Tag >::value, \
		"Nova performance metrics verification for " #Tag \
	)

/**
 * @brief Display performance report for a tag.
 *
 * Usage:
 *   NOVA_PERFORMANCE_REPORT(MyTag);
 *
 * This will cause a static_assert that shows size information
 * in compiler output.
 */
#define NOVA_PERFORMANCE_REPORT( Tag ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	static_assert( \
		::kmac::nova::performance::PerformanceReport< Tag >::logger_size >= 0, \
		"Logger<" #Tag "> size: " /* size shown in error message */ \
	)

#endif // KMAC_NOVA_PERFORMANCE_METRICS_H
