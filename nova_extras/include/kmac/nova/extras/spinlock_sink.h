#pragma once
#ifndef KMAC_NOVA_EXTRAS_SPINLOCK_SINK_H
#define KMAC_NOVA_EXTRAS_SPINLOCK_SINK_H

#include "kmac/nova/sink.h"

#include <atomic>

namespace kmac::nova::extras
{

/**
 * @brief Thread-safe sink using spinlock instead of mutex.
 *
 * ✅ SAFE FOR SAFETY-CRITICAL SYSTEMS
 *
 * SpinlockSink provides thread-safe access to a downstream sink using
 * a spinlock (busy-wait) instead of a mutex. This avoids kernel involvement
 * and is more suitable for real-time systems.
 *
 * Trade-offs vs SynchronizedSink (mutex):
 * - Pros: No syscalls, deterministic, better for real-time
 * - Cons: Wastes CPU cycles while waiting, bad for long critical sections
 *
 * Use when:
 * - Downstream sink operations are very short (< 1 microsecond)
 * - Real-time constraints (no kernel calls)
 * - Predictable latency required
 *
 * Avoid when:
 * - Downstream sink is slow (I/O, network, etc.)
 * - Many threads contending
 * - Battery/power efficiency matters
 *
 * Example:
 *   OStreamSink console(std::cout);
 *   SpinlockSink threadSafe(console);
 *
 *   // Multiple threads can safely call:
 *   Logger<MyTag>::bindSink(&threadSafe);
 */
class SpinlockSink final : public kmac::nova::Sink
{
private:
	kmac::nova::Sink* _downstream;
	std::atomic_flag _lock = ATOMIC_FLAG_INIT;

public:
	/**
	 * @brief Construct spinlock sink wrapping a downstream sink.
	 *
	 * @param downstream The sink to forward records to (must outlive this sink)
	 */
	explicit SpinlockSink( kmac::nova::Sink& downstream ) noexcept;

	/**
	 * @brief Process a record with spinlock protection.
	 *
	 * Acquires spinlock, calls downstream->process(), releases spinlock.
	 *
	 * WARNING: This busy-waits (spins) until the lock is acquired.
	 * If another thread holds the lock for a long time, this wastes CPU.
	 *
	 * @param record The record to process
	 */
	void process( const kmac::nova::Record& record ) noexcept override;
};

} // namespace kmac::nova::extras

#endif // KMAC_NOVA_EXTRAS_SPINLOCK_SINK_H
