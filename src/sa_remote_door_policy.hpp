#pragma once

#include <cstddef>
#include <cstdint>

namespace lcs {

enum class SaRemoteDoorPolicyCallKind : uint8_t {
    set_door_status,
    component_set_door_status,
    progress_door_damage,
};

struct SaRemoteDoorPolicyCallSite {
    uintptr_t address;
    SaRemoteDoorPolicyCallKind kind;
};

using SaRemoteDoorStatusCallback = void(__fastcall*)(uintptr_t, uintptr_t, int32_t, uint32_t);
using SaRemoteDoorProgressCallback = bool(__fastcall*)(uintptr_t, uintptr_t, int32_t, uintptr_t);

const SaRemoteDoorPolicyCallSite* sa_remote_door_policy_call_sites(size_t* count) noexcept;
// Returns a distinct, ABI-typed callback for this exact CALL site. The
// callback carries its site index in its compiled specialization, so it can
// still find the chained target if a later mod calls through it.
uintptr_t sa_remote_door_policy_callback_for_site(uintptr_t address) noexcept;

// Bind each callsite's pre-existing target before install_call patches its E8.
bool sa_remote_door_policy_bind_callsite(uintptr_t address, uintptr_t previous_target) noexcept;
bool sa_remote_door_policy_calls_healthy() noexcept;
bool sa_remote_door_policy_runtime_healthy() noexcept;

// Implemented beside the existing native entity ownership helpers. The policy
// entry remains registered until the native entity reference cell is cleared.
bool sa_remote_door_policy_track_vehicle(uintptr_t vehicle, int32_t handle) noexcept;
bool sa_remote_door_policy_vehicle_live(uintptr_t vehicle) noexcept;

} // namespace lcs
