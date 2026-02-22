#pragma once
#ifndef KMAC_NOVA_EXTRAS_NULL_SINK_H
#define KMAC_NOVA_EXTRAS_NULL_SINK_H

#include "kmac/nova/sink.h"

namespace kmac::nova
{
struct Record;
} // namespace kmac::nova

namespace kmac::nova::extras
{

/**
 * @brief Sink that discards all log records.
 *
 * ✅ SAFE FOR SAFETY-CRITICAL SYSTEMS
 *
 * NullSink implements the Sink interface but performs no actual logging.
 * Useful for:
 * - disabling logging output without changing code
 * - performance testing (measure overhead excluding I/O)
 * - testing scenarios where logging should be suppressed
 *
 * Design:
 * - can use single global instance or individual instances
 * - zero overhead process() implementation
 * - thread-safe (does nothing, no state)
 *
 * Performance:
 * NullSink::process() is the fastest possible sink - it simply returns.
 * Use it when measuring the overhead of record building without I/O costs.
 *
 * Logging can be disabled at compile-time by setting a logger_trait's enabled
 * flag to false, disabled at runtime by setting a Logger's sink to nullptr, or
 * by using NullSink as the sink.
 *
 * Usage:
 *   ScopedConfigurator config;
 *   config.bind<AppTag>(&NullSink::instance());
 *
 *   // all logs to MyTag will be discarded
 *   NOVA_LOG_TRUNC(AppTag) << "This is discarded";
 */
class NullSink final : public kmac::nova::Sink
{
public:
	/**
	 * @brief Get the single global instance.
	 *
	 * @return reference to global NullSink instance
	 */
	static NullSink& instance() noexcept;

public:
	/**
	 * @brief Constructor for singleton.
	 */
	NullSink() noexcept = default;

	NO_COPY_NO_MOVE( NullSink );

	/**
	 * @brief Discard log record (no-op).
	 *
	 * @param record Log record (ignored)
	 */
	void process( const kmac::nova::Record& record ) noexcept override;
};

} // namespace kmac::nova::extras

#endif // KMAC_NOVA_EXTRAS_NULL_SINK_H
