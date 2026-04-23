#pragma once
#ifndef KMAC_NOVA_EXTRAS_FILTER_SINK_H
#define KMAC_NOVA_EXTRAS_FILTER_SINK_H

#include <kmac/nova/sink.h>
#include <kmac/nova/record.h>

namespace kmac {
namespace nova {
namespace extras {

/**
 * @brief Sink that filters records based on a user-provided predicate.
 *
 * ⚠️ USE WITH CARE IN SAFETY-CRITICAL SYSTEMS, DEPENDS ON FILTER BEHAVIOR
 *
 * FilterSink allows runtime filtering of log records without requiring
 * compile-time changes. Records are forwarded to the downstream sink
 * only if the filter predicate returns true.
 *
 * This is useful for:
 * - filtering by tag name
 * - filtering by message content
 * - filtering by source file/function
 * - dynamic log level filtering
 * - temporary debugging filters
 *
 * Example:
 *   // only forward errors
 *   auto errorFilter = [](const Record& r) {
 *       return strstr(r.message, "ERROR") != nullptr;
 *   };
 *
 *   OStreamSink console(std::cout);
 *   FilterSink filtered(console, errorFilter);
 *
 *   Logger<MyTag>::bindSink(&filtered);
 */
template< typename FilterFn >
class FilterSink final : public kmac::nova::Sink
{
private:
	kmac::nova::Sink* _downstream;
	FilterFn _filter;

public:
	/**
	 * @brief Construct a filter sink.
	 *
	 * @param downstream Sink to forward filtered records to
	 * @param filter Predicate function that returns true for records to forward
	 */
	FilterSink( kmac::nova::Sink& downstream, FilterFn filter ) noexcept;

	/**
	 * @brief Process record through all child sinks.
	 *
	 * Calls each child sink's process() in insertion order.
	 *
	 * @param record log record to process
	 *
	 * @note if a sink throws, subsequent sinks are NOT called
	 */
	void process( const kmac::nova::Record& record ) noexcept override;
};

template< typename FilterFn >
FilterSink< FilterFn >::FilterSink( kmac::nova::Sink& downstream, FilterFn filter ) noexcept
	: _downstream( &downstream )
	, _filter( filter )
{
}

template< typename FilterFn >
void FilterSink< FilterFn >::process( const kmac::nova::Record& record ) noexcept
{
	if ( _filter( record ) )
	{
		_downstream->process( record );
	}
}

} // namespace extras
} // namespace nova
} // namespace kmac

#endif // KMAC_NOVA_EXTRAS_FILTER_SINK_H
