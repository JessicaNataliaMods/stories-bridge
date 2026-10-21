#include "lcs/route_jobs.hpp"
#include "lcs/route_vehicle_goal.hpp"
#include "lcs/route_effects.hpp"
#include "../src/sa_route_service.hpp"
#include "lcs/route_scripts.hpp"
#include "lcs/engine_dispatch.hpp"
#include "ScriptCommands.h"
#include "lcs/task_observer.hpp"
#include <iostream>
#include <stdexcept>
#include <limits>
using namespace lcs;
static void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
static ObjectiveActor actor{0x101,0x123400};
static OriginalRouteIntents::VehicleAdmission vehicle_admission(int state,bool in_vehicle,bool attached=false,bool self=false,bool same=false) {
    OriginalRouteIntents::VehicleAdmission value;
    value.source_state=state;value.in_vehicle=in_vehicle;value.attached=attached;
    value.self_target=self;value.same_target=same;value.target={42,0x987600,42};
    value.target_boat=false;value.target_bus=false;value.mission_character=false;value.target_disabled=false;value.player_controls_disabled=false;value.leave_car_deadline=0;
    return value;
}
static EpochScriptTask token{{0,{0x05D3,0,0x567800,0x123400,1}},4};
static RouteActorSnapshot pose{actor,{10,0,0},true,true,true};
static bool motion_active=true;
static bool service_fixture=false;
static bool service_motion_capture_fault=false;
static std::optional<RouteActorVehicleSnapshot> service_actor_override;
static std::optional<RouteActorVehicleSnapshot> service_driver_override;
static std::optional<RouteVehicleSnapshot> service_vehicle_override;
static std::optional<RouteVehicleSnapshot> service_target_car_override;
static bool service_vehicle_lookup_unavailable=false;
static int service_kill_car_stops=0,service_kill_car_stop_failure=0;
static int service_kill_entry_stops=0;
static bool service_kill_entry_stop_failure=false;
static uintptr_t* service_target_ped_cell=nullptr;
static std::vector<uintptr_t*> service_target_ped_cells;
static void resolve_service_ped_references() {
    if(!service_target_ped_cell)return;
    const auto retired=*service_target_ped_cell;
    for(auto* cell:service_target_ped_cells)if(*cell==retired)*cell=0;
}
static uintptr_t* service_target_vehicle_cell=nullptr;
static uint32_t service_now=1;
static OwnedRouteControlCallback service_control=nullptr;
static void* service_control_context=nullptr;
static int writes=0;
static int service_retarget_calls=0;
static int service_idle_calls=0,service_still_calls=0;
static int service_clear_aim_calls=0;
static int service_clear_aim_failure=0;
static bool service_seek_target_unavailable=false,service_clear_invalidates_seek=false;
static int* service_clear_state_to_change=nullptr;
static int service_owned_target_reads=0;
static bool service_initial_seek_foreign=false;
static bool service_entry_cancel_pending=false;
static int service_entry_cancels=0,service_entry_releases=0;
static bool service_entry_live=true,service_entry_wait_authorized=true,service_player_disabled=false;
static std::vector<bool> service_entry_waits;
static uint32_t service_entry_task=0xBC0000;
static int service_entry_positions=0;
static int service_door_entries=0,service_car_jacked_clears=0;
static RouteEntryDoor service_last_entry_door=RouteEntryDoor::front_right;
static std::array<float,3> last_motion_target{};
namespace lcs {
std::optional<bool> sa_owned_vehicle_entry_active(ObjectiveActor a,uint64_t epoch) {
    if(!service_fixture || !(a==actor) || !epoch)return {};
    return service_entry_live;
}
std::optional<bool> sa_set_owned_vehicle_entry_wait(ObjectiveActor a,uint64_t epoch,bool waiting) {
    if(!service_fixture || !(a==actor) || !epoch || !service_entry_wait_authorized)return {};
    service_entry_waits.push_back(waiting);return true;
}
std::optional<uint64_t> sa_owned_vehicle_entry_epoch(const CapturedScriptTask& binding,ObjectiveActor a) {
    if(!service_fixture || !(a==actor) || binding.record.ped!=actor.native_ped ||
        (binding.record.opcode!=0x05CA && binding.record.opcode!=0x05CB) || !binding.record.task)return {};
    return binding.record.task;
}
std::optional<bool> sa_cancel_owned_vehicle_entry(ObjectiveActor a,uint64_t epoch) {
    if(!service_fixture || !(a==actor) || !epoch)return {};
    ++service_entry_cancels;return !service_entry_cancel_pending;
}
void sa_release_owned_vehicle_entry(ObjectiveActor a,uint64_t epoch) {
    if(service_fixture && a==actor && epoch)++service_entry_releases;
}
void service_capture_entry(NativeResult& result,const NativeCall& call) {
    if(call.opcode==0x05CA || call.opcode==0x05CB)
        result.accepted_script_task=CapturedScriptTask{11,{call.opcode,0,++service_entry_task,uint32_t(actor.native_ped),1}};
}
std::optional<std::array<float,3>> sa_owned_route_target(uintptr_t task,ObjectiveActor a,uint64_t epoch) {
    ++service_owned_target_reads;
    if(!service_fixture || service_seek_target_unavailable || !(a==actor) ||
       task!=token.binding.record.task || epoch!=token.epoch)return {};
    return last_motion_target;
}
bool sa_bind_owned_route_control(const CapturedScriptTask&,ObjectiveActor,uint64_t,OwnedRouteControlCallback callback,void* context) {
    if(service_fixture){service_control=callback;service_control_context=context;return true;}
    throw std::runtime_error("Non-owned route fixtures must not bind private native control");
}
bool sa_unbind_owned_route_control(const CapturedScriptTask&,ObjectiveActor,uint64_t,OwnedRouteControlCallback,void*) {
    if(service_fixture){service_control=nullptr;service_control_context=nullptr;return true;}
    throw std::runtime_error("Non-owned route fixtures must not unbind private native control");
}
OwnedRouteRetargetResult sa_retarget_owned_route_task(uintptr_t,ObjectiveActor,uint64_t,const std::array<float,3>& target,OwnedRouteAuthorization authorized,void* context) {
    if(service_fixture && authorized && authorized(context)){++service_retarget_calls;last_motion_target=target;return {true,true};}
    throw std::runtime_error("Non-owned route fixtures must not enter private native retargeting");
}
std::optional<RouteActorVehicleSnapshot> sa_route_actor_vehicle_snapshot(const ObjectiveActor& a) {
    if(service_fixture && service_driver_override && service_driver_override->actor==a)return service_driver_override;
    if(!service_fixture || !(a==actor))return {};
    if(service_actor_override)return service_actor_override;
    RouteActorVehicleSnapshot facts;facts.actor=a;facts.alive=true;facts.controllable=true;return facts;
}
std::optional<RouteVehicleSnapshot> sa_route_vehicle_snapshot(int32_t handle) {
    if(service_fixture && service_target_car_override && service_target_car_override->handle==handle)return service_target_car_override;
    if(service_fixture && service_vehicle_override && service_vehicle_override->handle==handle)return service_vehicle_override;
    return {};
}
RouteVehicleLookup sa_route_vehicle_lookup(int32_t handle) {
    RouteVehicleLookup result;
    if(!service_fixture || service_vehicle_lookup_unavailable)return result;
    if(const auto snapshot=sa_route_vehicle_snapshot(handle)) {
        result.status=VehicleLookupStatus::present;result.snapshot=snapshot;
    } else result.status=VehicleLookupStatus::absent;
    return result;
}
std::optional<ObjectiveActor> sa_live_objective_actor(int32_t handle) {
    if(service_fixture && service_driver_override && service_driver_override->actor.handle==handle)return service_driver_override->actor;
    return service_fixture && handle==actor.handle?std::optional<ObjectiveActor>(actor):std::nullopt;
}
bool sa_register_actor_reference(const ObjectiveActor& target,uintptr_t* cell) {
    const auto live=sa_live_objective_actor(target.handle);
    if(!cell || !live || !(*live==target))return false;
    *cell=target.native_ped;
    if(std::find(service_target_ped_cells.begin(),service_target_ped_cells.end(),cell)==service_target_ped_cells.end())service_target_ped_cells.push_back(cell);
    service_target_ped_cell=cell;return true;
}
bool sa_actor_reference_live(const ObjectiveActor& target,const uintptr_t* cell) {
    const auto live=sa_live_objective_actor(target.handle);
    return cell && *cell==target.native_ped && live && *live==target;
}
bool sa_release_actor_reference(uintptr_t* cell) {
    if(!cell)return false;
    service_target_ped_cells.erase(std::remove(service_target_ped_cells.begin(),service_target_ped_cells.end(),cell),service_target_ped_cells.end());
    service_target_ped_cell=service_target_ped_cells.empty()?nullptr:service_target_ped_cells.back();
    *cell=0;return true;
}
std::optional<NativeFrameClock> sa_frame_clock(){return service_fixture?std::optional<NativeFrameClock>(NativeFrameClock{service_now,service_now-1,service_now}):std::nullopt;}
std::optional<bool> sa_route_player_controls_disabled(){return service_fixture?std::optional<bool>(service_player_disabled):std::nullopt;}
bool sa_route_clear_car_jacked(const RouteVehicleSnapshot&,bool(*authorized)(void*),void* context) {
    if(!service_fixture || !authorized || !authorized(context))return false;
    ++service_car_jacked_clears;return true;
}
NativeResult sa_route_stop_vehicle_for_kill_exit(const RouteActorVehicleSnapshot&,
        const RouteVehicleSnapshot&,bool(*authorized)(void*),void* context) {
    NativeResult result;
    if(!service_fixture || !authorized || !authorized(context) || service_kill_car_stop_failure==1)return result;
    ++service_kill_car_stops;result.native_entered=true;
    if(service_kill_car_stop_failure==4 && service_driver_override) {
        if(service_driver_override->in_vehicle && service_target_car_override)
            service_target_car_override->source_can_ped_exit_car=false;
        else service_driver_override->in_vehicle=true;
    }
    result.executed=service_kill_car_stop_failure!=2;return result;
}
NativeResult sa_route_stop_vehicle_for_kill_entry(const RouteActorVehicleSnapshot&,
        const RouteVehicleSnapshot&,bool(*authorized)(void*),void* context) {
    NativeResult result;
    if(!service_fixture || !authorized || !authorized(context))return result;
    ++service_kill_entry_stops;result.native_entered=true;
    result.executed=!service_kill_entry_stop_failure;return result;
}
std::optional<int32_t> sa_scripted_pad_state(int32_t selector){return service_fixture && selector==22?std::optional<int32_t>(0):std::nullopt;}
bool sa_register_vehicle_reference(int32_t handle,uintptr_t* cell) {
    const auto vehicle=sa_route_vehicle_snapshot(handle);if(!vehicle || !cell)return false;
    *cell=vehicle->entity;service_target_vehicle_cell=cell;return true;
}
bool sa_vehicle_reference_live(const RouteVehicleSnapshot& expected,const uintptr_t* cell) {
    const auto vehicle=sa_route_vehicle_snapshot(expected.handle);
    return cell && vehicle && *cell==expected.entity && vehicle->entity==expected.entity && vehicle->generation==expected.generation;
}
NativeResult sa_route_clear_point_gun_at(const RouteActorVehicleSnapshot& expected,RouteEffectAuthorization authorized,void* context) {
    NativeResult result;
    if(service_fixture && expected.actor==actor && authorized && authorized(context)) {
        ++service_clear_aim_calls;
        result.native_entered=service_clear_aim_failure!=1;
        result.executed=service_clear_aim_failure==0;
        if(service_clear_invalidates_seek)service_seek_target_unavailable=true;
        if(service_clear_state_to_change)*service_clear_state_to_change=0;
    }
    return result;
}
NativeResult sa_route_idle_ped_still(const RouteActorVehicleSnapshot& expected,RouteEffectAuthorization authorized,void* context) {
    NativeResult result;
    if(service_fixture && expected.actor==actor && authorized && authorized(context)) {
        ++service_idle_calls;result.executed=result.native_entered=true;
    }
    return result;
}
std::optional<ScriptTaskSnapshot> sa_script_task_snapshot() {
    if(!service_fixture)return {};
    ScriptTaskSnapshot bank{};bank[token.binding.slot]=token.binding.record;return bank;
}
std::optional<uint64_t> sa_owned_route_binding_epoch(const CapturedScriptTask& captured,ObjectiveActor a) {
    if(!service_fixture || service_initial_seek_foreign || !(a==actor))return {};
    if(!(captured.record==token.binding.record))return {};
    return token.epoch;
}
bool sa_release_vehicle_reference(const RouteVehicleSnapshot&,uintptr_t* cell) {
    check(service_fixture && cell,"Vehicle reference release belongs to explicit service fixture");
    if(service_target_vehicle_cell==cell)service_target_vehicle_cell=nullptr;
    *cell=0;return true;
}
NativeResult sa_route_promote_still_to_run(const RouteActorVehicleSnapshot& expected,RouteEffectAuthorization authorized,void* context) {
    NativeResult result;
    if(service_fixture && expected.actor==actor && authorized && authorized(context)) {
        result.executed=true;
        if(service_actor_override && service_actor_override->native_move_state==1) {
            service_actor_override->native_move_state=6;result.native_entered=true;
        }
    }
    return result;
}
NativeResult sa_route_move_still(const RouteActorVehicleSnapshot& expected,RouteEffectAuthorization authorized,void* context) {
    NativeResult result;
    if(service_fixture && expected.actor==actor && authorized && authorized(context)) {
        ++service_still_calls;result.executed=result.native_entered=true;
    }
    return result;
}
NativeResult sa_route_position_ped_for_car_entry(const RouteActorVehicleSnapshot& expected,
        const RouteVehicleSnapshot&,RouteEntryDoor,RouteEffectAuthorization authorized,void* context) {
    NativeResult result;
    if(service_fixture && expected.actor==actor && authorized && authorized(context)) {
        ++service_entry_positions;result.executed=result.native_entered=true;
    }
    return result;
}
NativeResult sa_route_enter_passenger_door(const RouteActorVehicleSnapshot& expected,
        const RouteVehicleSnapshot&,RouteEntryDoor door,bool,RouteEffectAuthorization authorized,void* context) {
    NativeResult result;
    if(service_fixture && expected.actor==actor && authorized && authorized(context)) {
        ++service_door_entries;service_last_entry_door=door;
        result.executed=result.native_entered=true;
        result.accepted_script_task=CapturedScriptTask{1,{0x05CA,0,0xAA8800,uint32_t(actor.native_ped),1}};
    }
    return result;
}
RouteWarpEffectResult sa_route_warp_ped_near_entity_offscreen(const RouteActorVehicleSnapshot& expected,
        const RouteVehicleSnapshot&,uint32_t now,uint32_t deadline,bool known,RouteEffectAuthorization authorized,void* context) {
    RouteWarpEffectResult result;
    if(service_fixture && expected.actor==actor && known && authorized && authorized(context)) {
        result.native.executed=true;
        if(deadline<=now)result.leave_car_deadline=now+3000;
    }
    return result;
}
std::optional<RouteActorSnapshot> sa_route_actor_snapshot(const ObjectiveActor& a){return a==actor?std::optional<RouteActorSnapshot>(pose):std::nullopt;}
// Like the production proof, only an ASSOCIATED task record (never the queued event) can be the active task.
std::optional<bool> sa_active_walking_task(const ObjectiveActor& a,const CapturedScriptTask& owned){
    if(!(a==actor) || owned.record.event || !owned.record.task)return std::nullopt;
    return motion_active;
}
NativeResult sa_create_route_motion(const RouteMotionRequest& request) {
    NativeResult result;
    if(!request.authorized || !request.authorized(request.authorization_context))return result;
    ++writes;last_motion_target=request.target;result.executed=result.native_entered=true;
    if(service_motion_capture_fault)return result;
    token.binding.record.task+=0x80;++token.epoch;service_initial_seek_foreign=false;
    result.accepted_script_task=token.binding;return result;
}
}
struct Host {
    std::optional<EpochScriptTask> refresh(const EpochScriptTask& expected) {
        return expected.epoch==token.epoch && expected.binding.record==token.binding.record?std::optional<EpochScriptTask>(token):std::nullopt;
    }
    std::optional<EpochScriptTask> accept(const CapturedScriptTask& value) {
        return value.record==token.binding.record?std::optional<EpochScriptTask>(token):std::nullopt;
    }
};
int main(){try{
    check(source_enter_car_range_multiplier(1.9f)==1 &&
          source_enter_car_range_multiplier(-1.9f)==65535 &&
          source_enter_car_range_multiplier(65536.f)==0,
          "1158 retains EE truncation and the stored low unsigned16 bits");
    check(source_enter_car_range_multiplier(std::numeric_limits<float>::max())==65535 &&
          source_enter_car_range_multiplier(-std::numeric_limits<float>::max())==0 &&
          !source_enter_car_range_multiplier(std::numeric_limits<float>::infinity()) &&
          !source_enter_car_range_multiplier(std::numeric_limits<float>::quiet_NaN()),
          "1158 saturates finite EE word conversion and refuses non-finite inputs");
    OriginalRouteTable table;for(float x:{0.f,10.f,20.f})check(table.add(7,{x,0,0}),"Source route fixture");
    OriginalRouteIntents ledger;
    check(!ledger.route_intent(actor) && !ledger.accepted(ledger.follow(actor,7,1,0,table)),"A polled or unknown actor cannot acquire invented source intent");
    check(ledger.initialized(actor) && !ledger.initialized(actor),"Actual source constructor initialization is explicit and never repeated");
    auto start=ledger.state(actor);check(start && start->current==0 && start->previous==0,"Source constructor starts with NONE, not an assumed native objective");
    for(int state:{54,55})check(ledger.follow(actor,7,1,state,table).status()==OriginalRouteIntents::Change::ignored,"Dead/dying source states ignore route writes");
    check(ledger.follow(actor,99,1,0,table).status()==OriginalRouteIntents::Change::unavailable,"Missing route has no invented first target");
    auto plan=ledger.follow(actor,0x10007,0x10001,0,table);
    check(plan.status()==OriginalRouteIntents::Change::ready && plan.proposed().route==7 && plan.proposed().mode==1 && plan.proposed().cursor->target()[0]==10,"Route assignment narrows signed halfwords and selects real second point");
    check(ledger.state(actor)->current==0,"Preflight does not publish source intent before native acceptance");
    check(ledger.accepted(plan) && !ledger.accepted(plan),"An accepted native/source assignment commits once; stale plan cannot replay");
    const auto running_before=ledger.state(actor)->revision;
    check(ledger.follow(actor,7,1,0,table).status()==OriginalRouteIntents::Change::ignored && ledger.state(actor)->revision==running_before,"Same source route/mode preserves progress and revision");
    check(ledger.accepted(ledger.running(actor,true)) && ledger.route_intent(actor)->running,"Accepted source running mutation reaches native route reader");
    check(ledger.follow(actor,7,3,32,table).status()==OriginalRouteIntents::Change::ready,"Entity-overload state32 guard must not be invented for source route overload");
    for(auto admission:{vehicle_admission(32,true,false,false,false),
                       vehicle_admission(0,true,true,false,false),
                       vehicle_admission(0,true,false,true,false)})
        check(ledger.vehicle_interruption(actor,18,admission).status()==OriginalRouteIntents::Change::ignored,"Retail state32, attachment and self-target guards refuse temporary transition before any effect");
    check(!ledger.accepted(ledger.vehicle_interruption(actor,16,vehicle_admission(0,false))),"LEAVE outside a car is ignored, never a fabricated temporary objective");
    check(ledger.accepted(ledger.vehicle_interruption(actor,18,vehicle_admission(0,false))),"Actual ENTER transition accepted");
    auto interrupted=ledger.state(actor);
    check(interrupted->current==18 && interrupted->previous==30 && ledger.route_intent(actor)->temporary && !ledger.route_intent(actor)->current_route,"Vehicle interruption preserves actual previous route separately");
    check(ledger.follow(actor,7,3,0,table).status()==OriginalRouteIntents::Change::ignored,"Previous FOLLOW_ROUTE blocks even a DIFFERENT new route mode before SetFollowRoute");
    check(ledger.accepted(ledger.restored(actor)),"Real restoration callback can commit previous objective");
    check(ledger.state(actor)->current==30 && ledger.state(actor)->previous==0 && ledger.state(actor)->cursor->target()[0]==10,"Restoration consumes previous objective without resetting route initialization");
    check(ledger.accepted(ledger.vehicle_interruption(actor,16,vehicle_admission(0,true))),"Actual LEAVE transition can interrupt route");
    check(ledger.accepted(ledger.vehicle_interruption(actor,17,vehicle_admission(0,true))),"ENTER while leaving uses ForceStoredObjective's DIFFERENT retail table");
    check(ledger.state(actor)->current==17 && ledger.state(actor)->previous==16,"ForceStored ENTER preserves LEAVE, not a guessed route stack");
    check(ledger.accepted(ledger.follow(actor,7,3,0,table)),"Route can replace temporary when PREVIOUS is not route");
    check(ledger.state(actor)->current==30 && ledger.state(actor)->previous==30 && !ledger.route_intent(actor)->temporary,"SetFollowRoute explicitly replaces current temporary after pending write");
    check(ledger.follow(actor,7,1,0,table).status()==OriginalRouteIntents::Change::ignored,"Duplicate current/previous route remains guarded until real restoration");

    // Feed the actual new ledger through the existing production native route
    // adapter and its real consumer. No hand-made current_route=true reader.
    ledger.clear();check(ledger.initialized(actor),"Fresh real source initialization");
    check(ledger.accepted(ledger.follow(actor,7,1,0,table)),"Accept source route");
    auto revision=ledger.state(actor)->revision;Host host;
    NativeRouteConsumerBackend<Host> backend(host,ledger,revision);
    auto consumer=OriginalRouteConsumer::accepted(*ledger.state(actor)->cursor,actor,token,false);
    check(consumer && consumer->tick(table,backend)==OriginalRouteConsumer::Result::advanced && writes==1 && consumer->cursor().target()[0]==20,"Source ledger, actual production adapter, observer receipt and live route consumer advance together");
    check(ledger.accepted(ledger.vehicle_interruption(actor,18,vehicle_admission(0,false))),"Accepted source interruption changes intent revision");
    const auto before=writes;
    check(consumer->tick(table,backend)==OriginalRouteConsumer::Result::unavailable && writes==before,"Old source/native ownership cannot continue after a real intent mutation");
    const auto stale=ledger.restored(actor);ledger.retired(actor);
    check(!ledger.route_intent(actor) && !ledger.accepted(stale),"Entered failure/retirement revokes source receipts rather than fabricating restoration");
    ObjectiveActor generation{0x102,actor.native_ped};check(ledger.initialized(generation),"New source actor generation can initialize");
    check(!ledger.route_intent(actor),"Old native pool generation cannot inherit current objective");
    const auto serial=ledger.state(generation)->revision;ledger.clear();
    check(ledger.initialized(actor) && ledger.state(actor)->revision>serial,"World reset never resurrects an earlier source intent revision");
    {
        ledger.clear();check(ledger.initialized(actor),"Initialize actual route-job source actor");
        OriginalRouteJobs<Host> jobs(host,ledger,table);
        check(jobs.source_accepted(ledger.follow(actor,7,1,0,table),true) && jobs.needs_assignment(actor) &&
            jobs.target(actor)==std::optional<std::array<float,3>>({10,0,0}),"Source assignment seed becomes a pending real engine motion, not an invented accepted task");
        NativeResult effect;effect.executed=effect.native_entered=true;effect.accepted_script_task=token.binding;
        check(jobs.motion_accepted(actor,ledger.state(actor)->revision,effect),"Actual observer accepts initial native motion receipt");
        pose.position={10,0,0};
        check(jobs.tick(actor)==OriginalRouteConsumer::Result::advanced && !jobs.needs_assignment(actor),"Owned route job advances through real native adapter");
        check(jobs.source_accepted(ledger.running(actor,true),false) && jobs.needs_assignment(actor) &&
            jobs.target(actor)==std::optional<std::array<float,3>>({20,0,0}),"Source running change requests SAME advanced target immediately, without resetting route progress");
        ++token.epoch;token.binding.record.task+=0x80;effect.accepted_script_task=token.binding;
        check(jobs.motion_accepted(actor,ledger.state(actor)->revision,effect),"Running update requires a new actual accepted task epoch");
        check(jobs.source_accepted(ledger.vehicle_interruption(actor,18,vehicle_admission(0,false)),false) &&
            jobs.tick(actor)==OriginalRouteConsumer::Result::suspended && !jobs.needs_assignment(actor),"Vehicle interruption saves advanced route cursor and cannot emit a replacement motion");
        check(jobs.source_accepted(ledger.restored(actor),false) && jobs.needs_assignment(actor) &&
            jobs.target(actor)==std::optional<std::array<float,3>>({20,0,0}),"Actual restoration reuses live advanced cursor, never assignment seed10");
        check(!jobs.motion_accepted(actor,ledger.state(actor)->revision,effect) && !jobs.needs_assignment(actor),"Restoration rejects identical OLD native ownership even when source revision is new, then quarantines");
        const auto count=writes;check(jobs.tick(actor)==OriginalRouteConsumer::Result::unavailable && writes==count,"Quarantined restore never replays an already entered effect");
        jobs.retired(actor);check(!ledger.route_intent(actor) && !jobs.target(actor),"Retirement removes source intent and native route job together");
    }
    {
        check(ledger.initialized(actor),"Fresh actual source actor after retirement");
        OriginalRouteJobs<Host> jobs(host,ledger,table);
        check(jobs.source_accepted(ledger.follow(actor,7,1,0,table),true),"New source route job");
        ++token.epoch;token.binding.record.task+=0x80;
        NativeResult effect;effect.executed=effect.native_entered=true;effect.accepted_script_task=token.binding;
        check(jobs.motion_accepted(actor,ledger.state(actor)->revision,effect),"Initial route job native assignment");
        pose.position={10,0,0};check(jobs.tick(actor)==OriginalRouteConsumer::Result::advanced,"Advance before temporary objective");
        check(jobs.source_accepted(ledger.vehicle_interruption(actor,18,vehicle_admission(0,false)),true) &&
            jobs.source_accepted(ledger.restored(actor),true),"Actual interruption/restoration pair");
        ++token.epoch;token.binding.record.task+=0x80;effect.accepted_script_task=token.binding;
        check(jobs.motion_accepted(actor,ledger.state(actor)->revision,effect),"Genuinely NEW native assignment resumes the retained route cursor");
        pose.position={20,0,0};const auto count=writes;
        check(jobs.tick(actor)==OriginalRouteConsumer::Result::sequence_ended &&
            jobs.tick(actor)==OriginalRouteConsumer::Result::sequence_ended && writes==count && !jobs.needs_assignment(actor) &&
            ledger.state(actor)->current==OriginalRouteIntents::follow_route,"STOP_WHEN_DONE end holds at the final point: no task replay, no quarantine, FOLLOW_ROUTE stays the source objective with no fabricated completion");
        jobs.clear();
    }
    {
        OriginalRouteJobs<Host> retire_previous(host,ledger,table);retire_previous.retired(actor);
        check(ledger.initialized(actor),"Fresh actual source actor for the pending-route end");
        OriginalRouteJobs<Host> jobs(host,ledger,table);
        check(jobs.source_accepted(ledger.follow(actor,7,1,0,table),true) && jobs.needs_assignment(actor),"New route job, no native task assigned yet");
        pose.position={10,0,0};
        check(jobs.tick(actor)==OriginalRouteConsumer::Result::advanced,"A pending route advances its copied points while another objective owns movement");
        pose.position={20,0,0};const auto count=writes;
        check(jobs.tick(actor)==OriginalRouteConsumer::Result::sequence_ended &&
            jobs.tick(actor)==OriginalRouteConsumer::Result::sequence_ended && writes==count && !jobs.needs_assignment(actor) &&
            ledger.state(actor)->current==OriginalRouteIntents::follow_route,
            "A pending STOP_WHEN_DONE end holds without quarantine, assignment or fabricated completion");
        jobs.clear();
    }
    {
        OriginalRouteIntents source;OriginalRouteVehicleGoals goals;
        check(source.constructed(actor) && source.accepted(source.follow(actor,7,1,0,table)),"Construct route before literal vehicle objective processing");
        const auto admission=vehicle_admission(0,false);
        check(source.accepted(source.vehicle_interruption(actor,18,admission)),"Accepted driver interruption owns original target identity");
        RouteVehicleNativeFacts facts;facts.now=1;facts.actor.actor=actor;facts.actor.alive=true;
        facts.actor.native_state=0;facts.actor.controllable=true;facts.enter_car_range_multiplier=1.0f;
        RouteVehicleSnapshot car;car.handle=42;car.entity=0x987600;car.generation=42;
        car.health=1000;car.alive=true;car.upside_down=false;car.on_screen=true;car.max_passengers=3;
        facts.target=car;facts.target_reference_cleared=false;
        check(goals.tick(actor,facts,source).action==RouteVehicleGoalAction::none,
            "Actor merely on foot never proves completion of ENTER_CAR");
        RouteActorVehicleSnapshot target_driver;target_driver.actor={0x102,0x234500};
        target_driver.alive=true;target_driver.native_type=4;target_driver.created_by=1;target_driver.source_created_by=1;
        facts.actor.native_type=4;facts.actor.created_by=2;facts.actor.source_created_by=2;
        facts.target->driver_handle=target_driver.actor.handle;facts.target->driver_ped=target_driver.actor.native_ped;
        facts.target_driver=target_driver;
        check(goals.tick(actor,facts,source).action==RouteVehicleGoalAction::none,
            "Mission actor retains driver objective for same-type random native driver");
        facts.target_driver->created_by=2;
        facts.target_driver->source_created_by=2;
        check(goals.tick(actor,facts,source).action==RouteVehicleGoalAction::enter_as_passenger,
            "Same-type mission driver triggers literal source passenger conversion");
        facts.target_driver->source_created_by.reset();
        check(goals.tick(actor,facts,source).action==RouteVehicleGoalAction::unavailable,
            "Native created-by byte cannot invent missing original driver provenance");
        facts.target_driver.reset();
        check(goals.tick(actor,facts,source).action==RouteVehicleGoalAction::unavailable,
            "Missing live driver facts cannot guess the source passenger conversion predicate");
        facts.target->driver_handle=-1;facts.target->driver_ped=0;
        facts.actor.in_vehicle=true;facts.actor.current_vehicle_handle=42;
        facts.actor.current_vehicle_entity=car.entity;facts.actor.current_vehicle_generation=42;facts.current_vehicle=car;
        auto step=goals.tick(actor,facts,source);
        check(step.action==RouteVehicleGoalAction::restore_previous && step.plan.proposed().current==30,
            "Actual occupancy of original target reaches global source completion restoration");
        check(source.state(actor)->current==18,"Source tick preflight never commits restoration itself");
        facts.current_vehicle->handle=43;facts.actor.current_vehicle_handle=43;
        step=goals.tick(actor,facts,source);
        check(step.action==RouteVehicleGoalAction::exit_current_vehicle && source.state(actor)->current==18,
            "Wrong vehicle requests real exit while preserving original ENTER goal and target");
        facts.actor.in_vehicle=false;facts.current_vehicle.reset();facts.target.reset();
        check(goals.tick(actor,facts,source).action==RouteVehicleGoalAction::unavailable,
            "Missing native snapshot cannot masquerade as cleared source weak reference");
        facts.target_reference_cleared=true;
        check(goals.tick(actor,facts,source).action==RouteVehicleGoalAction::restore_previous,
            "Actually cleared source car reference reaches original null-target completion");
        source.clear();check(source.constructed(actor) && source.accepted(source.follow(actor,7,1,0,table)),"Fresh source route for leave timer");
        auto leave=vehicle_admission(0,true);leave.leave_car_deadline=100;
        check(source.accepted(source.vehicle_interruption(actor,16,leave)),"Accepted source leave objective carries original timer");
        facts.now=100;facts.actor.in_vehicle=false;
        check(goals.tick(actor,facts,source).action==RouteVehicleGoalAction::none,"Leave objective uses strict now greater than deadline");
        facts.now=101;
        check(goals.tick(actor,facts,source).action==RouteVehicleGoalAction::restore_previous,"After real exit and deadline source restores route");
        facts.actor.in_vehicle=true;facts.actor.is_player=true;facts.actor.current_vehicle_handle=42;
        facts.actor.current_vehicle_entity=car.entity;facts.actor.current_vehicle_generation=42;
        facts.current_vehicle=car;facts.player_accelerating=false;facts.bus_jacked.reset();
        check(goals.tick(actor,facts,source).action==RouteVehicleGoalAction::exit_current_vehicle,
            "Nonaccelerating player takes literal OR short-circuit without inventing bus-jacked fact");
        facts.player_accelerating=true;
        check(goals.tick(actor,facts,source).action==RouteVehicleGoalAction::unavailable,
            "Accelerating player needs actual ped bus-jacked flag rather than car-jacked query");
    }
    {
        const auto saved_pose=pose;OriginalRouteIntents source;
        check(source.constructed(actor),"Construct FOLLOW_PATH source route fixture");
        OriginalRouteJobs<Host> jobs(host,source,table);
        check(jobs.source_accepted(source.follow(actor,7,2,11,table),false),"FOLLOW_PATH still accepts original objective30 metadata");
        pose={actor,{10,0,0},true,true,true};pose.source_seek_allowed=false;
        const auto count=writes;
        check(!jobs.assign_pending(actor).native_entered && writes==count,"Original SetSeek FOLLOW_PATH guard leaves existing movement untouched");
        check(jobs.tick(actor)==OriginalRouteConsumer::Result::advanced && writes==count,
            "Original route counters advance on actual XY arrival even while FOLLOW_PATH blocks SetSeek");
        pose.source_seek_allowed=true;
        const auto assignment=jobs.assign_pending(actor);
        check(assignment.executed && assignment.native_entered && writes==count+1 && last_motion_target[0]==20,
            "Leaving FOLLOW_PATH assigns the advanced copied route point rather than replaying the initial target");
        pose=saved_pose;
    }
    {
        OriginalRouteIntents source;check(source.constructed(actor),"Construct route timer overload fixture");
        auto admission=vehicle_admission(0,false);admission.mission_character=true;
        admission.target_disabled=true;admission.now=100;
        check(source.accepted(source.vehicle_interruption(actor,17,admission)) &&
              source.state(actor)->objective_deadline==14100,"Actual passenger setter supplies the inherited 14-second deadline");
        auto route=source.follow(actor,7,1,0,table);
        check(route.status()==OriginalRouteIntents::Change::ready && route.proposed().objective_deadline==0 &&
              source.accepted(route),"Accepted original route overload clears previous objective timer before SetFollowRoute");
        check(source.accepted(source.vehicle_interruption(actor,17,admission)) &&
              source.state(actor)->objective_deadline==14100,"A later passenger setter still supplies its own deadline");
        const auto ignored=source.follow(actor,7,2,0,table);
        check(ignored.status()==OriginalRouteIntents::Change::ignored && ignored.proposed().objective_deadline==14100,
            "Previous-route duplicate guard precedes ClearPointGunAt and timer zeroing");
    }
    {
        OriginalRouteIntents source;check(source.constructed(actor),"Construct ordinary overload fixture");
        OriginalRouteIntents::OrdinaryAdmission admission;admission.source_state=0;admission.attached=true;
        admission.setter=OriginalRouteIntents::Setter::plain;
        check(source.ordinary(actor,1,admission).status()==OriginalRouteIntents::Change::ignored,
            "Attached actor is refused by original plain objective overload");
        admission.setter=OriginalRouteIntents::Setter::position;
        admission.target_position=std::array<float,3>{5,0,0};admission.actor_position=std::array<float,3>{0,0,0};
        check(source.ordinary(actor,24,admission).status()==OriginalRouteIntents::Change::ready,
            "Position objective overload has no invented attachment guard");
        admission.setter=OriginalRouteIntents::Setter::entity;admission.target={42,0x987600,42};
        check(source.ordinary(actor,22,admission).status()==OriginalRouteIntents::Change::ready,
            "Original entity overload permits attached DESTROY_CAR");
        admission.self_target=true;
        check(source.ordinary(actor,22,admission).status()==OriginalRouteIntents::Change::ignored,
            "Entity self-target guard is unconditional and separate from attachment allowlist");
        admission={};admission.source_state=0;admission.setter=OriginalRouteIntents::Setter::plain;
        check(source.accepted(source.ordinary(actor,6,admission)),"Actual plain wait setter starts its original zero timer");
        auto duplicate=source.ordinary(actor,6,admission);
        check(duplicate.status()==OriginalRouteIntents::Change::ignored && duplicate.proposed().current==6 &&
            duplicate.proposed().previous==source.state(actor)->previous && duplicate.proposed().objective_deadline==0,
            "Duplicate plain WAIT with zero timer preserves objective enums and revision");
    }
    {
        service_fixture=true;
        {
            OriginalRouteIntents source;int clears=0;int native_state=0;
            OwnedRouteReaders readers;readers.actor=[](uint32_t ped){return ped==actor.native_ped?std::optional<ObjectiveActor>(actor):std::nullopt;};
            SaRouteService service(source,table,std::move(readers),
                [](int32_t handle){return handle==actor.handle?std::optional<ObjectiveActor>(actor):std::nullopt;},
                [&](ObjectiveActor){return std::optional<int32_t>(native_state);},
                [&](int32_t handle){check(handle==actor.handle,"Service clears walking policy for exact actor");++clears;},{});
            check(service.constructed(actor),"Production service receives actual source constructor");
            pose.position={0,0,0};const auto count=writes;
            const auto aim_clears=service_clear_aim_calls;
            const auto target_reads=service_owned_target_reads;
            const auto retargets=service_retarget_calls;
            native_state=6;
            service_initial_seek_foreign=true;
            check(!sa_owned_route_binding_epoch(token.binding,actor),"Pre-existing service fixture SEEK_POS begins with no route-owned epoch");
            check(service.follow_route(actor.handle,7,1,true)==StepResult::continued && writes==count+1 &&
                service_clear_aim_calls==aim_clears+1 && service_owned_target_reads==target_reads && service_retarget_calls==retargets &&
                source.state(actor)->current==30 && native_state==6 && last_motion_target==std::array<float,3>{10,0,0},
                "Foreign SEEK_POS is never queried or retargeted; FOLLOW_ROUTE adapts through a fresh SA factory assignment");
            service_initial_seek_foreign=false;
            native_state=0;
            check(source.state(actor)->current==30 && writes==count+1 && clears==1,
                "Fresh native assignment commits the original route once without importing the foreign task");
            check(service.follow_route(actor.handle,7,1,true)==StepResult::continued && writes==count+1 && clears==1,
                "Duplicate original FOLLOW_ROUTE preserves accepted task without replay");
            native_state=54;
            check(service.follow_route(actor.handle,7,2,true)==StepResult::continued && writes==count+1,
                "Original dying guard precedes native motion even for different route mode");
            check(service_clear_aim_calls==aim_clears+1,"Duplicate and dying guards do not replay ClearPointGunAt");
            service.retired(actor);
            native_state=0;
            check(service.follow_route(actor.handle,7,1,true)==StepResult::unsupported && writes==count+1,
                "Retired source actor cannot reacquire route from a native pose poll");
            check(service.constructed(actor),"Actual new source constructor authorizes new generation");
            service_clear_aim_failure=1;
            check(service.follow_route(actor.handle,7,1,true)==StepResult::unsupported && writes==count+1 && source.state(actor)->current!=30,
                "ClearPointGunAt refusal leaves the source route unpublished and motion untouched");
            service_clear_aim_failure=2;
            check(service.follow_route(actor.handle,7,1,true)==StepResult::failed && writes==count+1 && !source.state(actor),
                "Entered ClearPointGunAt failure revokes source authority before any route assignment");
            service_clear_aim_failure=0;
            check(service.constructed(actor),"Actual constructor restores authority after failed native prefix");
            service_motion_capture_fault=true;
            check(service.follow_route(actor.handle,7,1,true)==StepResult::failed && writes==count+2,
                "Entered route factory without accepted owned epoch is terminal, never unsupported retry");
            check(service.follow_route(actor.handle,7,1,true)==StepResult::unsupported && writes==count+2,
                "Lost owned acceptance revokes source authority before another instruction attempt");
            service_motion_capture_fault=false;
        }
        service_fixture=false;
    }
    {
        service_fixture=true;pose={actor,{0,0,0},true,true,true};
        OriginalRouteTable routes;
        for(const auto id:{7,8,9}) {
            check(routes.add(id,{0,0,0}) && routes.add(id,{id==9?40.f:10.f,0,id==7?0.f:90.f}) &&
                routes.add(id,{id==7?20.f:(id==9?50.f:40.f),0,91}),"Same-XY source setter routes");
        }
        OriginalRouteIntents source;int state=0;
        OwnedRouteReaders readers;readers.actor=[](uint32_t ped){return ped==actor.native_ped?std::optional<ObjectiveActor>(actor):std::nullopt;};
        SaRouteService service(source,routes,std::move(readers),
            [](int32_t handle){return handle==actor.handle?std::optional<ObjectiveActor>(actor):std::nullopt;},
            [&](ObjectiveActor){return std::optional<int32_t>(state);},[](int32_t){},{});
        check(service.constructed(actor) && service.follow_route(actor.handle,7,1,true)==StepResult::continued,
            "Same-XY source setter begins with an actual accepted route task");
        const auto epoch=token.epoch;const auto writes_before=writes;
        const auto callback=service_control;const auto callback_context=service_control_context;
        state=6;
        check(service.follow_route(actor.handle,8,2,true)==StepResult::continued && token.epoch==epoch &&
            writes==writes_before && source.state(actor)->route==8 && source.state(actor)->previous==0 &&
            last_motion_target[2]==0 && source.state(actor)->cursor->target()[2]==90 &&
            service_control==callback && service_control_context==callback_context,
            "Real source route change with identical seek XY retains accepted task, epoch, native Z and callback without storing current30 as previous30");
        check(service.follow_route(actor.handle,8,3,true)==StepResult::continued && token.epoch==epoch &&
            writes==writes_before && source.state(actor)->previous==0 && source.state(actor)->mode==3,
            "Another mode change of the same objective remains possible without a fabricated previous-route guard");
        pose.position={10,0,0};service_control(service_control_context,actor,token.binding.record.task,actor.native_ped);
        check(last_motion_target==std::array<float,3>{40,0,91} && token.epoch==epoch && writes==writes_before,
            "Retained native callback consumes the new live route cursor instead of the old route");
        const auto revision=source.state(actor)->revision;const auto aim_calls=service_clear_aim_calls;
        const auto mismatch_writes=writes;const auto mismatch_epoch=token.epoch;
        const auto mismatch_retargets=service_retarget_calls,mismatch_target_reads=service_owned_target_reads;
        check(service.follow_route(actor.handle,7,1,true)==StepResult::continued &&
            source.state(actor)->revision!=revision && source.state(actor)->current==30 && source.state(actor)->route==7 && state==6 &&
            service_clear_aim_calls==aim_calls+1 && writes==mismatch_writes+1 && token.epoch==mismatch_epoch+1 &&
            service_retarget_calls==mismatch_retargets && service_owned_target_reads==mismatch_target_reads+1 &&
            last_motion_target==std::array<float,3>{10,0,0},
            "Owned SEEK_POS with a different live XY target gets a fresh factory task instead of retargeting the existing task");
        service_seek_target_unavailable=true;
        const auto unavailable_revision=source.state(actor)->revision;
        const auto unavailable_writes=writes;const auto unavailable_epoch=token.epoch;
        const auto unavailable_aim_calls=service_clear_aim_calls,unavailable_retargets=service_retarget_calls;
        const auto unavailable_target_reads=service_owned_target_reads;
        check(service.follow_route(actor.handle,8,1,true)==StepResult::continued &&
            source.state(actor)->revision!=unavailable_revision && source.state(actor)->current==30 && source.state(actor)->route==8 && state==6 &&
            service_clear_aim_calls==unavailable_aim_calls+1 && writes==unavailable_writes+1 && token.epoch==unavailable_epoch+1 &&
            service_retarget_calls==unavailable_retargets && service_owned_target_reads==unavailable_target_reads+1 &&
            last_motion_target==std::array<float,3>{10,0,90},
            "Unavailable owned SEEK_POS target prevents reuse and safely falls back to a fresh factory task");
        service_seek_target_unavailable=false;
        service_clear_invalidates_seek=true;
        const auto invalidated_writes=writes;const auto invalidated_epoch=token.epoch;
        const auto invalidated_target_reads=service_owned_target_reads,invalidated_retargets=service_retarget_calls;
        check(service.follow_route(actor.handle,7,1,true)==StepResult::failed && !source.state(actor) &&
            writes==invalidated_writes && token.epoch==invalidated_epoch &&
            service_owned_target_reads==invalidated_target_reads+2 && service_retarget_calls==invalidated_retargets,
            "Matching owned SEEK_POS whose target authority is lost during clear effects is revoked without reassignment or replay");
        service_clear_invalidates_seek=false;service_seek_target_unavailable=false;
        state=0;
        check(service.constructed(actor) && service.follow_route(actor.handle,7,1,true)==StepResult::continued,
            "Fresh accepted constructor starts the separate state-change boundary");
        state=6;service_clear_state_to_change=&state;
        const auto state_change_epoch=token.epoch;const auto state_change_writes=writes;
        check(service.follow_route(actor.handle,8,2,true)==StepResult::failed && !source.state(actor) &&
            token.epoch==state_change_epoch && writes==state_change_writes && state==0,
            "Leaving SEEK_POS inside clear effects cannot reuse the old task or publish a new source route");
        service_clear_state_to_change=nullptr;service_fixture=false;
    }
    {
        service_fixture=true;service_now=100;pose={actor,{0,0,0},true,true,true};
        RouteActorVehicleSnapshot ped;ped.actor=actor;ped.alive=ped.controllable=true;
        ped.mission_character=false;ped.source_created_by=0;ped.on_screen=true;service_actor_override=ped;
        RouteVehicleSnapshot car;car.handle=42;car.entity=0x987600;car.generation=42;
        car.alive=true;car.health=1000;car.is_bus=false;car.player_disabled=false;
        car.upside_down=false;car.on_screen=true;car.max_passengers=3;service_vehicle_override=car;
        {
            OriginalRouteIntents unknown;
            int assigned=0;
            const auto execute_entry=[&](const NativeCall& call) {
                ++assigned;NativeResult result;result.executed=result.native_entered=true;
                service_capture_entry(result,call);return result;
            };
            OwnedRouteReaders readers;
            SaRouteService service(unknown,table,std::move(readers),
                [](int32_t)->std::optional<ObjectiveActor>{return {};},
                [](ObjectiveActor)->std::optional<int32_t>{return {};},[](int32_t){},execute_entry);
            NativeCall enter;enter.opcode=0x05CB;enter.inputs={actor.handle,42,-1};
            bool projected=false;
            const auto give=[&](){return service.execute_source_objective(COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_DRIVER,enter,projected,[&]{return execute_entry(enter);});};
            service_actor_override->created_by=2; // native SA mission class, deliberately no source provenance
            service_vehicle_override->position={40,0,0};service_entry_waits.clear();
            const auto cancels=service_entry_cancels;
            check(give().executed && !projected && assigned==1 && !unknown.state(actor),
                "Native-only actor entry is accepted without inventing a source ledger");
            check(service.tick_all() && service_entry_waits.size()==1 && service_entry_waits.back() && service_entry_cancels==cancels,
                "Production-style empty actor resolver still pauses native mission entry at40m beyond30m");
            service.set_enter_car_range_multiplier(2);
            check(service.tick_all() && !service_entry_waits.back() && assigned==1,
                "Range60m resumes the same owned root without reissuing05CB");
            service.set_enter_car_range_multiplier(1);service.tick_all();
            service_vehicle_lookup_unavailable=true;const auto policies=service_entry_waits.size();
            check(service.tick_all() && service_entry_waits.size()==policies && assigned==1,
                "Unreadable vehicle defers policy changes instead of deleting the guard");
            service_vehicle_lookup_unavailable=false;
            service_entry_wait_authorized=false;service_vehicle_override->position={5,0,0};
            check(service.tick_all() && service_entry_waits.size()==policies && assigned==1,
                "A replaced foreign task cannot be overwritten by a remembered entry packet");
            service_entry_wait_authorized=true;service.clear();
            service_vehicle_override->position={40,0,0};service_entry_waits.clear();
            // Transient lookup loss after native acceptance must not lose registration.
            check(service.execute_source_objective(COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_DRIVER,enter,projected,[&]{
                auto result=execute_entry(enter);service_vehicle_lookup_unavailable=true;return result;
            }).executed,"Preflight identity is retained across an unreadable post-accept vehicle observation");
            check(service.tick_all() && service_entry_waits.empty(),"Unavailable observation never mutates the native entry");
            service_vehicle_lookup_unavailable=false;
            check(service.tick_all() && service_entry_waits.size()==1 && service_entry_waits.back(),
                "Guard survives lookup loss and pauses once the same car becomes readable");
            service.clear();service_vehicle_lookup_unavailable=true;const auto before_refusal=assigned;
            check(!give().executed && assigned==before_refusal,"Unknown target before assignment refuses before entering native code");
            service_vehicle_lookup_unavailable=false;
            service_actor_override->is_player=true;service_player_disabled=true;service_entry_waits.clear();give();
            check(service.tick_all() && service_entry_waits.back(),"Player with disabled controls waits in the owned native task");
            service_player_disabled=false;const auto player_cancel=service_entry_cancels;
            check(service.tick_all() && service_entry_cancels==player_cancel+1,"Enabled player controls end the far private approach");
            service.clear();service_actor_override->is_player=false;service_actor_override->created_by=1;
            const auto ambient_cancel=service_entry_cancels;give();
            check(service.tick_all() && service_entry_cancels==ambient_cancel+1,"Native ambient class cancels a far approach without constructing previousNONE");
            service.clear();service_actor_override->created_by=2;give();service.tick_all();
            service_actor_override->in_vehicle=true;
            const auto occupancy_cancel=service_entry_cancels,occupancy_release=service_entry_releases;
            check(service.tick_all() && !service_entry_waits.back() && service_entry_cancels==occupancy_cancel &&
                service_entry_releases==occupancy_release+1,"Actual occupancy clears only the owned wait policy and leaves native close-door/driving intact");
            service_actor_override->in_vehicle=false;
            service.clear();give();service_vehicle_override->health=0;
            service_entry_cancel_pending=true;const auto dying_cancel=service_entry_cancels;
            check(service.tick_all() && service.tick_all() && service_entry_cancels==dying_cancel+2,
                "Ended target cancellation retains ownership until retirement is confirmed");
            service_entry_cancel_pending=false;service.tick_all();service.clear();
            service_actor_override=ped;service_vehicle_override=car;
        }
        {
            // No FOLLOW_ROUTE: this is the production gap found in1158 review.
            OriginalRouteIntents standalone;
            const auto execute_entry=[](const NativeCall& call) {
                NativeResult effect;effect.executed=effect.native_entered=true;
                service_capture_entry(effect,call);return effect;
            };
            OwnedRouteReaders readers;
            SaRouteService service(standalone,table,std::move(readers),
                [](int32_t h){return h==actor.handle?std::optional<ObjectiveActor>(actor):std::nullopt;},
                [](ObjectiveActor){return original_cop_state(service_actor_override->native_state);},
                [](int32_t){},execute_entry);
            check(service.constructed(actor),"Standalone entry has a genuine source constructor without a route job");
            service_vehicle_override->position={40,0,0};
            check(service.set_enter_car_range_multiplier(2),"Standalone entry accepts60m range");
            NativeCall enter;enter.opcode=0x05CB;enter.inputs={actor.handle,42,-1};
            bool projected=false;
            const auto accepted=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_DRIVER,
                enter,projected,[&]{return execute_entry(enter);});
            check(accepted.executed && projected && service.tick_all() && standalone.state(actor)->current==18,
                "Standalone entry inside60m is consumed without inventing FOLLOW_ROUTE");
            service.set_enter_car_range_multiplier(1);service_entry_cancel_pending=true;
            const auto cancels=service_entry_cancels;
            check(!service.tick_all() && service_entry_cancels==cancels+1 && standalone.state(actor)->current==18,
                "Standalone far entry cannot publish NONE while owned native cancellation is pending");
            service_entry_cancel_pending=false;
            check(service.tick_all() && standalone.state(actor)->current==0,
                "Confirmed private-entry cancellation restores real source NONE without clearing foreign tasks");
            service_vehicle_override->position={0,0,0};projected=false;
            check(service.execute_source_objective(COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_DRIVER,enter,projected,
                [&]{return execute_entry(enter);}).executed && projected,
                "A later standalone entry starts from the restored source state");
            auto& occupied=*service_actor_override;occupied.in_vehicle=true;occupied.native_state=50;
            occupied.current_vehicle_handle=42;occupied.current_vehicle_entity=car.entity;occupied.current_vehicle_generation=42;
            const auto before_occupancy_cancels=service_entry_cancels;
            check(service.tick_all() && standalone.state(actor)->current==0 &&
                  service_entry_cancels==before_occupancy_cancels,
                "Actual occupancy restores NONE while SA closes the door and drives, without cancelling the in-car task");
            service_actor_override=ped;service_vehicle_override=car;
        }
        {
            OriginalRouteIntents source;int native_effects=0;uint16_t last_vehicle_opcode=0;int last_passenger_seat=-1;
            const auto vehicle_executor=[&](const NativeCall& call){
                ++native_effects;last_vehicle_opcode=call.opcode;
                if(call.opcode==0x05CA && call.inputs.size()==4)last_passenger_seat=call.inputs[3];
                NativeResult effect;effect.executed=effect.native_entered=true;service_capture_entry(effect,call);return effect;
            };
            OwnedRouteReaders readers;readers.actor=[](uint32_t ped){return ped==actor.native_ped?std::optional<ObjectiveActor>(actor):std::nullopt;};
            SaRouteService service(source,table,std::move(readers),
                [](int32_t handle){return handle==actor.handle?std::optional<ObjectiveActor>(actor):std::nullopt;},
                [](ObjectiveActor){return original_cop_state(service_actor_override->native_state);},
                [](int32_t){},vehicle_executor);
            check(service.constructed(actor) && service.follow_route(actor.handle,7,1,true)==StepResult::continued,
                "Production source route starts through accepted owned native factory");
            check(service_control!=nullptr,"Owned route binds actual production control callback");
            pose.position={10,0,0};service_control(service_control_context,actor,token.binding.record.task,actor.native_ped);
            check(last_motion_target[0]==20,"Owned ControlSubTask advances same task to actual second source target");
            NativeCall enter;enter.opcode=0x05CB;enter.inputs={actor.handle,42,-1};enter.mission_context=true;
            bool projected=false;
            auto result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_DRIVER,enter,projected,[&]{return vehicle_executor(enter);});
            check(result.executed && projected && native_effects==1 && source.state(actor)->current==18,
                "Real source vehicle setter interrupts route and executes native entry once");
            check(service.tick_all() && native_effects==1,"Waiting on foot does not complete ENTER or replay its native task");
            auto& facts=*service_actor_override;facts.in_vehicle=true;facts.native_state=50;
            facts.current_vehicle_handle=42;facts.current_vehicle_entity=car.entity;facts.current_vehicle_generation=42;
            pose.on_foot=false;
            check(service.tick_all() && source.state(actor)->current==30,"Actual target occupancy restores original pending route");
            NativeCall leave;leave.opcode=0x05CD;leave.inputs={actor.handle,42};leave.mission_context=true;
            projected=false;result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_LEAVE_CAR,leave,projected,[&]{return vehicle_executor(leave);});
            check(result.executed && projected && native_effects==1 && source.state(actor)->current==16,
                "Leave setter retains source intent and waits for original ProcessObjective exit gate");
            check(service.tick_all() && last_vehicle_opcode==0x05CD && native_effects==2,
                "Source leave consumer issues one actual exit for owned vehicle identity");
            check(service.tick_all() && native_effects==2,"Repeated source frame does not replay accepted exit");
            const auto epoch=token.epoch;
            facts.in_vehicle=false;facts.native_state=0;facts.current_vehicle_handle=-1;facts.current_vehicle_entity=0;
            pose.on_foot=true;
            check(service.tick_all() && source.state(actor)->current==30 && token.epoch>epoch && last_motion_target[0]==20,
                "Actual exit restores advanced source cursor through genuinely new owned native epoch");
            facts.position={0,0,0};facts.on_screen=false;
            service_vehicle_override->position={40,0,0};service_vehicle_override->on_screen=false;
            check(service.set_enter_car_range_multiplier(2),"Production entry controller accepts source multiplier2");
            projected=false;result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_DRIVER,
                enter,projected,[&]{return vehicle_executor(enter);});
            check(result.executed && projected && service.tick_all() && source.state(actor)->current==18,
                "1158 multiplier2 keeps actual40m entry inside the60m range without restoring the route");
            check(service.set_enter_car_range_multiplier(1),"Production range can reset while an entry is pending");
            check(service.tick_all() && source.state(actor)->current==30 &&
                  source.state(actor)->leave_car_deadline==service_now+3000,
                "1158 reset reaches the real30m offscreen far branch and preserves native warp/restore timing");
            service_vehicle_override->position={0,0,0};service_vehicle_override->on_screen=true;facts.on_screen=true;
            projected=false;result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_DRIVER,
                enter,projected,[&]{return vehicle_executor(enter);});
            const auto idle_before=service_idle_calls,still_before=service_still_calls;
            check(result.executed && projected && service.tick_all() && service_still_calls==still_before+1 &&
                  service_idle_calls==idle_before && source.state(actor)->current==18,
                "Warp leave timer reaches MoveState STILL alone without SetIdle or premature route restoration");
            service_now=4000;facts.mission_character=true;facts.source_created_by=2;facts.source_entering_car=false;
            service_vehicle_override->player_disabled=true;service_vehicle_override->getting_in_flags=0xFF;
            NativeCall passenger;passenger.opcode=0x05CA;
            passenger.inputs={actor.handle,42,-1,-1};passenger.mission_context=true;
            const auto entry_count=native_effects;
            projected=false;result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_PASSENGER,
                passenger,projected,[&]{return vehicle_executor(passenger);});
            check(result.executed && projected && native_effects==entry_count+1 &&
                  source.state(actor)->current==17 && source.state(actor)->objective_deadline==18000,
                "Passenger native task starts immediately; original 14 seconds is a recovery deadline");
            service_now=18000;
            check(service.tick_all() && source.state(actor)->objective_deadline==18000,
                "Passenger recovery uses strictly expired deadline, retaining equality");
            service_now=18001;
            check(service.tick_all() && source.state(actor)->objective_deadline==0 &&
                  service.tick_all() && native_effects==entry_count+1,
                "Expired passenger recovery with all doors reserved clears timer once without replaying entry");
            facts.in_vehicle=true;facts.native_state=50;facts.current_vehicle_handle=42;
            facts.current_vehicle_entity=car.entity;facts.current_vehicle_generation=42;pose.on_foot=false;
            check(service.tick_all() && source.state(actor)->current==30,"Real passenger occupancy restores route before next source entry");
            facts.in_vehicle=false;facts.native_state=0;facts.current_vehicle_handle=-1;
            facts.current_vehicle_entity=0;pose.on_foot=true;
            check(service.tick_all(),"Route resumes after actual passenger leaves vehicle");
            service_vehicle_override->getting_in_flags=0;service_vehicle_override->max_passengers=2;
            projected=false;result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_PASSENGER,
                passenger,projected,[&]{return vehicle_executor(passenger);});
            check(result.executed && projected,"New real source passenger setter starts a fresh recovery deadline");
            const auto positions_before=service_entry_positions,entries_before=native_effects;
            service_now+=14001;
            check(service.tick_all() && service_entry_positions==positions_before+1 &&
                  native_effects==entries_before+1 && last_vehicle_opcode==0x05CA && last_passenger_seat==0 &&
                  source.state(actor)->objective_deadline==0,
                "Expired passenger recovery positions at original front door and issues one explicit-seat task");
            check(service.tick_all() && native_effects==entries_before+1 && service_entry_positions==positions_before+1,
                "Completed passenger recovery cannot repeat positioning or task on following frame");
            facts.in_vehicle=true;facts.native_state=50;facts.current_vehicle_handle=42;
            facts.current_vehicle_entity=car.entity;facts.current_vehicle_generation=42;pose.on_foot=false;
            check(service.tick_all(),"Actual occupancy completes standard recovery");
            facts.in_vehicle=false;facts.native_state=0;facts.current_vehicle_handle=-1;
            facts.current_vehicle_entity=0;pose.on_foot=true;
            check(service.tick_all(),"Real exit resumes source route before bus recovery");
            service_vehicle_override->is_bus=true;service_vehicle_override->getting_in_flags=4;
            projected=false;result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_PASSENGER,
                passenger,projected,[&]{return vehicle_executor(passenger);});
            check(result.executed && projected,"Real bus passenger setter keeps its source deadline");
            const auto bus_positions=service_entry_positions,bus_entries=native_effects,bus_doors=service_door_entries;
            service_now+=14001;
            check(service.tick_all() && source.state(actor)->objective_deadline==0 &&
                  native_effects==bus_entries && service_entry_positions==bus_positions+1 &&
                  service_door_entries==bus_doors+1 && service_last_entry_door==RouteEntryDoor::rear_left,
                "Bus recovery preserves the original rear-left door through the actual explicit-door consumer");
            check(service.tick_all() && service_door_entries==bus_doors+1 && service_entry_positions==bus_positions+1,
                "Bus explicit-door recovery does not replay after clearing its source timer");
            facts.in_vehicle=true;facts.native_state=50;facts.current_vehicle_handle=42;
            facts.current_vehicle_entity=car.entity;facts.current_vehicle_generation=42;pose.on_foot=false;
            check(service.tick_all(),"Actual bus occupancy restores source route");
            facts.in_vehicle=false;facts.native_state=0;facts.current_vehicle_handle=-1;
            facts.current_vehicle_entity=0;pose.on_foot=true;
            check(service.tick_all(),"Actual bus exit resumes route before occupied-driver entry");
            facts.native_type=4;facts.created_by=2;facts.source_created_by=2;
            RouteActorVehicleSnapshot driver;driver.actor={0x102,0x234500};driver.native_type=4;
            driver.created_by=2;driver.source_created_by=2;driver.alive=true;driver.in_vehicle=true;driver.native_state=50;
            service_driver_override=driver;
            service_vehicle_override->is_bus=false;service_vehicle_override->driver_handle=driver.actor.handle;
            service_vehicle_override->driver_ped=driver.actor.native_ped;
            projected=false;result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_DRIVER,
                enter,projected,[&]{return vehicle_executor(enter);});
            const auto conversion_entries=native_effects,conversion_clears=service_car_jacked_clears;
            check(result.executed && projected && service.tick_all() && source.state(actor)->current==17 &&
                  source.state(actor)->previous==30 && native_effects==conversion_entries+1 &&
                  last_vehicle_opcode==0x05CA && service_car_jacked_clears==conversion_clears+1,
                "Same-type occupied driver converts original ENTER_DRIVER to passenger and clears actual car-jacked flag");
            check(service.tick_all() && native_effects==conversion_entries+1 && service_car_jacked_clears==conversion_clears+1,
                "Accepted driver-to-passenger conversion does not replay on next source frame");
            service_driver_override.reset();
            facts.in_vehicle=true;facts.native_state=50;facts.current_vehicle_handle=42;
            facts.current_vehicle_entity=car.entity;facts.current_vehicle_generation=42;pose.on_foot=false;
            check(service.tick_all(),"Real passenger occupancy restores route before coordinate objective");
            facts.in_vehicle=false;facts.native_state=0;facts.current_vehicle_handle=-1;
            facts.current_vehicle_entity=0;pose.on_foot=true;facts.position={0,0,0};
            check(service.tick_all(),"Actual exit permits coordinate objective on foot");
            NativeCall coordinate;coordinate.opcode=0x05D3;
            coordinate.inputs={actor.handle,entity_bits(10.f),0,entity_bits(90.f),4,-2};coordinate.mission_context=true;
            projected=false;result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_GOTO_COORD_ON_FOOT,
                coordinate,projected,[&]{return vehicle_executor(coordinate);});
            check(result.executed && projected && source.state(actor)->current==24 &&
                  source.state(actor)->target_position && (*source.state(actor)->target_position)[2]==90.f,
                "Real coordinate objective retains its effective XYZ and suspends source route");
            facts.position={9.49f,0,999};
            check(service.tick_all() && source.state(actor)->current==24,
                "Source position completion refuses XY distance greater than half a metre");
            const auto arrival_epoch=token.epoch;const auto arrival_still=service_still_calls;
            facts.position={9.5f,0,999};
            check(service.tick_all() && source.state(actor)->current==30 && !source.state(actor)->running &&
                  token.epoch>arrival_epoch && service_still_calls==arrival_still+1 && last_motion_target[0]==20,
                "Exact planar half-metre arrival applies STILL then restores live route cursor through new epoch");
            const auto early_entries=native_effects;const auto early_epoch=token.epoch;
            coordinate.inputs[1]=entity_bits(9.6f);coordinate.inputs[3]=entity_bits(999.f);coordinate.inputs[4]=6;
            projected=false;result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_RUN_TO_COORD,
                coordinate,projected,[&]{return vehicle_executor(coordinate);});
            check(result.executed && projected && source.state(actor)->current==30 && source.state(actor)->running &&
                  native_effects==early_entries && token.epoch>early_epoch && last_motion_target[0]==20,
                "Near coordinate setter only changes running on actual route without replacing it with native GOTO");
            RouteActorVehicleSnapshot victim;victim.actor={0x102,0x234500};victim.alive=true;
            victim.native_state=0;victim.health=100;victim.position=facts.position;service_driver_override=victim;
            NativeCall kill;kill.opcode=0x05E2;kill.inputs={actor.handle,victim.actor.handle};kill.mission_context=true;
            projected=false;result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_KILL_CHAR_ON_FOOT,
                kill,projected,[&]{return vehicle_executor(kill);});
            check(result.executed && projected && source.state(actor)->current==8 && service_target_ped_cell,
                "Actual kill objective registers stable weak target before accepting native task");
            service_driver_override->health=0;service_driver_override->alive=false;
            check(service.tick_all() && source.state(actor)->current==8,
                "Zero target health with ordinary native state is not source DyingOrDead completion");
            service_driver_override.reset();
            check(!service.tick_all() && source.state(actor)->current==8,
                "Missing target lookup without cleared weak cell cannot manufacture objective completion");
            service_driver_override=victim;service_driver_override->native_state=54;service_driver_override->alive=false;
            const auto kill_epoch=token.epoch;
            check(service.tick_all() && source.state(actor)->current==30 && token.epoch>kill_epoch &&
                  !service_target_ped_cell && last_motion_target[0]==20,
                "Exact native DIE state restores retained source route and releases registered ped target");
            service_driver_override=victim;
            projected=false;result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_KILL_CHAR_ANY_MEANS,
                kill,projected,[&]{return vehicle_executor(kill);});
            service_driver_override->native_state=55;service_driver_override->alive=false;
            check(result.executed && projected && service.tick_all() && source.state(actor)->current==30,
                "Nearby mission ANY_MEANS reaches original shared on-foot DEAD completion branch");
            service_driver_override=victim;
            projected=false;result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_KILL_CHAR_ANY_MEANS,
                kill,projected,[&]{return vehicle_executor(kill);});
            service_driver_override->native_state=55;service_driver_override->alive=false;
            service_driver_override->position[0]=facts.position[0]+31.f;
            service_vehicle_override.reset(); // No live remembered-car backend in this refusal case.
            facts.kinda_stay_in_same_place=false;
            check(result.executed && projected && !service.tick_all() && source.state(actor)->current==9,
                "Far nonstationary ANY_MEANS death cannot skip original vehicle search");
            facts.kinda_stay_in_same_place.reset();
            check(!service.tick_all() && source.state(actor)->current==9,
                "Missing stationary fact cannot bypass original far-target branch");
            facts.kinda_stay_in_same_place=true;
            facts.in_vehicle=true;facts.native_state=50;facts.current_vehicle_handle=42;
            facts.current_vehicle_entity=car.entity;facts.current_vehicle_generation=42;pose.on_foot=false;
            check(!service.tick_all() && source.state(actor)->current==9,
                "Stationary flag never bypasses earlier ANY_MEANS in-vehicle branch");
            facts.in_vehicle=false;facts.native_state=0;facts.current_vehicle_handle=-1;
            facts.current_vehicle_entity=0;pose.on_foot=true;
            const auto stationary_epoch=token.epoch;
            check(service.tick_all() && source.state(actor)->current==30 && token.epoch>stationary_epoch &&
                  !service_target_ped_cell && last_motion_target[0]==20,
                "Far stationary ANY_MEANS DEAD target restores actual retained route without vehicle search");
            facts.kinda_stay_in_same_place.reset();
            service_driver_override=victim;
            projected=false;result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_KILL_CHAR_ANY_MEANS,
                kill,projected,[&]{return vehicle_executor(kill);});
            check(result.executed && projected && service_target_ped_cell,"New ANY_MEANS owns its actual ped reference");
            resolve_service_ped_references();service_driver_override->actor.handle++;
            check(service.tick_all() && source.state(actor)->current==30 && !service_target_ped_cell,
                "Native reference clearing restores route without following a recycled target pool slot");
            service_driver_override.reset();
            service_vehicle_override=car;
            NativeCall destroy;destroy.opcode=0x0672;destroy.inputs={actor.handle,car.handle};destroy.mission_context=true;
            projected=false;result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_DESTROY_CAR,
                destroy,projected,[&]{return vehicle_executor(destroy);});
            check(result.executed && projected && source.state(actor)->current==22 && service_target_vehicle_cell,
                "Destroy-car objective owns its original vehicle reference in production service");
            service_vehicle_override->health=0;service_vehicle_override->alive=false;
            check(service.tick_all() && source.state(actor)->current==22,
                "Destroy-car zero health is only SCM completion latch and cannot restore source route");
            *service_target_vehicle_cell=0;service_vehicle_override.reset();
            check(service.tick_all() && source.state(actor)->current==30 && !service_target_vehicle_cell,
                "Actual native vehicle reference clearing completes destroy objective and restores route");
            facts.created_by=1;facts.source_created_by=1;facts.mission_character=false;facts.position={0,0,0};
            coordinate.inputs[1]=entity_bits(10.f);coordinate.inputs[3]=0;coordinate.inputs[4]=4;
            projected=false;result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_GOTO_COORD_ON_FOOT,
                coordinate,projected,[&]{return vehicle_executor(coordinate);});
            facts.position={10,0,0};
            check(result.executed && projected && !service.tick_all() && source.state(actor)->current==24 &&
                  service.diagnostic().find("original wander transition")!=std::string::npos,
                "Random-character footer cannot be replaced with mission-ped route restoration");
            // Projection-only regression: this executor fixture does not
            // approve or enable the still-unimplemented native plain flee.
            NativeCall plain_flee;plain_flee.inputs={actor.handle};
            projected=false;
            result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_FLEE_ON_FOOT_TILL_SAFE,
                plain_flee,projected,[]{NativeResult accepted;accepted.executed=accepted.native_entered=true;return accepted;});
            check(result.executed && projected && source.state(actor)->current==3 && source.state(actor)->previous==30 &&
                  !source.state(actor)->target.valid() && source.state(actor)->objective_deadline==0,
                "Plain FLEE projects original objective3, distinct from entity FLEE_CHAR objective10");
        }
        {
            OriginalRouteIntents source;
            RouteActorVehicleSnapshot ped;ped.actor=actor;ped.alive=ped.controllable=true;ped.native_state=0;
            service_actor_override=ped;pose={actor,{0,0,0},true,true,true};motion_active=true;
            OwnedRouteReaders readers;readers.actor=[](uint32_t native_ped){
                return native_ped==actor.native_ped?std::optional<ObjectiveActor>(actor):std::nullopt;
            };
            SaRouteService service(source,table,std::move(readers),
                [](int32_t handle){return handle==actor.handle?std::optional<ObjectiveActor>(actor):std::nullopt;},
                [](ObjectiveActor){return std::optional<int32_t>(0);},[](int32_t){},
                [](const NativeCall&){return NativeResult{};});
            check(service.constructed(actor) && service.follow_route(actor.handle,7,1,true)==StepResult::continued &&
                  service_control!=nullptr && last_motion_target==std::array<float,3>{10,0,0},
                "Production mode1 route binds its owned ControlSubTask at the first seek point");
            const auto writes_after_assignment=writes,retargets_before_advance=service_retarget_calls;
            pose.position={10,0,0};
            service_control(service_control_context,actor,token.binding.record.task,actor.native_ped);
            check(writes==writes_after_assignment && service_retarget_calls==retargets_before_advance+1 &&
                  last_motion_target==std::array<float,3>{20,0,0},
                "Production ControlSubTask advances to the final mode1 route point");
            const auto writes_at_final=writes,retargets_at_final=service_retarget_calls;
            const auto final_binding=token.binding;const auto final_epoch=token.epoch;
            pose.position={20,0,0};
            service_control(service_control_context,actor,token.binding.record.task,actor.native_ped);
            check(service_control==nullptr && writes==writes_at_final && service_retarget_calls==retargets_at_final &&
                  token.epoch==final_epoch && token.binding.slot==final_binding.slot &&
                  token.binding.record==final_binding.record &&
                  source.state(actor)->current==OriginalRouteIntents::follow_route,
                "Reaching the final mode1 point unbinds route control without replay/retarget or changing FOLLOW_ROUTE");
            check(service.tick_all() && service.tick_all() && writes==writes_at_final &&
                  service_retarget_calls==retargets_at_final && source.state(actor)->current==OriginalRouteIntents::follow_route &&
                  service_control==nullptr && token.epoch==final_epoch && token.binding.record==final_binding.record &&
                  service.diagnostic().empty(),
                "Ended production route remains stable across frame ticks without quarantining the service");
        }
        service_actor_override.reset();service_vehicle_override.reset();service_fixture=false;
    }
    {
        service_fixture=true;service_now=100;service_vehicle_lookup_unavailable=false;
        pose={actor,{0,0,0},true,true,true};motion_active=true;
        RouteActorVehicleSnapshot ped;ped.actor=actor;ped.alive=ped.controllable=true;ped.native_state=0;
        service_actor_override=ped;
        RouteVehicleSnapshot car;car.handle=42;car.entity=0x987600;car.generation=42;
        car.alive=true;car.health=1000;car.is_bus=false;car.player_disabled=false;car.upside_down=false;
        service_vehicle_override=car;
        OriginalRouteIntents source;OwnedRouteReaders readers;
        readers.actor=[](uint32_t handle){return handle==actor.native_ped?std::optional<ObjectiveActor>(actor):std::nullopt;};
        // The real receipt observer (ScriptTaskObserver) fed with the hook choreography of SA: Set(event) when the
        // command is accepted, AssociateWithTask when the ped's AI turns the event into its task, task release.
        struct WalkBank:TaskObserverReader {
            ScriptTaskSnapshot bank{};bool live=true;uint32_t event_ptr=0,task_ptr=0;
            bool healthy()const override{return live;}
            bool record(size_t slot,ScriptTaskRecord& out)override{if(slot>=128)return false;out=bank[slot];return true;}
            bool event_task(uint32_t event,uint32_t& out)override{if(event!=event_ptr)return false;out=task_ptr;return true;}
        } walk;
        ScriptTaskObserver observer(walk);
        int issued=0,executor_calls=0;const size_t walk_slot=12;uint16_t walk_opcode=0;
        const auto walk_address=[&]{return uint32_t(0xC0B1E8+walk_slot*0x14);};
        const auto execute=[&](const NativeCall& call) {
            ++executor_calls;NativeResult effect;effect.executed=effect.native_entered=true;
            if(call.opcode==0x05D3 || call.opcode==0x05D9) {
                ++issued;walk.event_ptr=0x5100+0x10*issued;walk.task_ptr=0xD000+0x80*issued;walk_opcode=call.opcode;
                const auto hook=observer.begin(2,walk_address(),walk.event_ptr); // CPedScriptedTaskRecordData::Set(event)
                walk.bank[walk_slot]={call.opcode,walk.event_ptr,0,uint32_t(actor.native_ped),0};
                observer.end(hook,true);
                effect.task_capture=NativeResult::TaskCapture::captured;
                effect.accepted_script_task=CapturedScriptTask{walk_slot,walk.bank[walk_slot]};
            }
            return effect;
        };
        const auto associate=[&]{ // the ped's event response: AssociateWithTask(task)
            const auto hook=observer.begin(7,walk_address(),walk.task_ptr);
            walk.bank[walk_slot]={walk_opcode,0,walk.task_ptr,uint32_t(actor.native_ped),1};
            observer.end(hook,true);
        };
        const auto release_task=[&]{observer.begin(8,walk.task_ptr,0);}; // native task release, before pool reuse
        SaRouteService service(source,table,std::move(readers),
            [](int32_t handle){return handle==actor.handle?std::optional<ObjectiveActor>(actor):std::nullopt;},
            [](ObjectiveActor){return original_cop_state(service_actor_override->native_state);},[](int32_t){},execute);
        service.set_walking_receipts([&](const CapturedScriptTask& c){return observer.accept(c);},
            [&](const EpochScriptTask& t){return observer.refresh(t);},
            [&](const EpochScriptTask& t){return observer.released(t);},execute);
        observer.set_release_probe(service.walking_release_probe());
        check(service.constructed(actor) && service.objective_passed(actor.handle)==std::optional<bool>(false),
            "A constructed ped with no source objective starts with a false completion latch");
        NativeCall coordinate;coordinate.opcode=0x05D3;
        coordinate.inputs={actor.handle,entity_bits(10.f),entity_bits(20.f),entity_bits(0.f),4,-2};
        bool projected=false;
        auto result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_GOTO_COORD_ON_FOOT,
            coordinate,projected,[&]{return execute(coordinate);});
        check(result.executed && projected && service.objective_passed(actor.handle)==std::optional<bool>(false),
            "An accepted source coordinate goal resets the script completion latch");
        service_actor_override->position={10.5f,20,300};
        check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(false),
            "Standing on the target while the accepted command is still its queued event cannot pass the objective");
        associate();
        check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(true),
            "Event -> associated task -> active -> exact planar arrival latches completion through the production receipt");
        service_actor_override->position={90,90,0};motion_active=false;
        check(service.objective_passed(actor.handle)==std::optional<bool>(true),
            "A passed script objective stays latched after its native task ends");

        coordinate.inputs[1]=entity_bits(40.f);coordinate.inputs[2]=entity_bits(50.f);
        motion_active=true;projected=false;
        result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_RUN_TO_COORD,
            coordinate,projected,[&]{return execute(coordinate);});
        check(result.executed && projected && service.objective_passed(actor.handle)==std::optional<bool>(false),
            "A replacement source objective clears the old completion latch");
        associate();
        service_actor_override->position={39,50,0};
        check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(false),
            "A walking task still 1m away has not completed the objective");
        {   // a foreign writer replaces the bank slot (another script command, flush or mod): ownership is gone
            const auto hook=observer.begin(2,walk_address(),0x7777);
            walk.bank[walk_slot]={0x05D3,0x7777,0,uint32_t(actor.native_ped),0};observer.end(hook,true);
            service_actor_override->position={40,50,0};
            check(service.tick_all() && !service.objective_passed(actor.handle).has_value(),
                "Matching coordinates cannot pass after the accepted receipt was replaced by another bank writer");
        }
        // reissue (a different goal: an identical repeated goal is ignored by the source setter), associate, and
        // verify that an allocated-but-inactive task is not mistaken for a released task.
        coordinate.inputs[1]=entity_bits(41.f);projected=false;
        result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_RUN_TO_COORD,
            coordinate,projected,[&]{return execute(coordinate);});
        associate();service_actor_override->position={41.6f,50,0};
        check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(false),
            "The reissued objective is incomplete while the ped is still 0.6m from the target");
        service_actor_override->position={41.5f,50,0};motion_active=false;
        check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(false),
            "An allocated walking task absent from the active chain remains incomplete even inside the arrival radius");
        motion_active=true;service_actor_override->position={41.6f,50,0};
        check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(false),
            "An active task sampled 0.6m away still has not completed");
        const int issued_before_release=issued;
        release_task();service_actor_override->position={100,50,0};
        check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(false) && issued==issued_before_release+1,
            "A task SA released 0.6m from the target (probed at the release moment) is re-armed, never TRUE, even after a teleport");
        associate();
        check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(false),
            "The re-armed task is a fresh observed receipt that is far from the target: incomplete");
        service_actor_override->position={41.2f,50,0};
        check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(true),
            "The re-armed active task completes when it really reaches the target");
        // reissue, associate far from the target, then SA releases the task: no TRUE is invented
        coordinate.inputs[1]=entity_bits(42.f);projected=false;
        result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_RUN_TO_COORD,
            coordinate,projected,[&]{return execute(coordinate);});
        associate();service_actor_override->position={20,50,0};
        check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(false),"Far from the target: incomplete");
        const int issued_far=issued;
        release_task();service_actor_override->position={42,50,0};
        check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(false) && issued==issued_far+1,
            "A task released while the ped was far away cannot be turned into completion by a later position (re-armed)");
        walk.live=false;
        coordinate.inputs[1]=entity_bits(43.f);projected=false;
        result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_RUN_TO_COORD,coordinate,projected,[&]{return execute(coordinate);});
        check(service.tick_all() && !service.objective_passed(actor.handle).has_value(),
            "An unhealthy observer yields unavailable, never TRUE");
        walk.live=true;
        coordinate.inputs[1]=entity_bits(44.f);projected=false;
        result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_RUN_TO_COORD,coordinate,projected,[&]{return execute(coordinate);});
        associate();service_actor_override->position={44,50,0};motion_active=true;
        check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(true),
            "A fresh receipt after the observer recovered completes at the target");

        {   // ---- completion that happens between two script samples ----------------------------------------------------
            // Position goal: SA walks the last metres and frees the finished task inside one frame; the script never sees
            // the ped near the target while the task is active. The release notification carries the verdict.
            service_actor_override->in_vehicle=false;service_actor_override->native_state=0;motion_active=true;
            service_actor_override->position={20,50,0};
            coordinate.inputs[1]=entity_bits(60.f);coordinate.inputs[2]=entity_bits(50.f);projected=false;
            result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_RUN_TO_COORD,coordinate,projected,[&]{return execute(coordinate);});
            associate();
            check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(false),"Far and active: incomplete");
            service_actor_override->position={60.2f,50,0};release_task();       // arrives and SA frees the task
            service_actor_override->position={90,50,0};                          // something moves the ped before the next script sample
            check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(true),
                "A walk SA finished at the target between two samples is completed by the observed release evidence");
            check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(true),
                "The completion stays latched");
            // The release evidence is bound to its receipt: a new command on the same bank slot starts clean.
            coordinate.inputs[1]=entity_bits(61.f);projected=false;
            service_actor_override->position={0,0,0};
            result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_RUN_TO_COORD,coordinate,projected,[&]{return execute(coordinate);});
            associate();
            check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(false),
                "A recycled slot never inherits the previous receipt's release evidence");
            // Released near but not on the target (0.7m): not a completion, re-armed
            service_actor_override->position={60.3f,50,0};const int before=issued;release_task();
            service_actor_override->position={61.0f,50,0};
            check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(false) && issued==before+1,
                "A release 0.7m from the target is not completion");
            // Re-arm budget: a task that SA keeps freeing away from the target is finally reported unknown, never TRUE
            int failures=0;
            for(int i=0;i<12;++i) {
                associate();service_now+=1100;service_actor_override->position={10,50,0};
                release_task();
                if(!(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(false))){++failures;break;}
            }
            check(failures==1 && !service.objective_passed(actor.handle).has_value(),
                "After the bounded number of re-arms a task that never reaches the target is unknown, not TRUE or an endless replay");

            // GOTO_CHAR: SA frees the seek at the target before the 600ms source timer elapsed (target in a car: 3m).
            service_actor_override->position={0,0,0};
            RouteActorVehicleSnapshot buddy;buddy.actor={0x1A1,0x3A1000};buddy.alive=true;buddy.native_state=0;buddy.health=100;
            buddy.position={0,0,0};service_driver_override=buddy;
            NativeCall chase;chase.opcode=0x05D9;chase.inputs={actor.handle,buddy.actor.handle};projected=false;
            result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_GOTO_CHAR_ON_FOOT,chase,projected,[&]{return execute(chase);});
            associate();service_actor_override->position={0,1.5f,0};
            check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(false),"Inside 2m before the timer: not yet");
            release_task();
            check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(false),
                "Released at the target before the 600ms timer: completion waits for the timer");
            service_now+=700;
            check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(true),
                "Once the timer elapsed with the ped still at the target the early release completes");
            // same, but the ped is carried away before the timer elapses: re-armed instead
            projected=false;chase.inputs[1]=buddy.actor.handle;
            service_actor_override->position={0,50,0};
            RouteActorVehicleSnapshot buddy2=buddy;buddy2.actor={0x1A2,0x3A2000};service_driver_override=buddy2;
            chase.inputs[1]=buddy2.actor.handle;
            result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_GOTO_CHAR_ON_FOOT,chase,projected,[&]{return execute(chase);});
            associate();service_actor_override->position={0,1.0f,0};release_task();
            service_actor_override->position={0,40,0};
            const int before_chase=issued;service_now+=700;
            check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(false) && issued==before_chase+1,
                "A ped that left the target before the timer elapsed did not complete GOTO_CHAR; the task is re-armed");
            service_driver_override.reset();
        }

        RouteActorVehicleSnapshot victim;victim.actor={0x102,0x234500};victim.alive=true;
        victim.native_state=0;victim.health=100;victim.position={25,50,0};service_driver_override=victim;
        NativeCall kill;kill.opcode=0x05E2;kill.inputs={actor.handle,victim.actor.handle};
        projected=false;
        result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_KILL_CHAR_ANY_MEANS,
            kill,projected,[&]{return execute(kill);});
        check(result.executed && projected && service.objective_passed(actor.handle)==std::optional<bool>(false),
            "A live exact target keeps the accepted ANY_MEANS script objective incomplete");
        NativeCall self_goto;self_goto.opcode=0x05D9;self_goto.inputs={actor.handle,actor.handle};
        const auto calls_before_ignored=executor_calls;projected=false;
        result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_GOTO_CHAR_ON_FOOT,
            self_goto,projected,[&]{return execute(self_goto);});
        check(result.executed && projected && executor_calls==calls_before_ignored &&
              service.objective_passed(actor.handle)==std::optional<bool>(false),
            "An ignored self-target GOTO resets the latch but preserves the active KILL target and cannot pass at zero distance");
        service_driver_override->native_state=55;service_driver_override->alive=false;
        check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(true),
            "The preserved KILL objective latches only after its exact target enters the source dying state");

        // Source writers of the script latch never run for wait/flee objectives.
        const auto accepted=[]{NativeResult accepted;accepted.executed=accepted.native_entered=true;return accepted;};
        NativeCall wait;wait.inputs={actor.handle};projected=false;
        result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_WAIT_ON_FOOT,wait,projected,accepted);
        check(result.executed && projected && source.state(actor)->current==1 &&
              service.objective_passed(actor.handle)==std::optional<bool>(false) && service.tick_all() &&
              service.objective_passed(actor.handle)==std::optional<bool>(false),
            "WAIT_ON_FOOT resets the latch and owns a known FALSE because no source path can publish it");
        NativeCall flee_char;flee_char.inputs={actor.handle,victim.actor.handle};projected=false;
        result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_FLEE_CHAR_ON_FOOT_ALWAYS,flee_char,projected,accepted);
        check(result.executed && projected && service.tick_all() &&
              service.objective_passed(actor.handle)==std::optional<bool>(false),
            "FLEE_CHAR_ON_FOOT_ALWAYS also owns a known FALSE latch");
        // GOTO_CHAR / WALK_TO_CHAR: the accepted SA 05D9 command is turned into its task by the observed association, the
        // target is an exact weak ped reference and the source 3D distance (2m on foot, 3m seated) decides completion.
        service_driver_override->native_state=0;service_driver_override->alive=true;
        service_driver_override->position={25,50,0};service_actor_override->position={0,0,0};motion_active=true;
        NativeCall goto_char;goto_char.opcode=0x05D9;goto_char.inputs={actor.handle,victim.actor.handle};projected=false;
        result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_GOTO_CHAR_ON_FOOT,goto_char,projected,
            [&]{return execute(goto_char);});
        check(result.executed && projected && source.state(actor)->current==12 && result.accepted_script_task &&
              result.accepted_script_task->record.opcode==0x05D9 && result.accepted_script_task->record.event &&
              service.objective_passed(actor.handle)==std::optional<bool>(false) && service.tick_all() &&
              service.objective_passed(actor.handle)==std::optional<bool>(false),
            "GOTO_CHAR with its queued event and a far live target is a known FALSE after the frame consumer");
        service_driver_override->position={0,1.9f,0};
        check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(false),
            "A target inside 2m while the command is still a queued event has not been walked to yet");
        associate();
        service_driver_override->position={0,2.5f,0};
        check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(false),
            "Beyond the source 2m on-foot distance the associated GOTO_CHAR task stays incomplete");
        service_driver_override->position={0,1.9f,0};
        check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(false),
            "Within 2m but before the source 600ms pacing timer (m_nPedStateTimer) GOTO_CHAR has not completed");
        service_now+=700;
        check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(true),
            "Within 2m of the live target while the observed task is active and the timer elapsed, GOTO_CHAR completes");
        motion_active=false;service_driver_override->position={90,90,0};release_task();
        check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(true),
            "The passed GOTO_CHAR objective stays latched after its native task ends");
        motion_active=true;

        NativeCall walk_to_char;walk_to_char.opcode=0x05D9;walk_to_char.inputs={actor.handle,victim.actor.handle};projected=false;
        service_driver_override->position={25,50,0};
        result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_WALK_TO_CHAR,walk_to_char,projected,
            [&]{return execute(walk_to_char);});
        check(result.executed && projected && source.state(actor)->current==13 &&
              service.objective_passed(actor.handle)==std::optional<bool>(false),"WALK_TO_CHAR resets the latch");
        associate();
        {   const auto hook=observer.begin(2,walk_address(),0x7778); // another bank writer replaces the receipt
            walk.bank[walk_slot]={0x05D9,0x7778,0,uint32_t(actor.native_ped),0};observer.end(hook,true);}
        service_driver_override->position={0,1.0f,0};
        check(service.tick_all() && !service.objective_passed(actor.handle).has_value(),
            "A nearby target cannot complete WALK_TO_CHAR once the accepted receipt was replaced");
        projected=false;
        result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_GOTO_CHAR_ON_FOOT,goto_char,projected,[&]{return execute(goto_char);});
        associate();service_now+=700;
        check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(true),
            "A fresh observed GOTO_CHAR task (the identical WALK_TO_CHAR would be ignored by the source setter) completes at the same distance");

        // A lost receipt is not a release proof. The former last-distance fallback could publish TRUE from a stale
        // 2.5m sample after the target moved/died, or after this ped entered a vehicle.
        const auto begin_near_goto=[&](ObjectiveActor target_actor) {
            service_driver_override=RouteActorVehicleSnapshot{};
            service_driver_override->actor=target_actor;service_driver_override->alive=true;
            service_driver_override->native_state=0;service_driver_override->health=100;
            service_driver_override->position={0,0,0};
            service_actor_override->in_vehicle=false;service_actor_override->position={0,0,0};
            motion_active=true;goto_char.inputs[1]=target_actor.handle;projected=false;
            const auto started=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_GOTO_CHAR_ON_FOOT,
                goto_char,projected,[&]{return execute(goto_char);});
            associate();service_actor_override->position={0,2.5f,0};
            check(started.executed && projected && service.tick_all() &&
                  service.objective_passed(actor.handle)==std::optional<bool>(false),
                "A fresh GOTO_CHAR receipt at 2.5m is still active and incomplete");
        };
        begin_near_goto({0x103,0x345600});
        const int issued_goto=issued;
        service_driver_override->position={0,100,0};release_task();
        check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(false) && issued==issued_goto+1,
            "A task released after the target moved away is re-armed, never completed from a stale 2.5m sample");
        begin_near_goto({0x104,0x346700});
        service_driver_override->alive=false;release_task();
        check(service.tick_all() && !service.objective_passed(actor.handle).has_value(),
            "A dead target prevents stale GOTO_CHAR proximity from publishing TRUE after receipt loss");
        begin_near_goto({0x105,0x347800});
        service_actor_override->in_vehicle=true;service_actor_override->native_state=50;
        service_actor_override->current_vehicle_handle=42;service_actor_override->current_vehicle_entity=car.entity;
        service_actor_override->current_vehicle_generation=car.generation;release_task();
        check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(false),
            "Entering a vehicle makes GOTO_CHAR false even when the walking receipt is lost");
        service_actor_override->in_vehicle=false;service_actor_override->native_state=0;
        service_actor_override->current_vehicle_handle=-1;service_actor_override->current_vehicle_entity=0;
        service_actor_override->current_vehicle_generation=0;motion_active=true;

        check(service.follow_route(actor.handle,7,1,true)==StepResult::continued &&
              service.objective_passed(actor.handle)==std::optional<bool>(false) && service.tick_all() &&
              !service.objective_passed(actor.handle).has_value(),
            "FOLLOW_ROUTE exposes its reset FALSE for this frame, then leaves route completion unavailable");
        NativeCall enter;enter.opcode=0x05CB;enter.inputs={actor.handle,42,-1};projected=false;
        result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_DRIVER,
            enter,projected,[&]{return execute(enter);});
        check(result.executed && projected && service.objective_passed(actor.handle)==std::optional<bool>(false),
            "ENTER_DRIVER owns a fresh false latch before occupancy");
        service_actor_override->in_vehicle=true;service_actor_override->native_state=50;
        service_actor_override->current_vehicle_handle=42;service_actor_override->current_vehicle_entity=car.entity;
        service_actor_override->current_vehicle_generation=car.generation;
        check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(true),
            "Exact occupancy of the accepted vehicle completes ENTER_DRIVER");

        service_actor_override->in_vehicle=false;service_actor_override->native_state=0;
        service_actor_override->current_vehicle_handle=-1;service_actor_override->current_vehicle_entity=0;
        service_actor_override->current_vehicle_generation=0;
        NativeCall destroy;destroy.opcode=0x0672;destroy.inputs={actor.handle,42};projected=false;
        result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_DESTROY_CAR,
            destroy,projected,[&]{return execute(destroy);});
        check(result.executed && projected && service.objective_passed(actor.handle)==std::optional<bool>(false),
            "DESTROY_CAR resets completion while the accepted target remains intact");
        service_vehicle_override->health=0;service_vehicle_override->alive=false;
        check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(true),
            "A wrecked live target with the same accepted native identity completes DESTROY_CAR");
        check(service.follow_route(actor.handle,7,1,true)==StepResult::continued &&
              service.objective_passed(actor.handle)==std::optional<bool>(false),
            "The original pre-SetObjective reset also clears a completed latch when FOLLOW_ROUTE is ignored");

        // GET_RANDOM_CHAR_IN_AREA can hand the mission a live ambient ped that
        // this runtime did not create. Acquiring it establishes only a shadow
        // identity; the latch stays unknown until its accepted source setter
        // supplies the same event->task evidence used for created actors.
        service.retired(actor);
        service_driver_override.reset();
        service_actor_override=RouteActorVehicleSnapshot{};
        service_actor_override->actor=actor;service_actor_override->alive=true;
        service_actor_override->native_state=0;service_actor_override->position={0,0,0};
        check(service.acquired(actor,uint8_t(2)) && source.state(actor) &&
              source.state(actor)->source_created_by==std::optional<uint8_t>(2) &&
              !service.objective_passed(actor.handle).has_value(),
            "Acquiring a promoted live ambient actor records its proven MISSION_CHAR origin but keeps its old completion latch unknown");
        const auto acquired_revision=source.state(actor)->revision;
        check(service.acquired(actor,uint8_t(2)) && source.state(actor)->revision==acquired_revision,
            "Repeated acquisition preserves the existing actor generation and source revision");
        coordinate.inputs={actor.handle,entity_bits(10.f),entity_bits(20.f),entity_bits(0.f),4,-2};
        projected=false;
        result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_GOTO_COORD_ON_FOOT,
            coordinate,projected,[&]{return execute(coordinate);});
        check(result.executed && projected && source.state(actor)->current==24 &&
              service.objective_passed(actor.handle)==std::optional<bool>(false),
            "An acquired ambient actor's accepted GOTO_COORD installs its source completion watch");
        service_actor_override->position={10.f,20.f,250.f};
        check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(false),
            "An ambient actor at the destination remains incomplete while its exact task is still a queued event");
        associate();
        check(service.tick_all() && service.objective_passed(actor.handle)==std::optional<bool>(true),
            "The accepted ambient GOTO_COORD completes only after its associated task reaches the original planar radius");
        service.clear();service_driver_override.reset();service_actor_override.reset();service_vehicle_override.reset();
        motion_active=true;service_fixture=false;
    }
    for(int remembered_case:{0,1,2,3}) {
        service_fixture=true;service_now=100;pose={actor,{0,0,0},true,false,true};
        RouteActorVehicleSnapshot ped;ped.actor=actor;ped.alive=ped.controllable=true;
        ped.native_state=50;ped.in_vehicle=true;ped.kinda_stay_in_same_place=false;
        ped.current_vehicle_handle=42;ped.current_vehicle_entity=0x987600;ped.current_vehicle_generation=42;
        service_actor_override=ped;
        RouteVehicleSnapshot car;car.handle=42;car.entity=0x987600;car.generation=42;
        car.alive=true;car.health=1000;car.is_bus=false;car.player_disabled=false;car.upside_down=false;service_vehicle_override=car;
        RouteActorVehicleSnapshot target;target.actor={0x102,0x234500};target.alive=true;
        target.position={31,0,0};service_driver_override=target;
        service_kill_entry_stops=0;service_kill_entry_stop_failure=false;
        {
            OriginalRouteIntents source;OwnedRouteReaders readers;int entries=0;
            readers.actor=[](uint32_t ped){return ped==actor.native_ped?std::optional<ObjectiveActor>(actor):std::nullopt;};
            const auto execute=[&](const NativeCall& call) {
                if(call.opcode==0x05CB) {
                    check(service_kill_entry_stops==1,"Source cruise-only stop precedes real remembered-car driver entry");++entries;
                }
                NativeResult result;result.executed=result.native_entered=true;return result;
            };
            SaRouteService service(source,table,std::move(readers),
                [](int32_t handle){return handle==actor.handle?std::optional<ObjectiveActor>(actor):std::nullopt;},
                [](ObjectiveActor){return original_cop_state(service_actor_override->native_state);},[](int32_t){},execute);
            check(service.constructed(actor,uint8_t(2)) && service.follow_route(actor.handle,7,1,true)==StepResult::continued,
                "Actual in-car route admission observes source m_pMyVehicle through a native registered weak cell");
            auto* remembered_cell=service_target_vehicle_cell;
            check(remembered_cell && *remembered_cell==car.entity,"Remembered vehicle comes from actual occupancy, not an on-foot pointer");
            auto& facts=*service_actor_override;facts.in_vehicle=false;facts.native_state=0;pose.on_foot=true;
            // Deliberately leave native last-car-looking fields populated: they
            // cannot recreate source authority after a real new constructor.
            if(remembered_case==3) {
                check(service.constructed(actor,uint8_t(2)) && service.follow_route(actor.handle,7,1,true)==StepResult::continued,
                    "New source generation retires old remembered vehicle despite identical native handle/address");
            }
            check(service.tick_all(),"Actual on-foot state permits pending owned route motion");
            NativeCall kill;kill.opcode=0x05E2;kill.inputs={actor.handle,target.actor.handle};
            bool projected=false;
            const auto result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_KILL_CHAR_ANY_MEANS,
                kill,projected,[&]{return execute(kill);});
            check(result.executed && projected,"Far source ANY_MEANS suspends actual route with registered ped target");
            if(remembered_case==1)*remembered_cell=0;
            if(remembered_case==2)service_kill_entry_stop_failure=true;
            if(remembered_case==0) {
                service_driver_override->native_state=55;service_driver_override->alive=false;
            }
            const auto epoch=token.epoch;
            const bool accepted=service.tick_all();
            if(remembered_case==0) {
                check(accepted && entries==1 && service_kill_entry_stops==1 && source.state(actor)->current==18 &&
                      source.state(actor)->previous==9 && token.epoch==epoch &&
                      service.objective_passed(actor.handle)==std::optional<bool>(true),
                    "Far on-foot ANY_MEANS zeroes actual remembered-car cruise then enters driver with exact previous9, without route resurrection");
                check(service.tick_all() && entries==1 && service_kill_entry_stops==1,"Accepted remembered-car entry is not replayed");
            } else if(remembered_case==2) {
                check(!accepted && !source.state(actor) && !entries && service_kill_entry_stops==1 && service.tick_all(),
                    "Entered cruise-only failure retires source authority without entry or prefix replay");
            } else {
                check(!accepted && source.state(actor)->current==9 && !entries && !service_kill_entry_stops,
                    "Cleared reference or new constructor cannot guess a remembered vehicle from native on-foot fields");
            }
        }
        service_kill_entry_stop_failure=false;
        service_actor_override.reset();service_vehicle_override.reset();service_driver_override.reset();service_fixture=false;
    }
    for(int failure:{0,2,3,4}) {
        service_fixture=true;service_now=100;pose={actor,{0,0,0},true,true,true};
        RouteActorVehicleSnapshot ped;ped.actor=actor;ped.alive=ped.controllable=true;
        ped.on_screen=true;service_actor_override=ped;
        RouteActorVehicleSnapshot victim;victim.actor={0x102,0x234500};
        victim.alive=true;victim.native_state=0;victim.position={1,0,0};service_driver_override=victim;
        RouteVehicleSnapshot car;car.handle=42;car.entity=0x987600;car.generation=42;
        car.alive=true;car.health=1000;car.is_bus=false;car.player_disabled=false;
        car.speed_magnitude_sq_3d=0.f;service_vehicle_override=car;
        service_kill_car_stop_failure=0;service_kill_car_stops=0;
        {
            OriginalRouteIntents source;OwnedRouteReaders readers;int leaves=0;
            readers.actor=[](uint32_t ped){return ped==actor.native_ped?std::optional<ObjectiveActor>(actor):std::nullopt;};
            const auto execute=[&](const NativeCall& call) {
                NativeResult result;
                if(call.opcode==0x05CD) {
                    check(service_kill_car_stops==1,"Actual car stop precedes ANY_MEANS native LEAVE assignment");
                    ++leaves;if(failure==3)return result;
                }
                result.executed=result.native_entered=true;return result;
            };
            SaRouteService service(source,table,std::move(readers),
                [](int32_t handle){return handle==actor.handle?std::optional<ObjectiveActor>(actor):std::nullopt;},
                [](ObjectiveActor){return original_cop_state(service_actor_override->native_state);},[](int32_t){},execute);
            check(service.constructed(actor,uint8_t(2)) && service.follow_route(actor.handle,7,1,true)==StepResult::continued,
                "Vehicle ANY_MEANS fixture starts through real accepted source route");
            NativeCall kill;kill.opcode=0x05E2;kill.inputs={actor.handle,victim.actor.handle};
            bool projected=false;
            const auto result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_KILL_CHAR_ANY_MEANS,
                kill,projected,[&]{return execute(kill);});
            check(result.executed && projected && service_target_ped_cell,"Vehicle ANY_MEANS owns actual original target reference");
            auto& facts=*service_actor_override;facts.in_vehicle=true;facts.native_state=50;
            facts.current_vehicle_handle=42;facts.current_vehicle_entity=car.entity;facts.current_vehicle_generation=42;
            pose.on_foot=false;
            service_vehicle_override->speed_magnitude_sq_3d=0.0004f;
            check(!service.tick_all() && service_kill_car_stops==0 && leaves==0 && source.state(actor)->current==9,
                "Exact source speed boundary does not stop a moving ANY_MEANS car");
            service_vehicle_override->speed_magnitude_sq_3d=0.f;
            service_driver_override->position={20,0,0};
            check(!service.tick_all() && service_kill_car_stops==0 && leaves==0,
                "Exact twenty-metre boundary keeps original ANY_MEANS car chase branch");
            service_driver_override->position={1,0,0};service_driver_override->in_vehicle=true;
            check(!service.tick_all() && service_kill_car_stops==0 && leaves==0,
                "Target-car exit permission is never invented from target proximity");
            service_driver_override->in_vehicle=false;service_kill_car_stop_failure=1;
            check(!service.tick_all() && service_kill_car_stops==0 && source.state(actor)->current==9,
                "Car-stop rejection preserves source objective and never enters LEAVE");
            if(failure==0 || failure==4) {
                RouteVehicleSnapshot target_car=car;target_car.handle=43;target_car.generation=43;
                target_car.entity=0x987700;target_car.position={1,0,0};
                service_target_car_override=target_car;
                auto& victim=*service_driver_override;victim.in_vehicle=true;victim.native_state=50;
                victim.current_vehicle_handle=43;victim.current_vehicle_entity=target_car.entity;victim.current_vehicle_generation=43;
                check(!service.tick_all() && service_kill_car_stops==0 && leaves==0,
                    "Unknown original target-car exit query cannot authorize source vehicle stop");
                service_target_car_override->source_can_ped_exit_car=false;
                check(service.tick_all() && service_kill_car_stops==0 && leaves==0 && source.state(actor)->current==9,
                    "Actual source CanPedExitCar refusal waits without changing either vehicle or objective");
                service_target_car_override->position={20,0,0};
                check(!service.tick_all() && service_kill_car_stops==0,
                    "ANY_MEANS range uses occupied target car rather than the nearby ped position");
                victim.native_state=58; // Native DRAG_FROM_CAR maps to source57.
                check(service.tick_all() && service_kill_car_stops==0 && leaves==0,
                    "Dragged target uses original ped distance while preserving actual target-car exit refusal");
                victim.native_state=50;service_target_car_override->position={1,0,0};
                service_target_car_override->source_can_ped_exit_car=true;
            }
            service_kill_car_stop_failure=failure==2 || failure==4?failure:0;
            const auto route_epoch=token.epoch;
            const bool accepted=service.tick_all();
            if(!failure) {
                check(accepted && leaves==1 && source.state(actor)->current==16 && source.state(actor)->previous==9 &&
                      token.epoch==route_epoch,
                    "Target with real original car-exit permission allows stop then LEAVE and exact one-slot objective history");
            } else {
                check(!accepted && leaves==(failure==3?1:0) && !source.state(actor),
                    "Failure after entered car-stop prefix retires source authority instead of replaying partial effect");
            }
            check(service.tick_all() && service_kill_car_stops==1 && leaves==(failure==2 || failure==4?0:1),
                "Accepted or terminal car-stop sequence is never replayed on later source frame");
        }
        service_kill_car_stop_failure=0;
        service_target_car_override.reset();
        service_actor_override.reset();service_vehicle_override.reset();service_driver_override.reset();service_fixture=false;
    }
    {
        service_fixture=true;service_now=100;pose={actor,{0,0,0},true,true,true};
        RouteActorVehicleSnapshot native_actor;native_actor.actor=actor;
        native_actor.alive=native_actor.controllable=true;native_actor.created_by=1;
        service_actor_override=native_actor; // Native mission provenance is deliberately unknown.
        RouteVehicleSnapshot car;car.handle=42;car.entity=0x987600;car.generation=42;
        car.alive=true;car.health=1000;car.is_bus=false;car.player_disabled=true;
        car.on_screen=true;car.upside_down=false;car.max_passengers=3;service_vehicle_override=car;
        {
            OriginalRouteIntents source;OwnedRouteReaders readers;
            readers.actor=[](uint32_t ped){return ped==actor.native_ped?std::optional<ObjectiveActor>(actor):std::nullopt;};
            const auto execute=[](const NativeCall&){NativeResult result;result.executed=result.native_entered=true;return result;};
            SaRouteService service(source,table,std::move(readers),
                [](int32_t handle){return handle==actor.handle?std::optional<ObjectiveActor>(actor):std::nullopt;},
                [](ObjectiveActor){return original_cop_state(service_actor_override->native_state);},[](int32_t){},execute);
            check(service.constructed(actor,uint8_t(2)) && service.follow_route(actor.handle,7,1,false)==StepResult::continued,
                "Real original CREATE_CHAR constructor supplies mission provenance independently of C6");
            NativeCall passenger;passenger.opcode=0x05CA;passenger.inputs={actor.handle,42,-1,-1};
            bool projected=false;
            const auto result=service.execute_source_objective(COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_PASSENGER,
                passenger,projected,[&]{return execute(passenger);});
            check(result.executed && projected && source.state(actor)->current==17 &&
                  source.state(actor)->objective_deadline==14100 &&
                  source.state(actor)->source_created_by==std::optional<uint8_t>(2),
                "Production passenger timeout uses actual original constructor origin, not missing/native mission flag");
            check(service_actor_override->created_by==1 && !service_actor_override->mission_character &&
                  !service_actor_override->source_created_by,
                "Original provenance projection never overwrites native actor snapshot or mission-cleanup context");
        }
        service_actor_override.reset();service_vehicle_override.reset();service_fixture=false;
    }
    {
        RouteScripts scripts;size_t fallthroughs=0;
        auto step=[&](uint16_t opcode,const std::vector<int32_t>& inputs) {
            std::vector<uint8_t> bytes(64);bytes.push_back(uint8_t(opcode));bytes.push_back(uint8_t(opcode>>8));
            for(auto value:inputs){bytes.push_back(6);for(int n=0;n<4;++n)bytes.push_back(uint8_t(uint32_t(value)>>(8*n)));}
            Vm vm(std::move(bytes),64,64);Thread t;t.ip=64;
            vm.backend=[&](Vm& v,Thread& thread,uint16_t op){
                if(const auto result=try_engine_adapter(scripts,v,thread,op))return *result;
                ++fallthroughs;return StepResult::unsupported;
            };
            return vm.step(t,0);
        };
        auto add=[&](int route,float x){return step(COMMAND_ADD_ROUTE_POINT,{route,entity_bits(x),0,0});};
        check(add(0x10007,0)==StepResult::continued && add(7,10)==StepResult::continued && add(7,20)==StepResult::continued,"Real SCM ADD_ROUTE_POINT writes shared original pool and narrows halfword identity");
        check(step(COMMAND_ADD_ROUTE_POINT,{7,0})==StepResult::failed && scripts.table().nodes()[3].route==-1,"Malformed source command cannot partially mutate route pool");
        check(ledger.constructed(actor),"Actual source construction for shared VM/native route pipeline");
        OriginalRouteJobs<Host> jobs(host,ledger,scripts.table());
        check(jobs.source_accepted(ledger.follow(actor,7,1,0,scripts.table()),true),"Source follow plan consumes the SAME pool written by real SCM instructions");
        ++token.epoch;token.binding.record.task+=0x80;
        NativeResult effect;effect.executed=effect.native_entered=true;effect.accepted_script_task=token.binding;
        check(jobs.motion_accepted(actor,ledger.state(actor)->revision,effect),"Observed actual native assignment for VM-declared route");
        pose.position={10,0,0};check(jobs.tick(actor)==OriginalRouteConsumer::Result::advanced && last_motion_target[0]==20,"VM source points reach actual production native motion adapter");
        check(add(7,30)==StepResult::continued,"SCM can append point while actor already follows route");
        pose.position={20,0,0};check(jobs.tick(actor)==OriginalRouteConsumer::Result::advanced && last_motion_target[0]==30,"Live consumer reads appended source point instead of an initial cached route list");
        check(add(7,40)==StepResult::continued && step(COMMAND_REMOVE_ROUTE,{0x10007})==StepResult::continued,"Real SCM REMOVE_ROUTE applies source halfword identity and block-copy semantics");
        pose.position={30,0,0};const auto count=writes;
        check(jobs.tick(actor)==OriginalRouteConsumer::Result::unavailable && writes==count,"Removed route cannot emit stale cached tail40 or turn its invalid stop boundary into success");
        check(!fallthroughs && RouteScripts::handles(COMMAND_SET_CHAR_OBJ_FOLLOW_ROUTE),"Shared route frontend owns all three source opcodes");
        check(step(COMMAND_SET_CHAR_OBJ_FOLLOW_ROUTE,{actor.handle,7,1})==StepResult::unsupported,
            "Missing real route service refuses FOLLOW_ROUTE before inventing movement");
        int follows=0;
        scripts.set_follow_route_handler([&](int32_t ped,int32_t route,int32_t mode,Thread&){
            check(ped==actor.handle && route==7 && mode==2,"FOLLOW_ROUTE forwards all three original operands");
            ++follows;return StepResult::continued;
        });
        check(step(COMMAND_SET_CHAR_OBJ_FOLLOW_ROUTE,{actor.handle,7,2})==StepResult::continued && follows==1,
            "SCM FOLLOW_ROUTE invokes bound production service once");
        check(step(COMMAND_SET_CHAR_OBJ_FOLLOW_ROUTE,{actor.handle,7})==StepResult::failed && follows==1,
            "Truncated FOLLOW_ROUTE never invokes the source consumer");
        jobs.clear();
        RouteScripts full;
        for(size_t n=0;n<OriginalRouteTable::capacity;++n) {
            std::vector<uint8_t> bytes(64);const auto op=uint16_t(COMMAND_ADD_ROUTE_POINT);bytes.push_back(uint8_t(op));bytes.push_back(uint8_t(op>>8));
            for(int32_t value:{9,0,0,0}){bytes.push_back(6);for(int k=0;k<4;++k)bytes.push_back(uint8_t(uint32_t(value)>>(8*k)));}
            Vm vm(bytes,64,64);Thread t;t.ip=64;vm.backend=[&](Vm& v,Thread& thread,uint16_t code){return full.dispatch(v,thread,code);};
            check(vm.step(t,0)==StepResult::continued,"Fill actual route pool through SCM frontend");
            if(n+1==OriginalRouteTable::capacity){t={};t.ip=64;check(vm.step(t,0)==StepResult::unsupported && t.ip==64 && !t.diagnostic.empty(),"Retail index200 overflow is refused at original instruction, never converted into successful no-op");}
        }
    }
    std::cout<<"PASS: retail route intent transitions, vehicle interruption/restoration, live native adapter integration, accepted receipts and generation retirement; shared SCM frontend\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
