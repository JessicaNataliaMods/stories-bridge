#pragma once
#include "lcs/native.hpp"
#include "lcs/route_vehicle_native.hpp"
#include "lcs/sa_executor.hpp"
namespace lcs {
struct SaCrusherEntities { uintptr_t body=0,lid=0; };
enum class SaCrusherResolve {ready,streamed_out,unavailable};
// Exact mounted models and IPL placements; no arbitrary nearby garage objects.
bool sa_crusher_configure(uint16_t body,uint16_t lid);
std::optional<bool> sa_crusher_ready();
// Which check made the last sa_crusher_ready()/resolve unavailable (static text).
const char* sa_crusher_unavailable_reason();
SaCrusherResolve sa_crusher_resolve(uintptr_t pool_root,uint16_t body,uint16_t lid,SaCrusherEntities&);
NativeResult sa_crusher_door(int32_t garage,const std::array<float,8>& bounds,float angle);
std::optional<bool> sa_crusher_contains(const std::array<float,8>& bounds,const RouteVehicleSnapshot&);
std::optional<bool> sa_crusher_collector_vehicle_touches(const std::array<float,8>& bounds,const RouteVehicleSnapshot&);
std::optional<bool> sa_crusher_collector_player_outside(const std::array<float,8>& bounds,const ExclusiveGaragePlayer&,float margin);
std::optional<bool> sa_crusher_collector_vehicle_deliverable(const RouteVehicleSnapshot&);
NativeResult sa_crusher_hold(const RouteVehicleSnapshot&,float step=1.0f,bool locked=true);
bool sa_crusher_release_vehicle();
bool sa_crusher_cleanup();
}
