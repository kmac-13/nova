#pragma once
#ifndef KMAC_NOVA_VERSION_H
#define KMAC_NOVA_VERSION_H

#include <kmac/nova/platform/config.h>

#include <cstddef>

#define NOVA_VERSION_MAJOR_INT 0
#define NOVA_VERSION_MINOR_INT 1
#define NOVA_VERSION_PATCH_INT 0

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
// two-level stringify forces macro expansion before stringification
#define NOVA_VERSION_STRINGIFY( x ) #x
#define NOVA_VERSION_TOSTRING( x ) NOVA_VERSION_STRINGIFY( x )

#define NOVA_VERSION_STRING_LITERAL \
	NOVA_VERSION_TOSTRING( NOVA_VERSION_MAJOR_INT ) "." \
	NOVA_VERSION_TOSTRING( NOVA_VERSION_MINOR_INT ) "." \
	NOVA_VERSION_TOSTRING( NOVA_VERSION_PATCH_INT )
// NOLINTEND(cppcoreguidelines-macro-usage)

NOVA_INLINE_VAR constexpr std::size_t NOVA_VERSION_MAJOR = NOVA_VERSION_MAJOR_INT;
NOVA_INLINE_VAR constexpr std::size_t NOVA_VERSION_MINOR = NOVA_VERSION_MINOR_INT;
NOVA_INLINE_VAR constexpr std::size_t NOVA_VERSION_PATCH = NOVA_VERSION_PATCH_INT;
NOVA_INLINE_VAR constexpr const char* NOVA_VERSION_STRING = NOVA_VERSION_STRING_LITERAL;

#endif // KMAC_NOVA_VERSION_H
