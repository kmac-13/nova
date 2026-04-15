#pragma once
#ifndef KMAC_NOVA_EXTRAS_ANDROID_LOG_SINK_H
#define KMAC_NOVA_EXTRAS_ANDROID_LOG_SINK_H

#ifdef __ANDROID__

/**
 * @file android_log_sink.h
 * @brief Android logcat sink for Nova.
 *
 * ⚠️  ANDROID ONLY - requires <android/log.h>
 * ⚠️  NOT SUITABLE FOR REAL-TIME OR SAFETY-CRITICAL USE
 *
 * Provides AndroidLogSink, which writes log records to the Android logging
 * system via __android_log_write(), making them visible in logcat.  Two
 * construction modes are supported:
 *
 * Fixed priority:
 *   A single Android log priority used for all records.  Suitable when all
 *   records bound to this sink share the same importance.
 *
 *   AndroidLogSink<> sink( "MyApp", ANDROID_LOG_INFO );
 *
 * Dynamic priority:
 *   A function pointer that receives the record's tagId and returns the
 *   Android log priority to use.  Suitable when different Nova tags should
 *   map to different logcat priorities.  A captureless lambda is a natural
 *   way to express the mapping:
 *
 *   AndroidLogSink<> sink( "MyApp", []( std::uint64_t tagId ) -> int
 *   {
 *       switch ( tagId )
 *       {
 *       case kmac::nova::logger_traits< NetworkErrorTag >::tagId:
 *           return ANDROID_LOG_ERROR;
 *       case kmac::nova::logger_traits< UiWarningTag >::tagId:
 *           return ANDROID_LOG_WARN;
 *       default:
 *           return ANDROID_LOG_INFO;
 *       }
 *   } );
 *
 * Android log tag:
 *   The first constructor argument is the logcat tag string - the component
 *   identifier shown in logcat output (e.g. "MyApp", "AudioEngine").  This
 *   is distinct from Nova's record.tag, which is used for routing and priority
 *   mapping.  The tag pointer must remain valid for the lifetime of the sink;
 *   string literals are the typical choice.
 *
 * Formatting:
 *   AndroidLogSink writes record.message directly.  The Android logging system
 *   automatically adds timestamp, PID, and TID, so no additional metadata is
 *   included by default.  To include file/line/function/tag information, wrap
 *   the sink with FormattingSink:
 *
 *   AndroidLogSink<> androidSink( "MyApp", ANDROID_LOG_DEBUG );
 *   FormattingSink< AndroidLogSink<> > sink( androidSink, customFormatter );
 *
 * Thread safety:
 *   __android_log_write() is thread-safe, so AndroidLogSink does not require
 *   SynchronizedSink wrapping for thread safety.
 *
 * Usage:
 *   // fixed priority, all bound domains log the same priority:
 *   AndroidLogSink<> sink( "MyApp", ANDROID_LOG_INFO );
 *   ScopedConfigurator config;
 *   config.bind< NetworkingTag >( &sink );
 *   config.bind< DatabaseTag >( &sink );
 *
 *   NOVA_LOG( NetworkingTag ) << "Connecting...";
 *   NOVA_LOG( DatabaseTag ) << "Opening DB...";
 *   // logcat output: I/MyApp: Connecting...
 *   // logcat output: I/MyApp: Opening DB...
 *
 * Android log priority levels (lowest to highest):
 *   ANDROID_LOG_VERBOSE  - verbose (filtered out in release builds)
 *   ANDROID_LOG_DEBUG    - debug
 *   ANDROID_LOG_INFO     - informational
 *   ANDROID_LOG_WARN     - warning
 *   ANDROID_LOG_ERROR    - error
 *   ANDROID_LOG_FATAL    - fatal
 *
 * NOTE:
 *   The Android logging system truncates messages exceeding approximately
 *   4096 bytes at runtime.  This limit is not enforced by the sink since
 *   it varies by Android version; use ContinuationRecordBuilder if complete
 *   delivery of long messages is required.
 */

#include <kmac/nova/record.h>
#include <kmac/nova/sink.h>
#include <kmac/nova/platform/array.h>
#include <kmac/nova/platform/config.h>

#include <android/log.h>

#include <cstdint>
#include <cstring>

namespace kmac {
namespace nova {
namespace extras {

/**
 * @brief Sink that writes log records to the Android logging system (logcat).
 *
 * See file-level documentation for usage and formatting guidance.
 *
 * @tparam BufferSize max size of buffer to copy record message into for null
 *     termination (default 1024 bytes, matching Nova's default builder size)
 */
template< std::size_t BufferSize = 1024 >
class AndroidLogSink final : public kmac::nova::Sink
{
	static_assert( BufferSize >= 16, "BufferSize must be at least 16 bytes" );
	static_assert( BufferSize <= 65536, "BufferSize must not exceed 64KB (stack safety)" );

public:
	using PriorityFunc = int (*)( std::uint64_t tagId );

private:
	const char* _androidTag;     ///< logcat tag string (not owned, must remain valid)
	PriorityFunc _priorityFunc;  ///< returns Android log priority for a given tagId; null if fixed
	int _fixedPriority;          ///< used when _priorityFunc is null

public:
	/**
	 * @brief Construct with a fixed Android log priority for all records.
	 *
	 * @param androidTag logcat tag string (must remain valid for sink lifetime)
	 * @param priority Android log priority (e.g. ANDROID_LOG_INFO)
	 */
	AndroidLogSink( const char* androidTag, int priority ) noexcept;

	/**
	 * @brief Construct with a tag-to-priority mapping function.
	 *
	 * The function receives the record's tagId and returns the Android log
	 * priority to use.  A captureless lambda converts implicitly to a function
	 * pointer.
	 *
	 * @param androidTag logcat tag string (must remain valid for sink lifetime)
	 * @param priorityFunc function mapping tagId to Android log priority
	 */
	AndroidLogSink( const char* androidTag, PriorityFunc priorityFunc ) noexcept;

	/**
	 * @brief Write log record to the Android logging system.
	 *
	 * Passes record.message to __android_log_write() with the priority
	 * determined at construction.  The message is null-terminated before
	 * passing since Nova records are not guaranteed to be null-terminated.
	 *
	 * @param record log record to write
	 */
	void process( const kmac::nova::Record& record ) noexcept override;
};

template< std::size_t BufferSize >
AndroidLogSink< BufferSize >::AndroidLogSink( const char* androidTag, int priority ) noexcept
	: _androidTag( androidTag )
	, _priorityFunc( nullptr )
	, _fixedPriority( priority )
{
}

template< std::size_t BufferSize >
AndroidLogSink< BufferSize >::AndroidLogSink( const char* androidTag, PriorityFunc priorityFunc ) noexcept
	: _androidTag( androidTag )
	, _priorityFunc( priorityFunc )
	, _fixedPriority( 0 )
{
	NOVA_ASSERT( priorityFunc != nullptr && "AndroidLogSink: priorityFunc must not be null" );
}

template< std::size_t BufferSize >
void AndroidLogSink< BufferSize >::process( const kmac::nova::Record& record ) noexcept
{
	const int priority = _priorityFunc != nullptr
		? _priorityFunc( record.tagId )
		: _fixedPriority;

	// copy record.message into local buffer to ensure message is null-terminated
	kmac::nova::platform::Array< char, BufferSize > buf {};
	const std::size_t len = record.messageSize < BufferSize - 1 ? record.messageSize : BufferSize - 1;
	std::memcpy( buf.data(), record.message, len );
	buf[ len ] = '\0';
	__android_log_write( priority, _androidTag, buf.data() );
}

} // namespace extras
} // namespace nova
} // namespace kmac

#endif // __ANDROID__

#endif // KMAC_NOVA_EXTRAS_ANDROID_LOG_SINK_H
