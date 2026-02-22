#pragma once
#ifndef KMAC_NOVA_EXTRAS_STREAMING_MACROS_H
#define KMAC_NOVA_EXTRAS_STREAMING_MACROS_H

#include "streaming_record_builder.h"

/**
 * Extras Macros for StreamingRecordBuilder
 *
 * WARNING: These macros rely on StreamingRecordBuilder, which performs heap
 * allocation through the use of std::ostringstream.  This is NOT suitable
 * for real-time, safety-critical, or crash-logging scenarios.
 * 
 * Use only when:
 * - convenience is more important than determinism
 * - application is not real-time or safety-critical
 * - heap allocation is acceptable
 * 
 * For production systems, prefer the core library's TruncatingRecordBuilder
 * or ContinuationRecordBuilder instead.
 */

/**
 * NOVA_LOG_STREAM - Streaming builder using std::ostringstream
 * 
 * Usage: NOVA_LOG_STREAM(TagType) << "message" << complexObject;
 * 
 * Behavior:
 * - uses std::ostringstream for formatting
 * - unlimited message length (heap allocated)
 * - works with any type that has operator<<
 * - PERFORMS HEAP ALLOCATION (non-deterministic)
 * - can throw std::bad_alloc
 * 
 * Pros:
 * - simple to use, familiar API
 * - no truncation or continuation
 * - works with all streamable types
 * 
 * Cons:
 * - heap allocation (non-deterministic)
 * - slower than fixed-buffer builders
 * - not safe for crash handlers
 * - not real-time safe
 */
#define NOVA_LOG_STREAM( TagType ) \
	if constexpr ( ::kmac::nova::logger_traits< TagType >::enabled ) \
		::kmac::nova::extras::StreamingRecordBuilder< TagType >( \
			FILE_NAME, __func__, __LINE__ \
		)

#endif // KMAC_NOVA_EXTRAS_STREAMING_MACROS_H
