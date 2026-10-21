#include "lcs/sa_executor.hpp"
#include "lcs/route_vehicle_native.hpp"
#include "sa_owned_vehicle_entry.hpp"
#include <cmath>
namespace lcs {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
NativeResult native_execute_captured_walk(const NativeCall& call);
NativeResult native_execute_captured_vehicle(const NativeCall& call);
#else
static NativeResult native_execute_captured_walk(const NativeCall& call){return execute_sa(call);}
static NativeResult native_execute_captured_vehicle(const NativeCall& call){return execute_sa(call);}
#endif
NativeResult sa_execute_walk_with_capture(const NativeCall& call) {
    NativeResult refused;
    const bool goto_char=call.opcode==0x05D9;
    const bool kill_char=call.opcode==0x05E2;
    const size_t expected_inputs=goto_char?4u:(kill_char?2u:6u);
    if((!goto_char && !kill_char && call.opcode!=0x05D3 && call.opcode!=0x05F5) || call.inputs.size()!=expected_inputs || call.outputs || call.predicate ||
       call.text || call.text_position || call.replacement_text || call.mission_flag_reference || call.suppress_previous_brief ||
       call.source_wander_direction || call.source_wait_in_car || call.source_entity_command || call.source_statistic ||
       call.source_frenzy_headshots || call.source_environment || call.source_garage || call.source_traffic) {
        refused.diagnostic="Original walking acceptance capture packet mismatch";return refused;
    }
    const auto actor=sa_live_objective_actor(call.inputs[0]);
    if(goto_char) {
        // Reviewed packet: (actor, target ped, untimed -1, radius 1.0f). The target must be a live ped generation.
        if(call.inputs[2]!=-1 || call.inputs[3]!=0x3F800000 || !sa_live_objective_actor(call.inputs[1])) {
            refused.diagnostic="GOTO_CHAR capture requires a live target ped, the untimed marker and the reviewed radius";return refused;
        }
    } else if(kill_char) {
        // All four source KILL_CHAR/KILL_PLAYER variants map to SA's reviewed 05E2 (actor, target).
        if(!sa_live_objective_actor(call.inputs[1])) {
            refused.diagnostic="KILL_CHAR capture requires a live target ped";return refused;
        }
    } else {
        if(!source_vector(call.inputs.data()+1)) {
            refused.diagnostic="Non-finite walking destination before native entry";return refused;
        }
        if((call.inputs[4]!=4 && call.inputs[4]!=6 && call.inputs[4]!=7) || call.inputs[5]!=-2) {
            refused.diagnostic="Walking capture requires reviewed source movement and untimed task";return refused;
        }
    }
    if(!actor){refused.diagnostic="Walking acceptance requires live native ped generation";return refused;}
    const auto before=sa_script_task_snapshot();
    auto result=native_execute_captured_walk(call); // Exactly once; real receipt retained.
    if(!result.executed)return result;
    // Advisory capture cannot transform a successful task into a failed
    // original instruction. Missing snapshots must not replay its side effects.
    result.accepted_script_task.reset();
    if(!before){result.task_capture=NativeResult::TaskCapture::unavailable_before;return result;}
    const auto current=sa_live_objective_actor(actor->handle);
    if(!current || !(*current==*actor)){result.task_capture=NativeResult::TaskCapture::actor_changed;return result;}
    const auto after=sa_script_task_snapshot();
    if(!after){result.task_capture=NativeResult::TaskCapture::unavailable_after;return result;}
    result.accepted_script_task=capture_accepted_script_task(*before,*after,uint32_t(actor->native_ped),call.opcode);
    result.task_capture=result.accepted_script_task?NativeResult::TaskCapture::captured:NativeResult::TaskCapture::no_unique_record;
    return result;
}

NativeResult sa_execute_vehicle_task_with_capture(const NativeCall& call) {
    NativeResult refused;
    const auto empty_packet=[&] {
        return !call.outputs && !call.predicate && !call.text && !call.text_position &&
            !call.replacement_text && !call.mission_flag_reference && !call.suppress_previous_brief &&
            !call.source_wander_direction && !call.source_wait_in_car && !call.source_entity_command &&
            !call.source_statistic && !call.source_frenzy_headshots && !call.source_environment &&
            !call.source_garage && !call.source_traffic;
    };
    if(!empty_packet()) {
        refused.diagnostic="Original vehicle acceptance capture packet mismatch";return refused;
    }

    size_t expected_inputs=0;
    bool has_target=false;
    switch(call.opcode) {
    case 0x05CB: expected_inputs=3;has_target=true;break; // enter as driver
    case 0x05CA: expected_inputs=4;has_target=true;break; // enter as passenger
    case 0x05CD: expected_inputs=2;has_target=true;break; // leave specified car
    case 0x0633: expected_inputs=1;break;                // leave any car
    default:
        refused.diagnostic="Vehicle acceptance opcode has no reviewed native producer";return refused;
    }
    if(call.inputs.size()!=expected_inputs) {
        refused.diagnostic="Original vehicle acceptance operand count mismatch";return refused;
    }
    if((call.opcode==0x05CB || call.opcode==0x05CA) && call.inputs[2]!=-1) {
        refused.diagnostic="Vehicle enter task requires its reviewed untimed marker";return refused;
    }
    if(call.opcode==0x05CA && (call.inputs[3]<-1 || call.inputs[3]>7)) {
        refused.diagnostic="Passenger seat is outside the native passenger array";return refused;
    }

    const auto actor=sa_live_objective_actor(call.inputs[0]);
    if(!actor || !sa_route_actor_vehicle_snapshot(*actor)) {
        refused.diagnostic="Vehicle acceptance requires a live native ped snapshot";return refused;
    }
    std::optional<RouteVehicleSnapshot> target;
    if(has_target) {
        target=sa_route_vehicle_snapshot(call.inputs[1]);
        if(!target) {
            refused.diagnostic="Vehicle acceptance requires a live native vehicle generation";return refused;
        }
    }

#ifndef LCS_ISOLATED_ENTITY_BACKEND_TEST
    // Our entry factory owns the still-private task and all native clones.
    // Retain its epoch-backed acceptance instead of sampling a vanilla task.
    if(call.opcode==0x05CA || call.opcode==0x05CB)
        return sa_create_owned_vehicle_entry(call);
#endif
    // Capture is advisory for the remaining native exit commands: it never
    // suppresses a genuine source command because the task bank is unreadable. The native task itself is
    // entered once and its real result is always retained.
    const auto before=sa_script_task_snapshot();
    auto result=native_execute_captured_vehicle(call);
    if(!result.executed)return result;
    result.accepted_script_task.reset();
    if(!before) {result.task_capture=NativeResult::TaskCapture::unavailable_before;return result;}
    const auto current_actor=sa_live_objective_actor(actor->handle);
    if(!current_actor || !(*current_actor==*actor)) {
        result.task_capture=NativeResult::TaskCapture::actor_changed;return result;
    }
    if(target) {
        const auto current_target=sa_route_vehicle_snapshot(target->handle);
        if(!current_target || current_target->entity!=target->entity ||
           current_target->generation!=target->generation) {
            result.task_capture=NativeResult::TaskCapture::actor_changed;return result;
        }
    }
    const auto after=sa_script_task_snapshot();
    if(!after) {result.task_capture=NativeResult::TaskCapture::unavailable_after;return result;}
    result.accepted_script_task=capture_accepted_script_task(
        *before,*after,uint32_t(actor->native_ped),call.opcode);
    result.task_capture=result.accepted_script_task?NativeResult::TaskCapture::captured:
        NativeResult::TaskCapture::no_unique_record;
    return result;
}
}
