#pragma once
#include "vm.hpp"
#include <optional>

namespace lcs {
using ScriptId = uint64_t;
struct Script {
    ScriptId id = 0;
    Thread thread;
    bool mission = false, resident = false, blocked = false, processing = false, bootstrap = false;
    // Preserve original immediate-launch ordering across our bounded VM slices.
    ScriptId awaiting_initial_slice = 0;
    StepResult result = StepResult::continued;
};
struct Execution {
    ScriptId id;
    StepResult result;
    uint32_t ip;
    uint16_t opcode;
    std::string diagnostic;
    // A later script can terminate and reuse this slot during the same tick.
    std::string name;
};
enum class FailureScope { thread, mission, initialization };
struct ScriptFailure {
    ScriptId id;
    FailureScope scope;
    bool resumable,mission_code_leased;
    std::vector<ScriptId> dependents;
};
const char* failure_scope_name(FailureScope);
// Owns independent LCS threads/globals. The engine backend must use native SA
// entities; the scheduler itself never creates a second world or renderer.
class Runtime {
    ScmImage image_;
    Vm vm_;
    std::array<Script,128> scripts_{};
    std::vector<ScriptId> active_;
    ScriptId next_id_ = 1;
    std::optional<size_t> mission_index_;
    std::optional<uint32_t> on_mission_offset_;
    std::vector<Execution> executions_;
    uint32_t now_ = 0;
    size_t budget_ = 10000;
    bool ticking_ = false;
    Script& find(ScriptId id);
    Script& owner(Thread& thread);
    bool uses_mission(const Script& script) const;
    StepResult dispatch(Thread&,uint16_t);
    void process(Script& script);
    ScriptId start(uint32_t ip,const std::vector<int32_t>& args,bool mission,bool resident,bool pending_mission = false);
public:
    Vm::Backend engine;
    std::function<std::optional<float>()> frame_time_step;
    explicit Runtime(ScmImage image);
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&&) = delete;
    Runtime& operator=(Runtime&&) = delete;
    const Vm& vm() const { return vm_; }
    Vm& vm() { return vm_; }
    const Script& script(ScriptId id) const;
    size_t active_count() const { return active_.size(); }
    bool is_mission_script(const Thread& thread) const;
    // Generic fault boundaries, derived from code residency and actual launch
    // dependencies, never from a whitelist of script names. A blocked worker
    // cannot abort the scheduler or discard the current mission's state.
    std::optional<ScriptFailure> failure(ScriptId id) const;
    std::vector<ScriptFailure> failures() const;
    bool can_isolate_failure(ScriptId id) const;
    std::optional<size_t> mission_index() const { return mission_index_; }
    bool mission_buffer_in_use() const;
    bool on_mission() const {
        return on_mission_offset_ && *on_mission_offset_>=8 && !(*on_mission_offset_%4) &&
            uint64_t(*on_mission_offset_)+4<=vm_.variable_space &&
            uint64_t(*on_mission_offset_)+4<=vm_.memory.size() && read32(vm_.memory,*on_mission_offset_)==1;
    }
    bool notify_player_restart();
    void declare_mission_flag(uint32_t offset);
    ScriptId start_main();
    ScriptId start_script(uint32_t ip,const std::vector<int32_t>& args = {});
    ScriptId load_mission(size_t index);
    void terminate(ScriptId id);
    void resume(ScriptId id);
    // Newly started ordinary scripts run next tick. LOAD_AND_LAUNCH_MISSION_INTERNAL
    // processes its child immediately, matching Script6.cpp. elapsed_ms comes from
    // game time, not wall time; timers advance even while a script is waiting.
    std::vector<Execution> tick(uint32_t now,uint32_t elapsed_ms,size_t budget = 10000);
};
}
