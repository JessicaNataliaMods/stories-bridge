#pragma once

#include "lcs/objective_receipts.hpp"
#include <array>
#include <cstdint>
#include <optional>

namespace lcs {
struct NativeResult;

// Snapshot of native SA vehicle facts. Optional booleans are unknown when the
// relevant native query/profile cannot be proved; callers must fail closed
// when the source rule depends on one.
struct RouteVehicleSnapshot {
    int32_t handle=-1;
    uintptr_t entity=0;
    uint8_t generation=0;
    std::array<float,3> position{};
    float health=0.0f;
    bool alive=false;
    // Equivalent to source CVector::MagnitudeSqr2D over native m_vecMoveSpeed.
    std::optional<float> speed_magnitude_sq_2d;
    // Equivalent to source CVector::MagnitudeSqr over native m_vecMoveSpeed.
    std::optional<float> speed_magnitude_sq_3d;
    // Raw CPhysical::m_vecMoveSpeed (+0x44) when all three components are finite; the crane judges each axis.
    std::optional<std::array<float,3>> move_speed;
    // Literal source CVehicle::CanPedExitCar(false); unknown if matrix up.z or
    // a required speed/turn input cannot be read from the live native vehicle.
    std::optional<bool> source_can_ped_exit_car;
    // Literal source CVehicle::CanPedOpenLocks over the live raw lock status
    // forwarded by LOCK_CAR_DOORS; unknown outside the source enum range.
    std::optional<bool> source_can_ped_open_locks;
    // Native entity status translated through the existing source status map.
    std::optional<bool> player_disabled;
    bool is_boat=false;
    bool is_bike=false;
    std::optional<bool> is_bus;
    std::optional<bool> upside_down;
    std::optional<bool> on_screen;
    uint8_t num_passengers=0;
    uint8_t max_passengers=0;
    int32_t driver_handle=-1;
    uintptr_t driver_ped=0;
    // The native passenger array has eight physical seats. -1 represents an
    // empty seat; num_passengers remains SA's raw passenger count.
    std::array<int32_t,8> passenger_handles{{-1,-1,-1,-1,-1,-1,-1,-1}};
    std::array<uintptr_t,8> passenger_peds{};
    // Raw CVehicle::m_nGettingInFlags at +0x486; bits use native door flags.
    uint8_t getting_in_flags=0;
    int32_t native_class=-1; // SA CVehicle subclass0..11; unknown is not an automobile.
};

// Native SA actor facts only. `native_state` is the raw CPed state; this API
// does not translate it into an LCS/source enum. Objective/timer state remains
// in the route service's source ledger.
struct RouteActorVehicleSnapshot {
    ObjectiveActor actor;
    std::array<float,3> position{};
    int32_t native_state=0;
    // Raw CPed::m_nMoveState from the SDK field at +0x534.
    int32_t native_move_state=0;
    // Raw native CPed::m_nPedType (+0x598), not an LCS enum translation.
    int32_t native_type=0;
    // Source EnteringCar() after approved SA-to-source state translation:
    // source PED_CARJACK (56) or PED_ENTER_CAR (58). Unknown if the native
    // state has no source counterpart (SA DIE_BY_STEALTH at state 56).
    std::optional<bool> source_entering_car;
    float health=0.0f;
    uint8_t created_by=0;
    // Original provenance comes from a source constructor, never from the
    // native SA created-by byte or mission-cleanup context alone.
    std::optional<uint8_t> source_created_by;
    bool controllable=false;
    bool alive=false;
    bool attached=false;
    bool in_vehicle=false;
    bool is_player=false;
    // Native CPed::bBusJacked (SDK bitfield at +0x46F, bit 2), distinct from
    // CVehicle::bIsBeingCarJacked. Unknown when the actor snapshot is unavailable.
    std::optional<bool> bus_jacked;
    // CPed::bKindaStayInSamePlace, bit 22 (+0x46E mask 0x40).
    // Source ANY_MEANS skips vehicle search and player-police flight here.
    std::optional<bool> kinda_stay_in_same_place;
    std::optional<bool> on_screen;
    std::optional<bool> mission_character;
    // Raw native CPhysical/CPed standing-on entity pointer, zero if absent.
    // Compare only with a separately live-checked native entity identity.
    uintptr_t standing_on_entity=0;
    int32_t current_vehicle_handle=-1;
    uintptr_t current_vehicle_entity=0;
    uint8_t current_vehicle_generation=0;
};

std::optional<RouteVehicleSnapshot> sa_route_vehicle_snapshot(int32_t handle);
// Three-way vehicle lookup. A missing snapshot can be a read/profile failure, so callers must not
// treat `unavailable` as proof that the vehicle was deleted; only `absent` (profiled pool, free slot or
// different generation) ends a vehicle identity.
enum class VehicleLookupStatus {unavailable,absent,present};
struct RouteVehicleLookup {
    VehicleLookupStatus status=VehicleLookupStatus::unavailable;
    std::optional<RouteVehicleSnapshot> snapshot;
};
RouteVehicleLookup sa_route_vehicle_lookup(int32_t handle);std::optional<RouteActorVehicleSnapshot> sa_route_actor_vehicle_snapshot(const ObjectiveActor& actor);
std::optional<bool> sa_route_player_controls_disabled();
// Source driver-to-passenger conversion clears this exact vehicle flag after
// the passenger objective has been accepted. Other flag bits are preserved.
bool sa_route_clear_car_jacked(const RouteVehicleSnapshot& expected,
    bool(*authorized)(void*),void* authorization_context);
// Source ANY_MEANS: after caller proves the target is near and, if occupied,
// that the source CanPedExitCar(false) condition permits exit,
// zero only cruise speed and set the vehicle autopilot mission to MISSION_NONE
// before issuing the source LEAVE_CAR objective. Authorization is checked at
// both native write boundaries. It binds the service provenance/route owner;
// an entered partial stop must not be replayed.
NativeResult sa_route_stop_vehicle_for_kill_exit(const RouteActorVehicleSnapshot& actor,
    const RouteVehicleSnapshot& vehicle,bool(*authorized)(void*),void* authorization_context);
// Source ANY_MEANS far/on-foot branch: stop the source-owned remembered car
// by writing only cruise speed zero before ENTER_DRIVER. Authorization binds
// the real source weak-reference provenance; native on-foot state is checked
// independently. An entered partial write must not be replayed.
NativeResult sa_route_stop_vehicle_for_kill_entry(const RouteActorVehicleSnapshot& actor,
    const RouteVehicleSnapshot& car,bool(*authorized)(void*),void* authorization_context);

// RegisterReference cells must have stable addresses owned by the caller.
// Release requires the original identity, preventing cleanup against a reused
// pool slot after generation wrap.
bool sa_register_vehicle_reference(int32_t handle,uintptr_t* stable_cell);
bool sa_vehicle_reference_live(const RouteVehicleSnapshot& expected,const uintptr_t* stable_cell);
bool sa_release_vehicle_reference(const RouteVehicleSnapshot& expected,uintptr_t* stable_cell);

}
