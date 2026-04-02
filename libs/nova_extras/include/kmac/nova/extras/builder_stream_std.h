#pragma once
#ifndef KMAC_NOVA_EXTRAS_BUILDER_STREAM_STD_H
#define KMAC_NOVA_EXTRAS_BUILDER_STREAM_STD_H

/**
 * @file builder_stream_std.h
 * @brief Logging support for standard library container types.
 *
 * Provides operator<< overloads for TruncatingRecordBuilder and
 * ContinuationRecordBuilder for commonly used std types.  (Note that
 * StreamingRecordBuilder is not supported.)
 *
 * Supported types:
 * - std::vector<T>, std::array<T, N>, std::list<T>
 * - std::map<K, V>, std::unordered_map<K, V>
 * - std::set<T>, std::unordered_set<T>
 * - std::pair<A, B>, std::optional<T>
 *
 * Note that string implicitly casts to string_view, which is already supported
 * by the builders, so there's no need for specialization.
 *
 * Container format:  [a, b, c]
 * Map format:        {k: v, k: v}
 * Pair format:       (a, b)
 * optional format:   <value> or <nullopt>
 *
 * Usage:
 *   #include <kmac/nova/nova.h>
 *   #include <kmac/nova/extras/continuation_logging.h>  // if using NOVA_LOG_CONT
 *   #include <kmac/nova/extras/builder_stream_std.h>
 *
 *   std::vector< int > values = { 1, 2, 3 };
 *   NOVA_LOG( MyTag ) << "values: " << values;
 *   // output: values: [1, 2, 3]
 *
 * Custom types:
 *   To log your own type, define operator<< alongside it:
 *
 *   namespace MyApp
 *   {
 *       struct Point { int x; int y; };
 *
 *       template< std::size_t N >
 *       kmac::nova::TruncatingRecordBuilder< N >& operator<<(
 *           kmac::nova::TruncatingRecordBuilder< N >& builder,
 *           const Point& value )
 *       {
 *           return builder << '(' << value.x << ", " << value.y << ')';
 *       }
 *
 *       template< std::size_t N >
 *       kmac::nova::extras::ContinuationRecordBuilder< N >& operator<<(
 *           kmac::nova::extras::ContinuationRecordBuilder< N >& builder,
 *           const Point& value )
 *       {
 *           return builder << '(' << value.x << ", " << value.y << ')';
 *       }
 *   }
 */

#include <kmac/nova/truncating_logging.h>
#include <kmac/nova/extras/continuation_logging.h>

#include <array>
#include <list>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// ============================================================================
// Helper: write a sequence range as [a, b, c]
// ============================================================================

namespace kmac::nova::extras::std_support
{

template< typename Builder, typename Iterator >
Builder& writeRange( Builder& builder, Iterator first, Iterator last )
{
	builder << '[';
	for ( auto it = first; it != last; ++it )
	{
		if ( it != first )
		{
			builder << ", ";
		}
		builder << *it;
	}
	builder << ']';
	return builder;
}

template< typename Builder, typename Iterator >
Builder& writeMap( Builder& builder, Iterator first, Iterator last )
{
	builder << '{';
	for ( auto it = first; it != last; ++it )
	{
		if ( it != first )
		{
			builder << ", ";
		}
		builder << it->first << ": " << it->second;
	}
	builder << '}';
	return builder;
}

} // namespace kmac::nova::extras::std_support

// ============================================================================
// std::vector<T>
// ============================================================================

template< std::size_t N, typename T, typename Alloc >
kmac::nova::TruncatingRecordBuilder< N >& operator<<(
	kmac::nova::TruncatingRecordBuilder< N >& builder,
	const std::vector< T, Alloc >& value )
{
	return kmac::nova::extras::std_support::writeRange( builder, value.begin(), value.end() );
}

template< std::size_t N, typename T, typename Alloc >
kmac::nova::extras::ContinuationRecordBuilder< N >& operator<<(
	kmac::nova::extras::ContinuationRecordBuilder< N >& builder,
	const std::vector< T, Alloc >& value )
{
	return kmac::nova::extras::std_support::writeRange( builder, value.begin(), value.end() );
}

// ============================================================================
// std::array<T, M>
// ============================================================================

template< std::size_t N, typename T, std::size_t M >
kmac::nova::TruncatingRecordBuilder< N >& operator<<(
	kmac::nova::TruncatingRecordBuilder< N >& builder,
	const std::array< T, M >& value )
{
	return kmac::nova::extras::std_support::writeRange( builder, value.begin(), value.end() );
}

template< std::size_t N, typename T, std::size_t M >
kmac::nova::extras::ContinuationRecordBuilder< N >& operator<<(
	kmac::nova::extras::ContinuationRecordBuilder< N >& builder,
	const std::array< T, M >& value )
{
	return kmac::nova::extras::std_support::writeRange( builder, value.begin(), value.end() );
}

// ============================================================================
// std::list<T>
// ============================================================================

template< std::size_t N, typename T, typename Alloc >
kmac::nova::TruncatingRecordBuilder< N >& operator<<(
	kmac::nova::TruncatingRecordBuilder< N >& builder,
	const std::list< T, Alloc >& value )
{
	return kmac::nova::extras::std_support::writeRange( builder, value.begin(), value.end() );
}

template< std::size_t N, typename T, typename Alloc >
kmac::nova::extras::ContinuationRecordBuilder< N >& operator<<(
	kmac::nova::extras::ContinuationRecordBuilder< N >& builder,
	const std::list< T, Alloc >& value )
{
	return kmac::nova::extras::std_support::writeRange( builder, value.begin(), value.end() );
}

// ============================================================================
// std::set<T>
// ============================================================================

template< std::size_t N, typename T, typename Compare, typename Alloc >
kmac::nova::TruncatingRecordBuilder< N >& operator<<(
	kmac::nova::TruncatingRecordBuilder< N >& builder,
	const std::set< T, Compare, Alloc >& value )
{
	return kmac::nova::extras::std_support::writeRange( builder, value.begin(), value.end() );
}

template< std::size_t N, typename T, typename Compare, typename Alloc >
kmac::nova::extras::ContinuationRecordBuilder< N >& operator<<(
	kmac::nova::extras::ContinuationRecordBuilder< N >& builder,
	const std::set< T, Compare, Alloc >& value )
{
	return kmac::nova::extras::std_support::writeRange( builder, value.begin(), value.end() );
}

// ============================================================================
// std::unordered_set<T>
// ============================================================================

template< std::size_t N, typename T, typename Hash, typename Equal, typename Alloc >
kmac::nova::TruncatingRecordBuilder< N >& operator<<(
	kmac::nova::TruncatingRecordBuilder< N >& builder,
	const std::unordered_set< T, Hash, Equal, Alloc >& value )
{
	return kmac::nova::extras::std_support::writeRange( builder, value.begin(), value.end() );
}

template< std::size_t N, typename T, typename Hash, typename Equal, typename Alloc >
kmac::nova::extras::ContinuationRecordBuilder< N >& operator<<(
	kmac::nova::extras::ContinuationRecordBuilder< N >& builder,
	const std::unordered_set< T, Hash, Equal, Alloc >& value )
{
	return kmac::nova::extras::std_support::writeRange( builder, value.begin(), value.end() );
}

// ============================================================================
// std::map<K, V>
// ============================================================================

template< std::size_t N, typename K, typename V, typename Compare, typename Alloc >
kmac::nova::TruncatingRecordBuilder< N >& operator<<(
	kmac::nova::TruncatingRecordBuilder< N >& builder,
	const std::map< K, V, Compare, Alloc >& value )
{
	return kmac::nova::extras::std_support::writeMap( builder, value.begin(), value.end() );
}

template< std::size_t N, typename K, typename V, typename Compare, typename Alloc >
kmac::nova::extras::ContinuationRecordBuilder< N >& operator<<(
	kmac::nova::extras::ContinuationRecordBuilder< N >& builder,
	const std::map< K, V, Compare, Alloc >& value )
{
	return kmac::nova::extras::std_support::writeMap( builder, value.begin(), value.end() );
}

// ============================================================================
// std::unordered_map<K, V>
// ============================================================================

template< std::size_t N, typename K, typename V, typename Hash, typename Equal, typename Alloc >
kmac::nova::TruncatingRecordBuilder< N >& operator<<(
	kmac::nova::TruncatingRecordBuilder< N >& builder,
	const std::unordered_map< K, V, Hash, Equal, Alloc >& value )
{
	return kmac::nova::extras::std_support::writeMap( builder, value.begin(), value.end() );
}

template< std::size_t N, typename K, typename V, typename Hash, typename Equal, typename Alloc >
kmac::nova::extras::ContinuationRecordBuilder< N >& operator<<(
	kmac::nova::extras::ContinuationRecordBuilder< N >& builder,
	const std::unordered_map< K, V, Hash, Equal, Alloc >& value )
{
	return kmac::nova::extras::std_support::writeMap( builder, value.begin(), value.end() );
}

// ============================================================================
// std::pair<A, B>
// ============================================================================

template< std::size_t N, typename A, typename B >
kmac::nova::TruncatingRecordBuilder< N >& operator<<(
	kmac::nova::TruncatingRecordBuilder< N >& builder,
	const std::pair< A, B >& value )
{
	return builder << '(' << value.first << ", " << value.second << ')';
}

template< std::size_t N, typename A, typename B >
kmac::nova::extras::ContinuationRecordBuilder< N >& operator<<(
	kmac::nova::extras::ContinuationRecordBuilder< N >& builder,
	const std::pair< A, B >& value )
{
	return builder << '(' << value.first << ", " << value.second << ')';
}

// ============================================================================
// std::optional<T>
// ============================================================================

template< std::size_t N, typename T >
kmac::nova::TruncatingRecordBuilder< N >& operator<<(
	kmac::nova::TruncatingRecordBuilder< N >& builder,
	const std::optional< T >& value )
{
	if ( value.has_value() )
	{
		return builder << '<' << *value << '>';
	}
	return builder << "<nullopt>";
}

template< std::size_t N, typename T >
kmac::nova::extras::ContinuationRecordBuilder< N >& operator<<(
	kmac::nova::extras::ContinuationRecordBuilder< N >& builder,
	const std::optional< T >& value )
{
	if ( value.has_value() )
	{
		return builder << '<' << *value << '>';
	}
	return builder << "<nullopt>";
}

#endif // KMAC_NOVA_EXTRAS_BUILDER_STREAM_STD_H
