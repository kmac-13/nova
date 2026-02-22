#pragma once
#ifndef KMAC_NOVA_EXTRAS_SYNCHRONIZED_SINK_H
#define KMAC_NOVA_EXTRAS_SYNCHRONIZED_SINK_H

#include "kmac/nova/sink.h"

#include <mutex>

namespace kmac::nova::extras
{

/**
 * @brief Thread-safe sink using a mutex.
 *
 * ✅ SAFE FOR SAFETY-CRITICAL SYSTEMS
 */
class SynchronizedSink final : public kmac::nova::Sink
{
private:
	kmac::nova::Sink* _downstream;
	std::mutex _mutex;

public:
	/**
	 * @brief Construct synchronized sink wrapping a downstream sink.
	 *
	 * @param downstream the sink to forward records to (must outlive this sink)
	 */
	explicit SynchronizedSink( kmac::nova::Sink& downstream ) noexcept;

	NO_COPY_NO_MOVE( SynchronizedSink );

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

#endif // #ifndef KMAC_NOVA_EXTRAS_SYNCHRONIZED_SINK_H
