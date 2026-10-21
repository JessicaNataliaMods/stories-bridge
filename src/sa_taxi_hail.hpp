#pragma once

#include "lcs/native.hpp"
#include "lcs/sa_executor.hpp"

#include <functional>
#include <unordered_map>

namespace lcs {

struct TaxiWaitSfx {
    enum class Kind : uint8_t { unavailable, no_sample, sample };
    Kind kind = Kind::unavailable;
    int32_t id = -1;
};

#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
enum class TaxiHailTestTaskState : uint8_t { unknown, lost, suspended, active };
enum class TaxiHailTestAssociationState : uint8_t { unknown, absent, present };
struct SaTaxiHailTestHooks {
    std::function<std::optional<uint8_t>(uintptr_t)> task_pool_flag;
    std::function<TaxiHailTestTaskState(uintptr_t, uint8_t)> task_state;
    std::function<bool(uintptr_t, uintptr_t&, uint8_t&)> script_event_task;
    std::function<bool(const ObjectiveActor&)> animation_ready;
    std::function<TaxiHailTestAssociationState(uintptr_t, uintptr_t, uintptr_t)> association_state;
    std::function<bool(const ObjectiveActor&, uintptr_t, uint8_t,
                       uintptr_t&, uintptr_t&)> play_gesture;
    std::function<bool(const ObjectiveActor&, uintptr_t, uintptr_t)> fade_gesture;
};
#endif

// Owns only the source hail cycle. SA owns its stand-still task and animation
// manager; the supplied audio callbacks resolve/play the source taxi-wait SFX.
class SaTaxiHail {
public:
    using ResolveTaxiWaitSfx = std::function<TaxiWaitSfx(const ObjectiveActor&)>;
    using PlayPositionedSfx = std::function<bool(int32_t, const std::array<float, 3>&)>;

    SaTaxiHail(ResolveTaxiWaitSfx resolve_sfx = {}, PlayPositionedSfx play_sfx = {});
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
    SaTaxiHail(SaTaxiHailTestHooks test_hooks, ResolveTaxiWaitSfx resolve_sfx = {});
#endif

    // execute must be the normal SA native executor for this script thread.
    // Capture its exact task record or pending mission event before registering
    // the repeating source gesture with this owner.
    NativeResult start(int32_t handle, bool mission_context, const NativeExecutor& execute);

    // Called on the game thread once per frame. False means the cycle remains
    // owned but a required native observation, animation operation, or audio
    // callback was unavailable/failed.
    bool tick();

    // Stops this module's gesture/audio cycle. SA's stand-still task naturally
    // ends when another native task replaces it; this does not clear foreign tasks.
    bool clear(int32_t handle);
    bool clear_all();

private:
    struct Cycle {
        ObjectiveActor actor;
        CapturedScriptTask task;
        uint32_t next_gesture_at = 0;
        uintptr_t animation = 0;
        uintptr_t hierarchy = 0;
        uintptr_t pending_task = 0;
        uint8_t task_pool_flag = 0;
        bool owns_animation = false;
        bool pending_event = false;
        bool has_task_pool_flag = false;
    };

    enum class SoundResult : uint8_t { ok, unavailable, failed };

    ResolveTaxiWaitSfx resolve_sfx_;
    PlayPositionedSfx play_sfx_;
    std::unordered_map<int32_t, Cycle> cycles_;
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
    SaTaxiHailTestHooks test_hooks_;
#endif

    SoundResult say_taxi_wait(const ObjectiveActor& actor);
    bool play_hail_gesture(Cycle& cycle);
    bool fade_owned_gesture(const Cycle& cycle);
    bool retire(Cycle& cycle);
};

} // namespace lcs
