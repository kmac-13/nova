#include "kmac/nova/extras/multiline_formatter.h"

#include "kmac/nova/record.h"
#include "kmac/nova/sink.h"
#include "kmac/nova/extras/buffer.h"

#include <array>
#include <charconv>
#include <cstring>
#include <iterator>
#include <system_error>

namespace kmac::nova::extras
{

MultilineFormatter::MultilineFormatter(
	bool addLineNumbers,
	bool preserveEmptyLines
) noexcept
	: _addLineNumbers( addLineNumbers )
	, _preserveEmptyLines( preserveEmptyLines )
{
}

std::size_t MultilineFormatter::maxChunkSize() const noexcept
{
	// worst case: original message size + line number prefix
	// we'll use a reasonable max line length
	return 4096 + MAX_LINE_NUMBER_PREFIX;
}

void MultilineFormatter::formatAndWrite(
	const kmac::nova::Record& record,
	kmac::nova::Sink& downstream
) noexcept
{
	if ( record.messageSize == 0 )
	{
		// empty message - emit single empty record
		downstream.process( record );
		return;
	}

	// count total lines first (for line number formatting)
	const std::size_t totalLines = _addLineNumbers
		? countLines( record.message, record.messageSize )
		: 0;

	// process each line
	const char* current = record.message;
	const char* end = record.message + record.messageSize;
	std::size_t lineNumber = 0;

	while ( current < end )
	{
		const char* lineStart = nullptr;
		std::size_t lineLength = 0;

		current = findNextLine( current, end, lineStart, lineLength );

		// skip empty lines if configured
		if ( lineLength == 0 && !_preserveEmptyLines )
		{
			continue;
		}

		++lineNumber;

		// format the line
		constexpr std::size_t BUFFER_SIZE = 4096;
		std::array< char, BUFFER_SIZE + 1 > bufferStorage{};  // +1 for null terminator
		Buffer buffer( bufferStorage.data(), BUFFER_SIZE );   // Reserve last byte for null

		// add line number prefix if enabled
		if ( _addLineNumbers && totalLines > 1 )
		{
			std::array< char, MAX_LINE_NUMBER_PREFIX > prefix{};
			char* prefixEnd = prefix.data();

			// format: "[N/Total] "
			*prefixEnd++ = '[';

			auto [ ptr1, ec1 ] = std::to_chars( prefixEnd, std::data( prefix ) + sizeof( prefix ) - 10, lineNumber );
			if ( ec1 == std::errc{} )
			{
				prefixEnd = ptr1;
				*prefixEnd++ = '/';

				auto [ ptr2, ec2 ] = std::to_chars( prefixEnd, std::data( prefix ) + sizeof( prefix ) - 3, totalLines );
				if ( ec2 == std::errc{} )
				{
					prefixEnd = ptr2;
					*prefixEnd++ = ']';
					*prefixEnd++ = ' ';

					const std::size_t prefixLen = prefixEnd - std::data( prefix );
					(void) buffer.append( std::data( prefix ), prefixLen );
				}
			}
		}

		// add the line content
		if ( lineLength > 0 )
		{
			(void) buffer.append( lineStart, lineLength );
		}

		// null-terminate the buffer
		bufferStorage.data()[ buffer.size() ] = '\0';

		// emit record for this line
		kmac::nova::Record lineRecord = record;
		lineRecord.messageSize = static_cast< std::uint32_t >( buffer.size() );
		lineRecord.message = buffer.data();

		downstream.process( lineRecord );

		if ( current == nullptr )
		{
			break; // no more lines
		}
	}
}

std::size_t MultilineFormatter::countLines( const char* message, std::size_t messageSize ) const noexcept
{
	if ( messageSize == 0 )
	{
		return 0;
	}

	std::size_t count = 0;
	const char* current = message;
	const char* end = message + messageSize;

	while ( current < end )
	{
		const char* lineStart = nullptr;
		std::size_t lineLength = 0;

		current = findNextLine( current, end, lineStart, lineLength );

		// count line if it's not empty, or if we're preserving empty lines
		if ( lineLength > 0 || _preserveEmptyLines )
		{
			++count;
		}

		if ( current == nullptr )
		{
			break;
		}
	}

	// if no newlines, it's a single line
	return ( count == 0 ) ? 1 : count;
}

const char* MultilineFormatter::findNextLine(
	const char* start,
	const char* end,
	const char*& outLineStart,
	std::size_t& outLineLength
) const noexcept
{
	if ( start >= end )
	{
		outLineStart = start;
		outLineLength = 0;
		return nullptr;
	}

	outLineStart = start;
	const char* current = start;

	// find end of line (either \n or \r\n)
	while ( current < end )
	{
		if ( *current == '\n' )
		{
			// found newline
			outLineLength = current - start;

			// skip the \n
			++current;

			return current;
		}

		if ( *current == '\r' && current + 1 < end && *( current + 1 ) == '\n' )
		{
			// found \r\n
			outLineLength = current - start;

			// skip the \r\n
			current += 2;

			return current;
		}

		++current;
	}

	// reached end without finding newline - this is the last line
	outLineLength = current - start;
	return nullptr;
}

} // namespace kmac::nova::extras
