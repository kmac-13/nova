#pragma once
#ifndef KMAC_FLARE_SCANNER_H
#define KMAC_FLARE_SCANNER_H

#include <cstddef>
#include <cstdint>

namespace kmac::flare
{

/**
 * @brief Locates valid Flare records in potentially corrupted binary data.
 *
 * The Scanner is responsible for finding the boundaries of Flare records within
 * a byte stream.  It is designed to be robust against corruption, partial writes,
 * and torn records that may occur during crashes.
 *
 * Key Features:
 * - searches for FLARE_MAGIC number to identify record starts
 * - validates record size field
 * - validates END marker presence before accepting records
 * - resynchronizes after corruption (continues searching)
 * - never allocates memory (crash-safe)
 * - never throws exceptions
 *
 * Design Philosophy:
 * The Scanner is intentionally simple and defensive. It assumes the data may be
 * corrupted and makes no guarantees about what it finds.  Its job is to locate
 * *candidate* records; the Reader validates and parses them.
 *
 * Usage Pattern:
 * @code
 * Scanner scanner;
 * const uint8_t* data = ...; // Binary crash log data
 * size_t dataSize = ...;
 *
 * while (scanner.scan(data, dataSize))
 * {
 *     size_t offset = scanner.recordOffset();
 *     size_t size = scanner.recordSize();
 *
 *     // Pass to Reader for parsing
 *     Reader reader;
 *     Record record;
 *     if (reader.parseNext(data + offset, size, record))
 *     {
 *         // Successfully parsed record
 *     }
 * }
 * @endcode
 *
 * Thread Safety:
 * Scanner is not thread-safe.  Each thread should use its own Scanner instance.
 *
 * Performance:
 * - linear scan: O(n) where n = buffer size
 * - early rejection of invalid candidates
 * - minimal memory footprint (~24 bytes)
 *
 * Corruption Recovery:
 * The Scanner will continue searching for valid records even after encountering
 * corruption.  It uses several validation layers:
 * 1. magic number match (8 bytes)
 * 2. size field sanity check (0 < size <= 64KB)
 * 3. size fits within remaining buffer
 * 4. END marker present at expected position (P3 feature)
 *
 * If any validation fails, Scanner continues to next byte position.
 */
class Scanner
{
private:
	/**
	 * @brief Internal state machine for record scanning.
	 *
	 * The Scanner uses a simple state machine to track its progress:
	 * - SeekingMagic: searching for FLARE_MAGIC number
	 * - ReadingSize: found magic, reading size field
	 * - Validating: validating complete record structure
	 *
	 * Note: This state is primarily for internal tracking and debugging.
	 * The scan() method is the primary interface.
	 */
	enum class State
	{
		SeekingMagic,  ///< Searching for record start (FLARE_MAGIC)
		ReadingSize,   ///< Reading size field after magic
		Validating     ///< Validating record structure
	};

	State _state;            ///< Current scanner state
	size_t _offset;          ///< Current search position in buffer
	uint32_t _expectedSize;  ///< Expected size of current candidate record

public:
	/**
	 * @brief Construct a new Scanner.
	 *
	 * The Scanner starts in SeekingMagic state with offset 0.
	 * No memory allocation occurs.
	 */
	Scanner();

	/**
	 * @brief Get offset of last found record.
	 *
	 * Returns the byte offset into the buffer where the last valid record
	 * found by scan() begins.  This offset points to the FLARE_MAGIC number
	 * that starts the record.
	 *
	 * @return byte offset of record start (0-based)
	 *
	 * @note Only valid after scan() returns true.
	 * @note Undefined behavior if called before any successful scan().
	 */
	size_t recordOffset() const;

	/**
	 * @brief Get size of last found record.
	 *
	 * Returns the total size in bytes of the last valid record found by scan().
	 * This is the value stored in the record's size field and includes:
	 * - MAGIC (8 bytes)
	 * - SIZE field itself (4 bytes)
	 * - all TLVs
	 * - END marker (4 bytes)
	 *
	 * @return total record size in bytes
	 *
	 * @note Only valid after scan() returns true.
	 * @note Undefined behavior if called before any successful scan().
	 * @note Maximum possible value: 65536 (64 KB)
	 *
	 * Size Validation:
	 * The size returned is guaranteed to be:
	 * - greater than 0
	 * - less than or equal to MAX_RECORD_SIZE (64 KB)
	 * - fits within the scanned buffer (offset + size <= buffer size)
	 *
	 * This is enforced by scan() before returning true.
	 */
	size_t recordSize() const;

	/**
	 * @brief Reset scanner to initial state.
	 *
	 * After reset, the scanner returns to SeekingMagic state and
	 * will begin searching from the start of the buffer on the next
	 * scan() call.
	 *
	 * Use Case:
	 * - reusing scanner for multiple independent buffers
	 * - restarting search from beginning
	 */
	void reset();

	/**
	 * @brief Set the starting search offset.
	 *
	 * This allows resuming a search from a specific position, useful when
	 * processing records incrementally or continuing after a found record.
	 *
	 * @param offset byte offset to start searching from (0-based)
	 *
	 * Use Cases:
	 * - continue searching after previously found record
	 * - skip known corrupted regions
	 * - implement custom resynchronization logic
	 *
	 * @note The scanner will continue from the next scan() call at this offset.
	 */
	void setStartOffset( size_t offset );

	/**
	 * @brief Scan buffer for next valid Flare record.
	 *
	 * Searches for a valid Flare record starting from the current offset.
	 * If a valid record is found, returns true and the record's offset/size
	 * can be retrieved via recordOffset() and recordSize().
	 *
	 * Validation Process:
	 * 1. search for FLARE_MAGIC (0x4B4D41435F464C52 = "KMAC_FLR")
	 * 2. read and validate size field:
	 *    - must be > 0
	 *    - must be <= MAX_RECORD_SIZE (64KB)
	 *    - must fit within remaining buffer
	 * 3. validate END marker at expected position (P3 feature):
	 *    - read 2 bytes at (offset + size - 4)
	 *    - must equal TlvType::RecordEnd (0xFFFF)
	 *
	 * If any validation fails, scanner advances to next byte and continues.
	 *
	 * @param data pointer to binary data buffer
	 * @param size size of data buffer in bytes
	 * @return true if valid record found, false if no more records
	 *
	 * @note This method updates internal state.  After returning true,
	 * call recordOffset() and recordSize() to get record location.
	 *
	 * @note This method never throws and never allocates memory.
	 *
	 * False Positive Filtering:
	 * The END marker validation significantly reduces false positives where
	 * random data happens to match FLARE_MAGIC.  Without this check,
	 * approximately 1 in 2^64 random byte sequences would be accepted as
	 * valid records.  With END marker validation, this drops to approximately
	 * 1 in 2^80 (assuming independent random data).
	 *
	 * Corruption Recovery:
	 * A valid record can be found in the middle of corrupted data, e.g.:
	 * [garbage][MAGIC][SIZE][...valid record...][END][garbage][MAGIC]...
	 *
	 * Thread Safety:
	 * Not thread-safe.  Each thread must use its own Scanner instance.
	 *
	 * Performance:
	 * - best case: O(1) if record at current offset
	 * - worst case: O(n) if no valid records (scans entire buffer)
	 * - typical: O(k) where k = bytes to next valid record
	 * - validation overhead: minimal per candidate
	 */
	bool scan( const uint8_t* data, size_t size );
};

} // namespace kmac::flare

#endif // KMAC_FLARE_SCANNER_H
