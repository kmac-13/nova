/**
 * @file fuzz_flare_reader.cpp
 * @brief LibFuzzer target for the Flare Reader.
 *
 * Feeds arbitrary bytes directly to Reader::parseNext(), bypassing the
 * Scanner, to stress the TLV parser in isolation.  This exercises:
 *   - TLV type dispatch for all known types (1-4, 10-16, 20-21, 30-31, 0xFFFF)
 *   - unknown TLV type skip path (forward compatibility)
 *   - length field validation (zero-length, max-length, truncated)
 *   - RecordEnd (0xFFFF) marker detection
 *   - partial records (input shorter than declared record size)
 *   - all fixed-buffer copy paths (file, function, message, stackFrames)
 *   - buffer overflow guards on MAX_FILENAME_LEN, MAX_FUNCTION_LEN,
 *     MAX_MESSAGE_LEN, and MAX_STACK_FRAMES
 *
 * Unlike fuzz_flare_scanner, which exercises record boundary detection,
 * this target focuses entirely on what happens after a candidate record
 * region has been identified.  Both targets are needed:
 *   - Scanner: finds where records are in arbitrary binary data
 *   - Reader: parses the TLV payload within a record boundary
 *
 * The "some data is better than no data" contract is validated here:
 * parseNext() must never crash or invoke UB regardless of input content,
 * and must return a usable (possibly partially populated) Record or false.
 */

#include "kmac/flare/reader.h"
#include "kmac/flare/record.h"

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* data, size_t size )
{
	kmac::flare::Reader reader;
	kmac::flare::Record record;

	// parseNext() must never crash or invoke UB on arbitrary input
	reader.parseNext( data, size, record );

	return 0;
}
