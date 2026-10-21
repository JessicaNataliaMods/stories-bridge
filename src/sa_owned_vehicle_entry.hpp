#pragma once

#include "lcs/sa_executor.hpp"

namespace lcs {

#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
uintptr_t native_owned_vehicle_entry_passenger_vtable();
#endif
// Retail class table of CTaskComplexEnterCarAsPassenger. The isolated backend test supplies
// the callable fixture table so the explicit-door factory runs its production code.
inline uintptr_t sa_owned_entry_passenger_vtable() {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
    return native_owned_vehicle_entry_passenger_vtable();
#else
    return 0x86EADC;
#endif
}

// Retail class vtable for each entry subtask type the wait policy recognises
// (800 approach, 203 stand-still). The backend test checks this table against
// the PE: the class must report exactly that task type.
constexpr uintptr_t sa_owned_entry_subtask_vtable(int32_t type) {
    return type==800?0x86EC4C:(type==203?0x86DD2C:0);
}

// Build an untimed 05CA/05CB entry task with a private lifetime vtable, then
// assign it through SA's ordinary GivePedScriptedTask consumer. The receipt is
// still the real SA script-task-bank delta.
NativeResult sa_create_owned_vehicle_entry(const NativeCall& call);

// Used by the existing explicit-door passenger factory after construction
// and before GivePedScriptedTask. The returned epoch identifies this object and
// every native clone made through its wrapped Clone slot.
uint64_t sa_track_private_vehicle_entry(uintptr_t task,ObjectiveActor actor,
        uint16_t opcode);
bool sa_discard_private_vehicle_entry(uintptr_t task,ObjectiveActor actor,
        uint16_t opcode);

std::optional<uint64_t> sa_owned_vehicle_entry_epoch(
        const CapturedScriptTask& binding,ObjectiveActor actor);

// Hold or resume this exact owned entry lineage without replaying 05CA/05CB.
// true means the policy was accepted; false means its current child cannot be
// safely paused; nullopt means actor/task ownership or native profiles are unavailable.
std::optional<bool> sa_set_owned_vehicle_entry_wait(ObjectiveActor actor,
        uint64_t epoch,bool waiting);
bool sa_owned_vehicle_entry_wait_ready();
std::optional<bool> sa_owned_vehicle_entry_active(ObjectiveActor actor,uint64_t epoch);

// false means native retirement/cancellation is still pending; nullopt means
// the task or actor cannot be proven as ours. No foreign task is cleared.
std::optional<bool> sa_cancel_owned_vehicle_entry(ObjectiveActor actor,
        uint64_t epoch);

// Detach policy state and release its weak actor reference once every tracked
// native clone has retired. Outstanding vtables remain valid until then.
void sa_release_owned_vehicle_entry(ObjectiveActor actor,uint64_t epoch);

} // namespace lcs
