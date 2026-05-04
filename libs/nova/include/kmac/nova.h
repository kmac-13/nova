#pragma once
#ifndef KMAC_NOVA_H
#define KMAC_NOVA_H

/**
 * @file nova.h
 * @brief Umbrella header for Nova core.
 *
 * Including this header pulls in the complete Nova core public API.  For
 * fine-grained inclusion, include individual headers from <kmac/nova/> directly.
 *
 * Component overview:
 *
 *   platform/config.h    - platform detection and configuration macros: NOVA_BARE_METAL,
 *                          NOVA_HAS_TLS, NOVA_IF_CONSTEXPR, NOVA_HAS_STD_ATOMIC, etc.
 *                          included first to ensure a consistent configuration across
 *                          all translation units
 *
 *   macros.h             - NOVA_LOG, NOVA_LOG_STACK, NOVA_LOG_BUF, and related
 *                          logging macros; also defines NOVA_DEFAULT_BUFFER_SIZE
 *   logger_traits.h      - LoggerTraits<Tag> struct and NOVA_LOGGER_TRAITS macro;
 *                          controls the tag name, enabled flag, and timestamp source
 *   logger.h             - Logger<Tag>; the per-tag routing endpoint that holds the
 *                          atomic sink pointer
 *   record.h             - Record struct; the immutable log event passed to sinks
 *   sink.h               - Sink base class; implement process() to receive records
 *   scoped_configurator.h - ScopedConfigurator; RAII sink binding and unbinding
 *   timestamp_helper.h   - TimestampHelper::steadyNanosecs / systemNanosecs;
 *                          ready-made timestamp functions for use in LoggerTraits
 *   version.h            - NOVA_VERSION_MAJOR / MINOR / PATCH / STRING constants
 *
 * Not included here (include directly when needed):
 *
 *   <kmac/nova/performance_metrics.h>  - compile-time performance guarantee assertions
 *                                        (opt-in diagnostic / verification use)
 *
 * Typical usage:
 *
 *   #include <kmac/nova.h>
 *
 *   struct NetworkTag {};
 *   NOVA_LOGGER_TRAITS( NetworkTag, NETWORK, true, kmac::nova::TimestampHelper::steadyNanosecs );
 *
 *   // bind a sink (see Nova Extras for concrete sink implementations)
 *   kmac::nova::ScopedConfigurator config;
 *   config.bind< NetworkTag >( &mySink );
 *
 *   NOVA_LOG( NetworkTag ) << "Connected to " << host;
 */

#include <kmac/nova/platform/config.h>
#include <kmac/nova/macros.h>
#include <kmac/nova/logger_traits.h>
#include <kmac/nova/logger.h>
#include <kmac/nova/record.h>
#include <kmac/nova/sink.h>
#include <kmac/nova/scoped_configurator.h>
#include <kmac/nova/timestamp_helper.h>
#include <kmac/nova/version.h>

#endif // KMAC_NOVA_H
