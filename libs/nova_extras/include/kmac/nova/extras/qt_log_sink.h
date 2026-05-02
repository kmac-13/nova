#pragma once
#ifndef KMAC_NOVA_EXTRAS_QT_LOG_SINK_H
#define KMAC_NOVA_EXTRAS_QT_LOG_SINK_H

#ifdef __has_include
#if __has_include( <QtCore/QtGlobal> )
#include <QtCore/QtGlobal>
#endif  // QtCore/QtGlobal
#endif  // __has_include

#ifdef QT_VERSION

/**
 * @file qt_log_sink.h
 * @brief Qt logging sink for Nova.
 *
 * ⚠️  QT ONLY - requires Qt core
 * ⚠️  NOT SUITABLE FOR REAL-TIME OR SAFETY-CRITICAL USE
 *
 * Provides QtLogSink, which writes log records to Qt's logging system via
 * QMessageLogger, making them visible to any Qt log handler (e.g. logcat on
 * Android, the system log on macOS/iOS, or a custom qInstallMessageHandler).
 * Two construction modes are supported, each with an optional QLoggingCategory:
 *
 * Fixed message type:
 *   A single QtMsgType used for all records.
 *
 *   QtLogSink<> sink( QtInfoMsg );
 *
 * Dynamic message type:
 *   A function pointer that receives the record's tagId and returns the
 *   QtMsgType to use.  A captureless lambda is a natural way to express
 *   the mapping:
 *
 *   QtLogSink<> sink( []( std::uint64_t tagId ) -> QtMsgType
 *   {
 *       switch ( tagId )
 *       {
 *       case kmac::nova::LoggerTraits< NetworkErrorTag >::tagId:
 *           return QtCriticalMsg;
 *       case kmac::nova::LoggerTraits< UiWarningTag >::tagId:
 *           return QtWarningMsg;
 *       default:
 *           return QtInfoMsg;
 *       }
 *   } );
 *
 * Optional QLoggingCategory:
 *   Either constructor accepts an optional QLoggingCategory pointer.  When
 *   provided, the sink uses the category-aware QMessageLogger overloads,
 *   allowing runtime filtering via QT_LOGGING_RULES or qInstallMessageHandler.
 *   If the category is disabled at runtime, the message is suppressed by Qt
 *   automatically.  The category pointer must remain valid for the lifetime
 *   of the sink.
 *
 *   Q_LOGGING_CATEGORY( networkLog, "myapp.network" )
 *
 *   // fixed message type with category:
 *   QtLogSink<> sink( QtDebugMsg, &networkLog );
 *
 *   // dynamic message type with category:
 *   QtLogSink<> sink( []( std::uint64_t tagId ) -> QtMsgType { ... }, &networkLog );
 *
 * Source location:
 *   QtLogSink uses QMessageLogger with Nova's record.file, record.line, and
 *   record.function, so Qt log output shows the original log call site rather
 *   than the sink's process() method.
 *
 * Formatting:
 *   QtLogSink writes record.message directly.  Qt's log handler adds its own
 *   metadata (timestamp, category, type prefix) depending on the handler in
 *   use.  To include additional Nova metadata (e.g. tag name), wrap the sink
 *   with FormattingSink:
 *
 *   QtLogSink<> qtSink( QtDebugMsg );
 *   FormattingSink< QtLogSink<> > sink( qtSink, customFormatter );
 *
 * Thread safety:
 *   Qt's logging functions are thread-safe, so QtLogSink does not require
 *   SynchronizedSink wrapping for thread safety.
 *
 * Qt message type levels (lowest to highest):
 *   QtDebugMsg    - debug (filtered out in release builds by default)
 *   QtInfoMsg     - informational
 *   QtWarningMsg  - warning
 *   QtCriticalMsg - critical error
 *   QtFatalMsg    - fatal (aborts the program)
 *
 * Qt categories vs Nova domains:
 *   Nova domains are compile-time types that route and filter at compile time
 *   with zero overhead for disabled domains.  Qt categories are runtime objects
 *   that filter by named string, configurable externally via QT_LOGGING_RULES
 *   without recompiling.  They complement each other: use Nova domains for
 *   compile-time routing, Qt categories for runtime filtering by operators or
 *   end users.
 */

#include <kmac/nova/record.h>
#include <kmac/nova/sink.h>
#include <kmac/nova/platform/array.h>
#include <kmac/nova/platform/config.h>

#include <QLoggingCategory>
#include <QMessageLogger>

#include <cstdint>
#include <cstring>

namespace kmac {
namespace nova {
namespace extras {

/**
 * @brief Sink that writes log records to Qt's logging system.
 *
 * See file-level documentation for usage, categories, and formatting guidance.
 *
 * @tparam BufferSize max size of buffer to copy record message into for null
 *     termination (default 1024 bytes, matching Nova's default builder size)
 */
template< std::size_t BufferSize = 1024 >
class QtLogSink final : public kmac::nova::Sink
{
	static_assert( BufferSize >= 16, "BufferSize must be at least 16 bytes" );
	static_assert( BufferSize <= 65536, "BufferSize must not exceed 64KB (stack safety)" );

public:
	using PriorityFunc = QtMsgType (*)( std::uint64_t tagId );

private:
	PriorityFunc _priorityFunc;         ///< returns QtMsgType for a given tagId; null if fixed
	QtMsgType _fixedMsgType;            ///< used when _priorityFunc is null
	const QLoggingCategory* _category;  ///< optional category for runtime filtering; null if none

	using LogMethod = void ( QtLogSink::* )( const QMessageLogger&, QtMsgType, const char* ) const noexcept;
	LogMethod _logMethod;  ///< points to logWithCategory or logWithoutCategory

public:
	/**
	 * @brief Construct with a fixed QtMsgType for all records.
	 *
	 * @param msgType Qt message type (e.g. QtInfoMsg)
	 * @param category optional logging category for runtime filtering; null for none
	 */
	explicit QtLogSink( QtMsgType msgType, const QLoggingCategory* category = nullptr ) noexcept;

	/**
	 * @brief Construct with a tag-to-message-type mapping function.
	 *
	 * The function receives the record's tagId and returns the QtMsgType to
	 * use.  A captureless lambda converts implicitly to a function pointer.
	 *
	 * @param priorityFunc function mapping tagId to QtMsgType
	 * @param category optional logging category for runtime filtering; null for none
	 */
	explicit QtLogSink( PriorityFunc priorityFunc, const QLoggingCategory* category = nullptr ) noexcept;

	/**
	 * @brief Write log record to Qt's logging system.
	 *
	 * Uses QMessageLogger with the record's source location so that Qt log
	 * output shows the original call site.  The message is null-terminated
	 * before passing since Nova records are not guaranteed to be null-terminated.
	 *
	 * @param record log record to write
	 */
	void process( const kmac::nova::Record& record ) noexcept override;

private:
	/**
	 * @brief Dispatch a null-terminated message to the correct QMessageLogger
	 * overload based on msgType with the given category.
	 *
	 * @param logger QMessageLogger constructed with the record's source location
	 * @param msgType resolved Qt message type for this record
	 * @param msg null-terminated message string
	 *
	 * @note There is no QMessageLogger::fatal overload that accepts a category;
	 *   fatal messages are always emitted regardless of category enabled state.
	 */
	void logWithCategory( const QMessageLogger& logger, QtMsgType msgType, const char* msg ) const noexcept;

	/**
	 * @brief Dispatch a null-terminated message to the correct QMessageLogger
	 * overload based on msgType (no category).
	 *
	 * @param logger QMessageLogger constructed with the record's source location
	 * @param msgType resolved Qt message type for this record
	 * @param msg null-terminated message string
	 */
	void logWithoutCategory( const QMessageLogger& logger, QtMsgType msgType, const char* msg ) const noexcept;
};

template< std::size_t BufferSize >
QtLogSink< BufferSize >::QtLogSink( QtMsgType msgType, const QLoggingCategory* category ) noexcept
	: _priorityFunc( nullptr )
	, _fixedMsgType( msgType )
	, _category( category )
	, _logMethod( category != nullptr ? &QtLogSink::logWithCategory : &QtLogSink::logWithoutCategory )
{
}

template< std::size_t BufferSize >
QtLogSink< BufferSize >::QtLogSink( PriorityFunc priorityFunc, const QLoggingCategory* category ) noexcept
	: _priorityFunc( priorityFunc )
	, _fixedMsgType( QtDebugMsg )
	, _category( category )
	, _logMethod( category != nullptr ? &QtLogSink::logWithCategory : &QtLogSink::logWithoutCategory )
{
	NOVA_ASSERT( priorityFunc != nullptr && "QtLogSink: priorityFunc must not be null" );
}

template< std::size_t BufferSize >
void QtLogSink< BufferSize >::process( const kmac::nova::Record& record ) noexcept
{
	const QtMsgType msgType = _priorityFunc != nullptr
		? _priorityFunc( record.tagId )
		: _fixedMsgType;

	// copy record.message into local buffer to ensure message is null-terminated
	kmac::nova::platform::Array< char, BufferSize > buf {};
	const std::size_t len = record.messageSize < BufferSize - 1 ? record.messageSize : BufferSize - 1;
	std::memcpy( buf.data(), record.message, len );
	buf[ len ] = '\0';

	// use record's source location so Qt output shows the original call site
	const QMessageLogger logger( record.file, static_cast< int >( record.line ), record.function );
	( this->*_logMethod )( logger, msgType, buf.data() );
}

template< std::size_t BufferSize >
void QtLogSink< BufferSize >::logWithCategory( const QMessageLogger& logger, QtMsgType msgType, const char* msg ) const noexcept
{
	switch ( msgType )
	{
	case QtDebugMsg:
		logger.debug( *_category, "%s", msg );
		break;

	case QtInfoMsg:
		logger.info( *_category, "%s", msg );
		break;

	case QtWarningMsg:
		logger.warning( *_category, "%s", msg );
		break;

	case QtCriticalMsg:
		logger.critical( *_category, "%s", msg );
		break;

	case QtFatalMsg:
		logger.fatal( "%s", msg );  // Qt provides no category overload for fatal
		break;

	default:
		logger.debug( *_category, "%s", msg );
		break;
	}
}

template< std::size_t BufferSize >
void QtLogSink< BufferSize >::logWithoutCategory( const QMessageLogger& logger, QtMsgType msgType, const char* msg ) const noexcept
{
	switch ( msgType )
	{
	case QtDebugMsg:
		logger.debug( "%s", msg );
		break;

	case QtInfoMsg:
		logger.info( "%s", msg );
		break;

	case QtWarningMsg:
		logger.warning( "%s", msg );
		break;

	case QtCriticalMsg:
		logger.critical( "%s", msg );
		break;

	case QtFatalMsg:
		logger.fatal( "%s", msg );
		break;

	default:
		logger.debug( "%s", msg );
		break;
	}
}

} // namespace extras
} // namespace nova
} // namespace kmac

#endif // QT_VERSION

#endif // KMAC_NOVA_EXTRAS_QT_LOG_SINK_H
