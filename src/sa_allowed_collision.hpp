#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include "lcs/native.hpp"

namespace lcs {

// Runtime state for LCS SET_ALLOWED_COLLISION. The original consumer uses
// the first non-generic level (or the second when the first is generic) to
// request collision around that level's streaming position. SA keeps its
// ordinary collision load; the native hook can add this one position.
class AllowedCollisionRuntime {
    int32_t first_ = 0;
    int32_t second_ = 0;

public:
    // Returns false without changing the current request when the selected
    // level has no proven LCS streaming position.
    bool set(int32_t first, int32_t second) noexcept;
    void clear() noexcept;

    // Empty means the original generic level is selected. A value is the
    // exact LCS LevelPos location used by the original COL-store consumer.
    std::optional<std::array<float, 3>> position() const noexcept;
};

NativeResult sa_set_allowed_collision(int32_t first,int32_t second);
bool sa_release_allowed_collision();
bool sa_allowed_collision_healthy();

} // namespace lcs
