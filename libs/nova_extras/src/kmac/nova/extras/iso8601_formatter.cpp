#include "kmac/nova/extras/iso8601_formatter.h"
#include "kmac/nova/extras/formatting_helper.h"

#include <kmac/nova/record.h>
#include <kmac/nova/platform/array.h>

#include <cstring>

namespace kmac {
namespace nova {
namespace extras {

ISO8601Formatter::ISO8601Formatter() noexcept
{
}

void ISO8601Formatter::begin( const kmac::nova::Record& record ) noexcept
{
	_stage = Stage::TimestampWithSpace;
	_offset = 0;

	// build timestamp with trailing space
	char* out = _timestampBuf.data();
	out = FormattingHelper::formatTimestamp( out, record.timestamp );
	*out++ = ' ';
	_timestampLen = static_cast< std::size_t >( out - _timestampBuf.data() );

	// pre-format line number with trailing space into _lineBuf
	_lineLen = 0;
	unsigned int line = record.line;
	kmac::nova::platform::Array< char, 16 > tmp {};
	int idx = 0;
	do
	{
		tmp.data()[ idx++ ] = char( '0' + ( line % 10 ) );
		line /= 10;
	} while ( line != 0U && idx < 15 );

	while ( idx-- != 0 )
	{
		_lineBuf.data()[ _lineLen++ ] = tmp.data()[ idx ];
	}
	_lineBuf.data()[ _lineLen++ ] = ' ';

	// cache the string lengths
	_tagNameLen = record.tag != nullptr ? std::strlen( record.tag ) : 0;
	_fileNameLen = record.file != nullptr ? std::strlen( record.file ) : 0;
	_funcNameLen = record.function != nullptr ? std::strlen( record.function ) : 0;
}

bool ISO8601Formatter::format( const kmac::nova::Record& record, Buffer& buffer ) noexcept
{
	if ( isPassthrough() )
	{
		return formatPassthrough( record, buffer );
	}

	if ( _stage == Stage::TimestampWithSpace )
	{
		if ( tryFormatFast( record, buffer ) )
		{
			return true;
		}
		// fall through to slow path if record doesn't fit
	}

	return formatSlow( record, buffer );
}

bool ISO8601Formatter::isPassthrough() const noexcept
{
	return _stage == Stage::TimestampWithSpace
		&& _tagNameLen == 0
		&& _fileNameLen == 0
		&& _funcNameLen == 0;
}

bool ISO8601Formatter::formatPassthrough( const kmac::nova::Record& record, Buffer& buffer ) noexcept
{
	// ULTRA-FAST PATH: raw message passthrough (when called with zero-length tag, file, and func)
	//
	// assume the message is pre-formatted, so process only the message part of the record

	// this is likely a pre-formatted message, just attempt to append it
	if ( ! buffer.append( record.message, record.messageSize ) )
	{
		return false;
	}
	_stage = Stage::Done;
	return true;
}

bool ISO8601Formatter::tryFormatFast( const kmac::nova::Record& record, Buffer& buffer ) noexcept
{
	// FAST PATH: try to write entire record in one go
	//
	// this avoids the state machine overhead for the common case where
	// the record fits entirely in the buffer
	const std::size_t totalSize =
		_timestampLen       +  // "2025-02-07T12:34:56.789Z "
		1                   +  // "["
		_tagNameLen         +  // "INFO"
		1                   +  // "]"
		1                   +  // " "
		_fileNameLen        +  // "main.cpp"
		1                   +  // ":"
		_lineLen            +  // "42 "
		_funcNameLen        +  // "main"
		3                   +  // " - "
		record.messageSize  +  // message
		1;                     // "\n"

	if ( buffer.remaining() < totalSize || totalSize > 480 )
	{
		return false;
	}

	// fast path: format into temp buffer, then single memcpy to destination,
	// which eliminates all the Buffer::append() overhead and boundary checks
	kmac::nova::platform::Array< char, 512 > tempBuf {};
	char* dest = tempBuf.data();

	// timestamp
	std::memcpy( dest, _timestampBuf.data(), _timestampLen );
	dest += _timestampLen;

	// [tag]
	*dest++ = '[';
	if ( _tagNameLen != 0 )
	{
		std::memcpy( dest, record.tag, _tagNameLen );
		dest += _tagNameLen;
	}
	*dest++ = ']';
	*dest++ = ' ';

	// file:line
	if ( _fileNameLen != 0 )
	{
		std::memcpy( dest, record.file, _fileNameLen );
		dest += _fileNameLen;
	}
	*dest++ = ':';
	std::memcpy( dest, _lineBuf.data(), _lineLen );
	dest += _lineLen;

	// function - message
	if ( _funcNameLen != 0 )
	{
		std::memcpy( dest, record.function, _funcNameLen );
		dest += _funcNameLen;
	}
	std::memcpy( dest, " - ", 3 );
	dest += 3;
	if ( record.messageSize != 0 )
	{
		std::memcpy( dest, record.message, record.messageSize );
		dest += record.messageSize;
	}
	*dest++ = '\n';

	// single append to output buffer
	const std::size_t bytesWritten = static_cast< std::size_t >( dest - tempBuf.data() );

	// safety check - should match totalSize
	if ( bytesWritten != totalSize )
	{
		// something went wrong, fall through to slow path
		_stage = Stage::TimestampWithSpace;
		return false;
	}

	(void) buffer.append( tempBuf.data(), bytesWritten );
	_stage = Stage::Done;
	return true;
}

bool ISO8601Formatter::formatSlow( const kmac::nova::Record& record, Buffer& buffer ) noexcept  // NOLINT(readability-function-cognitive-complexity)
{
	// SLOW PATH: state machine for incremental formatting
	//
	// only used for large records that don't fit in one buffer,
	// or when resuming after a partial write
	//

	while ( true )
	{
		switch ( _stage )
		{
		case Stage::TimestampWithSpace:
			if ( ! buffer.append( _timestampBuf.data(), _timestampLen ) )
			{
				return false;
			}
			_stage = Stage::OpenBracket;
			// fallthrough intentional
			[[fallthrough]];

		case Stage::OpenBracket:
			if ( ! buffer.appendChar( '[' ) )
			{
				return false;
			}
			_stage = Stage::Tag;
			// fallthrough intentional
			[[fallthrough]];

		case Stage::Tag:
			if ( ! buffer.append( record.tag, _tagNameLen ) )
			{
				return false;
			}
			_stage = Stage::CloseBracket;
			// fallthrough intentional
			[[fallthrough]];

		case Stage::CloseBracket:
			if ( ! buffer.appendChar( ']' ) )
			{
				return false;
			}
			_stage = Stage::SpaceAfterTag;
			// fallthrough intentional
			[[fallthrough]];

		case Stage::SpaceAfterTag:
			if ( ! buffer.appendChar( ' ' ) )
			{
				return false;
			}
			_stage = Stage::File;
			// fallthrough intentional
			[[fallthrough]];

		case Stage::File:
			if ( ! buffer.append( record.file, _fileNameLen ) )
			{
				return false;
			}
			_stage = Stage::Colon;
			// fallthrough intentional
			[[fallthrough]];

		case Stage::Colon:
			if ( ! buffer.appendChar( ':' ) )
			{
				return false;
			}
			_stage = Stage::LineWithSpace;
			// fallthrough intentional
			[[fallthrough]];

		case Stage::LineWithSpace:
			if ( ! buffer.append( _lineBuf.data(), _lineLen ) )
			{
				return false;
			}
			_stage = Stage::Function;
			// fallthrough intentional
			[[fallthrough]];

		case Stage::Function:
			if ( ! buffer.append( record.function, _funcNameLen ) )
			{
				return false;
			}
			_stage = Stage::Separator;
			// fallthrough intentional
			[[fallthrough]];

		case Stage::Separator:
			if ( ! buffer.appendLiteral( " - " ) )
			{
				return false;
			}
			_stage = Stage::Message;
			// fallthrough intentional
			[[fallthrough]];

		case Stage::Message:
			if ( ! buffer.append( record.message, record.messageSize ) )
			{
				return false;
			}
			_stage = Stage::Newline;
			// fallthrough intentional
			[[fallthrough]];

		case Stage::Newline:
			if ( ! buffer.appendChar( '\n' ) )
			{
				return false;
			}
			_stage = Stage::Done;
			return true;

		// if stage is already Done at this point, return true, but likely
		// indicates a missing begin() call to reset the formatter
		case Stage::Done:
			return true;
		}
	}
}

void ISO8601Formatter::buildTimestamp( std::uint64_t timestamp ) noexcept
{
	// delegates to FormattingHelper; trailing space preserved for ISO 8601 output
	char* out = _timestampBuf.data();
	out = FormattingHelper::formatTimestamp( out, timestamp );
	*out++ = ' ';
	_timestampLen = static_cast< std::size_t >( out - _timestampBuf.data() );
}

} // namespace extras
} // namespace nova
} // namespace kmac
