#include "kmac/nova/extras/csv_formatter.h"
#include "kmac/nova/extras/formatting_helper.h"

#include <cstring>

namespace kmac {
namespace nova {
namespace extras {

CsvFormatter::CsvFormatter( char delimiter ) noexcept
	: _delimiter( delimiter )
{
}

void CsvFormatter::begin( const kmac::nova::Record& record ) noexcept
{
	buildTimestamp( record.timestamp );
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

	// null fields produce unquoted empty fields - no quoting check needed
	_tagNeedsQuoting = ! _tagIsNull && needsQuoting( record.tag, _tagLen );
	_fileNeedsQuoting = ! _fileIsNull && needsQuoting( record.file, _fileLen );
	_funcNeedsQuoting = ! _funcIsNull && needsQuoting( record.function, _funcLen );
	_msgNeedsQuoting = ! _msgIsNull && needsQuoting( record.message, _msgLen );

	_stage = Stage::Timestamp;
	_field = FieldStage::Delimiter;
	_offset = 0;
}

bool CsvFormatter::format( const kmac::nova::Record& record, Buffer& buffer ) noexcept
{
	return formatSlow( record, buffer );
}

void CsvFormatter::buildTimestamp( std::uint64_t timestamp ) noexcept
{
	char* out = _timestampBuf.data();
	out = FormattingHelper::formatTimestamp( out, timestamp );
	_timestampLen = static_cast< std::size_t >( out - _timestampBuf.data() );
}

void CsvFormatter::buildTagId( std::uint64_t tagId ) noexcept
{
	char* out = _tagIdBuf.data();
	out = FormattingHelper::formatTagId( out, tagId );
	_tagIdLen = static_cast< std::size_t >( out - _tagIdBuf.data() );
}

void CsvFormatter::buildLine( std::uint32_t line ) noexcept
{
	char* out = _lineBuf.data();
	char* start = out;
	out = FormattingHelper::formatLine( out, line );
	_lineLen = static_cast< std::size_t >( out - start );
}

bool CsvFormatter::needsQuoting( const char* str, std::size_t len ) const noexcept
{
	for ( std::size_t i = 0; i < len; ++i )
	{
		const char chr = str[ i ];
		if ( chr == _delimiter || chr == '"' || chr == '\r' || chr == '\n' )
		{
			return true;
		}
	}

	return false;
}

bool CsvFormatter::appendField(  // NOLINT(readability-function-cognitive-complexity)
	const char* str,
	std::size_t len,
	bool isNull,
	bool quoting,
	Buffer& buffer ) noexcept
{
	switch ( _field )
	{
	case FieldStage::Delimiter:
		if ( ! buffer.appendChar( _delimiter ) )
		{
			return false;
		}
		if ( isNull )
		{
			return true;
		}
		if ( len == 0 )
		{
			// empty non-null field - always quoted to distinguish from null
			return buffer.append( "\"\"", 2 );
		}
		if ( quoting )
		{
			if ( ! buffer.appendChar( '"' ) )
			{
				return false;
			}
		}
		_field = FieldStage::Content;
		_offset = 0;
		// [[fallthrough]];

	case FieldStage::Content:
		if ( ! writeQuotedContent( str, len, buffer ) )
		{
			return false;
		}
		if ( ! quoting )
		{
			return true;
		}
		_field = FieldStage::CloseQuote;
		// [[fallthrough]];

	case FieldStage::CloseQuote:
		if ( ! buffer.appendChar( '"' ) )
		{
			return false;
		}
		break;
	}

	return true;
}

bool CsvFormatter::writeQuotedContent(
	const char* str,
	std::size_t len,
	Buffer& buffer ) noexcept
{
	while ( _offset < len )
	{
		if ( str[ _offset ] == '"' )
		{
			if ( ! buffer.append( "\"\"", 2 ) )
			{
				return false;
			}
		}
		else
		{
			if ( ! buffer.appendChar( str[ _offset ] ) )
			{
				return false;
			}
		}
		++_offset;
	}

	return true;
}

bool CsvFormatter::formatSlow( const kmac::nova::Record& record, Buffer& buffer ) noexcept  // NOLINT(readability-function-cognitive-complexity)
{
	switch ( _stage )  // NOLINT(hicpp-multiway-paths-covered)
	{
	case Stage::Timestamp:
		if ( ! buffer.append( _timestampBuf.data(), _timestampLen ) )
		{
			return false;
		}
		_stage = Stage::TagId;
		_field = FieldStage::Delimiter;
		_offset = 0;
		// [[fallthrough]];

	case Stage::TagId:
		if ( ! buffer.appendChar( _delimiter ) )
		{
			return false;
		}
		if ( ! buffer.append( _tagIdBuf.data(), _tagIdLen ) )
		{
			return false;
		}
		_stage = Stage::Tag;
		_field = FieldStage::Delimiter;
		_offset = 0;
		// [[fallthrough]];

	case Stage::Tag:
		if ( ! appendField( record.tag, _tagLen, _tagIsNull, _tagNeedsQuoting, buffer ) )
		{
			return false;
		}
		_stage = Stage::File;
		_field = FieldStage::Delimiter;
		_offset = 0;
		// [[fallthrough]];

	case Stage::File:
		if ( ! appendField( record.file, _fileLen, _fileIsNull, _fileNeedsQuoting, buffer ) )
		{
			return false;
		}
		_stage = Stage::Function;
		_field = FieldStage::Delimiter;
		_offset = 0;
		// [[fallthrough]];

	case Stage::Function:
		if ( ! appendField( record.function, _funcLen, _funcIsNull, _funcNeedsQuoting, buffer ) )
		{
			return false;
		}
		_stage = Stage::Line;
		_field = FieldStage::Delimiter;
		_offset = 0;
		// [[fallthrough]];

	case Stage::Line:
		if ( ! buffer.appendChar( _delimiter ) )
		{
			return false;
		}
		if ( ! buffer.append( _lineBuf.data(), _lineLen ) )
		{
			return false;
		}
		_stage = Stage::Message;
		_field = FieldStage::Delimiter;
		_offset = 0;
		// [[fallthrough]];

	case Stage::Message:
		if ( ! appendField( record.message, _msgLen, _msgIsNull, _msgNeedsQuoting, buffer ) )
		{
			return false;
		}
		_stage = Stage::Crlf;
		_field = FieldStage::Delimiter;
		_offset = 0;
		// [[fallthrough]];

	case Stage::Crlf:
		if ( ! buffer.appendLiteral( "\r\n" ) )
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
