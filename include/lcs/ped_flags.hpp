#pragma once
#include <cstdint>
namespace lcs {
// Native CPed flags 19 and 22, verified independently in Plugin-SDK CPed.h.
// Do not freeze the actor or overwrite the separate stay-in-place flag (21).
inline void stationary_shooter_flags(uint8_t& flags,bool enabled) {
    constexpr uint8_t mask=(1u<<3)|(1u<<6);
    flags=enabled?uint8_t(flags|mask):uint8_t(flags&uint8_t(~mask));
}
}
