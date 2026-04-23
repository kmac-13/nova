// nova_c11_check.cpp - intentionally minimal, just forces header compilation
#include <kmac/nova/nova.h>
#include <kmac/nova/logger_traits.h>
#include <kmac/nova/scoped_configurator.h>

struct CheckTag {};
std::uint64_t checkTimestamp() noexcept { return 0; }
NOVA_LOGGER_TRAITS( CheckTag, CHECK, true, checkTimestamp );
