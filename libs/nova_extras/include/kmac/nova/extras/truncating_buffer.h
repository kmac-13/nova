#pragma once
#ifndef KMAC_NOVA_EXTRAS_TRUNCATING_BUFFER_H
#define KMAC_NOVA_EXTRAS_TRUNCATING_BUFFER_H

#include <cstddef>

namespace kmac::nova::extras
{

/**
 * @brief Fixed-size buffer helper for record builders.
 *
 * ✅ SAFE FOR SAFETY-CRITICAL SYSTEMS
 *
 * Buffer provides a simple, safe interface for building log messages
 * in a fixed-size char array. Used internally by TruncatingRecordBuilder
 * and ContinuationRecordBuilder.
 *
 * Features:
 * - no heap allocation (wraps existing buffer)
 * - tracks truncation state
 * - safe append operations that never overflow
 * - [[nodiscard]] return values enforce error checking
 *
 * Truncation behavior:
 * - once truncation occurs, truncated() returns true
 * - further appends fail but don't corrupt buffer
 * - buffer remains null-terminated when possible
 *
 * Thread safety:
 * - not thread-safe (single-threaded use expected)
 * - buffer does not own memory (caller manages lifetime)
 *
 * Usage:
 *   char storage[256];
 *   Buffer buf(storage, sizeof(storage));
 *
 *   if (!buf.append("Hello ", 6)) {
 *       // truncation occurred
 *   }
 *
 *   buf.appendChar('!');
 *
 *   std::string_view result(buf.data(), buf.size());
 *   bool wasTruncated = buf.truncated();
 */
class TruncatingBuffer
{
private:
	char* _buffer = nullptr;    ///< external buffer (not owned)
	std::size_t _capacity = 0;  ///< total buffer capacity in bytes
	std::size_t _size = 0;      ///< current content size in bytes
	bool _truncated = false;    ///< true if any append operation failed

public:
	/**
	 * @brief Construct buffer wrapper.
	 *
	 * @param buffer destination buffer (must remain valid)
	 * @param capacity total buffer size in bytes
	 */
	TruncatingBuffer( char* buffer, std::size_t capacity ) noexcept;

	/**
	 * @brief Get buffer contents.
	 *
	 * @return pointer to buffer data (might not be null-terminated)
	 */
	const char* data() const noexcept;

	/**
	 * @brief Get current content size.
	 *
	 * @return number of bytes written to buffer
	 */
	std::size_t size() const noexcept;

	/**
	 * @brief Get the remaining space.
	 *
	 * @return number of bytes remaining in the buffer
	 */
	std::size_t remaining() const noexcept;

	/**
	 * @brief Check truncation state.
	 *
	 * @return true if any append operation failed
	 */
	bool truncated() const noexcept;

	/**
	 * @brief Append data to buffer.
	 *
	 * @param data source data to append
	 * @param length number of bytes to append
	 * @return true if all data fit, false if truncated
	 *
	 * @note truncation sets truncated() flag permanently
	 */
	[[nodiscard]]
	bool append( const char* data, std::size_t length ) noexcept;

	/**
	 * @brief Append single character to buffer.
	 *
	 * @param c character to append
	 * @return true if character fit, false if truncated
	 */
	[[nodiscard]]
	bool appendChar( char chr ) noexcept;
};

} // namespace kmac::nova::extras

#endif // KMAC_NOVA_EXTRAS_TRUNCATING_BUFFER_H
