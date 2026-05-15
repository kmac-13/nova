/**
 * @file test_nova_composite.cpp
 * @brief Google Test unit tests for Nova composite sinks
 */

#include "kmac/nova.h"
#include "kmac/nova/extras/bounded_composite_sink.h"
#include "kmac/nova/extras/composite_sink.h"
#include "kmac/nova/extras/fixed_composite_sink.h"
#include "kmac/nova/extras/ostream_sink.h"

#include <gtest/gtest.h>

#include <atomic>
#include <sstream>
#include <vector>

// test tag
struct CompositeTag { };
std::uint64_t tagTimestamp() noexcept { return 88888ULL; }
NOVA_LOGGER_TRAITS( CompositeTag, COMP, true, tagTimestamp );


// Counter sink for testing
class CounterSink : public kmac::nova::Sink
{
public:
	std::atomic< size_t > count { 0 };

	void process( const kmac::nova::Record& ) noexcept override
	{
		++count;
	}
};

class NovaComposite : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F( NovaComposite, CompositeSinkEmpty )
{
	kmac::nova::extras::CompositeSink compositeSink;

	kmac::nova::ScopedConfigurator config;
	config.bind< CompositeTag >( &compositeSink );

	// logging to empty composite should not crash
	NOVA_LOG( CompositeTag ) << "test";

	SUCCEED();
}

TEST_F( NovaComposite, CompositeSinkSingleChild )
{
	CounterSink counter;
	kmac::nova::extras::CompositeSink compositeSink;
	compositeSink.add( counter );

	kmac::nova::ScopedConfigurator config;
	config.bind< CompositeTag >( &compositeSink );

	NOVA_LOG( CompositeTag ) << "test";

	EXPECT_EQ( counter.count.load(), 1u );
}

TEST_F( NovaComposite, CompositeSinkMultipleChildren )
{
	CounterSink counter1;
	CounterSink counter2;
	CounterSink counter3;

	kmac::nova::extras::CompositeSink compositeSink;
	compositeSink.add( counter1 );
	compositeSink.add( counter2 );
	compositeSink.add( counter3 );

	kmac::nova::ScopedConfigurator config;
	config.bind< CompositeTag >( &compositeSink );

	NOVA_LOG( CompositeTag ) << "broadcast";

	EXPECT_EQ( counter1.count.load(), 1u );
	EXPECT_EQ( counter2.count.load(), 1u );
	EXPECT_EQ( counter3.count.load(), 1u );
}

TEST_F( NovaComposite, CompositeSinkMultipleMessages )
{
	CounterSink counter1;
	CounterSink counter2;

	kmac::nova::extras::CompositeSink compositeSink;
	compositeSink.add( counter1 );
	compositeSink.add( counter2 );

	kmac::nova::ScopedConfigurator config;
	config.bind< CompositeTag >( &compositeSink );

	for ( int i = 0; i < 10; ++i )
	{
		NOVA_LOG( CompositeTag ) << "message " << i;
	}

	EXPECT_EQ( counter1.count.load(), 10u );
	EXPECT_EQ( counter2.count.load(), 10u );
}

TEST_F( NovaComposite, CompositeSinkRemoveSink )
{
	CounterSink counter1;
	CounterSink counter2;

	kmac::nova::extras::BoundedCompositeSink< 2 > compositeSink;
	(void) compositeSink.add( counter1 );
	(void) compositeSink.add( counter2 );

	kmac::nova::ScopedConfigurator config;
	config.bind< CompositeTag >( &compositeSink );

	NOVA_LOG( CompositeTag ) << "both";

	EXPECT_EQ( counter1.count.load(), 1u );
	EXPECT_EQ( counter2.count.load(), 1u );

	(void) compositeSink.remove( counter1 );

	NOVA_LOG( CompositeTag ) << "one only";

	EXPECT_EQ( counter1.count.load(), 1u );  // still 1
	EXPECT_EQ( counter2.count.load(), 2u );  // now 2
}

TEST_F( NovaComposite, CompositeSinkClearSinks )
{
	CounterSink counter1;
	CounterSink counter2;

	kmac::nova::extras::CompositeSink compositeSink;
	compositeSink.add( counter1 );
	compositeSink.add( counter2 );

	kmac::nova::ScopedConfigurator config;
	config.bind< CompositeTag >( &compositeSink );

	NOVA_LOG( CompositeTag ) << "before clear";

	EXPECT_EQ( counter1.count.load(), 1u );
	EXPECT_EQ( counter2.count.load(), 1u );

	compositeSink.clear();

	NOVA_LOG( CompositeTag ) << "after clear";

	// counts should remain at 1
	EXPECT_EQ( counter1.count.load(), 1u );
	EXPECT_EQ( counter2.count.load(), 1u );
}

TEST_F( NovaComposite, CompositeSinkWithOStreams )
{
	std::ostringstream oss1;
	std::ostringstream oss2;

	kmac::nova::extras::OStreamSink sink1( oss1 );
	kmac::nova::extras::OStreamSink sink2( oss2 );

	kmac::nova::extras::CompositeSink compositeSink;
	compositeSink.add( sink1 );
	compositeSink.add( sink2 );

	kmac::nova::ScopedConfigurator config;
	config.bind< CompositeTag >( &compositeSink );

	NOVA_LOG( CompositeTag ) << "dual output";

	std::string output1 = std::string( oss1.str() );
	std::string output2 = std::string( oss2.str() );

	EXPECT_TRUE( ( output1.find( "dual output" ) != std::string::npos ) );
	EXPECT_TRUE( ( output2.find( "dual output" ) != std::string::npos ) );
}

TEST_F( NovaComposite, FixedCompositeSinkBasic )
{
	CounterSink counter1;
	CounterSink counter2;

	kmac::nova::Sink* sinks[] = { &counter1, &counter2 };
	kmac::nova::extras::FixedCompositeSink fixedSink( sinks, 2 );

	kmac::nova::ScopedConfigurator config;
	config.bind< CompositeTag >( &fixedSink );

	NOVA_LOG( CompositeTag ) << "fixed test";

	EXPECT_EQ( counter1.count.load(), 1u );
	EXPECT_EQ( counter2.count.load(), 1u );
}

TEST_F( NovaComposite, FixedCompositeSinkLargeSize )
{
	std::vector< CounterSink > counters( 10 );
	std::vector< kmac::nova::Sink* > sinks( 10 );

	for ( size_t i = 0; i < 10; ++i )
	{
		sinks[ i ] = &counters[ i ];
	}

	kmac::nova::extras::FixedCompositeSink fixedSink( sinks.data(), sinks.size() );

	kmac::nova::ScopedConfigurator config;
	config.bind< CompositeTag >( &fixedSink );

	NOVA_LOG( CompositeTag ) << "many sinks";

	for ( size_t i = 0; i < 10; ++i )
	{
		EXPECT_EQ( counters[ i ].count.load(), 1u );
	}
}

TEST_F( NovaComposite, FixedCompositeSinkPartialFill )
{
	CounterSink counter1;

	// create array with one sink and rest nullptr
	kmac::nova::Sink* sinks[ 5 ] = { &counter1, nullptr, nullptr, nullptr, nullptr };
	kmac::nova::extras::FixedCompositeSink fixedSink( sinks, 5 );

	kmac::nova::ScopedConfigurator config;
	config.bind< CompositeTag >( &fixedSink );

	NOVA_LOG( CompositeTag ) << "partial test";

	EXPECT_EQ( counter1.count.load(), 1u );
}

TEST_F( NovaComposite, NestedCompositeSinks )
{
	CounterSink counter1;
	CounterSink counter2;
	CounterSink counter3;

	kmac::nova::extras::CompositeSink inner;
	inner.add( counter1 );
	inner.add( counter2 );

	kmac::nova::extras::CompositeSink outer;
	outer.add( inner );
	outer.add( counter3 );

	kmac::nova::ScopedConfigurator config;
	config.bind< CompositeTag >( &outer );

	NOVA_LOG( CompositeTag ) << "nested";

	// inner composite should forward to counter1 and counter2
	EXPECT_EQ( counter1.count.load(), 1u );
	EXPECT_EQ( counter2.count.load(), 1u );
	// outer composite should also send to counter3
	EXPECT_EQ( counter3.count.load(), 1u );
}

TEST_F( NovaComposite, CompositeSinkDuplicateSink )
{
	CounterSink counter;

	kmac::nova::extras::CompositeSink compositeSink;
	compositeSink.add( counter );
	compositeSink.add( counter );  // add same sink twice

	kmac::nova::ScopedConfigurator config;
	config.bind< CompositeTag >( &compositeSink );

	NOVA_LOG( CompositeTag ) << "duplicate";

	// counter should receive message twice
	EXPECT_EQ( counter.count.load(), 2u );
}

TEST_F( NovaComposite, CompositeSinkReuse )
{
	CounterSink counter1;
	CounterSink counter2;

	kmac::nova::extras::CompositeSink compositeSink;

	{
		kmac::nova::ScopedConfigurator config;
		config.bind< CompositeTag >( &compositeSink );

		compositeSink.add( counter1 );
		NOVA_LOG( CompositeTag ) << "first phase";
		EXPECT_EQ( counter1.count.load(), 1u );
	}

	{
		kmac::nova::ScopedConfigurator config;
		config.bind< CompositeTag >( &compositeSink );

		compositeSink.add( counter2 );
		NOVA_LOG( CompositeTag ) << "second phase";

		// counter1 should now have 2 (from both phases)
		// counter2 should have 1 (from second phase only)
		EXPECT_EQ( counter1.count.load(), 2u );
		EXPECT_EQ( counter2.count.load(), 1u );
	}
}

// ============================================================================
// BoundedCompositeSink - contains(), return values, capacity
// ============================================================================

TEST_F( NovaComposite, BoundedContainsReturnsFalseWhenEmpty )
{
	kmac::nova::extras::BoundedCompositeSink< 4 > sink;
	CounterSink counter;

	EXPECT_FALSE( sink.contains( counter ) );
}

TEST_F( NovaComposite, BoundedContainsReturnsTrueAfterAdd )
{
	kmac::nova::extras::BoundedCompositeSink< 4 > sink;
	CounterSink counter;

	sink.add( counter );

	EXPECT_TRUE( sink.contains( counter ) );
}

TEST_F( NovaComposite, BoundedContainsReturnsFalseAfterRemove )
{
	kmac::nova::extras::BoundedCompositeSink< 4 > sink;
	CounterSink counter;

	sink.add( counter );
	sink.remove( counter );

	EXPECT_FALSE( sink.contains( counter ) );
}

TEST_F( NovaComposite, BoundedContainsDistinguishesBetweenSinks )
{
	kmac::nova::extras::BoundedCompositeSink< 4 > sink;
	CounterSink a;
	CounterSink b;
	CounterSink c;

	sink.add( a );
	sink.add( b );

	EXPECT_TRUE( sink.contains( a ) );
	EXPECT_TRUE( sink.contains( b ) );
	EXPECT_FALSE( sink.contains( c ) );
}

TEST_F( NovaComposite, BoundedAddReturnsTrueOnSuccess )
{
	kmac::nova::extras::BoundedCompositeSink< 4 > sink;
	CounterSink counter;

	EXPECT_TRUE( sink.add( counter ) );
}

TEST_F( NovaComposite, BoundedAddReturnsFalseAtCapacity )
{
	kmac::nova::extras::BoundedCompositeSink< 2 > sink;
	CounterSink a;
	CounterSink b;
	CounterSink c;

	EXPECT_TRUE( sink.add( a ) );
	EXPECT_TRUE( sink.add( b ) );
	EXPECT_FALSE( sink.add( c ) );

	// size must be capped at capacity
	EXPECT_EQ( sink.size(), 2u );
	EXPECT_EQ( sink.capacity(), 2u );
}

TEST_F( NovaComposite, BoundedAddReturnsFalseForDuplicate )
{
	// add() rejects a sink that is already present - prevents duplicate dispatch
	kmac::nova::extras::BoundedCompositeSink< 4 > sink;
	CounterSink counter;

	EXPECT_TRUE( sink.add( counter ) );
	EXPECT_FALSE( sink.add( counter ) );

	// size must remain 1
	EXPECT_EQ( sink.size(), 1u );
}

TEST_F( NovaComposite, BoundedRemoveReturnsTrueOnSuccess )
{
	kmac::nova::extras::BoundedCompositeSink< 4 > sink;
	CounterSink counter;

	sink.add( counter );
	EXPECT_TRUE( sink.remove( counter ) );
	EXPECT_EQ( sink.size(), 0u );
}

TEST_F( NovaComposite, BoundedRemoveReturnsFalseWhenNotPresent )
{
	kmac::nova::extras::BoundedCompositeSink< 4 > sink;
	CounterSink counter;

	EXPECT_FALSE( sink.remove( counter ) );
}

TEST_F( NovaComposite, BoundedSizeAndCapacityAccessors )
{
	kmac::nova::extras::BoundedCompositeSink< 3 > sink;
	CounterSink a;
	CounterSink b;

	EXPECT_EQ( sink.size(), 0u );
	EXPECT_EQ( sink.capacity(), 3u );

	sink.add( a );
	EXPECT_EQ( sink.size(), 1u );

	sink.add( b );
	EXPECT_EQ( sink.size(), 2u );

	sink.remove( a );
	EXPECT_EQ( sink.size(), 1u );
}

TEST_F( NovaComposite, BoundedAtCapacityAddThenRemoveThenAdd )
{
	// fill to capacity, remove one, verify a new add succeeds
	kmac::nova::extras::BoundedCompositeSink< 2 > sink;
	CounterSink a;
	CounterSink b;
	CounterSink c;

	sink.add( a );
	sink.add( b );
	EXPECT_FALSE( sink.add( c ) );

	sink.remove( a );
	EXPECT_TRUE( sink.add( c ) );

	// b and c should now receive records
	kmac::nova::ScopedConfigurator<> config;
	config.bind< CompositeTag >( &sink );

	NOVA_LOG( CompositeTag ) << "after swap";

	EXPECT_EQ( b.count.load(), 1u );
	EXPECT_EQ( c.count.load(), 1u );
	EXPECT_EQ( a.count.load(), 0u );
}

TEST_F( NovaComposite, BoundedMiddleRemovePreservesOrder )
{
	// removing a middle sink must shift remaining sinks and preserve dispatch order
	kmac::nova::extras::BoundedCompositeSink< 3 > sink;

	std::vector< int > order;
	class OrderedSink : public kmac::nova::Sink
	{
	public:
		int _id;
		std::vector< int >& _order;
		OrderedSink( int id, std::vector< int >& o ) : _id( id ), _order( o ) {}
		void process( const kmac::nova::Record& ) noexcept override { _order.push_back( _id ); }
	};

	OrderedSink s1( 1, order );
	OrderedSink s2( 2, order );
	OrderedSink s3( 3, order );

	sink.add( s1 );
	sink.add( s2 );
	sink.add( s3 );
	sink.remove( s2 );

	kmac::nova::ScopedConfigurator<> config;
	config.bind< CompositeTag >( &sink );
	NOVA_LOG( CompositeTag ) << "order test";

	// s1 and s3 must still fire in original insertion order
	ASSERT_EQ( order.size(), 2u );
	EXPECT_EQ( order[ 0 ], 1 );
	EXPECT_EQ( order[ 1 ], 3 );
}
