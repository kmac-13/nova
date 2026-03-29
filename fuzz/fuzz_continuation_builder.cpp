/**
 * @file fuzz_continuation_builder.cpp
 * @brief LibFuzzer target for ContinuationRecordBuilder.
 *
 * Feeds arbitrary fuzzer-supplied bytes into ContinuationRecordBuilder via
 * the public StackContinuationBuilder wrapper, exercising:
 *   - continuation emission: when the 64-byte buffer fills, commitCurrent()
 *     emits a partial record and resumes into a fresh buffer
 *   - the "[cont] " prefix prepended to continuation records
 *   - commit() flushing a partially-filled final buffer
 *   - operator<< overloads for StringView and char
 *
 * The fuzzer input is split in half: first half via StringView (bulk copy
 * and continuation boundary), second half char-by-char (fill-and-flush one
 * byte at a time).
 *
 * A 64-byte buffer ensures continuations are triggered frequently even with
 * short inputs; the seed corpus contains a 200-byte input that forces ~4
 * continuation records immediately.
 */

#include "kmac/nova/nova.h"
#include "kmac/nova/scoped_configurator.h"
#include "kmac/nova/sink.h"
#include "kmac/nova/extras/continuation_logging.h"

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

struct FuzzContTag {};
static std::uint64_t fuzzTimestamp() noexcept { return 0ULL; }

} // namespace

NOVA_LOGGER_TRAITS( FuzzContTag, FUZZ_CONT_BUILDER, true, fuzzTimestamp );

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
		config.bind< FuzzContTag >( &sink );
		bound = true;
	}

	// StackContinuationBuilder is the public RAII wrapper around ContinuationRecordBuilder,
	// constructor calls setContext<Tag>(); destructor calls commit(), which flushes any
	// remaining buffered content
	//
	// a 64-byte buffer ensures continuation records are emitted frequently
	{
		kmac::nova::StackContinuationBuilder< FuzzContTag, 64 > builder(
			__FILE__, __func__, static_cast< std::uint32_t >( __LINE__ ) );

		const size_t mid = size / 2;

		// first half: StringView with explicit length so embedded nulls in the
		// fuzzer input are passed through rather than terminating early
		if ( mid > 0 )
		{
			builder << kmac::nova::platform::StringView(
				reinterpret_cast< const char* >( data ), mid );
		}

		// second half: char-by-char to exercise the per-byte fill-and-flush path
		for ( size_t i = mid; i < size; ++i )
		{
			builder << static_cast< char >( data[ i ] );
		}
	} // destructor calls commit() here

	return 0;
}
