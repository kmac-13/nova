#pragma once
#ifndef KMAC_FLARE_H
#define KMAC_FLARE_H

/**
 * @file flare.h
 * @brief Umbrella header for Nova Flare - async-signal-safe crash/forensic logging.
 *
 * Including this header pulls in the complete Flare public API.  Platform-specific
 * components (FdWriter, SignalHandler, BareMetalFaultHandler) guard their own
 * contents with the appropriate platform checks, so this header is safe to include
 * on any supported target.
 *
 * For fine-grained inclusion, include individual headers from <kmac/flare/> directly.
 *
 * Component overview:
 *
 *   EmergencySink          - Nova Sink that writes Flare TLV records; the central
 *                            component for crash and signal handler logging
 *   IWriter                - interface for the underlying output destination
 *   FdWriter               - IWriter for POSIX file descriptors (async-signal-safe)
 *                            (POSIX only: Linux, macOS, Android, FreeBSD)
 *   FileWriter             - IWriter for C FILE* handles
 *   RamWriter              - IWriter for a fixed RAM buffer (no OS dependency)
 *   UartWriter             - IWriter via a user-supplied UART write callback
 *   SignalHandler<>        - installs POSIX signal handlers and captures CPU registers
 *                            and fault addresses into Flare records
 *                            (POSIX only: Linux, macOS, Android, FreeBSD)
 *   BareMetalFaultHandler  - Cortex-M / RISC-V fault handler with vector table
 *                            entry points (bare-metal only: NOVA_BARE_METAL)
 *   FaultContext           - fault address and CPU register snapshot
 *   Reader                 - parses Flare TLV records from a byte buffer
 *   Scanner                - locates Flare records in a raw byte stream, tolerating
 *                            partial writes and corruption
 *   Record                 - Flare record structure populated by the reader
 *   TLV constants          - type tags and wire format constants
 *
 * Macros (defined in this header, require <kmac/nova.h>):
 *
 *   NOVA_FLARE_LOG(Tag)            - alias for NOVA_LOG_STACK; safe in signal handlers
 *   NOVA_FLARE_LOG_BUF(Tag, Size)  - alias for NOVA_LOG_BUF_STACK with custom buffer size
 *
 * Usage:
 *
 *   #include <kmac/nova.h>   // required for tag configuration and NOVA_LOG_* macros
 *   #include <kmac/flare.h>
 *
 *   struct CrashTag {};
 *   NOVA_LOGGER_TRAITS( CrashTag, CRASH, true, kmac::nova::TimestampHelper::steadyNanosecs );
 *
 *   // setup (before any crash)
 *   static int fd = open( "/var/log/crash.flare", O_WRONLY | O_CREAT | O_APPEND, 0644 );
 *   static kmac::flare::FdWriter  writer( fd );
 *   static kmac::flare::EmergencySink sink( &writer );
 *   kmac::nova::Logger< CrashTag >::bindSink( &sink );
 *
 *   // in signal handler
 *   void onSignal( int sig )
 *   {
 *       NOVA_FLARE_LOG( CrashTag ) << "Signal " << sig;
 *       _Exit( 128 + sig );
 *   }
 */

// - core record construction and emission
#include <kmac/flare/emergency_sink.h>
#include <kmac/flare/iwriter.h>
#include <kmac/flare/record.h>
#include <kmac/flare/tlv.h>

// - output writers (each guards itself with platform checks where required)
#include <kmac/flare/fd_writer.h>
#include <kmac/flare/file_writer.h>
#include <kmac/flare/ram_writer.h>
#include <kmac/flare/uart_writer.h>

// - fault/signal capture
#include <kmac/flare/fault_context.h>
#include <kmac/flare/signal_handler.h>
#include <kmac/flare/bare_metal_fault_handler.h>

// - post-mortem analysis
#include <kmac/flare/reader.h>
#include <kmac/flare/scanner.h>

// ============================================================================
// Flare logging macros
//
// NOVA_FLARE_LOG and NOVA_FLARE_LOG_BUF are thin aliases over NOVA_LOG_STACK
// and NOVA_LOG_BUF_STACK.  They are the correct macros to use in signal
// handlers and crash contexts: they allocate the record builder on the stack
// (no thread-local storage, no heap) and are safe to call from any context
// where the Nova Sink is also async-signal-safe (e.g. EmergencySink+FdWriter).
//
// Requires <kmac/nova.h> to be included first.
// ============================================================================

/**
 * @brief Stack-based logger for use in signal handlers and crash contexts.
 *
 * Equivalent to NOVA_LOG_STACK.  Uses a stack-allocated buffer of the default
 * size (NOVA_DEFAULT_BUFFER_SIZE bytes).  Safe to call from signal handlers
 * when paired with an async-signal-safe sink such as EmergencySink + FdWriter.
 *
 * @param TagType the logging tag type (must have LoggerTraits specialization)
 */
#define NOVA_FLARE_LOG( TagType ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	NOVA_LOG_STACK( TagType )

/**
 * @brief Stack-based logger with a custom buffer size for signal handler use.
 *
 * Equivalent to NOVA_LOG_BUF_STACK.  Use a smaller buffer (e.g. 128, 256, 512,
 * etc bytes) in signal handlers where stack space is limited (Linux default:
 * 8 KB alternate signal stack).
 *
 * @param TagType the logging tag type
 * @param BufferSize buffer size in bytes; keep below 2 KB in signal handlers
 */
#define NOVA_FLARE_LOG_BUF( TagType, BufferSize ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	NOVA_LOG_BUF_STACK( TagType, BufferSize )

#endif // KMAC_FLARE_H
