#include "lcs/native.hpp"
#include "lcs/route_intent.hpp"
#include "ScriptCommands.h"
#include <iostream>
#include <stdexcept>
using namespace lcs;
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static Vm program(uint16_t opcode,const std::vector<int32_t>& inputs,bool output,bool valid_output=true) {
    std::vector<uint8_t> bytes(64);bytes.push_back(uint8_t(opcode));bytes.push_back(uint8_t(opcode>>8));
    for(auto value:inputs){bytes.push_back(6);for(int i=0;i<4;++i)bytes.push_back(uint8_t(uint32_t(value)>>(8*i)));}
    if(output)bytes.push_back(valid_output?12:2); // LCS compact local0, or invalid reserved local96
    return Vm(std::move(bytes),64,64);
}
int main(){try{
    {
        ScmImage image;NativeAdapter selected(image);selected.world_ready=true;
        int32_t handle=27;size_t acquisitions=0,births=0,selections=0;bool identity=true;
        selected.random_char_in_area=[&](const auto&,const auto&,int32_t,bool)->std::optional<int32_t>{++selections;return handle;};
        selected.live_ped_identity=[&](int32_t h)->std::optional<ObjectiveActor>{
            return identity?std::optional<ObjectiveActor>(ObjectiveActor{h,0x123400}):std::nullopt;
        };
        selected.source_actor_created=[&](ObjectiveActor){++births;};
        selected.source_actor_acquired=[&](ObjectiveActor actor){
            check(actor==ObjectiveActor{27,0x123400},"Random acquisition forwards actual native identity");++acquisitions;
        };
        auto run=[&](){auto vm=program(COMMAND_GET_RANDOM_CHAR_IN_AREA,
            {entity_bits(0.f),entity_bits(0.f),entity_bits(10.f),entity_bits(10.f),1,1,1},true);
            Thread t;t.ip=64;vm.backend=[&](Vm& v,Thread& th,uint16_t op){return selected.dispatch(v,th,op);};
            const auto result=vm.step(t,0);
            if(result==StepResult::continued)check(t.locals[0]==handle,"Acquisition preserves the original script output");
            return result;
        };
        check(run()==StepResult::continued && acquisitions==1 && births==0,"Ambient selection registers objective ownership without CREATE_CHAR authority");
        handle=-1;check(run()==StepResult::continued && acquisitions==1,"No selected ped means no lifetime acquisition");
        handle=27;identity=false;
        check(run()==StepResult::failed && acquisitions==1 && selections==3,"An entered selection with lost identity fails without replay or fabricated ownership");
    }
    ScmImage image;NativeAdapter adapter(image);adapter.world_ready=true;
    adapter.bind_model(10,124,"source-civilian",ModelKind::ped);
    OriginalRouteIntents intents;ObjectiveActor actor{0x101,0x123400};
    bool alive=false,identity_available=true,refuse=false,throw_callback=false,model_ready=true;
    size_t creations=0,deletions=0,births=0,retirements=0,identities=0,unknown_goals=0,motions=0;
    size_t friend_births=0;
    adapter.player_friend_actor_created=[&](int32_t handle){
        check(alive && handle==actor.handle,"Player relationship policy retires only after real native creation");
        ++friend_births;
    };
    adapter.live_ped_identity=[&](int32_t handle)->std::optional<ObjectiveActor>{
        ++identities;return identity_available && alive && handle==actor.handle?std::optional<ObjectiveActor>(actor):std::nullopt;
    };
    adapter.execute=[&](const NativeCall& call) {
        NativeResult result;result.executed=true;result.native_entered=true;result.condition=true;
        if(call.opcode==0x0248)result.condition=model_ready;
        if(call.opcode==0x05D3 || call.opcode==0x05F5)++motions;
        if(call.opcode==0x009A) {
            ++creations;result.executed=!refuse;result.native_entered=!refuse;
            if(!refuse){alive=true;result.values[0]=actor.handle;}
        }
        if(call.opcode==0x009B) {
            ++deletions;result.executed=!refuse;result.native_entered=!refuse;
            if(!refuse)alive=false;
        }
        return result;
    };
    adapter.source_actor_created=[&](ObjectiveActor created){
        ++births;check(alive && created==actor,"Source constructor receives the actual accepted native pool identity");
        check(intents.constructed(created,uint8_t(2)),"Accepted source constructor initializes actual intent ledger");
        check(intents.state(created)->source_created_by==std::optional<uint8_t>(2),
            "Original CREATE_CHAR provenance is MISSION_CHAR even without native mission cleanup");
        if(throw_callback)throw std::runtime_error("injected lifecycle observer failure");
    };
    adapter.source_actor_deleted=[&](ObjectiveActor deleted){
        ++retirements;check(!alive && deleted==actor,"Retirement receives identity captured BEFORE real native deletion");intents.retired(deleted);
    };
    adapter.source_actor_goal_unknown=[&](int32_t handle){++unknown_goals;intents.untracked_objective(handle);};
    adapter.source_actor_running=[&](ObjectiveActor current,bool enabled){
        check(current==actor,"Source running observation uses actual live generation");
        const auto plan=intents.running(current,enabled);
        if(plan.status()==OriginalRouteIntents::Change::ready)check(intents.accepted(plan),"Commit actual source running mutation");
    };
    auto step=[&](uint16_t opcode,const std::vector<int32_t>& input,bool output=false,bool valid=true) {
        auto vm=program(opcode,input,output,valid);Thread t;t.ip=64;
        vm.backend=[&](Vm& v,Thread& thread,uint16_t op){return adapter.dispatch(v,thread,op);};
        return vm.step(t,0);
    };
    const std::vector<int32_t> creation{4,10,entity_bits(10.f),entity_bits(20.f),entity_bits(30.f)};
    check(step(COMMAND_CREATE_CHAR,creation,true,false)==StepResult::failed && !creations && !births,"Malformed output cannot create native ped or announce constructor");
    model_ready=false;
    check(step(COMMAND_CREATE_CHAR,creation,true)==StepResult::yielded && !creations && !births,"Real model-loading wait cannot fabricate constructor acceptance or force source progress");model_ready=true;
    check(step(COMMAND_CREATE_CHAR,creation,true)==StepResult::continued && creations==1 && births==1 &&
        intents.state(actor) && intents.state(actor)->current==0,"Actual SCM CREATE_CHAR acceptance feeds real source intent initialization");
    check(friend_births==1,"Accepted creation retires the old player relationship receipt");
    check(step(COMMAND_SET_CHAR_RUNNING,{actor.handle,2})==StepResult::continued && intents.state(actor)->running &&
        step(COMMAND_SET_CHAR_RUNNING,{actor.handle,0})==StepResult::continued && !intents.state(actor)->running,"Actual original running setter feeds accepted nonzero/zero source flags without replacing objective NONE");
    OriginalRouteTable table;check(table.add(7,{0,0,0}) && table.add(7,{10,0,0}),"Source route fixture");
    auto source_route=intents.follow(actor,7,1,0,table);check(intents.accepted(source_route),"Real constructor permits a reviewed route intent");
    const auto revision=intents.state(actor)->revision;
    refuse=true;
    check(step(COMMAND_CREATE_CHAR,creation,true)==StepResult::unsupported && births==1 && intents.state(actor)->revision==revision,"Rejected native creation cannot invent a new source generation");
    check(step(COMMAND_DELETE_CHAR,{actor.handle})==StepResult::unsupported && !retirements && intents.state(actor),"Rejected deletion preserves actual source actor state");
    refuse=false;
    const auto before_identity=identities;
    check(step(COMMAND_DELETE_CHAR,{actor.handle})==StepResult::continued && retirements==1 && !intents.state(actor) &&
        identities==before_identity+1,"Successful native deletion retires exact source identity; no post-delete pool poll");
    check(step(COMMAND_CREATE_CHAR,creation,true)==StepResult::continued && births==2 && intents.state(actor)->revision>revision,"New accepted constructor cannot resurrect an old source intent revision");
    auto old=intents.follow(actor,7,1,0,table);check(intents.accepted(old),"Known source route before complete native handle/address wrap");
    const auto old_revision=intents.state(actor)->revision;alive=false;
    check(step(COMMAND_CREATE_CHAR,creation,true)==StepResult::continued && intents.state(actor)->current==0 &&
        intents.state(actor)->revision>old_revision && !intents.accepted(old),"Real source constructor authority handles even identical pool handle/address after generation wrap");
    const auto before_unknown=unknown_goals;
    check(step(COMMAND_SET_CHAR_OBJ_WAIT_ON_FOOT,{actor.handle})==StepResult::continued &&
        unknown_goals==before_unknown+1 && !intents.state(actor),"Accepted but unprojected source objective revokes constructor NONE rather than preserving stale source goal knowledge");
    check(!intents.route_intent(actor) && step(COMMAND_CREATE_CHAR,creation,true)==StepResult::continued && intents.state(actor),"Only another actual source constructor can restore birth authority; an empty task poll cannot");
    check(step(COMMAND_SET_CHAR_OBJ_GOTO_COORD_ON_FOOT,{actor.handle,entity_bits(70.f),entity_bits(80.f)})==StepResult::continued,"Old source walking objective before native pool reuse");
    const auto old_motions=motions;alive=false;
    check(step(COMMAND_CREATE_CHAR,creation,true)==StepResult::continued &&
        step(COMMAND_SET_CHAR_RUNNING,{actor.handle,1})==StepResult::continued && motions==old_motions,
        "Actual constructor clears old occupant's walking job; running new character cannot replay old destination after full generation wrap");
    throw_callback=true;adapter.diagnostic_event=[](const std::string&){throw std::runtime_error("injected logger failure");};
    const auto before_creations=creations;
    check(step(COMMAND_CREATE_CHAR,creation,true)==StepResult::continued && creations==before_creations+1 && !intents.state(actor),"Observer AND logger failure revoke source authority without replaying or rejecting an already accepted native creation");
    throw_callback=false;identity_available=false;actor={0x201,0x555500};const auto before_births=births;
    check(step(COMMAND_CREATE_CHAR,creation,true)==StepResult::continued && births==before_births && !intents.state(actor),"Missing live identity cannot be replaced with a fabricated source actor");
    {
        NativeAdapter projected(image);projected.world_ready=true;
        size_t effects=0,revocations=0;
        projected.execute=[&](const NativeCall& call){
            check(call.opcode==0x05CB && call.inputs==std::vector<int32_t>({27,42,-1}),"Source vehicle wrapper preserves the actual native entry packet");
            ++effects;NativeResult result;result.executed=result.native_entered=true;return result;
        };
        projected.source_actor_goal_unknown=[&](int32_t handle){check(handle==27,"Revoke only the accepted source actor");++revocations;};
        auto enter=[&](){auto vm=program(COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_DRIVER,{27,42},false);Thread thread;thread.ip=64;
            vm.backend=[&](Vm& target,Thread& th,uint16_t op){return projected.dispatch(target,th,op);};return vm.step(thread,0);};
        projected.execute_source_objective=[](uint16_t op,const NativeCall&,bool& known,const std::function<NativeResult()>&){
            check(op==COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_DRIVER,"Original opcode survives native task translation");
            known=true;NativeResult result;result.executed=true;return result;
        };
        check(enter()==StepResult::continued && effects==0 && revocations==0,"A literal ignored source setter preserves current goal and never invokes the native task");
        projected.execute_source_objective=[](uint16_t,const NativeCall&,bool& known,const std::function<NativeResult()>& once){
            auto result=once();known=true;return result;
        };
        check(enter()==StepResult::continued && effects==1 && revocations==0,"Accepted projected source goal is not revoked by old walking cleanup");
        projected.execute_source_objective=[](uint16_t,const NativeCall&,bool&,const std::function<NativeResult()>& once){return once();};
        check(enter()==StepResult::continued && effects==2 && revocations==1,"Unprojected accepted objectives retain the original revocation path");
        projected.execute_source_objective=[](uint16_t,const NativeCall&,bool&,const std::function<NativeResult()>& once){once();return once();};
        check(enter()==StepResult::failed && effects==3 && revocations==2,"A second executor invocation is terminal and cannot replay the already-entered task");
    }
    {
        NativeAdapter relinquished(image);relinquished.world_ready=true;
        OriginalRouteIntents tracked;check(tracked.constructed(actor,uint8_t(2)),"Known original mission actor before release");
        bool accepted=false;size_t releases=0;
        relinquished.execute=[&](const NativeCall& call){
            check(call.opcode==0x01C2,"Original release keeps native mission-cleanup command");
            NativeResult result;result.executed=result.native_entered=accepted;return result;
        };
        relinquished.source_actor_goal_unknown=[&](int32_t handle){++releases;tracked.untracked_objective(handle);};
        const auto release=[&](){auto vm=program(COMMAND_MARK_CHAR_AS_NO_LONGER_NEEDED,{actor.handle},false);Thread t;t.ip=64;
            vm.backend=[&](Vm& v,Thread& thread,uint16_t op){return relinquished.dispatch(v,thread,op);};return vm.step(t,0);};
        check(release()==StepResult::unsupported && tracked.state(actor) && releases==0,
            "Refused cleanup cannot retire original constructor provenance");
        accepted=true;
        check(release()==StepResult::continued && !tracked.state(actor) && releases==1,
            "Accepted MARK_CHAR_AS_NO_LONGER_NEEDED retires stale mission origin without deleting the native actor");
    }
    std::cout<<"PASS: actual source lifecycle, projected/ignored objective acceptance and no side-effect replay; engine mocked\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
