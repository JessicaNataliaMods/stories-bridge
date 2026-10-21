#pragma once

#include "lcs/sa_executor.hpp"

namespace lcs {

struct CharFollowPathPedFacts {
    ObjectiveActor actor;
    bool controllable = false;
    bool on_foot = false;
    int32_t native_state = -1;
};

struct CharFollowPathBackend {
    std::function<std::optional<CharFollowPathPedFacts>(int32_t)> resolve_actor;
    std::function<std::optional<ScriptTaskSnapshot>()> task_snapshot;
    std::function<std::optional<bool>(const ObjectiveActor&, const CapturedScriptTask&)> task_active;
    NativeExecutor execute;
};

// Injectable executor used by the production adapter and the offline contract test.
NativeResult execute_char_follow_path(int32_t ped, const std::array<float, 3>& destination,
    float abort_radius, int32_t mode, bool mission_context, const CharFollowPathBackend& backend);

// SA adaptation of retail CHAR_FOLLOW_PATH (0x009E) through the native
// TASK_FOLLOW_PATH_NODES_TO_COORD_WITH_RADIUS command (0x0A2E).
NativeResult sa_char_follow_path(int32_t ped, const std::array<float, 3>& destination,
    float abort_radius, int32_t mode, bool mission_context, const NativeExecutor& execute);

} // namespace lcs
