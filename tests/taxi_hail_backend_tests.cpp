#define LCS_ISOLATED_ENTITY_BACKEND_TEST
#include "../src/sa_taxi_hail.cpp"

#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace {
using namespace lcs;
constexpr int32_t ped_handle = 7;
constexpr uintptr_t ped_address = 0x10000;
constexpr uintptr_t task_address = 0x20000;
constexpr uintptr_t event_address = 0x30000;
ObjectiveActor actor{ped_handle, ped_address};
bool actor_live = true;
bool animation_available = true;
bool pose_succeeds = true;
uint32_t frame_now = 100;
TaxiHailTestTaskState task_state = TaxiHailTestTaskState::active;
size_t play_calls = 0, fade_calls = 0, executor_calls = 0;
ScriptTaskSnapshot task_bank{};
std::unordered_map<uintptr_t, uint8_t> pool_flags;
std::unordered_map<uintptr_t, std::pair<uintptr_t, uint8_t>> event_tasks;
std::unordered_set<uintptr_t> live_associations;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void reset() {
    actor_live = true;
    animation_available = true;
    pose_succeeds = true;
    frame_now = 100;
    task_state = TaxiHailTestTaskState::active;
    play_calls = fade_calls = executor_calls = 0;
    task_bank = ScriptTaskSnapshot{};
    pool_flags.clear();
    event_tasks.clear();
    live_associations.clear();
}

SaTaxiHailTestHooks hooks() {
    SaTaxiHailTestHooks result;
    result.task_pool_flag = [](uintptr_t task) -> std::optional<uint8_t> {
        const auto found = pool_flags.find(task);
        if (found == pool_flags.end() || (found->second & 0x80)) return {};
        return found->second;
    };
    result.task_state = [](uintptr_t task, uint8_t expected) {
        const auto found = pool_flags.find(task);
        if (found == pool_flags.end() || (found->second & 0x80) || found->second != expected)
            return TaxiHailTestTaskState::lost;
        return task_state;
    };
    result.script_event_task = [](uintptr_t event, uintptr_t& task, uint8_t& flag) {
        const auto found = event_tasks.find(event);
        if (found == event_tasks.end()) return false;
        task = found->second.first;
        flag = found->second.second;
        return !task || (pool_flags.count(task) && pool_flags[task] == flag && !(flag & 0x80));
    };
    result.animation_ready = [](const ObjectiveActor&) { return animation_available; };
    result.association_state = [](uintptr_t, uintptr_t animation, uintptr_t) {
        return live_associations.count(animation)
            ? TaxiHailTestAssociationState::present
            : TaxiHailTestAssociationState::absent;
    };
    result.play_gesture = [](const ObjectiveActor&, uintptr_t, uint8_t,
                             uintptr_t& animation, uintptr_t& hierarchy) {
        ++play_calls;
        if (!pose_succeeds) return false;
        animation = 0xA000 + play_calls * 0x100;
        hierarchy = 0xB000;
        live_associations.insert(animation);
        return true;
    };
    result.fade_gesture = [](const ObjectiveActor&, uintptr_t animation, uintptr_t) {
        ++fade_calls;
        live_associations.erase(animation);
        return true;
    };
    return result;
}

TaxiWaitSfx no_sample(const ObjectiveActor&) {
    return {TaxiWaitSfx::Kind::no_sample, -1};
}

ScriptTaskRecord task_record(uintptr_t task, int32_t status = 1) {
    return {0x05BA, 0, uint32_t(task), uint32_t(ped_address), status};
}
ScriptTaskRecord event_record(uintptr_t event) {
    return {0x05BA, uint32_t(event), 0, uint32_t(ped_address), 0};
}

NativeResult run_start(SaTaxiHail& hail, const ScriptTaskRecord& accepted) {
    return hail.start(ped_handle, true, [&](const NativeCall& call) {
        ++executor_calls;
        check(call.opcode == 0x05BA && call.inputs.size() == 2 &&
              call.inputs[0] == ped_handle && call.inputs[1] == -1,
              "Opcode adapter did not issue native stand-still");
        task_bank[0] = accepted;
        NativeResult result;
        result.executed = true;
        result.native_entered = true;
        return result;
    });
}

void check_queued_event_promotes_only_its_task() {
    reset();
    pool_flags[task_address] = 5;
    event_tasks[event_address] = {task_address, 5};
    SaTaxiHail hail(hooks(), no_sample);
    const auto started = run_start(hail, event_record(event_address));
    check(started.executed && started.native_entered &&
          started.task_capture == NativeResult::TaskCapture::captured && play_calls == 0,
          "Accepted mission event must retain its deferred hail consumer");
    check(hail.tick() && play_calls == 0, "Pending event should wait for task association");
    task_bank[0] = task_record(task_address);
    check(hail.tick() && play_calls == 1, "The captured event's task should start one hail wave");
}

void check_suspend_cancel_and_failed_entry() {
    reset();
    pool_flags[task_address] = 9;
    task_state = TaxiHailTestTaskState::suspended;
    SaTaxiHail hail(hooks(), no_sample);
    const auto started = run_start(hail, task_record(task_address));
    check(started.executed && started.native_entered && play_calls == 0,
          "Accepted suspended task must defer its gesture without stopping the script");
    check(hail.tick() && play_calls == 0, "Suspended task must wait without waving");
    task_state = TaxiHailTestTaskState::active;
    check(hail.tick() && play_calls == 1, "Hail should begin when the captured task becomes active");
    check(hail.clear(ped_handle) && fade_calls == 1 && hail.tick() && play_calls == 1,
          "Cancellation must retire the owned wave and stop future repeats");

    reset();
    SaTaxiHail failed(hooks(), no_sample);
    const auto rejected = failed.start(ped_handle, true, [](const NativeCall&) {
        NativeResult result;
        result.diagnostic = "native refused";
        return result;
    });
    check(!rejected.executed && !rejected.native_entered && executor_calls == 0,
          "A refused native assignment must remain a pre-entry failure");
}

void check_animation_failure_actor_end_and_generation_reuse() {
    reset();
    pool_flags[task_address] = 11;
    pose_succeeds = false;
    SaTaxiHail failed_pose(hooks(), no_sample);
    const auto pose_result = run_start(failed_pose, task_record(task_address));
    check(!pose_result.executed && pose_result.native_entered && play_calls == 1,
          "Animation failure after assignment must not report success");

    reset();
    pool_flags[task_address] = 13;
    SaTaxiHail ended_actor(hooks(), no_sample);
    check(run_start(ended_actor, task_record(task_address)).executed,
          "Initial active task should complete the first hail wave");
    actor_live = false;
    check(ended_actor.tick() && play_calls == 1,
          "An ended actor must release its cycle without another wave");

    reset();
    pool_flags[task_address] = 17;
    SaTaxiHail reused_slot(hooks(), no_sample);
    check(run_start(reused_slot, task_record(task_address)).executed && play_calls == 1,
          "Initial task should produce exactly one wave");
    pool_flags[task_address] = 18; // Same address, new low-seven-bit pool generation.
    check(reused_slot.tick() && play_calls == 1 && fade_calls == 0,
          "A reused CTask pool slot must not continue the old hail cycle");
}
void check_gesture_cadence_and_task_replacement() {
    reset();
    pool_flags[task_address] = 21;
    SaTaxiHail hail(hooks(), no_sample);
    check(run_start(hail, task_record(task_address)).executed && play_calls == 1,
          "An active hail must issue the first gesture");
    live_associations.clear();
    frame_now = 2099;
    check(hail.tick() && play_calls == 1, "Gesture must wait for the two-second cadence");
    frame_now = 2100;
    check(hail.tick() && play_calls == 2, "Gesture must repeat when the cadence expires");
    task_bank[0] = {};
    check(hail.tick() && fade_calls == 1, "A replacing task must retire only our current gesture");
    frame_now += 10000;
    check(hail.tick() && play_calls == 2, "A replaced task must never restart its hail cycle");
}
} // namespace

namespace lcs {
uintptr_t native_entity_address(uintptr_t) { return 0; }
bool sa_executor_profile_matches() { return true; }
std::optional<ObjectiveActor> sa_live_objective_actor(int32_t handle) {
    return actor_live && handle == actor.handle ? std::optional<ObjectiveActor>(actor) : std::nullopt;
}
std::optional<ScriptTaskSnapshot> sa_script_task_snapshot() { return task_bank; }
std::optional<NativeFrameClock> sa_frame_clock() { return NativeFrameClock{frame_now, 0, 1}; }
}

int main() {
    try {
        check_queued_event_promotes_only_its_task();
        check_suspend_cancel_and_failed_entry();
        check_animation_failure_actor_end_and_generation_reuse();
        check_gesture_cadence_and_task_replacement();
        std::cout << "taxi hail backend fixtures passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
