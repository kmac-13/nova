#include "kmac/nova/extras/spinlock_sink.h"

#include "kmac/nova/record.h"
#include "kmac/nova/sink.h"

#include <atomic>

#if defined( __x86_64__ ) || defined( __i386__ )
#include <immintrin.h>  // For _mm_pause()
#endif

namespace kmac::nova::extras
{

SpinlockSink::SpinlockSink( kmac::nova::Sink& downstream ) noexcept
	: _downstream( &downstream )
{
}

void SpinlockSink::process( const kmac::nova::Record& record ) noexcept
{
	// acquire spinlock
	while ( _lock.test_and_set( std::memory_order_acquire ) )
	{
		// spin - busy wait
		// use CPU pause instruction to reduce power and improve performance
#if defined(__x86_64__) || defined(__i386__)
		_mm_pause();
#elif defined(__aarch64__) || defined(__arm__)
		__asm__ __volatile__( "yield" );
#endif
	}

	// critical section - process the record
	_downstream->process( record );

	// release spinlock
	_lock.clear( std::memory_order_release );
}

} // namespace kmac::nova::extras
