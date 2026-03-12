#pragma once
#ifndef KMAC_NOVA_EXTRAS_SYNCHRONIZED_COMPOSITE_SINK_H
#define KMAC_NOVA_EXTRAS_SYNCHRONIZED_COMPOSITE_SINK_H

#include "kmac/nova/sink.h"

#include <mutex>

namespace kmac::nova::extras
{

class CompositeSink;

/**
 * @brief Thread-safe composite sink using a mutex.
 *
 * All operations are guarded by a mutex.
 */
class SynchronizedCompositeSink final : public kmac::nova::Sink
{
private:
	CompositeSink* _composite;
	std::mutex _mutex;

public:
	/**
	 * @brief Construct synchronized sink wrapping a downstream composite sink.
	 *
	 * @param downstream the sink to forward records to (must outlive this sink)
	 */
	explicit SynchronizedCompositeSink( CompositeSink& composite ) noexcept;

	NO_COPY_NO_MOVE( SynchronizedCompositeSink );

	/**
	 * @brief Adds a sink.
	 *
	 * Acquires lock, calls downstream->add(), releases lock.
	 *
	 * @param sink the sink to add
	 */
	void addSink( kmac::nova::Sink& sink ) noexcept;

	/**
	 * @brief Clears all sinks.
	 *
	 * Acquires lock, calls downstream->clear(), releases lock.
	 */
	void clearSinks() noexcept;

	/**
	 * @brief Process a record with mutex protection.
	 *
	 * Acquires lock, calls downstream->process(), releases lock.
	 *
	 * @param record the record to process
	 */
	void process( const kmac::nova::Record& record ) noexcept override;
};

} // namespace kmac::nova::extras

#endif // KMAC_NOVA_EXTRAS_SYNCHRONIZED_COMPOSITE_SINK_H
