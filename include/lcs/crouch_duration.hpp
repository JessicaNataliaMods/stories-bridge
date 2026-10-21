#pragma once
#include <cstdint>
#include <optional>
namespace lcs {
// Original mission scripts use -1 with bCrouchWhenShooting. SA's duck task
// represents a persistent script crouch with a zero timer, not uint16(-1).
inline std::optional<uint16_t> native_crouch_duration(int32_t original) {
    if(original==-1)return uint16_t(0);
    if(original<0 || original>65535)return {};
    return uint16_t(original);
}
}
