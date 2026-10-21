#pragma once
#include "lcs/native.hpp"
#include "lcs/route_vehicle_native.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace lcs {
// Physical side of lcs::FerryService (retail CFerryInst): one real SA boat per ferry id, built from the packaged
// ferry model (SA id 454 "tropic", the converted original ferry hull with its embedded COL3 collision) and driven
// kinematically by the service pose. The service decides where the ferry is; this module owns the entities
// (creation, weak reference, pose, frozen physics, cleanup), the per-model docked collision swap and the
// door/ramp frame nodes. Every engine call it makes is pinned by tests/ferry_abi_pin_tests.py against the SA image.
//
// docked_col is the packaged COL1 file (models/lcs/ferry-docked.col): one box and the bounding sphere of the
// retail ms_colModelFerryDocked. Empty = collision switching is refused.
bool sa_ferry_configure(int32_t sa_model,NativeExecutor execute,std::vector<uint8_t> docked_col={});
// true once the hull is bound (entities may still be streaming or not yet created).
bool sa_ferry_configured();
std::optional<bool> sa_ferry_ready();
// Source Init ignores file Z and probes only z=1 down to z=0.5;
// a miss keeps z=0. FerryService then adds its source 0.2m offset.
std::optional<float> sa_ferry_surface_z(float x,float y);
// Model-space FERRY_POS_* points (Ped_point, car1..car4 dummies of the shipped hull).
std::optional<FerryOffsets> sa_ferry_offsets();
FerryApply sa_ferry_pose(int32_t id,const FerryPose& pose,const std::array<float,3>& speed);
// Retail SwitchFerryCollision: 1 installs the docked CColModel on the ferry model info, 0 puts the original back.
NativeResult sa_ferry_set_collision(int32_t id,int32_t type);
// Retail OpenFerryDoor: rotates the door and ramp nodes of the chosen end about X by ratio of their open angle.
NativeResult sa_ferry_set_door(int32_t id,bool front,float ratio);
bool sa_ferry_collision_installed();
bool sa_ferry_cleanup();
}
