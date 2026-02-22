#pragma once
#ifndef KMAC_NOVA_EXTRAS_FIXED_COMPOSITE_SINK_H
#define KMAC_NOVA_EXTRAS_FIXED_COMPOSITE_SINK_H

#include <cstddef>

#include "kmac/nova/sink.h"

namespace kmac::nova
{
struct Record;
} // namespace kmac::nova

namespace kmac::nova::extras
{

/**
 * @brief Fixed-size composite sink that wraps an external array of sinks.
 *
 * ✅ SAFE FOR SAFETY-CRITICAL SYSTEMS
 *
 * FixedCompositeSink takes a pointer to an array of Sink pointers and a count,
 * then forwards all log records to each sink in the array.  Unlike CompositeSink
 * (which uses std::vector) or BoundedCompositeSink (which uses std::array),
 * this sink wraps an externally-managed array.
 *
 * Use cases:
 * - working with C-style arrays of sinks
 * - zero overhead wrapper (no internal storage)
 * - integrating with existing sink arrays
 * - stack-allocated sink arrays with runtime size
 *
 * Differences from other composites:
 * - no internal storage (wraps external array)
 * - runtime size (not compile-time like BoundedCompositeSink)
 * - immutable after construction (no add/remove)
 * - caller manages sink array lifetime
 *
 * Features:
 * - zero heap allocation
 * - minimal overhead (just stores pointer and count)
 * - non-owning (caller manages array lifetime)
 * - maintains array order
 *
 * Memory safety:
 * - array must remain valid during FixedCompositeSink lifetime
 * - all sinks in array must remain valid
 * - count must be at most actual array size
 * - no bounds checking in release builds
 *
 * Performance:
 * - O(n) process() cost where n = count
 * - no allocation overhead
 * - direct pointer dereference
 *
 * Thread safety:
 * - not thread-safe (wrap with SynchronizedSink if needed)
 *
 * Usage:
 *   OStreamSink console(std::cout);
 *   FileSink file("app.log");
 *
 *   // Create array of sink pointers
 *   Sink* sinks[] = { &console, &file };
 *
 *   FixedCompositeSink multi(sinks, 2);
 *
 *   ScopedConfigurator config;
 *   config.bind<InfoTag>(&multi);
 *
 *   NOVA_LOG_TRUNC(InfoTag) << "Logged to both";
 *
 * With std::array:
 *   std::array<Sink*, 3> sinks = { &sink1, &sink2, &sink3 };
 *   FixedCompositeSink multi(sinks.data(), sinks.size());
 */
class FixedCompositeSink final : public kmac::nova::Sink
{
private:
	kmac::nova::Sink** _sinks;  ///< external array of sinks (not owned)
	std::size_t _count;         ///< number of sinks in array

public:
	/**
	 * @brief Construct with external sink array.
	 *
	 * @param sinks pointer to array of Sink pointers (must remain valid)
	 * @param count number of sinks in array
	 *
	 * @note array is not owned (caller manages lifetime)
	 * @note all sinks must remain valid during lifetime
	 * @note count must be at most actual array size
	 */
	FixedCompositeSink( kmac::nova::Sink** sinks, std::size_t count ) noexcept;

	NO_COPY_NO_MOVE( FixedCompositeSink );

	/**
	 * @brief Process record through all sinks in array.
	 *
	 * Calls each sink's process() in array order.
	 *
	 * @param record log record to process
	 *
	 * @note if a sink throws, subsequent sinks are NOT called
	 * @note each sink is checked for null pointer
	 */
	void process( const kmac::nova::Record& record ) noexcept override;
};

} // namespace kmac::nova::extras

#endif // KMAC_NOVA_EXTRAS_FIXED_COMPOSITE_SINK_H
