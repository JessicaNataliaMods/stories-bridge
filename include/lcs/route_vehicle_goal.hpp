#pragma once

#include "cop_native_translation.hpp"
#include "route_intent.hpp"
#include "route_vehicle_native.hpp"
#include <cmath>
#include <utility>

namespace lcs {

// Retail 1158 converts one float with EE CVT.W.S, then stores the low
// uint16 word. EE conversion truncates and saturates the signed word; never
// depend on the host game's FPU rounding mode or an out-of-range C++ cast.
inline std::optional<uint16_t> source_enter_car_range_multiplier(float value) {
    if(!std::isfinite(value))return {};
    const double wide=value;
    const int32_t word=wide>=2147483648.0 ? INT32_MAX :
        wide<=-2147483648.0 ? INT32_MIN : static_cast<int32_t>(wide);
    return static_cast<uint16_t>(static_cast<uint32_t>(word));
}

struct RouteVehicleNativeFacts {
    uint32_t now=0;
    RouteActorVehicleSnapshot actor;
    // `target` must identify the source m_carInObjective, if it still resolves.
    // A missing snapshot alone is not proof that the source weak reference was
    // cleared (it may simply be unavailable this frame).
    std::optional<RouteVehicleSnapshot> target;
    // Needed only for ProcessObjective's driver-to-passenger promotion.
    // The vehicle's driver handle and pointer must match this live ped.
    std::optional<RouteActorVehicleSnapshot> target_driver;
    std::optional<RouteVehicleSnapshot> current_vehicle;
    std::optional<bool> target_reference_cleared;
    std::optional<bool> player_accelerating;
    std::optional<bool> player_controls_disabled;
    std::optional<bool> bus_jacked;
    std::optional<float> enter_car_range_multiplier;
};

enum class RouteVehicleGoalAction {none,unavailable,restore_previous,exit_current_vehicle,enter_as_passenger,move_still,idle_still};
struct RouteVehicleGoalStep {
    RouteVehicleGoalAction action=RouteVehicleGoalAction::none;
    OriginalRouteIntents::Plan plan;
    SourceEntityIdentity target;
    std::optional<ObjectiveActor> driver;
    bool warp_near_entity_offscreen=false;
};

// Source ProcessObjective decisions for the vehicle objectives that can
// interrupt a route. This consumes source intent and validated native facts;
// it never treats a missing task as objective completion.
class OriginalRouteVehicleGoals {
    static SourceEntityIdentity identity(const RouteVehicleSnapshot& vehicle) {
        return {vehicle.handle,vehicle.entity,vehicle.generation};
    }
    static SourceEntityIdentity current_vehicle_identity(const RouteActorVehicleSnapshot& actor) {
        return {actor.current_vehicle_handle,actor.current_vehicle_entity,actor.current_vehicle_generation};
    }
    static bool same_vehicle(const SourceEntityIdentity& identity,const RouteVehicleSnapshot& vehicle) {
        return identity.valid() && identity==OriginalRouteVehicleGoals::identity(vehicle);
    }
    static bool process_objective_runs(int32_t state) {
        // ProcessObjective's source gate is IsPedInControl() or PED_DRIVING.
        // The reviewed source enum uses state < 39 for the former and 50 for
        // PED_DRIVING; no SA raw value is compared directly.
        return state>=0 && (state<39 || state==50);
    }
    static bool ready_restore(OriginalRouteIntents& intents,ObjectiveActor actor,
            RouteVehicleGoalStep& step) {
        auto plan=intents.restored(actor);
        if(plan.status()!=OriginalRouteIntents::Change::ready)return false;
        step.action=RouteVehicleGoalAction::restore_previous;
        step.plan=std::move(plan);
        return true;
    }
    static bool exit_current_vehicle(const RouteVehicleNativeFacts& facts,RouteVehicleGoalStep& step) {
        if(!facts.actor.in_vehicle || !facts.current_vehicle ||
           !same_vehicle(current_vehicle_identity(facts.actor),*facts.current_vehicle)) {
            step.action=RouteVehicleGoalAction::unavailable;
            return false;
        }
        step.action=RouteVehicleGoalAction::exit_current_vehicle;
        step.target=identity(*facts.current_vehicle);
        return true;
    }
    static bool promote_driver_entry(const OriginalRouteIntents::State& state,
            const RouteVehicleNativeFacts& facts,const RouteVehicleSnapshot& target,
            RouteVehicleGoalStep& step) {
        // PedAI.cpp's non-kill branch promotes driver-entry to passenger-entry
        // only for the original FOLLOW_ROUTE interruption handled here.
        if(state.current!=18 || state.previous!=OriginalRouteIntents::follow_route)return false;
        const bool has_driver_handle=target.driver_handle>=0;
        const bool has_driver_pointer=target.driver_ped!=0;
        if(!has_driver_handle && !has_driver_pointer)return false;
        if(!has_driver_handle || !has_driver_pointer || !facts.target_driver) {
            step.action=RouteVehicleGoalAction::unavailable;return true;
        }
        const auto& driver=*facts.target_driver;
        if(driver.actor.handle!=target.driver_handle || driver.actor.native_ped!=target.driver_ped ||
           !driver.actor.native_ped) {
            step.action=RouteVehicleGoalAction::unavailable;return true;
        }
        if(driver.native_type!=facts.actor.native_type)return false;
        if(!facts.actor.source_created_by ||
           (*facts.actor.source_created_by==2 && !driver.source_created_by)) {
            step.action=RouteVehicleGoalAction::unavailable;return true;
        }
        const bool mission_actor=*facts.actor.source_created_by==2;
        const bool random_driver=driver.source_created_by==std::optional<uint8_t>(1);
        if(mission_actor && random_driver)return false;
        step.action=RouteVehicleGoalAction::enter_as_passenger;
        step.target=identity(target);
        step.driver=driver.actor;
        return true;
    }
    static std::optional<bool> source_leave_exit_allowed(const RouteVehicleNativeFacts& facts,
            int32_t source_state) {
        // PedAI::ProcessObjective skips another exit for EXIT_TRAIN,
        // DRAG_FROM_CAR, and EXIT_CAR in the source enum.
        if(source_state==48 || source_state==57 || source_state==60)return false;
        // PEDTYPE_COP is source/native type 6. Cops only exit from a moving
        // vehicle when it is a boat or its horizontal speed squared is < .005².
        if(facts.actor.native_type!=6)return true;
        if(!facts.current_vehicle ||
           !same_vehicle(current_vehicle_identity(facts.actor),*facts.current_vehicle))return {};
        if(facts.current_vehicle->is_boat)return true;
        const auto speed=facts.current_vehicle->speed_magnitude_sq_2d;
        if(!speed || !std::isfinite(*speed) || *speed<0.0f)return {};
        return *speed<0.005f*0.005f;
    }
public:
    // Preflight only. The caller executes the native effect exactly once and
    // commits this immutable source projection only after an accepted receipt.
    OriginalRouteIntents::Plan accepted(OriginalRouteIntents& intents,ObjectiveActor actor,
            int32_t objective,const OriginalRouteIntents::VehicleAdmission& admission) const {
        if(objective!=16 && objective!=17 && objective!=18)return {};
        return intents.vehicle_interruption(actor,objective,admission);
    }

    RouteVehicleGoalStep tick(ObjectiveActor actor,const RouteVehicleNativeFacts& facts,
            OriginalRouteIntents& intents) const {
        RouteVehicleGoalStep step;
        if(!(facts.actor.actor==actor) || !facts.actor.alive) {
            step.action=RouteVehicleGoalAction::unavailable;return step;
        }
        const auto source_state=original_cop_state(facts.actor.native_state);
        const auto state=intents.state(actor);if(!state)return step;
        if(state->current!=16 && state->current!=17 && state->current!=18)return step;
        if(!source_state) {step.action=RouteVehicleGoalAction::unavailable;return step;}
        if(!process_objective_runs(*source_state))return step;

        if(state->current==16) {
            if(!state->leave_car_deadline_known) {
                step.action=RouteVehicleGoalAction::unavailable;return step;
            }
            if(facts.now<=state->leave_car_deadline)return step;
            if(!facts.actor.in_vehicle) {
                ready_restore(intents,actor,step);
                return step;
            }
            bool source_requests_exit=!facts.actor.is_player;
            if(facts.actor.is_player) {
                // The source player path restores only when the player is
                // accelerating and the vehicle is not in the bus-jacked state.
                if(!facts.player_accelerating) {
                    step.action=RouteVehicleGoalAction::unavailable;return step;
                }
                source_requests_exit=!*facts.player_accelerating;
                if(!source_requests_exit) {
                    if(!facts.bus_jacked) {step.action=RouteVehicleGoalAction::unavailable;return step;}
                    source_requests_exit=*facts.bus_jacked;
                }
            }
            if(source_requests_exit) {
                const auto allowed=source_leave_exit_allowed(facts,*source_state);
                if(!allowed) {
                    step.action=RouteVehicleGoalAction::unavailable;return step;
                }
                if(*allowed)
                    exit_current_vehicle(facts,step);
                return step;
            }
            if(facts.actor.is_player)
                ready_restore(intents,actor,step);
            return step;
        }

        if(state->current!=17 && state->current!=18)return step;
        if(facts.actor.in_vehicle) {
            if(!facts.current_vehicle ||
               !same_vehicle(current_vehicle_identity(facts.actor),*facts.current_vehicle)) {
                step.action=RouteVehicleGoalAction::unavailable;return step;
            }
            if(state->target.valid() && same_vehicle(state->target,*facts.current_vehicle)) {
                // ProcessObjective marks bObjectiveCompleted. Its global
                // post-switch completion check then restores the previous
                // objective after this case returns.
                ready_restore(intents,actor,step);
                return step;
            }
            exit_current_vehicle(facts,step); // source keeps goal17/18 and exits this vehicle.
            return step;
        }

        if(!state->target.valid()) {
            // ProcessObjective marks bObjectiveCompleted for a null car
            // reference; its global post-switch check restores previous.
            ready_restore(intents,actor,step);
            return step;
        }
        if(facts.target_reference_cleared==true) {
            // RegisterReference cleared the source target pointer; the null
            // target completion path restores previous after the switch.
            ready_restore(intents,actor,step);
            return step;
        }
        if(!facts.target || !same_vehicle(state->target,*facts.target)) {
            step.action=RouteVehicleGoalAction::unavailable;return step;
        }

        const auto& target=*facts.target;
        if(state->current==17 && target.num_passengers>=target.max_passengers) {
            ready_restore(intents,actor,step);
            return step;
        }
        // ENTER_CAR checks this timer before seeking or evaluating the distant
        // branch. That branch issues only SetMoveState(STILL), then breaks.
        if(!state->leave_car_deadline_known) {
            step.action=RouteVehicleGoalAction::unavailable;return step;
        }
        if(facts.now<state->leave_car_deadline) {
            step.action=RouteVehicleGoalAction::move_still;return step;
        }
        if(*source_state>=39)return step; // only IsPedInControl runs the remaining enter checks.
        if(promote_driver_entry(*state,facts,target,step))return step;
        if(!std::isfinite(target.health)) {
            step.action=RouteVehicleGoalAction::unavailable;return step;
        }
        if(!target.is_bike && !target.upside_down) {
            step.action=RouteVehicleGoalAction::unavailable;return step;
        }
        if(target.upside_down==true && !target.is_bike) {
            ready_restore(intents,actor,step);
            return step;
        }
        // The objective's passenger timer is a fallback timeout while
        // EnteringCar fails; it does not delay the initial native objective.
        if(target.health<=0.0f) {
            ready_restore(intents,actor,step);
            return step;
        }

        if(!facts.enter_car_range_multiplier || !std::isfinite(*facts.enter_car_range_multiplier) ||
           *facts.enter_car_range_multiplier<0.0f) {
            step.action=RouteVehicleGoalAction::unavailable;return step;
        }
        if(facts.enter_car_range_multiplier && std::isfinite(*facts.enter_car_range_multiplier) &&
           *facts.enter_car_range_multiplier>=0.0f) {
            const auto dx=target.position[0]-facts.actor.position[0];
            const auto dy=target.position[1]-facts.actor.position[1];
            const auto dz=target.position[2]-facts.actor.position[2];
            const auto distance=std::sqrt(dx*dx+dy*dy+dz*dz);
            const auto range=*facts.enter_car_range_multiplier*30.0f;
            if(!std::isfinite(distance) || !std::isfinite(range)) {
                step.action=RouteVehicleGoalAction::unavailable;return step;
            }
            if(distance>range) {
                // LCS first warps an unoccupied, off-screen vehicle when both
                // it and the ped are off-screen. Preserve that effect as a
                // separate action before the already-computed far branch.
                if(target.driver_handle<0) {
                    if(!target.on_screen || !facts.actor.on_screen) {
                        step.action=RouteVehicleGoalAction::unavailable;return step;
                    }
                    if(!*target.on_screen && !*facts.actor.on_screen) {
                        step.warp_near_entity_offscreen=true;
                    }
                }
                const bool previous_kill=state->previous==9;
                if(previous_kill) {
                    ready_restore(intents,actor,step);
                    return step;
                }
                if(facts.actor.is_player) {
                    if(!facts.player_controls_disabled) {
                        step.action=RouteVehicleGoalAction::unavailable;return step;
                    }
                    if(!*facts.player_controls_disabled) {
                        ready_restore(intents,actor,step);
                        return step;
                    }
                }
                if(!facts.actor.mission_character) {
                    step.action=RouteVehicleGoalAction::unavailable;return step;
                }
                if(!*facts.actor.mission_character)ready_restore(intents,actor,step);
                else {
                    // The literal mission-character branch calls SetIdle and
                    // SetMoveState(STILL). The service performs both through
                    // its native effect adapter after revalidating this step.
                    step.action=RouteVehicleGoalAction::idle_still;return step;
                }
            }
        }
        return step;
    }
};

}
