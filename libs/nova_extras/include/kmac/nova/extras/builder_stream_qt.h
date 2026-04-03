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
 * - QVariant
 * - QUrl
 * - QDir
 * - QFileInfo
 * - QDateTime, QDate, QTime
 * - QPoint, QPointF
 * - QSize, QSizeF
 * - QRect, QRectF
 *
 * String format:      UTF-8 encoded text
 * QVariant format:    toString() value, or <TypeName> if not string-convertible
 * QUrl format:        full URL string
 * QDir format:        absolute path
 * QFileInfo format:   absolute file path
 * QDateTime format:   ISO 8601 with milliseconds (e.g. 2024-01-15T13:45:30.123)
 * QDate format:       ISO 8601 (e.g. 2024-01-15)
 * QTime format:       ISO 8601 with milliseconds (e.g. 13:45:30.123)
 * QPoint format:      (x, y)
 * QSize format:       WxH
 * QRect format:       (x, y WxH)
 *
 * Qt enums registered with Q_ENUM can be logged using the novaQEnum() helper:
 *
 *   NOVA_LOG( MyTag ) << novaQEnum( MyClass::SomeValue );
 *   // output: SomeValue
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
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMetaEnum>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QStringView>
#include <QUrl>
#include <QVariant>

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

// ============================================================================
// QVariant
// ============================================================================
//
// Format: toString() value if convertible, otherwise <TypeName>

NOVA_QT_OVERLOADS(
	const QVariant& value,
	{
		const QString str = value.toString();
#if QT_VERSION >= QT_VERSION_CHECK( 6, 0, 0 )
		if ( !str.isEmpty() || value.metaType() == QMetaType::fromType< QString >() )
#else
		if ( !str.isEmpty() || value.type() == QVariant::String )
#endif
		{
			return builder << QStringView( str );
		}
#if QT_VERSION >= QT_VERSION_CHECK( 6, 0, 0 )
		return builder << '<' << value.metaType().name() << '>';
#else
		return builder << '<' << value.typeName() << '>';
#endif
	}
)

// ============================================================================
// QUrl
// ============================================================================
//
// Format: full URL string

NOVA_QT_OVERLOADS(
	const QUrl& value,
	{
		const QString str = value.toString();
		return builder << QStringView( str );
	}
)

// ============================================================================
// QDir
// ============================================================================
//
// Format: absolute path

NOVA_QT_OVERLOADS(
	const QDir& value,
	{
		const QString str = value.absolutePath();
		return builder << QStringView( str );
	}
)

// ============================================================================
// QFileInfo
// ============================================================================
//
// Format: absolute file path

NOVA_QT_OVERLOADS(
	const QFileInfo& value,
	{
		const QString str = value.absoluteFilePath();
		return builder << QStringView( str );
	}
)

// ============================================================================
// QDateTime
// ============================================================================
//
// Format: ISO 8601 with milliseconds (e.g. 2024-01-15T13:45:30.123)

NOVA_QT_OVERLOADS(
	const QDateTime& value,
	{
		const QString str = value.toString( Qt::ISODateWithMs );
		return builder << QStringView( str );
	}
)

// ============================================================================
// QDate
// ============================================================================
//
// Format: ISO 8601 (e.g. 2024-01-15)

NOVA_QT_OVERLOADS(
	const QDate& value,
	{
		const QString str = value.toString( Qt::ISODate );
		return builder << QStringView( str );
	}
)

// ============================================================================
// QTime
// ============================================================================
//
// Format: ISO 8601 with milliseconds (e.g. 13:45:30.123)

NOVA_QT_OVERLOADS(
	const QTime& value,
	{
		const QString str = value.toString( Qt::ISODateWithMs );
		return builder << QStringView( str );
	}
)

#undef NOVA_QT_OVERLOADS

// ============================================================================
// novaQEnum
// ============================================================================
//
// Helper for logging Qt enums registered with Q_ENUM.  Returns the enum
// value's key name as a const char*, routing through the builder's existing
// const char* overload.  Returns nullptr if the value is not registered,
// which will log nothing.
//
// Usage:
//   NOVA_LOG( MyTag ) << novaQEnum( MyClass::SomeValue );
//   // output: SomeValue
//
// @tparam E enum type registered with Q_ENUM

template< typename E >
inline const char* novaQEnum( E value )
{
	return QMetaEnum::fromType< E >().valueToKey( static_cast< int >( value ) );
}

#endif // QT_VERSION

#endif // KMAC_NOVA_EXTRAS_BUILDER_STREAM_QT_H
