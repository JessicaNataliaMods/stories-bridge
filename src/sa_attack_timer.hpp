#pragma once

#include "lcs/objective_receipts.hpp"
#include <cstdint>

namespace lcs {

// Game-thread API. Apply the original SET_CHAR_ATTACK_TIMER interval to this
// live SA actor's native GunControl task. Zero removes the override and
// restores native SA cadence. The value is kept as unsigned milliseconds.
bool sa_set_char_attack_timer(const ObjectiveActor& actor, uint32_t milliseconds);
// Apply SET_CHAR_SHOOT_TIMER to an actor's existing native GunControl task.
// SA counts this duration down after a real UseGun FIREBURST and ends that
// task through its native END_LEISURE path. Zero restores the prior SA
// duration. The raw uint32 value is represented as signed countdown segments;
// SA observes frame-quantized durations and the timer never becomes its -1
// disabled sentinel.
bool sa_set_char_shoot_timer(const ObjectiveActor& actor, uint32_t milliseconds);
void sa_clear_char_attack_timer(const ObjectiveActor& actor);

// Remove the ProcessPed vtable hook when the service shuts down. If another
// plugin has replaced our slot, it is left untouched and false is returned.
bool sa_release_char_attack_timer();

#if defined(LCS_ATTACK_TIMER_BACKEND_TESTS)
namespace attack_timer_test {
using NativeProcessPed = bool(*)(uintptr_t task, uintptr_t ped);
void bind(uintptr_t* vtable, uintptr_t gun_control_vtable, uintptr_t native_process_ped,
          NativeProcessPed original, uint32_t now);
void set_now(uint32_t now);
void set_use_gun(uintptr_t ped, uintptr_t task);
void set_task_generation(uintptr_t task, uint8_t generation);
void fail_next_deadline_write();
void fail_next_duration_write();
bool process(uintptr_t task, uintptr_t ped);
uintptr_t process_hook_address();
constexpr uint32_t next_attack_offset() { return 0x34; }
constexpr uint32_t process_slot_offset() { return 0x1C; }
}
#endif

}
