#pragma once
#ifndef KMAC_NOVA_LOGGER_H
#define KMAC_NOVA_LOGGER_H

#include "logger_traits.h"
#include "record.h"
#include "sink.h"
#include "platform/config.h"
#include "platform/atomic.h"

#include <cstring>

namespace kmac::nova
{

/**
 * @brief Compile-time logging endpoint for a specific tag.
 *
 * Logger<Tag> represents a statically typed logging endpoint identified by
 * the tag type Tag.  Tags are types that describe the context for which logging
 * is performed (e.g. severity levels, subsystems, a combination, or any other
 * desired context).  Because tags are types, routing decisions can be resolved
 * at compile time.
 *
 * Logger<Tag> does not own configuration or storage.  Instead, its runtime
 * behavior is determined by externally managed sink bindings.  If a sink is
 * bound for this tag, log records are routed to that sink; if no sink is bound,
 * no routing is performed and logging for this tag is effectively disabled at
 * runtime.
 *
 * This design cleanly separates:
 *  - Compile-time structure (tags and Logger instantiations)
 *  - Runtime behavior (sink binding and unbinding)
 *
 * Runtime enabling and disabling of logging is achieved by binding or unbinding
 * sinks.  This is independent of compile-time enablement controlled by
 * logger_traits::enabled.
 *
 * Logger<Tag> is not instantiated and does not represent an object with
 * lifetime.  All interaction is performed through static member functions.
 *
 * @tparam Tag type representing the logging context
 */
template< typename Tag >
class Logger
{
private:
	static platform::AtomicPtr< Sink > _sink;

public:
	/**
	 * @brief Binds the specified Sink to this Logger.
	 * If Sink is null, logging will not be performed.
	 * Binding a null Sink will disable logging at runtime for this Logger.
	 *
	 * @param sink
	 */
	static void bindSink( Sink* sink ) noexcept;

	/**
	 * @brief Unbinds the current Sink, which is equivalent to setting the
	 * Sink to null.
	 */
	static void unbindSink() noexcept;

	/**
	 * @brief Gets the current Sink.
	 * @return
	 */
	static Sink* getSink() noexcept;

	/**
	 * @brief Creates a Record with the specified message and routes it to the
	 * current Sink.
	 *
	 * @param file
	 * @param function
	 * @param line
	 * @param message
	 */
	static void log( const char* file, const char* function, std::uint32_t line, const char* message ) noexcept;

	/**
	 * @brief Logs the specified Record by routing it to the current Sink.
	 *
	 * @param record
	 */
	static void log( const Record& record ) noexcept;
};

template< typename Tag >
platform::AtomicPtr< Sink > Logger< Tag >::_sink;

template< typename Tag >
void Logger< Tag >::bindSink( Sink* sink ) noexcept
{
	_sink.store( sink );
}

template< typename Tag >
void Logger< Tag >::unbindSink() noexcept
{
	_sink.store( nullptr );
}

template< typename Tag >
Sink* Logger< Tag >::getSink() noexcept
{
	return _sink.load();
}

template< typename Tag >
void Logger< Tag >::log( const char* file, const char* function, std::uint32_t line, const char* message ) noexcept
{
	if constexpr ( logger_traits< Tag >::enabled )
	{
		Record record {
			logger_traits< Tag >::timestamp(),
			logger_traits< Tag >::tagId,
			logger_traits< Tag >::tagName,
			file,
			function,
			line,
			static_cast< std::uint32_t >( std::strlen( message ) ),
			message
		};
		log( record );
	}
}

template< typename Tag >
void Logger< Tag >::log( const Record& record ) noexcept
{
	if constexpr ( logger_traits< Tag >::enabled )
	{
		Sink* sink = _sink.load();
		if ( sink == nullptr )
		{
			return;
		}
		sink->process( record );
	}
}

} // namespace kmac::nova

#endif // KMAC_NOVA_LOGGER_H
