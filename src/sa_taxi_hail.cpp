#include <windows.h>

#include "sa_taxi_hail.hpp"
#include "lcs/native_memory.hpp"

#include <cmath>
#include <cstring>
#include <utility>
#include <vector>

namespace lcs {
namespace {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
using HailTestHooks = SaTaxiHailTestHooks;
#else
struct HailTestHooks {};
#endif
constexpr uint16_t taxi_hail_opcode = 0x05BA; // TASK_STAND_STILL
constexpr uint32_t hail_interval_ms = 2000;
constexpr uintptr_t anim_block_lookup = 0x4D3940;
constexpr uintptr_t anim_hierarchy_lookup = 0x4D42F0;
constexpr uintptr_t anim_blend = 0x4D4410;

enum class TaskState : uint8_t { unknown, lost, suspended, active };
enum class AssociationState : uint8_t { unknown, absent, present };
struct AssociationLookup {
    AssociationState state = AssociationState::unknown;
    uintptr_t animation = 0;
};

bool profile(uintptr_t address, const uint8_t* expected, size_t size) noexcept {
    return !std::memcmp(reinterpret_cast<const void*>(native_entity_address(address)), expected, size);
}

bool animation_api_profile() noexcept {
    // PE prefixes from the approved US SA executable. GetAnimationBlock
    // returns a CAnimBlock pointer; the following body prefix and base-relative
    // table references distinguish it from GetAnimationBlockIndex (0x4D3990).
    const uint8_t block_entry[] = {0xE9,0xB5,0xDE,0xF2,0xFF};
    const uint8_t block_body[] = {0x53,0x56,0x33,0xF6,0x85,0xC0};
    const uint8_t hierarchy_lookup[] = {0x8B,0x44,0x24,0x04,0x56,0x50};
    const uint8_t blend[] = {0x6A,0xFF,0x68,0x6B,0xBD,0x83,0x00};
    return profile(0x4D3940, block_entry, sizeof(block_entry)) &&
        profile(0x4D3945, block_body, sizeof(block_body)) &&
        profile(0x4D42F0, hierarchy_lookup, sizeof(hierarchy_lookup)) &&
        profile(0x4D4410, blend, sizeof(blend));
}

int task_pool_slot_state(uintptr_t first, uintptr_t flags, uint32_t count,
                         uintptr_t task) noexcept {
    if (!task || task < first || (task - first) % 128 ||
        (task - first) / 128 >= count)
        return 0;
    const auto slot = (task - first) / 128;
    return (*reinterpret_cast<const uint8_t*>(flags + slot) & 0x80) ? 0 : 1;
}

bool read_task_pool_flag(uintptr_t task, uint8_t* out_flag,
                         const HailTestHooks* hooks = nullptr) noexcept {
    if (!task || !out_flag) return false;
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
    if (hooks && hooks->task_pool_flag) {
        const auto flag = hooks->task_pool_flag(task);
        if (!flag || (*flag & 0x80)) return false;
        *out_flag = *flag;
        return true;
    }
#else
    (void)hooks;
#endif
    __try {
        const uint8_t allocator[] = {0x8B,0x0D,0xA8,0x44,0xB7,0x00,0xE9,0x55,0xFF,0xFF,0xFF};
        const uint8_t stride[] = {0xC1,0xE0,0x07};
        if (!task || !out_flag || !profile(0x61A5A0, allocator, sizeof(allocator)) ||
            !profile(0x61A55A, stride, sizeof(stride))) return false;
        const auto pool = *reinterpret_cast<const uintptr_t*>(native_entity_address(0xB744A8));
        if (!pool) return false;
        const auto first = *reinterpret_cast<const uintptr_t*>(pool);
        const auto flags = *reinterpret_cast<const uintptr_t*>(pool + 4);
        const auto count = *reinterpret_cast<const int32_t*>(pool + 8);
        if (!first || !flags || count <= 0 || count > 0x7FFFFF || task < first ||
            (task - first) % 128 || (task - first) / 128 >= uint32_t(count)) return false;
        const auto slot = (task - first) / 128;
        if (slot > UINTPTR_MAX - flags) return false;
        const auto flag = *reinterpret_cast<const uint8_t*>(flags + slot);
        if (flag & 0x80) return false;
        *out_flag = flag;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// missionGivePedScriptedTask can first publish a real CEventCommandScript
// record, then associate its CTask on a later AI frame. Retain the event as
// the command owner, but dereference its embedded task only after validating
// the exact SA event vtable, GetType implementation and live CTask pool slot.
bool resolve_script_event_task(uintptr_t event, uintptr_t* task, uint8_t* task_pool_flag,
                               const HailTestHooks* hooks = nullptr) noexcept {
    if (!task || !task_pool_flag || !event) return false;
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
    if (hooks && hooks->script_event_task) {
        return hooks->script_event_task(event, *task, *task_pool_flag);
    }
#else
    (void)hooks;
#endif
    __try {
        const uint8_t event_type[] = {0xB8,0x20,0x00,0x00,0x00,0xC3};
        if (!task || !task_pool_flag || !event || event > UINTPTR_MAX - 0x10 ||
            !profile(0x4B0A30, event_type, sizeof(event_type))) return false;
        if (*reinterpret_cast<const uintptr_t*>(event) != 0x85B378) return false;
        const auto get_type = reinterpret_cast<int32_t(__thiscall*)(uintptr_t)>(
            native_entity_address(0x4B0A30));
        if (get_type(event) != 32) return false;
        const auto pending = *reinterpret_cast<const uintptr_t*>(event + 0x10);
        uint8_t flag = 0;
        if (pending && !read_task_pool_flag(pending, &flag, hooks)) return false;
        *task = pending;
        *task_pool_flag = flag;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool resolve_pending_script_event(const ScriptTaskRecord& record, uintptr_t* task,
                                 uint8_t* task_pool_flag,
                                 const HailTestHooks* hooks = nullptr) noexcept {
    if (record.status != 0 || !record.event || record.task) return false;
    return resolve_script_event_task(uintptr_t(record.event), task, task_pool_flag, hooks);
}

bool accepted_task_status(int32_t status) noexcept {
    return status == 1 || status == 2 || status == 4 || status == 5 || status == 6;
}

NativeResult fail_after_native_entry(NativeResult result, const char* diagnostic) {
    result.executed = false;
    result.native_entered = true;
    result.diagnostic = diagnostic;
    return result;
}

// The known SA CTask pool/primary-tree observer used by the route task owner,
// narrowed to this command's exact accepted task pointer. No virtual method is
// called on a task whose pool slot has not first been proved live.
TaskState native_task_state(uintptr_t ped, uintptr_t expected_task,
                            uint8_t expected_pool_flag,
                            const HailTestHooks* hooks = nullptr) noexcept {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
    if (hooks && hooks->task_state) {
        return static_cast<TaskState>(hooks->task_state(expected_task, expected_pool_flag));
    }
#else
    (void)hooks;
#endif
    __try {
        const uint8_t active_root[] = {0x33,0xC0,0x83,0x3C,0x81,0x00,0x75,0x09,0x40,0x83,0xF8,0x05};
        const uint8_t allocator[] = {0x8B,0x0D,0xA8,0x44,0xB7,0x00,0xE9,0x55,0xFF,0xFF,0xFF};
        const uint8_t stride[] = {0xC1,0xE0,0x07};
        if (!profile(0x681720, active_root, sizeof(active_root)) ||
            !profile(0x61A5A0, allocator, sizeof(allocator)) ||
            !profile(0x61A55A, stride, sizeof(stride)))
            return TaskState::unknown;

        const auto pool = *reinterpret_cast<const uintptr_t*>(native_entity_address(0xB744A8));
        if (!pool) return TaskState::unknown;
        const auto first = *reinterpret_cast<const uintptr_t*>(pool);
        const auto flags = *reinterpret_cast<const uintptr_t*>(pool + 4);
        const auto count = *reinterpret_cast<const int32_t*>(pool + 8);
        if (!first || !flags || count <= 0 || count > 0x7FFFFF) return TaskState::unknown;

        if (!expected_task || !task_pool_slot_state(first, flags, uint32_t(count), expected_task))
            return TaskState::lost;
        const auto expected_slot = (expected_task - first) / 128;
        if (expected_slot > UINTPTR_MAX - flags ||
            *reinterpret_cast<const uint8_t*>(flags + expected_slot) != expected_pool_flag)
            return TaskState::lost;

        const auto intelligence = *reinterpret_cast<const uintptr_t*>(ped + 0x47C);
        if (!intelligence) return TaskState::unknown;
        uintptr_t task = 0;
        for (size_t slot = 0; slot < 5; ++slot) {
            task = *reinterpret_cast<const uintptr_t*>(intelligence + 4 + slot * 4);
            if (task) break;
        }

        uintptr_t previous = 0;
        for (size_t depth = 0; task && depth < 32; ++depth) {
            if (!task_pool_slot_state(first, flags, uint32_t(count), task) ||
                *reinterpret_cast<const uintptr_t*>(task + 4) != previous)
                return TaskState::unknown;
            if (task == expected_task) return TaskState::active;

            const auto table = *reinterpret_cast<const uintptr_t*>(task);
            if (!table) return TaskState::unknown;
            const auto getter = *reinterpret_cast<const uintptr_t*>(table + 8);
            const uint8_t simple_getter[] = {0x33,0xC0,0xC3};
            const uint8_t complex_getter[] = {0x8B,0x41,0x08,0xC3};
            if (profile(getter, simple_getter, sizeof(simple_getter)))
                return TaskState::suspended;
            if (!profile(getter, complex_getter, sizeof(complex_getter)))
                return TaskState::unknown;
            previous = task;
            task = *reinterpret_cast<const uintptr_t*>(task + 8);
        }
        return task ? TaskState::unknown : TaskState::suspended;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return TaskState::unknown;
    }
}

bool read_ped_position(uintptr_t ped, std::array<float, 3>* position) noexcept {
    __try {
        if (!ped || !position) return false;
        const auto matrix = *reinterpret_cast<const uintptr_t*>(ped + 0x14);
        if (matrix && matrix > UINTPTR_MAX - 0x3C) return false;
        std::memcpy(position->data(), reinterpret_cast<const void*>(matrix ? matrix + 0x30 : ped + 4), 12);
        return std::isfinite((*position)[0]) && std::isfinite((*position)[1]) &&
            std::isfinite((*position)[2]);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Read the ped's current association list using the same Rw-clump plugin
// layout already used by the native gameplay-animation owner.
AssociationState association_state(uintptr_t ped, uintptr_t association,
                                   uintptr_t hierarchy,
                                   const HailTestHooks* hooks = nullptr) noexcept {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
    if (hooks && hooks->association_state) {
        return static_cast<AssociationState>(hooks->association_state(ped, association, hierarchy));
    }
#else
    (void)hooks;
#endif
    __try {
        if (!ped || !association || !hierarchy) return AssociationState::absent;
        const auto clump = *reinterpret_cast<const uintptr_t*>(ped + 0x18);
        const auto offset = *reinterpret_cast<const uint32_t*>(native_entity_address(0xB5F878));
        if (!clump || offset > 4096) return AssociationState::unknown;
        const auto list = *reinterpret_cast<const uintptr_t*>(clump + offset);
        if (!list) return AssociationState::unknown;
        auto link = *reinterpret_cast<const uintptr_t*>(list);
        unsigned visited = 0;
        while (link) {
            if (++visited > 1024) return AssociationState::unknown;
            const auto candidate = link - 4;
            if (candidate == association)
                return *reinterpret_cast<const uintptr_t*>(candidate + 0x14) == hierarchy
                    ? AssociationState::present : AssociationState::absent;
            link = *reinterpret_cast<const uintptr_t*>(link);
        }
        return AssociationState::absent;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return AssociationState::unknown;
    }
}

AssociationLookup find_hail_association(uintptr_t ped, uintptr_t hierarchy) noexcept {
    __try {
        if (!ped || !hierarchy) return {};
        const auto clump = *reinterpret_cast<const uintptr_t*>(ped + 0x18);
        const auto offset = *reinterpret_cast<const uint32_t*>(native_entity_address(0xB5F878));
        if (!clump || offset > 4096) return {};
        const auto list = *reinterpret_cast<const uintptr_t*>(clump + offset);
        if (!list) return {};
        auto link = *reinterpret_cast<const uintptr_t*>(list);
        unsigned visited = 0;
        while (link) {
            if (++visited > 1024) return {};
            const auto association = link - 4;
            if (*reinterpret_cast<const uintptr_t*>(association + 0x14) == hierarchy)
                return {AssociationState::present, association};
            link = *reinterpret_cast<const uintptr_t*>(link);
        }
        return {AssociationState::absent, 0};
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return {};
    }
}

uintptr_t resolve_hail_hierarchy() noexcept {
    __try {
        if (!animation_api_profile()) return 0;
        const auto block = reinterpret_cast<uintptr_t(__cdecl*)(const char*)>(
            native_entity_address(anim_block_lookup))("PED");
        if (!block || !*reinterpret_cast<const uint8_t*>(block + 0x10)) return 0;
        return reinterpret_cast<uintptr_t(__cdecl*)(const char*, uintptr_t)>(
            native_entity_address(anim_hierarchy_lookup))("IDLE_taxi", block);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

bool animation_ready(const ObjectiveActor& actor,
                     const HailTestHooks* hooks = nullptr) noexcept {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
    if (hooks && hooks->animation_ready) {
        return hooks->animation_ready(actor);
    }
#else
    (void)hooks;
#endif
    __try {
        if (!actor.native_ped) return false;
        const auto clump = *reinterpret_cast<const uintptr_t*>(actor.native_ped + 0x18);
        const auto hierarchy = resolve_hail_hierarchy();
        return clump && hierarchy &&
            find_hail_association(actor.native_ped, hierarchy).state != AssociationState::unknown;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool captured_task_is_live(const ObjectiveActor& actor,
                           const CapturedScriptTask& captured,
                           uint8_t expected_pool_flag,
                           const HailTestHooks* hooks = nullptr) noexcept {
    if (captured.slot >= 128 || !captured.record.task || captured.record.event ||
        captured.record.opcode != taxi_hail_opcode || captured.record.ped != actor.native_ped)
        return false;
    const auto live = sa_live_objective_actor(actor.handle);
    if (!live || !(*live == actor)) return false;
    const auto snapshot = sa_script_task_snapshot();
    if (!snapshot) return false;
    const auto& record = (*snapshot)[captured.slot];
    if (record.opcode != taxi_hail_opcode || record.ped != actor.native_ped || record.event ||
        record.task != captured.record.task || !accepted_task_status(record.status))
        return false;
    const auto state = native_task_state(actor.native_ped, captured.record.task,
                                         expected_pool_flag, hooks);
    return state == TaskState::active || state == TaskState::suspended;
}

bool blend_hail_animation(const ObjectiveActor& actor, uintptr_t hierarchy,
                          uintptr_t* association) noexcept {
    __try {
        if (!actor.native_ped || !hierarchy || !association || !animation_api_profile()) return false;
        const auto clump = *reinterpret_cast<const uintptr_t*>(actor.native_ped + 0x18);
        if (!clump) return false;
        // Source HAIL_TAXI is a repeated gesture, not an infinite idle. Keep
        // SA's PED animation non-looping and restart it after its association
        // ends, on the original two-second cadence.
        const auto result = reinterpret_cast<uintptr_t(__cdecl*)(uintptr_t, uintptr_t, int, float)>(
            native_entity_address(anim_blend))(clump, hierarchy, 8, 4.0f); // finish-auto-remove, no loop
        if (!result) return false;
        *association = result;
        *reinterpret_cast<float*>(result + 0x24) = 1.0f;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool fade_association(uintptr_t ped, uintptr_t association, uintptr_t hierarchy) noexcept {
    __try {
        if (!ped || !association || !hierarchy) return true;
        const auto state = association_state(ped, association, hierarchy);
        if (state == AssociationState::absent) return true;
        if (state == AssociationState::unknown) return false;
        *reinterpret_cast<float*>(association + 0x1C) = -8.0f;
        *reinterpret_cast<uint16_t*>(association + 0x2E) |= 4; // blend-auto-remove (PE4D1446)
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace

SaTaxiHail::SaTaxiHail(ResolveTaxiWaitSfx resolve_sfx, PlayPositionedSfx play_sfx)
    : resolve_sfx_(std::move(resolve_sfx)), play_sfx_(std::move(play_sfx)) {}
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
SaTaxiHail::SaTaxiHail(SaTaxiHailTestHooks test_hooks, ResolveTaxiWaitSfx resolve_sfx)
    : resolve_sfx_(std::move(resolve_sfx)), test_hooks_(std::move(test_hooks)) {}
#endif

SaTaxiHail::SoundResult SaTaxiHail::say_taxi_wait(const ObjectiveActor& actor) {
    if (!resolve_sfx_) return SoundResult::unavailable;
    const auto before = sa_live_objective_actor(actor.handle);
    if (!before || !(*before == actor)) return SoundResult::failed;
    TaxiWaitSfx sound;
    try { sound = resolve_sfx_(actor); }
    catch (...) { return SoundResult::failed; }
    if (sound.kind == TaxiWaitSfx::Kind::unavailable) return SoundResult::unavailable;
    if (sound.kind == TaxiWaitSfx::Kind::no_sample) return SoundResult::ok;
    if (sound.kind != TaxiWaitSfx::Kind::sample || sound.id < 0 || !play_sfx_)
        return SoundResult::unavailable;

    const auto live = sa_live_objective_actor(actor.handle);
    if (!live || !(*live == actor)) return SoundResult::failed;
    std::array<float, 3> position{};
    if (!read_ped_position(actor.native_ped, &position)) return SoundResult::failed;
    try { return play_sfx_(sound.id, position) ? SoundResult::ok : SoundResult::failed; }
    catch (...) { return SoundResult::failed; }
}

bool SaTaxiHail::play_hail_gesture(Cycle& cycle) {
    const HailTestHooks* hooks = nullptr;
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
    hooks = &test_hooks_;
    if (test_hooks_.play_gesture) {
        const auto live_test = sa_live_objective_actor(cycle.actor.handle);
        if (!live_test || !(*live_test == cycle.actor) || !cycle.has_task_pool_flag ||
            native_task_state(cycle.actor.native_ped, cycle.task.record.task,
                              cycle.task_pool_flag, hooks) != TaskState::active) return false;
        uintptr_t association = 0, hierarchy = 0;
        bool played = false;
        try {
            played = test_hooks_.play_gesture(cycle.actor, cycle.task.record.task,
                cycle.task_pool_flag, association, hierarchy);
        } catch (...) { return false; }
        if (association && hierarchy) {
            cycle.animation = association;
            cycle.hierarchy = hierarchy;
            cycle.owns_animation = true;
        }
        return played && association && hierarchy;
    }
#endif
    const auto live = sa_live_objective_actor(cycle.actor.handle);
    if (!live || !(*live == cycle.actor) || !cycle.has_task_pool_flag ||
        native_task_state(cycle.actor.native_ped, cycle.task.record.task,
                          cycle.task_pool_flag, hooks) != TaskState::active) return false;
    const auto hierarchy = resolve_hail_hierarchy();
    if (!hierarchy) return false;

    if (cycle.animation && cycle.hierarchy == hierarchy) {
        const auto current = association_state(cycle.actor.native_ped, cycle.animation, hierarchy, hooks);
        if (current == AssociationState::present) return true;
        if (current == AssociationState::unknown) return false;
        cycle.animation = 0;
        cycle.hierarchy = 0;
        cycle.owns_animation = false;
    }

    // Preserve an association created by SA or another script. The source
    // gesture will wait for it to finish, then create its own next wave.
    const auto existing = find_hail_association(cycle.actor.native_ped, hierarchy);
    if (existing.state == AssociationState::unknown) return false;
    if (existing.state == AssociationState::present) {
        cycle.animation = existing.animation;
        cycle.hierarchy = hierarchy;
        cycle.owns_animation = false;
        return true;
    }

    uintptr_t association = 0;
    const bool blended = blend_hail_animation(cycle.actor, hierarchy, &association);
    if (association) {
        cycle.animation = association;
        cycle.hierarchy = hierarchy;
        cycle.owns_animation = true;
    }
    return blended && association;
}

bool SaTaxiHail::fade_owned_gesture(const Cycle& cycle) {
    if (!cycle.owns_animation || !cycle.animation || !cycle.hierarchy) return true;
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
    if (test_hooks_.fade_gesture) {
        const auto live_test = sa_live_objective_actor(cycle.actor.handle);
        if (!live_test || !(*live_test == cycle.actor)) return true;
        if (!cycle.has_task_pool_flag) return false;
        const auto task_state = native_task_state(cycle.actor.native_ped, cycle.task.record.task,
            cycle.task_pool_flag, &test_hooks_);
        if (task_state == TaskState::unknown) return false;
        if (task_state == TaskState::lost) return true;
        try { return test_hooks_.fade_gesture(cycle.actor, cycle.animation, cycle.hierarchy); }
        catch (...) { return false; }
    }
#endif
    const auto live = sa_live_objective_actor(cycle.actor.handle);
    if (!live || !(*live == cycle.actor)) return true;
    if (!cycle.has_task_pool_flag) return false;
    const auto state = native_task_state(cycle.actor.native_ped, cycle.task.record.task,
                                         cycle.task_pool_flag
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
                                         , &test_hooks_
#endif
                                         );
    if (state == TaskState::unknown) return false;
    if (state == TaskState::lost) return true; // The animation may outlive a reused task-pool slot.
    return fade_association(cycle.actor.native_ped, cycle.animation, cycle.hierarchy);
}

bool SaTaxiHail::retire(Cycle& cycle) {
    return fade_owned_gesture(cycle);
}

NativeResult SaTaxiHail::start(int32_t handle, bool mission_context, const NativeExecutor& execute) {
    NativeResult result;
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
    const HailTestHooks* hooks = &test_hooks_;
#else
    const HailTestHooks* hooks = nullptr;
#endif
    if (!sa_executor_profile_matches() || handle < 0 || !execute) {
        result.diagnostic = "Taxi hail requires a live SA executor and script-thread backend";
        return result;
    }
    const auto actor = sa_live_objective_actor(handle);
    if (!actor) {
        result.diagnostic = "Taxi hail actor identity is unavailable";
        return result;
    }
    const auto clock = sa_frame_clock();
    const auto before = sa_script_task_snapshot();
    if (!clock || !before || !animation_ready(*actor, hooks)) {
        result.diagnostic = "Taxi hail task, frame clock, or PED/IDLE_taxi consumer is unavailable";
        return result;
    }

    NativeCall stand;
    stand.opcode = taxi_hail_opcode;
    stand.inputs = {handle, -1}; // Native infinite TASK_STAND_STILL.
    stand.mission_context = mission_context;
    result = execute(stand); // Exactly one real SA assignment.
    if (!result.executed) return result;
    result.native_entered = true;

    result.accepted_script_task.reset();
    const auto current = sa_live_objective_actor(handle);
    if (!current || !(*current == *actor)) {
        result.task_capture = NativeResult::TaskCapture::actor_changed;
        auto old = cycles_.find(handle);
        if (old != cycles_.end()) {
            retire(old->second);
            cycles_.erase(old);
        }
        return fail_after_native_entry(std::move(result),
            "SA accepted stand-still after the taxi hail actor identity changed");
    }
    const auto after = sa_script_task_snapshot();
    if (!after) {
        result.task_capture = NativeResult::TaskCapture::unavailable_after;
        return fail_after_native_entry(std::move(result),
            "SA accepted stand-still but its task assignment could not be observed");
    }

    auto captured = capture_accepted_script_task(*before, *after,
        uint32_t(actor->native_ped), taxi_hail_opcode);
    bool captured_pending_event = false;
    uintptr_t captured_pending_task = 0;
    uint8_t captured_pool_flag = 0;
    bool captured_has_pool_flag = false;
    if (captured) {
        const auto& record = captured->record;
        if (record.task && !record.event && accepted_task_status(record.status)) {
            captured_has_pool_flag = read_task_pool_flag(record.task, &captured_pool_flag, hooks);
            if (!captured_has_pool_flag) captured.reset();
        } else if (record.status == 0 && record.event && !record.task &&
                   resolve_pending_script_event(record, &captured_pending_task, &captured_pool_flag, hooks)) {
            // Own the accepted script event while it constructs/associates its
            // CTask on the next AI frame. Do not treat the CEvent as a CTask.
            captured_pending_event = true;
            captured_has_pool_flag = captured_pending_task != 0;
        } else {
            captured.reset();
        }
    }

    Cycle cycle;
    cycle.actor = *actor;
    bool retained_previous = false;
    if (captured) {
        cycle.task = *captured;
        cycle.pending_event = captured_pending_event;
        cycle.pending_task = captured_pending_task;
        cycle.task_pool_flag = captured_pool_flag;
        cycle.has_task_pool_flag = captured_has_pool_flag;
        result.accepted_script_task = *captured;
        result.task_capture = NativeResult::TaskCapture::captured;
    } else {
        const auto previous = cycles_.find(handle);
        if (previous != cycles_.end() && previous->second.actor == *actor &&
            previous->second.task.slot < after->size()) {
            const auto& record = (*after)[previous->second.task.slot];
            if (record.opcode == taxi_hail_opcode && record.ped == actor->native_ped) {
                if (previous->second.pending_event) {
                    uintptr_t linked_task = previous->second.pending_task;
                    uint8_t linked_flag = previous->second.task_pool_flag;
                    bool linked_has_flag = previous->second.has_task_pool_flag;
                    if (!linked_task && resolve_script_event_task(
                            previous->second.task.record.event, &linked_task, &linked_flag, hooks))
                        linked_has_flag = linked_task != 0;
                    uintptr_t event_task = 0;
                    uint8_t event_flag = 0;
                    const auto pending_record = record.status == 0 && record.event && !record.task &&
                        record.event == previous->second.task.record.event &&
                        resolve_pending_script_event(record, &event_task, &event_flag, hooks) &&
                        (!previous->second.pending_task || !event_task ||
                         (event_task == previous->second.pending_task &&
                          event_flag == previous->second.task_pool_flag));
                    uint8_t current_flag = 0;
                    const auto associated_task = !record.event && record.task &&
                        accepted_task_status(record.status) && linked_task && linked_has_flag &&
                        record.task == linked_task && read_task_pool_flag(record.task, &current_flag, hooks) &&
                        current_flag == linked_flag;
                    if (pending_record || associated_task) {
                        cycle = previous->second;
                        cycle.task.record = record;
                        if (associated_task) {
                            cycle.pending_event = false;
                            cycle.pending_task = 0;
                            cycle.task_pool_flag = linked_flag;
                            cycle.has_task_pool_flag = true;
                        } else if (event_task) {
                            cycle.pending_task = event_task;
                            cycle.task_pool_flag = event_flag;
                            cycle.has_task_pool_flag = true;
                        }
                        retained_previous = true;
                    }
                } else if (!record.event && record.task == previous->second.task.record.task &&
                           accepted_task_status(record.status) && previous->second.has_task_pool_flag) {
                    const auto state = native_task_state(actor->native_ped, record.task,
                                                         previous->second.task_pool_flag, hooks);
                    if (state == TaskState::active || state == TaskState::suspended) {
                        cycle = previous->second;
                        cycle.task.record = record;
                        retained_previous = true;
                    }
                }
                if (retained_previous) {
                    result.accepted_script_task = cycle.task;
                    result.task_capture = NativeResult::TaskCapture::captured;
                }
            }
        }
        if (!retained_previous) {
            result.task_capture = NativeResult::TaskCapture::no_unique_record;
            return fail_after_native_entry(std::move(result),
                "SA accepted stand-still but no unique task assignment can own the hail cycle");
        }
    }

    // Keep ownership of a still-running wave across a repeated stand-still
    // assignment for the same actor. The task binding changes, but the native
    // animation association remains the same object and must still be retired
    // by this owner when replaced or cancelled.
    const auto previous = cycles_.find(handle);
    if (previous != cycles_.end()) {
        if (previous->second.actor == cycle.actor) {
            if (!cycle.has_task_pool_flag) {
                if (!retire(previous->second)) {
                    return fail_after_native_entry(std::move(result),
                        "Taxi hail pending event could not retire the prior gesture safely");
                }
            } else if (!cycle.animation) {
                cycle.animation = previous->second.animation;
                cycle.hierarchy = previous->second.hierarchy;
                cycle.owns_animation = previous->second.owns_animation;
            }
        } else {
            if (!retire(previous->second)) {
                return fail_after_native_entry(std::move(result),
                    "Taxi hail task was accepted, but its prior owned gesture could not be retired");
            }
            cycles_.erase(previous);
        }
    }

    cycle.next_gesture_at = clock->now;
    // Record the exact task binding before invoking animation or audio APIs.
    // Those native callbacks can fail or re-enter script processing.
    auto owned = cycles_.insert_or_assign(handle, std::move(cycle));
    auto& registered = owned.first->second;
    if (registered.pending_event) {
        result.diagnostic = "SA accepted a pending script event; frame consumer owns the deferred hail";
        return result;
    }
    if (!registered.has_task_pool_flag) {
        return fail_after_native_entry(std::move(result),
            "Taxi hail task pool generation is unavailable");
    }
    const auto task_state = native_task_state(actor->native_ped, registered.task.record.task,
                                              registered.task_pool_flag, hooks);
    if (task_state == TaskState::unknown || task_state == TaskState::lost) {
        return fail_after_native_entry(std::move(result),
            "Taxi hail task was accepted but its native task lifetime is not provable");
    }
    if (task_state == TaskState::active) {
        if (!play_hail_gesture(registered)) {
            registered.next_gesture_at = clock->now + hail_interval_ms;
            return fail_after_native_entry(std::move(result),
                "Taxi hail task was accepted but PED/IDLE_taxi could not be blended");
        }
        registered.next_gesture_at = clock->now + hail_interval_ms;
        const auto registered_actor = registered.actor;
        const auto registered_task = registered.task;
        const auto registered_pool_flag = registered.task_pool_flag;
        const auto sound = say_taxi_wait(*actor);
        if (sound == SoundResult::unavailable)
            return fail_after_native_entry(std::move(result),
                "Taxi hail pose is active; SOUND_PED_TAXI_WAIT resolver is unavailable");
        if (sound == SoundResult::failed)
            return fail_after_native_entry(std::move(result),
                "Taxi hail pose is active; SOUND_PED_TAXI_WAIT playback failed");
        const auto still_registered = cycles_.find(handle);
        if (still_registered == cycles_.end() ||
            !(still_registered->second.actor == registered_actor) ||
            still_registered->second.task.slot != registered_task.slot ||
            still_registered->second.task.record.task != registered_task.record.task ||
            still_registered->second.task_pool_flag != registered_pool_flag ||
            !captured_task_is_live(registered_actor, registered_task,
                                   registered_pool_flag, hooks))
            return fail_after_native_entry(std::move(result),
                "Taxi hail audio callback changed the actor or native task binding");
    } else {
        result.diagnostic = "SA accepted stand-still; frame consumer waits for its captured task to become active";
        return result;
    }
    return result;
}

bool SaTaxiHail::tick() {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
    const HailTestHooks* hooks = &test_hooks_;
#else
    const HailTestHooks* hooks = nullptr;
#endif
    if (!sa_executor_profile_matches()) return false;
    const auto clock = sa_frame_clock();
    if (!clock) return false;
    bool ok = true;

    std::vector<int32_t> handles;
    handles.reserve(cycles_.size());
    for (const auto& entry : cycles_) handles.push_back(entry.first);

    for (const auto handle : handles) {
        auto it = cycles_.find(handle);
        if (it == cycles_.end()) continue;
        auto& cycle = it->second;
        const auto live = sa_live_objective_actor(cycle.actor.handle);
        if (!live || !(*live == cycle.actor)) {
            // The entity generation ended. Do not dereference/fade its former
            // address; all native associations died with the actor.
            cycles_.erase(it);
            continue;
        }

        if (cycle.task.slot >= 128 || cycle.task.record.opcode != taxi_hail_opcode ||
            cycle.task.record.ped != cycle.actor.native_ped ||
            (cycle.pending_event
                ? (!cycle.task.record.event || cycle.task.record.task || cycle.task.record.status != 0)
                : (cycle.task.record.event || !cycle.task.record.task))) {
            ok = false;
            continue;
        }
        const auto bank = sa_script_task_snapshot();
        if (!bank) {
            ok = false;
            continue;
        }
        const auto& record = (*bank)[cycle.task.slot];
        if (cycle.pending_event) {
            const bool same_event = record.opcode == taxi_hail_opcode &&
                record.ped == cycle.actor.native_ped && record.status == 0 &&
                record.event == cycle.task.record.event && !record.task;
            if (same_event) {
                uintptr_t event_task = 0;
                uint8_t event_flag = 0;
                if (!resolve_pending_script_event(record, &event_task, &event_flag, hooks) ||
                    (event_task && cycle.pending_task && event_task != cycle.pending_task) ||
                    (event_task && cycle.has_task_pool_flag &&
                     event_flag != cycle.task_pool_flag)) {
                    ok = false;
                    continue;
                }
                if (event_task) {
                    cycle.pending_task = event_task;
                    cycle.task_pool_flag = event_flag;
                    cycle.has_task_pool_flag = true;
                }
                continue; // The captured CEvent still owns task construction.
            }
            uintptr_t linked_task = cycle.pending_task;
            uint8_t linked_flag = cycle.task_pool_flag;
            bool linked_has_flag = cycle.has_task_pool_flag;
            if (!linked_task && resolve_script_event_task(
                    cycle.task.record.event, &linked_task, &linked_flag, hooks))
                linked_has_flag = linked_task != 0;
            uint8_t current_flag = 0;
            const bool associated_task = record.opcode == taxi_hail_opcode &&
                record.ped == cycle.actor.native_ped && !record.event && record.task &&
                accepted_task_status(record.status) && linked_task && linked_has_flag &&
                record.task == linked_task && read_task_pool_flag(record.task, &current_flag, hooks) &&
                current_flag == linked_flag;
            if (!associated_task) {
                if (!retire(cycle)) { ok = false; continue; }
                cycles_.erase(it);
                continue;
            }
            cycle.pending_event = false;
            cycle.pending_task = 0;
            cycle.task_pool_flag = linked_flag;
            cycle.has_task_pool_flag = true;
            cycle.task.record = record;
        }
        if (record.opcode != taxi_hail_opcode || record.ped != cycle.actor.native_ped ||
            record.event || record.task != cycle.task.record.task || !accepted_task_status(record.status)) {
            if (!retire(cycle)) { ok = false; continue; }
            cycles_.erase(it); // Another native task replaced/cancelled our owner.
            continue;
        }

        if (!cycle.has_task_pool_flag) {
            ok = false;
            continue;
        }
        const auto task_state = native_task_state(cycle.actor.native_ped, cycle.task.record.task,
                                                  cycle.task_pool_flag, hooks);
        if (task_state == TaskState::unknown) {
            ok = false;
            continue;
        }
        if (task_state == TaskState::lost) {
            if (!retire(cycle)) { ok = false; continue; }
            cycles_.erase(it);
            continue;
        }
        if (task_state == TaskState::suspended) {
            const auto association = cycle.animation
                ? association_state(cycle.actor.native_ped, cycle.animation, cycle.hierarchy, hooks)
                : AssociationState::absent;
            if (association == AssociationState::unknown) { ok = false; continue; }
            if (association == AssociationState::present && !fade_owned_gesture(cycle)) {
                ok = false;
                continue;
            }
            cycle.animation = 0;
            cycle.hierarchy = 0;
            cycle.owns_animation = false;
            continue;
        }

        if (int32_t(clock->now - cycle.next_gesture_at) < 0) {
            continue;
        }
        const auto association = cycle.animation
            ? association_state(cycle.actor.native_ped, cycle.animation, cycle.hierarchy, hooks)
            : AssociationState::absent;
        if (association == AssociationState::unknown) {
            ok = false;
            continue;
        }
        if (association == AssociationState::present) {
            // Let SA finish the current non-looping gesture.
            continue;
        }

        cycle.animation = 0;
        cycle.hierarchy = 0;
        cycle.owns_animation = false;
        if (!play_hail_gesture(cycle)) {
            ok = false;
            cycle.next_gesture_at = clock->now + hail_interval_ms;
            continue;
        }
        cycle.next_gesture_at = clock->now + hail_interval_ms;
        const auto actor = cycle.actor;
        const auto task = cycle.task;
        const auto pool_flag = cycle.task_pool_flag;
        const auto sound = say_taxi_wait(actor);
        const auto still_registered = cycles_.find(handle);
        if (sound != SoundResult::ok || still_registered == cycles_.end() ||
            !(still_registered->second.actor == actor) ||
            still_registered->second.task.slot != task.slot ||
            still_registered->second.task.record.task != task.record.task ||
            still_registered->second.task_pool_flag != pool_flag ||
            !captured_task_is_live(actor, task, pool_flag, hooks))
            ok = false;
    }
    return ok;
}

bool SaTaxiHail::clear(int32_t handle) {
    const auto found = cycles_.find(handle);
    if (found == cycles_.end()) return true;
    if (!retire(found->second)) return false;
    cycles_.erase(found);
    return true;
}

bool SaTaxiHail::clear_all() {
    bool ok = true;
    for (auto it = cycles_.begin(); it != cycles_.end();) {
        if (retire(it->second)) it = cycles_.erase(it);
        else { ok = false; ++it; }
    }
    return ok;
}

} // namespace lcs
