#pragma once
#ifndef KMAC_NOVA_EXTRAS_SYSLOG_SINK_H
#define KMAC_NOVA_EXTRAS_SYSLOG_SINK_H

#ifdef __linux__

/**
 * @file syslog_sink.h
 * @brief Linux syslog sink for Nova.
 *
 * ⚠️  LINUX ONLY - requires <syslog.h>
 * ⚠️  NOT SUITABLE FOR REAL-TIME OR SAFETY-CRITICAL USE
 *
 * Provides SyslogSink, which writes log records to the system syslog daemon
 * via syslog(3).  Two construction modes are supported:
 *
 * Fixed priority:
 *   A single facility + severity value used for all records.  Suitable when
 *   all records bound to this sink share the same importance.
 *
 *   SyslogSink<> sink( LOG_LOCAL0 | LOG_INFO );
 *
 * Dynamic priority:
 *   A function pointer that receives the record's tagId and returns the
 *   facility + severity value to use.  Suitable when different tags should
 *   map to different severities.  A captureless lambda is the natural way
 *   to express the mapping:
 *
 *   SyslogSink<> sink( []( std::uint64_t tagId ) -> int
 *   {
 *       switch ( tagId )
 *       {
 *       case kmac::nova::LoggerTraits< NetworkErrorTag >::tagId:
 *           return LOG_LOCAL0 | LOG_ERR;
 *       case kmac::nova::LoggerTraits< UiWarningTag >::tagId:
 *           return LOG_LOCAL0 | LOG_WARNING;
 *       default:
 *           return LOG_LOCAL0 | LOG_INFO;
 *       }
 *   } );
 *
 * Formatting:
 *   SyslogSink writes record.message directly.  The syslog daemon
 *   automatically adds timestamp, hostname, program name, and PID, so no
 *   additional metadata is included by default.  To include file/line/function/
 *   tag, information, wrap the sink with FormattingSink:
 *
 *   SyslogSink<> syslogSink( LOG_LOCAL0 | LOG_DEBUG );
 *   FormattingSink< SyslogSink<> > sink( syslogSink, customFormatter );
 *
 * openlog / closelog:
 *   SyslogSink does not call openlog() or closelog().  These affect
 *   process-global state and must be managed by the application.  Call
 *   openlog() before binding the sink if a custom ident or options are
 *   needed.  If openlog() is not called, syslog() uses the process name
 *   automatically.
 *
 * Thread safety:
 *   syslog(3) is thread-safe on Linux (glibc uses an internal lock), so
 *   SyslogSink does not require SynchronizedSink wrapping for thread safety.
 *
 * Usage:
 *   // in application startup (optional):
 *   openlog( "myapp", LOG_PID | LOG_NDELAY, LOG_LOCAL0 );
 *
 *   // fixed priority:
 *   SyslogSink<> sink( LOG_LOCAL0 | LOG_INFO );
 *   ScopedConfigurator config;
 *   config.bind< InfoTag >( &sink );
 *
 *   NOVA_LOG( InfoTag ) << "Starting up";
 *   // syslog output: Jan 15 13:45:30 hostname myapp[1234]: Starting up
 *
 *   // in application shutdown (optional):
 *   closelog();
 *
 * Refer to the syslog documentation for severity levels and facilities.
 */

#include <kmac/nova/record.h>
#include <kmac/nova/sink.h>
#include <kmac/nova/platform/array.h>
#include <kmac/nova/platform/config.h>

#include <syslog.h>

#include <cstdint>
#include <cstring>

namespace kmac {
namespace nova {
namespace extras {

/**
 * @brief Sink that writes log records to the Linux syslog daemon.
 *
 * See file-level documentation for usage, formatting, and openlog guidance.
 *
 * @tparam BufferSize max size of buffer to copy record message into for null termination
 */
template< std::size_t BufferSize = 1024UL >
class SyslogSink final : public kmac::nova::Sink
{
	static_assert( BufferSize >= 16, "BufferSize must be at least 16 bytes" );
	static_assert( BufferSize <= 65536, "BufferSize must not exceed 64KB (stack safety)" );

public:
	using PriorityFunc = int (*)( std::uint64_t tagId );

private:
	PriorityFunc _priorityFunc;  ///< returns facility|severity for a given tagId; null if fixed
	int _fixedPriority;          ///< used when _priorityFunc is null

public:
	/**
	 * @brief Construct with a fixed facility+severity for all records.
	 *
	 * @param priority facility|severity value (e.g. LOG_LOCAL0 | LOG_INFO)
	 */
	explicit SyslogSink( int priority ) noexcept;

	/**
	 * @brief Construct with a tag-to-priority mapping function.
	 *
	 * The function receives the record's tagId and returns the
	 * facility|severity value to pass to syslog().  A captureless lambda
	 * converts implicitly to a function pointer.
	 *
	 * @param priorityFunc function mapping tagId to facility|severity
	 */
	explicit SyslogSink( PriorityFunc priorityFunc ) noexcept;

	/**
	 * @brief Write log record to syslog.
	 *
	 * Passes record.message to syslog() with the priority determined at
	 * construction.  The message is null-terminated before passing since
	 * Nova records are not guaranteed to be null-terminated.
	 *
	 * @param record log record to write
	 */
	void process( const kmac::nova::Record& record ) noexcept override;
};

template< std::size_t BufferSize >
SyslogSink< BufferSize >::SyslogSink( int priority ) noexcept
	: _priorityFunc( nullptr )
	, _fixedPriority( priority )
{
}

template< std::size_t BufferSize >
SyslogSink< BufferSize >::SyslogSink( PriorityFunc priorityFunc ) noexcept
	: _priorityFunc( priorityFunc )
	, _fixedPriority( 0 )
{
	NOVA_ASSERT( priorityFunc != nullptr && "SyslogSink: priorityFunc must not be null" );
}

template< std::size_t BufferSize >
void SyslogSink< BufferSize >::process( const kmac::nova::Record& record ) noexcept
{
	const int priority = _priorityFunc != nullptr
		? _priorityFunc( record.tagId )
		: _fixedPriority;

	// copy record.message into local buffer to ensure message is null-terminated
	kmac::nova::platform::Array< char, BufferSize > buf {};
	const std::size_t len = record.messageSize < BufferSize - 1 ? record.messageSize : BufferSize - 1;
	std::memcpy( buf.data(), record.message, len );
	buf[ len ] = '\0';
	syslog( priority, "%s", buf.data() );
}

} // namespace extras
} // namespace nova
} // namespace kmac

#endif // __linux__

#endif // KMAC_NOVA_EXTRAS_SYSLOG_SINK_H
