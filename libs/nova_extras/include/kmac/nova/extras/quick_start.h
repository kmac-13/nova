#pragma once
#ifndef KMAC_NOVA_EXTRAS_QUICK_START_H
#define KMAC_NOVA_EXTRAS_QUICK_START_H

/**
 * @file quick_start.h
 * @brief Zero-configuration console logging for Nova.
 *
 * ⚠️  NOT SUITABLE FOR REAL-TIME OR SAFETY-CRITICAL USE
 * ⚠️  HOSTED PLATFORMS ONLY - depends on std::cout and std::mutex
 *
 * QuickStart wires up a complete console logging pipeline for all six
 * severity tags (Trace through Fatal) with a single declaration.  It is
 * intended for getting logging working immediately in examples, prototypes,
 * and early development as well as supporting a severity-based approach to
 * logging that many developers are familiar with.  Additionally, the
 * severity tags are always enabled, so this approach relies on runtime
 * disabling of the severities through the lack of binding a sink, i.e.
 * log calls for tags with no bound sink are silently dropped at runtime - there's
 * no compile-time stripping regardless of the severity threshold chosen.
 *
 * This is not intended for production use.  For production use, define
 * your own domains (tags) and construct the individual components directly
 * so that buffer sizes, threading strategy, and sink lifetime are under
 * explicit control.
 *
 * Pipeline (owned entirely by QuickStart):
 *
 *   NOVA_LOG_INFO() << "..."
 *          |
 *   SynchronizedSink    - serialises concurrent log calls with a mutex
 *          |
 *   FormattingSink<>    - applies ISO 8601 formatting
 *          |
 *   ISO8601Formatter    - "2025-02-07T12:34:56.789Z [INFO] file.cpp:42 fn - msg\n"
 *          |
 *   OStreamSink         - writes to std::cout (or a user-supplied stream)
 *
 * All severity tags are bound via a ScopedConfigurator.  Destruction of
 * QuickStart unbinds all tags and tears down the pipeline in safe order.
 *
 * Usage - default (all severities, std::cout):
 *
 *   #include <kmac/nova/extras/quick_start.h>
 *
 *   int main()
 *   {
 *       kmac::nova::extras::QuickStart logging;
 *       NOVA_LOG_INFO()  << "started";
 *       NOVA_LOG_WARN()  << "something looks off";
 *       NOVA_LOG_ERROR() << "something went wrong";
 *   }
 *
 * Usage - minimum severity threshold (Info and above):
 *
 *   kmac::nova::extras::QuickStart logging( kmac::nova::extras::QuickStart::Severity::Info );
 *
 * Usage - custom output stream (e.g. std::cerr):
 *
 *   kmac::nova::extras::QuickStart logging( std::cerr );
 *
 * Usage - custom stream with severity threshold:
 *
 *   kmac::nova::extras::QuickStart logging(
 *       std::cerr,
 *       kmac::nova::extras::QuickStart::Severity::Warning
 *   );
 *
 * Severity thresholds:
 *   Severity::Trace   - binds all six tags (most verbose)
 *   Severity::Debug   - binds Debug through Fatal
 *   Severity::Info    - binds Info through Fatal (default)
 *   Severity::Warning - binds Warning through Fatal
 *   Severity::Error   - binds Error and Fatal only
 *   Severity::Fatal   - binds Fatal only
 *
 * Thread safety:
 *   SynchronizedSink serialises concurrent writes.  Constructing and
 *   destructing QuickStart must be done on a single thread - do not
 *   log on other threads while QuickStart is being constructed or destroyed.
 *
 * Lifetime:
 *   QuickStart is an RAII type.  All severity tags are unbound when it goes
 *   out of scope.  For single-threaded applications, any logging performed
 *   after QuickStart has been destroyed will be safely ignored.  For
 *   multi-threaded applications, however, either the pipeline must outlive all
 *   log calls or the user must provide manual synchronization; otherwise, there
 *   is a risk of dereferencing an invalid sink pointer.
 */

#include "formatting_sink.h"
#include "iso8601_formatter.h"
#include "ostream_sink.h"
#include "severities.h"
#include "synchronized_sink.h"

#include <kmac/nova/immovable.h>
#include <kmac/nova/scoped_configurator.h>

#include <iostream>

namespace kmac {
namespace nova {
namespace extras {

/**
 * @brief Zero-configuration RAII console logging pipeline.
 *
 * Owns and manages a complete sink chain for all six severity tags.
 * See file-level documentation for usage and pipeline details.
 */
class QuickStart : private kmac::nova::Immovable
{
public:
	/**
	 * @brief Minimum severity level to bind.
	 *
	 * Tags below the threshold are not bound - they produce no output and
	 * incur no runtime cost beyond the unbound logger check.
	 */
	enum class Severity
	{
		Trace,    ///< bind all six tags (most verbose)
		Debug,    ///< bind Debug through Fatal
		Info,     ///< bind Info through Fatal (default)
		Warning,  ///< bind Warning through Fatal
		Error,    ///< bind Error and Fatal only
		Fatal     ///< bind Fatal only
	};

private:
	// declaration order determines construction and (reverse) destruction order,
	// which means _ostreamSink must be constructed first and destroyed last since
	// all downstream members hold a reference to it transitively
	OStreamSink _ostreamSink;
	ISO8601Formatter _formatter;
	FormattingSink<> _formattingSink;
	SynchronizedSink _synchronizedSink;
	ScopedConfigurator< 6 > _configurator;

public:
	/**
	 * @brief Construct with default stream (std::cout) and severity threshold.
	 *
	 * @param minSeverity minimum severity to bind (default: Info)
	 */
	explicit QuickStart( Severity minSeverity = Severity::Info ) noexcept;

	/**
	 * @brief Construct with a custom output stream and severity threshold.
	 *
	 * @param stream output stream to write to (must outlive QuickStart)
	 * @param minSeverity minimum severity to bind (default: Info)
	 */
	explicit QuickStart( std::ostream& stream, Severity minSeverity = Severity::Info ) noexcept;

private:
	/**
	 * @brief Bind severity tags at or above minSeverity to the pipeline.
	 *
	 * Extracted from the constructors to avoid duplication.
	 *
	 * @param minSeverity minimum severity level to bind
	 */
	void bindSeverities( Severity minSeverity ) noexcept;
};

} // namespace extras
} // namespace nova
} // namespace kmac

#endif // KMAC_NOVA_EXTRAS_QUICK_START_H
