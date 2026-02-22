#pragma once
#ifndef KMAC_NOVA_EXTRAS_BUFFER_H
#define KMAC_NOVA_EXTRAS_BUFFER_H

#include <cstddef>

namespace kmac::nova::extras
{

/**
 * @brief Fixed-size buffer helper for record builders.
 *
 * ✅ SAFE FOR SAFETY-CRITICAL SYSTEMS
 *
 * Buffer provides a simple, safe interface for building log messages
 * in a fixed-size char array.
 * Features:
 * - no heap allocation (wraps existing buffer)
 * - safe append operations that never overflow
 *
 * Thread safety:
 * - not thread-safe (single-threaded use expected)
 * - buffer does not own memory (caller manages lifetime)
 *
 * Usage:
 *   char storage[256];
 *   Buffer buf(storage, sizeof(storage));
 *
 *   if ( ! buf.append( "Hello ", 6 ) ) {
 *       // nothing appended
 *   }
 *
 *   buf.appendChar( '!' );
 *
 *   std::string_view result( buf.data(), buf.size() );
 *
 * @todo consider reserve/commit API
 */
class Buffer
{
private:
	char* _buffer;           ///< external buffer (not owned)
	std::size_t _capacity;   ///< total buffer capacity in bytes
	std::size_t _size;       ///< current content size in bytes

public:
	/**
	 * @brief Construct buffer wrapper.
	 *
	 * @param buffer destination buffer (must remain valid)
	 * @param capacity total buffer size in bytes
	 */
	Buffer( char* buffer, std::size_t capacity ) noexcept;

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
	 * @brief Append data to buffer.
	 *
	 * @param data source data to append
	 * @param length number of bytes to append
	 * @return true if all data fit, false if rejected
	 *
	 * @note partial appends are rejected (all-or-nothing)
	 */
	[[nodiscard]]
	bool append( const char* data, std::size_t length ) noexcept;

	/**
	 * @brief Append single character to buffer.
	 *
	 * @param c character to append
	 * @return true if character fit, false if rejected
	 */
	[[nodiscard]]
	bool appendChar( char c ) noexcept;

	/**
	 * @brief Append a string literal to buffer.
	 *
	 * @param lit literal string to append
	 * @return true if string fit, false if rejected
	 */
	template< std::size_t N >
	bool appendLiteral( const char ( &lit )[ N ] ) noexcept;
};

template< std::size_t N >
bool Buffer::appendLiteral( const char ( &lit )[ N ] ) noexcept
{
	static_assert( N > 0 );
	return append( lit, N - 1 );
}

} // namespace kmac::nova::extras

#endif // KMAC_NOVA_EXTRAS_BUFFER_H
