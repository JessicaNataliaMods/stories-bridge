#include "sa_char_follow_path.hpp"

#include <cmath>
#include <cstring>
#ifndef LCS_ISOLATED_CHAR_FOLLOW_PATH_TEST
#include <windows.h>
#endif

namespace lcs {
namespace {
constexpr int32_t native_ground_z_opcode = 0x02CE;
constexpr int32_t native_clear_tasks_opcode = 0x0687;
constexpr int32_t native_follow_route_opcode = 0x0A2E;
constexpr int32_t native_follow_timeout_ms = 999999;
constexpr int32_t native_move_walk = 4;
constexpr int32_t native_move_run = 6;
constexpr float source_map_z_low_limit = -100.0f;
constexpr float invalid_ground_z_sentinel = -1000.0f;
constexpr float native_ground_query_height = 1000.0f;
constexpr float source_default_abort_radius = 20.0f;
constexpr int32_t native_pedstate_attack = 16;
constexpr int32_t native_pedstate_fight = 17;

int32_t float_word(float value) {
    int32_t word = 0;
    static_assert(sizeof(word) == sizeof(value));
    std::memcpy(&word, &value, sizeof(word));
    return word;
}

float word_float(int32_t word) {
    float value = 0.0f;
    std::memcpy(&value, &word, sizeof(value));
    return value;
}

NativeResult refused(const char* diagnostic, bool entered = false) {
    NativeResult result;
    result.native_entered = entered;
    result.diagnostic = diagnostic;
    return result;
}

bool valid_destination(const std::array<float, 3>& destination) {
    for (const float value : destination) if (!std::isfinite(value)) return false;
    return true;
}

bool source_gate_allows_follow(const CharFollowPathPedFacts& facts, int32_t ped) {
    return facts.actor.handle == ped && facts.actor.native_ped && facts.controllable && facts.on_foot &&
        facts.native_state != native_pedstate_attack && facts.native_state != native_pedstate_fight;
}

std::optional<CharFollowPathPedFacts> resolve(const CharFollowPathBackend& backend, int32_t ped) {
    if (!backend.resolve_actor) return {};
    try { return backend.resolve_actor(ped); }
    catch (...) { return {}; }
}

std::optional<ScriptTaskSnapshot> snapshot(const CharFollowPathBackend& backend) {
    if (!backend.task_snapshot) return {};
    try { return backend.task_snapshot(); }
    catch (...) { return {}; }
}

NativeResult invoke(const CharFollowPathBackend& backend, NativeCall call) {
    if (!backend.execute) return refused("CHAR_FOLLOW_PATH has no native SA executor");
    try { return backend.execute(call); }
    catch (...) { return refused("Native SA executor threw during CHAR_FOLLOW_PATH", true); }
}
}

NativeResult execute_char_follow_path(int32_t ped, const std::array<float, 3>& requested_destination,
        float abort_radius, int32_t mode, bool mission_context, const CharFollowPathBackend& backend) {
    if (ped < 0 || !valid_destination(requested_destination) || !std::isfinite(abort_radius))
        return refused("CHAR_FOLLOW_PATH packet contains an invalid ped or non-finite coordinate/radius");
    if (!backend.execute || !backend.resolve_actor)
        return refused("CHAR_FOLLOW_PATH actor or native executor provider is unavailable");

    auto actor = resolve(backend, ped);
    if (!actor || actor->actor.handle != ped || !actor->actor.native_ped)
        return refused("CHAR_FOLLOW_PATH cannot resolve a live SA ped generation");
    if (!source_gate_allows_follow(*actor, ped)) {
        // Retail returns normally without clearing or assigning when the ped is
        // attacking, fighting, or not under script foot control.
        NativeResult ignored;
        ignored.executed = true;
        return ignored;
    }

    auto destination = requested_destination;
    bool entered = false;
    if (destination[2] <= source_map_z_low_limit) {
        NativeCall ground;
        ground.opcode = native_ground_z_opcode;
        ground.inputs = {float_word(destination[0]), float_word(destination[1]),
            float_word(native_ground_query_height)};
        ground.outputs = 1;
        ground.mission_context = mission_context;
        const auto ground_result = invoke(backend, std::move(ground));
        entered = entered || ground_result.native_entered;
        if (!ground_result.executed) {
            auto result = ground_result;
            result.native_entered = entered;
            if (result.diagnostic.empty()) result.diagnostic = "SA ground projection failed before CHAR_FOLLOW_PATH clear";
            return result;
        }
        destination[2] = word_float(ground_result.values[0]);
        if (!std::isfinite(destination[2]) || destination[2] <= invalid_ground_z_sentinel)
            return refused("SA ground projection returned an invalid height", entered);

        const auto refreshed = resolve(backend, ped);
        if (!refreshed || !(refreshed->actor == actor->actor))
            return refused("CHAR_FOLLOW_PATH actor changed during ground projection", entered);
        if (!source_gate_allows_follow(*refreshed, ped)) {
            NativeResult ignored;
            ignored.executed = true;
            ignored.native_entered = entered;
            return ignored;
        }
        actor = refreshed;
    }

    NativeCall clear;
    clear.opcode = native_clear_tasks_opcode;
    clear.inputs = {ped};
    clear.mission_context = mission_context;
    const auto clear_result = invoke(backend, std::move(clear));
    entered = entered || clear_result.native_entered || clear_result.executed;
    if (!clear_result.executed) {
        auto result = clear_result;
        result.native_entered = entered;
        if (result.diagnostic.empty()) result.diagnostic = "SA task clear failed before CHAR_FOLLOW_PATH assignment";
        return result;
    }

    const auto refreshed = resolve(backend, ped);
    if (!refreshed || !(refreshed->actor == actor->actor))
        return refused("CHAR_FOLLOW_PATH actor changed after SA task clear", entered);
    if (!source_gate_allows_follow(*refreshed, ped))
        return refused("CHAR_FOLLOW_PATH actor left source foot control after SA task clear", entered);

    // Take the pre-assignment snapshot after clear, so task/event changes made
    // by the clear itself cannot be mistaken for the new route's receipt. This
    // receipt is advisory: an unavailable observer must not suppress the route.
    const auto before = snapshot(backend);

    const float native_radius = abort_radius > 0.0f ? abort_radius : source_default_abort_radius;
    NativeCall route;
    route.opcode = native_follow_route_opcode;
    route.inputs = {ped, float_word(destination[0]), float_word(destination[1]), float_word(destination[2]),
        mode == 1 ? native_move_run : native_move_walk, native_follow_timeout_ms, float_word(native_radius)};
    route.mission_context = mission_context;
    auto result = invoke(backend, std::move(route));
    entered = entered || result.native_entered || result.executed;
    result.native_entered = entered;
    if (!result.executed) {
        if (result.diagnostic.empty()) result.diagnostic = "SA follow-node route failed after CHAR_FOLLOW_PATH clear";
        return result;
    }

    const auto after_actor = resolve(backend, ped);
    if (!after_actor || !(after_actor->actor == actor->actor)) {
        result.task_capture = NativeResult::TaskCapture::actor_changed;
        result.accepted_script_task.reset();
        if (result.diagnostic.empty()) result.diagnostic = "Native route ran, but its actor generation changed before ownership capture";
        return result;
    }
    const auto after = snapshot(backend);
    if (!after) {
        result.task_capture = before ? NativeResult::TaskCapture::unavailable_after :
            NativeResult::TaskCapture::unavailable_before;
        result.accepted_script_task.reset();
        if (result.diagnostic.empty()) result.diagnostic = before ?
            "Native route ran, but the task observer could not refresh ownership" :
            "Native route ran, but task ownership snapshots were unavailable";
        return result;
    }

    if (!before) {
        result.task_capture = NativeResult::TaskCapture::unavailable_before;
        result.accepted_script_task.reset();
        if (result.diagnostic.empty()) result.diagnostic = "Native route ran, but its pre-assignment task snapshot was unavailable";
        return result;
    }

    result.accepted_script_task = capture_accepted_script_task(*before, *after,
        uint32_t(actor->actor.native_ped), native_follow_route_opcode);
    result.task_capture = result.accepted_script_task ? NativeResult::TaskCapture::captured :
        NativeResult::TaskCapture::no_unique_record;
    if (result.accepted_script_task && result.accepted_script_task->record.task) {
        std::optional<bool> active;
        if (backend.task_active) {
            try { active = backend.task_active(actor->actor, *result.accepted_script_task); }
            catch (...) { active.reset(); }
        }
        if (!active || !*active) {
            result.accepted_script_task.reset();
            result.task_capture = active ? NativeResult::TaskCapture::no_unique_record :
                NativeResult::TaskCapture::unavailable_after;
            if (result.diagnostic.empty()) result.diagnostic = active ?
                "0A2E bank receipt is no longer the actor's active native task" :
                "0A2E bank receipt was captured, but active-task ownership is unavailable";
            return result;
        }
    }
    if (!result.accepted_script_task && result.diagnostic.empty())
        result.diagnostic = "Native route ran, but no unique 0A2E task-bank receipt was observed";
    return result;
}

#ifndef LCS_ISOLATED_CHAR_FOLLOW_PATH_TEST
namespace {
bool read_native_ped_state(uintptr_t ped, int32_t* state) {
    if (!ped || !state || ped > UINTPTR_MAX - 0x530) return false;
    __try {
        *state = *reinterpret_cast<const int32_t*>(ped + 0x530);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::optional<CharFollowPathPedFacts> live_char_follow_path_ped(int32_t handle) {
    const auto actor = sa_live_objective_actor(handle);
    if (!actor) return {};
    const auto pose = sa_route_actor_snapshot(*actor);
    if (!pose || !(pose->actor == *actor)) return {};
    int32_t state = -1;
    if (!read_native_ped_state(actor->native_ped, &state)) return {};
    return CharFollowPathPedFacts{*actor, pose->source_foot_controllable, pose->on_foot, state};
}
}

NativeResult sa_char_follow_path(int32_t ped, const std::array<float, 3>& destination,
        float abort_radius, int32_t mode, bool mission_context, const NativeExecutor& execute) {
    CharFollowPathBackend backend;
    backend.resolve_actor = live_char_follow_path_ped;
    backend.task_snapshot = sa_script_task_snapshot;
    backend.task_active = sa_active_walking_task;
    backend.execute = execute;
    return execute_char_follow_path(ped, destination, abort_radius, mode, mission_context, backend);
}
#endif

} // namespace lcs
