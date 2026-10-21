#include <windows.h>
#include "lcs/sa_executor.hpp"
#include "sa_attack_timer.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace lcs {
namespace {
constexpr uintptr_t gun_control_vtable_address = 0x86D76C;
constexpr uintptr_t native_process_ped_address = 0x625270;
constexpr uintptr_t process_ped_slot_offset = 0x1C; // CTask vtable slot 7
constexpr uintptr_t task_type_slot_offset = 0x10;
constexpr uintptr_t abort_slot_offset = 0x18;
constexpr uintptr_t native_get_task_type_address = 0x61F4C0;
constexpr uintptr_t native_make_abortable_address = 0x61F530;
constexpr uintptr_t use_gun_vtable_address = 0x86D724;
constexpr uintptr_t use_gun_abort_address = 0x624E30;
constexpr uint32_t next_attack_offset = 0x34;
constexpr uint32_t leisure_duration_offset = 0x28;
constexpr uint32_t firing_command_offset = 0x2E;
constexpr uint32_t use_gun_next_command_offset = 0x0F;
constexpr uint32_t use_gun_last_command_offset = 0x10;
constexpr uint32_t native_waiting_sentinel = UINT32_MAX;
constexpr uintptr_t ped_intelligence_offset = 0x47C;
constexpr uintptr_t task_manager_attack_slot_offset = 0x14;
constexpr uintptr_t task_manager_owner_offset = 0x2C;
constexpr uintptr_t task_pool_pointer_address = 0xB744A8;
constexpr uint32_t task_pool_storage_offset = 0x00;
constexpr uint32_t task_pool_state_map_offset = 0x04;
constexpr uint32_t task_pool_capacity_offset = 0x08;
constexpr uint32_t task_pool_stride = 128;
constexpr uint8_t task_pool_empty_bit = 0x80;
constexpr int32_t max_native_shoot_segment_ms = INT32_MAX;
constexpr int32_t shoot_segment_renewal_margin_ms = max_native_shoot_segment_ms / 2;
constexpr int8_t gun_command_fireburst = 3;
constexpr int8_t gun_command_aim = 1;
constexpr int8_t gun_command_end_leisure = 6;
constexpr int8_t gun_command_end_now = 7;

constexpr uint8_t process_ped_prologue[] = {
    0x6A,0xFF,0x64,0xA1,0x00,0x00,0x00,0x00,0x68,0x2B,0xFD,0x83,0x00,0x50,
    0x64,0x89,0x25,0x00,0x00,0x00,0x00,0x83,0xEC,0x28,0x53,0x55,0x56,0x8B,0xF1
};
constexpr uint8_t process_ped_due_profile[] = {
    0x8B,0x46,0x34,0x39,0x05,0x84,0xCB,0xB7,0x00,0x0F,0x82,0x8F,0x00,0x00,0x00,
    0x89,0x54,0x24,0x10,0x8B,0x54,0x24,0x14,0xC7,0x46,0x34,0xFF,0xFF,0xFF,0xFF
};
constexpr uint8_t process_ped_sentinel_profile[] = {
    0x83,0xF8,0xFF,0x75,0x11,0x85,0xDB,0x74,0x0D,0x80,0x7B,0x10,0x03,0x74,0x07,
    0xC7,0x46,0x34,0x00,0x00,0x00,0x00
};
constexpr uint8_t process_ped_zero_profile[] = {
    0x8B,0x46,0x34,0x85,0xC0,0x75,0x77,0x8B,0x0D,0x78,0x2E,0x8D,0x00
};
// Verified from the local SA retail PE: +0x28 is the signed leisure timer,
// decremented by the native frame delta and written back to the same field.
constexpr uint8_t process_ped_shoot_countdown_profile[] = {
    0x8B,0x7E,0x28,0x83,0xFF,0xFF,0x7E,0x29,0xD9,0x05,0x5C,0xCB,0xB7,0x00,
    0xD8,0x0D,0x38,0x8B,0x85,0x00,0xD8,0x0D,0x4C,0x8C,0x85,0x00,0xE8,0xD7,
    0xC4,0x1F,0x00,0x8B,0xCF,0x2B,0xC8,0xB8,0x00,0x00,0x00,0x00,0x0F,0x98,
    0xC0,0x48,0x23,0xC1,0x89,0x46,0x28,0x8A
};
// A zero countdown issues command 6 (END_LEISURE) and aborts native UseGun.
constexpr uint8_t process_ped_shoot_end_profile[] = {
    0x8B,0x46,0x28,0x85,0xC0,0x75,0x16,0x85,0xDB,0x74,0x12,0x8B,0x46,0x0C,
    0x6A,0x06,0x50,0x55,0x8B,0xCB,0xC6,0x46,0x2E,0x06,0xE8,0x27,0x89,0xFF,0xFF
};

struct GunControlTaskLayout {
    uintptr_t vtable;
    uintptr_t parent;
    uint8_t finished;
    uint8_t pad_09[3];
    uintptr_t target_entity;
    float target_position[3];
    float move_target[3];
    int32_t leisure_duration_ms;
    int16_t burst_ammo;
    int8_t firing_command;
    uint8_t pad_2f;
    float attack_interval_multiplier;
    uint32_t next_attack_time_ms;
};
static_assert(sizeof(uintptr_t) == 4, "SA native backend requires the 32-bit game ABI");
static_assert(offsetof(GunControlTaskLayout, next_attack_time_ms) == next_attack_offset,
              "CTaskSimpleGunControl::m_nextAtkTimeMs layout changed");
static_assert(offsetof(GunControlTaskLayout, leisure_duration_ms) == leisure_duration_offset,
              "CTaskSimpleGunControl::m_leisureDurationMs layout changed");
static_assert(process_ped_slot_offset == 7 * sizeof(uintptr_t),
              "CTaskSimpleGunControl::ProcessPed vtable slot changed");

using ProcessPed = bool(__thiscall*)(uintptr_t task, uintptr_t ped);
struct Policy {
    ObjectiveActor actor;
    uint32_t interval_ms = 0;
    uint32_t shoot_duration_ms = 0;
    uint32_t shoot_revision = 0;
    bool has_shoot_duration = false;
};
struct TaskIdentity {
    uintptr_t address = 0;
    uintptr_t pool = 0;
    uint32_t slot = 0;
    uint8_t generation_state = 0;

    bool operator==(const TaskIdentity& other) const {
        return address == other.address && pool == other.pool && slot == other.slot &&
               generation_state == other.generation_state;
    }
};
struct WaitState {
    ObjectiveActor actor;
    TaskIdentity task;
    TaskIdentity use_gun_task;
    uint32_t interval_ms = 0;
    uint32_t native_deadline = 0;
    uint32_t owned_deadline = 0;
    bool owns_deadline = false;
    bool burst_seen = false;
};

struct ShootDurationState {
    ObjectiveActor actor;
    TaskIdentity task;
    uint32_t revision = 0;
    uint32_t tail_ms = 0;
    int32_t native_remaining_ms = -1;
    int32_t owned_remaining_ms = 0;
    bool owns_duration = false;
};

struct UseGunState {
    bool exists = false;
    uintptr_t task = 0;
    TaskIdentity identity{};
    int8_t next_command = 0;
    int8_t last_command = 0;
};

std::vector<Policy> policies;
std::vector<WaitState> waits;
std::vector<ShootDurationState> shoot_durations;
uint32_t next_shoot_revision = 1;
uintptr_t previous_process_ped = 0;
bool hook_installed = false;

bool __fastcall gun_control_process_ped(uintptr_t task, void*, uintptr_t ped);
uintptr_t active_vtable_address();

#if defined(LCS_ATTACK_TIMER_BACKEND_TESTS)
uintptr_t* test_vtable = nullptr;
uintptr_t test_vtable_address = 0;
uintptr_t test_native_process_ped = 0;
uint32_t test_now = 0;
attack_timer_test::NativeProcessPed test_original = nullptr;
uintptr_t test_use_gun_ped = 0;
uintptr_t test_use_gun_task = 0;
bool test_fail_next_deadline_write = false;
bool test_fail_next_duration_write = false;
std::vector<std::pair<uintptr_t, uint8_t>> test_task_generations;
#endif

bool actor_is_live(const ObjectiveActor& actor);

bool safe_read_task(uintptr_t task, uintptr_t& vtable, uint32_t& next_attack) {
    __try {
        vtable = *reinterpret_cast<const uintptr_t*>(task);
        next_attack = *reinterpret_cast<const uint32_t*>(task + next_attack_offset);
        return vtable != 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool safe_write_next_attack(uintptr_t task, uint32_t value) {
#if defined(LCS_ATTACK_TIMER_BACKEND_TESTS)
    if (test_fail_next_deadline_write) {
        test_fail_next_deadline_write = false;
        return false;
    }
#endif
    __try {
        *reinterpret_cast<uint32_t*>(task + next_attack_offset) = value;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool safe_write_leisure_duration(uintptr_t task, int32_t duration) {
#if defined(LCS_ATTACK_TIMER_BACKEND_TESTS)
    if (test_fail_next_duration_write) {
        test_fail_next_duration_write = false;
        return false;
    }
#endif
    __try {
        *reinterpret_cast<int32_t*>(task + leisure_duration_offset) = duration;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool safe_read_leisure_duration(uintptr_t task, int32_t& duration) {
    __try {
        duration = *reinterpret_cast<const int32_t*>(task + leisure_duration_offset);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool safe_read_firing_command(uintptr_t task, int8_t& command) {
    __try {
        command = *reinterpret_cast<const int8_t*>(task + firing_command_offset);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool task_identity_for(uintptr_t task, TaskIdentity& identity) {
#if defined(LCS_ATTACK_TIMER_BACKEND_TESTS)
    if (!task || !test_vtable_address) return false;
    const auto generation = std::find_if(test_task_generations.begin(), test_task_generations.end(),
        [&](const auto& entry) { return entry.first == task; });
    const uint32_t slot = static_cast<uint32_t>((task >> 7) & 0xFFFFu);
    identity = TaskIdentity{task, test_vtable_address, slot,
                            generation == test_task_generations.end() ? uint8_t(0) : generation->second};
    return true;
#else
    __try {
        const uintptr_t pool = *reinterpret_cast<const uintptr_t*>(task_pool_pointer_address);
        if (!pool) return false;
        const uintptr_t storage = *reinterpret_cast<const uintptr_t*>(pool + task_pool_storage_offset);
        const uintptr_t state_map = *reinterpret_cast<const uintptr_t*>(pool + task_pool_state_map_offset);
        const int32_t capacity = *reinterpret_cast<const int32_t*>(pool + task_pool_capacity_offset);
        if (!storage || !state_map || capacity <= 0 ||
            task < storage) return false;
        const uintptr_t byte_offset = task - storage;
        if (byte_offset % task_pool_stride) return false;
        const uintptr_t slot = byte_offset / task_pool_stride;
        if (slot >= static_cast<uintptr_t>(capacity)) return false;
        const uint8_t state = *reinterpret_cast<const uint8_t*>(state_map + slot);
        if (state & task_pool_empty_bit) return false;
        identity = TaskIdentity{task, pool, static_cast<uint32_t>(slot), state};
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#endif
}

bool safe_read_use_gun_state(uintptr_t ped, UseGunState& state) {
    state = {};
#if defined(LCS_ATTACK_TIMER_BACKEND_TESTS)
    if (ped != test_use_gun_ped) return false;
    if (!test_use_gun_task) return true;
    state.exists = true;
    state.task = test_use_gun_task;
    if (!task_identity_for(state.task, state.identity)) return false;
    state.next_command = *reinterpret_cast<const int8_t*>(state.task + use_gun_next_command_offset);
    state.last_command = *reinterpret_cast<const int8_t*>(state.task + use_gun_last_command_offset);
    return true;
#else
    __try {
        const uintptr_t intelligence =
            *reinterpret_cast<const uintptr_t*>(ped + ped_intelligence_offset);
        if (!intelligence || *reinterpret_cast<const uintptr_t*>(intelligence) != ped) return false;
        const uintptr_t manager = intelligence + sizeof(uintptr_t);
        if (*reinterpret_cast<const uintptr_t*>(manager + task_manager_owner_offset) != ped) return false;
        const uintptr_t use_gun =
            *reinterpret_cast<const uintptr_t*>(manager + task_manager_attack_slot_offset);
        if (!use_gun) return true;
        const uintptr_t vtable = *reinterpret_cast<const uintptr_t*>(use_gun);
        if (vtable != use_gun_vtable_address ||
            *reinterpret_cast<const uintptr_t*>(vtable + 6 * sizeof(uintptr_t)) != use_gun_abort_address ||
            !task_identity_for(use_gun, state.identity)) return false;
        state.exists = true;
        state.task = use_gun;
        state.next_command = *reinterpret_cast<const int8_t*>(use_gun + use_gun_next_command_offset);
        state.last_command = *reinterpret_cast<const int8_t*>(use_gun + use_gun_last_command_offset);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        state = {};
        return false;
    }
#endif
}

bool safe_read_native_clock(uint32_t& now) {
#if defined(LCS_ATTACK_TIMER_BACKEND_TESTS)
    now = test_now;
    return true;
#else
    __try {
        now = *reinterpret_cast<const volatile uint32_t*>(0xB7CB84);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#endif
}

bool safe_code_matches(uintptr_t address, const uint8_t* expected, size_t size) {
#if defined(LCS_ATTACK_TIMER_BACKEND_TESTS)
    (void)address;
    (void)expected;
    (void)size;
    return true;
#else
    __try {
        const auto* actual = reinterpret_cast<const volatile uint8_t*>(address);
        for (size_t i = 0; i < size; ++i)
            if (actual[i] != expected[i]) return false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#endif
}

bool restore_native_deadline(WaitState& wait) {
    if (!wait.owns_deadline) return true;
    if (!actor_is_live(wait.actor)) {
        wait.owns_deadline = false;
        return true;
    }
    TaskIdentity current_identity{};
    uintptr_t vtable = 0;
    uint32_t current_deadline = 0;
    if (!task_identity_for(wait.task.address, current_identity)) return false;
    if (!(current_identity == wait.task)) {
        wait.owns_deadline = false;
        return true;
    }
    if (!safe_read_task(wait.task.address, vtable, current_deadline)) return false;
    if (vtable != active_vtable_address() || current_deadline != wait.owned_deadline) {
        wait.owns_deadline = false;
        return true;
    }
    if (!safe_write_next_attack(wait.task.address, wait.native_deadline)) return false;
    wait.owns_deadline = false;
    return true;
}

bool write_owned_deadline(WaitState& wait, uint32_t value) {
    if (!safe_write_next_attack(wait.task.address, value)) return false;
    wait.owned_deadline = value;
    wait.owns_deadline = true;
    return true;
}

bool restore_all_waits() {
    bool restored = true;
    for (auto& wait : waits) {
        if (!restore_native_deadline(wait)) restored = false;
    }
    return restored;
}

bool restore_and_prune_waits_for_actor(const ObjectiveActor& actor) {
    bool restored = true;
    waits.erase(std::remove_if(waits.begin(), waits.end(), [&](WaitState& wait) {
        if (!(wait.actor == actor)) return false;
        if (!restore_native_deadline(wait)) {
            restored = false;
            return false;
        }
        return true;
    }), waits.end());
    return restored;
}

void clear_wait_for_task_address(uintptr_t task) {
    waits.erase(std::remove_if(waits.begin(), waits.end(), [&](WaitState& wait) {
        return wait.task.address == task && restore_native_deadline(wait);
    }), waits.end());
}

void clear_stale_wait_for_task(const TaskIdentity& task) {
    waits.erase(std::remove_if(waits.begin(), waits.end(), [&](const WaitState& wait) {
        return wait.task.address == task.address && !(wait.task == task);
    }), waits.end());
}

void clear_other_waits_for_actor(const ObjectiveActor& actor, const TaskIdentity& task) {
    waits.erase(std::remove_if(waits.begin(), waits.end(), [&](WaitState& wait) {
        if (!(wait.actor == actor) || (wait.task == task)) return false;
        return restore_native_deadline(wait);
    }), waits.end());
}

bool clear_wait_for_task(const ObjectiveActor& actor, const TaskIdentity& task) {
    bool restored = true;
    waits.erase(std::remove_if(waits.begin(), waits.end(), [&](WaitState& wait) {
        if (!(wait.actor == actor) || !(wait.task == task)) return false;
        if (!restore_native_deadline(wait)) {
            restored = false;
            return false;
        }
        return true;
    }), waits.end());
    return restored;
}

void clear_waits_for_handle(int32_t handle) {
    waits.erase(std::remove_if(waits.begin(), waits.end(), [&](const WaitState& wait) {
        return wait.actor.handle == handle;
    }), waits.end());
}

void clear_shoot_durations_for_handle(int32_t handle) {
    shoot_durations.erase(std::remove_if(shoot_durations.begin(), shoot_durations.end(),
        [&](const ShootDurationState& state) { return state.actor.handle == handle; }),
        shoot_durations.end());
}

bool restore_native_shoot_duration(ShootDurationState& state) {
    if (!state.owns_duration) return true;
    if (!actor_is_live(state.actor)) {
        state.owns_duration = false;
        return true;
    }
    TaskIdentity current_identity{};
    uintptr_t vtable = 0;
    uint32_t ignored_deadline = 0;
    int32_t current_duration = 0;
    if (!task_identity_for(state.task.address, current_identity)) return false;
    if (!(current_identity == state.task)) {
        state.owns_duration = false;
        return true;
    }
    if (!safe_read_task(state.task.address, vtable, ignored_deadline) ||
        !safe_read_leisure_duration(state.task.address, current_duration)) return false;
    if (vtable != active_vtable_address() || current_duration != state.owned_remaining_ms) {
        state.owns_duration = false;
        return true;
    }
    if (!safe_write_leisure_duration(state.task.address, state.native_remaining_ms)) return false;
    state.owns_duration = false;
    return true;
}

bool restore_all_shoot_durations() {
    bool restored = true;
    for (auto& state : shoot_durations)
        if (!restore_native_shoot_duration(state)) restored = false;
    return restored;
}

bool clear_shoot_durations_for_actor(const ObjectiveActor& actor) {
    bool restored = true;
    shoot_durations.erase(std::remove_if(shoot_durations.begin(), shoot_durations.end(),
        [&](ShootDurationState& state) {
            if (!(state.actor == actor)) return false;
            if (!restore_native_shoot_duration(state)) {
                restored = false;
                return false;
            }
            return true;
        }), shoot_durations.end());
    return restored;
}

void clear_stale_shoot_duration_task(const TaskIdentity& task) {
    shoot_durations.erase(std::remove_if(shoot_durations.begin(), shoot_durations.end(),
        [&](const ShootDurationState& state) {
            return state.task.address == task.address && !(state.task == task);
        }), shoot_durations.end());
}

void clear_other_shoot_durations_for_actor(const ObjectiveActor& actor,
                                            const TaskIdentity& task) {
    shoot_durations.erase(std::remove_if(shoot_durations.begin(), shoot_durations.end(),
        [&](ShootDurationState& state) {
            if (!(state.actor == actor) || (state.task == task)) return false;
            return restore_native_shoot_duration(state);
        }), shoot_durations.end());
}

uint32_t next_shoot_policy_revision() {
    const uint32_t revision = next_shoot_revision++;
    if (next_shoot_revision == 0) next_shoot_revision = 1;
    return revision ? revision : next_shoot_policy_revision();
}

bool apply_shoot_duration(const ObjectiveActor& actor, const TaskIdentity& task,
                          const UseGunState& use_gun, uint32_t duration_ms,
                          uint32_t revision) {
    // Only modify the task after SA has actually entered FIREBURST. The native
    // ProcessPed countdown at +0x28 will then end UseGun with END_LEISURE.
    if (!duration_ms || !use_gun.exists ||
        use_gun.last_command != gun_command_fireburst) return true;

    int8_t firing_command = 0;
    if (!safe_read_firing_command(task.address, firing_command) ||
        firing_command == gun_command_end_leisure || firing_command == gun_command_end_now)
        return true;

    clear_stale_shoot_duration_task(task);
    clear_other_shoot_durations_for_actor(actor, task);
    auto found = std::find_if(shoot_durations.begin(), shoot_durations.end(),
        [&](const ShootDurationState& state) {
            return state.actor == actor && state.task == task;
        });
    if (found == shoot_durations.end()) {
        try {
            shoot_durations.push_back(ShootDurationState{});
            found = shoot_durations.end() - 1;
        } catch (...) {
            return false;
        }
    } else if (found->revision == revision && found->owns_duration) {
        return true;
    } else {
        if (!restore_native_shoot_duration(*found)) return false;
        shoot_durations.erase(found);
        try {
            shoot_durations.push_back(ShootDurationState{});
            found = shoot_durations.end() - 1;
        } catch (...) {
            return false;
        }
    }

    uintptr_t vtable = 0;
    uint32_t ignored_deadline = 0;
    int32_t native_duration = -1;
    TaskIdentity current_identity{};
    if (!actor_is_live(actor) || !task_identity_for(task.address, current_identity) ||
        !(current_identity == task) ||
        !safe_read_task(task.address, vtable, ignored_deadline) ||
        vtable != active_vtable_address() ||
        !safe_read_leisure_duration(task.address, native_duration)) {
        shoot_durations.erase(found);
        return false;
    }
    const uint32_t first_segment = std::min(duration_ms,
        static_cast<uint32_t>(max_native_shoot_segment_ms));
    if (!first_segment || !safe_write_leisure_duration(task.address,
            static_cast<int32_t>(first_segment))) {
        shoot_durations.erase(found);
        return false;
    }
    found->actor = actor;
    found->task = task;
    found->revision = revision;
    found->tail_ms = duration_ms - first_segment;
    found->native_remaining_ms = native_duration;
    found->owned_remaining_ms = static_cast<int32_t>(first_segment);
    found->owns_duration = true;
    return true;
}

Policy* live_policy_for_ped(uintptr_t ped) {
    for (size_t i = 0; i < policies.size();) {
        auto& policy = policies[i];
        if (policy.actor.native_ped != ped) {
            ++i;
            continue;
        }
        const auto live = sa_live_objective_actor(policy.actor.handle);
        if (live && *live == policy.actor) return &policy;
        clear_waits_for_handle(policy.actor.handle);
        clear_shoot_durations_for_handle(policy.actor.handle);
        policies.erase(policies.begin() + static_cast<std::ptrdiff_t>(i));
    }
    return nullptr;
}

bool actor_is_live(const ObjectiveActor& actor) {
    if (actor.handle < 0 || !actor.native_ped) return false;
    const auto live = sa_live_objective_actor(actor.handle);
    return live && *live == actor;
}

uint32_t native_deadline_for_interval(uint32_t now, uint32_t interval_ms) {
    uint32_t deadline = now + interval_ms;
    // Zero means "attack immediately" and UINT32_MAX is ProcessPed's native
    // FIREBURST-wait sentinel. Preserve the modulo-32-bit cadence while moving
    // only those reserved outcomes by one millisecond.
    if (deadline == 0) deadline = 1;
    else if (deadline == native_waiting_sentinel) deadline = native_waiting_sentinel - 1;
    return deadline;
}

bool safe_executable_pointer(uintptr_t address) {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) ||
        info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ |
                             PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (info.Protect & executable) != 0;
}

#if defined(LCS_ATTACK_TIMER_BACKEND_TESTS)
bool patch_process_slot(uintptr_t expected, uintptr_t replacement) {
    if (!test_vtable) return false;
    auto& slot = test_vtable[process_ped_slot_offset / sizeof(uintptr_t)];
    if (slot != expected) return false;
    slot = replacement;
    return true;
}
uintptr_t current_process_slot() {
    if (!test_vtable) return 0;
    return test_vtable[process_ped_slot_offset / sizeof(uintptr_t)];
}
uintptr_t active_vtable_address() { return test_vtable_address; }
uintptr_t expected_native_process_ped() { return test_native_process_ped; }
bool native_profile_valid() {
    return sa_executor_profile_matches() && test_vtable && test_vtable_address &&
           test_native_process_ped && test_original;
}
bool invoke_original(uintptr_t task, uintptr_t ped, bool& result) {
    if (!test_original) return false;
    result = test_original(task, ped);
    return true;
}
#else
bool patch_process_slot(uintptr_t address, uintptr_t expected, uintptr_t replacement) {
    volatile bool changed = false;
    __try {
        auto* slot = reinterpret_cast<volatile LONG*>(address);
        if (*slot != static_cast<LONG>(expected)) return false;
        DWORD previous_protection = 0;
        if (!VirtualProtect(const_cast<LONG*>(slot), sizeof(LONG), PAGE_READWRITE,
                            &previous_protection)) return false;
        const LONG observed = InterlockedCompareExchange(
            slot, static_cast<LONG>(replacement), static_cast<LONG>(expected));
        changed = observed == static_cast<LONG>(expected);
        DWORD ignored = 0;
        if (!VirtualProtect(const_cast<LONG*>(slot), sizeof(LONG), previous_protection, &ignored))
            VirtualProtect(const_cast<LONG*>(slot), sizeof(LONG), previous_protection, &ignored);
        // Once CAS changed the slot, retain the forwarding pointer even if
        // restoring page protection fails or raises a structured exception.
        return changed;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return changed;
    }
}
uintptr_t current_process_slot() {
    __try {
        return *reinterpret_cast<const uintptr_t*>(gun_control_vtable_address + process_ped_slot_offset);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}
uintptr_t active_vtable_address() { return gun_control_vtable_address; }
uintptr_t expected_native_process_ped() { return native_process_ped_address; }
bool native_profile_valid() {
    if (!sa_executor_profile_matches()) return false;
    __try {
        const auto* table = reinterpret_cast<const uintptr_t*>(gun_control_vtable_address);
        const auto* use_gun_table = reinterpret_cast<const uintptr_t*>(use_gun_vtable_address);
        return table[task_type_slot_offset / sizeof(uintptr_t)] == native_get_task_type_address &&
               table[abort_slot_offset / sizeof(uintptr_t)] == native_make_abortable_address &&
               use_gun_table[6] == use_gun_abort_address &&
               (table[process_ped_slot_offset / sizeof(uintptr_t)] == native_process_ped_address ||
                (hook_installed && table[process_ped_slot_offset / sizeof(uintptr_t)] ==
                    reinterpret_cast<uintptr_t>(&gun_control_process_ped))) &&
               safe_executable_pointer(native_process_ped_address) &&
               safe_code_matches(native_process_ped_address, process_ped_prologue,
                                 sizeof(process_ped_prologue)) &&
               safe_code_matches(0x62555F, process_ped_due_profile,
                                 sizeof(process_ped_due_profile)) &&
               safe_code_matches(0x6255FD, process_ped_sentinel_profile,
                                 sizeof(process_ped_sentinel_profile)) &&
               safe_code_matches(0x625719, process_ped_zero_profile,
                                 sizeof(process_ped_zero_profile)) &&
               safe_code_matches(0x62564A, process_ped_shoot_countdown_profile,
                                 sizeof(process_ped_shoot_countdown_profile)) &&
               safe_code_matches(0x6256FC, process_ped_shoot_end_profile,
                                 sizeof(process_ped_shoot_end_profile));
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool invoke_original(uintptr_t task, uintptr_t ped, bool& result) {
    const auto function = reinterpret_cast<ProcessPed>(previous_process_ped);
    if (!function) return false;
    __try {
        result = function(task, ped);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
#endif

ShootDurationState* shoot_duration_for_task(const ObjectiveActor& actor,
                                           const TaskIdentity& task) {
    const auto found = std::find_if(shoot_durations.begin(), shoot_durations.end(),
        [&](const ShootDurationState& state) {
            return state.actor == actor && state.task == task;
        });
    return found == shoot_durations.end() ? nullptr : &*found;
}

bool prepare_shoot_duration_tick(const ObjectiveActor& actor, const TaskIdentity& task,
                                 bool has_policy, uint32_t revision,
                                 int32_t& before_duration) {
    auto* state = shoot_duration_for_task(actor, task);
    if (!state) return false;
    if (!actor_is_live(actor)) {
        clear_shoot_durations_for_actor(actor);
        return false;
    }
    if (!has_policy || state->revision != revision) {
        if (!restore_native_shoot_duration(*state)) {
            if (state->owns_duration && safe_read_leisure_duration(task.address, before_duration) &&
                before_duration == state->owned_remaining_ms) return true;
            return false;
        }
        shoot_durations.erase(std::remove_if(shoot_durations.begin(), shoot_durations.end(),
            [&](const ShootDurationState& candidate) {
                return candidate.actor == actor && candidate.task == task;
            }), shoot_durations.end());
        return false;
    }

    uintptr_t vtable = 0;
    uint32_t ignored_deadline = 0;
    TaskIdentity current_identity{};
    int32_t current_duration = 0;
    if (!task_identity_for(task.address, current_identity) || !(current_identity == task) ||
        !safe_read_task(task.address, vtable, ignored_deadline) ||
        vtable != active_vtable_address() ||
        !safe_read_leisure_duration(task.address, current_duration)) return false;
    if (!state->owns_duration || current_duration != state->owned_remaining_ms) {
        state->owns_duration = false;
        shoot_durations.erase(std::remove_if(shoot_durations.begin(), shoot_durations.end(),
            [&](const ShootDurationState& candidate) {
                return candidate.actor == actor && candidate.task == task;
            }), shoot_durations.end());
        return false;
    }

    if (state->tail_ms && current_duration > 0 &&
        current_duration <= shoot_segment_renewal_margin_ms) {
        const uint32_t room = static_cast<uint32_t>(max_native_shoot_segment_ms - current_duration);
        const uint32_t extension = std::min(state->tail_ms, room);
        if (extension && safe_write_leisure_duration(task.address,
                current_duration + static_cast<int32_t>(extension))) {
            current_duration += static_cast<int32_t>(extension);
            state->tail_ms -= extension;
            state->owned_remaining_ms = current_duration;
        }
    }
    before_duration = current_duration;
    return true;
}

void finish_shoot_duration_tick(const ObjectiveActor& actor, const TaskIdentity& task,
                                int32_t before_duration, bool state_was_active,
                                bool native_result, bool has_policy,
                                uint32_t duration_ms, uint32_t revision,
                                uintptr_t ped) {
    auto* state = shoot_duration_for_task(actor, task);
    if (state && (native_result || !actor_is_live(actor))) {
        shoot_durations.erase(std::remove_if(shoot_durations.begin(), shoot_durations.end(),
            [&](const ShootDurationState& candidate) {
                return candidate.actor == actor && candidate.task == task;
            }), shoot_durations.end());
        state = nullptr;
    }
    if (state) {
        uintptr_t vtable = 0;
        uint32_t ignored_deadline = 0;
        TaskIdentity current_identity{};
        int32_t current_duration = 0;
        int8_t firing_command = 0;
        if (!task_identity_for(task.address, current_identity) || !(current_identity == task) ||
            !safe_read_task(task.address, vtable, ignored_deadline) ||
            vtable != active_vtable_address() ||
            !safe_read_leisure_duration(task.address, current_duration) ||
            !safe_read_firing_command(task.address, firing_command) ||
            firing_command == gun_command_end_leisure || firing_command == gun_command_end_now) {
            shoot_durations.erase(std::remove_if(shoot_durations.begin(), shoot_durations.end(),
                [&](const ShootDurationState& candidate) {
                    return candidate.actor == actor && candidate.task == task;
                }), shoot_durations.end());
            state = nullptr;
        } else if (state->owns_duration && state_was_active) {
            if (current_duration == before_duration) {
                state->owned_remaining_ms = current_duration;
            } else if (before_duration > 0 && current_duration >= 0 &&
                       current_duration < before_duration) {
                const int32_t elapsed = before_duration - current_duration;
                if (state->native_remaining_ms > 0)
                    state->native_remaining_ms = std::max<int32_t>(0,
                        state->native_remaining_ms - elapsed);
                state->owned_remaining_ms = current_duration;
            } else {
                state->owns_duration = false;
                shoot_durations.erase(std::remove_if(shoot_durations.begin(), shoot_durations.end(),
                    [&](const ShootDurationState& candidate) {
                        return candidate.actor == actor && candidate.task == task;
                    }), shoot_durations.end());
                state = nullptr;
            }
        }
    }

    if (!state && has_policy && !native_result && actor_is_live(actor)) {
        UseGunState use_gun{};
        if (safe_read_use_gun_state(ped, use_gun) && use_gun.exists &&
            use_gun.last_command == gun_command_fireburst)
            apply_shoot_duration(actor, task, use_gun, duration_ms, revision);
    }
}

bool invoke_with_shoot_duration(uintptr_t task, uintptr_t ped,
                                const ObjectiveActor& actor,
                                const TaskIdentity& task_identity,
                                bool has_policy, uint32_t duration_ms,
                                uint32_t revision, bool& result) {
    int32_t before_duration = 0;
    const bool state_was_active = prepare_shoot_duration_tick(
        actor, task_identity, has_policy, revision, before_duration);
    if (!invoke_original(task, ped, result)) return false;
    finish_shoot_duration_tick(actor, task_identity, before_duration, state_was_active,
                               result, has_policy, duration_ms, revision, ped);
    return true;
}

WaitState* wait_for_task(const ObjectiveActor& actor, const TaskIdentity& task) {
    const auto found = std::find_if(waits.begin(), waits.end(), [&](const WaitState& wait) {
        return wait.actor == actor && wait.task == task;
    });
    return found == waits.end() ? nullptr : &*found;
}

WaitState* observe_task_burst(const ObjectiveActor& actor, const TaskIdentity& task,
                              const UseGunState& use_gun, uint32_t interval_ms,
                              bool burst_seen) {
    try {
        auto* state = wait_for_task(actor, task);
        if (!state) {
            waits.push_back(WaitState{});
            state = &waits.back();
            state->actor = actor;
            state->task = task;
            state->use_gun_task = use_gun.identity;
            state->interval_ms = interval_ms;
            state->burst_seen = burst_seen;
        }
        return state;
    } catch (...) {
        return nullptr;
    }
}

bool arm_native_interval(WaitState& state, uint32_t native_deadline,
                         uint32_t now, uint32_t interval_ms) {
    const uint32_t owned_deadline = native_deadline_for_interval(now, interval_ms);
    state.native_deadline = native_deadline;
    state.interval_ms = interval_ms;
    state.burst_seen = false;
    state.owns_deadline = false;
    if (!write_owned_deadline(state, owned_deadline)) return false;
    return true;
}

bool use_gun_requested_burst(const UseGunState& state) {
    return state.exists && (state.next_command == gun_command_fireburst ||
                            state.last_command == gun_command_fireburst);
}

bool process_shoot_duration_only(uintptr_t task, uintptr_t ped,
                                 const ObjectiveActor& actor,
                                 const TaskIdentity& task_identity,
                                 uint32_t duration_ms, uint32_t revision) {
    bool native_result = false;
    if (!invoke_with_shoot_duration(task, ped, actor, task_identity, true,
                                    duration_ms, revision, native_result)) return false;
    return native_result;
}

bool process_gun_control(uintptr_t task, uintptr_t ped) {
    if (!previous_process_ped) return false;

    uintptr_t before_vtable = 0;
    uint32_t before_deadline = 0;
    if (!safe_read_task(task, before_vtable, before_deadline) ||
        before_vtable != active_vtable_address()) {
        // This callback is reached through a virtual slot. Even if the object
        // no longer has the profiled vtable, do not swallow native task work.
        bool native_result = false;
        return invoke_original(task, ped, native_result) ? native_result : false;
    }

    TaskIdentity task_identity{};
    if (!task_identity_for(task, task_identity)) {
        clear_wait_for_task_address(task);
        shoot_durations.erase(std::remove_if(shoot_durations.begin(), shoot_durations.end(),
            [&](const ShootDurationState& state) { return state.task.address == task; }),
            shoot_durations.end());
        bool native_result = false;
        return invoke_original(task, ped, native_result) ? native_result : false;
    }

    auto* policy = live_policy_for_ped(ped);
    if (!policy || (!policy->interval_ms && !policy->has_shoot_duration)) {
        // A void lifecycle clear can leave a receipt behind when a protected
        // task field temporarily refuses restoration. Retry it on the next
        // native ProcessPed visit, while still letting SA process the task.
        waits.erase(std::remove_if(waits.begin(), waits.end(), [&](WaitState& wait) {
            if (wait.actor.native_ped != ped) return false;
            return restore_native_deadline(wait);
        }), waits.end());
        bool native_result = false;
        const auto duration_state = std::find_if(shoot_durations.begin(), shoot_durations.end(),
            [&](const ShootDurationState& state) {
                return state.actor.native_ped == ped && state.task == task_identity;
            });
        if (duration_state != shoot_durations.end()) {
            const ObjectiveActor actor = duration_state->actor;
            return invoke_with_shoot_duration(task, ped, actor, task_identity, false,
                0, 0, native_result) ? native_result : false;
        }
        return invoke_original(task, ped, native_result) ? native_result : false;
    }
    const ObjectiveActor actor = policy->actor;
    const uint32_t interval_ms = policy->interval_ms;
    const bool has_shoot_duration = policy->has_shoot_duration;
    const uint32_t shoot_duration_ms = policy->shoot_duration_ms;
    const uint32_t shoot_revision = policy->shoot_revision;
    clear_stale_shoot_duration_task(task_identity);
    clear_other_shoot_durations_for_actor(actor, task_identity);
    if (!interval_ms) {
        return process_shoot_duration_only(task, ped, actor, task_identity,
                                           shoot_duration_ms, shoot_revision);
    }
    clear_stale_wait_for_task(task_identity);
    clear_other_waits_for_actor(actor, task_identity);

    int8_t firing_command = 0;
    if (!safe_read_firing_command(task, firing_command)) {
        if (auto* stale = wait_for_task(actor, task_identity)) restore_native_deadline(*stale);
        clear_wait_for_task(actor, task_identity);
        bool native_result = false;
        return invoke_with_shoot_duration(task, ped, actor, task_identity,
            has_shoot_duration, shoot_duration_ms, shoot_revision, native_result)
            ? native_result : false;
    }

    auto* pending = wait_for_task(actor, task_identity);
    if (pending && (firing_command == gun_command_end_leisure ||
                    firing_command == gun_command_end_now)) {
        restore_native_deadline(*pending);
        clear_wait_for_task(actor, task_identity);
        pending = nullptr;
    }

    UseGunState use_gun_before{};
    if (pending && (!safe_read_use_gun_state(ped, use_gun_before) ||
                    !use_gun_before.exists || !(use_gun_before.identity == pending->use_gun_task))) {
        restore_native_deadline(*pending);
        clear_wait_for_task(actor, task_identity);
        pending = nullptr;
    }

    bool native_result = false;
    if (!invoke_with_shoot_duration(task, ped, actor, task_identity,
            has_shoot_duration, shoot_duration_ms, shoot_revision, native_result)) {
        if (pending) restore_native_deadline(*pending);
        clear_wait_for_task(actor, task_identity);
        return false;
    }
    if (native_result || !actor_is_live(actor)) {
        if (actor_is_live(actor))
            if (auto* finished = wait_for_task(actor, task_identity)) restore_native_deadline(*finished);
        clear_wait_for_task(actor, task_identity);
        return native_result;
    }

    uintptr_t after_vtable = 0;
    uint32_t after_deadline = 0;
    TaskIdentity after_identity{};
    if (!safe_read_task(task, after_vtable, after_deadline) ||
        after_vtable != active_vtable_address() ||
        !task_identity_for(task, after_identity) || !(after_identity == task_identity)) {
        clear_wait_for_task(actor, task_identity);
        return native_result;
    }
    if (!safe_read_firing_command(task, firing_command) ||
        firing_command == gun_command_end_leisure || firing_command == gun_command_end_now) {
        if (auto* finished = wait_for_task(actor, task_identity)) restore_native_deadline(*finished);
        clear_wait_for_task(actor, task_identity);
        return native_result;
    }

    UseGunState use_gun_after{};
    if (!safe_read_use_gun_state(ped, use_gun_after) || !use_gun_after.exists) {
        if (auto* stale = wait_for_task(actor, task_identity)) restore_native_deadline(*stale);
        clear_wait_for_task(actor, task_identity);
        return native_result;
    }

    pending = wait_for_task(actor, task_identity);
    if (pending && !(use_gun_after.identity == pending->use_gun_task)) {
        restore_native_deadline(*pending);
        clear_wait_for_task(actor, task_identity);
        pending = nullptr;
    }

    const bool requested_burst = use_gun_requested_burst(use_gun_after);
    if (pending) {
        if (before_deadline != native_waiting_sentinel &&
            after_deadline == native_waiting_sentinel) {
            // ProcessPed's due branch at 0x62555F writes the sentinel at
            // 0x625576 before ControlGun(FIREBURST) at 0x6257F7.
            pending->owns_deadline = false;
            pending->burst_seen = requested_burst;
        } else if (pending->owns_deadline && after_deadline != pending->owned_deadline) {
            // SA changed the deadline, so it is no longer ours to restore.
            pending->owns_deadline = false;
        }

        if (use_gun_after.last_command == gun_command_fireburst)
            pending->burst_seen = true;

        if (before_deadline == native_waiting_sentinel &&
            after_deadline != 0 && after_deadline != native_waiting_sentinel &&
            pending->burst_seen && use_gun_after.last_command != gun_command_fireburst) {
            uint32_t now = 0;
            const auto* current_policy = live_policy_for_ped(ped);
            if (current_policy && current_policy->actor == actor &&
                current_policy->interval_ms == interval_ms && safe_read_native_clock(now)) {
                if (!arm_native_interval(*pending, after_deadline, now, interval_ms))
                    clear_wait_for_task(actor, task_identity);
            } else {
                clear_wait_for_task(actor, task_identity);
            }
        }
        return native_result;
    }

    // Observe the first real FIREBURST without changing it. Only after the PE
    // sentinel->finite transition proves that this burst ended do we replace
    // SA's newly scheduled deadline with raw now+interval.
    observe_task_burst(actor, task_identity, use_gun_after, interval_ms,
                       use_gun_after.last_command == gun_command_fireburst ||
                       (before_deadline != native_waiting_sentinel &&
                        after_deadline == native_waiting_sentinel && requested_burst));
    return native_result;
}

bool __fastcall gun_control_process_ped(uintptr_t task, void*, uintptr_t ped) {
    return process_gun_control(task, ped);
}

bool install_process_hook() {
    if (!native_profile_valid()) return false;
    const uintptr_t hook = reinterpret_cast<uintptr_t>(&gun_control_process_ped);
    const uintptr_t current = current_process_slot();
    if (hook_installed) return current == hook;
    if (current != expected_native_process_ped()) return false;
    previous_process_ped = current;
#if defined(LCS_ATTACK_TIMER_BACKEND_TESTS)
    if (!patch_process_slot(current, hook)) {
        if (current_process_slot() == hook) {
            hook_installed = true;
            return true;
        }
        previous_process_ped = 0;
        return false;
    }
#else
    if (!patch_process_slot(gun_control_vtable_address + process_ped_slot_offset, current, hook)) {
        if (current_process_slot() == hook) {
            hook_installed = true;
            return true;
        }
        previous_process_ped = 0;
        return false;
    }
#endif
    hook_installed = true;
    return true;
}

}

bool sa_set_char_attack_timer(const ObjectiveActor& actor, uint32_t milliseconds) {
    if (!sa_executor_profile_matches() || !actor_is_live(actor)) return false;
    if (!milliseconds) {
        if (!restore_and_prune_waits_for_actor(actor)) return false;
        const auto found = std::find_if(policies.begin(), policies.end(), [&](const Policy& policy) {
            return policy.actor.handle == actor.handle;
        });
        if (found == policies.end()) return true;
        if (!(found->actor == actor)) {
            clear_waits_for_handle(actor.handle);
            clear_shoot_durations_for_handle(actor.handle);
            policies.erase(found);
            return true;
        }
        found->interval_ms = 0;
        if (!found->has_shoot_duration) policies.erase(found);
        return true;
    }

    if (!install_process_hook()) return false;
    const auto found = std::find_if(policies.begin(), policies.end(), [&](const Policy& policy) {
        return policy.actor.handle == actor.handle;
    });
    if (found != policies.end()) {
        if (!(found->actor == actor)) {
            clear_waits_for_handle(actor.handle);
            clear_shoot_durations_for_handle(actor.handle);
            *found = Policy{actor};
        } else if (found->interval_ms != milliseconds) {
            if (!restore_and_prune_waits_for_actor(found->actor)) return false;
        }
        found->actor = actor;
        found->interval_ms = milliseconds;
        return true;
    }
    try {
        policies.push_back(Policy{actor, milliseconds});
        return true;
    } catch (...) {
        if (policies.empty()) sa_release_char_attack_timer();
        return false;
    }
}

bool sa_set_char_shoot_timer(const ObjectiveActor& actor, uint32_t milliseconds) {
    if (!sa_executor_profile_matches() || !actor_is_live(actor)) return false;

    const auto found = std::find_if(policies.begin(), policies.end(), [&](const Policy& policy) {
        return policy.actor.handle == actor.handle;
    });
    if (!milliseconds) {
        bool restored = clear_shoot_durations_for_actor(actor);
        if (found == policies.end()) return restored;
        if (!(found->actor == actor)) {
            clear_waits_for_handle(actor.handle);
            clear_shoot_durations_for_handle(actor.handle);
            policies.erase(found);
            return restored;
        }
        found->has_shoot_duration = false;
        found->shoot_duration_ms = 0;
        found->shoot_revision = 0;
        if (!found->interval_ms) policies.erase(found);
        return restored;
    }

    if (!install_process_hook()) return false;
    auto current = found;
    if (current != policies.end()) {
        if (!(current->actor == actor)) {
            clear_waits_for_handle(actor.handle);
            clear_shoot_durations_for_handle(actor.handle);
            *current = Policy{actor};
        }
        current->actor = actor;
        current->has_shoot_duration = true;
        current->shoot_duration_ms = milliseconds;
        current->shoot_revision = next_shoot_policy_revision();
        return true;
    }
    try {
        Policy policy{actor};
        policy.has_shoot_duration = true;
        policy.shoot_duration_ms = milliseconds;
        policy.shoot_revision = next_shoot_policy_revision();
        policies.push_back(policy);
        return true;
    } catch (...) {
        if (policies.empty()) sa_release_char_attack_timer();
        return false;
    }
}

void sa_clear_char_attack_timer(const ObjectiveActor& actor) {
    restore_and_prune_waits_for_actor(actor);
    clear_shoot_durations_for_actor(actor);
    policies.erase(std::remove_if(policies.begin(), policies.end(), [&](const Policy& policy) {
        return policy.actor == actor;
    }), policies.end());
}

bool sa_release_char_attack_timer() {
    if (!restore_all_waits() || !restore_all_shoot_durations()) return false;
    policies.clear();
    waits.clear();
    shoot_durations.clear();
    if (!hook_installed) return true;
    const uintptr_t hook = reinterpret_cast<uintptr_t>(&gun_control_process_ped);
    const uintptr_t current = current_process_slot();
    if (current == previous_process_ped) {
        // Another component already restored the native function.
        hook_installed = false;
        previous_process_ped = 0;
        return true;
    }
    if (current != hook) return false; // Preserve a later plugin's vtable hook.
#if defined(LCS_ATTACK_TIMER_BACKEND_TESTS)
    if (!patch_process_slot(hook, previous_process_ped)) {
        if (current_process_slot() == previous_process_ped) {
            hook_installed = false;
            previous_process_ped = 0;
            return true;
        }
        return false;
    }
#else
    if (!patch_process_slot(gun_control_vtable_address + process_ped_slot_offset,
                            hook, previous_process_ped)) {
        if (current_process_slot() == previous_process_ped) {
            hook_installed = false;
            previous_process_ped = 0;
            return true;
        }
        return false;
    }
#endif
    hook_installed = false;
    previous_process_ped = 0;
    return true;
}

#if defined(LCS_ATTACK_TIMER_BACKEND_TESTS)
namespace attack_timer_test {
void bind(uintptr_t* vtable, uintptr_t gun_control_vtable, uintptr_t native_process_ped,
          NativeProcessPed original, uint32_t now) {
    policies.clear();
    waits.clear();
    shoot_durations.clear();
    next_shoot_revision = 1;
    test_vtable = vtable;
    test_vtable_address = gun_control_vtable;
    test_native_process_ped = native_process_ped;
    test_original = original;
    test_now = now;
    test_use_gun_ped = 0;
    test_use_gun_task = 0;
    test_fail_next_deadline_write = false;
    test_fail_next_duration_write = false;
    test_task_generations.clear();
    previous_process_ped = 0;
    hook_installed = false;
}
void set_now(uint32_t now) { test_now = now; }
void set_use_gun(uintptr_t ped, uintptr_t task) {
    test_use_gun_ped = ped;
    test_use_gun_task = task;
}
void set_task_generation(uintptr_t task, uint8_t generation) {
    const auto found = std::find_if(test_task_generations.begin(), test_task_generations.end(),
        [&](const auto& entry) { return entry.first == task; });
    if (found == test_task_generations.end()) test_task_generations.emplace_back(task, generation);
    else found->second = generation;
}
void fail_next_deadline_write() { test_fail_next_deadline_write = true; }
void fail_next_duration_write() { test_fail_next_duration_write = true; }
bool process(uintptr_t task, uintptr_t ped) { return gun_control_process_ped(task, nullptr, ped); }
uintptr_t process_hook_address() { return reinterpret_cast<uintptr_t>(&gun_control_process_ped); }
}
#endif

}
