#pragma once
#ifndef KMAC_NOVA_EXTRAS_BUILDER_STREAM_QT_H
#define KMAC_NOVA_EXTRAS_BUILDER_STREAM_QT_H

#ifdef QT_VERSION

/**
 * @file builder_stream_qt.h
 * @brief Logging support for Qt types.
 *
 * Provides operator<< overloads for TruncatingRecordBuilder and
 * ContinuationRecordBuilder for commonly used Qt types.  (Note that
 * StreamingRecordBuilder is not supported.)
 *
 * Supported types:
 * - QStringView (covers implicit conversion from QString)
 * - QByteArray
 * - QLatin1StringView (Qt6) / QLatin1String (Qt5)
 * - QPoint, QPointF
 * - QSize, QSizeF
 * - QRect, QRectF
 *
 * String format:    UTF-8 encoded text
 * QPoint format:    (x, y)
 * QSize format:     WxH
 * QRect format:     (x, y WxH)
 *
 * QString is handled implicitly via QStringView - Qt provides an implicit
 * conversion from QString to QStringView, so no separate overload is needed.
 *
 * Usage:
 *   #include <kmac/nova/nova.h>
 *   #include <kmac/nova/extras/continuation_logging.h>  // if using NOVA_LOG_CONT
 *   #include <kmac/nova/extras/builder_stream_qt.h>
 *
 *   QString name = "Alice";
 *   QSize size( 1920, 1080 );
 *   NOVA_LOG( MyTag ) << "user: " << name << " resolution: " << size;
 *   // output: user: Alice resolution: 1920x1080
 *
 * Custom Qt types:
 *   To log your own Qt type, define operator<< for it in the global namespace
 *   (since Qt types are unnamespaced) or alongside your type in its namespace:
 *
 *   template< std::size_t N >
 *   kmac::nova::TruncatingRecordBuilder< N >& operator<<(
 *       kmac::nova::TruncatingRecordBuilder< N >& builder,
 *       const MyQtType& value )
 *   {
 *       return builder << value.toString();
 *   }
 *
 *   template< std::size_t N >
 *   kmac::nova::extras::ContinuationRecordBuilder< N >& operator<<(
 *       kmac::nova::extras::ContinuationRecordBuilder< N >& builder,
 *       const MyQtType& value )
 *   {
 *       return builder << value.toString();
 *   }
 */

#include <kmac/nova/truncating_logging.h>
#include <kmac/nova/platform/string_view.h>
#include <kmac/nova/extras/continuation_logging.h>

#include <QByteArray>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringView>

#if QT_VERSION >= QT_VERSION_CHECK( 5, 10, 0 )
#include <QLatin1String>
#endif

// ============================================================================
// Helper macros - undefined at end of file
// ============================================================================

// Expands both TruncatingRecordBuilder and ContinuationRecordBuilder overloads
// for a given signature and body, avoiding repetition for each supported type.
#define NOVA_QT_OVERLOADS( Signature, Body ) /* NOLINT(cppcoreguidelines-macro-usage, bugprone-macro-parentheses) */ \
template< std::size_t N > \
	kmac::nova::TruncatingRecordBuilder< N >& operator<<( \
		kmac::nova::TruncatingRecordBuilder< N >& builder, Signature ) Body \
	template< std::size_t N > \
	kmac::nova::extras::ContinuationRecordBuilder< N >& operator<<( \
		kmac::nova::extras::ContinuationRecordBuilder< N >& builder, Signature ) Body

// ============================================================================
// QStringView
// ============================================================================
//
// Handles QStringView directly and QString implicitly via Qt's built-in
// implicit conversion from QString to QStringView (Qt 5.10+).

NOVA_QT_OVERLOADS(
	QStringView value,
	{
		const QByteArray utf8 = value.toUtf8();
		return builder << kmac::nova::platform::StringView( utf8.constData(), static_cast< std::size_t >( utf8.size() ) );
	}
	)

// ============================================================================
// QByteArray
// ============================================================================

NOVA_QT_OVERLOADS(
	const QByteArray& value,
	{
		return builder << kmac::nova::platform::StringView( value.constData(), static_cast< std::size_t >( value.size() ) );
	}
	)

// ============================================================================
// QLatin1StringView / QLatin1String
// ============================================================================

#if QT_VERSION >= QT_VERSION_CHECK( 6, 0, 0 )

NOVA_QT_OVERLOADS(
	QLatin1StringView value,
	{
		return builder << kmac::nova::platform::StringView( value.data(), static_cast< std::size_t >( value.size() ) );
	}
	)

#elif QT_VERSION >= QT_VERSION_CHECK( 5, 10, 0 )

NOVA_QT_OVERLOADS(
	QLatin1String value,
	{
		return builder << kmac::nova::platform::StringView( value.data(), static_cast< std::size_t >( value.size() ) );
	}
	)

#endif

// ============================================================================
// QPoint
// ============================================================================
//
// Format: (x, y)

NOVA_QT_OVERLOADS(
	const QPoint& value,
	{
		return builder << '(' << value.x() << ", " << value.y() << ')';
	}
	)

// ============================================================================
// QPointF
// ============================================================================
//
// Format: (x, y)

NOVA_QT_OVERLOADS(
	const QPointF& value,
	{
		return builder << '(' << value.x() << ", " << value.y() << ')';
	}
	)

// ============================================================================
// QSize
// ============================================================================
//
// Format: WxH

NOVA_QT_OVERLOADS(
	const QSize& value,
	{
		return builder << value.width() << 'x' << value.height();
	}
	)

// ============================================================================
// QSizeF
// ============================================================================
//
// Format: WxH

NOVA_QT_OVERLOADS(
	const QSizeF& value,
	{
		return builder << value.width() << 'x' << value.height();
	}
	)

// ============================================================================
// QRect
// ============================================================================
//
// Format: (x, y WxH)

NOVA_QT_OVERLOADS(
	const QRect& value,
	{
		return builder << '(' << value.x() << ", " << value.y()
		<< ' ' << value.width() << 'x' << value.height() << ')';
	}
	)

// ============================================================================
// QRectF
// ============================================================================
//
// Format: (x, y WxH)

NOVA_QT_OVERLOADS(
	const QRectF& value,
	{
		return builder << '(' << value.x() << ", " << value.y()
		<< ' ' << value.width() << 'x' << value.height() << ')';
	}
	)

#undef NOVA_QT_OVERLOADS

#endif // QT_VERSION

#endif // KMAC_NOVA_EXTRAS_BUILDER_STREAM_QT_H
