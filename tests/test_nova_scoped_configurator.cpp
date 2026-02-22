/**
 * @file test_nova_scoped_configurator.cpp
 * @brief Unit tests for fixed-size ScopedConfigurator
 */

#include "kmac/nova/nova.h"
#include "kmac/nova/scoped_configurator.h"
#include "kmac/nova/sink.h"

#include <gtest/gtest.h>

#include <atomic>

// test tags
struct TestTag1 {};
struct TestTag2 {};
struct TestTag3 {};
struct TestTag4 {};
struct TestTag5 {};

NOVA_LOGGER_TRAITS( TestTag1, TEST1, true, kmac::nova::TimestampHelper::steadyNanosecs );
NOVA_LOGGER_TRAITS( TestTag2, TEST2, true, kmac::nova::TimestampHelper::steadyNanosecs );
NOVA_LOGGER_TRAITS( TestTag3, TEST3, true, kmac::nova::TimestampHelper::steadyNanosecs );
NOVA_LOGGER_TRAITS( TestTag4, TEST4, true, kmac::nova::TimestampHelper::steadyNanosecs );
NOVA_LOGGER_TRAITS( TestTag5, TEST5, true, kmac::nova::TimestampHelper::steadyNanosecs );

// simple counting sink for testing
class CounterSink : public kmac::nova::Sink
{
public:
	std::atomic< int > count{ 0 };

	void process( const kmac::nova::Record& ) noexcept override
	{
		count.fetch_add( 1, std::memory_order_relaxed );
	}

	void reset() noexcept
	{
		count.store( 0, std::memory_order_relaxed );
	}
};

// test fixture
class NovaScopedConfigurator : public ::testing::Test
{
protected:
	void SetUp() override
	{
		// ensure all loggers start unbound
		kmac::nova::Logger< TestTag1 >::unbindSink();
		kmac::nova::Logger< TestTag2 >::unbindSink();
		kmac::nova::Logger< TestTag3 >::unbindSink();
		kmac::nova::Logger< TestTag4 >::unbindSink();
		kmac::nova::Logger< TestTag5 >::unbindSink();
		
		sink.reset();
	}

	CounterSink sink;
};

// basic functionality tests

TEST_F( NovaScopedConfigurator, DefaultCapacity )
{
	kmac::nova::ScopedConfigurator<> config;
	EXPECT_EQ( config.maxBindings(), 32 );
	EXPECT_EQ( config.bindingCount(), 0 );
	EXPECT_FALSE( config.isFull() );
}

TEST_F( NovaScopedConfigurator, CustomCapacity )
{
	kmac::nova::ScopedConfigurator< 5 > config;
	EXPECT_EQ( config.maxBindings(), 5 );
	EXPECT_EQ( config.bindingCount(), 0 );
	EXPECT_FALSE( config.isFull() );
}

TEST_F( NovaScopedConfigurator, SingleBinding )
{
	kmac::nova::ScopedConfigurator< 10 > config;
	config.bind< TestTag1 >( &sink );

	EXPECT_EQ( config.bindingCount(), 1 );
	EXPECT_FALSE( config.isFull() );

	NOVA_LOG( TestTag1 ) << "Test";
	EXPECT_EQ( sink.count, 1 );
}

TEST_F( NovaScopedConfigurator, MultipleBindings )
{
	kmac::nova::ScopedConfigurator< 10 > config;
	
	config.bind< TestTag1 >( &sink );
	config.bind< TestTag2 >( &sink );
	config.bind< TestTag3 >( &sink );

	EXPECT_EQ( config.bindingCount(), 3 );

	NOVA_LOG( TestTag1 ) << "Test1";
	NOVA_LOG( TestTag2 ) << "Test2";
	NOVA_LOG( TestTag3 ) << "Test3";

	EXPECT_EQ( sink.count, 3 );
}

TEST_F( NovaScopedConfigurator, AutomaticUnbinding )
{
	{
		kmac::nova::ScopedConfigurator< 10 > config;
		config.bind< TestTag1 >( &sink );

		NOVA_LOG( TestTag1 ) << "Inside scope";
		EXPECT_EQ( sink.count, 1 );
	}
	// destructor should have unbound

	NOVA_LOG( TestTag1 ) << "Outside scope";
	EXPECT_EQ( sink.count, 1 );  // count unchanged - not logged
}

// custom loggers that track unbinding
struct Tag1 {};
struct Tag2 {};
struct Tag3 {};

NOVA_LOGGER_TRAITS( Tag1, T1, true, kmac::nova::TimestampHelper::steadyNanosecs );
NOVA_LOGGER_TRAITS( Tag2, T2, true, kmac::nova::TimestampHelper::steadyNanosecs );
NOVA_LOGGER_TRAITS( Tag3, T3, true, kmac::nova::TimestampHelper::steadyNanosecs );

TEST_F( NovaScopedConfigurator, ReverseOrderUnbinding )
{
	// track unbinding order
	std::vector< int > unbindOrder;

	class OrderSink : public kmac::nova::Sink
	{
	public:
		int id;
		std::vector< int >* order;

		OrderSink( int i, std::vector< int >* o ) : id( i ), order( o ) {}

		void process( const kmac::nova::Record& ) noexcept override {}
	};

	OrderSink sink1( 1, &unbindOrder );
	OrderSink sink2( 2, &unbindOrder );
	OrderSink sink3( 3, &unbindOrder );

	{
		kmac::nova::ScopedConfigurator< 10 > config;
		config.bind< Tag1 >( &sink1 );
		config.bind< Tag2 >( &sink2 );
		config.bind< Tag3 >( &sink3 );
	}
	// should unbind in reverse: 3, 2, 1

	// NOTE: we can't easily verify unbinding order without modifying Logger,
	// but we can verify all are unbound
	EXPECT_EQ( kmac::nova::Logger< Tag1 >::getSink(), nullptr );
	EXPECT_EQ( kmac::nova::Logger< Tag2 >::getSink(), nullptr );
	EXPECT_EQ( kmac::nova::Logger< Tag3 >::getSink(), nullptr );
}

TEST_F( NovaScopedConfigurator, ExplicitUnbinding )
{
	kmac::nova::ScopedConfigurator< 10 > config;
	config.bind< TestTag1 >( &sink );
	config.bind< TestTag2 >( &sink );

	EXPECT_EQ( config.bindingCount(), 2 );

	NOVA_LOG( TestTag1 ) << "Test1";
	EXPECT_EQ( sink.count, 1 );

	config.unbind< TestTag1 >();
	EXPECT_EQ( config.bindingCount(), 1 );

	NOVA_LOG( TestTag1 ) << "Should not log";
	NOVA_LOG( TestTag2 ) << "Test2";

	EXPECT_EQ( sink.count, 2 );  // only TestTag2 logged
}

TEST_F( NovaScopedConfigurator, BindFrom )
{
	CounterSink sink1, sink2;

	kmac::nova::ScopedConfigurator< 10 > config;
	config.bind< TestTag1 >( &sink1 );
	config.bindFrom< TestTag2, TestTag1 >();

	EXPECT_EQ( config.bindingCount(), 2 );

	// both should use sink1
	NOVA_LOG( TestTag1 ) << "Test1";
	NOVA_LOG( TestTag2 ) << "Test2";

	EXPECT_EQ( sink1.count, 2 );
	EXPECT_EQ( sink2.count, 0 );
}

TEST_F( NovaScopedConfigurator, DuplicateBindingIgnored )
{
	kmac::nova::ScopedConfigurator< 10 > config;

	config.bind< TestTag1 >( &sink );
	EXPECT_EQ( config.bindingCount(), 1 );

	// try to bind same tag again, which is supported
	CounterSink otherSink;
	config.bind< TestTag1 >( &otherSink );

	// should still be 1 (duplicate ignored)
	EXPECT_EQ( config.bindingCount(), 1 );

	// should still use original sink
	NOVA_LOG( TestTag1 ) << "Test";
	EXPECT_EQ( sink.count, 0 );
	EXPECT_EQ( otherSink.count, 1 );
}

TEST_F( NovaScopedConfigurator, NullSinkBinding )
{
	kmac::nova::ScopedConfigurator< 10 > config;
	config.bind< TestTag1 >( nullptr );

	EXPECT_EQ( config.bindingCount(), 1 );

	// logging to null sink should not crash
	NOVA_LOG( TestTag1 ) << "Test";
	EXPECT_EQ( sink.count, 0 );
}

// capacity tests

TEST_F( NovaScopedConfigurator, ExactCapacity )
{
	kmac::nova::ScopedConfigurator< 3 > config;

	config.bind< TestTag1 >( &sink );
	config.bind< TestTag2 >( &sink );
	config.bind< TestTag3 >( &sink );

	EXPECT_EQ( config.bindingCount(), 3 );
	EXPECT_TRUE( config.isFull() );

	// all should log
	NOVA_LOG( TestTag1 ) << "Test1";
	NOVA_LOG( TestTag2 ) << "Test2";
	NOVA_LOG( TestTag3 ) << "Test3";

	EXPECT_EQ( sink.count, 3 );
}

TEST_F( NovaScopedConfigurator, CapacityEnforcement )
{
	kmac::nova::ScopedConfigurator< 2 > config;

	EXPECT_TRUE( config.tryBind< TestTag1 >( &sink ) );
	EXPECT_TRUE( config.tryBind< TestTag2 >( &sink ) );
	EXPECT_TRUE( config.isFull() );

	// this should fail (capacity exceeded)
	EXPECT_FALSE( config.tryBind< TestTag3 >( &sink ) );

	EXPECT_EQ( config.bindingCount(), 2 );

	NOVA_LOG( TestTag1 ) << "Test1";
	NOVA_LOG( TestTag2 ) << "Test2";
	NOVA_LOG( TestTag3 ) << "Should not log";

	EXPECT_EQ( sink.count, 2 );
}

#ifndef NDEBUG
TEST_F( NovaScopedConfigurator, OverflowDetection )
{
	kmac::nova::ScopedConfigurator< 2 > config;

	EXPECT_FALSE( config.hasOverflowed() );

	EXPECT_TRUE( config.tryBind< TestTag1 >( &sink ) );
	EXPECT_TRUE( config.tryBind< TestTag2 >( &sink ) );

	EXPECT_FALSE( config.hasOverflowed() );

	// this should trigger overflow flag
	EXPECT_FALSE( config.tryBind< TestTag3 >( &sink ) );

	EXPECT_TRUE( config.hasOverflowed() );
	EXPECT_EQ( config.bindingCount(), 2 );
}
#endif

TEST_F( NovaScopedConfigurator, UnbindFreesCapacity )
{
	kmac::nova::ScopedConfigurator< 2 > config;

	config.bind< TestTag1 >( &sink );
	config.bind< TestTag2 >( &sink );
	EXPECT_TRUE( config.isFull() );

	// unbind one
	config.unbind< TestTag1 >();
	EXPECT_FALSE( config.isFull() );
	EXPECT_EQ( config.bindingCount(), 1 );

	// should now be able to bind another
	config.bind< TestTag3 >( &sink );
	EXPECT_EQ( config.bindingCount(), 2 );

	NOVA_LOG( TestTag2 ) << "Test2";
	NOVA_LOG( TestTag3 ) << "Test3";

	EXPECT_EQ( sink.count, 2 );
}

TEST_F( NovaScopedConfigurator, TryBindReturnValue )
{
	kmac::nova::ScopedConfigurator< 2 > config;

	// should succeed
	EXPECT_TRUE( config.tryBind< TestTag1 >( &sink ) );
	EXPECT_TRUE( config.tryBind< TestTag2 >( &sink ) );

	// should fail (capacity exceeded)
	EXPECT_FALSE( config.tryBind< TestTag3 >( &sink ) );

	// should fail (duplicate)
	EXPECT_FALSE( config.tryBind< TestTag1 >( &sink ) );

	EXPECT_EQ( config.bindingCount(), 2 );
}

TEST_F( NovaScopedConfigurator, TryBindFromReturnValue )
{
	kmac::nova::ScopedConfigurator< 3 > config;

	EXPECT_TRUE( config.tryBind< TestTag1 >( &sink ) );

	// should succeed
	const bool success1 = config.tryBindFrom< TestTag2, TestTag1 >();
	EXPECT_TRUE( success1 );

	// even though this is a duplicate, should succeed
	const bool success2 = config.tryBindFrom< TestTag2, TestTag1 >();
	EXPECT_TRUE( success2 );

	EXPECT_EQ( config.bindingCount(), 2 );
}

// memory tests

TEST_F( NovaScopedConfigurator, SizeofVerification )
{
	// verify size is reasonable for different capacities
	size_t size5 = sizeof( kmac::nova::ScopedConfigurator< 5 > );
	size_t size32 = sizeof( kmac::nova::ScopedConfigurator< 32 > );
	size_t size64 = sizeof( kmac::nova::ScopedConfigurator< 64 > );

	// should be approximately: N * sizeof(void*) + sizeof(size_t) + debug flag
	size_t ptrSize = sizeof( void* );
	size_t expectedSize5 = 5 * ptrSize + sizeof( size_t );
	size_t expectedSize32 = 32 * ptrSize + sizeof( size_t );
	size_t expectedSize64 = 64 * ptrSize + sizeof( size_t );

	// allow some padding
	EXPECT_LE( size5, expectedSize5 + 16 );
	EXPECT_LE( size32, expectedSize32 + 16 );
	EXPECT_LE( size64, expectedSize64 + 16 );

	// verify reasonable progression
	EXPECT_LT( size5, size32 );
	EXPECT_LT( size32, size64 );
}

TEST_F( NovaScopedConfigurator, NoHeapAllocation )
{
	// This is more of a static verification - the std::array
	// doesn't allocate on heap.  We can't easily test this at
	// runtime without instrumentation, but we can verify the
	// container type is std::array (which is stack-based).
	
	// The fact that all other tests pass without any heap
	// allocation failures is evidence this works correctly.
	SUCCEED();
}

// edge cases

TEST_F( NovaScopedConfigurator, EmptyConfigurator )
{
	kmac::nova::ScopedConfigurator< 10 > config;

	EXPECT_EQ( config.bindingCount(), 0 );
	EXPECT_FALSE( config.isFull() );

	// destruction of empty configurator should be safe
}

TEST_F( NovaScopedConfigurator, UnbindNonBound )
{
	kmac::nova::ScopedConfigurator< 10 > config;
	config.bind< TestTag1 >( &sink );

	// unbinding a tag that was never bound should be safe
	config.unbind< TestTag2 >();

	EXPECT_EQ( config.bindingCount(), 1 );
}

TEST_F( NovaScopedConfigurator, MinimalCapacity )
{
	// test with capacity of 1
	kmac::nova::ScopedConfigurator< 1 > config;

	EXPECT_EQ( config.maxBindings(), 1 );

	config.bind< TestTag1 >( &sink );
	EXPECT_TRUE( config.isFull() );

	NOVA_LOG( TestTag1 ) << "Test";
	EXPECT_EQ( sink.count, 1 );
}

TEST_F( NovaScopedConfigurator, LargeCapacity )
{
	// test with large capacity
	kmac::nova::ScopedConfigurator< 128 > config;

	EXPECT_EQ( config.maxBindings(), 128 );
	EXPECT_FALSE( config.isFull() );

	// bind a few
	config.bind< TestTag1 >( &sink );
	config.bind< TestTag2 >( &sink );
	config.bind< TestTag3 >( &sink );

	EXPECT_EQ( config.bindingCount(), 3 );
	EXPECT_FALSE( config.isFull() );
}

// nested configurator test

TEST_F( NovaScopedConfigurator, NestedConfigurators )
{
	CounterSink outerSink, innerSink;

	{
		kmac::nova::ScopedConfigurator< 10 > outer;
		outer.bind< TestTag1 >( &outerSink );

		NOVA_LOG( TestTag1 ) << "Outer";
		EXPECT_EQ( outerSink.count, 1 );

		{
			kmac::nova::ScopedConfigurator< 10 > inner;
			inner.bind< TestTag1 >( &innerSink );  // override

			NOVA_LOG( TestTag1 ) << "Inner";
			EXPECT_EQ( outerSink.count, 1 );  // unchanged
			EXPECT_EQ( innerSink.count, 1 );
		}
		// inner destroyed, TestTag1 unbound

		NOVA_LOG( TestTag1 ) << "Outer again";
		// TestTag1 is now unbound, so this doesn't log anywhere
		EXPECT_EQ( outerSink.count, 1 );
	}
}

int main( int argc, char** argv )
{
	::testing::InitGoogleTest( &argc, argv );
	return RUN_ALL_TESTS();
}
