/**
 * @file test_nova_streaming.cpp
 * @brief Unit tests for streaming builder, SynchronizedCompositeSink,
 *        HierarchicalTag, and the predefined severity tags.
 */

#include "kmac/nova.h"
#include "kmac/nova/extras/composite_sink.h"
#include "kmac/nova/extras/hierarchical_tag.h"
#include "kmac/nova/extras/ostream_sink.h"
#include "kmac/nova/extras/severities.h"
#include "kmac/nova/extras/streaming_logging.h"
#include "kmac/nova/extras/synchronized_composite_sink.h"

#include <gtest/gtest.h>

#include <atomic>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// ============================================================================
// Shared helpers
// ============================================================================

// counting sink with message capture
class CaptureSink : public kmac::nova::Sink
{
public:
	std::vector< std::string > messages;

	void process( const kmac::nova::Record& record ) noexcept override
	{
		messages.emplace_back( record.message, record.messageSize );
	}

	void clear()
	{
		messages.clear();
	}
};

class CountSink : public kmac::nova::Sink
{
public:
	std::atomic< std::size_t > count { 0 };

	void process( const kmac::nova::Record& ) noexcept override
	{
		++count;
	}
};

// ============================================================================
// Streaming builder tags
// ============================================================================

struct StreamTag {};
std::uint64_t streamTs() noexcept { return 1111ULL; }
NOVA_LOGGER_TRAITS( StreamTag, STREAM_TAG, true, streamTs );

struct StreamDisabledTag {};
std::uint64_t streamDisabledTs() noexcept { return 0ULL; }
NOVA_LOGGER_TRAITS( StreamDisabledTag, STREAM_DISABLED, false, streamDisabledTs );

// ============================================================================
// StreamingRecordBuilder / NOVA_LOG_STREAM
// ============================================================================

class NovaStreamingBuilder : public ::testing::Test
{
protected:
	CaptureSink sink;

	void SetUp() override
	{
		sink.clear();
	}
};

TEST_F( NovaStreamingBuilder, BasicMessage )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< StreamTag >( &sink );

	NOVA_LOG_STREAM( StreamTag ) << "streaming message";

	ASSERT_EQ( sink.messages.size(), 1u );
	EXPECT_NE( sink.messages[ 0 ].find( "streaming message" ), std::string::npos );
}

TEST_F( NovaStreamingBuilder, IntegerStreaming )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< StreamTag >( &sink );

	NOVA_LOG_STREAM( StreamTag ) << "value=" << 42;

	ASSERT_EQ( sink.messages.size(), 1u );
	EXPECT_NE( sink.messages[ 0 ].find( "value=42" ), std::string::npos );
}

TEST_F( NovaStreamingBuilder, DoubleStreaming )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< StreamTag >( &sink );

	NOVA_LOG_STREAM( StreamTag ) << "pi=" << 3.14;

	ASSERT_EQ( sink.messages.size(), 1u );
	EXPECT_NE( sink.messages[ 0 ].find( "3.14" ), std::string::npos );
}

TEST_F( NovaStreamingBuilder, MultipleTypesInOneRecord )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< StreamTag >( &sink );

	NOVA_LOG_STREAM( StreamTag ) << "int=" << 7 << " str=" << "hello" << " bool=" << true;

	ASSERT_EQ( sink.messages.size(), 1u );
	EXPECT_NE( sink.messages[ 0 ].find( "int=7" ), std::string::npos );
	EXPECT_NE( sink.messages[ 0 ].find( "str=hello" ), std::string::npos );
}

TEST_F( NovaStreamingBuilder, MultipleCalls )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< StreamTag >( &sink );

	NOVA_LOG_STREAM( StreamTag ) << "first";
	NOVA_LOG_STREAM( StreamTag ) << "second";
	NOVA_LOG_STREAM( StreamTag ) << "third";

	EXPECT_EQ( sink.messages.size(), 3u );
}

TEST_F( NovaStreamingBuilder, NoSinkDoesNotCrash )
{
	// no binding - message should be silently discarded
	NOVA_LOG_STREAM( StreamTag ) << "no sink";
	SUCCEED();
}

TEST_F( NovaStreamingBuilder, DisabledTagDoesNotLog )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< StreamDisabledTag >( &sink );

	NOVA_LOG_STREAM( StreamDisabledTag ) << "should not appear";

	EXPECT_EQ( sink.messages.size(), 0u );
}

TEST_F( NovaStreamingBuilder, EmptyMessage )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< StreamTag >( &sink );

	NOVA_LOG_STREAM( StreamTag );

	ASSERT_EQ( sink.messages.size(), 1u );
	EXPECT_EQ( sink.messages[ 0 ].size(), 0u );
}

TEST_F( NovaStreamingBuilder, LargeMessage )
{
	// streaming builder handles arbitrarily long messages via ostringstream;
	// build a long string separately then stream it in one call
	kmac::nova::ScopedConfigurator<> config;
	config.bind< StreamTag >( &sink );

	std::string longMsg;
	longMsg.reserve( 5000 );
	for ( int i = 0; i < 500; ++i )
	{
		longMsg += "word" + std::to_string( i ) + " ";
	}

	NOVA_LOG_STREAM( StreamTag ) << longMsg;

	ASSERT_EQ( sink.messages.size(), 1u );
	EXPECT_GT( sink.messages[ 0 ].size(), 1000u );
}

// ============================================================================
// SynchronizedCompositeSink
// ============================================================================

struct SyncCompTag {};
std::uint64_t syncCompTs() noexcept { return 2222ULL; }
NOVA_LOGGER_TRAITS( SyncCompTag, SYNCCOMP, true, syncCompTs );

class NovaSynchronizedCompositeSink : public ::testing::Test
{
protected:
	kmac::nova::extras::CompositeSink composite;
	kmac::nova::extras::SynchronizedCompositeSink syncComposite { composite };

	void SetUp() override
	{
		composite.clear();
	}
};

TEST_F( NovaSynchronizedCompositeSink, ForwardsToChild )
{
	CountSink counter;
	syncComposite.addSink( counter );

	kmac::nova::ScopedConfigurator<> config;
	config.bind< SyncCompTag >( &syncComposite );

	NOVA_LOG( SyncCompTag ) << "forwarded";

	EXPECT_EQ( counter.count.load(), 1u );
}

TEST_F( NovaSynchronizedCompositeSink, ForwardsToMultipleChildren )
{
	CountSink c1;
	CountSink c2;
	syncComposite.addSink( c1 );
	syncComposite.addSink( c2 );

	kmac::nova::ScopedConfigurator<> config;
	config.bind< SyncCompTag >( &syncComposite );

	NOVA_LOG( SyncCompTag ) << "broadcast";

	EXPECT_EQ( c1.count.load(), 1u );
	EXPECT_EQ( c2.count.load(), 1u );
}

TEST_F( NovaSynchronizedCompositeSink, ClearSinksStopsForwarding )
{
	CountSink counter;
	syncComposite.addSink( counter );

	kmac::nova::ScopedConfigurator<> config;
	config.bind< SyncCompTag >( &syncComposite );

	NOVA_LOG( SyncCompTag ) << "before clear";
	EXPECT_EQ( counter.count.load(), 1u );

	syncComposite.clearSinks();

	NOVA_LOG( SyncCompTag ) << "after clear";
	EXPECT_EQ( counter.count.load(), 1u );
}

TEST_F( NovaSynchronizedCompositeSink, ThreadSafeAccess )
{
	CountSink counter;
	syncComposite.addSink( counter );

	kmac::nova::ScopedConfigurator<> config;
	config.bind< SyncCompTag >( &syncComposite );

	const int numThreads = 8;
	const int logsPerThread = 100;

	std::vector< std::thread > threads;
	for ( int i = 0; i < numThreads; ++i )
	{
		threads.emplace_back( [ logsPerThread ]() {
			for ( int j = 0; j < logsPerThread; ++j )
			{
				NOVA_LOG( SyncCompTag ) << "thread log " << j;
			}
		} );
	}

	for ( auto& t : threads )
	{
		t.join();
	}

	EXPECT_EQ( counter.count.load(), static_cast< std::size_t >( numThreads * logsPerThread ) );
}

// ============================================================================
// HierarchicalTag
// ============================================================================

// subsystem types
struct Sensors {};
struct Network {};

// severity types
struct Trace {};
struct Info {};
struct Error {};

// concrete hierarchical tag types
using SensorsTrace = kmac::nova::extras::HierarchicalTag< Sensors, Trace >;
using SensorsInfo = kmac::nova::extras::HierarchicalTag< Sensors, Info >;
using SensorsError = kmac::nova::extras::HierarchicalTag< Sensors, Error >;
using NetworkInfo = kmac::nova::extras::HierarchicalTag< Network, Info >;
using NetworkError = kmac::nova::extras::HierarchicalTag< Network, Error >;

// register traits for each hierarchical tag
std::uint64_t hierarchTs() noexcept { return 3333ULL; }
NOVA_LOGGER_TRAITS( SensorsTrace, SENSORS.TRACE, true, hierarchTs );
NOVA_LOGGER_TRAITS( SensorsInfo, SENSORS.INFO, true, hierarchTs );
NOVA_LOGGER_TRAITS( SensorsError, SENSORS.ERROR, true, hierarchTs );
NOVA_LOGGER_TRAITS( NetworkInfo, NET.INFO, true, hierarchTs );
NOVA_LOGGER_TRAITS( NetworkError, NET.ERROR, true, hierarchTs );

class NovaHierarchicalTag : public ::testing::Test
{
protected:
	CountSink counter;

	void SetUp() override
	{
		counter.count.store( 0 );
	}
};

TEST_F( NovaHierarchicalTag, IndependentBindingPerTag )
{
	CountSink sensorsSink;
	CountSink networkSink;

	kmac::nova::ScopedConfigurator<> config;
	config.bind< SensorsInfo >( &sensorsSink );
	config.bind< NetworkInfo >( &networkSink );

	NOVA_LOG( SensorsInfo ) << "sensor reading";
	NOVA_LOG( NetworkInfo ) << "packet received";

	EXPECT_EQ( sensorsSink.count.load(), 1u );
	EXPECT_EQ( networkSink.count.load(), 1u );
}

TEST_F( NovaHierarchicalTag, SharedSinkForSubsystem )
{
	// bind all Sensor tags to the same sink to simulate subsystem-level routing
	kmac::nova::ScopedConfigurator<> config;
	config.bind< SensorsTrace >( &counter );
	config.bind< SensorsInfo >( &counter );
	config.bind< SensorsError >( &counter );

	NOVA_LOG( SensorsTrace ) << "trace";
	NOVA_LOG( SensorsInfo ) << "info";
	NOVA_LOG( SensorsError ) << "error";

	EXPECT_EQ( counter.count.load(), 3u );
}

TEST_F( NovaHierarchicalTag, CrossSubsystemIsolation )
{
	CountSink sensorSink;
	CountSink networkSink;

	kmac::nova::ScopedConfigurator<> config;
	config.bind< SensorsError >( &sensorSink );
	config.bind< NetworkError >( &networkSink );

	NOVA_LOG( SensorsError ) << "sensor error";
	NOVA_LOG( NetworkError ) << "network error";

	EXPECT_EQ( sensorSink.count.load(), 1u );
	EXPECT_EQ( networkSink.count.load(), 1u );
}

TEST_F( NovaHierarchicalTag, IsSubsystemTrait )
{
	constexpr bool isSensor = kmac::nova::extras::IsSubsystem< SensorsInfo, Sensors >::value;
	constexpr bool notSensor = kmac::nova::extras::IsSubsystem< NetworkInfo, Sensors >::value;

	EXPECT_TRUE( isSensor );
	EXPECT_FALSE( notSensor );
}

TEST_F( NovaHierarchicalTag, IsSeverityTrait )
{
	constexpr bool isInfo = kmac::nova::extras::IsSeverity< SensorsInfo, Info >::value;
	constexpr bool notInfo = kmac::nova::extras::IsSeverity< SensorsError, Info >::value;

	EXPECT_TRUE( isInfo );
	EXPECT_FALSE( notInfo );
}

// ============================================================================
// Severity tags (severities.h)
// ============================================================================

class NovaSeverities : public ::testing::Test
{
protected:
	CountSink counter;

	void SetUp() override
	{
		counter.count.store( 0 );
	}
};

TEST_F( NovaSeverities, AllSeverityMacrosLog )
{
	kmac::nova::ScopedConfigurator<> config;
	config.bind< kmac::nova::extras::TraceTag >( &counter );
	config.bind< kmac::nova::extras::DebugTag >( &counter );
	config.bind< kmac::nova::extras::InfoTag >( &counter );
	config.bind< kmac::nova::extras::WarningTag >( &counter );
	config.bind< kmac::nova::extras::ErrorTag >( &counter );
	config.bind< kmac::nova::extras::FatalTag >( &counter );

	NOVA_LOG_TRACE() << "trace";
	NOVA_LOG_DEBUG() << "debug";
	NOVA_LOG_INFO() << "info";
	NOVA_LOG_WARN() << "warn";
	NOVA_LOG_ERROR() << "error";
	NOVA_LOG_FATAL() << "fatal";

	EXPECT_EQ( counter.count.load(), 6u );
}

TEST_F( NovaSeverities, IndividualSinkBindings )
{
	CountSink traceSink;
	CountSink debugSink;
	CountSink infoSink;

	kmac::nova::ScopedConfigurator<> config;
	config.bind< kmac::nova::extras::TraceTag >( &traceSink );
	config.bind< kmac::nova::extras::DebugTag >( &debugSink );
	config.bind< kmac::nova::extras::InfoTag >( &infoSink );

	NOVA_LOG_TRACE() << "t";
	NOVA_LOG_TRACE() << "t2";
	NOVA_LOG_DEBUG() << "d";
	NOVA_LOG_INFO() << "i";

	EXPECT_EQ( traceSink.count.load(), 2u );
	EXPECT_EQ( debugSink.count.load(), 1u );
	EXPECT_EQ( infoSink.count.load(), 1u );
}

TEST_F( NovaSeverities, SeverityTagNamesPresent )
{
	EXPECT_STREQ( kmac::nova::LoggerTraits< kmac::nova::extras::TraceTag >::tagName, "TRACE" );
	EXPECT_STREQ( kmac::nova::LoggerTraits< kmac::nova::extras::DebugTag >::tagName, "DEBUG" );
	EXPECT_STREQ( kmac::nova::LoggerTraits< kmac::nova::extras::InfoTag >::tagName, "INFO" );
	EXPECT_STREQ( kmac::nova::LoggerTraits< kmac::nova::extras::WarningTag >::tagName, "WARNING" );
	EXPECT_STREQ( kmac::nova::LoggerTraits< kmac::nova::extras::ErrorTag >::tagName, "ERROR" );
	EXPECT_STREQ( kmac::nova::LoggerTraits< kmac::nova::extras::FatalTag >::tagName, "FATAL" );
}

TEST_F( NovaSeverities, AllSeverityTagsEnabled )
{
	EXPECT_TRUE( ( kmac::nova::LoggerTraits< kmac::nova::extras::TraceTag >::enabled ) );
	EXPECT_TRUE( ( kmac::nova::LoggerTraits< kmac::nova::extras::DebugTag >::enabled ) );
	EXPECT_TRUE( ( kmac::nova::LoggerTraits< kmac::nova::extras::InfoTag >::enabled ) );
	EXPECT_TRUE( ( kmac::nova::LoggerTraits< kmac::nova::extras::WarningTag >::enabled ) );
	EXPECT_TRUE( ( kmac::nova::LoggerTraits< kmac::nova::extras::ErrorTag >::enabled ) );
	EXPECT_TRUE( ( kmac::nova::LoggerTraits< kmac::nova::extras::FatalTag >::enabled ) );
}

TEST_F( NovaSeverities, SeverityTagsHaveDistinctTagIds )
{
	const std::uint64_t traceId = kmac::nova::LoggerTraits< kmac::nova::extras::TraceTag >::tagId;
	const std::uint64_t debugId = kmac::nova::LoggerTraits< kmac::nova::extras::DebugTag >::tagId;
	const std::uint64_t infoId = kmac::nova::LoggerTraits< kmac::nova::extras::InfoTag >::tagId;
	const std::uint64_t warningId = kmac::nova::LoggerTraits< kmac::nova::extras::WarningTag >::tagId;
	const std::uint64_t errorId = kmac::nova::LoggerTraits< kmac::nova::extras::ErrorTag >::tagId;
	const std::uint64_t fatalId = kmac::nova::LoggerTraits< kmac::nova::extras::FatalTag >::tagId;

	EXPECT_NE( traceId, debugId );
	EXPECT_NE( debugId, infoId );
	EXPECT_NE( infoId, warningId );
	EXPECT_NE( warningId, errorId );
	EXPECT_NE( errorId, fatalId );
}

TEST_F( NovaSeverities, NoSinkDoesNotCrash )
{
	NOVA_LOG_TRACE() << "no sink";
	NOVA_LOG_INFO() << "no sink";
	NOVA_LOG_ERROR() << "no sink";
	SUCCEED();
}
