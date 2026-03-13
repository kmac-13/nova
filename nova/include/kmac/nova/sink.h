#pragma once
#ifndef KMAC_NOVA_SINK_H
#define KMAC_NOVA_SINK_H

#include "immovable.h"

namespace kmac::nova
{

struct Record;

/**
 * @brief Interface for log record destinations.
 *
 * Sink is the core abstraction for all log output destinations in Nova.
 * All concrete sink implementations (file, console, network, etc.) must
 * inherit from this interface and implement process().
 *
 * Design principles:
 * - single method interface (process) for simplicity
 * - virtual but not pure virtual destructor for RAII support
 * - noexcept not required (implementations choose safety level)
 * - non-copyable/non-movable
 *
 * Composition:
 * Sinks can wrap other sinks (decorator pattern):
 * - FormattingSink wraps another sink to add formatting
 * - SynchronizedSink wraps another sink to add thread safety
 * - CompositeSink fans out to multiple sinks
 *
 * Thread safety:
 * - base interface makes no thread safety guarantees
 * - implementations may be thread-safe (SynchronizedSink, MemoryPoolAsyncSink)
 * - default implementations are NOT thread-safe
 *
 * Usage:
 *   class SampleSink : public Sink {
 *   public:
 *       void process(const Record& record) override {
 *           // write record to destination
 *       }
 *   };
 *
 *   SampleSink sink;
 *   ScopedConfigurator config;
 *   config.bind<SampleTag>(&sink);
 *   NOVA_LOG_TRUNC(SampleTag) << "Hello!";
 */
class Sink : private Immovable
{
public:
	virtual ~Sink() = default;

	/**
	 * @brief Process a log record.
	 *
	 * Called by Logger<Tag> for each log entry when tag is enabled.
	 *
	 * @param record log record with metadata and message
	 *
	 * @note Record lifetime is limited to this call
	 * @note do not store pointers from record
	 * @note message might not be null-terminated
	 */
	virtual void process( const Record& record ) = 0;
};

} // namespace kmac::nova

#endif // KMAC_NOVA_SINK_H
