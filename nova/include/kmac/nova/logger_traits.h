#pragma once
#ifndef KMAC_NOVA_LOGGER_TRAITS_H
#define KMAC_NOVA_LOGGER_TRAITS_H

#include "details.h"
#include "timestamp_helper.h"

#include <cstdint>

namespace kmac::nova
{

/**
 * @brief Customization point for per-tag logging behavior.
 *
 * logger_traits<Tag> controls compile-time and runtime behavior for each tag type.
 *
 * Customizable properties:
 * - tagName: string identifier for the tag (default: stringified tag type)
 * - tagId: unique numeric identifier for each tag
 * - enabled: compile-time on/off switch
 * - timestamp(): timestamp source (default: steady_clock nanoseconds)
 *
 * The default implementation uses std::chrono::steady_clock for monotonic timestamps.
 * Users can specialize this template manually for tags that need alternate properties
 * or by using various convenience macros, e.g. NOVA_LOGGER_TRAITS.
 *
 * @tparam Tag User-defined tag type representing logging context
 */
template< typename Tag >
struct logger_traits;

} // namespace kmac::nova

//
// void SPECIALIZATION
//

/**
 * @brief Specialization for void tag (used internally).
 */
template<>
struct kmac::nova::logger_traits< void >
{
	/**
	 * @brief String identifier for this tag.
	 *
	 * Used in log records to identify the source/domain.
	 * Typically the stringified tag type name.
	 */
	static constexpr const char* tagName = "<void>";

	/**
	 * @brief Unique ID of the ta, which is hash of tag typeg.
	 *
	 * @return numeric value representing the unique ID of the tag
	 */
	static constexpr std::uint64_t tagId = kmac::nova::details::fnv1a( "void" );

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
	 * void specialized implementation always returns zero.
	 *
	 * @return zero
	 */
	static std::uint64_t timestamp() noexcept
	{
		return 0;
	}
};
// specialize
template<>
constexpr const char* kmac::nova::details::tagIdOwner< kmac::nova::logger_traits< void >::tagId > = "<void>";

//
// Customization macros
//

// NOTE: These macro cannot be replaced with a constexpr template function -
// one requires template specialization, inline variable specialization, and
// a static_assert at the call site, none of which a function can provide.
// NOLINT is used to suppress the cppcoreguidelines-macro-usage diagnostic accordingly.

/**
 * @brief Full logger_traits specialization macro.
 *
 * Defines all properties: type, name, enabled, and timestamp.
 *
 * @param TagType tag type to specialize for; must use the fully-qualified
 *    name (e.g. kmac::sensors::DebugTag) to maximise hash entropy and produce
 *    unambiguous collision error messages
 * @param Name tag name
 * @param Enabled boolean (true/false)
 * @param TimestampExpr expression that returns the timestamp
 *
 * ### Hash collision detection
 *
 * Each invocation registers the tag with the tagIdOwner guard (see details.h).
 * If two tags in the same binary hash to the same tagId, the compiler produces
 * a redefinition error.  For cross-binary collision detection across independently
 * compiled shared libraries, provide a validation translation unit - see the
 * documentation on details::tagIdOwner for the recommended pattern.
 *
 * Usage:
 *   NOVA_LOGGER_TRAITS(MyTag, MYtag, true, TimestampHelper::systemNanosecs)
 *   NOVA_LOGGER_TRAITS(OtherTag, OTHER, true, myCustomClock)
 *   NOVA_LOGGER_TRAITS(DisabledTag, DIS, false, TimestampHelper::steadyNanosecs)
 */
#define NOVA_LOGGER_TRAITS( TagType, Name, Enabled, TimestampExpr ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	template<> \
	struct kmac::nova::logger_traits< TagType > \
	{ \
		static constexpr const char* tagName = #Name; \
		static constexpr std::uint64_t tagId = kmac::nova::details::fnv1a( #TagType ); \
		static constexpr bool enabled = Enabled; \
		static std::uint64_t timestamp() noexcept { \
			return TimestampExpr(); \
		} \
	}; \
	template<> \
	inline constexpr const char* kmac::nova::details::tagIdOwner< kmac::nova::logger_traits< TagType >::tagId > = #TagType; \
	static_assert( kmac::nova::details::tagIdOwner< kmac::nova::logger_traits< TagType >::tagId > == nullptr \
		|| std::string_view( kmac::nova::details::tagIdOwner< kmac::nova::logger_traits< TagType >::tagId > ) == #TagType, \
		"Nova tagId collision detected" )

/**
 * @brief Simplified logger_traits macro with defaults.
 *
 * Uses steady_clock and enabled=true by default.
 *
 * @param TagType the tag type to specialize for
 * @param Name string literal for the tag name
 *
 * Usage:
 *   NOVA_LOGGER_TRAITS_SIMPLE(MyTag, My)
 */
#define NOVA_LOGGER_TRAITS_SIMPLE( TagType, Name ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	NOVA_LOGGER_TRAITS( TagType, Name, true, ::kmac::nova::TimestampHelper::systemNanosecs )

/**
 * @brief Disabled logger_traits macro.
 *
 * Creates a disabled logger (enabled=false).
 *
 * @param TagType the tag type to specialize for
 * @param Name string literal for the tag name
 *
 * Usage:
 *   NOVA_LOGGER_TRAITS_DISABLED(DebugTag, Debug)
 */
#define NOVA_LOGGER_TRAITS_DISABLED( TagType, Name ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	NOVA_LOGGER_TRAITS( TagType, Name, false, ::kmac::nova::TimestampHelper::steadyNanosecs )

#endif // KMAC_NOVA_LOGGER_TRAITS_H
