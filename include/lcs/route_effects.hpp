#pragma once

#include "lcs/route_vehicle_native.hpp"
#include "lcs/sa_executor.hpp"
#include <cstdint>
#include <optional>

namespace lcs {

// The caller supplies the source-intent/owned-epoch gate. It is checked at
// each native mutation boundary after the live CPed identity was refreshed.
using RouteEffectAuthorization=bool(*)(void*);

struct RouteWarpEffectResult {
    NativeResult native;
    bool teleported=false;
    // The source helper updates m_leaveCarTimer after the complete loop, even
    // when no candidate was safe. Set only when that post-loop write is safe.
    std::optional<uint32_t> leave_car_deadline;
};

// Passenger-entry doors used by the source fallback after its objective timer
// expires. These are source-side meanings; the native backend maps them to
// San Andreas eCarNodes explicitly.
enum class RouteEntryDoor : uint8_t { front_right, rear_left, rear_right };

// Entity objective setter promotes an existing STILL state to RUN before its
// boat admission/restore decision. Other movement states are preserved.
NativeResult sa_route_promote_still_to_run(const RouteActorVehicleSnapshot& expected,
        RouteEffectAuthorization authorized,void* authorization_context);

// Source enter-car timer branch: only CPed::SetMoveState(PEDMOVE_STILL).
NativeResult sa_route_move_still(const RouteActorVehicleSnapshot& expected,
        RouteEffectAuthorization authorized,void* authorization_context);

// Source far/off-screen mission-ped branch: SetIdle then SetMoveState(STILL).
NativeResult sa_route_idle_ped_still(const RouteActorVehicleSnapshot& expected,
        RouteEffectAuthorization authorized,void* authorization_context);

// Source fallback's SetPosition(GetPositionToOpenCarDoor(vehicle, door))
// prefix. The caller confirms the seat and door before calling; this receipt
// gates its subsequent native entry command.
NativeResult sa_route_position_ped_for_car_entry(const RouteActorVehicleSnapshot& actor,
        const RouteVehicleSnapshot& vehicle,RouteEntryDoor door,
        RouteEffectAuthorization authorized,void* authorization_context);

// Native 05CA passenger-task equivalent with a source-selected vehicle node.
// Carry-on-after-falling-off matches the original 05CA timed wrapper (true).
// The task is reported executed only when the scripted-task bank records its
// accepted 05CA binding; an entered assignment without a unique capture is
// terminal and must not be replayed.
NativeResult sa_route_enter_passenger_door(const RouteActorVehicleSnapshot& actor,
        const RouteVehicleSnapshot& vehicle,RouteEntryDoor door,bool mission_cleanup,
        RouteEffectAuthorization authorized,void* authorization_context);

// Clear the source point-gun effect on a live route actor. Only a profiled
// vanilla secondary UseGun task is aborted; non-gun secondary tasks are kept.
NativeResult sa_route_clear_point_gun_at(const RouteActorVehicleSnapshot& actor,
        RouteEffectAuthorization authorized,void* authorization_context);

// Literal CPed::WarpPedToNearEntityOffScreen port for the source's empty,
// off-screen target-vehicle branch. Timer input/output remains in the source
// route ledger; `now` is the already captured source frame time.
RouteWarpEffectResult sa_route_warp_ped_near_entity_offscreen(
        const RouteActorVehicleSnapshot& actor,const RouteVehicleSnapshot& vehicle,
        uint32_t now,uint32_t leave_car_deadline,bool leave_car_deadline_known,
        RouteEffectAuthorization authorized,void* authorization_context);

}
