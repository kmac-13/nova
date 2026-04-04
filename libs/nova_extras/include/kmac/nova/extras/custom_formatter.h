#pragma once
#ifndef KMAC_NOVA_EXTRAS_CUSTOM_FORMATTER_H
#define KMAC_NOVA_EXTRAS_CUSTOM_FORMATTER_H

#include "buffer.h"
#include "formatter.h"

#include "kmac/nova/record.h"
#include "kmac/nova/platform/array.h"
#include "kmac/nova/platform/int_to_chars.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>

namespace kmac::nova::extras
{

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

// lookup table for 2-digit strings "00".."99", shared with ISO8601Formatter
static constexpr const char DIGITS_2[ 100 ][ 3 ] = {  // NOLINT(cppcoreguidelines-avoid-c-arrays)
	"00", "01", "02", "03", "04", "05", "06", "07", "08", "09",
	"10", "11", "12", "13", "14", "15", "16", "17", "18", "19",
	"20", "21", "22", "23", "24", "25", "26", "27", "28", "29",
	"30", "31", "32", "33", "34", "35", "36", "37", "38", "39",
	"40", "41", "42", "43", "44", "45", "46", "47", "48", "49",
	"50", "51", "52", "53", "54", "55", "56", "57", "58", "59",
	"60", "61", "62", "63", "64", "65", "66", "67", "68", "69",
	"70", "71", "72", "73", "74", "75", "76", "77", "78", "79",
	"80", "81", "82", "83", "84", "85", "86", "87", "88", "89",
	"90", "91", "92", "93", "94", "95", "96", "97", "98", "99"
};

// lookup table for 3-digit strings "000".."999"
static constexpr const char DIGITS_3[ 1000 ][ 4 ] = {  // NOLINT(cppcoreguidelines-avoid-c-arrays)
	"000", "001", "002", "003", "004", "005", "006", "007", "008", "009",
	"010", "011", "012", "013", "014", "015", "016", "017", "018", "019",
	"020", "021", "022", "023", "024", "025", "026", "027", "028", "029",
	"030", "031", "032", "033", "034", "035", "036", "037", "038", "039",
	"040", "041", "042", "043", "044", "045", "046", "047", "048", "049",
	"050", "051", "052", "053", "054", "055", "056", "057", "058", "059",
	"060", "061", "062", "063", "064", "065", "066", "067", "068", "069",
	"070", "071", "072", "073", "074", "075", "076", "077", "078", "079",
	"080", "081", "082", "083", "084", "085", "086", "087", "088", "089",
	"090", "091", "092", "093", "094", "095", "096", "097", "098", "099",
	"100", "101", "102", "103", "104", "105", "106", "107", "108", "109",
	"110", "111", "112", "113", "114", "115", "116", "117", "118", "119",
	"120", "121", "122", "123", "124", "125", "126", "127", "128", "129",
	"130", "131", "132", "133", "134", "135", "136", "137", "138", "139",
	"140", "141", "142", "143", "144", "145", "146", "147", "148", "149",
	"150", "151", "152", "153", "154", "155", "156", "157", "158", "159",
	"160", "161", "162", "163", "164", "165", "166", "167", "168", "169",
	"170", "171", "172", "173", "174", "175", "176", "177", "178", "179",
	"180", "181", "182", "183", "184", "185", "186", "187", "188", "189",
	"190", "191", "192", "193", "194", "195", "196", "197", "198", "199",
	"200", "201", "202", "203", "204", "205", "206", "207", "208", "209",
	"210", "211", "212", "213", "214", "215", "216", "217", "218", "219",
	"220", "221", "222", "223", "224", "225", "226", "227", "228", "229",
	"230", "231", "232", "233", "234", "235", "236", "237", "238", "239",
	"240", "241", "242", "243", "244", "245", "246", "247", "248", "249",
	"250", "251", "252", "253", "254", "255", "256", "257", "258", "259",
	"260", "261", "262", "263", "264", "265", "266", "267", "268", "269",
	"270", "271", "272", "273", "274", "275", "276", "277", "278", "279",
	"280", "281", "282", "283", "284", "285", "286", "287", "288", "289",
	"290", "291", "292", "293", "294", "295", "296", "297", "298", "299",
	"300", "301", "302", "303", "304", "305", "306", "307", "308", "309",
	"310", "311", "312", "313", "314", "315", "316", "317", "318", "319",
	"320", "321", "322", "323", "324", "325", "326", "327", "328", "329",
	"330", "331", "332", "333", "334", "335", "336", "337", "338", "339",
	"340", "341", "342", "343", "344", "345", "346", "347", "348", "349",
	"350", "351", "352", "353", "354", "355", "356", "357", "358", "359",
	"360", "361", "362", "363", "364", "365", "366", "367", "368", "369",
	"370", "371", "372", "373", "374", "375", "376", "377", "378", "379",
	"380", "381", "382", "383", "384", "385", "386", "387", "388", "389",
	"390", "391", "392", "393", "394", "395", "396", "397", "398", "399",
	"400", "401", "402", "403", "404", "405", "406", "407", "408", "409",
	"410", "411", "412", "413", "414", "415", "416", "417", "418", "419",
	"420", "421", "422", "423", "424", "425", "426", "427", "428", "429",
	"430", "431", "432", "433", "434", "435", "436", "437", "438", "439",
	"440", "441", "442", "443", "444", "445", "446", "447", "448", "449",
	"450", "451", "452", "453", "454", "455", "456", "457", "458", "459",
	"460", "461", "462", "463", "464", "465", "466", "467", "468", "469",
	"470", "471", "472", "473", "474", "475", "476", "477", "478", "479",
	"480", "481", "482", "483", "484", "485", "486", "487", "488", "489",
	"490", "491", "492", "493", "494", "495", "496", "497", "498", "499",
	"500", "501", "502", "503", "504", "505", "506", "507", "508", "509",
	"510", "511", "512", "513", "514", "515", "516", "517", "518", "519",
	"520", "521", "522", "523", "524", "525", "526", "527", "528", "529",
	"530", "531", "532", "533", "534", "535", "536", "537", "538", "539",
	"540", "541", "542", "543", "544", "545", "546", "547", "548", "549",
	"550", "551", "552", "553", "554", "555", "556", "557", "558", "559",
	"560", "561", "562", "563", "564", "565", "566", "567", "568", "569",
	"570", "571", "572", "573", "574", "575", "576", "577", "578", "579",
	"580", "581", "582", "583", "584", "585", "586", "587", "588", "589",
	"590", "591", "592", "593", "594", "595", "596", "597", "598", "599",
	"600", "601", "602", "603", "604", "605", "606", "607", "608", "609",
	"610", "611", "612", "613", "614", "615", "616", "617", "618", "619",
	"620", "621", "622", "623", "624", "625", "626", "627", "628", "629",
	"630", "631", "632", "633", "634", "635", "636", "637", "638", "639",
	"640", "641", "642", "643", "644", "645", "646", "647", "648", "649",
	"650", "651", "652", "653", "654", "655", "656", "657", "658", "659",
	"660", "661", "662", "663", "664", "665", "666", "667", "668", "669",
	"670", "671", "672", "673", "674", "675", "676", "677", "678", "679",
	"680", "681", "682", "683", "684", "685", "686", "687", "688", "689",
	"690", "691", "692", "693", "694", "695", "696", "697", "698", "699",
	"700", "701", "702", "703", "704", "705", "706", "707", "708", "709",
	"710", "711", "712", "713", "714", "715", "716", "717", "718", "719",
	"720", "721", "722", "723", "724", "725", "726", "727", "728", "729",
	"730", "731", "732", "733", "734", "735", "736", "737", "738", "739",
	"740", "741", "742", "743", "744", "745", "746", "747", "748", "749",
	"750", "751", "752", "753", "754", "755", "756", "757", "758", "759",
	"760", "761", "762", "763", "764", "765", "766", "767", "768", "769",
	"770", "771", "772", "773", "774", "775", "776", "777", "778", "779",
	"780", "781", "782", "783", "784", "785", "786", "787", "788", "789",
	"790", "791", "792", "793", "794", "795", "796", "797", "798", "799",
	"800", "801", "802", "803", "804", "805", "806", "807", "808", "809",
	"810", "811", "812", "813", "814", "815", "816", "817", "818", "819",
	"820", "821", "822", "823", "824", "825", "826", "827", "828", "829",
	"830", "831", "832", "833", "834", "835", "836", "837", "838", "839",
	"840", "841", "842", "843", "844", "845", "846", "847", "848", "849",
	"850", "851", "852", "853", "854", "855", "856", "857", "858", "859",
	"860", "861", "862", "863", "864", "865", "866", "867", "868", "869",
	"870", "871", "872", "873", "874", "875", "876", "877", "878", "879",
	"880", "881", "882", "883", "884", "885", "886", "887", "888", "889",
	"890", "891", "892", "893", "894", "895", "896", "897", "898", "899",
	"900", "901", "902", "903", "904", "905", "906", "907", "908", "909",
	"910", "911", "912", "913", "914", "915", "916", "917", "918", "919",
	"920", "921", "922", "923", "924", "925", "926", "927", "928", "929",
	"930", "931", "932", "933", "934", "935", "936", "937", "938", "939",
	"940", "941", "942", "943", "944", "945", "946", "947", "948", "949",
	"950", "951", "952", "953", "954", "955", "956", "957", "958", "959",
	"960", "961", "962", "963", "964", "965", "966", "967", "968", "969",
	"970", "971", "972", "973", "974", "975", "976", "977", "978", "979",
	"980", "981", "982", "983", "984", "985", "986", "987", "988", "989",
	"990", "991", "992", "993", "994", "995", "996", "997", "998", "999"
};

// ---------------------------------------------------------------------------
// pre-computed per-record data stored in begin() and consumed by format(),
// each Field type caches only what it actually needs.
// ---------------------------------------------------------------------------

// maximum size of a formatted ISO 8601 timestamp: "YYYY-MM-DDTHH:MM:SS.mmmZ" = 24 chars
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
// NOLINTBEGIN(cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-pro-bounds-constant-array-index)
inline std::size_t buildISOTimestamp( char* buf, std::uint64_t timestamp ) noexcept
{
	const std::uint64_t seconds = timestamp / 1'000'000'000ULL;
	const std::uint64_t millis = ( timestamp / 1'000'000ULL ) % 1000ULL;

	const std::time_t timeVal = static_cast< std::time_t >( seconds );
	std::tm time {};

#if defined( _WIN32 )
	gmtime_s( &time, &timeVal );
#else
	gmtime_r( &timeVal, &time );
#endif

	char* out = buf;

	// YYYY-MM-DD
	const int year = 1900 + time.tm_year;
	out[ 0 ] = char( '0' + ( year / 1000 ) );
	out[ 1 ] = char( '0' + ( ( year / 100 ) % 10 ) );
	out[ 2 ] = char( '0' + ( ( year / 10  ) % 10 ) );
	out[ 3 ] = char( '0' + ( year % 10 ) );
	out += 4;
	*out++ = '-';
	std::memcpy( out, DIGITS_2[ time.tm_mon + 1 ], 2 );
	out += 2;
	*out++ = '-';
	std::memcpy( out, DIGITS_2[ time.tm_mday ], 2 );
	out += 2;

	// THH:MM:SS.mmmZ
	*out++ = 'T';
	std::memcpy( out, DIGITS_2[ time.tm_hour ], 2 );
	out += 2;
	*out++ = ':';
	std::memcpy( out, DIGITS_2[ time.tm_min  ], 2 );
	out += 2;
	*out++ = ':';
	std::memcpy( out, DIGITS_2[ time.tm_sec  ], 2 );
	out += 2;
	*out++ = '.';
	std::memcpy( out, DIGITS_3[ millis ], 3 );
	out += 3;
	*out++ = 'Z';

	return static_cast< std::size_t >( out - buf ); // always ISO_TIMESTAMP_SIZE
}
// NOLINTEND(cppcoreguidelines-pro-bounds-array-to-pointer-decay, cppcoreguidelines-pro-bounds-constant-array-index)

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
	if constexpr ( Open != '\0' )
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
	if constexpr ( Fld == Field::None )
	{
		// nothing to emit, fall through immediately
	}
	else if constexpr ( Fld == Field::Tag )
	{
		const char* src = record.tag ? record.tag : "";
		if ( ! detail::writePartial( buffer, src, cache.tagLen, contentOffset ) )
		{
			return false;
		}
	}
	else if constexpr ( Fld == Field::Message )
	{
		if ( ! detail::writePartial( buffer, record.message, record.messageSize, contentOffset ) )
		{
			return false;
		}
	}
	else if constexpr ( Fld == Field::File )
	{
		const char* src = record.file ? record.file : "";
		if ( ! detail::writePartial( buffer, src, cache.fileLen, contentOffset ) )
		{
			return false;
		}
	}
	else if constexpr ( Fld == Field::Function )
	{
		const char* src = record.function ? record.function : "";
		if ( ! detail::writePartial( buffer, src, cache.funcLen, contentOffset ) )
		{
			return false;
		}
	}
	else if constexpr ( Fld == Field::Line )
	{
		if ( ! detail::writePartial( buffer, cache.lineBuf.data(), cache.lineLen, contentOffset ) )
		{
			return false;
		}
	}
	else if constexpr ( Fld == Field::TimestampRaw || Fld == Field::TimestampISO )
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
	if constexpr ( Close != '\0' )
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
	if constexpr ( HasField< Field::TimestampISO, Specs... >::value )
	{
		cache.timestampLen = detail::buildISOTimestamp( cache.timestampBuf.data(), record.timestamp );
	}
	else if constexpr ( HasField< Field::TimestampRaw, Specs... >::value )
	{
		cache.timestampLen = detail::buildRawTimestamp( cache.timestampBuf.data(), record.timestamp );
	}

	if constexpr ( HasField< Field::Line, Specs... >::value )
	{
		cache.lineLen = detail::buildLineNumber( cache.lineBuf.data(), record.line );
	}

	if constexpr ( HasField< Field::Tag, Specs... >::value )
	{
		cache.tagLen = record.tag ? std::strlen( record.tag ) : 0;
	}

	if constexpr ( HasField< Field::File, Specs... >::value )
	{
		cache.fileLen = record.file ? std::strlen( record.file ) : 0;
	}

	if constexpr ( HasField< Field::Function, Specs... >::value )
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

} // namespace kmac::nova::extras

#endif // KMAC_NOVA_EXTRAS_CUSTOM_FORMATTER_H
