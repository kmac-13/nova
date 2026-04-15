#pragma once
#ifndef KMAC_NOVA_SCOPED_CONFIGURATOR_H
#define KMAC_NOVA_SCOPED_CONFIGURATOR_H

#include "immovable.h"
#include "logger.h"
#include "platform/array.h"
#include "platform/config.h"

#include <cstddef>

namespace kmac {
namespace nova {

class Sink;

/**
 * @brief RAII helper for binding sinks to tagged Loggers with fixed capacity.
 *
 * ScopedConfigurator provides scoped binding of sinks to tagged loggers.
 * Any logger bound through a ScopedConfigurator is automatically unbound
 * when the configurator is destroyed. It does NOT preserve or restore
 * prior configuration state - destruction simply unbinds.
 *
 * ScopedConfigurator is suitable for safety-critical systems that prohibit
 * heap allocation due to the use of platform::Array (std::array in standard mode,
 * C-style array wrapper in bare-metal mode).
 *
 * Key characteristics:
 * - RAII pattern (bindings active during configurator lifetime)
 * - explicit configuration (no automatic restoration)
 * - deterministic unbinding (reverse order on destruction)
 * - duplicate detection (same tag cannot be bound twice)
 * - zero heap allocation (all storage is stack-based)
 * - compile-time capacity limit (template parameter)
 *
 * Rules:
 * - bind<Tag>(sink) binds sink to Logger<Tag> and registers with configurator
 * - bindFrom<DestTag, SrcTag>() copies binding from SrcTag to DestTag
 * - unbind<Tag>() unbinds Logger<Tag> and unregisters from configurator
 * - each Logger<Tag> may be bound at most once per configurator
 * - destruction unbinds all registered loggers in reverse registration order
 * - no prior state restoration (explicit over implicit)
 * - exceeding MaxBindings is a programming error (assert in debug, silently ignore in release)
 *
 * Thread safety:
 * - not thread-safe (single-threaded configuration expected)
 * - Logger<Tag> binding itself IS thread-safe (atomic)
 * - configurator modifications must be serialized
 *
 * Memory:
 * - uses platform::Array for tracking (stack allocation only)
 * - stores function pointers only (MaxBindings * sizeof(void*))
 * - O(n) unbinding where n = number of bound loggers
 *
 * @tparam MaxBindings maximum number of loggers that can be bound (default: 16)
 *
 * Capacity selection guidelines:
 * - small embedded: 5-10
 * - typical application: 16-32 (default)
 * - large system: 64-128
 * - choose generously - no runtime cost for unused capacity
 *
 * Important notes:
 * - sinks must outlive the configurator (non-owning pointers)
 * - configurator does NOT own sinks (caller manages lifetime)
 * - duplicate bind<Tag>() calls are ignored (first wins)
 * - destruction does NOT restore previous bindings
 * - use nested configurators for temporary overrides
 * - exceeding capacity is caught by assert in debug builds
 * - exceeding capacity is silently ignored in release builds (fail-safe)
 */
template< size_t MaxBindings = 16 >
class ScopedConfigurator : private Immovable
{
private:
	using UnbindFn = void ( * )( );  // noexcept not allowed in C++14

	platform::Array< UnbindFn, MaxBindings > _boundList;  ///< registered unbind functions
	size_t _count = 0;  ///< number of registered unbind functions

#ifndef NDEBUG
	bool _overflowed = false;  ///< debug flag: attempted to exceed capacity
#endif

public:
	/**
	 * @brief Construct empty configurator.
	 */
	ScopedConfigurator() noexcept = default;

	/**
	 * @brief Destroy configurator and unbind all registered loggers.
	 *
	 * Unbinds loggers in reverse registration order (LIFO/stack order).
	 */
	inline ~ScopedConfigurator() noexcept;

	/**
	 * @brief Bind sink to logger and register for automatic unbinding.
	 *
	 * If the configurator is at capacity, the binding is ignored.
	 * In debug builds, this triggers an assertion.
	 * In release builds, this fails silently (fail-safe behavior).
	 *
	 * @tparam Tag logger tag type
	 * @param sink sink to bind (nullptr disables logging at runtime)
	 *
	 * @note if Tag is already bound by this configurator, call is ignored
	 * @note sink is not owned (must remain valid until unbind/destruction)
	 * @note exceeding capacity is a programming error (assert in debug)
	 */
	template< typename Tag >
	inline void bind( Sink* sink ) noexcept;

	/**
	 * @brief Try to bind sink to logger (non-asserting variant).
	 *
	 * This is similar to bind() but returns success/failure instead of asserting.
	 * Useful for testing and when overflow is a valid runtime condition.
	 *
	 * @tparam Tag logger tag type
	 * @param sink sink to bind (nullptr disables logging at runtime)
	 * @return true if binding succeeded, false if capacity exceeded or already bound
	 *
	 * @note if Tag is already bound by this configurator, returns false
	 * @note sink is not owned (must remain valid until unbind/destruction)
	 */
	template< typename Tag >
	inline bool tryBind( Sink* sink ) noexcept;

	/**
	 * @brief Copy sink binding from one logger to another.
	 *
	 * Binds DestTag to use the same sink currently bound to SrcTag.
	 * Useful for routing multiple tags to the same destination.
	 *
	 * If the configurator is at capacity, the binding is ignored.
	 *
	 * @tparam DestTag destination logger tag
	 * @tparam SrcTag source logger tag
	 *
	 * @note if DestTag is already bound, call is ignored
	 * @note SrcTag must have a sink bound (or nullptr is copied)
	 * @note exceeding capacity is a programming error (assert in debug)
	 */
	template< typename DestTag, typename SrcTag >
	inline void bindFrom() noexcept;

	/**
	 * @brief Try to copy sink binding (non-asserting variant).
	 *
	 * @tparam DestTag destination logger tag
	 * @tparam SrcTag source logger tag
	 * @return true if binding succeeded, false if capacity exceeded or already bound
	 */
	template< typename DestTag, typename SrcTag >
	inline bool tryBindFrom() noexcept;

	/**
	 * @brief Unbind logger and unregister from configurator.
	 *
	 * @tparam Tag logger tag type
	 *
	 * @note if Tag is not bound by this configurator, call is a no-op
	 * @note does NOT restore previous binding
	 */
	template< typename Tag >
	inline void unbind() noexcept;

	/**
	 * @brief Get number of currently bound loggers.
	 *
	 * @return number of loggers registered with this configurator
	 */
	inline size_t bindingCount() const noexcept;

	/**
	 * @brief Get maximum capacity of this configurator.
	 *
	 * @return maximum number of loggers that can be bound (MaxBindings)
	 */
	inline constexpr size_t maxBindings() const noexcept;

	/**
	 * @brief Check if configurator is at capacity.
	 *
	 * @return true if no more bindings can be added, false otherwise
	 */
	bool isFull() const noexcept;

#ifndef NDEBUG
	/**
	 * @brief Check if capacity was exceeded (debug builds only).
	 *
	 * This flag is set when bind() or bindFrom() is called after
	 * reaching capacity. It indicates a programming error that should
	 * be fixed by increasing MaxBindings or reducing bindings.
	 *
	 * @return true if capacity was exceeded, false otherwise
	 * @note only available in debug builds
	 */
	inline bool hasOverflowed() const noexcept;
#endif

private:
	/**
	 * @brief Check if unbind function is registered.
	 *
	 * @param ubf unbind function to check
	 * @return true if registered, false otherwise
	 */
	inline bool isBound( UnbindFn unbindFn ) const noexcept;

	/**
	 * @brief Register unbind function.
	 *
	 * @param ubf unbind function to register
	 *
	 * @note Ignores if already registered (duplicate detection)
	 * @note Ignores if at capacity (asserts in debug, silent in release)
	 */
	inline void add( UnbindFn unbindFn ) noexcept;

	/**
	 * @brief Unregister unbind function.
	 *
	 * @param ubf unbind function to unregister
	 */
	inline void remove( UnbindFn unbindFn ) noexcept;
};

template< size_t MaxBindings >
ScopedConfigurator< MaxBindings >::~ScopedConfigurator() noexcept
{
	// unbind in reverse order (LIFO/stack order)
	for ( size_t i = _count; i > 0; --i )
	{
		_boundList[ i - 1 ]();
	}
}

template< size_t MaxBindings >
template < typename Tag >
void ScopedConfigurator< MaxBindings >::bind( Sink* sink ) noexcept
{
	Logger< Tag >::bindSink( sink );
	add( &Logger< Tag >::unbindSink );
}

template< size_t MaxBindings >
template< typename Tag >
bool ScopedConfigurator< MaxBindings >::tryBind( Sink* sink ) noexcept
{
	// check capacity
	if ( _count >= MaxBindings )
	{
#ifndef NDEBUG
		_overflowed = true;
#endif
		return false;
	}

	// bind the sink
	Logger< Tag >::bindSink( sink );

	// only register if not already bound
	if ( ! isBound( &Logger< Tag >::unbindSink ) )
	{
		_boundList[ _count++ ] = &Logger< Tag >::unbindSink;
	}

	return true;
}

template< size_t MaxBindings >
template < typename DestTag, typename SrcTag >
void ScopedConfigurator< MaxBindings >::bindFrom() noexcept
{
	Logger< DestTag >::bindSink( Logger< SrcTag >::getSink() );
	add( &Logger< DestTag >::unbindSink );
}

template< size_t MaxBindings >
template< typename DestTag, typename SrcTag >
bool ScopedConfigurator< MaxBindings >::tryBindFrom() noexcept
{
	// check capacity
	if ( _count >= MaxBindings )
	{
#ifndef NDEBUG
		_overflowed = true;
#endif
		return false;
	}

	// bind the sink
	Logger< DestTag >::bindSink( Logger< SrcTag >::getSink() );

	// only register if not already bound
	if ( ! isBound( &Logger< DestTag >::unbindSink ) )
	{
		_boundList[ _count++ ] = &Logger< DestTag >::unbindSink;
	}

	return true;
}

template< size_t MaxBindings >
template < typename Tag >
void ScopedConfigurator< MaxBindings >::unbind() noexcept
{
	Logger< Tag >::unbindSink();
	remove( &Logger< Tag >::unbindSink );
}

template< size_t MaxBindings >
size_t ScopedConfigurator< MaxBindings >::bindingCount() const noexcept
{
	return _count;
}

template< size_t MaxBindings >
constexpr size_t ScopedConfigurator< MaxBindings >::maxBindings() const noexcept
{
	return MaxBindings;
}

template< size_t MaxBindings >
bool ScopedConfigurator< MaxBindings >::isFull() const noexcept
{
	return _count >= MaxBindings;
}

#ifndef NDEBUG
template< size_t MaxBindings >
bool ScopedConfigurator< MaxBindings >::hasOverflowed() const noexcept
{
	return _overflowed;
}
#endif

template< size_t MaxBindings >
bool ScopedConfigurator< MaxBindings >::isBound( UnbindFn unbindFn ) const noexcept
{
	for ( size_t i = 0; i < _count; ++i )
	{
		if ( _boundList[ i ] == unbindFn )
		{
			return true;
		}
	}

	return false;
}

template< size_t MaxBindings >
void ScopedConfigurator< MaxBindings >::add( UnbindFn unbindFn ) noexcept
{
	// don't add if already bound (duplicate detection)
	if ( isBound( unbindFn ) )
	{
		return;
	}

	// check capacity
	if ( _count >= MaxBindings )
	{
		// in debug, assert to catch programming error
		// NOLINT NOTE: runtime guard, not compile-time; overflow is a runtime condition
		NOVA_ASSERT( false && "ScopedConfigurator capacity exceeded - increase MaxBindings" );  // NOLINT(cert-dcl03-c,misc-static-assert)

#ifndef NDEBUG
		_overflowed = true;
#endif
		// in release, silently ignore (fail-safe - logging misconfiguration
		// should not crash the system)
		return;
	}

	// add to list
	_boundList[ _count++ ] = unbindFn;
}

template< size_t MaxBindings >
void ScopedConfigurator< MaxBindings >::remove( UnbindFn unbindFn ) noexcept
{
	// find and remove the unbind function
	for ( size_t i = 0; i < _count; ++i )
	{
		if ( _boundList[ i ] == unbindFn )
		{
			// shift remaining elements left
			for ( size_t j = i + 1; j < _count; ++j )
			{
				_boundList[ j - 1 ] = _boundList[ j ];
			}

			--_count;
			return;
		}
	}
}

} // namespace nova
} // namespace kmac

#endif // KMAC_NOVA_SCOPED_CONFIGURATOR_H
