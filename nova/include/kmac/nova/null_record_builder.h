#pragma once
#ifndef KMAC_NOVA_NULL_RECORD_BUILDER_H
#define KMAC_NOVA_NULL_RECORD_BUILDER_H

#include <cstddef>
#include <cstring>

namespace kmac::nova
{

/**
 * @brief Null record builder that compiles to no-op.
 *
 * NullRecordBuilder provides a no-op implementation of the record builder
 * interface.  All operations are no-ops and should be completely optimized
 * away by the compiler.
 *
 * Use cases:
 *
 * 1. **Testing and mocking:**
 *    Useful for testing code that accepts any builder type without
 *    actually performing logging:
 *      template<typename Builder>
 *      void processWithLogging(Builder& builder) {
 *          builder << "Processing data";
 *          // ... actual work ...
 *      }
 *
 *      // Test without logging overhead
 *      NullRecordBuilder nullBuilder;
 *      processWithLogging(nullBuilder);
 *
 * 2. **Conditional compilation at call site:**
 *    When you want to conditionally enable/disable logging based on
 *    runtime or compile-time conditions outside of tag traits.
 *      #ifdef ENABLE_VERBOSE_LOGGING
 *          TruncatingRecordBuilder<VerboseTag> builder(__FILE__, __func__, __LINE__);
 *      #else
 *          NullRecordBuilder builder;
 *      #endif
 *      builder << "Verbose message";
 *
 * 3. **Generic code with builder parameters:**
 *    When writing generic functions that accept any builder type:
 *      template<typename Builder>
 *      void algorithm(Builder& log) {
 *          log << "Step 1";
 *          // work...
 *          log << "Step 2";
 *          // work...
 *      }
 *
 * NOTE: This builder is NOT used by the NOVA_LOG_* macros.  Those macros
 * use `if constexpr` to compile away disabled log statements entirely,
 * without needing a null builder:
 *   // macro expansion when enabled=false:
 *   if constexpr (false)
 *       TruncatingRecordBuilder<Tag>(...) << message;
 *   // ^ entire expression removed by compiler, no NullRecordBuilder needed
 *
 * This class exists for advanced use cases where you need explicit control
 * over builder selection outside of the macro system.
 */
class NullRecordBuilder
{
public:
	/**
	 * @brief No-op stream insertion (optimized away).
	 *
	 * @tparam T type being "appended" (ignored)
	 * @param value value being "appended" (ignored)
	 * @return reference to this builder (for chaining)
	 *
	 * @note entire call should be eliminated by compiler optimization
	 */
	template< typename T >
	inline constexpr NullRecordBuilder& operator<<( const T& ) noexcept;
};

template< typename T >
constexpr NullRecordBuilder& NullRecordBuilder::operator<<( const T & ) noexcept
{
	return *this;
}

} // namespace kmac::nova

#endif // KMAC_NOVA_NULL_RECORD_BUILDER_H
