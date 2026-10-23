#include "lcs/audio.hpp"
#include "lcs/engine_dispatch.hpp"
#include "lcs/hud.hpp"
#include "lcs/native.hpp"
#include "lcs/threat_ranges.hpp"
#include "lcs/crouch_duration.hpp"
#include "lcs/gang_damage.hpp"
#include "ScriptCommands.h"
#include "audio_sample_ids.inc"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
using namespace lcs;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static int32_t bits(float value){int32_t result;std::memcpy(&result,&value,4);return result;}
static void integer(std::vector<uint8_t>& code,int32_t value){code.push_back(6);for(int i=0;i<4;++i)code.push_back(uint8_t(uint32_t(value)>>(8*i)));}
static Vm program(uint16_t opcode,const std::vector<int32_t>& args,size_t outputs=0,bool malformed=false){
    std::vector<uint8_t> code(64);code.push_back(uint8_t(opcode));code.push_back(uint8_t(opcode>>8));
    for(auto value:args)integer(code,value);
    for(size_t i=0;i<outputs;++i){if(malformed && i+1==outputs){code.push_back(204);code.push_back(0);}else code.push_back(uint8_t(12+i));}
    return Vm(std::move(code),64,64);
}
static Vm help(uint16_t opcode){
    std::vector<uint8_t> code(64);code.push_back(uint8_t(opcode));code.push_back(uint8_t(opcode>>8));
    const char label[8]={'V','I','C','2','_','1',0,0};code.insert(code.end(),label,label+8);
    return Vm(std::move(code),64,64);
}
struct RejectingOwner {
    StepResult dispatch(Vm& vm,Thread& t,uint16_t){(void)vm.parameter(t);t.diagnostic="Owned operation rejected after reading its handle";return StepResult::unsupported;}
};
static void verify_pickup_weapon_contract(ScmImage& image,const std::string& source) {
    {
        NativeAdapter pickup(image);pickup.world_ready=true;
        std::istringstream weapons(source);pickup.load_weapon_definitions(weapons);
        pickup.bind_model(274,346,"colt45-pickup",ModelKind::object);
        pickup.bind_model(277,349,"shotgun-pickup",ModelKind::object);
        std::optional<int32_t> native_model=-1;
        std::array<float,4> seen{};size_t scans=0;
        pickup.pickup_model_in_area=[&](const std::array<float,4>& sphere){
            ++scans;seen=sphere;return native_model;
        };
        auto scan=[&](Vm& vm,Thread& t){t.ip=64;vm.backend=[&](Vm& v,Thread& target,uint16_t op){return pickup.dispatch(v,target,op);};return vm.step(t,0);};
        const std::vector<int32_t> sphere{bits(1.125f),bits(-2.f),bits(3.f),bits(8.f)};
        auto command=program(COMMAND_GET_WEAPON_TYPE_FOR_PICKUP_IN_AREA,sphere,1);
        Thread no_pickup;check(scan(command,no_pickup)==StepResult::continued && no_pickup.locals[0]==-1 && scans==1 &&
            seen==std::array<float,4>{{1.125f,-2.f,3.f,8.f}},"1487 reads native sphere in original argument order and returns -1 on a proven empty scan");
        native_model=346;Thread pistol;check(scan(command,pistol)==StepResult::continued && pistol.locals[0]==17 && scans==2,
            "Native model is translated to original Colt45 weapon index, not SA weapon ID");
        native_model=349;Thread shotgun;check(scan(command,shotgun)==StepResult::continued && shotgun.locals[0]==19,
            "Distinct source weapon binding is retained");
        // The source model table binds these names to native object IDs.
        // Explicit source WeaponForModel special branches are HEALTH=37 and
        // ARMOUR=38, including the retail adrenaline->armour oddity.
        pickup.bind_named_model(601,"health",ModelKind::object);
        pickup.bind_named_model(602,"bodyarmour",ModelKind::object);
        pickup.bind_named_model(603,"adrenaline",ModelKind::object);
        native_model=601;Thread health;check(scan(command,health)==StepResult::continued && health.locals[0]==37,
            "1487 resolves a real health pickup's original special type");
        native_model=602;Thread armour;check(scan(command,armour)==StepResult::continued && armour.locals[0]==38,
            "1487 maps body armour to original weapon type 38");
        native_model=603;Thread adrenaline;check(scan(command,adrenaline)==StepResult::continued && adrenaline.locals[0]==38,
            "1487 maps adrenaline to ARMOUR, as original retail WeaponForModel does");
        // A native world-collision model is not a live pickup object even if
        // another resource happens to have the same familiar model name.
        pickup.bind_named_model(604,"health-world-collision",ModelKind::world_collision);
        native_model=604;Thread wrong_class;
        check(scan(command,wrong_class)==StepResult::unsupported && wrong_class.ip==64,
            "A non-object model binding is never interpreted as a health pickup");
        native_model=777;Thread unknown;unknown.locals[0]=121;
        check(scan(command,unknown)==StepResult::unsupported && unknown.ip==64 && unknown.locals[0]==121,
            "Unmapped live pickup cannot yield invented source weapon type");
        native_model.reset();Thread unavailable;check(scan(command,unavailable)==StepResult::unsupported && unavailable.ip==64,
            "Unavailable native pickup pool is not an empty world");
        native_model=346;auto malformed=program(COMMAND_GET_WEAPON_TYPE_FOR_PICKUP_IN_AREA,sphere,1,true);
        const auto before=scans;Thread bad;check(scan(malformed,bad)==StepResult::failed && scans==before && bad.ip==64,
            "Malformed output is rejected before native scan");
        auto invalid=sphere;invalid[3]=bits(-1.f);auto invalid_radius=program(COMMAND_GET_WEAPON_TYPE_FOR_PICKUP_IN_AREA,invalid,1);
        Thread negative_radius;check(scan(invalid_radius,negative_radius)==StepResult::unsupported && scans==before,
            "Negative radius never enters the native pool");
        invalid=sphere;invalid[0]=bits(std::numeric_limits<float>::quiet_NaN());
        auto nan=program(COMMAND_GET_WEAPON_TYPE_FOR_PICKUP_IN_AREA,invalid,1);Thread bad_coord;
        check(scan(nan,bad_coord)==StepResult::unsupported && scans==before,"NaN coordinates are not passed to native pickup scanner");
        pickup.world_ready=false;Thread no_world;check(scan(command,no_world)==StepResult::unsupported && scans==before,
            "Unverified mounted world blocks live pickup query");pickup.world_ready=true;
        pickup.bind_model(279,346,"duplicate-model",ModelKind::object);Thread ambiguous;
        check(scan(command,ambiguous)==StepResult::unsupported && ambiguous.ip==64,
            "Two original weapons on one donor model must not be assigned an arbitrary type");
        // A collided special pickup must not silently override a gun, either.
        NativeAdapter clash(image);clash.world_ready=true;
        std::istringstream clash_weapons(source);clash.load_weapon_definitions(clash_weapons);
        clash.bind_model(274,346,"colt45-pickup",ModelKind::object);
        clash.bind_named_model(346,"health",ModelKind::object);
        clash.pickup_model_in_area=[&](const auto&){return std::optional<int32_t>(346);};
        auto clashed=program(COMMAND_GET_WEAPON_TYPE_FOR_PICKUP_IN_AREA,sphere,1);
        clashed.backend=[&](Vm& v,Thread& thread,uint16_t op){return clash.dispatch(v,thread,op);};
        Thread collision;collision.ip=64;
        check(clashed.step(collision,0)==StepResult::unsupported && collision.ip==64,
            "Conflicting special and weapon donor models are not distinguishable by appearance");
    }
}
int main(int argc,char** argv){try{
    if(argc==3 && std::string(argv[1])=="--pickup-only") {
        std::ifstream original(argv[2]);
        check(original.good(),"Original LCS weapon definitions fixture missing");
        const std::string source((std::istreambuf_iterator<char>(original)),{});
        ScmImage image;verify_pickup_weapon_contract(image,source);
        std::cout<<"PASS: 1487 pickup scan model mapping and transactional failure contracts; gameExecuted=false\n";
        return 0;
    }
    check(argc==3,"Original audio directory and weapon definitions are required");ScmImage image;NativeAdapter native(image);
    native.world_ready=native.text_ready=true;NativeCall packet;size_t calls=0;bool executed=true;
    native.execute=[&](const NativeCall& call){packet=call;++calls;NativeResult result;result.executed=executed;result.condition=true;
        result.values={{bits(32.5f),bits(-14.f),bits(7.f),bits(271.25f)}};if(call.opcode==0x01F5)result.values[0]=777;if(call.opcode==0x0226)result.values[0]=99;return result;};
    auto step=[&](Vm& vm,Thread& t){t.ip=64;vm.backend=[&](Vm& v,Thread& thread,uint16_t op){return native.dispatch(v,thread,op);};return vm.step(t,0);};
    GangDamagePolicy damage;uintptr_t live_actor=0x123400;int32_t live_handle=77;size_t damage_changes=0;
    auto resolve_damage=[&](int32_t handle){return handle==live_handle?live_actor:uintptr_t(0);};
    native.set_gang_damage=[&](int32_t actor,int32_t gang,bool enabled){++damage_changes;return actor==live_handle && damage.set(actor,live_actor,gang,enabled);};
    for(int gang=0;gang<8;++gang) {
        auto disable=program(COMMAND_SET_CHAR_CAN_BE_DAMAGED_BY_MEMBERS_OF_GANG,{77,gang,0});Thread protected_actor;
        check(step(disable,protected_actor)==StepResult::continued,"Original gang permission must consume actor, gang and flag without forwarding SA's empty 0514 handler");
        check(damage.denies(live_actor,7+gang,true,resolve_damage),"Disabled gang weapon attacks must be denied before native health damage");
        check(!damage.denies(live_actor,7+gang,false,resolve_damage) && !damage.denies(live_actor,6,true,resolve_damage) &&
              !damage.denies(0x987600,7+gang,true,resolve_damage),"Permissions cannot grant invulnerability to vehicle/environment damage, civilians or another actor");
        auto enable=program(COMMAND_SET_CHAR_CAN_BE_DAMAGED_BY_MEMBERS_OF_GANG,{77,gang,1});Thread restored;
        check(step(enable,restored)==StepResult::continued && !damage.denies(live_actor,7+gang,true,resolve_damage),"Each permission restores independently");
    }
    check(damage.set(77,live_actor,1,false) && damage.set(77,live_actor,2,false) && damage.set(77,live_actor,1,true) &&
          !damage.denies(live_actor,8,true,resolve_damage) && damage.denies(live_actor,9,true,resolve_damage),"Restoring one gang must preserve every other permission bit");
    check(damage.set(77,live_actor,8,false) && damage.set(77,live_actor,8,true) && damage.denies(live_actor,15,true,resolve_damage) &&
          damage.denies(live_actor,9,true,resolve_damage),"Original byte mask cannot create a ninth permission bit even when explicitly enabled");
    live_handle=78;check(!damage.denies(live_actor,9,true,resolve_damage) && damage.size()==0,"Recycled pool generation at the same address must not inherit protection");
    check(damage.set(78,live_actor,0,false),"New actor may receive its own policy");live_actor=0x223400;
    check(!damage.denies(0x123400,7,true,resolve_damage) && damage.size()==0,"Changed actor pointer invalidates a stale handle's permission");
    check(!damage.set(78,0,0,false) && !damage.set(78,live_actor,-1,false),"Missing actor and undefined source shift must be rejected without policy mutation");
    native.world_ready=false;auto gated_damage=program(COMMAND_SET_CHAR_CAN_BE_DAMAGED_BY_MEMBERS_OF_GANG,{78,0,0});Thread damage_gate;
    const auto before_damage_gate=damage_changes;check(step(gated_damage,damage_gate)==StepResult::unsupported && damage_changes==before_damage_gate,"Gang permission is gated on the mounted world");native.world_ready=true;
    live_handle=77;auto missing_damage=program(COMMAND_SET_CHAR_CAN_BE_DAMAGED_BY_MEMBERS_OF_GANG,{99,0,0});Thread absent_actor;
    check(step(missing_damage,absent_actor)==StepResult::unsupported && absent_actor.ip==64,"Rejected actor policy keeps the exact original instruction for diagnosis");damage.clear();
    const auto initial_executor=native.execute;
    bool task_actor_exists=true,task_ok=true,task_entered=false;size_t original_tasks=0;
    native.execute=guard_native_executor([&](const NativeCall& call){
        packet=call;++calls;NativeResult result;
        if(call.opcode==0x056D){result.executed=true;result.condition=task_actor_exists;return result;}
        check(call.inputs==std::vector<int32_t>{77} && !call.outputs && !call.predicate,"Original native task keeps the actor and has no invented outputs");
        check((call.opcode==0x05DE && call.source_wander_direction && !call.source_wait_in_car) ||
              (call.opcode==0x05D0 && call.source_wait_in_car && !call.source_wander_direction),"Source task mode must bypass the original native random/NOP handler explicitly");
        ++original_tasks;result.executed=task_ok;result.native_entered=task_entered;result.diagnostic="Task constructor fixture rejection";return result;
    });
    for(int direction:{0,1,2,3,4,5,6,7,-1,8,1000}) {
        auto wander=program(COMMAND_CHAR_WANDER_DIR,{77,direction});Thread walker;
        check(step(wander,walker)==StepResult::continued && packet.source_wander_direction==direction,"Every explicit source wander octant and the original random-selector fallback reaches its own constructor policy");
    }
    auto wait_car=program(COMMAND_SET_CHAR_OBJ_WAIT_IN_CAR,{77});Thread passenger;
    check(step(wait_car,passenger)==StepResult::continued && packet.source_wait_in_car && packet.opcode==0x05D0,"Wait-in-car cannot be implemented by forwarding SA's empty legacy command");
    task_actor_exists=false;const auto before_removed_task=original_tasks;Thread removed_task;
    check(step(wait_car,removed_task)==StepResult::unsupported && removed_task.ip==64 && original_tasks==before_removed_task,"Removed task actor is rejected before allocating or assigning a native task");task_actor_exists=true;
    task_ok=false;Thread before_entry;
    check(step(wait_car,before_entry)==StepResult::unsupported && before_entry.ip==64,"Pre-entry task rejection preserves the original instruction");
    task_entered=true;Thread after_entry;
    check(step(wait_car,after_entry)==StepResult::failed && after_entry.ip==64 && after_entry.diagnostic.find("replay rejected after entry")!=std::string::npos,
          "A fault after task construction/assignment is retained for repair instead of being replayed and duplicating native tasks");
    native.execute=initial_executor;
    std::array<int32_t,4> sticks{{-128,127,-43,61}};
    bool object_exists=true,object_targettable=false;size_t target_mutations=0;
    std::array<uint8_t,2> next_components{};
    native.execute=[&](const NativeCall& call){
        packet=call;++calls;NativeResult result;result.executed=executed;
        if(!executed)return result;
        if(call.opcode==0x0494)std::copy(sticks.begin(),sticks.end(),result.values.begin());
        else if(call.opcode==0x0454)result.values={{bits(-125.25f),bits(300.5f),bits(46.f)}};
        else if(call.opcode==0x0506)next_components={{uint8_t(call.inputs[1]),uint8_t(call.inputs[2])}};
        else if(call.opcode==0x03CA)result.condition=object_exists;
        else if(call.opcode==0x035D){object_targettable=call.inputs[1]!=0;++target_mutations;}
        else throw std::runtime_error("Unexpected native query in input/target tests");
        return result;
    };
    for(int pad:{0,1}) {
        auto axes=program(COMMAND_GET_POSITION_OF_ANALOGUE_STICKS,{pad},4);Thread input;
        check(step(axes,input)==StepResult::continued && packet.opcode==0x0494 && packet.inputs==std::vector<int32_t>{pad} && packet.outputs==4,
              "Analogue query retains pad identity and every native signed axis");
        for(size_t axis=0;axis<sticks.size();++axis)check(input.locals[axis]==sticks[axis],"Analogue axes must remain integers in left-X/Y then right-X/Y order");
        sticks={{91,-105,0,-1}};Thread moved;
        check(step(axes,moved)==StepResult::continued && moved.locals[0]==91 && moved.locals[3]==-1,"Controller reads cannot cache or synthesize prior input");
        const auto before=calls;auto malformed=program(COMMAND_GET_POSITION_OF_ANALOGUE_STICKS,{pad},4,true);Thread bad;
        check(step(malformed,bad)==StepResult::failed && calls==before && bad.ip==64,"Fourth analogue output is validated before reading the engine");
        executed=false;Thread rejected;rejected.locals={{11,22,33,44}};
        check(step(axes,rejected)==StepResult::unsupported && rejected.ip==64 && rejected.locals[0]==11 && rejected.locals[3]==44,"Rejected controller read cannot overwrite any original output");executed=true;
    }
    for(int pad:{-1,2,999}) {
        const auto before=calls;auto axes=program(COMMAND_GET_POSITION_OF_ANALOGUE_STICKS,{pad},4);Thread bad;
        check(step(axes,bad)==StepResult::unsupported && calls==before && bad.ip==64,"Invalid controller selector cannot reach the native pad array");
    }
    auto debug_camera=program(COMMAND_GET_DEBUG_CAMERA_COORDINATES,{},3);Thread camera;
    check(step(debug_camera,camera)==StepResult::continued && packet.opcode==0x0454 && packet.inputs.empty() && packet.outputs==3 &&
          camera.locals[0]==bits(-125.25f) && camera.locals[1]==bits(300.5f) && camera.locals[2]==bits(46.f),"Debug-camera query preserves its actual source, coordinate order and float bits");
    const auto before_camera=calls;auto bad_camera=program(COMMAND_GET_DEBUG_CAMERA_COORDINATES,{},3,true);Thread invalid_camera;
    check(step(bad_camera,invalid_camera)==StepResult::failed && calls==before_camera,"All debug camera outputs must validate before engine access");
    for(const auto selectors:{std::array<int32_t,2>{{-2,-1}},std::array<int32_t,2>{{0,5}},std::array<int32_t,2>{{258,-257}}}) {
        auto components=program(COMMAND_SET_CAR_MODEL_COMPONENTS,{-9876,selectors[0],selectors[1]});Thread extras;
        check(step(components,extras)==StepResult::continued && packet.opcode==0x0506 && packet.inputs==std::vector<int32_t>({-9876,selectors[0],selectors[1]}) &&
              next_components==std::array<uint8_t,2>{{uint8_t(selectors[0]),uint8_t(selectors[1])}},"Both retail component handlers ignore the first model operand and preserve byte truncation, including reset/none sentinels");
    }
    auto target=program(COMMAND_MAKE_OBJECT_TARGETTABLE,{501});Thread enable;
    check(step(target,enable)==StepResult::continued && object_targettable && target_mutations==1 && packet.opcode==0x035D && packet.inputs==std::vector<int32_t>({501,1}),
          "Original one-operand targettable command enables the live native object instead of consuming another original operand");
    object_exists=false;Thread deleted;const auto before_missing_object=calls;
    check(step(target,deleted)==StepResult::unsupported && deleted.ip==64 && calls==before_missing_object+1 && target_mutations==1,
          "A removed object is rejected by its real native pool predicate before any target mutation");
    executed=false;Thread unavailable_target;check(step(target,unavailable_target)==StepResult::unsupported && target_mutations==1,"Failed existence query cannot enable an unchecked object");executed=true;
    native.world_ready=false;Thread gated;const auto before_gate=calls;
    check(step(debug_camera,gated)==StepResult::unsupported && calls==before_gate,"Camera/input/object adapters retain mounted-world gating");native.world_ready=true;
    native.execute=initial_executor;
    struct Node {uint16_t original,native;size_t outputs;bool nth;};
    const Node nodes[]={{COMMAND_GET_CLOSEST_CAR_NODE,0x02C1,3,false},{COMMAND_GET_CLOSEST_CHAR_NODE,0x02C0,3,false},
        {COMMAND_GET_NTH_CLOSEST_CAR_NODE,0x04D3,3,true},{COMMAND_GET_CLOSEST_CAR_NODE_WITH_HEADING,0x03D3,4,false}};
    for(const auto& node:nodes){
        std::vector<int32_t> args{bits(-83.25f),bits(400.5f),bits(22.f)};if(node.nth)args.push_back(3);
        auto vm=program(node.original,args,node.outputs);Thread t;
        check(step(vm,t)==StepResult::continued && packet.opcode==node.native && packet.inputs==args,"Mounted path query must keep coordinates and one-based node selector");
        const std::array<int32_t,4> expected{{bits(32.5f),bits(-14.f),bits(7.f),bits(271.25f)}};
        for(size_t i=0;i<node.outputs;++i)check(t.locals[i]==expected[i],"Every native path output, including heading, must reach original variables");
        const auto before=calls;auto invalid=program(node.original,args,node.outputs,true);Thread bad;
        check(step(invalid,bad)==StepResult::failed && calls==before && bad.ip==64,"Malformed final output is rejected before any native path call");
        native.world_ready=false;auto gated=program(node.original,args,node.outputs);Thread held;
        check(step(gated,held)==StepResult::unsupported && calls==before && held.ip==64,"Unverified path/world cannot run native queries");native.world_ready=true;
        executed=false;Thread failed;check(step(vm,failed)==StepResult::unsupported && failed.ip==64 && failed.locals[0]==0,"Failed native query keeps instruction and original output variables");executed=true;
    }
    auto health=program(COMMAND_GET_PLAYER_HEALTH,{0},1);Thread hp;
    check(step(health,hp)==StepResult::continued && packet.opcode==0x0226 && packet.inputs==std::vector<int32_t>{777} && hp.locals[0]==99,"Player health must resolve the actual native ped instead of using player ID as a ped handle");
    const uint16_t stopped_players[]={COMMAND_LOCATE_STOPPED_PLAYER_ANY_MEANS_2D,COMMAND_LOCATE_STOPPED_PLAYER_ON_FOOT_2D,COMMAND_LOCATE_STOPPED_PLAYER_IN_CAR_2D,
        COMMAND_LOCATE_STOPPED_PLAYER_ANY_MEANS_3D,COMMAND_LOCATE_STOPPED_PLAYER_ON_FOOT_3D,COMMAND_LOCATE_STOPPED_PLAYER_IN_CAR_3D};
    const uint16_t stopped_chars[]={COMMAND_LOCATE_STOPPED_CHAR_ANY_MEANS_2D,COMMAND_LOCATE_STOPPED_CHAR_ON_FOOT_2D,COMMAND_LOCATE_STOPPED_CHAR_IN_CAR_2D,
        COMMAND_LOCATE_STOPPED_CHAR_ANY_MEANS_3D,COMMAND_LOCATE_STOPPED_CHAR_ON_FOOT_3D,COMMAND_LOCATE_STOPPED_CHAR_IN_CAR_3D};
    const uint16_t stopped_native[]={0x00EF,0x00F0,0x00F1,0x0101,0x0102,0x0103};
    for(size_t i=0;i<6;++i)for(bool player:{false,true}){
        std::vector<int32_t> args{player?0:301,bits(12.f),bits(13.f)};if(i>=3)args.push_back(bits(14.f));
        args.push_back(bits(1.f));args.push_back(bits(2.f));if(i>=3)args.push_back(bits(3.f));args.push_back(1);
        auto locate=program(uint16_t((player?stopped_players[i]:stopped_chars[i])|0x8000),args);Thread at;
        check(step(locate,at)==StepResult::continued && packet.opcode==stopped_native[i] && packet.predicate && !at.condition &&
              packet.inputs.size()==args.size() && packet.inputs[0]==(player?777:301),"Stopped locate family preserves actual native stop/occupancy/area predicate and resolves player identity");
        for(size_t j=1;j<args.size();++j)check(packet.inputs[j]==args[j],"Stopped locate radius/coordinates/draw marker must remain unchanged");
    }
    auto aim=program(COMMAND_SET_CHAR_OBJ_AIM_GUN_AT_CHAR,{301,302});Thread gun;
    check(step(aim,gun)==StepResult::continued && packet.opcode==0x0635 && packet.inputs==std::vector<int32_t>({301,302,-1}),"Aim is an indefinite native aim task, not a shooting or damage command");
    for(bool player:{false,true}){
        auto shoot=program(uint16_t((player?COMMAND_IS_PLAYER_SHOOTING_IN_AREA:COMMAND_IS_CHAR_SHOOTING_IN_AREA)|0x8000),
            {player?0:301,bits(-10.f),bits(-20.f),bits(30.f),bits(40.f),1});Thread shooting;
        check(step(shoot,shooting)==StepResult::continued && packet.opcode==0x02D6 && packet.inputs[0]==(player?777:301) &&
              packet.inputs[1]==bits(-10.f) && packet.inputs[4]==bits(40.f) && packet.inputs[5]==1 && packet.predicate && !shooting.condition,
              "Area gunfire predicate preserves real firing state, box, highlight and NOT; player IDs resolve to ped handles");
    }
    const std::array<int32_t,12> explosion_types{{0,1,2,4,5,6,7,-1,8,9,10,-1}};
    for(int32_t type=0;type<12;++type){
        auto explosion=program(COMMAND_ADD_EXPLOSION,{bits(1.f),bits(2.f),bits(3.f),type});Thread t;const auto before=calls;
        if(explosion_types[size_t(type)]<0)check(step(explosion,t)==StepResult::unsupported && calls==before && t.ip==64,"Exclusive helicopter explosion cannot be silently merged with a native kind");
        else check(step(explosion,t)==StepResult::continued && packet.opcode==0x020C && packet.inputs[3]==explosion_types[size_t(type)],"Vehicle explosion IDs account for SA's inserted weak-rocket kind");
    }
    auto any_explosion=program(uint16_t(COMMAND_IS_EXPLOSION_IN_AREA|0x8000),{-1,0,0,0,bits(2.f),bits(2.f),bits(2.f)});Thread negative;
    check(step(any_explosion,negative)==StepResult::continued && packet.inputs[0]==-1 && packet.predicate && !negative.condition,"Explosion ANY and original NOT preserve the real native area predicate");
    std::vector<std::pair<int32_t,bool>> policies;bool policy_ok=true;
    native.set_char_heed_threats=[&](int32_t actor,bool enabled){policies.emplace_back(actor,enabled);return policy_ok;};
    for(int flag:{0,1,12}){auto vm=program(COMMAND_SET_CHAR_HEED_THREATS,{301,flag});Thread t;check(step(vm,t)==StepResult::continued && policies.back()==std::make_pair(301,flag!=0),"Threat reactions update a live actor without replacing its objective");}
    auto player_threat=program(COMMAND_SET_PLAYER_HEED_THREATS,{0,1});Thread player_policy;
    check(step(player_threat,player_policy)==StepResult::continued && policies.back()==std::make_pair(777,true),"Player threat policy must resolve its actual ped handle");
    policy_ok=false;Thread held;const auto before_policy=calls;
    check(step(player_threat,held)==StepResult::unsupported && held.ip==64 && calls==before_policy+1,"Threat backend failure remains explicit at the original instruction");policy_ok=true;
    size_t frightened=0;native.set_char_crouch_when_threatened=[&](int32_t actor){check(actor==301,"Frightened crouch actor identity");++frightened;return true;};
    for(int flag:{0,1}){auto vm=program(COMMAND_SET_CHAR_CROUCH_WHEN_THREATENED,{301,flag});Thread t;check(step(vm,t)==StepResult::continued,"Original frightened-crouch setter consumes both operands");}
    check(frightened==2,"Original Script7 unconditionally enables scared crouch, including a zero second operand");
    std::vector<std::pair<int32_t,bool>> occlusion;bool occlusion_ok=true;
    native.set_char_threats_behind_objects=[&](int32_t actor,bool block){occlusion.emplace_back(actor,block);return occlusion_ok;};
    for(int value:{1,0}) {
        auto vm=program(COMMAND_SET_CHAR_IGNORE_THREATS_BEHIND_OBJECTS,{301,value});Thread t;t.condition=true;
        check(step(vm,t)==StepResult::continued && t.condition && occlusion.back()==std::make_pair(301,value!=0),
              "1383 reaches the per-actor native threat occlusion with the exact boolean (TRUE blocks on objects, FALSE sees through them)");
    }
    occlusion_ok=false;
    for(int value:{1,0}) {
        auto vm=program(COMMAND_SET_CHAR_IGNORE_THREATS_BEHIND_OBJECTS,{301,value});Thread t;
        check(step(vm,t)==StepResult::unsupported && t.ip==64,"1383 backend failure of either value stays unsupported at the original instruction");
    }
    native.world_ready=false;occlusion_ok=true;
    {auto vm=program(COMMAND_SET_CHAR_IGNORE_THREATS_BEHIND_OBJECTS,{301,0});Thread t;
     check(step(vm,t)==StepResult::unsupported,"1383 needs the mounted world");}
    native.world_ready=true;
    {auto vm=program(COMMAND_POLICE_RADIO_MESSAGE,{bits(1.f),bits(2.f),bits(3.f)});Thread t;t.condition=true;
     check(step(vm,t)==StepResult::continued && t.condition,"943 consumes its three floats; the retail queue it fills is never drained (no audible effect)");}
    {auto vm=program(COMMAND_POLICE_RADIO_MESSAGE,{bits(1.f),bits(std::numeric_limits<float>::infinity()),bits(3.f)});Thread t;
     check(step(vm,t)==StepResult::unsupported,"943 rejects a non-finite position");}
    check(native_crouch_duration(-1)==0 && native_crouch_duration(30000)==30000 && native_crouch_duration(65535)==65535 &&
          !native_crouch_duration(-2) && !native_crouch_duration(65536),"Persistent original crouch uses native zero timeout without truncating finite durations");
    auto turret=program(COMMAND_ATTACH_CHAR_TO_CAR,{301,501,bits(1.f),bits(2.f),bits(3.f),2,bits(80.f),24});Thread mounted;
    native.equivalent_entities=true;
    check(step(turret,mounted)==StepResult::continued && packet.opcode==0x0464 && packet.inputs[7]==23 && packet.inputs[5]==2 && packet.inputs[6]==bits(80.f),"Turret attachment translates the weapon (silenced Ingram -> SA silenced pistol, docs/weapons.md) while preserving facing and degree limit");
    native.equivalent_entities=false;Thread unavailable;const auto before_turret=calls;
    check(step(turret,unavailable)==StepResult::unsupported && unavailable.ip==64 && calls==before_turret,"Unmapped exclusive turret weapon cannot reach the native task");
    for(int wheel=0;wheel<4;++wheel){auto vm=program(COMMAND_BURST_CAR_TYRE,{501,wheel});Thread t;check(step(vm,t)==StepResult::continued && packet.inputs[1]==wheel && packet.opcode==0x04FE,"Tyre effects retain native bike aliases and individual wheel selection");}
    auto invalid_wheel=program(COMMAND_BURST_CAR_TYRE,{501,4});Thread wheel;const auto before_wheel=calls;
    check(step(invalid_wheel,wheel)==StepResult::unsupported && calls==before_wheel,"ANY is a query selector and cannot index the tyre mutation array");
    const uint16_t helps[]={COMMAND_PRINT_HELP_FOREVER_NO_BRIEF,COMMAND_PRINT_HELP_ALWAYS_NO_BRIEF,COMMAND_PRINT_HELP_FOREVER_ALWAYS_NO_BRIEF};
    for(auto op:helps){auto vm=help(op);Thread t;check(step(vm,t)==StepResult::continued && packet.text=="VIC2_1" && packet.suppress_previous_brief,"No-brief help must carry the original key and scoped brief policy");
        check(packet.opcode==(op==COMMAND_PRINT_HELP_ALWAYS_NO_BRIEF?0x03E5:0x0512),"Help permanence must match the original request");packet.encode();}
    auto normal=help(COMMAND_PRINT_HELP_FOREVER);Thread regular;
    check(step(normal,regular)==StepResult::continued && !packet.suppress_previous_brief,"No-brief policy cannot leak into the next ordinary help command");
    NativeCall illegal{0x009A,{4,7,0,0,0}};illegal.suppress_previous_brief=true;bool rejected=false;
    try{illegal.encode();}catch(const std::exception&){rejected=true;}check(rejected,"Brief policy cannot change unrelated native entity commands");
    std::optional<MissionMessage> subtitle;native.display_message=[&](const MissionMessage& message){subtitle=message;return true;};
    auto subtitle_code=help(COMMAND_PRINT_SUBTITLE_NOW_NO_BRIEF).memory;integer(subtitle_code,5000);integer(subtitle_code,1);
    Vm subtitle_vm(std::move(subtitle_code),64,64);Thread subtitle_thread;
    check(step(subtitle_vm,subtitle_thread)==StepResult::continued && subtitle && subtitle->key=="VIC2_1" && subtitle->duration==5000 && subtitle->flags==1 &&
          subtitle->subtitle && !subtitle->previous_brief && !subtitle->soon,"No-brief subtitles retain original preference policy, text key, timing and queue order");
    for(const auto op:{COMMAND_DOT_PRODUCT_2D,COMMAND_DOT_PRODUCT_3D}){
        const bool three=op==COMMAND_DOT_PRODUCT_3D;auto vm=program(op,three?std::vector<int32_t>{bits(2.f),bits(-3.f),bits(4.f),bits(5.f),bits(7.f),bits(-2.f)}:
            std::vector<int32_t>{bits(2.f),bits(-3.f),bits(5.f),bits(7.f)},1);Thread t;t.ip=64;
        check(vm.step(t,0)==StepResult::continued && t.locals[0]==bits(three?-19.f:-11.f),"Original vector products preserve signed float values and output order without an engine fallback");
    }
    auto query=program(COMMAND_GET_CHAR_WEAPON_IN_SLOT,{301,4},3);Thread inventory;
    const auto before_missing=calls;
    check(step(query,inventory)==StepResult::unsupported && calls==before_missing,"Missing original weapon definitions cannot fabricate an inventory");
    std::ifstream weapon_data(argv[2]);native.load_weapon_definitions(weapon_data);
    std::ifstream valid_data(argv[2]);std::string source((std::istreambuf_iterator<char>(valid_data)),{});
    const auto first_row=source.find("Unarmed");const auto first_end=source.find('\n',first_row);
    auto duplicate=source;duplicate.insert(source.find("ENDWEAPONDATA"),source.substr(first_row,first_end-first_row+1));
    for(const auto& broken:{source.substr(0,source.find("ENDWEAPONDATA")),duplicate}) {
        std::istringstream bad(broken);bool rejected=false;try{native.load_weapon_definitions(bad);}catch(...){rejected=true;}
        check(rejected,"Truncated or duplicate weapon data is rejected transactionally");
    }
    verify_pickup_weapon_contract(image,source);
    const auto original_execute=native.execute;
    std::unordered_map<int32_t,std::array<int32_t,3>> inventory_slots;
    native.native_weapon_slot=[](int32_t weapon)->std::optional<int32_t>{
        if(weapon==0 || weapon==1)return 0;if(weapon>=2 && weapon<=9)return 1;
        if(weapon==22 || weapon==24)return 2;if(weapon>=25 && weapon<=27)return 3;
        if(weapon==28 || weapon==29 || weapon==32)return 4;if(weapon==30 || weapon==31)return 5;
        if(weapon==34)return 6;if(weapon==33 || weapon==35 || weapon==37 || weapon==38)return 7; // converted weapon.dat: country rifle (M60) is heavy
        if(weapon==43)return 9;if(weapon==40)return 12;return 8;
    };
    native.execute=[&](const NativeCall& call){if(call.opcode!=0x04B8)return original_execute(call);packet=call;++calls;
        NativeResult result;result.executed=executed;const auto slot=inventory_slots.find(call.inputs[1]);
        const auto values=slot==inventory_slots.end()?std::array<int32_t,3>{{0,0,-1}}:slot->second;
        for(size_t i=0;i<3;++i)result.values[i]=values[i];return result;};
    inventory_slots[3]={{22,67,346}};
    check(step(query,inventory)==StepResult::continued && inventory.locals[0]==17 && inventory.locals[1]==67 && inventory.locals[2]==274 && packet.inputs[1]==3,"LCS pistol slot four resolves native slot three, real ammo and original model identity");
    auto malformed_inventory=program(COMMAND_GET_CHAR_WEAPON_IN_SLOT,{301,4},3,true);Thread malformed;const auto before_read=calls;
    check(step(malformed_inventory,malformed)==StepResult::failed && calls==before_read,"Validate every inventory destination before reading native state");
    executed=false;Thread read_fail;read_fail.locals[0]=123;
    check(step(query,read_fail)==StepResult::unsupported && read_fail.locals[0]==123 && read_fail.ip==64,"A failed inventory read preserves all outputs and original IP");executed=true;
    auto sniper=program(COMMAND_GET_CHAR_WEAPON_IN_SLOT,{301,9},3);Thread rifle;inventory_slots[7]={{34,12,358}};
    check(step(sniper,rifle)==StepResult::continued && rifle.locals[0]==28 && rifle.locals[1]==12 && rifle.locals[2]==285,"Original sniper slot nine maps to SA seven rather than native heavy weapons");
    native.equivalent_entities=true;
    auto give=program(COMMAND_GIVE_WEAPON_TO_CHAR,{301,32,300});Thread armed;check(step(give,armed)==StepResult::continued,"Give equivalent M60 through real native weapon request");
    inventory_slots[8]={{33,300,357}};
    auto heavy=program(COMMAND_GET_CHAR_WEAPON_IN_SLOT,{301,8},3);Thread machinegun;
    check(step(heavy,machinegun)==StepResult::continued && machinegun.locals[0]==32 && machinegun.locals[1]==300 && machinegun.locals[2]==289,"M60 maps to its own SA country-rifle slot in the heavy inventory slot");
    inventory_slots[8]={{35,1,359}};Thread ambiguous;
    // All original heavy weapons now share SA's heavy slot, as in LCS: the slot
    // holds one weapon and resolves to its own original identity.
    check(step(heavy,ambiguous)==StepResult::continued && ambiguous.locals[0]==30 && ambiguous.locals[1]==1 && ambiguous.locals[2]==287,"The shared heavy slot resolves the rocket launcher to its original identity");
    inventory_slots.clear();Thread empty;check(step(sniper,empty)==StepResult::continued && empty.locals[0]==0 && empty.locals[1]==0 && empty.locals[2]==-1,"Empty slot retains original unarmed identity and model");
    native.execute=original_execute;
    int32_t seen_actor=0,seen_type=-1;std::optional<bool> seen=false;
    native.can_see_dead_char=[&](int32_t actor,int32_t type){seen_actor=actor;seen_type=type;return seen;};
    auto bodies=program(COMMAND_CAN_CHAR_SEE_DEAD_CHAR,{301,17});Thread body;
    check(step(bodies,body)==StepResult::continued && !body.condition && seen_actor==301 && seen_type==19,"Dead visibility preserves original fireman type after SA inserted categories");
    seen=true;auto no_bodies=program(uint16_t(COMMAND_CAN_CHAR_SEE_DEAD_CHAR|0x8000),{301,17});Thread not_body;
    check(step(no_bodies,not_body)==StepResult::continued && !not_body.condition,"Dead visibility obeys original NOT using the actual predicate");
    seen.reset();Thread missing_body;check(step(bodies,missing_body)==StepResult::unsupported && missing_body.ip==64,"Unavailable dead scanner is held instead of reporting false success");
    uint16_t range=0;native.set_threat_range_multiplier=[&](uint16_t multiplier){range=multiplier;return true;};
    for(float value:{-2.f,0.f,1.9f,3.7f}){auto vm=program(COMMAND_SET_THREAT_REACTION_RANGE_MULTIPLIER,{bits(value)});Thread t;
        check(step(vm,t)==StepResult::continued && range==(value<1?1:uint16_t(value)),"Threat multiplier reproduces original uint16 truncation and minimum one");}
    auto invalid_range=program(COMMAND_SET_THREAT_REACTION_RANGE_MULTIPLIER,{int32_t(0x7fc00000)});Thread invalid_range_thread;
    check(step(invalid_range,invalid_range_thread)==StepResult::failed,"Invalid floating range never reaches native scanner fields");
    ThreatRangePolicy ranges;ranges.set(2);std::vector<ThreatRangeSnapshot> actors{{301,10,20,{{15,40}}},{302,11,21,{{20,30}}}};
    size_t range_writes=0;bool second_ok=false;
    const auto writer=[&](const ThreatRangeSnapshot& snapshot,const std::array<float,2>& wanted){++range_writes;if(snapshot.handle==302 && !second_ok)return false;
        for(auto& actor:actors)if(actor.handle==snapshot.handle)actor.ranges=wanted;return true;};
    check(!ranges.tick(actors,writer) && actors[0].ranges==std::array<float,2>{{30,80}} && actors[1].ranges[0]==20,"Partial policy pass commits only successful native actors");
    second_ok=true;check(ranges.tick(actors,writer) && actors[0].ranges[0]==30 && actors[1].ranges[0]==40,"Retry cannot multiply a previously successful actor again");
    const auto before_ranges=range_writes;check(ranges.tick(actors,writer) && range_writes==before_ranges,"Stable frames do not rewrite scanner ranges");
    actors[0].ranges={{7,9}};check(ranges.tick(actors,writer) && actors[0].ranges[0]==14,"External range edit becomes a baseline instead of being overwritten by stale state");
    actors[1].ped=50;actors[1].intelligence=60;actors[1].ranges={{5,6}};
    check(ranges.tick(actors,writer) && actors[1].ranges[0]==10,"Recycled native actor pointer does not inherit the previous actor's baseline");
    actors.push_back({303,70,80,{{18,12}}});check(ranges.tick(actors,writer) && actors[2].ranges[0]==36,"Newly streamed pedestrians receive the active policy");
    ranges.set(1);check(ranges.tick(actors,writer) && actors[0].ranges[0]==7 && actors[1].ranges[0]==5 && actors[2].ranges[0]==18 && !ranges.active(),"Reset restores live baselines, preserves external edits and releases all policy leases");
    AudioArchive archive(argv[1]);AudioAdapter audio(archive);HudBridge hud;
    auto assert_owner_failure=[&](Vm& vm,auto& owner,const char* diagnostic){
        Thread t;t.ip=64;t.locals[0]=1234;const auto before=calls;bool fallback=false;
        vm.backend=[&](Vm& v,Thread& thread,uint16_t op){if(const auto claimed=try_engine_adapter(owner,v,thread,op))return *claimed;fallback=true;return native.dispatch(v,thread,op);};
        check(vm.step(t,0)==StepResult::unsupported && !fallback && calls==before && t.ip==64 && t.locals[0]==1234,"A rejected owned command cannot fall through, replay or overwrite original state");
        check(t.diagnostic.find(diagnostic)!=std::string::npos,"The owning adapter's diagnosis must survive routing");
    };
    auto speech=program(COMMAND_PLAY_MISSION_AUDIO,{1});assert_owner_failure(speech,audio,"speech playback backend");
    Thread prior;prior.ip=64;prior.diagnostic="Original mission speech playback backend is missing";
    const auto existing_diagnostic=try_engine_adapter(audio,speech,prior,COMMAND_PLAY_MISSION_AUDIO);
    check(existing_diagnostic==StepResult::unsupported,"Explicit opcode ownership cannot disappear when the same diagnostic already exists and no operand was consumed");
    auto clock=program(COMMAND_FREEZE_ONSCREEN_TIMER,{1});assert_owner_failure(clock,hud,"clock freeze");
    auto creation=program(COMMAND_CREATE_CHAR,{4,7,bits(0.f),bits(0.f),bits(0.f)},1);RejectingOwner owner;
    assert_owner_failure(creation,owner,"after reading its handle");
    auto unhandled=program(COMMAND_GET_PLAYER_HEALTH,{0},1);Thread t;t.ip=64;bool reached_native=false;
    unhandled.backend=[&](Vm& v,Thread& thread,uint16_t op){if(const auto sound=try_engine_adapter(audio,v,thread,op))return *sound;if(const auto display=try_engine_adapter(hud,v,thread,op))return *display;reached_native=true;return native.dispatch(v,thread,op);};
    check(unhandled.step(t,0)==StepResult::continued && reached_native,"Unclaimed opcodes still reach their actual native owner");
    AudioAdapter spatial(archive);SpatialListener listener;std::vector<std::pair<size_t,AudioMix>> mixes;
    bool mixer_ok=true;size_t plays=0,stops=0;std::array<bool,2> speech_done{};
    spatial.play=[&](size_t slot,const AudioSample& sample){check(slot<2 && !sample.pcm.empty(),"Positioned speech must play real original PCM in a dialogue slot");++plays;return true;};
    spatial.stop=[&](size_t slot){check(slot<2,"Speech clear cannot stop NEWS/effects");++stops;};spatial.finished=[&](size_t slot){return speech_done.at(slot);};
    spatial.spatial_listener=[&]()->std::optional<SpatialListener>{return listener;};
    spatial.set_mix=[&](size_t slot,AudioMix mix){mixes.emplace_back(slot,mix);return mixer_ok;};
    const auto sample_name=AudioArchive::mission_names().front();
    auto sound_command=[&](uint16_t opcode,const std::vector<int32_t>& args,bool label=false){
        auto bytes=program(opcode,args).memory;
        if(label){check(sample_name.size()<=8,"Original speech label capacity");for(size_t i=0;i<8;++i)bytes.push_back(i<sample_name.size()?uint8_t(sample_name[i]):0);}
        Vm vm(std::move(bytes),64,64);Thread thread;thread.ip=64;vm.backend=[&](Vm& v,Thread& actor,uint16_t op){return spatial.dispatch(v,actor,op);};
        const auto result=vm.step(thread,0);if(result!=StepResult::continued)check(thread.ip==64,"Spatial failure retains original command");return result;
    };
    for(int32_t slot=1;slot<=2;++slot){
        check(sound_command(COMMAND_LOAD_MISSION_AUDIO,{slot},true)==StepResult::continued,"Load original speech PCM");
        check(sound_command(COMMAND_SET_MISSION_AUDIO_POSITION,{slot,bits(slot==1?32.f:-32.f),0,0})==StepResult::continued,"Store the requested real world speech position");
        check(sound_command(COMMAND_PLAY_MISSION_AUDIO,{slot})==StepResult::continued,"Start positioned original speech");
    }
    check(spatial.tick_spatial() && mixes.size()==2 && mixes[0].first==0 && mixes[1].first==1,"Both positioned speech slots update independently");
    check(mixes[0].second.volume==71 && mixes[0].second.pan==20 && mixes[1].second.volume==71 && mixes[1].second.pan==107,"Original 80m attenuation and pan table retain left/right asymmetry");
    listener.position={{32,0,0}};mixes.clear();check(spatial.tick_spatial() && mixes[0].second.volume==127 && mixes[0].second.pan==63,"Listener movement updates an already playing voice to near full centred volume");
    listener.position={};listener.right={{0,1,0}};mixes.clear();check(spatial.tick_spatial() && mixes[0].second.pan==63,"Pan uses the actual camera basis, including its orientation");
    listener.right={{1,0,0}};
    check(sound_command(COMMAND_SET_MISSION_AUDIO_POSITION,{1,bits(80.f),0,0})==StepResult::continued && spatial.tick_spatial() && mixes[mixes.size()-2].second.volume==0,"At the original range boundary speech is actually muted, without pretending playback finished");
    speech_done[0]=true;mixes.clear();
    check(spatial.tick_spatial() && mixes.size()==1 && mixes.front().first==1,
          "Finished distant dialogue cannot keep muting a live slot through stale spatial updates");
    speech_done[0]=false;
    mixer_ok=false;check(!spatial.tick_spatial() && plays==2,"Advisory mixer failure reports failure without replaying the original speech");mixer_ok=true;
    check(sound_command(COMMAND_SET_MISSION_AUDIO_POSITION,{1,int32_t(0x7fc00000),0,0})==StepResult::failed,"NaN positions are rejected before mutating voice state");
    check(sound_command(COMMAND_LOAD_MISSION_AUDIO,{1},true)==StepResult::continued && sound_command(COMMAND_PLAY_MISSION_AUDIO,{1})==StepResult::continued,"Loading a new line resets that slot to original two-dimensional speech");
    mixes.clear();check(spatial.tick_spatial() && mixes.size()==1 && mixes[0].first==1,"Reloading slot one cannot reset spatial position in slot two");
    check(sound_command(COMMAND_CLEAR_MISSION_AUDIO,{2})==StepResult::continued,"Clear original positioned speech");mixes.clear();
    check(spatial.tick_spatial() && mixes.empty() && stops==4,"Cleared dialogue leaves no stale mixer callback or NEWS side effect");
    {
        // 1486 SOUND_CAR_JERK one-shot: real SFX_SHAG_SUSPENSION PCM, source frequency/volume arithmetic, 35 m range.
        AudioAdapter jerk(archive);std::vector<AudioSample> played;std::vector<AudioMix> played_mixes;bool accept=true;
        std::optional<SpatialListener> listener_state=SpatialListener{};
        check(!jerk.play_car_jerk({{0,0,0}}),"Car jerk without a positioned PCM backend is a failure, not silent success");
        jerk.play_positioned_effect=[&](const AudioSample& sample,AudioMix mix){played.push_back(sample);played_mixes.push_back(mix);return accept;};
        check(!jerk.play_car_jerk({{0,0,0}}),"Car jerk without a spatial listener provider is a failure");
        jerk.spatial_listener=[&]{return listener_state;};listener_state.reset();
        check(!jerk.play_car_jerk({{0,0,0}}),"Car jerk without a spatial listener is a failure");
        listener_state=SpatialListener{};
        const auto base=archive.sample(uint32_t(SFX_SHAG_SUSPENSION));
        check(!base.pcm.empty() && base.frequency>0,"The authored bank-0 suspension sample exists");
        const auto volume_of=[&](const AudioSample& sample)->int{ // the volume that reproduces every sample
            for(int volume=0;volume<=127;++volume){bool same=true;for(size_t i=0;i+1<base.pcm.size() && same;i+=2){int16_t a,b;std::memcpy(&a,base.pcm.data()+i,2);std::memcpy(&b,sample.pcm.data()+i,2);same=b==int16_t(int(a)*volume/127);}if(same && volume>0)return volume;}
            return -1;};
        check(jerk.play_car_jerk({{3,0,0}}) && played.size()==1 && played[0].pcm.size()==base.pcm.size() && played[0].channels==base.channels,"A jerk inside range plays one real-sized PCM voice");
        const auto displacement=int64_t(played[0].frequency)-int64_t(base.frequency);
        check(std::llabs(displacement)<int64_t(base.frequency>>3),"Frequency is the sample frequency plus a displacement below frequency>>3");
        const int near_volume=volume_of(played[0]);
        check(near_volume>=55 && near_volume<=69 && played_mixes[0].volume==127,"Inside range/5 PCM volume is table[1]%15 + 55 and backend gain stays unity");
        check(played_mixes[0].pan==55,"A source to the listener's right is panned left by the native listener basis");
        played.clear();played_mixes.clear();check(jerk.play_car_jerk({{-3.f,0,0}}) && played.size()==1 && played_mixes[0].pan==71 && played_mixes[0].volume==127,"A source on the opposite side pans right with unity backend gain");
        played.clear();played_mixes.clear();listener_state->right={{0,1,0}};check(jerk.play_car_jerk({{3.f,0,0}}) && played.size()==1 && played_mixes[0].pan==63,"Pan follows listener orientation instead of fixed world X");
        listener_state->right={{1,0,0}};
        played.clear();played_mixes.clear();check(jerk.play_car_jerk({{28.f,0,0}}) && played.size()==1,"A jerk at 28 m still plays a faint voice");played.clear();played_mixes.clear();check(jerk.play_car_jerk({{34.5f,0,0}}) && played.empty(),"Where the uint8 ComputeVolume rounds to zero no voice is created");played.clear();played_mixes.clear();jerk.play_car_jerk({{28.f,0,0}});
        const int far_volume=volume_of(played[0]);check(far_volume>=0 && far_volume<near_volume,"The ComputeVolume squared fall-off lowers a distant jerk");
        played.clear();played_mixes.clear();check(jerk.play_car_jerk({{35.f,0,0}}) && played.empty(),"At the 35 m boundary the original rejects the one-shot without a voice, not an error");
        check(jerk.play_car_jerk({{0,60.f,0}}) && played.empty(),"Far one-shots are a genuine silent success");
        listener_state->position={{100,0,0}};check(jerk.play_car_jerk({{100,2,0}}) && played.size()==1,"Distance is measured from the real spatial listener");
        played.clear();played_mixes.clear();listener_state.reset();check(!jerk.play_car_jerk({{0,0,0}}) && played.empty(),"An unavailable spatial listener is a failure");listener_state=SpatialListener{};
        check(!jerk.play_car_jerk({{std::numeric_limits<float>::quiet_NaN(),0,0}}) && played.empty(),"A non-finite position is a failure");
        accept=false;check(!jerk.play_car_jerk({{1,0,0}}),"A refused positioned device is reported");accept=true;
        SourceAudioRandom shared(1);SourceAudioRandom same(1);
        for(int i=0;i<64;++i){shared.regenerate();same.regenerate();const auto a=shared.displacement(2500),b=same.displacement(2500);check(a==b && std::abs(a)<2500,"The shared audio random producer is deterministic and bounded by its seed");}
        check(shared.displacement(0)==0,"RandomDisplacement(0) is zero");
    }    std::cout<<"PASS: shared mission path/mira/HUD/math contracts, invalid output isolation and production adapter routing failures; gameExecuted=false\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
