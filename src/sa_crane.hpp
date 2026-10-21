#pragma once
#include "lcs/native.hpp"
#include "lcs/route_vehicle_native.hpp"
#include <array>
#include <cstdint>
#include <optional>

namespace lcs {
// Physical consumers of the LCS crusher crane (retail CCrane): the exact mounted crane building turns about Z,
// and a carried car is held by a real SA vehicle reference (collision/gravity suspended, speeds zeroed, pose
// written from the hook). Nothing here decides pickup or drop; that is lcs::CraneService.
enum class SaCraneResolve {ready,streamed_out,unavailable};

bool sa_crane_configure(uint16_t model,const std::array<float,3>& placement);
SaCraneResolve sa_crane_resolve(uintptr_t pool_root,uint16_t model,const std::array<float,3>& placement,uintptr_t& entity);
std::optional<bool> sa_crane_ready(size_t index);
// CPlaceable::SetHeading (SetRotateZOnly), then the same Remove/UpdateRW/UpdateRwFrame/Add refresh as the bridge.
NativeResult sa_crane_rotate(size_t index,float angle);
// Retail boundingBox.max.z / forward angle of the exact live vehicle.
std::optional<float> sa_crane_vehicle_top(const RouteVehicleSnapshot&);
std::optional<float> sa_crane_vehicle_forward_angle(const RouteVehicleSnapshot&);
// Exact live target referenced by the focused player's SA enter-car task.
std::optional<RouteVehicleSnapshot> sa_crane_player_entry_target();
NativeResult sa_crane_hold(const RouteVehicleSnapshot&);
NativeResult sa_crane_carry(const RouteVehicleSnapshot&,const std::array<float,3>& position,float forward_angle,
                            const std::array<float,3>& up);
bool sa_crane_release();
// Restores the approved IPL crane pose and releases any held vehicle.
bool sa_crane_cleanup();

#ifdef LCS_CRANE_TEST_BACKEND
void sa_crane_test_pool_root(uintptr_t address);
void sa_crane_test_model_info(uintptr_t address);
#endif
}
