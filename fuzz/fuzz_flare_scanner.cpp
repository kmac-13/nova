/**
 * @file fuzz_flare_scanner.cpp
 * @brief LibFuzzer target for the Flare Scanner.
 *
 * Feeds arbitrary bytes to Scanner::scan(), exercising:
 *   - magic number search across arbitrary byte sequences
 *   - record size field validation (MAX_RECORD_SIZE guard)
 *   - END marker detection and acceptance
 *   - resynchronization after corrupt or partial records
 *   - scanner state across multiple successive scan() calls on the same buffer
 *
 * The scanner is designed to be robust against adversarial input - this target
 * validates that guarantee: no crash, no hang, and recordOffset()/recordSize()
 * are always within bounds when scan() returns true.
 *
 * After each successful scan(), setStartOffset() advances the scanner past the
 * found record before the next call.  Without this, scan() would re-find the
 * same magic on every iteration and loop forever.  This matches the documented
 * usage pattern from scanner.h.
 *
 * The fuzzer input is passed directly to scan() as the byte buffer.  No
 * preprocessing is done: the fuzzer controls the full byte sequence, including
 * the magic number, size fields, TLV content, and end marker.  A valid-looking
 * seed corpus helps the fuzzer find interesting paths quickly; see
 * fuzz/corpus/flare_scanner/.
 */

#include "kmac/flare/scanner.h"

#include <cassert>
#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* data, size_t size )
{
	kmac::flare::Scanner scanner;

	while ( scanner.scan( data, size ) )
	{
		const std::size_t offset = scanner.recordOffset();
		const std::size_t recSize = scanner.recordSize();

		// recordOffset() and recordSize() must be within the input buffer
		// when scan() returns true - assert catches any bounds violation
		// (ASan will catch out-of-bounds reads in the scanner itself)
		assert( offset < size );
		assert( recSize > 0 );
		assert( offset + recSize <= size );

		// advance past the found record before the next scan() call;
		// without this, scan() re-finds the same magic every iteration
		scanner.setStartOffset( offset + recSize );
	}

	return 0;
}
