#pragma once
#ifndef KMAC_NOVA_DETAILS_H
#define KMAC_NOVA_DETAILS_H

#include <cstddef>
#include <cstdint>

namespace kmac {
namespace nova {
namespace details {

/**
 * @brief Compile-time stripping of path from, e.g., __FILE__.
 */
constexpr const char* fileName( const char* path );

/**
 * @brief FNV-1a hashing function for generating a 64-bit hash from a string.
 *
 * @param str string to hash
 * @return 64-bit hash
 */
constexpr std::uint64_t fnv1a( const char* str ) noexcept;

/**
 * @brief FNV-1a hashing function for generating a 64-bit hash from a string literal.
 *
 * @param str string literal to hash
 * @return 64-bit hash
 */
template< std::size_t N >
constexpr std::uint64_t fnv1a( const char ( &str )[ N ] ) noexcept;  // NOLINT(cppcoreguidelines-avoid-c-arrays)

/**
 * @brief Primary template used to guard against duplicate tag IDs.
 *
 * This variable template acts as a compile-time registry for tag identifiers.
 * Each NOVA_LOGGER_TRAITS invocation defines a specialisation:
 *
 *     TagIdVal<tagId>::value = tagId
 *
 * If two tags hash to the same tagId within a translation unit, the compiler
 * encounters multiple definitions of the same specialisation and emits a
 * redefinition error.  The conflicting tag types appear in the error message
 * via the surrounding template instantiation context - the compiler already
 * has this information, so no string storage is required.
 *
 * ### Collision detection scope
 *
 * This mechanism detects collisions within a single translation unit or binary.
 * Tags defined in separately compiled shared libraries are not automatically
 * validated against each other unless a dedicated validation translation unit
 * is provided that includes all tag headers from all linked libraries.
 *
 * ### Cross-binary collision detection
 *
 * To detect collisions across all tags in a project - including those defined in
 * third-party libraries - applications may provide a validation translation unit
 * containing a switch statement referencing every tagId. The compiler rejects
 * duplicate case labels if any two tags produce the same identifier:
 *
 * @code
 * // nova_tag_validation.cpp
 * // This file exists solely to detect tagId hash collisions at compile time.
 * // A "duplicate case value" error means two tags hash to the same tagId.
 * // Add every tag from every linked library as a case statement below.
 *
 * #include <kmac/nova/logger_traits.h>
 * #include <libsensors/tags.h>
 * #include <libui/tags.h>
 * #include "app/tags.h"
 * // ... all other tag headers ...
 *
 * namespace
 * {
 *     void novaTagCollisionCheck();
 *     void novaTagCollisionCheck()
 *     {
 *         std::uint64_t id = 0;
 *         switch ( id )
 *         {
 *         case kmac::nova::logger_traits< sensorslib::DebugTag >::tagId: break;
 *         case kmac::nova::logger_traits< sensorslib::ErrorTag >::tagId: break;
 *         case kmac::nova::logger_traits< app::ui::RenderTag >::tagId:   break;
 *         // ... one case per tag ...
 *         }
 *     }
 * } // namespace
 * @endcode
 *
 * The function is never called - the compiler validates case labels regardless.
 * Placing it in an anonymous namespace suppresses unused-function warnings without
 * needing compiler-specific attributes.
 *
 * Consider maintaining the list of Nova domain tags using an X-macro to auto-expand
 * the case statements based on all tags, e.g.:
 *   #define NOVA_TAG_LIST(X) \
 *      X( sensorslib::DebugTag ) \
 *      X( sensorslib::ErrorTag ) \
 *      X( app::ui::RenderTag ) \
 *      X( app::net::PacketTag )
 *   switch ( id ) {
 *   #define NOVA_TAG_CASE(Tag) \
 *      case kmac::nova::logger_traits< Tag >::tagId: break;
 *      NOVA_TAG_LIST( NOVA_TAG_CASE )
 *   #undef NOVA_TAG_CASE
 *   }
 *
 * ### Hashing algorithm
 *
 * Tag identifiers are produced using a constexpr FNV-1a 64-bit hash over the
 * fully-qualified tag type name.  After the standard FNV-1a iteration, a final
 * avalanche mix is applied to improve bit diffusion:
 *
 *     hash ^= hash >> 32
 *     hash *= FNV_FINAL
 *     hash ^= hash >> 32
 *
 * This inexpensive finalisation step significantly reduces clustering in the
 * upper bits of the hash while maintaining deterministic results across
 * compilers and binaries.
 *
 * ### Collision probability
 *
 * With a 64-bit identifier space and the avalanche mix applied, the probability
 * of a collision among 1000 distinct tags is approximately 2^-54, which is
 * negligible for logging domain identifiers.  Users should nonetheless specify
 * fully-qualified tag type names in NOVA_LOGGER_TRAITS invocations to maximize
 * hash entropy and produce clear diagnostic messages if a collision occurs.
 */

// helper requiring specialization to detect/prevent collisions
//
// Each NOVA_LOGGER_TRAITS invocation specialises this variable template with
// the tag's own hash value.  A collision between two tags produces a duplicate
// specialisation which the compiler rejects as a redefinition error.  The
// conflicting tag types appear in the error message via the surrounding template
// instantiation context - no string storage is needed for diagnostics.
template< std::uint64_t Id >
struct TagIdVal { static constexpr std::uint64_t value = 0; };

//
// IMPLEMENTATION
//

// NOLINT NOTE - intentional tail recursion required for constexpr
// compatibility with C++11, which does not permit loops in constexpr
// functions; recursion depth is bounded by string length
constexpr const char* recurseFileName( const char* path, const char* last ) noexcept  // NOLINT(misc-no-recursion)
{
	return *path == '\0'
		? last
		: recurseFileName( path + 1, ( *path == '/' || *path == '\\' ) ? path + 1 : last );
}

constexpr const char* fileName( const char* path )
{
	return recurseFileName( path, path );
}

// FNV-1a constants
constexpr std::uint64_t FNV_OFFSET = 14695981039346656037ULL;
constexpr std::uint64_t FNV_PRIME = 1099511628211ULL;
constexpr std::uint64_t FNV_FINAL = 0xd6e8feb86659fd93ULL;

constexpr std::uint64_t fnv1aAvalanche( std::uint64_t hash ) noexcept
{
	return ( ( hash ^ ( hash >> 32U ) ) * FNV_FINAL ) ^ ( ( ( hash ^ ( hash >> 32U ) ) * FNV_FINAL ) >> 32U );
}

// NOLINT NOTE - intentional tail recursion required for constexpr
// compatibility with C++11, which does not permit loops in constexp
// functions; recursion depth is bounded by string length
constexpr std::uint64_t recurseFnv1a( const char* str, std::uint64_t hash ) noexcept  // NOLINT(misc-no-recursion)
{
	return *str == '\0'
		? fnv1aAvalanche( hash )
		: recurseFnv1a( str + 1, ( hash ^ static_cast< std::uint8_t >( *str ) ) * FNV_PRIME );
}

// string hash
constexpr std::uint64_t fnv1a( const char* str ) noexcept
{
	return recurseFnv1a( str, FNV_OFFSET );
}

// string literal hash
template< std::size_t N >
constexpr std::uint64_t fnv1a( const char ( &str )[ N ] ) noexcept  // NOLINT(cppcoreguidelines-avoid-c-arrays)
{
	return fnv1a( static_cast< const char* >( str ) );
}

} // namespace details
} // namespace nova
} // namespace kmac

#endif // KMAC_NOVA_DETAILS_H
