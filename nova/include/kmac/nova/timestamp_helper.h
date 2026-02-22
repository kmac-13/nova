#pragma once
#ifndef KMAC_NOVA_TIMESTAMP_HELPER_H
#define KMAC_NOVA_TIMESTAMP_HELPER_H

#include "platform/chrono.h"

#include <cstdint>

namespace kmac::nova
{

/**
 * @brief Helper functions for common timestamp sources.
 * 
 * These functions delegate to platform::chrono for actual implementation.
 * In standard environments, they use std::chrono.
 * In bare-metal environments, users must implement platform::steadyNanosecs().
 */
namespace TimestampHelper
{

inline std::uint64_t steadyNanosecs() noexcept
{
	return platform::steadyNanosecs();
}

inline std::uint64_t steadyMicrosecs() noexcept
{
	return platform::steadyNanosecs() / 1000;
}

inline std::uint64_t steadyMillisecs() noexcept
{
	return platform::steadyNanosecs() / 1000000;
}

inline std::uint64_t systemNanosecs() noexcept
{
	return platform::systemNanosecs();
}

inline std::uint64_t systemMicrosecs() noexcept
{
	return platform::systemNanosecs() / 1000;
}

inline std::uint64_t systemMillisecs() noexcept
{
	return platform::systemNanosecs() / 1000000;
}

inline std::uint64_t highResNanosecs() noexcept
{
	return platform::highResNanosecs();
}

inline std::uint64_t highResMicrosecs() noexcept
{
	return platform::highResNanosecs() / 1000;
}

inline std::uint64_t highResMillisecs() noexcept
{
	return platform::highResNanosecs() / 1000000;
}

} // namespace TimestampHelper

} // namespace kmac::nova

#endif // KMAC_NOVA_TIMESTAMP_HELPER_H
