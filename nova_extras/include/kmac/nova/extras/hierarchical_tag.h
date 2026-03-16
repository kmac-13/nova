#pragma once
#ifndef KMAC_NOVA_EXTRAS_HIERARCHICAL_TAG_H
#define KMAC_NOVA_EXTRAS_HIERARCHICAL_TAG_H

#include "kmac/nova/logger_traits.h"

#include <cstring>

namespace kmac::nova::extras
{

/**
 * @brief Template for creating hierarchical tag types.
 *
 * ✅ SAFE FOR SAFETY-CRITICAL SYSTEMS
 *
 * HierarchicalTag combines a Subsystem and Severity into a single tag type,
 * allowing you to organize logs by both category and importance level.
 *
 * This provides a traditional logging model (subsystems + severity levels)
 * while maintaining Nova's compile-time tag-based routing.
 *
 * Benefits:
 * - familiar model for users accustomed to traditional loggers
 * - compile-time type safety
 * - easy to bind all tags from a subsystem or severity level
 * - hierarchical filtering capabilities
 *
 * Example:
 *   struct Audio {};
 *   struct Network {};
 *
 *   struct Debug {};
 *   struct Info {};
 *   struct Error {};
 *
 *   using AudioDebug = HierarchicalTag<Audio, Debug>;
 *   using AudioError = HierarchicalTag<Audio, Error>;
 *   using NetworkInfo = HierarchicalTag<Network, Info>;
 *
 *   NOVA_LOG(AudioDebug) << "Buffer underrun";
 *   NOVA_LOG(NetworkInfo) << "Connected";
 */
template< typename Subsystem, typename Severity >
struct HierarchicalTag
{
	using subsystem = Subsystem;
	using severity = Severity;

	// compile-time name generation
	// result format: "Subsystem.Severity"
	// static constexpr const char* tagName = /* generated for associated logger_traits below */;
};

} // namespace kmac::nova::extras

//
// Specialization to provide automatic tag names
//

// NOLINT NOTE: stringification operator # has no constexpr equivalent
#define TAG_NAME( tag ) #tag /* NOLINT(cppcoreguidelines-macro-usage) */

template< typename Subsystem, typename Severity >
struct kmac::nova::logger_traits< kmac::nova::extras::HierarchicalTag< Subsystem, Severity > >
{
	// generate tag name as "Subsystem.Severity"
	static constexpr const char* tagName = TAG_NAME( Subsystem ) "." TAG_NAME( Severity );

	static constexpr char tagIdStorage = '\0';
	static /*constexpr*/ std::uintptr_t tagId() noexcept
	{
		// NOLINT NOTE: pointer-to-integer for unique tag ID generation; no safer alternative in C++17
		return reinterpret_cast< std::uintptr_t >( &tagIdStorage );  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
	}

	static constexpr bool enabled = true;

	static std::uint64_t timestamp() noexcept
	{
		return TimestampHelper::steadyNanosecs();
	}
};


//
// Common Severity Levels
//

namespace kmac::nova::extras
{
	// standard severity levels
	struct Trace {};
	struct Debug {};
	struct Info {};
	struct Warning {};
	struct Error {};
	struct Fatal {};

	// numeric severity values for filtering
	enum class SeverityLevel : std::uint8_t
	{
		Trace   = 0,
		Debug   = 1,
		Info    = 2,
		Warning = 3,
		Error   = 4,
		Fatal   = 5
	};

	// helper to get severity level
	template< typename Severity >
	struct SeverityValue;

	template<> struct SeverityValue< Trace >   { static constexpr int value = int( SeverityLevel::Trace   ); };
	template<> struct SeverityValue< Debug >   { static constexpr int value = int( SeverityLevel::Debug   ); };
	template<> struct SeverityValue< Info >    { static constexpr int value = int( SeverityLevel::Info    ); };
	template<> struct SeverityValue< Warning > { static constexpr int value = int( SeverityLevel::Warning ); };
	template<> struct SeverityValue< Error >   { static constexpr int value = int( SeverityLevel::Error   ); };
	template<> struct SeverityValue< Fatal >   { static constexpr int value = int( SeverityLevel::Fatal   ); };
} // namespace kmac::nova::extras

//
// Helper Functions
//

namespace kmac::nova::extras
{

/**
 * @brief Check if a tag belongs to a specific subsystem.
 */
template< typename Tag, typename Subsystem >
struct IsSubsystem : std::false_type {};

template< typename Subsystem, typename Severity >
struct IsSubsystem< HierarchicalTag< Subsystem, Severity >, Subsystem >
	: std::true_type {};

/**
 * @brief Check if a tag has a specific severity.
 */
template< typename Tag, typename Severity >
struct IsSeverity : std::false_type {};

template< typename Subsystem, typename Severity >
struct IsSeverity< HierarchicalTag< Subsystem, Severity >, Severity >
	: std::true_type {};

/**
 * @brief Get severity level value from tag.
 */
template< typename Tag >
struct GetSeverityLevel
{
	static constexpr int value = -1;
};

template< typename Subsystem, typename Severity >
struct GetSeverityLevel< HierarchicalTag< Subsystem, Severity > >
{
	static constexpr int value = SeverityValue< Severity >::value;
};

} // namespace kmac::nova::extras

#endif // KMAC_NOVA_EXTRAS_HIERARCHICAL_TAG_H
