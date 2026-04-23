/**
 * @file fuzz_record_builder.cpp
 * @brief LibFuzzer target for TruncatingRecordBuilder.
 *
 * Feeds arbitrary fuzzer-supplied bytes into TruncatingRecordBuilder via the
 * public StackTruncBuilderWrapper, exercising:
 *   - truncation boundary logic (USABLE_SIZE - _offset clamping)
 *   - the null-termination and "..." marker appended on truncation
 *   - commit() path: Record construction and sink delivery
 *   - operator<< overloads for StringView and char
 *
 * The fuzzer input is split in half: the first half is appended as a
 * StringView (bulk copy path), the second half one byte at a time via the
 * char overload.  This gives the fuzzer two independent surfaces to explore
 * with a single input.
 *
 * A 64-byte buffer is used so truncation is triggered frequently even with
 * short fuzzer inputs, keeping coverage of the truncation path high.
 *
 * A NullSink discards delivered records; the interesting behaviour is the
 * builder's internal state transitions, not the sink.
 */

#include "kmac/nova/nova.h"
#include "kmac/nova/scoped_configurator.h"
#include "kmac/nova/sink.h"
#include "kmac/nova/truncating_logging.h"

#include <cstddef>
#include <cstdint>

// ============================================================================
// Minimal sink - discards all records
// ============================================================================

namespace
{

class NullSink : public kmac::nova::Sink
{
public:
	void process( const kmac::nova::Record& /*record*/ ) noexcept override {}
};

struct FuzzTag {};
static std::uint64_t fuzzTimestamp() noexcept { return 0ULL; }

} // namespace

NOVA_LOGGER_TRAITS( FuzzTag, FUZZ_RECORD_BUILDER, true, fuzzTimestamp );

// ============================================================================
// Fuzzer entry point
// ============================================================================

extern "C" int LLVMFuzzerTestOneInput( const uint8_t* data, size_t size )
{
	static NullSink sink;
	static kmac::nova::ScopedConfigurator config;

	static bool bound = false;
	if ( ! bound )
	{
		config.bind< FuzzTag >( &sink );
		bound = true;
	}

	// StackTruncBuilderWrapper is the public-facing RAII wrapper around TruncatingRecordBuilder,
	// constructor calls setContext<Tag>(); destructor calls commit(), which delivers the Record
	// to the bound sink
	//
	// a 64-byte buffer is specified so truncation is triggered even on short
	// inputs; the default (1024 bytes) would rarely truncate with -max_len=1024
	{
		kmac::nova::StackTruncBuilderWrapper< FuzzTag, 64 > builder(
			__FILE__, __func__, static_cast< std::uint32_t >( __LINE__ ) );

		const size_t mid = size / 2;

		// first half: StringView with explicit length - embedded nulls in the
		// fuzzer input are not silently truncated (unlike the const char* overload
		// which uses strlen internally)
		if ( mid > 0 )
		{
			builder << kmac::nova::platform::StringView(
				reinterpret_cast< const char* >( data ), mid );
		}

		// second half: char-by-char to exercise single-char boundary checks
		for ( size_t i = mid; i < size; ++i )
		{
			builder << static_cast< char >( data[ i ] );
		}
	} // destructor calls commit() here

	return 0;
}
