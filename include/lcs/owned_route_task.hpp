#pragma once
#include "objective_receipts.hpp"
#include "script_task_capture.hpp"
#include <array>
namespace lcs {
// Only a still-private, constructed native GoToPointAndStandStill may enter.
// Native destructor/Clone callbacks preserve lineage through the event system.
// This is task lifetime only: callers still require a live actor reference,
// source intent revision and actual native bank/tree membership.
uint64_t sa_track_private_route_task(uintptr_t task,ObjectiveActor actor);
bool sa_discard_private_route_task(uintptr_t task,ObjectiveActor actor,uint64_t epoch);
std::optional<uint64_t> sa_owned_route_task_epoch(uintptr_t task,ObjectiveActor actor);
// Read the live target from an already accepted private GoToPoint task. This
// is a read-only snapshot and may be used outside its ControlSubTask callback.
std::optional<std::array<float,3>> sa_owned_route_target(uintptr_t task,ObjectiveActor actor,uint64_t epoch);
std::optional<uint64_t> sa_owned_route_binding_epoch(const CapturedScriptTask&,ObjectiveActor);
using OwnedRouteControlCallback=void(*)(void*,ObjectiveActor,uintptr_t task,uintptr_t ped);
using OwnedRouteAuthorization=bool(*)(void*);
struct OwnedRouteRetargetResult {bool executed=false,entered=false;};
// Bind a route controller to an already-owned complex task represented by a
// freshly accepted script-task receipt. Event receipts resolve to their real
// private task before the callback is installed.
bool sa_bind_owned_route_control(const CapturedScriptTask&,ObjectiveActor,uint64_t,
    OwnedRouteControlCallback,void* context);
bool sa_unbind_owned_route_control(const CapturedScriptTask&,ObjectiveActor,uint64_t,
    OwnedRouteControlCallback,void* context);
// Same-task GoToPoint retarget. Revalidates the source/observer gate at the
// native write boundary and verifies the accepted private-task epoch.
OwnedRouteRetargetResult sa_retarget_owned_route_task(uintptr_t task,ObjectiveActor,uint64_t epoch,
    const std::array<float,3>& target,OwnedRouteAuthorization,void* context);
}
