// nova_c11_check.cpp - intentionally minimal, just forces header compilation
#include <kmac/nova.h>

struct CheckTag {};
std::uint64_t checkTimestamp() noexcept { return 0; }
NOVA_LOGGER_TRAITS( CheckTag, CHECK, true, checkTimestamp );
