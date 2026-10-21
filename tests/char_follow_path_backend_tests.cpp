#include "../src/sa_char_follow_path.hpp"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace lcs;

namespace {
int checks = 0;

void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

int32_t word(float value) {
    int32_t result = 0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

float real(int32_t value) {
    float result = 0.0f;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

struct Fixture {
    ObjectiveActor actor{17, 0x123400};
    CharFollowPathPedFacts facts{actor, true, true, 1};
    ScriptTaskSnapshot bank{};
    std::vector<NativeCall> calls;
    int resolutions = 0;
    int snapshots = 0;
    int active_checks = 0;
    int unavailable_snapshot_call = 0;
    bool fail_clear = false;
    bool omit_clear_native_entry = false;
    bool publish_task = true;
    float ground_z = 12.5f;

    CharFollowPathBackend backend() {
        CharFollowPathBackend result;
        result.resolve_actor = [this](int32_t handle) -> std::optional<CharFollowPathPedFacts> {
            ++resolutions;
            if (handle != actor.handle) return {};
            return facts;
        };
        result.task_snapshot = [this]() -> std::optional<ScriptTaskSnapshot> {
            ++snapshots;
            if (snapshots == unavailable_snapshot_call) return {};
            return bank;
        };
        result.task_active = [this](const ObjectiveActor& owner, const CapturedScriptTask& task) -> std::optional<bool> {
            ++active_checks;
            return owner == actor && task.slot == 9 && task.record.task == 0xABCDEF;
        };
        result.execute = [this](const NativeCall& call) {
            calls.push_back(call);
            NativeResult result;
            result.executed = true;
            result.native_entered = call.opcode != 0x0687 || !omit_clear_native_entry;
            if (call.opcode == 0x02CE) {
                result.values[0] = word(ground_z);
            } else if (call.opcode == 0x0687 && fail_clear) {
                result.executed = false;
                result.diagnostic = "injected clear failure";
            } else if (call.opcode == 0x0A2E && publish_task) {
                bank[9] = ScriptTaskRecord{0x0A2E, 0, 0xABCDEF, uint32_t(actor.native_ped), 1};
            }
            return result;
        };
        return result;
    }
};

void native_route_carries_source_inputs_and_long_timeout() {
    Fixture fixture;
    const std::array<float, 3> destination{10.25f, -4.5f, 2.0f};
    const auto result = execute_char_follow_path(17, destination, 0.0f, 1, true, fixture.backend());
    check(result.executed && result.native_entered, "Native clear and route should complete");
    check(fixture.calls.size() == 2 && fixture.calls[0].opcode == 0x0687 && fixture.calls[1].opcode == 0x0A2E,
        "Follow path clears prior tasks before assigning the native route");
    const auto& route = fixture.calls[1];
    check(route.inputs.size() == 7 && route.inputs[0] == 17, "SA radius route packet has seven expected operands");
    check(route.inputs[1] == word(destination[0]) && route.inputs[2] == word(destination[1]) &&
        route.inputs[3] == word(destination[2]), "XYZ source destination is preserved");
    check(route.inputs[4] == 6 && route.inputs[5] == 999999 && real(route.inputs[6]) == 20.0f,
        "Run mode, long timeout, and source default abort radius are translated");
    check(route.mission_context && result.task_capture == NativeResult::TaskCapture::captured &&
        result.accepted_script_task && fixture.active_checks == 1,
        "Mission context and observer-backed active-task receipt are preserved");
}

void low_z_is_projected_before_clear_and_route() {
    Fixture fixture;
    fixture.ground_z = 23.75f;
    const auto result = execute_char_follow_path(17, {1.0f, 2.0f, -100.0f}, 4.0f, 9, false, fixture.backend());
    check(result.executed && fixture.calls.size() == 3 && fixture.calls[0].opcode == 0x02CE &&
        fixture.calls[1].opcode == 0x0687 && fixture.calls[2].opcode == 0x0A2E,
        "Low-Z ground projection happens before clear and assignment");
    check(real(fixture.calls[0].inputs[2]) == 1000.0f && real(fixture.calls[2].inputs[3]) == fixture.ground_z,
        "SA ground query uses the XY-only source sentinel and projected height");
    check(fixture.calls[2].inputs[4] == 4 && real(fixture.calls[2].inputs[6]) == 4.0f &&
        fixture.resolutions == 4 && fixture.snapshots == 2,
        "Unknown source movement defaults to walk and actor/task state is refreshed around native work");
}

void invalid_ground_sentinel_stops_before_task_clear() {
    Fixture fixture;
    fixture.ground_z = -1000.0f;
    const auto result = execute_char_follow_path(17, {1.0f, 2.0f, -100.0f}, 4.0f, 1, false, fixture.backend());
    check(!result.executed && result.native_entered && fixture.calls.size() == 1 &&
        fixture.calls[0].opcode == 0x02CE,
        "Invalid ground sentinel is rejected before clearing or assigning tasks");
}

void source_attack_gate_is_a_noop_without_clearing() {
    Fixture fixture;
    fixture.facts.native_state = 16;
    const auto result = execute_char_follow_path(17, {1.0f, 2.0f, 3.0f}, 2.0f, 0, false, fixture.backend());
    check(result.executed && !result.native_entered && fixture.calls.empty(),
        "Retail attack state returns normally without clearing or assigning a route");
}

void a_failed_clear_never_assigns_a_route() {
    Fixture fixture;
    fixture.fail_clear = true;
    const auto result = execute_char_follow_path(17, {1.0f, 2.0f, 3.0f}, 2.0f, 0, false, fixture.backend());
    check(!result.executed && result.native_entered && fixture.calls.size() == 1 && fixture.calls[0].opcode == 0x0687,
        "Clear failure is surfaced after entry and does not issue the route task");
    check(fixture.snapshots == 0, "No route ownership snapshot is taken when clear fails");
}

void successful_clear_counts_as_native_entry_even_without_receipt_bit() {
    Fixture fixture;
    fixture.omit_clear_native_entry = true;
    const auto result = execute_char_follow_path(17, {1.0f, 2.0f, 3.0f}, 2.0f, 0, false, fixture.backend());
    check(result.executed && result.native_entered && fixture.calls.size() == 2,
        "Successful task clear proves native entry even when an executor omits its entry bit");
}

void task_receipt_must_still_be_the_active_native_task() {
    Fixture fixture;
    auto backend = fixture.backend();
    backend.task_active = [](const ObjectiveActor&, const CapturedScriptTask&) -> std::optional<bool> { return false; };
    const auto result = execute_char_follow_path(17, {1.0f, 2.0f, 3.0f}, 2.0f, 0, false, backend);
    check(result.executed && result.native_entered && !result.accepted_script_task &&
        result.task_capture == NativeResult::TaskCapture::no_unique_record,
        "Route effect is retained while stale task ownership is rejected");
}

void unavailable_task_capture_is_advisory_after_route_acceptance() {
    Fixture fixture;
    fixture.unavailable_snapshot_call = 1;
    const auto result = execute_char_follow_path(17, {1.0f, 2.0f, 3.0f}, 2.0f, 0, false, fixture.backend());
    check(result.executed && result.native_entered && fixture.calls.size() == 2 &&
        fixture.calls[1].opcode == 0x0A2E,
        "Unavailable pre-assignment task snapshot does not suppress an accepted route");
    check(!result.accepted_script_task && result.task_capture == NativeResult::TaskCapture::unavailable_before,
        "Missing observer receipt is recorded as advisory ownership uncertainty");
}
}

int main() {
    try {
        native_route_carries_source_inputs_and_long_timeout();
        low_z_is_projected_before_clear_and_route();
        invalid_ground_sentinel_stops_before_task_clear();
        source_attack_gate_is_a_noop_without_clearing();
        a_failed_clear_never_assigns_a_route();
        successful_clear_counts_as_native_entry_even_without_receipt_bit();
        task_receipt_must_still_be_the_active_native_task();
        unavailable_task_capture_is_advisory_after_route_acceptance();
        std::cout << "PASS: " << checks << " CHAR_FOLLOW_PATH backend assertions\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << "\n";
        return 1;
    }
}
