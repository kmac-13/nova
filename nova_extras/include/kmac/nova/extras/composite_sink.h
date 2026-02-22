#pragma once
#ifndef KMAC_NOVA_EXTRAS_COMPOSITE_SINK_H
#define KMAC_NOVA_EXTRAS_COMPOSITE_SINK_H

/**
 * ⚠️ WARNING ⚠️
 *
 * NOT SAFE FOR SAFETY-CRITICAL SYSTEMS - UNBOUNDED GROWTH
 *
 * This sink uses std::vector with unbounded growth.  Each add() may trigger
 * heap allocation and vector reallocation.  The number of sinks is unlimited.
 *
 * SAFETY ISSUES:
 * 1. UNBOUNDED MEMORY: add() can be called unlimited times
 * 2. HEAP ALLOCATION: vector may reallocate during add()
 * 3. NON-DETERMINISTIC: reallocation timing varies
 * 4. NO CAPACITY LIMIT: no compile-time or runtime bounds
 *
 * For safety-critical systems, consider BoundedCompositeSink<N> or FixedCompositeSink.
 * BoundedCompositeSink has:
 * - fixed capacity N (compile-time limit)
 * - no heap allocation (uses std::array)
 * - explicit capacity checks (add() returns bool)
 * - deterministic behavior
 *
 * Example Migration:
 *   // UNSAFE:
 *   CompositeSink composite;
 *   composite.add( sink1 );
 *   composite.add( sink2 );
 *
 *   // SAFE:
 *   BoundedCompositeSink< 4 > composite;  // max 4 sinks
 *   if ( ! composite.add( sink1 ) ) {
 *       // handle capacity error
 *   }
 *   composite.add( sink2 );
 *
 * DO-178C/IEC 61508/ISO 26262 CONSIDERATION:
 * This component cannot be certified due to unbounded memory growth.
 * Use BoundedCompositeSink<N> for certifiable systems.
 *
 * @see BoundedCompositeSink for safety-critical alternative
 * @see FixedCompositeSink for safety-critical alternative
 */

#include "kmac/nova/sink.h"

#include <vector>

namespace kmac::nova
{
struct Record;
} // namespace kmac::nova

namespace kmac::nova::extras
{

/**
 * @brief Sink that fans out log records to multiple child sinks.
 *
 * CompositeSink implements the fan-out pattern: each log record is
 * sent to all registered child sinks in the order they were added.
 *
 * Use cases:
 * - log to both console and file simultaneously
 * - send errors to multiple destinations (email, file, monitoring)
 * - split logs by severity using FilterSink children
 *
 * Features:
 * - dynamic sink management (add/clear at runtime)
 * - heap-allocated vector (use alternate composite sink for fixed-size)
 * - maintains insertion order
 * - non-owning pointers (sinks must outlive composite)
 *
 * Error handling:
 * - if a child sink throws, subsequent sinks are NOT called
 * - no exception safety guarantees (sinks should be noexcept)
 * - consider wrapping individual sinks with error handlers
 *
 * Performance:
 * - O(n) process() cost where n = number of child sinks
 * - vector allocation
 * - no virtual dispatch overhead beyond base Sink
 *
 * Thread safety:
 * - not thread-safe (wrap with SynchronizedSink if needed)
 * - add()/clear() are not synchronized with process()
 *
 * Usage:
 *   OStreamSink console(std::cout);
 *   FileSink file("app.log");
 *
 *   CompositeSink multi;
 *   multi.add(console);
 *   multi.add(file);
 *
 *   ScopedConfigurator config;
 *   config.bind<InfoTag>(&multi);
 *
 *   NOVA_LOG_TRUNC(InfoTag) << "Logged to both console and file";
 *
 * With filtering:
 *   OStreamSink console(std::cout);
 *   OStreamSink errorFile(errorStream);
 *
 *   FilterSink errorFilter(
 *       errorFile,
 *       [](const Record& r) { return strstr(r.tag, "ERROR") != nullptr; }
 *   );
 *
 *   CompositeSink multi;
 *   multi.add(console);       // all logs
 *   multi.add(errorFilter);   // errors only
 */
class CompositeSink final : public kmac::nova::Sink
{
private:
	std::vector< kmac::nova::Sink* > _sinks;  ///< child sinks (non-owning)

public:
	/**
	 * @brief Construct empty composite sink.
	 */
	CompositeSink() noexcept = default;

	NO_COPY_NO_MOVE( CompositeSink );

	/**
	 * @brief Add a child sink to the composite.
	 *
	 * @param sink sink to add (must remain valid)
	 *
	 * @note sink is not owned (caller manages lifetime)
	 * @note sink is added to end (maintains insertion order)
	 * @note may allocate heap memory (vector growth)
	 */
	void add( kmac::nova::Sink& sink ) noexcept;

	/**
	 * @brief Remove all child sinks.
	 *
	 * @note Does not destroy sinks (non-owning pointers)
	 */
	void clear() noexcept;

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

} // namespace kmac::nova::extras

#endif // KMAC_NOVA_EXTRAS_COMPOSITE_SINK_H
