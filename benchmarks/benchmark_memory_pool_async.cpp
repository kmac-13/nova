/**
 * @file benchmark_memory_pool_async.cpp
 * @brief Benchmark for MemoryPoolAsyncSink with various configurations
 *
 * This benchmark compares:
 * - MemoryPoolAsyncSink vs AsyncQueueSink (baseline)
 * - different pool sizes
 * - different index offset types (uint16_t, uint32_t)
 * - heap vs Stack allocation
 * - variable message sizes
 * - thread scaling (1, 2, 4, 8 threads)
 */

#include <benchmark/benchmark.h>

#include "kmac/nova/nova.h"
#include "kmac/nova/scoped_configurator.h"
#include "kmac/nova/timestamp_helper.h"

#include "kmac/nova/extras/memory_pool_async_sink.h"
#include "kmac/nova/extras/null_sink.h"

// ============================================================================
// Test Infrastructure
// ============================================================================

struct MemPoolTag { };
NOVA_LOGGER_TRAITS( MemPoolTag, MEMPOOL, true, kmac::nova::TimestampHelper::steadyNanosecs );

// ============================================================================
// MemoryPoolAsyncSink: Default Configuration
// ============================================================================

static void BM_MemoryPoolAsyncSink_Default( benchmark::State& state )
{
	kmac::nova::extras::NullSink nullSink;
	// default: 1MB pool, 8192 index queue, uint32_t offsets, heap allocation
	auto asyncSink = std::make_unique< kmac::nova::extras::MemoryPoolAsyncSink<> >( nullSink );
	kmac::nova::ScopedConfigurator config;
	config.bind< MemPoolTag >( asyncSink.get() );

	for ( auto _ : state )
	{
		NOVA_LOG( MemPoolTag ) << "Default test " << state.thread_index();
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_MemoryPoolAsyncSink_Default )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 )->UseRealTime();

// ============================================================================
// Pool Size Comparison
// ============================================================================

// small pool: 256KB
static void BM_MemoryPoolAsyncSink_256KB( benchmark::State& state )
{
	kmac::nova::extras::NullSink nullSink;
	auto asyncSink = std::make_unique< kmac::nova::extras::MemoryPoolAsyncSink< 256 * 1024, 4096 > >( nullSink );
	kmac::nova::ScopedConfigurator config;
	config.bind< MemPoolTag >( asyncSink.get() );

	for ( auto _ : state )
	{
		NOVA_LOG( MemPoolTag ) << "256KB pool test " << state.thread_index();
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_MemoryPoolAsyncSink_256KB )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 )->UseRealTime();

// medium pool: 1MB (default)
static void BM_MemoryPoolAsyncSink_1MB( benchmark::State& state )
{
	kmac::nova::extras::NullSink nullSink;
	auto asyncSink = std::make_unique< kmac::nova::extras::MemoryPoolAsyncSink< 1024 * 1024, 8192 > >( nullSink );
	kmac::nova::ScopedConfigurator config;
	config.bind< MemPoolTag >( asyncSink.get() );

	for ( auto _ : state )
	{
		NOVA_LOG( MemPoolTag ) << "1MB pool test " << state.thread_index();
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_MemoryPoolAsyncSink_1MB )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 )->UseRealTime();

// large pool: 4MB
static void BM_MemoryPoolAsyncSink_4MB( benchmark::State& state )
{
	kmac::nova::extras::NullSink nullSink;
	auto asyncSink = std::make_unique< kmac::nova::extras::MemoryPoolAsyncSink< 4 * 1024 * 1024, 16384 > >( nullSink );
	kmac::nova::ScopedConfigurator config;
	config.bind< MemPoolTag >( asyncSink.get() );

	for ( auto _ : state )
	{
		NOVA_LOG( MemPoolTag ) << "4MB pool test " << state.thread_index();
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_MemoryPoolAsyncSink_4MB )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 )->UseRealTime();

// ============================================================================
// Offset Type Comparison
// ============================================================================

// 16-bit offsets (for small pools)
static void BM_MemoryPoolAsyncSink_Uint16Offset( benchmark::State& state )
{
	kmac::nova::extras::NullSink nullSink;
	// 32KB max for uint16_t (65536 exceeds uint16_t max of 65535)
	auto asyncSink = std::make_unique< kmac::nova::extras::MemoryPoolAsyncSink< 32 * 1024, 1024, uint16_t > >( nullSink );
	kmac::nova::ScopedConfigurator config;
	config.bind< MemPoolTag >( asyncSink.get() );

	for ( auto _ : state )
	{
		NOVA_LOG( MemPoolTag ) << "uint16 offset test " << state.thread_index();
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_MemoryPoolAsyncSink_Uint16Offset )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 )->UseRealTime();

// 32-bit offsets (default)
static void BM_MemoryPoolAsyncSink_Uint32Offset( benchmark::State& state )
{
	kmac::nova::extras::NullSink nullSink;
	auto asyncSink = std::make_unique< kmac::nova::extras::MemoryPoolAsyncSink< 1024 * 1024, 8192, uint32_t > >( nullSink );
	kmac::nova::ScopedConfigurator config;
	config.bind< MemPoolTag >( asyncSink.get() );

	for ( auto _ : state )
	{
		NOVA_LOG( MemPoolTag ) << "uint32 offset test " << state.thread_index();
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_MemoryPoolAsyncSink_Uint32Offset )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 )->UseRealTime();

// ============================================================================
// Allocator Comparison (Heap vs Stack)
// ============================================================================

// heap allocation (default)
static void BM_MemoryPoolAsyncSink_Heap( benchmark::State& state )
{
	kmac::nova::extras::NullSink nullSink;
	auto asyncSink = std::make_unique< kmac::nova::extras::MemoryPoolAsyncSink<
		256 * 1024,
		4096,
		uint32_t,
		kmac::nova::extras::PoolAllocator::Heap
		> >( nullSink );
	kmac::nova::ScopedConfigurator config;
	config.bind< MemPoolTag >( asyncSink.get() );

	for ( auto _ : state )
	{
		NOVA_LOG( MemPoolTag ) << "Heap allocator test " << state.thread_index();
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_MemoryPoolAsyncSink_Heap )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 )->UseRealTime();

// stack allocation
static void BM_MemoryPoolAsyncSink_Stack( benchmark::State& state )
{
	kmac::nova::extras::NullSink nullSink;
	auto asyncSink = std::make_unique< kmac::nova::extras::MemoryPoolAsyncSink<
		256 * 1024,
		4096,
		uint32_t,
		kmac::nova::extras::PoolAllocator::Stack
		> >( nullSink );
	kmac::nova::ScopedConfigurator config;
	config.bind< MemPoolTag >( asyncSink.get() );

	for ( auto _ : state )
	{
		NOVA_LOG( MemPoolTag ) << "Stack allocator test " << state.thread_index();
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_MemoryPoolAsyncSink_Stack )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 )->UseRealTime();

// ============================================================================
// Message Size Variations
// ============================================================================

// short messages (< 32 bytes)
static void BM_MemoryPoolAsyncSink_ShortMessages( benchmark::State& state )
{
	kmac::nova::extras::NullSink nullSink;
	auto asyncSink = std::make_unique< kmac::nova::extras::MemoryPoolAsyncSink<> >( nullSink );
	kmac::nova::ScopedConfigurator config;
	config.bind< MemPoolTag >( asyncSink.get() );

	for ( auto _ : state )
	{
		NOVA_LOG( MemPoolTag ) << "Short msg " << state.thread_index();
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_MemoryPoolAsyncSink_ShortMessages )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 )->UseRealTime();

// medium messages (~ 100 bytes)
static void BM_MemoryPoolAsyncSink_MediumMessages( benchmark::State& state )
{
	kmac::nova::extras::NullSink nullSink;
	auto asyncSink = std::make_unique< kmac::nova::extras::MemoryPoolAsyncSink<> >( nullSink );
	kmac::nova::ScopedConfigurator config;
	config.bind< MemPoolTag >( asyncSink.get() );

	for ( auto _ : state )
	{
		NOVA_LOG( MemPoolTag )
			<< "Medium message with more data to test pool efficiency "
			<< state.thread_index()
			<< " iteration " << state.iterations();
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_MemoryPoolAsyncSink_MediumMessages )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 )->UseRealTime();

// large messages (~ 500 bytes)
static void BM_MemoryPoolAsyncSink_LargeMessages( benchmark::State& state )
{
	kmac::nova::extras::NullSink nullSink;
	auto asyncSink = std::make_unique< kmac::nova::extras::MemoryPoolAsyncSink<> >( nullSink );
	kmac::nova::ScopedConfigurator config;
	config.bind< MemPoolTag >( asyncSink.get() );

	for ( auto _ : state )
	{
		NOVA_LOG( MemPoolTag )
			<< "Large message with substantial data to test pool allocation and memory efficiency "
			<< "across multiple cache lines and potentially triggering wrap-around behavior "
			<< "thread " << state.thread_index()
			<< " iteration " << state.iterations()
			<< " with additional padding to reach approximately 500 bytes of message content "
			<< "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 repeated multiple times to increase size "
			<< "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 "
			<< "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_MemoryPoolAsyncSink_LargeMessages )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 )->UseRealTime();

// ============================================================================
// Optimized Configuration (Expected Best Performance)
// ============================================================================

static void BM_MemoryPoolAsyncSink_Optimized( benchmark::State& state )
{
	kmac::nova::extras::NullSink nullSink;
	// variant: 2MB pool, 16K queue, uint32_t, heap
	auto asyncSink = std::make_unique< kmac::nova::extras::MemoryPoolAsyncSink<
		2 * 1024 * 1024,
		16384
		> >( nullSink );
	kmac::nova::ScopedConfigurator config;
	config.bind< MemPoolTag >( asyncSink.get() );

	for ( auto _ : state )
	{
		NOVA_LOG( MemPoolTag ) << "Variant config " << state.thread_index();
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_MemoryPoolAsyncSink_Optimized )->Threads( 1 )->Threads( 2 )->Threads( 4 )->Threads( 8 )->UseRealTime();

// ============================================================================
// Embedded Configuration (Minimal Memory)
// ============================================================================

static void BM_MemoryPoolAsyncSink_Embedded( benchmark::State& state )
{
	kmac::nova::extras::NullSink nullSink;
	// embedded: 32KB stack pool (max power-of-2 for uint16_t), 512 queue, uint16_t
	auto asyncSink = std::make_unique< kmac::nova::extras::MemoryPoolAsyncSink<
		32 * 1024,  // 32KB (max power-of-2 that fits in uint16_t)
		512,
		uint16_t,
		kmac::nova::extras::PoolAllocator::Stack
		> >( nullSink );
	kmac::nova::ScopedConfigurator config;
	config.bind< MemPoolTag >( asyncSink.get() );

	for ( auto _ : state )
	{
		NOVA_LOG( MemPoolTag ) << "Embedded config " << state.thread_index();
	}

	state.SetItemsProcessed( state.iterations() );
}
BENCHMARK( BM_MemoryPoolAsyncSink_Embedded )->Threads( 1 )->Threads( 2 )->Threads( 4 )->UseRealTime(); // limited to 4 threads for embedded

BENCHMARK_MAIN();
