#pragma once
#ifndef KMAC_NOVA_EXTRAS_SEVERITIES_H
#define KMAC_NOVA_EXTRAS_SEVERITIES_H

#include <kmac/nova/nova.h>

namespace kmac {
namespace nova {
namespace extras {

/**
 * @brief Predefined severity level tags and convenience macros.
 *
 * ✅ SAFE FOR SAFETY-CRITICAL SYSTEMS
 *
 * Provides example standard logging severity levels (TRACE through FATAL)
 * with corresponding tag types and shortened macro names.
 *
 * Severity Levels:
 * - TRACE: detailed diagnostic information (disabled by default in production)
 * - DEBUG: developer-focused debugging information
 * - INFO:  general informational messages
 * - WARNING: warning messages for potentially problematic situations
 * - ERROR: error messages for recoverable failures
 * - FATAL: fatal error messages for unrecoverable failures
 *
 * Usage with full builder macros:
 *   NOVA_LOG(TraceTag) << "Detailed trace";
 *   NOVA_LOG_CONT(DebugTag) << "Debug info";
 *   NOVA_LOG_STREAM(ErrorTag) << "Error: " << code;
 *
 * Usage with convenience macros (TRUNC builder):
 *   NOVA_LOG_TRACE() << "Trace message";
 *   NOVA_LOG_DEBUG() << "Debug message";
 *   NOVA_LOG_INFO() << "Info message";
 *   NOVA_LOG_WARN() << "Warning message";
 *   NOVA_LOG_ERROR() << "Error message";
 *   NOVA_LOG_FATAL() << "Fatal message";
 *
 * Note: Convenience macros use TRUNC builder for performance.
 * For longer messages, use explicit NOVA_LOG_CONT or NOVA_LOG_STREAM.
 */

/**
 * @brief Trace severity tag (most verbose).
 */
struct TraceTag { };

/**
 * @brief Debug severity tag.
 */
struct DebugTag { };

/**
 * @brief Info severity tag.
 */
struct InfoTag { };

/**
 * @brief Warning severity tag.
 */
struct WarningTag { };

/**
 * @brief Error severity tag.
 */
struct ErrorTag { };

/**
 * @brief Fatal severity tag (most severe).
 */
struct FatalTag { };

} // namespace extras
} // namespace nova
} // namespace kmac

// configure tag traits for each severity level
NOVA_LOGGER_TRAITS( ::kmac::nova::extras::TraceTag, TRACE, true, ::kmac::nova::TimestampHelper::systemNanosecs );
NOVA_LOGGER_TRAITS( ::kmac::nova::extras::DebugTag, DEBUG, true, ::kmac::nova::TimestampHelper::systemNanosecs );
NOVA_LOGGER_TRAITS( ::kmac::nova::extras::InfoTag, INFO, true, ::kmac::nova::TimestampHelper::systemNanosecs );
NOVA_LOGGER_TRAITS( ::kmac::nova::extras::WarningTag, WARNING, true, ::kmac::nova::TimestampHelper::systemNanosecs );
NOVA_LOGGER_TRAITS( ::kmac::nova::extras::ErrorTag, ERROR, true, ::kmac::nova::TimestampHelper::systemNanosecs );
NOVA_LOGGER_TRAITS( ::kmac::nova::extras::FatalTag, FATAL, true, ::kmac::nova::TimestampHelper::systemNanosecs );

/**
 * @brief Shortened logging macros (using truncation builder).
 *
 * These macros provide convenient shortened names for common severity levels.
 * All use the truncating record builder for maximum performance.
 *
 * @note These are convenience macros. For explicit control over record builder
 *       type, use NOVA_LOG, NOVA_LOG_CONT, or NOVA_LOG_STREAM macros directly.
 */

/// Trace level logging
#define NOVA_LOG_TRACE() /* NOLINT(cppcoreguidelines-macro-usage) */ \
	NOVA_LOG( kmac::nova::extras::TraceTag )

/// Debug level logging
#define NOVA_LOG_DEBUG() /* NOLINT(cppcoreguidelines-macro-usage) */ \
	NOVA_LOG( kmac::nova::extras::DebugTag )

/// Info level logging
#define NOVA_LOG_INFO() /* NOLINT(cppcoreguidelines-macro-usage) */ \
	NOVA_LOG( kmac::nova::extras::InfoTag )

/// Warning level logging
#define NOVA_LOG_WARN() /* NOLINT(cppcoreguidelines-macro-usage) */ \
	NOVA_LOG( kmac::nova::extras::WarningTag )

/// Error level logging
#define NOVA_LOG_ERROR() /* NOLINT(cppcoreguidelines-macro-usage) */ \
	NOVA_LOG( kmac::nova::extras::ErrorTag )

/// Fatal level logging
#define NOVA_LOG_FATAL() /* NOLINT(cppcoreguidelines-macro-usage) */ \
	NOVA_LOG( kmac::nova::extras::FatalTag )

#endif // KMAC_NOVA_EXTRAS_SEVERITIES_H
