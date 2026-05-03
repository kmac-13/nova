#include "kmac/nova/extras/json_formatter.h"
#include "kmac/nova/extras/formatting_helper.h"

#include <cstring>

namespace kmac {
namespace nova {
namespace extras {

namespace detail {

/**
 * @brief Look-up table for hex characters 0-F.
 */
// NOLINT NOTE: 2D string literal lookup table; using std::array<std::array> would be cumbersome
static constexpr const char HEX_CHARS[ 16 ] = {  // NOLINT(cppcoreguidelines-avoid-c-arrays)
	'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'
};

} // namespace detail

void JsonFormatter::begin( const kmac::nova::Record& record ) noexcept
{
	buildTs( record.timestamp );
	buildTagId( record.tagId );
	buildLine( record.line );

	_tagIsNull = ( record.tag == nullptr );
	_fileIsNull = ( record.file == nullptr );
	_funcIsNull = ( record.function == nullptr );
	_msgIsNull = ( record.message == nullptr );

	_tagLen = _tagIsNull ? 0 : std::strlen( record.tag );
	_fileLen = _fileIsNull ? 0 : std::strlen( record.file );
	_funcLen = _funcIsNull ? 0 : std::strlen( record.function );
	_msgLen = _msgIsNull ? 0 : record.messageSize;

	_stage = Stage::Timestamp;
	_field = FieldStage::Key;
	_offset = 0;
}

bool JsonFormatter::format( const kmac::nova::Record& record, Buffer& buffer ) noexcept
{
	return formatSlow( record, buffer );
}

void JsonFormatter::buildTs( std::uint64_t timestamp ) noexcept
{
	char* out = _tsBuf.data();
	std::memcpy( out, "{\"ts\":\"", 7 );
	out += 7;
	out = FormattingHelper::formatTimestamp( out, timestamp );
	*out++ = '"';
	_tsLen = static_cast< std::size_t >( out - _tsBuf.data() );
}

void JsonFormatter::buildTagId( std::uint64_t tagId ) noexcept
{
	char* out = _tagIdBuf.data();
	std::memcpy( out, ",\"tagId\":\"", 10 );
	out += 10;
	out = FormattingHelper::formatTagId( out, tagId );
	*out++ = '"';
	_tagIdLen = static_cast< std::size_t >( out - _tagIdBuf.data() );
}

void JsonFormatter::buildLine( std::uint32_t line ) noexcept
{
	char* out = _lineBuf.data();
	std::memcpy( out, ",\"line\":", 8 );
	out += 8;
	out = FormattingHelper::formatLine( out, line );
	_lineLen = static_cast< std::size_t >( out - _lineBuf.data() );
}

bool JsonFormatter::appendEscaped(
	const char* str,
	std::size_t len,
	std::size_t& offset,
	Buffer& buffer ) noexcept
{
	while ( offset < len )
	{
		const auto ch = static_cast< unsigned char >( str[ offset ] );

		// fast path - plain ASCII that needs no escaping
		if ( ch >= 0x20 && ch != '"' && ch != '\\' )
		{
			if ( ! buffer.appendChar( static_cast< char >( ch ) ) )
			{
				return false;
			}
			++offset;
			continue;
		}

		// escape sequences
		char esc[ 6 ] = { '\\', '\0', '\0', '\0', '\0', '\0' };
		std::size_t escLen = 2;

		switch ( ch )
		{
		case '"':
			esc[ 1 ] = '"';
			break;
		case '\\':
			esc[ 1 ] = '\\';
			break;
		case '\b':
			esc[ 1 ] = 'b';
			break;
		case '\f':
			esc[ 1 ] = 'f';
			break;
		case '\n':
			esc[ 1 ] = 'n';
			break;
		case '\r':
			esc[ 1 ] = 'r';
			break;
		case '\t':
			esc[ 1 ] = 't';
			break;
		default:
			// control character U+0000-U+001F - emit \uXXXX
			esc[ 1 ] = 'u';
			esc[ 2 ] = '0';
			esc[ 3 ] = '0';
			esc[ 4 ] = detail::HEX_CHARS[ ( ch >> 4 ) & 0xFu ];
			esc[ 5 ] = detail::HEX_CHARS[ ch & 0xFu ];
			escLen = 6;
			break;
		}

		if ( ! buffer.append( static_cast< const char* >( esc ), escLen ) )  // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
		{
			return false;
		}

		++offset;
	}

	return true;
}

bool JsonFormatter::formatSlow( const kmac::nova::Record& record, Buffer& buffer ) noexcept  // NOLINT(readability-function-cognitive-complexity)
{
	switch ( _stage )  // NOLINT(hicpp-multiway-paths-covered)
	{
	case Stage::Timestamp:
		if ( ! buffer.append( _tsBuf.data(), _tsLen ) )
		{
			return false;
		}
		_stage = Stage::TagId;
		_field = FieldStage::Key;
		_offset = 0;
		// [[fallthrough]];

	case Stage::TagId:
		if ( ! buffer.append( _tagIdBuf.data(), _tagIdLen ) )
		{
			return false;
		}
		_stage = Stage::Tag;
		_field = FieldStage::Key;
		_offset = 0;
		// [[fallthrough]];

	case Stage::Tag:
		if ( ! appendField(
			",\"tag\":\"",
			",\"tag\":null",
			record.tag, _tagLen, _tagIsNull,
			buffer ) )
		{
			return false;
		}
		_stage = Stage::File;
		_field = FieldStage::Key;
		_offset = 0;
		// [[fallthrough]];

	case Stage::File:
		if ( ! appendField(
			",\"file\":\"",
			",\"file\":null",
			record.file, _fileLen, _fileIsNull,
			buffer ) )
		{
			return false;
		}
		_stage = Stage::Function;
		_field = FieldStage::Key;
		_offset = 0;
		// [[fallthrough]];

	case Stage::Function:
		if ( ! appendField(
			",\"function\":\"",
			",\"function\":null",
			record.function, _funcLen, _funcIsNull,
			buffer ) )
		{
			return false;
		}
		_stage = Stage::Line;
		_field = FieldStage::Key;
		_offset = 0;
		// [[fallthrough]];

	case Stage::Line:
		if ( ! buffer.append( _lineBuf.data(), _lineLen ) )
		{
			return false;
		}
		_stage = Stage::Message;
		_field = FieldStage::Key;
		_offset = 0;
		// [[fallthrough]];

	case Stage::Message:
		if ( ! appendField(
			",\"message\":\"",
			",\"message\":null",
			record.message, _msgLen, _msgIsNull,
			buffer ) )
		{
			return false;
		}
		_stage = Stage::Suffix;
		_field = FieldStage::Key;
		_offset = 0;
		// [[fallthrough]];

	case Stage::Suffix:
		if ( ! buffer.appendLiteral( "}\n" ) )
		{
			return false;
		}
		_stage = Stage::Done;
		// [[fallthrough]];

	case Stage::Done:
		break;
	}

	return true;
}

} // namespace extras
} // namespace nova
} // namespace kmac
