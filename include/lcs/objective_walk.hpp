#pragma once
#include <array>
#include <cmath>
#include <optional>

namespace lcs {
// Normal GOTO_AREA_ON_FOOT/RUN_TO_AREA/SPRINT_TO_AREA only, not attractors,
// routes, target peds or vehicles. Native ownership and source-controllable
// state must be observed by the caller; task disappearance proves neither.
// Retail tables: 3C7690 entries 24/25/49 -> 30BF48; radius initialized to
// .5 at 308EC0/308EC4, stored at 308F0C; comparison at 30BF90..30BFEC.
inline std::optional<bool> source_walk_goal_complete(const std::array<float,2>& position,
        const std::array<float,2>& destination,bool owned_current_objective,
        bool controllable,bool on_foot) {
    if(!owned_current_objective)return {};
    if(!controllable || !on_foot)return false;
    for(const auto& xy:{position,destination})for(float value:xy)if(!std::isfinite(value))return {};
    const float dx=destination[0]-position[0],dy=destination[1]-position[1];
    const float distance_square=dx*dx+dy*dy;
    if(!std::isfinite(distance_square))return {};
    // No height component or host-radius substitute. Original zeroes Z
    // before testing radius squared >= XY distance squared, inclusive.
    return distance_square<=.25f;
}
}
