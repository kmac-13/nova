#pragma once
#ifndef KMAC_NOVA_EXTRAS_CUSTOM_FORMATTER_H
#define KMAC_NOVA_EXTRAS_CUSTOM_FORMATTER_H

#include "buffer.h"
#include "formatter.h"
#include "formatting_helper.h"

#include <kmac/nova/record.h>
#include <kmac/nova/platform/array.h>
#include <kmac/nova/platform/int_to_chars.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>

namespace kmac {
namespace nova {
namespace extras {

/**
 * @brief Selects which Record field a FieldSpec writes.
 *
 * Field::None emits only the surrounding Open/Close delimiters (no field
 * content), which is useful for inserting literal punctuation.
 */
enum class Field : std::uint8_t
{
	None,          ///< emit Open/Close only (no field content)
	Tag,           ///< record.tag (null-safe)
	Message,       ///< record.message + record.messageSize
	File,          ///< record.file (null-safe)
	Function,      ///< record.function (null-safe)
	Line,          ///< record.line (uint32 -> decimal)
	TimestampRaw,  ///< record.timestamp as raw uint64 decimal nanoseconds
	TimestampISO   ///< record.timestamp as "YYYY-MM-DDTHH:MM:SS.mmmZ"
};

/**
 * @brief Compile-time descriptor for one field in a CustomFormatter layout.
 *
 * Emits: Open character (if not '\0') + field content + Close character (if
 * not '\0').
 *
 * Examples:
 * @code
 *   FieldSpec< '[',  Field::Tag,          ']' >  // "[NETWORK]"
 *   FieldSpec< '\0', Field::TimestampISO, ' ' >  // "2025-01-01T00:00:00.000Z "
 *   FieldSpec< ' ',  Field::Message,     '\n' >  // " the message\n"
 *   FieldSpec< '|',  Field::None,         '|' >  // "||" (literal separator)
 * @endcode
 */
template< char Open, Field F, char Close >
struct FieldSpec
{
	static constexpr char open = Open;
	static constexpr Field field = F;
	static constexpr char close = Close;
};

// ---------------------------------------------------------------------------
// Internal detail helpers
// ---------------------------------------------------------------------------
namespace detail
{

// fixed byte count of an ISO 8601 UTC timestamp "YYYY-MM-DDTHH:MM:SS.mmmZ"
static constexpr std::size_t ISO_TIMESTAMP_SIZE = 24;

// maximum size of a uint64 decimal string (20 digits)
static constexpr std::size_t RAW_TIMESTAMP_SIZE = 20;

// maximum size of a uint32 line-number decimal string (10 digits)
static constexpr std::size_t LINE_NUMBER_SIZE = 10;

/**
 * @brief Fills buf[0..ISO_TIMESTAMP_SIZE) with a UTC ISO 8601 timestamp derived
 * from a nanosecond epoch value, and returns the number of bytes written
 * (always ISO_TIMESTAMP_SIZE).
 *
 * @return ISO_TIMESTAMP_SIZE
 */
inline std::size_t buildISOTimestamp( char* buf, std::uint64_t timestamp ) noexcept
{
	// delegates to FormattingHelper - writes exactly ISO_TIMESTAMP_SIZE (24) bytes
	char* end = kmac::nova::extras::FormattingHelper::formatTimestamp( buf, timestamp );
	return static_cast< std::size_t >( end - buf );
}

// ---------------------------------------------------------------------------
// buildRawTimestamp: decimal nanosecond string in buf, returns length
// ---------------------------------------------------------------------------
inline std::size_t buildRawTimestamp( char* buf, std::uint64_t timestamp ) noexcept
{
	auto result = kmac::nova::platform::intToChars( buf, buf + RAW_TIMESTAMP_SIZE, timestamp );
	if ( ! result.ok )
	{
		// should never happen with a 20-byte buffer for uint64
		buf[ 0 ] = '0';
		return 1;
	}
	return static_cast< std::size_t >( result.ptr - buf );
}

// ---------------------------------------------------------------------------
// buildLineNumber: decimal uint32 string in buf, returns length
// ---------------------------------------------------------------------------
inline std::size_t buildLineNumber( char* buf, std::uint32_t line ) noexcept
{
	auto result = kmac::nova::platform::intToChars( buf, buf + LINE_NUMBER_SIZE, line );
	if ( ! result.ok )
	{
		buf[ 0 ] = '0';
		return 1;
	}
	return static_cast< std::size_t >( result.ptr - buf );
}

inline bool writePartial( Buffer& buffer, const char* src, std::size_t totalLen, std::size_t& contentOffset ) noexcept
{
	const std::size_t delta = totalLen - contentOffset;
	const std::size_t toWrite = delta < buffer.remaining() ? delta : buffer.remaining();
	(void) buffer.append( src + contentOffset, toWrite );
	contentOffset += toWrite;
	return contentOffset >= totalLen;
}

} // namespace detail

/**
 * @brief Per-record pre-computed data populated by begin() and consumed by format().
 *
 * Stores all pre-computed data produced during begin() for one record.
 * The individual writeSpec() overloads read from this struct so that
 * format() touches no runtime arithmetic and calls only Buffer::append /
 * Buffer::appendChar.
 */
struct FieldCache
{
	// ISO or raw timestamp
	kmac::nova::platform::Array< char,
		( detail::ISO_TIMESTAMP_SIZE > detail::RAW_TIMESTAMP_SIZE
			? detail::ISO_TIMESTAMP_SIZE
			: detail::RAW_TIMESTAMP_SIZE )
		> timestampBuf = {};
	std::size_t timestampLen = 0;

	// line number decimal string
	kmac::nova::platform::Array< char, detail::LINE_NUMBER_SIZE > lineBuf = {};
	std::size_t lineLen = 0;

	// cached string lengths (avoids repeated strlen in format())
	std::size_t tagLen = 0;
	std::size_t fileLen = 0;
	std::size_t funcLen = 0;
};

/**
 * @brief Slow-path resume state.
 *
 * A format() call that returns false suspended mid-way through emitting some
 * FieldSpec at index specIdx, in sub-phase phase (0=Open, 1=Content, 2=Close).
 * On the next call format() skips all specIdx already completed and replays
 * from (specIdx, phase).
 */
struct ResumeState
{
	// index of the FieldSpec currently being written (0-based).
	std::size_t specIdx = 0;

	// sub-phase within that FieldSpec:
	//   0 = Open char
	//   1 = field content (or fully skipped for Field::None)
	//   2 = Close char
	//   3 = spec complete - move to specIdx + 1
	int phase = 0;

	// byte offset within the current variable-length content field
	// (Tag, Message, File, Function, and the pre-computed ts/line buffers)
	std::size_t contentOffset = 0;
};

template< char Open >
inline bool writeSpecPhase0( Buffer& buffer, std::size_t& contentOffset ) noexcept
{
	NOVA_IF_CONSTEXPR ( Open != '\0' )
	{
		if ( ! buffer.appendChar( Open ) )
		{
			return false;
		}
	}
	contentOffset = 0;
	return true;
}

template< Field Fld >
inline bool writeSpecPhase1(  // NOLINT(readability-function-cognitive-complexity)
	const kmac::nova::Record& record,
	const FieldCache& cache,
	Buffer& buffer,
	std::size_t& contentOffset
) noexcept
{
	NOVA_IF_CONSTEXPR ( Fld == Field::None )
	{
		// nothing to emit, fall through immediately
	}
	else NOVA_IF_CONSTEXPR ( Fld == Field::Tag )
	{
		const char* src = record.tag ? record.tag : "";
		if ( ! detail::writePartial( buffer, src, cache.tagLen, contentOffset ) )
		{
			return false;
		}
	}
	else NOVA_IF_CONSTEXPR ( Fld == Field::Message )
	{
		if ( ! detail::writePartial( buffer, record.message, record.messageSize, contentOffset ) )
		{
			return false;
		}
	}
	else NOVA_IF_CONSTEXPR ( Fld == Field::File )
	{
		const char* src = record.file ? record.file : "";
		if ( ! detail::writePartial( buffer, src, cache.fileLen, contentOffset ) )
		{
			return false;
		}
	}
	else NOVA_IF_CONSTEXPR ( Fld == Field::Function )
	{
		const char* src = record.function ? record.function : "";
		if ( ! detail::writePartial( buffer, src, cache.funcLen, contentOffset ) )
		{
			return false;
		}
	}
	else NOVA_IF_CONSTEXPR ( Fld == Field::Line )
	{
		if ( ! detail::writePartial( buffer, cache.lineBuf.data(), cache.lineLen, contentOffset ) )
		{
			return false;
		}
	}
	else NOVA_IF_CONSTEXPR ( Fld == Field::TimestampRaw || Fld == Field::TimestampISO )
	{
		if ( ! detail::writePartial( buffer, cache.timestampBuf.data(), cache.timestampLen, contentOffset ) )
		{
			return false;
		}
	}
	return true;
}

template< char Close >
inline bool writeSpecPhase2( Buffer& buffer ) noexcept
{
	NOVA_IF_CONSTEXPR ( Close != '\0' )
	{
		if ( ! buffer.appendChar( Close ) )
		{
			return false;
		}
	}
	return true;
}

/**
 * @brief Attempts to write one FieldSpec into buffer, resuming from state as needed.
 *
 * Template parameters mirror FieldSpec so the compiler can constant-fold the
 * Open/Close character comparisons and dead-strip irrelevant branches.
 *
 * @return true when this spec is fully written, false if not
 */
template< char Open, Field Fld, char Close >
inline bool writeSpec(
	const kmac::nova::Record& record,
	const FieldCache& cache,
	Buffer& buffer,
	int& phase,
	std::size_t& contentOffset
) noexcept
{
	// phase 0: Open delimiter
	if ( phase == 0 )
	{
		if ( ! writeSpecPhase0< Open >( buffer, contentOffset ) )
		{
			return false;
		}
		phase = 1;
	}

	// phase 1: Field content
	//
	// each branch writes min(fieldRemaining, bufferRemaining) bytes in one
	// append call (guaranteed to succeed by construction), advances
	// contentOffset by that amount, then returns false if the field is still
	// incomplete, which gives bulk-memcpy throughput while remaining correctly
	// resumable across buffer boundaries
	if ( phase == 1 )
	{
		if ( ! writeSpecPhase1< Fld >( record, cache, buffer, contentOffset ) )
		{
			return false;
		}
		phase = 2;
	}

	// phase 2: Close delimiter
	if ( phase == 2 )
	{
		if ( ! writeSpecPhase2< Close >( buffer ) )
		{
			return false;
		}

		// mark complete
		phase = 3;
	}

	return true;
}

/**
 * @brief Compile-time recursion over the Specs... pack.
 *
 * Advances resume.specIdx and delegates to writeSpec for the current spec.
 * Returns true when all specs are fully written.
 */
template< std::size_t SpecIdx, typename... Specs >
struct SpecWriter;

/**
 * @brief Base case - all specs written.
 */
template< std::size_t SpecIdx >
struct SpecWriter< SpecIdx >
{
	static bool write(
		const kmac::nova::Record&,
		const FieldCache&,
		Buffer&,
		ResumeState&
	) noexcept
	{
		return true;
	}
};

/**
 * @brief Recursive case - writes HeadSpec then recurses into TailSpecs.
 *
 * Skips specs already completed in a prior format() call before attempting
 * to write the current spec.
 */
template< std::size_t SpecIdx, typename HeadSpec, typename... TailSpecs >
struct SpecWriter< SpecIdx, HeadSpec, TailSpecs... >
{
	static bool write(
		const kmac::nova::Record& record,
		const FieldCache& cache,
		Buffer& buffer,
		ResumeState& resume
	) noexcept
	{
		// skip specs already completed in a prior format() call
		if ( resume.specIdx > SpecIdx )
		{
			return SpecWriter< SpecIdx + 1, TailSpecs... >::write( record, cache, buffer, resume );
		}

		// attempt to write this spec (may be mid-resume)
		const bool done = writeSpec< HeadSpec::open, HeadSpec::field, HeadSpec::close >(
			record, cache, buffer, resume.phase, resume.contentOffset );

		if ( ! done )
		{
			return false;
		}

		// advance to next spec
		resume.specIdx = SpecIdx + 1;
		resume.phase   = 0;
		resume.contentOffset = 0;

		return SpecWriter< SpecIdx + 1, TailSpecs... >::write( record, cache, buffer, resume );
	}
};

/**
 * @brief Compile-time predicate: true if any Spec in the pack uses Field Target.
 */
template< Field Target, typename... Specs >
struct HasField : std::false_type {};

template< Field Target, char Opn, Field Fld, char Cls, typename... Rest >
struct HasField< Target, FieldSpec< Opn, Fld, Cls >, Rest... >
	: std::conditional_t< Fld == Target, std::true_type, HasField< Target, Rest... > > {};

/**
 * @brief Compile-time dispatch to populate only the FieldCache members
 * actually needed by the Specs pack, avoiding unnecessary work in begin().
 */
template< typename... Specs >
void fillCache( FieldCache& cache, const kmac::nova::Record& record ) noexcept
{
	NOVA_IF_CONSTEXPR ( HasField< Field::TimestampISO, Specs... >::value )
	{
		cache.timestampLen = detail::buildISOTimestamp( cache.timestampBuf.data(), record.timestamp );
	}
	else NOVA_IF_CONSTEXPR ( HasField< Field::TimestampRaw, Specs... >::value )
	{
		cache.timestampLen = detail::buildRawTimestamp( cache.timestampBuf.data(), record.timestamp );
	}

	NOVA_IF_CONSTEXPR ( HasField< Field::Line, Specs... >::value )
	{
		cache.lineLen = detail::buildLineNumber( cache.lineBuf.data(), record.line );
	}

	NOVA_IF_CONSTEXPR ( HasField< Field::Tag, Specs... >::value )
	{
		cache.tagLen = record.tag ? std::strlen( record.tag ) : 0;
	}

	NOVA_IF_CONSTEXPR ( HasField< Field::File, Specs... >::value )
	{
		cache.fileLen = record.file ? std::strlen( record.file ) : 0;
	}

	NOVA_IF_CONSTEXPR ( HasField< Field::Function, Specs... >::value )
	{
		cache.funcLen = record.function ? std::strlen( record.function ) : 0;
	}
}

// ---------------------------------------------------------------------------
// CustomFormatter
// ---------------------------------------------------------------------------

/**
 * @brief Compile-time configurable log formatter.
 *
 * ✅ SAFE FOR SAFETY-CRITICAL SYSTEMS
 *
 * CustomFormatter assembles a log line from an ordered sequence of FieldSpec
 * descriptors, each of which emits an optional open delimiter, an optional
 * record field, and an optional close delimiter.  The entire layout is
 * resolved at compile time, producing zero-overhead branching in the hot path.
 *
 * ### Layout specification
 *
 * A layout is a comma-separated list of FieldSpec template arguments:
 *
 * @code
 * using MyFormatter = CustomFormatter<
 *     FieldSpec< '\0', Field::TimestampISO, ' '  >,   // "2025-01-01T00:00:00.000Z "
 *     FieldSpec< '[',  Field::Tag,          ']'  >,   // "[INFO]"
 *     FieldSpec< ' ',  Field::Message,      '\n' >    // " the message\n"
 * >;
 * @endcode
 *
 * A '\0' Open or Close means no delimiter is emitted for that side.
 * Field::None emits only delimiters (useful for literal separators):
 *
 * @code
 *     FieldSpec< ' ', Field::None, '-' >   // " -"
 *     FieldSpec< ' ', Field::None, ' ' >   // " "
 * @endcode
 *
 * ### Available fields
 *
 * | Field               | Content                                    |
 * |---------------------|--------------------------------------------|
 * | Field::Tag          | record.tag (null-safe, empty if null)      |
 * | Field::Message      | record.message (messageSize bytes)         |
 * | Field::File         | record.file (null-safe, empty if null)     |
 * | Field::Function     | record.function (null-safe, empty if null) |
 * | Field::Line         | record.line as decimal integer             |
 * | Field::TimestampRaw | record.timestamp as decimal nanoseconds    |
 * | Field::TimestampISO | record.timestamp as ISO 8601 UTC string    |
 * | Field::None         | (no content, delimiters only)              |
 *
 * ### Formatting model
 *
 * `begin()` is called once per record.  It pre-computes the ISO/raw timestamp
 * string, the line-number decimal string, and the string lengths of tag, file,
 * and function, but only for the fields actually present in the layout.
 *
 * `format()` then writes fields into the caller-provided Buffer.  If the buffer
 * fills before all fields are written, format() returns false and suspends at
 * its current position.  FormattingSink calls format() again with a fresh
 * buffer; the formatter resumes exactly where it left off.
 *
 * ### Overflow behaviour
 *
 * Partial output in the log is more informative than a truncation marker.
 * If a record is too large for a single buffer pass, it is emitted as
 * consecutive buffer-sized chunks.  In debug builds, begin() asserts if called
 * before the previous record completed (indicating a missing final format()
 * call, a programming error in the driving code).
 *
 * ### Thread safety
 *
 * Not thread-safe.  Each thread must own its own CustomFormatter instance.
 *
 * ### Example
 *
 * @code
 * // "[INFO] 2025-01-01T00:00:00.000Z main.cpp:42 myFunc - hello\n"
 * using MyFormatter = CustomFormatter<
 *     FieldSpec< '[',  Field::Tag,          ']'  >,
 *     FieldSpec< ' ',  Field::TimestampISO, ' '  >,
 *     FieldSpec< '\0', Field::File,         ':'  >,
 *     FieldSpec< '\0', Field::Line,         ' '  >,
 *     FieldSpec< '\0', Field::Function,     '\0' >,
 *     FieldSpec< ' ',  Field::None,         '-'  >,
 *     FieldSpec< ' ',  Field::Message,      '\n' >
 * >;
 *
 * MyFormatter formatter;
 * FormattingSink< 4096 > sink( downstream, formatter );
 * @endcode
 *
 * @tparam Specs one or more FieldSpec< Open, Field, Close > template arguments
 *   describing the output layout in emission order.
 */
template< typename... Specs >
class CustomFormatter final : public Formatter
{
private:
	FieldCache  _cache;
	ResumeState _resume;

	// true when begin() was called but format() has not yet returned true,
	// used in the debug-build assert to catch missing format() completions
	bool _inProgress = false;

public:
	/**
	 * @brief Pre-computes all per-record metadata before format() is called.
	 *
	 * Only computes what the layout actually uses (timestamps, line number,
	 * string lengths).  Resets the resume state so format() starts at the
	 * first FieldSpec.
	 *
	 * Must be called exactly once per record, before any format() calls.
	 *
	 * @param record the record about to be formatted
	 */
	void begin( const kmac::nova::Record& record ) noexcept override;

	/**
	 * @brief Writes as much of the formatted record as fits into @p buffer.
	 *
	 * Returns true when the record is complete, false if the buffer filled
	 * before all FieldSpecs were written.  In the false case the formatter
	 * suspends and resumes on the next call from exactly where it left off.
	 *
	 * @param record the record being formatted (same instance passed to begin())
	 * @param buffer destination buffer
	 * @return true if the record is fully written, false if more buffer space is needed
	 */
	bool format( const kmac::nova::Record& record, Buffer& buffer ) noexcept override;
};

template< typename... Specs >
void CustomFormatter< Specs... >::begin( const kmac::nova::Record& record ) noexcept
{
	// detect missing format() completion from the previous record
	assert( ! _inProgress && "CustomFormatter: begin() called before previous record completed" );

	_inProgress = true;

	fillCache< Specs... >( _cache, record );

	_resume.specIdx = 0;
	_resume.phase = 0;
	_resume.contentOffset = 0;
}

template< typename... Specs >
bool CustomFormatter< Specs... >::format( const kmac::nova::Record& record, Buffer& buffer ) noexcept
{
	const bool done = SpecWriter< 0, Specs... >::write( record, _cache, buffer, _resume );

	if ( done )
	{
		_inProgress = false;
	}

	return done;
}

} // namespace extras
} // namespace nova
} // namespace kmac

#endif // KMAC_NOVA_EXTRAS_CUSTOM_FORMATTER_H
