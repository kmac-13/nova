#pragma once
#ifndef KMAC_NOVA_NON_COPYABLE_H
#define KMAC_NOVA_NON_COPYABLE_H

// convenience macro to explicitly delete copy and move constructors and assignment operators
// #define IMMOVABLE( Type ) \
// 	Type( const Type& ) = delete; \
// 	Type& operator=( const Type& ) = delete; \
// 	Type( Type&& ) = delete; \
// 	Type& operator=( Type&& ) = delete

namespace kmac::nova
{

/**
 * @brief Base class that disables copy and move semantics.
 *
 * Inherit from this class to prevent copying and moving of derived types.
 *
 * Usage:
 * @code
 * class MyClass : private Immovable
 * {
 *     // ...
 * };
 * @endcode
 *
 * Inheritance should be private — Immovable is a implementation detail,
 * not part of the public interface of the derived class.
 *
 * @note This class has no data members and imposes no runtime overhead.
 */struct Immovable
{
	Immovable() = default;
	Immovable( const Immovable& ) = delete;
	Immovable& operator=( const Immovable& ) = delete;
	Immovable( Immovable&& ) = delete;
	Immovable& operator=( Immovable&& ) = delete;
};

} // namespace kmac::nova

#endif // KMAC_NOVA_NON_COPYABLE_H
