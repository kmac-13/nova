#pragma once
#ifndef KMAC_FLARE_READER_H
#define KMAC_FLARE_READER_H

#include "scanner.h"

#include <cstddef>
#include <cstdint>

namespace kmac {
namespace flare {

struct Record;

/**
 * @brief Parses and decodes Flare binary records into structured data.
 *
 * The Reader is responsible for taking raw binary data (typically located by
 * Scanner) and extracting the structured record information.  It handles TLV
 * (Type-Length-Value) decoding and tolerates various forms of corruption.
 *
 * Key Features:
 * - parses all Flare TLV types (status, sequence, timestamp, metadata, etc.)
 * - tolerates unknown TLV types (skips them for forward compatibility)
 * - handles truncated records (extracts what's available)
 * - handles torn records (partial writes from crashes)
 * - no heap allocation during parsing
 * - never throws exceptions
 *
 * Design Philosophy:
 * The Reader follows the "some data is better than no data" philosophy.  If
 * a record is partially corrupted, the Reader will extract all valid fields
 * it can find rather than rejecting the entire record.  This maximizes
 * forensic value from crash dumps.
 *
 * Relationship with Scanner:
 * - Scanner: finds record boundaries in binary data
 * - Reader: parses record contents into structured format
 *
 * Typical usage combines both:
 * @code
 * Scanner scanner;
 * Reader reader;
 * Record record;
 *
 * const uint8_t* data = loadCrashLog();
 * size_t dataSize = getCrashLogSize();
 *
 * while (scanner.scan(data, dataSize))
 * {
 *     size_t offset = scanner.recordOffset();
 *     size_t size = scanner.recordSize();
 *
 *     if (reader.parseNext(data + offset, size, record))
 *     {
 *         // Record successfully parsed
 *         std::cout << "Tag: 0x" << std::hex << record.tagId << "\n";
 *         std::cout << "Message: " << record.message << "\n";
 *         std::cout << "Status: " << record.statusString() << "\n";
 *     }
 *
 *     scanner.setStartOffset(offset + size);
 * }
 * @endcode
 *
 * Simplified Usage (Reader with Internal Scanner):
 * The Reader contains its own Scanner instance, so you can also use it
 * directly on a buffer without manually managing Scanner:
 * @code
 * Reader reader;
 * Record record;
 *
 * const uint8_t* data = loadCrashLog();
 * size_t dataSize = getCrashLogSize();
 *
 * while (reader.parseNext(data, dataSize, record))
 * {
 *     // Process record...
 *     // Reader automatically advances its internal scanner
 * }
 * @endcode
 *
 * TLV Parsing:
 * The Reader decodes all standard Flare TLV types:
 * - RecordStatus (0x03): Complete/Truncated/InProgress
 * - SequenceNumber (0x04): Monotonic sequence counter
 * - TimestampNs (0x0A): Nanosecond timestamp
 * - TagId (0x0B): FNV-1a hash of tag string
 * - FileName (0x0C): Source file name
 * - LineNumber (0x0D): Source line number
 * - FunctionName (0x0E): Function name
 * - ProcessId (0x0F): Process ID (if available)
 * - ThreadId (0x10): Thread ID (if available)
 * - MessageBytes (0x14): Log message text
 * - MessageTruncated (0x15): Truncation flag
 * - RecordEnd (0xFFFF): End marker
 *
 * Forward Compatibility:
 * Unknown TLV types are silently skipped.  This allows old readers to parse
 * files created by newer writers that may have added new TLV types.
 *
 * Corruption Handling:
 * The Reader is defensive against various corruption scenarios:
 * - missing TLVs: fields default to zero/empty
 * - truncated TLVs: parsing stops, partial data preserved
 * - invalid lengths: TLV skipped or parsing stopped
 * - unknown types: TLV skipped
 * - missing END marker: Record still processed
 *
 * Thread Safety:
 * Reader is not thread-safe.  Each thread should use its own Reader instance.
 *
 * Record Structure:
 * The parsed output is stored in a flare::Record structure with fixed-size
 * buffers:
 * - file: 256 bytes
 * - function: 256 bytes
 * - message: 4096 bytes
 *
 * If source data exceeds these limits, content is truncated.
 *
 * @note No allocation occurs during parsing.
 */
class Reader
{
private:
	/**
	 * @brief Internal scanner for locating records.
	 *
	 * The Reader maintains its own Scanner instance for convenience.
	 * This allows parseNext() to automatically find and parse the next
	 * record without requiring external scanner management.
	 *
	 * When parseNext() is called:
	 * 1. scanner locates next valid record
	 * 2. reader parses that record
	 * 3. scanner position updated for next call
	 *
	 * This is an implementation detail - users don't need to interact
	 * with this scanner directly.
	 */
	Scanner _scanner;

public:
	/**
	 * @brief Find and parse next record in buffer.
	 *
	 * This is the primary interface for reading Flare crash logs.  It combines
	 * scanning (finding records) and parsing (extracting fields) in a single
	 * convenient method.
	 *
	 * The method will:
	 * 1. use internal scanner to find next valid record
	 * 2. parse the record's TLV structure
	 * 3. populate outRecord with extracted data
	 * 4. update internal position for next call
	 *
	 * @param data pointer to binary crash log data
	 * @param size size of data buffer in bytes
	 * @param outRecord putput parameter - filled with parsed record data
	 * @return true if record found and parsed, false if no more records
	 *
	 * @note This method modifies internal scanner state.  Repeated calls
	 * on the same buffer will iterate through all records.
	 *
	 * @note Never throws exceptions, never allocates memory.
	 *
	 * Partial Record Handling:
	 * If a record is corrupted or incomplete, parseNext() will:
	 * - extract all valid fields it can find
	 * - set missing fields to zero/empty
	 * - still return true (partial data is better than no data)
	 *
	 * Return Value:
	 * - true: record found and parsed (check record.status for quality)
	 * - false: no more records in buffer
	 *
	 * False does NOT mean error - it means end of records.
	 *
	 * Multi-Buffer Usage:
	 * To process multiple separate crash logs, create new Reader for each
	 * or manually reset the internal scanner.
	 *
	 * Thread Safety:
	 * Not thread-safe.  Use separate Reader instance per thread.
	 *
	 * Field Defaults:
	 * If a TLV is missing from the record, the corresponding field will have
	 * its default value:
	 * - numeric fields: 0
	 * - string fields: "" (empty string)
	 * - boolean fields: false
	 *
	 * This means you can safely access all fields without checking for
	 * presence, though you may want to check record.status to assess
	 * data quality.
	 */
	bool parseNext( const std::uint8_t* data, std::size_t size, Record& outRecord );

private:
	/**
	 * @brief Parse a single record from binary data.
	 *
	 * This is the internal implementation that does the actual TLV parsing.
	 * It's called by parseNext() after the scanner has located a valid record.
	 *
	 * This method:
	 * 1. initializes outRecord to defaults (all zeros/empty)
	 * 2. skips MAGIC and SIZE fields (already validated by scanner)
	 * 3. iterates through TLVs, parsing each one
	 * 4. stops at END marker or end of record
	 *
	 * @param data pointer to start of record (MAGIC number)
	 * @param size total size of record in bytes
	 * @param outRecord output parameter - filled with parsed data
	 *
	 * @return true if parsing succeeded (i.e. record structure appears
	 * valid, although completeness is not guaranteed), false if parsing
	 * failed (i.e. record is completely unparseable)
	 *
	 * @note This is an internal method.  External users should use parseNext().
	 *
	 * TLV Decoding Process:
	 * For each TLV in the record:
	 * 1. read type (2 bytes)
	 * 2. read length (2 bytes)
	 * 3. read value (length bytes)
	 * 4. store in appropriate Record field
	 * 5. advance to next TLV
	 *
	 * Buffer Overflow Protection:
	 * When copying strings to Record fields, the parser ensures null-termination
	 * and prevents buffer overflows:
	 * - file[256]: max 255 chars + null terminator
	 * - function[256]: max 255 chars + null terminator
	 * - message[4096]: max 4095 chars + null terminator
	 *
	 * If source data is longer, it's silently truncated.
	 *
	 * Corruption Handling:
	 * The parser is defensive against various corruption scenarios:
	 *
	 * 1. Invalid TLV length:
	 *    - if length extends beyond record boundary, stop parsing
	 *    - preserve all fields parsed so far
	 *
	 * 2. Unknown TLV type:
	 *    - skip the TLV entirely
	 *    - continue parsing remaining TLVs
	 *
	 * 3. Missing END marker:
	 *    - not an error
	 *    - record is still processed
	 *    - common in torn writes
	 *
	 * 4. Truncated value:
	 *    - if record ends mid-TLV, stop parsing
	 *    - return what was successfully parsed
	 *
	 * Note that parseRecord() returning true doesn't mean the record is
	 * complete or uncorrupted.  Check record.status for quality information:
	 * - status == Complete: full record, all data valid
	 * - status == Truncated: intentional truncation, partial data
	 * - status == InProgress: torn write, extract what's available
	 * - status == Unknown: old format or corruption
	 *
	 * Guarantees:
	 * - no heap allocation
	 * - no exceptions
	 *
	 * Thread Safety:
	 * Not thread-safe.  parseRecord() modifies outRecord.
	 * Each thread must use its own Reader and Record instances.
	 */
	bool parseRecord( const std::uint8_t* data, std::size_t size, Record& outRecord );
};

} // namespace flare
} // namespace kmac

#endif // KMAC_FLARE_READER_H
