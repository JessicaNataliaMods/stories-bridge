#include "lcs/native.hpp"
#include "ScriptCommands.h"
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace lcs;
static void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
static Vm program(uint16_t opcode,const std::vector<int32_t>& operands) {
    std::vector<uint8_t> bytes(64);bytes.push_back(uint8_t(opcode));bytes.push_back(uint8_t(opcode>>8));
    for(auto n:operands){bytes.push_back(6);for(int b=0;b<4;++b)bytes.push_back(uint8_t(uint32_t(n)>>(b*8)));}
    return Vm(std::move(bytes),64,64);
}
static int32_t bits(float value){int32_t out;std::memcpy(&out,&value,4);return out;}

// ---- fixture: a tiny native world behind NativeAdapter ---------------------------------------------------------------------
struct World {
    std::map<int32_t,ObjectiveActor> live;
    std::map<int32_t,NativeAdapter::AttackPolicyActor> facts;
    std::map<int32_t,EpochScriptTask> active_tasks;
    NativeAdapter::AttackPolicyWorld world{1,true,true};
    int world_reads=0;uint64_t next_task_epoch=1;bool facts_available=true,world_available=true,task_query_available=true,capture_enabled=true,capture_as_event=false,flee_refuses=false,flee_enters_then_fails=false;
    std::vector<NativeCall> sent;
};

int main(){try{
    World w;
    const ObjectiveActor thug{27,0x1000},second{28,0x2000},player{1,0x3000};
    w.live={{27,thug},{28,second},{1,player}};
    w.facts[27]={true,true,false,true};      // alive, on foot, not a cop, released from mission control
    w.facts[28]={true,true,false,true};
    ScmImage image;NativeAdapter adapter(image);adapter.world_ready=true;
    adapter.live_ped_identity=[&](int32_t handle)->std::optional<ObjectiveActor>{const auto f=w.live.find(handle);return f==w.live.end()?std::nullopt:std::optional<ObjectiveActor>(f->second);};
    adapter.attack_policy_actor=[&](const ObjectiveActor& a)->std::optional<NativeAdapter::AttackPolicyActor>{
        if(!w.facts_available)return std::nullopt;const auto f=w.facts.find(a.handle);return f==w.facts.end()?std::nullopt:std::optional<NativeAdapter::AttackPolicyActor>(f->second);};
    adapter.attack_policy_world=[&]()->std::optional<NativeAdapter::AttackPolicyWorld>{++w.world_reads;return w.world_available?std::optional<NativeAdapter::AttackPolicyWorld>(w.world):std::nullopt;};
    adapter.attack_policy_task_accept=[&](const CapturedScriptTask& captured)->std::optional<EpochScriptTask>{
        for(const auto& [handle,actor]:w.live)if(uint32_t(actor.native_ped)==captured.record.ped) {
            EpochScriptTask token{captured,w.next_task_epoch++};w.active_tasks[handle]=token;return token;
        }
        return std::nullopt;
    };
    adapter.attack_policy_task_active=[&](const ObjectiveActor& actor,const EpochScriptTask& expected)->std::optional<bool>{
        if(!w.task_query_available)return std::nullopt;const auto current=w.active_tasks.find(actor.handle);
        if(current==w.active_tasks.end() || current->second.epoch!=expected.epoch)return false;
        const auto& record=current->second.binding.record;
        if(record.status==0 && record.event && !record.task)return std::nullopt;
        if(record.opcode!=expected.binding.record.opcode || record.ped!=expected.binding.record.ped || !record.task || record.event)return false;
        if(expected.binding.record.task && record.task!=expected.binding.record.task)return false;
        return true;
    };
    adapter.execute=[&](const NativeCall& call){
        NativeResult r;w.sent.push_back(call);
        if(call.opcode==0x05DA && w.flee_refuses){return r;}
        r.executed=true;r.native_entered=true;
        if(call.opcode==0x05DA && w.flee_enters_then_fails){r.executed=false;}
        if(call.opcode==0x00DF)r.condition=false;                                  // not in a vehicle
        if(call.opcode==0x01F5)r.values={1};                                       // the player ped handle
        if(call.opcode==0x00A0){r.values={bits(10.f),bits(20.f),bits(0.f)};}
        return r;
    };
    adapter.execute_walking_objective=[&](const NativeCall& call){
        auto r=adapter.execute(call);
        if(r.executed && call.opcode==0x05E2 && w.capture_enabled && call.inputs.size()==2) {
            const auto live=w.live.find(call.inputs[0]);
            if(live!=w.live.end()) {
                CapturedScriptTask captured{size_t(call.inputs[0])%128,
                    ScriptTaskRecord{call.opcode,w.capture_as_event?uint32_t(0x6000+call.inputs[0]):0,
                        w.capture_as_event?0:uint32_t(0x7000+call.inputs[0]),uint32_t(live->second.native_ped),w.capture_as_event?0:2}};
                r.task_capture=NativeResult::TaskCapture::captured;r.accepted_script_task=captured;
            }
        }
        return r;
    };
    auto run=[&](uint16_t op,std::vector<int32_t> args){auto vm=program(op,args);Thread t;t.ip=64;t.condition=true;
        vm.backend=[&](Vm& v,Thread& th,uint16_t command){return adapter.dispatch(v,th,command);};
        const auto status=vm.step(t,0);return std::make_pair(status,t);};
    auto flees=[&]{size_t n=0;for(const auto& c:w.sent)if(c.opcode==0x05DA)++n;return n;};
    auto reset=[&]{w.sent.clear();};

    // ---- the command: both values, exact identity, no derived effect of its own ----
    check(run(COMMAND_SET_CHAR_ATTACKS_PLAYER_WITH_COPS,{27,1}).first==StepResult::continued,"1593 TRUE on a live ped is accepted");
    check(run(COMMAND_SET_CHAR_ATTACKS_PLAYER_WITH_COPS,{27,0}).first==StepResult::continued,"1593 FALSE on a live ped is accepted");
    check(run(COMMAND_SET_CHAR_ATTACKS_PLAYER_WITH_COPS,{99,0}).first==StepResult::unsupported && run(COMMAND_SET_CHAR_ATTACKS_PLAYER_WITH_COPS,{99,1}).first==StepResult::unsupported,
          "A handle that is not a live ped is refused for both values");
    check(flees()==0,"Setting the value performs no task by itself");

    // ---- a script goal to kill the player exists (the only goal the reaction concerns) ----
    w.capture_as_event=true;
    check(run(COMMAND_SET_CHAR_OBJ_KILL_PLAYER_ON_FOOT,{27,0}).first==StepResult::continued,"A script kill-the-player goal is accepted natively");
    reset();
    check(!adapter.tick_attack_with_cops() && flees()==0,"An accepted script event waits for the observer to see its associated task");
    auto associated=w.active_tasks.at(27);associated.binding.record={0x05E2,0,0x7000+27,uint32_t(thug.native_ped),1};w.active_tasks[27]=associated;
    check(adapter.tick_attack_with_cops() && flees()==1,"FALSE + released + associated kill-player task + cops present: the ped switches to FLEE_ON_FOOT_TILL_SAFE (once)");
    w.capture_as_event=false;
    check(w.sent.back().opcode==0x05DA && w.sent.back().inputs[0]==27,"The SA flee task is issued to the exact ped");
    reset();
    check(adapter.tick_attack_with_cops() && flees()==0,"The reaction is one-shot, like the source SetObjective");

    // A target handle alone cannot keep a completed or replaced native task alive.
    check(run(COMMAND_SET_CHAR_OBJ_KILL_PLAYER_ON_FOOT,{27,0}).first==StepResult::continued,"A captured kill task is accepted again");
    auto replaced=w.active_tasks.at(27);replaced.binding.record.task++ ;w.active_tasks[27]=replaced;
    reset();check(adapter.tick_attack_with_cops() && flees()==0,"A replaced native task is not made to flee from a stale accepted goal");
    check(run(COMMAND_SET_CHAR_OBJ_KILL_PLAYER_ON_FOOT,{27,0}).first==StepResult::continued,"A byte-identical task can be accepted again after a slot reuse");
    auto reused=w.active_tasks.at(27);++reused.epoch;w.active_tasks[27]=reused;
    reset();check(adapter.tick_attack_with_cops() && flees()==0,"A reused script-task slot with identical bytes cannot revive an old epoch receipt");
    w.capture_enabled=false;
    check(run(COMMAND_SET_CHAR_OBJ_KILL_PLAYER_ON_FOOT,{27,0}).first==StepResult::continued,"An accepted kill command without a unique capture still completes");
    w.capture_enabled=true;reset();check(adapter.tick_attack_with_cops() && flees()==0,"Missing capture never fabricates a current kill goal");

    // ---- TRUE keeps the native SA reaction: nothing replaces the task ----
    check(run(COMMAND_SET_CHAR_ATTACKS_PLAYER_WITH_COPS,{28,1}).first==StepResult::continued,"TRUE for the second ped");
    check(run(COMMAND_SET_CHAR_OBJ_KILL_PLAYER_ANY_MEANS,{28,0}).first==StepResult::continued,"Second ped carries a kill-player goal");
    reset();check(adapter.tick_attack_with_cops() && flees()==0,"TRUE: released, goal and cops present never replace the attack");
    check(run(COMMAND_SET_CHAR_ATTACKS_PLAYER_WITH_COPS,{28,0}).first==StepResult::continued,"The script later changes the value to FALSE");
    w.task_query_available=false;reset();check(!adapter.tick_attack_with_cops() && flees()==0,"An unreadable active-task result defers the reaction instead of guessing");
    w.task_query_available=true;reset();check(adapter.tick_attack_with_cops() && flees()==1,"Changing TRUE to FALSE makes the reaction apply to the goal that is still current");
    check(run(COMMAND_SET_CHAR_OBJ_KILL_PLAYER_ON_FOOT,{27,0}).first==StepResult::continued,"A fresh kill-the-player goal for the first ped after its flee");
    reset();check(adapter.tick_attack_with_cops() && flees()==1,"Each new kill goal gets its own reaction (the flee replaced only the previous goal)");
    check(run(COMMAND_SET_CHAR_ATTACKS_PLAYER_WITH_COPS,{27,0}).first==StepResult::continued,"Setting the same value again");
    reset();check(adapter.tick_attack_with_cops() && flees()==0,"Without a new goal nothing is replayed");

    // ---- phases and conditions ----
    w.live.erase(28);w.facts.erase(28);
    check(adapter.tick_attack_with_cops(),"A ped that no longer exists is forgotten without error");

    const ObjectiveActor third{29,0x4000};w.live[29]=third;w.facts[29]={true,true,false,false};      // still mission-controlled
    check(run(COMMAND_SET_CHAR_ATTACKS_PLAYER_WITH_COPS,{29,0}).first==StepResult::continued &&
          run(COMMAND_SET_CHAR_OBJ_KILL_PLAYER_ON_FOOT,{29,0}).first==StepResult::continued,"mission ped with FALSE and a goal");
    reset();check(adapter.tick_attack_with_cops() && flees()==0,"A mission-controlled ped is never made to flee, whatever the value");
    w.facts[29].released=true;
    w.world.cops_present=false;reset();
    check(adapter.tick_attack_with_cops() && flees()==0,"No cops around: the reaction does not apply");
    w.world.cops_present=true;w.world.player_alive=false;reset();
    check(adapter.tick_attack_with_cops() && flees()==0,"A dead player is not a threat to flee from");
    w.world.player_alive=true;w.facts[29].cop=true;reset();
    check(adapter.tick_attack_with_cops() && flees()==0,"Cops never take the reaction (retail test: type != COP)");
    w.facts[29].cop=false;w.facts[29].on_foot=false;reset();
    check(adapter.tick_attack_with_cops() && flees()==0,"A seated ped is not made to flee on foot");
    w.facts[29].on_foot=true;w.facts[29].alive=false;reset();
    check(adapter.tick_attack_with_cops() && flees()==0,"A dead ped is skipped");
    w.facts[29].alive=true;
    w.world.player_ped=2;reset();
    check(adapter.tick_attack_with_cops() && flees()==0,"A goal against someone else is not a player goal");
    w.world.player_ped=1;

    // another goal replaces the kill goal: the reaction concerns only a kill-the-player goal that is still current
    check(run(COMMAND_SET_CHAR_OBJ_WAIT_ON_FOOT,{29}).first==StepResult::continued,"Another goal replaces the kill goal");
    reset();check(adapter.tick_attack_with_cops() && flees()==0,"Without the kill-player goal nothing happens");
    check(run(COMMAND_SET_CHAR_OBJ_KILL_CHAR_ON_FOOT,{29,1}).first==StepResult::continued,"KILL_CHAR whose target is the player ped counts as a kill-player goal");
    w.live[1]=player;
    reset();check(adapter.tick_attack_with_cops() && flees()==1,"KILL_CHAR on the player handle reaches the same reaction");

    // identity: a recycled handle never inherits value or goal
    const ObjectiveActor recycled{29,0x5000};w.live[29]=recycled;w.facts[29]={true,true,false,true};
    check(adapter.tick_attack_with_cops(),"Identity change handled");
    reset();check(adapter.tick_attack_with_cops() && flees()==0,"The new occupant of the handle has no value and no goal");

    // failure handling
    const ObjectiveActor fourth{30,0x6000};w.live[30]=fourth;w.facts[30]={true,true,false,true};
    check(run(COMMAND_SET_CHAR_ATTACKS_PLAYER_WITH_COPS,{30,0}).first==StepResult::continued &&
          run(COMMAND_SET_CHAR_OBJ_KILL_PLAYER_ON_FOOT,{30,0}).first==StepResult::continued,"arm the fourth ped");
    w.facts_available=false;check(!adapter.tick_attack_with_cops(),"Unreadable native facts are reported, not guessed");w.facts_available=true;
    w.world_available=false;check(!adapter.tick_attack_with_cops(),"An unreadable world is reported, not guessed");w.world_available=true;
    w.flee_refuses=true;check(!adapter.tick_attack_with_cops(),"A refused flee is reported and retried");w.flee_refuses=false;
    w.flee_enters_then_fails=true;bool threw=false;try{(void)adapter.tick_attack_with_cops();}catch(const std::exception&){threw=true;}
    check(threw,"A flee that entered SA and failed is never replayed silently");w.flee_enters_then_fails=false;
    std::cout<<"PASS: 1593 both values reach SA: FALSE makes a released ped that carries a script kill-the-player goal flee while cops are present, TRUE keeps the native attack; mission control, cops, vehicles, identity and failures are gated\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
