#pragma once
#ifndef KMAC_NOVA_NULL_LOGGING_H
#define KMAC_NOVA_NULL_LOGGING_H

#include <kmac/nova/immovable.h>

#include <cstdint>

/**
 * @file null_logging.h
 * @brief Null (no-op) builder and macro for Nova.
 *
 * Include this header when you need a builder that discards all output,
 * or when writing builder-generic template code that needs a no-op variant.
 *
 * Provides:
 * - NullRecordBuilder   : no-op builder, all operations compile away
 * - NullBuilderWrapper  : RAII wrapper around NullRecordBuilder exposing
 *                         a uniform builder() interface; useful when pairing
 *                         with std::conditional to select between a real
 *                         builder wrapper and a no-op at compile time
 * - NOVA_LOG_NULL(Tag)  : always a no-op regardless of tag or sink state
 *
 * Usage:
 *   #include <kmac/nova/extras/null_logging.h>
 *
 *   // unconditional no-op at call site
 *   NOVA_LOG_NULL( VerboseTag ) << "never emitted";
 *
 *   // generic builder-templated code
 *   template< typename Builder >
 *   void algorithm( Builder& log )
 *   {
 *       log << "Step 1";
 *       // work...
 *       log << "Step 2";
 *   }
 *
 *   kmac::nova::extras::NullRecordBuilder nullBuilder;
 *   algorithm( nullBuilder );  // zero overhead
 *
 *   // std::conditional dispatch example - selects builder type at compile time;
 *   // NOTE: Arguments to operator<< are still evaluated on C++11/14 since
 *   // std::conditional does not suppress argument evaluation the way
 *   // if constexpr does on C++17.  Use only where argument evaluation cost
 *   // is acceptable or arguments are guaranteed side-effect-free.
 *   typename std::conditional<
 *       ::kmac::nova::logger_traits< TagType >::enabled,
 *       ::kmac::nova::TlsTruncBuilderWrapper< TagType, BufferSize >,
 *       ::kmac::nova::extras::NullBuilderWrapper >::type(
 *           file, func, line ).builder() << value;
 */

namespace kmac {
namespace nova {
namespace extras {

/**
 * @brief No-op record builder that compiles to nothing.
 *
 * All stream insertion operations are no-ops and are completely eliminated
 * by the compiler.  Useful for builder-generic template code that needs a
 * discard variant, or for explicitly silencing a logging call site.
 *
 * The NOVA_LOG_* macros do not use this type directly - they use
 * NOVA_IF_CONSTEXPR on logger_traits<Tag>::enabled to eliminate disabled
 * tags.  NullRecordBuilder is for advanced use cases where explicit builder
 * selection outside the macro system is required.
 */
class NullRecordBuilder : private ::kmac::nova::Immovable
{
public:
	/**
	 * @brief No-op stream insertion (optimized away entirely).
	 *
	 * @tparam T type being "appended" (ignored)
	 * @param value value being "appended" (ignored)
	 * @return reference to this builder (for chaining)
	 */
	template< typename T >
	inline NullRecordBuilder& operator<<( const T& ) noexcept;
};

/**
 * @brief RAII wrapper returning a NullRecordBuilder from builder().
 *
 * Provides a uniform builder() interface matching TlsTruncBuilderWrapper
 * and StackTruncBuilderWrapper.  Intended for uses such as std::conditional
 * to select between a real builder wrapper and a no-op one at compile time.
 * Constructed as a temporary at the call site and never copied or moved.
 *
 * Note: When used with std::conditional, arguments to operator<< are still
 * evaluated on C++11/14 since std::conditional does not suppress argument
 * evaluation.  On C++17, prefer NOVA_IF_CONSTEXPR for true zero-cost
 * disabled tag elimination including argument evaluation.
 *
 * Example:
 * @code
 *   typename std::conditional<
 *       ::kmac::nova::logger_traits< TagType >::enabled,
 *       ::kmac::nova::TlsTruncBuilderWrapper< TagType, BufferSize >,
 *       ::kmac::nova::extras::NullBuilderWrapper >::type(
 *           file, func, line ).builder() << value;
 * @endcode
 */
class NullBuilderWrapper
{
private:
	NullRecordBuilder _builder;

public:
	NullBuilderWrapper() noexcept = default;

	inline NullBuilderWrapper( const char* /*file*/, const char* /*function*/, std::uint32_t /*line*/ ) noexcept;

	inline NullRecordBuilder& builder() noexcept;
};

template< typename T >
inline NullRecordBuilder& NullRecordBuilder::operator<<( const T& ) noexcept
{
	return *this;
}

inline NullBuilderWrapper::NullBuilderWrapper( const char* /*file*/, const char* /*function*/, std::uint32_t /*line*/ ) noexcept
{
}

inline NullRecordBuilder& NullBuilderWrapper::builder() noexcept
{
	return _builder;
}

} // namespace extras
} // namespace nova
} // namespace kmac

// ============================================================================
// Null Logging Macro
// ============================================================================

/**
 * @brief Unconditional no-op logger.
 *
 * Always compiles to nothing regardless of tag enablement or sink binding.
 * Useful for explicitly silencing a logging call site without removing it,
 * or for temporarily disabling a log statement during development.
 *
 * Unlike NOVA_LOG which respects logger_traits<Tag>::enabled and sink
 * binding, NOVA_LOG_NULL discards everything unconditionally.
 *
 * Usage:
 *   NOVA_LOG_NULL( VerboseTag ) << "silenced " << value;
 *
 * @param TagType the logging tag type (evaluated but result discarded)
 */
#define NOVA_LOG_NULL( TagType ) /* NOLINT(cppcoreguidelines-macro-usage) */ \
	::kmac::nova::extras::NullRecordBuilder{}

#endif // KMAC_NOVA_NULL_LOGGING_H
