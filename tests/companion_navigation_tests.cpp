#include "lcs/native.hpp"
#include "lcs/seek_policy.hpp"
#include "lcs/riot_wait.hpp"
#include "ScriptCommands.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <cstring>
using namespace lcs;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static void integer(std::vector<uint8_t>& code,int32_t value){code.push_back(6);for(int i=0;i<4;++i)code.push_back(uint8_t(uint32_t(value)>>(i*8)));}
int main(int argc,char** argv){try {
    check(argc==2,"Original SCM required");const auto image=ScmImage::load(argv[1]);NativeAdapter adapter(image);adapter.world_ready=true;
    bool exists=true,alive=true,member=false,frozen=false,in_car=false,leader_driving=true,full=false,backend=true,entering=false;
    int32_t leader_vehicle=600,status=4,capacity=3,free_seat=2;size_t entries=0,policies=0;bool use_nodes=true;NativeCall boarding;
    uint16_t failed_opcode=0;bool entry_query_available=true,control_query_available=true,partial_entry_on_failure=false;
    size_t status_queries=0,failed_queries=0;std::vector<std::string> events;
    uint32_t now=0;size_t stands=0,clears=0;
    uintptr_t current_native_ped=0x13570000;
    bool identity_available=true;
    adapter.live_ped_identity=[&](int32_t actor)->std::optional<ObjectiveActor>{
        check(actor==777,"Companion identity reader must use the live ped handle");
        if(!identity_available)return {};
        return ObjectiveActor{actor,current_native_ped};
    };
    adapter.char_in_control=[&](int32_t actor)->std::optional<bool>{check(actor==777,"Control query references actual companion");if(!control_query_available)return {};return !frozen;};
    adapter.char_entering_as_passenger=[&](int32_t actor)->std::optional<bool>{check(actor==777,"Entry query references actual companion");if(!entry_query_available)return {};return entering;};
    adapter.diagnostic_event=[&](const std::string& event){events.push_back(event);};
    adapter.set_pednode_seek=[&](int32_t actor,bool value){check(actor==777,"Policy addresses the original actor");if(!backend)return false;++policies;use_nodes=value;return true;};
    adapter.execute=[&](const NativeCall& call){NativeResult r;r.executed=true;
        if(call.opcode==failed_opcode) {
            ++failed_queries;r.executed=false;r.diagnostic="Injected native boarding failure";
            if(call.opcode==0x05CA && partial_entry_on_failure){++entries;entering=true;}
            return r;
        }
        switch(call.opcode){
        case 0x056D:r.condition=exists;break;case 0x0118:r.condition=!alive;break;
        case 0x01F5:r.values[0]=100;break;case 0x07AF:r.values[0]=50;break;case 0x07FD:r.condition=true;break;
        case 0x0631:member=true;break;case 0x06C9:member=false;break;case 0x06EE:r.condition=member;break;
        case 0x0687:++clears;entering=false;break;
        case 0x092B:r.values[0]=call.inputs[1]==3 && member?777:-1;break;
        case 0x04D7:frozen=call.inputs[1]!=0;break;
        case 0x00DF:r.condition=call.inputs[0]==100?leader_driving:in_car;break;
        case 0x03C0:r.values[0]=leader_vehicle;break;
        case 0x046C:r.values[0]=100;break;case 0x00A0:break; // Actual fixture positions coincide.
        case 0x062E:++status_queries;r.values[0]=status;r.executed=false;break; // Deliberately unusable; must never be needed by the updater.
        case 0x01EA:r.values[0]=capacity;break;
        case 0x0431:r.condition=!full && call.inputs[1]==free_seat;break; // Other passenger seats are occupied.
        case 0x05CA:boarding=call;++entries;status=0;break; // Issuing a task never invents occupancy.
        case 0x01BD:r.values[0]=int32_t(now);break;
        case 0x05BA:check(call.inputs[1]==500,"Original traffic wait ignores the animation timeout");++stands;break;
        }return r;
    };
    auto run=[&](uint16_t op,std::initializer_list<int32_t> args){std::vector<uint8_t> code(64);code.push_back(uint8_t(op));code.push_back(uint8_t(op>>8));for(auto value:args)integer(code,value);Vm vm(std::move(code),64,64);Thread thread;thread.ip=64;vm.backend=[&](Vm& v,Thread& t,uint16_t command){return adapter.dispatch(v,t,command);};return vm.step(thread,0);};
    std::string error;
    check(run(COMMAND_SET_PLAYER_AS_LEADER,{777,0})==StepResult::continued,"Follower joins after player is already driving");
    check(adapter.tick_scripted_peds(error) && entries==1 && !in_car,"Late joining follower gets an explicit entry despite a stale group record, without fake occupancy");
    check(boarding.inputs==std::vector<int32_t>({777,600,-1,2}),"Passenger task uses actual vehicle and an actually free seat");
    for(auto pending:{-1,0,1,2,3,4,5,6,7,INT32_MAX}){status=pending;check(adapter.tick_scripted_peds(error) && entries==1 && status_queries==0,"Boarding never depends on 062E status values, including unmapped states or failed status queries");}
    now=1999;status=4;check(adapter.tick_scripted_peds(error) && entries==1,"Group queue has a bounded initial grace period");
    now=2000;check(adapter.tick_scripted_peds(error) && entries==2 && !in_car,"Stalled group queue retries with player still driving, without inventing occupancy");
    entering=true;now=60000;check(adapter.tick_scripted_peds(error) && entries==2,"Actual passenger task protects even a lengthy approach/animation regardless of queued script status");
    check(run(COMMAND_SET_PLAYER_AS_LEADER,{777,0})==StepResult::continued && entering && clears==1 && adapter.tick_scripted_peds(error) && entries==2,"Repeated original SET_PLAYER_AS_LEADER preserves the live entry tree and existing group membership");
    Vm slacker(image);slacker.load_mission(image.mission(48));Thread retry;retry.name="vic2";retry.ip=277456;
    check(read16(slacker.memory,retry.ip)==COMMAND_SET_PLAYER_AS_LEADER,"VIC2 repeats SET_PLAYER_AS_LEADER at original IP 277456");
    auto retry_args=retry;retry_args.ip+=2;slacker.store(retry_args,777);retry.locals=retry_args.locals;
    slacker.backend=[&](Vm& v,Thread& t,uint16_t command){return adapter.dispatch(v,t,command);};
    check(slacker.step(retry,0)==StepResult::continued && entering && clears==1,"Exact original VIC2 retry cannot cancel a real passenger entry");
    entering=false;now=60001;
    const auto entry_baseline=entries;
    in_car=true;status=3;check(adapter.tick_scripted_peds(error) && entries==entry_baseline,"Engine reports successful boarding; no repeated task");
    in_car=false;full=true;check(adapter.tick_scripted_peds(error) && entries==entry_baseline,"Full car does not cause forced boarding or a campaign stop");
    full=false;check(run(COMMAND_FREEZE_ALL_PLAYER_FOLLOWERS,{0,1})==StepResult::continued && adapter.tick_scripted_peds(error) && entries==entry_baseline,"Frozen follower cannot be retasked inside an interior");
    check(run(COMMAND_FREEZE_ALL_PLAYER_FOLLOWERS,{0,0})==StepResult::continued && adapter.tick_scripted_peds(error) && entries==entry_baseline+1,"Thawed follower can resume real entry");
    leader_vehicle=601;status=2;check(adapter.tick_scripted_peds(error) && entries==entry_baseline+2 && boarding.inputs[1]==601,"Changing vehicle replaces the old pending target");
    check(run(COMMAND_LEAVE_GROUP,{777})==StepResult::continued && adapter.tick_scripted_peds(error) && entries==entry_baseline+2,"Original dropoff removes companion transport ownership");
    // Reproduce the user's motorcycle transition: the task may have finished
    // before IS_CHAR_IN_ANY_CAR catches up. The updater must not halt the VM.
    capacity=1;free_seat=0;leader_vehicle=602;now+=3000;
    check(run(COMMAND_SET_PLAYER_AS_LEADER,{777,0})==StepResult::continued,"Dealer can join with player already on motorcycle");
    check(run(COMMAND_SET_CHAR_ONLY_ALLOWED_TO_SIT_BEHIND,{777,1})==StepResult::continued,"Original rear seat preference retained");
    const auto bike_entries=entries;
    status=7;check(adapter.tick_scripted_peds(error) && entries==bike_entries+1 && boarding.inputs==std::vector<int32_t>({777,602,-1,0}) && status_queries==0,"Single passenger motorcycle uses real seat zero even with rear preference and unmapped status");
    entering=true;now+=10000;
    check(adapter.tick_scripted_peds(error) && entries==bike_entries+1,"Motorcycle entry animation is never cancelled by assist");
    entering=false;status=7;
    check(adapter.tick_scripted_peds(error) && error.empty(),"Entry completion before occupancy propagation does not stop campaign");
    const auto transition_entries=entries;
    in_car=true;
    check(adapter.tick_scripted_peds(error) && entries==transition_entries && error.empty(),"Confirmed motorcycle occupancy completes assistance without synthetic mission success");
    in_car=false;
    check(run(COMMAND_LEAVE_GROUP,{777})==StepResult::continued,"Bike dropoff releases companion");
    // A front-restricted follower must not take seat 1/2 when seat 0 is busy.
    // The original 1485 state also affects automatic companion boarding.
    capacity=3;free_seat=1;leader_vehicle=604;now+=3000;status=4;
    check(run(COMMAND_SET_CHAR_ONLY_ALLOWED_TO_SIT_BEHIND,{777,0})==StepResult::continued,"Clear previous rear-only policy");
    check(run(COMMAND_SET_CHAR_ONLY_ALLOWED_TO_SIT_IN_FRONT,{777,1})==StepResult::continued,"1485 validates front seat owner");
    check(run(COMMAND_SET_PLAYER_AS_LEADER,{777,0})==StepResult::continued,"Front-only companion joins active leader");
    const auto front_begin=entries;
    check(adapter.tick_scripted_peds(error) && entries==front_begin,"Front-only follower cannot board occupied seat 0 by falling back to free rear seat 1");
    free_seat=0;now+=3000;
    check(adapter.tick_scripted_peds(error) && entries==front_begin+1 && boarding.inputs==std::vector<int32_t>({777,604,-1,0}),"Front seat becomes available; real native task uses seat 0");
    check(run(COMMAND_LEAVE_GROUP,{777})==StepResult::continued,"Front-only companion can leave the group");
    check(run(COMMAND_SET_CHAR_ONLY_ALLOWED_TO_SIT_IN_FRONT,{777,0})==StepResult::continued,"1485 can relinquish front-only policy");
    identity_available=false;now+=250;entering=false;
    const auto before_unavailable_identity=entries;
    check(adapter.tick_scripted_peds(error) && entries==before_unavailable_identity,"Unverifiable live identity must not schedule a potentially stale passenger task");
    identity_available=true;current_native_ped=0x13570010;now+=250;

    // Restore the fixture's original rear preference and single-passenger
    // topology before exercising its existing failure/recovery suite.
    capacity=1;leader_vehicle=602;free_seat=0;
    check(run(COMMAND_SET_CHAR_ONLY_ALLOWED_TO_SIT_BEHIND,{777,1})==StepResult::continued,"Restore existing rear preference for regression");
    const auto saved_memory=slacker.memory;const auto saved_ip=retry.ip;const auto saved_locals=retry.locals;
    for(uint16_t opcode:{0x056D,0x0118,0x06EE,0x01F5,0x00DF,0x03C0,0x046C,0x00A0,0x01EA,0x0431}) {
        now+=3000;check(run(COMMAND_SET_PLAYER_AS_LEADER,{777,0})==StepResult::continued,"Failure fixture registers real companion");
        failed_opcode=opcode;failed_queries=0;events.clear();const auto baseline=entries;
        check(adapter.tick_scripted_peds(error) && error.empty() && entries==baseline && failed_queries>=1,"Failed boarding query defers only assistance, never campaign");
        const auto failed_baseline=failed_queries;
        now+=249;check(adapter.tick_scripted_peds(error) && failed_queries==failed_baseline && events.size()==1,"Deferred query is throttled and logged once");
        failed_opcode=0;++now;
        check(adapter.tick_scripted_peds(error) && entries==baseline+1 && events.back().find("recovered")!=std::string::npos,"Recovered backend resumes actual entry after retry interval");
        check(slacker.memory==saved_memory && retry.ip==saved_ip && retry.locals==saved_locals,"Advisory failure/recovery cannot rewrite original script state or progression");
        check(run(COMMAND_LEAVE_GROUP,{777})==StepResult::continued,"Failure fixture releases group");
    }
    // An entry command can fault after a task was accepted. Observe the actual
    // task tree rather than immediately repeating a potentially partial call.
    now+=3000;check(run(COMMAND_SET_PLAYER_AS_LEADER,{777,0})==StepResult::continued,"Partial task failure fixture joins group");
    failed_opcode=0x05CA;partial_entry_on_failure=true;events.clear();const auto partial_baseline=entries;
    check(adapter.tick_scripted_peds(error) && entries==partial_baseline+1 && entering && error.empty(),"Rejected executor result preserves campaign even if task was accepted");
    failed_opcode=0;now+=250;
    check(adapter.tick_scripted_peds(error) && entries==partial_baseline+1 && entering,"Recovered query sees live entry; partially accepted task is not duplicated");
    entering=false;now+=500;
    check(adapter.tick_scripted_peds(error) && entries==partial_baseline+1,"Rejected task retains two second grace before any retry");
    now+=1250;check(adapter.tick_scripted_peds(error) && entries==partial_baseline+2,"Only absent real task after grace allows retry");
    check(run(COMMAND_LEAVE_GROUP,{777})==StepResult::continued,"Partial task fixture releases group");partial_entry_on_failure=false;
    // A temporarily unreadable task tree must not replace a still live task.
    now+=3000;check(run(COMMAND_SET_PLAYER_AS_LEADER,{777,0})==StepResult::continued,"Unavailable task tree fixture joins group");
    const auto query_baseline=entries;entry_query_available=false;events.clear();
    check(adapter.tick_scripted_peds(error) && entries==query_baseline && error.empty(),"Unreadable task tree defers entry safely");
    now+=250;entry_query_available=true;entering=true;
    check(adapter.tick_scripted_peds(error) && entries==query_baseline,"Recovered tree protects existing animation");
    entering=false;control_query_available=false;now+=250;
    check(adapter.tick_scripted_peds(error) && entries==query_baseline && error.empty(),"Unreadable control state cannot abort campaign");
    control_query_available=true;now+=250;
    check(adapter.tick_scripted_peds(error) && entries==query_baseline+1,"Control state recovery permits actual boarding");
    // Persistent failure logs are bounded; timer subtraction survives wrap.
    leader_vehicle=603;capacity=255;events.clear();now=UINT32_MAX-100;
    check(adapter.tick_scripted_peds(error) && entries==query_baseline+1 && events.size()==1,"Invalid capacity is reported without inventing a seat");
    now=148;check(adapter.tick_scripted_peds(error) && events.size()==1,"Recovery throttle survives game timer wrap");
    now=149;check(adapter.tick_scripted_peds(error) && events.size()==1,"Persistent failure avoids one log per frame");
    now=4900;check(adapter.tick_scripted_peds(error) && events.size()==2,"Persistent failure is reported again after five seconds");
    capacity=1;now+=250;
    check(adapter.tick_scripted_peds(error) && entries==query_baseline+2,"Valid capacity restores real entry");
    failed_opcode=0x01BD;events.clear();
    check(adapter.tick_scripted_peds(error) && error.empty() && events.size()==1,"Unavailable helper clock is nonfatal");
    check(adapter.tick_scripted_peds(error) && events.size()==1,"Unavailable clock is not logged every frame");
    failed_opcode=0;now+=250;
    check(adapter.tick_scripted_peds(error) && events.size()==2 && events.back().find("clock recovered")!=std::string::npos,"Clock recovery resumes assistance");
    check(run(COMMAND_LEAVE_GROUP,{777})==StepResult::continued,"All regression fixtures release companion");
    // Load the real mission buffer before checking the user's exact reported IP.
    Vm original(image);original.load_mission(image.mission(49));Thread mission;mission.name="vic3";mission.ip=275838;
    check(read16(original.memory,mission.ip)==COMMAND_SET_CHAR_USE_PEDNODE_SEEK,"Reported VIC3 IP is the untouched original navigation command");
    auto arguments=mission;arguments.ip+=2;
    check(original.memory[arguments.ip]>=10,"Original actor is a variable reference");original.store(arguments,777);
    original.backend=[&](Vm& v,Thread& t,uint16_t command){return adapter.dispatch(v,t,command);};
    check(original.step(mission,0)==StepResult::continued && !use_nodes && policies==1,"Original VIC3 disables node seek and advances without native unsupported 0411");
    check(adapter.tick_scripted_peds(error) && policies==2,"Navigation policy also applies to combat subtasks created later");
    backend=false;check(!adapter.tick_scripted_peds(error) && !error.empty(),"Failed native navigation application remains explicit");backend=true;
    exists=false;check(adapter.tick_scripted_peds(error) && policies==2,"Removed actor retires the policy instead of touching a reused handle");
    exists=true;now=11000;check(run(COMMAND_SET_CHAR_WAIT_STATE,{777,1,9000})==StepResult::continued && stands==1,"Traffic signal state creates movement wait without requiring animation resources");
    now=11499;check(adapter.tick_scripted_peds(error) && stands==1,"Original signal wait lasts at least 500ms");
    now=11500;check(adapter.tick_scripted_peds(error) && stands==2,"Red signal preserves actual native stand task");
    now=12000;check(adapter.tick_scripted_peds(error) && stands==2,"Original green signal releases the wait without changing mission flags");
    now=16384+11500;check(run(COMMAND_SET_CHAR_WAIT_STATE,{777,1,0})==StepResult::continued,"Repeated original traffic cycle can start");
    check(run(COMMAND_SET_CHAR_OBJ_NO_OBJ,{777})==StepResult::continued,"Original objective replacement cancels traffic wait");
    now+=500;check(adapter.tick_scripted_peds(error) && stands==3,"Cancelled traffic wait cannot overwrite the next original task");
    check(native_seek_radius(true)==0 && std::isfinite(native_seek_radius(false)) && native_seek_radius(false)>1.0e30f,"Node/direct seek radii select the actual native route branch");
    Vm parking(image);Thread airport;airport.name="aportpk";airport.ip=210692;
    check(read16(parking.memory,airport.ip)==COMMAND_REQUEST_ANIMATION,"User's exact parking failure IP retains original REQUEST_ANIMATION");
    bool resident=false;size_t requests=0;
    adapter.named_animation=[&](const std::string& name,uint16_t action)->std::optional<bool>{
        if(name!="riot")return {};if(action==0){++requests;return true;}if(action==1)return resident;resident=false;return true;
    };
    parking.backend=[&](Vm& v,Thread& t,uint16_t command){return adapter.dispatch(v,t,command);};
    check(parking.step(airport,0)==StepResult::continued && requests==1 && airport.ip==210702,"Original parking command requests the complete RIOT block without altering campaign state");
    auto named_command=[&](uint16_t opcode,const char* name,Thread& t){std::vector<uint8_t> code(64);code.push_back(uint8_t(opcode));code.push_back(uint8_t(opcode>>8));for(unsigned i=0;i<8;++i)code.push_back(i<std::strlen(name)?name[i]:0);Vm v(std::move(code),64,64);t={};t.ip=64;v.backend=[&](Vm& vm,Thread& th,uint16_t op){return adapter.dispatch(vm,th,op);};return v.step(t,0);};
    Thread query;
    check(named_command(COMMAND_HAS_ANIMATION_LOADED,"RIOT",query)==StepResult::continued && !query.condition,"Unloaded named block remains false even after a request");
    resident=true;check(named_command(COMMAND_HAS_ANIMATION_LOADED,"RIOT",query)==StepResult::continued && query.condition,"Residency reads real backend completion");
    check(named_command(COMMAND_REMOVE_ANIMATION,"RIOT",query)==StepResult::continued && !resident,"Removal reaches owned native block");
    check(named_command(COMMAND_REQUEST_ANIMATION,"MISSING",query)==StepResult::unsupported,"Absent original block cannot silently succeed");
    airport={};airport.name="aportpk";airport.ip=213289;
    check(read16(parking.memory,airport.ip)==COMMAND_SET_CHAR_WAIT_STATE,"Latest user halt IP is the unchanged airport wait-state command");
    auto wait_args=airport;wait_args.ip+=2;parking.store(wait_args,777);
    const auto wait_state=parking.parameter(wait_args),wait_duration=parking.parameter(wait_args);
    check(wait_state==29,"Airport invokes original RIOT state, not a mission objective bypass");
    size_t riot_requests=0;
    adapter.play_wait_animation=[&](int32_t actor,int32_t state,int32_t duration,bool repeat){
        check(actor==777 && state==29 && duration==wait_duration && !repeat,"All original airport wait operands reach the animation backend");++riot_requests;return true;
    };
    check(parking.step(airport,0)==StepResult::continued && riot_requests==1 && airport.ip==wait_args.ip,"Original concurrent airport thread advances past exact failure IP without rewriting progression");
    RiotWaitCycle cycle;check(cycle.choose(1000,0,4999)==0 && cycle.looping() && cycle.deadline==8999 && cycle.flags()==27,"Original random RIOT loops last 3 to 8 seconds and remain partial");
    check(!cycle.expired(8999) && cycle.expired(9000),"Original timeout releases repeat after its actual deadline");
    check(cycle.choose(9000,0,0)==1,"Consecutive random animations cannot repeat the previous choice");
    check(cycle.choose(10000,5,0)==6 && !cycle.looping() && cycle.deadline==18000 && cycle.flags()==25,"Final RIOT gestures play once and fade naturally");
    cycle.choose(UINT32_MAX-100,0,0);check(!cycle.expired(2899) && cycle.expired(2900),"Riot cycle deadline survives native timer wrap");
    std::cout<<"PASS: car/motorcycle boarding, no 062E dependency, completion transition, helper fault isolation/recovery, log throttling/timer wrap, unchanged original VM, pending animation, freeze/thaw, exact VIC3 navigation and later subtasks; gameExecuted=false\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
