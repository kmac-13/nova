#pragma once
#ifndef KMAC_NOVA_EXTRAS_MULTI_RECORD_FORMATTER_H
#define KMAC_NOVA_EXTRAS_MULTI_RECORD_FORMATTER_H

#include <cstddef>

namespace kmac {
namespace nova {

struct Record;
class Sink;

namespace extras {

/**
 * @brief Interface for formatters that emit multiple records from a single input.
 *
 * ✅ SAFE FOR SAFETY-CRITICAL SYSTEMS
 *
 * MultiRecordFormatter is designed for scenarios where a single log record needs
 * to be broken into multiple output records. Common use cases include:
 * - large payload chunking (breaking oversized messages into smaller pieces)
 * - multi-line formatting (emitting separate records for each line)
 * - structured data expansion (converting compact records into verbose output)
 *
 * Unlike regular formatters that modify a single record, MultiRecordFormatters
 * can call downstream.process() multiple times per input record.
 *
 * Design principles:
 * - no heap allocation during formatAndWrite()
 * - deterministic behavior (number of chunks is predictable)
 * - thread-safe (no internal state modified during formatting)
 * - exception-safe (noexcept guarantee)
 */
class MultiRecordFormatter
{
public:
	virtual ~MultiRecordFormatter() = default;

	/**
	 * @brief Returns the maximum size of any single chunk this formatter will emit.
	 *
	 * This helps downstream sinks allocate appropriately-sized buffers.
	 * The formatter guarantees that no individual formatted record will exceed
	 * this size.
	 *
	 * @return Maximum chunk size in bytes
	 */
	virtual std::size_t maxChunkSize() const noexcept = 0;

	/**
	 * @brief Format the record and write one or more formatted records downstream.
	 *
	 * This method may call downstream.process() multiple times to emit the
	 * formatted output. The formatter creates modified copies of the input record
	 * with updated message fields.
	 *
	 * All formatting must be completed within this call - the formatter cannot
	 * maintain state between calls.
	 *
	 * @param record the input record to format
	 * @param downstream the sink to receive formatted record(s)
	 */
	virtual void formatAndWrite(
		const kmac::nova::Record& record,
		kmac::nova::Sink& downstream
	) noexcept = 0;
};

} // namespace extras
} // namespace nova
} // namespace kmac

#endif // KMAC_NOVA_EXTRAS_MULTI_RECORD_FORMATTER_H
