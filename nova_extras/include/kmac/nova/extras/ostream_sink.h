#pragma once
#ifndef KMAC_NOVA_EXTRAS_OSTREAM_SINK_H
#define KMAC_NOVA_EXTRAS_OSTREAM_SINK_H

#include "kmac/nova/sink.h"
#include "kmac/nova/record.h"

#include <ostream>

namespace kmac::nova::extras
{

/**
 * @brief Sink that writes raw log messages to a std::ostream.
 *
 * ⚠️ USE WITH CARE IN SAFETY-CRITICAL SYSTEMS, DEPENDS ON std::ostream BEHAVIOR
 *
 * OStreamSink writes the raw message bytes to any std::ostream
 * (cout, cerr, file stream, etc.) with no additional formatting,
 * no newlines, and optional automatic flushing.
 *
 * Features:
 * - direct ostream output (no buffering beyond ostream's own)
 * - raw message bytes only (no metadata, no newlines)
 * - works with any std::ostream (console, file, stringstream)
 *
 * Limitations:
 * - not thread-safe (wrap with SynchronizedSink if needed)
 * - no formatting (use FormattingSink for that)
 * - no automatic newlines (include '\n' in message if needed)
 * - no automatic flushing by default (use optional flush-on-write flag)
 * - writes raw message bytes (may not be null-terminated)
 *
 * For formatted output with newlines and timestamps:
 * - wrap with FormattingSink or FormattingSinkIso8601
 * - implement custom formatter function
 *
 * Usage:
 *   OStreamSink coutSink(std::cout);
 *   ScopedConfigurator config;
 *   config.bind<InfoTag>(&coutSink);
 *
 *   NOVA_LOG_TRUNC(InfoTag) << "Hello World";
 *   // output: Hello World (no newline)
 *
 * With newlines in message:
 *   NOVA_LOG_TRUNC(InfoTag) << "Hello World\n";
 *   // output: Hello World\n
 *
 * With FormattingSink and automatic flushing:
 *   OStreamSink coutSink(std::cout, true);
 *   FormattingSink formatted(coutSink, myFormatter);
 *   config.bind<InfoTag>(&formatted);
 *
 *   NOVA_LOG_TRUNC(InfoTag) << "Hello";
 *   // Output: [2025-01-11 10:30:45] INFO: Hello\n
 */
class OStreamSink final : public kmac::nova::Sink
{
private:
	std::ostream* _stream;  ///< output stream (not owned, must remain valid)
	bool _flushOnWrite;     ///< true for automatic stream flush after write

public:
	/**
	 * @brief Construct sink with output stream.
	 *
	 * @param stream output stream (must remain valid during sink lifetime)
	 * @param flushOnWrite true to flush output stream on each write
	 *
	 * @note sink does not own stream (caller manages lifetime)
	 * @note stream should support write()
	 */
	explicit OStreamSink( std::ostream& stream, bool flushOnWrite = false ) noexcept;

	/**
	 * @brief Write raw log message to stream.
	 *
	 * Output format: raw message bytes (no newline, no formatting)
	 *
	 * @param record Log record to output
	 *
	 * @note does NOT add newline
	 * @note flush output stream only if initialized to do so
	 * @note not thread-safe (use SynchronizedSink wrapper)
	 */
	void process( const kmac::nova::Record& record ) noexcept override;
};

} // namespace kmac::nova::extras

#endif // KMAC_NOVA_EXTRAS_OSTREAM_SINK_H
