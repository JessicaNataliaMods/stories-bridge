#include "sa_allowed_collision.hpp"

namespace lcs {
namespace {

std::optional<std::array<float, 3>> level_position(int32_t level) noexcept {
    // Source: res-relcs/src/collision/ColStore.cpp::LevelPos.
    // The generic level is represented by the active native SA stream and
    // does not need an extra collision position.
    switch (level) {
    case 0:
        return std::nullopt;
    case 1: // LEVEL_INDUSTRIAL
        return std::array<float, 3>{1060.0f, -800.0f, 0.0f};
    case 2: // LEVEL_COMMERCIAL
        return std::array<float, 3>{350.0f, -624.0f, 0.0f};
    case 3: // LEVEL_SUBURBAN
        return std::array<float, 3>{-670.0f, -511.0f, 0.0f};
    default:
        // LEVEL_UNDERGROUND has no corresponding entry in the original
        // LevelPos table, so do not invent a native-world location for it.
        return std::nullopt;
    }
}

int32_t selected_level(int32_t first, int32_t second) noexcept {
    return first != 0 ? first : second;
}

} // namespace

bool AllowedCollisionRuntime::set(int32_t first, int32_t second) noexcept {
    const auto selected = selected_level(first, second);
    if (selected < 0 || selected > 3)
        return false;

    first_ = first;
    second_ = second;
    return true;
}

void AllowedCollisionRuntime::clear() noexcept {
    first_ = 0;
    second_ = 0;
}

std::optional<std::array<float, 3>> AllowedCollisionRuntime::position() const noexcept {
    return level_position(selected_level(first_, second_));
}

} // namespace lcs
