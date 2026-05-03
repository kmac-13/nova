#include "kmac/nova/extras/xml_formatter.h"
#include "kmac/nova/extras/formatting_helper.h"

#include <cstring>

namespace kmac {
namespace nova {
namespace extras {

void XmlFormatter::begin( const kmac::nova::Record& record ) noexcept
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

	_stage = Stage::OpenRecord;
	_field = FieldStage::OpenTag;
	_offset = 0;
}

bool XmlFormatter::format( const kmac::nova::Record& record, Buffer& buffer ) noexcept
{
	return formatSlow( record, buffer );
}

void XmlFormatter::buildTs( std::uint64_t timestamp ) noexcept
{
	char* out = _tsBuf.data();
	std::memcpy( out, "<ts>", 4 );
	out += 4;
	out = FormattingHelper::formatTimestamp( out, timestamp );
	std::memcpy( out, "</ts>", 5 );
	out += 5;
	_tsLen = static_cast< std::size_t >( out - _tsBuf.data() );
}

void XmlFormatter::buildTagId( std::uint64_t tagId ) noexcept
{
	char* out = _tagIdBuf.data();
	std::memcpy( out, "<tagId>", 7 );
	out += 7;
	out = FormattingHelper::formatTagId( out, tagId );
	std::memcpy( out, "</tagId>", 8 );
	out += 8;
	_tagIdLen = static_cast< std::size_t >( out - _tagIdBuf.data() );
}

void XmlFormatter::buildLine( std::uint32_t line ) noexcept
{
	char* out = _lineBuf.data();
	char* start = out;
	std::memcpy( out, "<line>", 6 );
	out += 6;
	out = FormattingHelper::formatLine( out, line );
	std::memcpy( out, "</line>", 7 );
	out += 7;
	_lineLen = static_cast< std::size_t >( out - start );
}

bool XmlFormatter::appendEscaped(  // NOLINT(readability-function-cognitive-complexity)
	const char* content,
	std::size_t len,
	std::size_t& offset,
	Buffer& buffer ) noexcept
{
	while ( offset < len )
	{
		const auto byte = static_cast< unsigned char >( content[ offset ] );

		// pass through: plain ASCII with no special XML meaning, plus tab/LF/CR
		// which are legal XML 1.0 element content characters
		const bool isPassThrough = ( byte >= 0x09 && byte <= 0x0D )
			|| ( byte > 0x0D && byte != '&' && byte != '<' && byte != '>' );

		if ( isPassThrough )
		{
			if ( ! buffer.appendChar( static_cast< char >( byte ) ) )
			{
				return false;
			}
			++offset;
			continue;
		}

		// XML entity references for characters requiring escaping
		const char* entity = nullptr;
		std::size_t entityLen = 0;

		switch ( byte )
		{
		case '&':
			entity = "&amp;";
			entityLen = 5;
			break;
		case '<':
			entity = "&lt;";
			entityLen = 4;
			break;
		case '>':
			entity = "&gt;";
			entityLen = 4;
			break;
		default:
			// control character U+0000-U+0008, U+000B-U+000C, U+000E-U+001F
			// illegal in XML 1.0 and cannot be represented as a numeric
			// character reference; drop silently
			++offset;
			continue;
		}

		if ( ! buffer.append( entity, entityLen ) )
		{
			return false;
		}

		++offset;
	}

	return true;
}

bool XmlFormatter::appendElement(
	const char* openTag,
	std::size_t openTagLen,
	const char* closeTag,
	std::size_t closeTagLen,
	const char* selfClose,
	std::size_t selfCloseLen,
	const char* content,
	std::size_t len,
	Buffer& buffer ) noexcept
{
	if ( len == 0 )
	{
		// empty string - emit self-closing element in one shot
		return buffer.append( selfClose, selfCloseLen );
	}

	switch ( _field )
	{
	case FieldStage::OpenTag:
		if ( ! buffer.append( openTag, openTagLen ) )
		{
			return false;
		}
		_field = FieldStage::Content;
		_offset = 0;
		// [[fallthrough]];

	case FieldStage::Content:
		if ( ! appendEscaped( content, len, _offset, buffer ) )
		{
			return false;
		}
		_field = FieldStage::CloseTag;
		// [[fallthrough]];

	case FieldStage::CloseTag:
		if ( ! buffer.append( closeTag, closeTagLen ) )
		{
			return false;
		}
		break;
	}

	return true;
}

bool XmlFormatter::formatSlow( const kmac::nova::Record& record, Buffer& buffer ) noexcept  // NOLINT(readability-function-cognitive-complexity)
{
	switch ( _stage )  // NOLINT(hicpp-multiway-paths-covered)
	{
	case Stage::OpenRecord:
		if ( ! buffer.appendLiteral( "<record>" ) )
		{
			return false;
		}
		_stage = Stage::Ts;
		_field = FieldStage::OpenTag;
		_offset = 0;
		// [[fallthrough]];

	case Stage::Ts:
		if ( ! buffer.append( _tsBuf.data(), _tsLen ) )
		{
			return false;
		}
		_stage = Stage::TagId;
		_field = FieldStage::OpenTag;
		_offset = 0;
		// [[fallthrough]];

	case Stage::TagId:
		if ( ! buffer.append( _tagIdBuf.data(), _tagIdLen ) )
		{
			return false;
		}
		_stage = Stage::Tag;
		_field = FieldStage::OpenTag;
		_offset = 0;
		// [[fallthrough]];

	case Stage::Tag:
		if ( ! _tagIsNull )
		{
			if ( ! appendElement(
				"<tag>",  5,
				"</tag>", 6,
				"<tag/>", 6,
				record.tag, _tagLen,
				buffer ) )
			{
				return false;
			}
		}
		_stage = Stage::File;
		_field = FieldStage::OpenTag;
		_offset = 0;
		// [[fallthrough]];

	case Stage::File:
		if ( ! _fileIsNull )
		{
			if ( ! appendElement(
				"<file>",  6,
				"</file>", 7,
				"<file/>", 7,
				record.file, _fileLen,
				buffer ) )
			{
				return false;
			}
		}
		_stage = Stage::Function;
		_field = FieldStage::OpenTag;
		_offset = 0;
		// [[fallthrough]];

	case Stage::Function:
		if ( ! _funcIsNull )
		{
			if ( ! appendElement(
				"<function>",  10,
				"</function>", 11,
				"<function/>", 11,
				record.function, _funcLen,
				buffer ) )
			{
				return false;
			}
		}
		_stage = Stage::Line;
		_field = FieldStage::OpenTag;
		_offset = 0;
		// [[fallthrough]];

	case Stage::Line:
		if ( ! buffer.append( _lineBuf.data(), _lineLen ) )
		{
			return false;
		}
		_stage = Stage::Message;
		_field = FieldStage::OpenTag;
		_offset = 0;
		// [[fallthrough]];

	case Stage::Message:
		if ( ! _msgIsNull )
		{
			if ( ! appendElement(
				"<message>",  9,
				"</message>", 10,
				"<message/>", 10,
				record.message, _msgLen,
				buffer ) )
			{
				return false;
			}
		}
		_stage = Stage::CloseRecord;
		_field = FieldStage::OpenTag;
		_offset = 0;
		// [[fallthrough]];

	case Stage::CloseRecord:
		if ( ! buffer.appendLiteral( "</record>\n" ) )
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
