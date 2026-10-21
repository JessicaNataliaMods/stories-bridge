#pragma once
#include "scm.hpp"
#include <array>
#include <functional>
#include <string>

namespace lcs {
enum class StepResult { continued, yielded, terminated, unsupported, failed };
struct Thread {
    uint32_t ip = 0, wake_time = 0;
    std::array<uint32_t, 16> stack{};
    std::array<int32_t, 106> locals{};
    uint16_t stack_size = 0, locals_base = 0, and_or = 0;
    bool condition = false, inverted = false, active = true, waiting = false;
    bool message_wait = false;
    bool death_arrest_enabled = true, death_arrest_executed = false;
    std::string name, diagnostic;
    uint32_t last_ip = 0;
    uint16_t last_opcode = 0;
};
class Vm {
public:
    std::vector<uint8_t> memory;
    uint32_t main_size, variable_space;
    // Original CTimer units: one step is 20 ms. Runtime supplies the actual
    // native fractional timestep; offline fixtures use their explicit frame time.
    float time_step=1.0f;
    // A backend must implement the actual engine operation. Unknown commands stop;
    // they are never guessed from SA opcode numbers or silently treated as NOP.
    using Backend = std::function<StepResult(Vm&, Thread&, uint16_t)>;
    Backend backend;
    // Profile-scoped compatibility hooks run before decoding. Returning true
    // means the hook deliberately advanced IP; unknown opcodes still stop.
    std::function<bool(Vm&,Thread&)> before_step;
    std::function<bool()> message_skip;
    explicit Vm(const ScmImage& image);
    Vm(std::vector<uint8_t> code, uint32_t variables, uint32_t main);
    StepResult step(Thread& thread, uint32_t now);
    StepResult tick(Thread& thread, uint32_t now, size_t budget = 10000);
    int32_t parameter(Thread& thread);
    int32_t variable(Thread& thread);
    uint32_t global_reference(Thread& thread);
    // Address identity used by retail marker writers. Peeks without consuming
    // the operand; validates arrays/timers exactly as a normal variable read.
    // Valid only while this VM's storage and the owning Thread remain stable.
    uintptr_t peek_variable_identity(Thread& thread);
    void store(Thread& thread, int32_t value);
    void compare(Thread& thread, bool value);
    uint32_t resolve_label(int32_t label) const;
    std::vector<int32_t> arguments(Thread& thread, size_t maximum);
    std::string text_label(Thread& thread);
    void load_mission(const std::vector<uint8_t>& code);
    void restart_after_death_arrest(Thread&);
    uint32_t mission_size() const { return mission_size_; }
private:
    struct Ref { bool global; uint32_t index; };
    uint32_t mission_size_ = 0;
    void require_code(const Thread&, size_t bytes) const;
    uint8_t byte(Thread&);
    uint16_t word(Thread&);
    uint32_t dword(Thread&);
    Ref reference(Thread&);
    int32_t get(Thread&, Ref) const;
    void set(Thread&, Ref, int32_t);
    void return_from_call(Thread&);
};
}
