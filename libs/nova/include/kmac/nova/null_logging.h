#pragma once
#ifndef KMAC_NOVA_NULL_LOGGING_H
#define KMAC_NOVA_NULL_LOGGING_H

#include "immovable.h"

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
 * - NOVA_LOG_NULL(Tag)  : always a no-op regardless of tag or sink state
 *
 * Usage:
 *   #include <kmac/nova/null_logging.h>
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
 *   kmac::nova::NullRecordBuilder nullBuilder;
 *   algorithm( nullBuilder );  // zero overhead
 */

namespace kmac {
namespace nova {

/**
 * @brief No-op record builder that compiles to nothing.
 *
 * All stream insertion operations are no-ops and are completely eliminated
 * by the compiler.  Useful for builder-generic template code that needs a
 * discard variant, or for explicitly silencing a logging call site.
 *
 * NOTE: The NOVA_LOG_* macros use this type in conjunction with std::conditional
 * on logger_traits<Tag>::enabled to eliminate disabled tags entirely.
 * NullRecordBuilder is for advanced use cases where explicit builder selection
 * outside the macro system is required.
 */
class NullRecordBuilder : private Immovable
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
 * Used by NOVA_LOG_BUF and NOVA_LOG_BUF_STACK as the disabled-tag path of
 * std::conditional, providing a uniform .builder() interface with the real
 * wrapper types.  Constructed as a temporary at the call site and never
 * copied or moved.
 */
class NullBuilderWrapper
{
private:
	NullRecordBuilder _builder;
public:
	NullBuilderWrapper() noexcept = default;
	inline NullBuilderWrapper( const char* /*file*/, const char* /*function*/, std::uint32_t /*line*/ )  noexcept;
	inline NullRecordBuilder& builder() noexcept;
};

template< typename T >
NullRecordBuilder& NullRecordBuilder::operator<<( const T& ) noexcept
{
	return *this;
}

NullBuilderWrapper::NullBuilderWrapper( const char* /*file*/, const char* /*function*/, std::uint32_t /*line*/ )  noexcept
{
}

NullRecordBuilder& NullBuilderWrapper::builder() noexcept
{
	return _builder;
}

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
	::kmac::nova::NullBuilderWrapper().builder()

#endif // KMAC_NOVA_NULL_LOGGING_H
