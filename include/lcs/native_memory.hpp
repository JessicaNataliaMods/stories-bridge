#pragma once
#include <cstdint>
namespace lcs {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
// Defined only by the isolated test executable. Its owned pages represent
// retail addresses as data, without fixed mappings or executable permissions.
uintptr_t native_entity_address(uintptr_t retail);
#else
constexpr uintptr_t native_entity_address(uintptr_t retail){return retail;}
#endif
}
