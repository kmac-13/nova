#pragma once
#ifndef KMAC_NOVA_EXTRAS_BOUNDED_COMPOSITE_SINK_H
#define KMAC_NOVA_EXTRAS_BOUNDED_COMPOSITE_SINK_H

#include "kmac/nova/sink.h"

#include <array>
#include <cstddef>

// namespace kmac::nova
// {
// struct Record;
// } // namespace kmac::nova

namespace kmac::nova::extras
{

/**
 * @brief Fixed-capacity composite sink with compile-time maximum.
 *
 * ✅ SAFE FOR SAFETY-CRITICAL SYSTEMS
 *
 * BoundedCompositeSink is similar to CompositeSink but uses a fixed-size
 * std::array instead of std::vector.  This eliminates heap allocation at
 * the cost of a compile-time capacity limit.
 *
 * Use cases:
 * - embedded systems with no heap allocation
 * - real-time systems requiring deterministic behavior
 * - safety-critical code with strict memory guarantees
 *
 * Differences from CompositeSink:
 * - template parameter MaxSinks specifies maximum capacity
 * - add() returns bool (false when at capacity)
 * - no heap allocation (stack or static storage)
 * - additional remove() and contains() methods
 *
 * Features:
 * - compile-time capacity checking
 * - zero heap allocation
 * - maintains insertion order
 * - non-owning pointers (sinks must outlive composite)
 * - [[nodiscard]] return values enforce error checking
 *
 * Performance:
 * - same O(n) process() cost as CompositeSink
 * - no allocation overhead
 * - O(n) remove() operation (shifts elements)
 * - O(n) contains() operation (linear search)
 *
 * Thread safety:
 * - not thread-safe (wrap with SynchronizedSink if needed)
 * - add()/remove() are not synchronized with process()
 *
 * Usage:
 *   OStreamSink console(std::cout);
 *   FileSink file("app.log");
 *
 *   // maximum 4 child sinks
 *   BoundedCompositeSink<4> multi;
 *   if (!multi.add(console)) {
 *       // failed to add (at capacity)
 *   }
 *   multi.add(file);
 *
 *   ScopedConfigurator config;
 *   config.bind<InfoTag>(&multi);
 *
 *   NOVA_LOG_TRUNC(InfoTag) << "Logged to both";
 *
 *   // remove a sink
 *   if (multi.remove(console)) {
 *       // successfully removed
 *   }
 *
 * Capacity check:
 *   BoundedCompositeSink<2> limited;
 *   limited.add(sink1);  // returns true
 *   limited.add(sink2);  // returns true
 *   limited.add(sink3);  // returns false (at capacity)
 *
 * @tparam MaxSinks maximum number of sinks
 */
template< std::size_t MaxSinks >
class BoundedCompositeSink final : public kmac::nova::Sink
{
private:
	std::array< kmac::nova::Sink*, MaxSinks > _sinks { };  ///< fixed array of child sinks
	std::size_t _count = 0;                                ///< current number of sinks

public:
	/**
	 * @brief Construct empty bounded composite sink.
	 */
	BoundedCompositeSink() noexcept;

	/**
	 * @brief Get current number of child sinks.
	 *
	 * @return number of sinks currently registered
	 */
	std::size_t size() const noexcept;

	/**
	 * @brief Get maximum number of child sinks.
	 *
	 * @return maximum number of sinks that can be registered
	 */
	std::size_t capacity() const noexcept;

	/**
	 * @brief Add a child sink if capacity allows.
	 *
	 * @param sink sink to add (must remain valid)
	 * @return true if added successfully, false if at capacity
	 *
	 * @note sink is not owned (caller manages lifetime)
	 * @note sink is added to end (maintains insertion order)
	 */
	[[nodiscard]]
	bool add( kmac::nova::Sink& sink ) noexcept;

	/**
	 * @brief Remove a child sink if present.
	 *
	 * @param sink sink to remove
	 * @return true if removed, false if not found
	 *
	 * @note shifts subsequent sinks to maintain contiguous array
	 * @note O(n) operation
	 */
	[[nodiscard]]
	bool remove( kmac::nova::Sink& sink ) noexcept;

	/**
	 * @brief Check if sink is present.
	 *
	 * @param sink sink to check
	 * @return true if sink is registered, false otherwise
	 *
	 * @note O(n) operation (linear search)
	 */
	[[nodiscard]]
	bool contains( const kmac::nova::Sink& sink ) const noexcept;

	/**
	 * @brief Process record through all child sinks.
	 *
	 * Calls each child sink's process() in insertion order.
	 *
	 * @param record log record to process
	 *
	 * @note if a sink throws, subsequent sinks are NOT called
	 */
	void process( const kmac::nova::Record& record ) noexcept override;
};

template< std::size_t MaxSinks >
BoundedCompositeSink< MaxSinks >::BoundedCompositeSink() noexcept
{
}

template< std::size_t MaxSinks >
std::size_t BoundedCompositeSink< MaxSinks >::size() const noexcept
{
	return _count;
}

template< std::size_t MaxSinks >
std::size_t BoundedCompositeSink< MaxSinks >::capacity() const noexcept
{
	return MaxSinks;
}

template< std::size_t MaxSinks >
bool BoundedCompositeSink< MaxSinks >::add( kmac::nova::Sink& sink ) noexcept
{
	// can't add if already at capacity or already registered
	if ( _count >= MaxSinks || contains( sink ) )
	{
		return false;
	}

	// add the sink
	_sinks[ _count ] = &sink;
	++_count;
	return true;
}

template< std::size_t MaxSinks >
bool BoundedCompositeSink< MaxSinks >::remove( kmac::nova::Sink& sink ) noexcept
{
	for ( std::size_t i = 0; i < _count; ++i )
	{
		if ( _sinks[ i ] == &sink )
		{
			for ( std::size_t j = i + 1; j < _count; ++j )
			{
				_sinks[ j - 1 ] = _sinks[ j ];
			}

			_sinks[ _count - 1 ] = nullptr;
			--_count;
			return true;
		}
	}

	return false;
}

template< std::size_t MaxSinks >
bool BoundedCompositeSink< MaxSinks >::contains( const kmac::nova::Sink& sink ) const noexcept
{
	for ( std::size_t i = 0; i < _count; ++i )
	{
		if ( _sinks[ i ] == &sink )
		{
			return true;
		}
	}

	return false;
}

template< std::size_t MaxSinks >
void BoundedCompositeSink< MaxSinks >::process( const kmac::nova::Record& record ) noexcept
{
	for ( std::size_t i = 0; i < _count; ++i )
	{
		_sinks[ i ]->process( record );
	}
}

} // namespace kmac::nova::extras

#endif // KMAC_NOVA_EXTRAS_BOUNDED_COMPOSITE_SINK_H
