#pragma once
#ifndef KMAC_NOVA_LOGGER_TRAITS_H
#define KMAC_NOVA_LOGGER_TRAITS_H

#include "timestamp_helper.h"

#include <cstdint>

namespace kmac::nova
{

#define TAG_NAME( tag ) #tag

/**
 * @brief Customization point for per-tag logging behavior.
 * 
 * logger_traits<Tag> controls compile-time and runtime behavior for each tag type.
 * 
 * Customizable properties:
 * - tagName: string identifier for the tag (default: stringified tag type)
 * - enabled: compile-time on/off switch (default: true)
 * - tagId: unique numeric identifier for each tag
 * - timestamp(): timestamp source (default: steady_clock nanoseconds)
 * 
 * The default implementation uses std::chrono::steady_clock for monotonic timestamps.
 * Users can specialize this template manually for tags that need alternate properties
 * or by using various convenience macros, e.g. NOVA_LOGGER_TRAITS.
 * 
 * @tparam Tag User-defined tag type representing logging context
 */
template< typename Tag >
struct logger_traits
{
	/**
	 * @brief String identifier for this tag.
	 * 
	 * Used in log records to identify the source/category.
	 * Typically the stringified tag type name.
	 */
	static constexpr const char* tagName = TAG_NAME( Tag );
	
	/**
	 * @brief Character used to generate address for unique ID.
	 */
	static constexpr char tagIdStorage = '\0';

	/**
	 * @brief Gets the unique ID of the tag.
	 *
	 * @return numeric value representing the unique ID of the tag
	 */
	static /*constexpr*/ std::uintptr_t tagId() noexcept
	{
		return reinterpret_cast< std::uintptr_t >( &tagIdStorage );
	}

	/**
	 * @brief Compile-time enablement flag.
	 *
	 * When false, all logging code for this tag is eliminated at compile time.
	 * When true, logging behavior is controlled by runtime sink binding.
	 */
	static constexpr bool enabled = true;

	/**
	 * @brief Get current timestamp in nanoseconds.
	 * 
	 * Default implementation uses a steady clock for monotonic time.
	 * This clock is not affected by system time adjustments and provides
	 * consistent ordering of events.
	 * 
	 * @return timestamp in nanoseconds since an unspecified epoch
	 */
	static std::uint64_t timestamp() noexcept
	{
		return TimestampHelper::steadyNanosecs();
	}
};

/**
 * @brief Specialization for void tag (used internally).
 */
template<>
struct logger_traits< void >
{
	static constexpr const char* tagName = "<void>";
	static constexpr bool enabled = true;
	
	static std::uint64_t timestamp() noexcept
	{
		return 0;
	}
};

} // namespace kmac::nova

//
// Customization macros
//

/**
 * @brief Full logger_traits specialization macro.
 *
 * Defines all three properties: tagName, enabled, and timestamp.
 *
 * @param TagType The tag type to specialize for
 * @param Name String literal for the tag name
 * @param Enabled Boolean (true/false)
 * @param TimestampExpr Expression that returns the timestamp
 *
 * Usage:
 *   NOVA_LOGGER_TRAITS(MyTag, MY, true, TimestampHelper::systemNanosecs)
 *   NOVA_LOGGER_TRAITS(OtherTag, OTHER, true, myCustomClock)
 *   NOVA_LOGGER_TRAITS(DisabledTag, DISABLED, false, TimestampHelper::steadyNanosecs)
 */
#define NOVA_LOGGER_TRAITS( TagType, Name, Enabled, TimestampExpr ) \
	template<> \
	struct kmac::nova::logger_traits< TagType > \
	{ \
		static constexpr const char* tagName = #Name; \
		static constexpr char tagIdStorage = '\0'; \
		static /*constexpr*/ std::uintptr_t tagId() noexcept { \
			return reinterpret_cast< std::uintptr_t >( &tagIdStorage ); \
		} \
		static constexpr bool enabled = Enabled; \
			\
		static std::uint64_t timestamp() noexcept { \
			return TimestampExpr(); \
		} \
	}

/**
 * @brief Simplified logger_traits macro with defaults.
 *
 * Uses steady_clock and enabled=true by default.
 *
 * @param TagType The tag type to specialize for
 * @param Name String literal for the tag name
 *
 * Usage:
 *   NOVA_LOGGER_TRAITS_SIMPLE(MyTag, My)
 */
#define NOVA_LOGGER_TRAITS_SIMPLE( TagType, Name ) \
	NOVA_LOGGER_TRAITS( TagType, Name, true, ::kmac::nova::TimestampHelper::systemNanosecs )

/**
 * @brief Disabled logger_traits macro.
 *
 * Creates a disabled logger (enabled=false).
 *
 * @param TagType The tag type to specialize for
 * @param Name String literal for the tag name
 *
 * Usage:
 *   NOVA_LOGGER_TRAITS_DISABLED(DebugTag, "Debug")
 */
#define NOVA_LOGGER_TRAITS_DISABLED( TagType, Name ) \
	NOVA_LOGGER_TRAITS( TagType, Name, false, ::kmac::nova::TimestampHelper::steadyNanosecs )


#endif // KMAC_NOVA_LOGGER_TRAITS_H
