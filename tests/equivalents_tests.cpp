#include "lcs/equivalents.hpp"
#include "lcs/road_area.hpp"
#include "lcs/hideout_capacity.hpp"
#include "ScriptCommands.h"
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <cstring>
using namespace lcs;
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void op(std::vector<uint8_t>& c,uint16_t n){c.push_back(uint8_t(n));c.push_back(uint8_t(n>>8));}
static void arg(std::vector<uint8_t>& c,int32_t n){c.push_back(6);for(int i=0;i<4;++i)c.push_back(uint8_t(uint32_t(n)>>(8*i)));}
int main(){try {
    ScmImage image;NativeAdapter native(image);std::istringstream profile("109 124 vinc_01 vmaff1 ped\n152 547 kuruma primo vehicle\n157 547 alias primo vehicle\n79 125 gang01 vmaff2 ped\n");
    bind_equivalents(native,read_equivalents(profile));native.world_ready=native.text_ready=true;
    std::istringstream weapon_profile("274 346 glock17 colt45 object\n");
    check(equivalent_model_class(read_equivalents(weapon_profile).at(0))==4,"Native weapon models must pass the WEAP class check, not an atomic world-object check");
    std::istringstream player_profile("0 0 player null ped\n1 7 cop lapd1 ped\n");
    const auto player_entities=read_equivalents(player_profile);
    check(equivalent_runtime_name(player_entities.at(0))=="player" && player_entities.at(0).native_name=="null","Native bootstrap replaces the IDE placeholder with the streamed player name; original profile stays unchanged");
    check(equivalent_runtime_name(player_entities.at(1))=="lapd1","Regular peds retain strict native name validation");
    std::vector<NativeCall> calls;bool ready=false;int32_t weapon=0,current_model=0,vehicle_model=547;
    native.native_model_class=[](int32_t id)->std::optional<uint8_t>{return id>=400 && id<=611?6:4;};
    native.execute=[&](const NativeCall& c){calls.push_back(c);NativeResult r;r.executed=true;r.condition=ready;r.values[0]=42;
        if(c.opcode==0x01B9)weapon=c.inputs[1];if(c.opcode==0x0470)r.values[0]=weapon;if(c.opcode==0x0441)r.values[0]=vehicle_model;if(c.opcode==0x0665)r.values[0]=current_model;return r;};
    const auto run=[&](std::vector<uint8_t> c,Thread& t){std::vector<uint8_t> bytes(64);bytes.insert(bytes.end(),c.begin(),c.end());Vm vm(std::move(bytes),64,64);vm.backend=[&](Vm& v,Thread& th,uint16_t n){return native.dispatch(v,th,n);};t={};t.ip=64;return vm.step(t,0);};
    Thread t;std::vector<uint8_t> c;op(c,COMMAND_LOAD_SPECIAL_CHARACTER);arg(c,1);c.insert(c.end(),{'v','i','n','c','_','0','1',0});
    check(run(c,t)==StepResult::continued && calls.back().opcode==0x0247 && calls.back().inputs==std::vector<int32_t>{124} && !calls.back().text,"Special name resolves to regular SA ped, never absent named streaming resource");
    std::string personality_name;native.set_char_personality=[&](int32_t actor,const std::string& name){personality_name=name;return actor==70;};
    c.clear();op(c,COMMAND_SET_CHAR_PERSONALITY);arg(c,70);arg(c,0);
    check(run(c,t)==StepResult::continued && personality_name=="STAT_COWARD","Original personality zero retains its own identity instead of becoming native personality zero, PLAYER");
    c.clear();op(c,COMMAND_SET_CHAR_PERSONALITY);arg(c,70);arg(c,41);
    check(run(c,t)==StepResult::continued && personality_name=="STAT_STD_MISSION","Last original personality resolves by its primary enum name");
    c.clear();op(c,COMMAND_SET_CHAR_PERSONALITY);arg(c,70);arg(c,42);
    check(run(c,t)==StepResult::unsupported,"Invalid original personality cannot select an unrelated native stats row");
    c.clear();op(c,COMMAND_HAS_SPECIAL_CHARACTER_LOADED);arg(c,1);
    check(run(c,t)==StepResult::continued && !t.condition && calls.back().opcode==0x0248,"Real false residency result remains false");
    c.clear();op(c,COMMAND_CREATE_CAR);arg(c,157);for(int i=0;i<3;++i)arg(c,0);c.push_back(12);
    check(run(c,t)==StepResult::yielded && t.ip==64,"Native model not loaded cannot create mission vehicle");ready=true;
    check(run(c,t)==StepResult::continued && t.locals[0]==42 && calls.back().inputs[0]==547,"Resident equivalent uses native factory and real handle");
    c.clear();op(c,COMMAND_GET_CAR_MODEL);arg(c,42);c.push_back(12);
    check(run(c,t)==StepResult::continued && t.locals[0]==157,"Two original vehicles sharing one donor keep factory identity");
    vehicle_model=400;
    check(run(c,t)==StepResult::continued && t.locals[0]==native_vehicle_model_base+400 && native.model(t.locals[0])==400,"Stock native traffic without a donor retains a usable disjoint model identity, including a reused vehicle handle");
    const auto native_alias=t.locals[0];
    c.clear();op(c,COMMAND_REQUEST_MODEL);arg(c,native_alias);
    check(run(c,t)==StepResult::continued && calls.back().inputs==std::vector<int32_t>{400},"Opaque native identity roundtrips through original streaming command");
    c.clear();op(c,COMMAND_CREATE_CAR);arg(c,native_alias);for(int i=0;i<3;++i)arg(c,0);c.push_back(12);
    check(run(c,t)==StepResult::continued && calls.back().inputs[0]==400,"Original CREATE_CAR can reuse a model returned for real native traffic");
    c.clear();op(c,COMMAND_GET_CAR_MODEL);arg(c,42);c.push_back(12);
    check(run(c,t)==StepResult::continued && t.locals[0]==native_alias,"Native traffic alias remains stable after creation");
    vehicle_model=346;
    check(run(c,t)==StepResult::unsupported && t.ip==64,"A weapon/object cannot masquerade as an unmatched vehicle model");
    vehicle_model=547;
    c.clear();op(c,COMMAND_SET_CURRENT_CHAR_WEAPON);arg(c,42);arg(c,32);
    check(run(c,t)==StepResult::continued && weapon==31,"LCS M60 uses existing native M4 weapon type");
    c.clear();op(c,COMMAND_HAS_CHAR_BEEN_DAMAGED_BY_WEAPON);arg(c,70);arg(c,46);
    check(run(c,t)==StepResult::continued && calls.back().opcode==0x031D && calls.back().inputs==std::vector<int32_t>{70,56},"Original ANYMELEE damage sentinel translates to the independent native sentinel, never an equipable weapon");
    c.clear();op(c,COMMAND_HAS_CAR_BEEN_DAMAGED_BY_WEAPON);arg(c,43);arg(c,41);
    check(run(c,t)==StepResult::continued && calls.back().inputs==std::vector<int32_t>{43,51},"Original explosion damage category retains its actual native category");
    c.clear();op(c,COMMAND_SET_CHAR_OBJ_KILL_PLAYER_ANY_MEANS);arg(c,70);arg(c,0);
    check(run(c,t)==StepResult::continued && calls.back().opcode==0x05E2 && calls.back().inputs==std::vector<int32_t>{70,42},"Combat resolves the player target without replacing the attacker handle");
    c.clear();op(c,COMMAND_SET_CHAR_OBJ_GOTO_PLAYER_ON_FOOT);arg(c,70);arg(c,0);
    check(run(c,t)==StepResult::continued && calls.back().opcode==0x05D9 && calls.back().inputs==std::vector<int32_t>{70,42,-1,0x3f800000},"Actor follows the live player, using native continuous tracking and the original approach radius");
    c.clear();op(c,COMMAND_SET_CHAR_OBJ_WAIT_ON_FOOT);arg(c,70);
    check(run(c,t)==StepResult::continued && calls.back().opcode==0x05BA && calls.back().inputs==std::vector<int32_t>{70,-1},"Waiting creates a real indefinite native stand-still task");
    c.clear();op(c,COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_DRIVER);arg(c,70);arg(c,43);
    check(run(c,t)==StepResult::continued && calls.back().opcode==0x05CB && calls.back().inputs==std::vector<int32_t>{70,43,-1},"Driver objective retains actual actor and vehicle handles without a forced warp");
    c.clear();op(c,COMMAND_GET_CURRENT_CHAR_WEAPON);arg(c,42);c.push_back(12);
    check(run(c,t)==StepResult::continued && t.locals[0]==32,"Donor weapon returns original alias last equipped by this actor");
    c.clear();op(c,COMMAND_CLEAR_THREAT_FOR_PED_TYPE);arg(c,7);arg(c,1|32768);
    check(run(c,t)==StepResult::continued && calls[calls.size()-2].opcode==0x0747 && calls.back().inputs==std::vector<int32_t>{4,7,15},"Threat flags update real native relationships");
    c.clear();op(c,COMMAND_CLEAR_CHAR_THREAT_SEARCH);arg(c,42);
    check(run(c,t)==StepResult::continued && calls.back().opcode==0x077C && calls.back().inputs==std::vector<int32_t>{42,4},"Clearing per-character threats changes the actor's real relationships");
    int32_t crouch_duration=-1;native.set_char_crouch=[&](int32_t actor,bool enabled,int32_t duration){crouch_duration=duration;return actor==42 && enabled;};
    c.clear();op(c,COMMAND_SET_CHAR_CROUCH);arg(c,42);arg(c,1);arg(c,30000);
    check(run(c,t)==StepResult::continued && crouch_duration==30000,"Crouch backend receives the original duration and actor");
    native.set_char_crouch=[](int32_t,bool,int32_t){return false;};
    check(run(c,t)==StepResult::unsupported,"An unavailable native crouch task cannot silently succeed");
    const auto previous_executor=native.execute;bool member=false,group_exists=true;float follower_x=31.0f;
    native.execute=[&](const NativeCall& call){auto result=previous_executor(call);
        if(call.opcode==0x07AF)result.values[0]=50;
        if(call.opcode==0x07FD)result.condition=group_exists;
        if(call.opcode==0x0631)member=call.inputs==std::vector<int32_t>{50,70};
        if(call.opcode==0x06EE)result.condition=member && call.inputs==std::vector<int32_t>{70,50};
        if(call.opcode==0x00A0){result.values={};if(call.inputs[0]==70)std::memcpy(&result.values[0],&follower_x,4);}
        return result;
    };
    c.clear();op(c,COMMAND_SET_PLAYER_AS_LEADER);arg(c,70);arg(c,0);
    check(run(c,t)==StepResult::continued && member && calls[calls.size()-2].opcode==0x0631 && calls.back().opcode==0x0940 && calls.back().inputs==std::vector<int32_t>{50,1},"Companion joins the existing native player group and enables passenger following without creating another leader or group");
    c.clear();op(c,COMMAND_IS_CHAR_IN_PLAYERS_GROUP);arg(c,70);arg(c,0);
    check(run(c,t)==StepResult::continued && !t.condition,"A companion more than 30m behind fails the original following condition even while still in the native group");
    follower_x=30.0f;check(run(c,t)==StepResult::continued && t.condition,"Original leader distance boundary is inclusive");
    member=false;check(run(c,t)==StepResult::continued && !t.condition,"Physical proximity cannot invent group membership");
    group_exists=false;check(run(c,t)==StepResult::unsupported,"Missing native player group is an explicit backend failure");
    native.execute=previous_executor;
    bool fighting=true;native.set_char_can_fight=[&](int32_t actor,bool value){fighting=value;return actor==70;};
    c.clear();op(c,COMMAND_SET_CHAR_IN_PLAYERS_GROUP_CAN_FIGHT);arg(c,70);arg(c,0);
    check(run(c,t)==StepResult::continued && !fighting,"Original fight policy reaches the correct native companion");
    size_t chats=0;native.play_wait_animation=[&](int32_t actor,int32_t state,int32_t duration,bool repeat){++chats;return (actor==70 || actor==71) && state==19 && duration==9000 && repeat;};
    c.clear();op(c,COMMAND_SET_CHARS_CHATTING);arg(c,70);arg(c,71);arg(c,9000);
    check(run(c,t)==StepResult::continued && chats==2 && calls.back().opcode==0x05BF && calls.back().inputs==std::vector<int32_t>{71,70,9000},"Paired chat retains both original actors, original animation and duration without using the unsupported SA opcode or confusing a stage with milliseconds");
    c.clear();op(c,COMMAND_SET_CHAR_OBJ_GOTO_COORD_ON_FOOT);arg(c,42);arg(c,0);arg(c,0);
    check(run(c,t)==StepResult::continued && calls.back().opcode==0x05D3 && calls.back().inputs[4]==4,"Native walking objective begins with walking movement");
    c.clear();op(c,COMMAND_SET_CHAR_RUNNING);arg(c,42);arg(c,1);
    check(run(c,t)==StepResult::continued && calls.back().opcode==0x05D3 && calls.back().inputs[4]==6,"Running changes the actor's active owned walking task");
    c.clear();op(c,COMMAND_UNDRESS_CHAR);arg(c,42);c.insert(c.end(),{'p','l','a','y','e','r','2',0});
    check(run(c,t)==StepResult::continued,"Original outfit transition starts");
    c.clear();op(c,COMMAND_DRESS_CHAR);arg(c,42);
    check(run(c,t)==StepResult::continued,"Original outfit commits only after native visibility/model/reset operations");
    c.clear();op(c,COMMAND_IS_PLAYER_WEARING);arg(c,0);c.insert(c.end(),{'p','l','a','y','e','r','2',0});
    check(run(c,t)==StepResult::continued && t.condition,"Committed equivalent outfit has its original identity");
    current_model=7;check(run(c,t)==StepResult::continued && !t.condition,"An externally changed native skin cannot still report the old original outfit");
    c.clear();op(c,COMMAND_SET_CHAR_RUNNING);arg(c,42);arg(c,0);
    check(run(c,t)==StepResult::continued && calls.back().opcode==0x056D,"Outfit objective reset releases the old walking task alias");
    // A failed visual request must not hide the actor or invent a completed
    // change. Exercise this with equivalents disabled, as in FirstMission.
    native.equivalent_entities=false;std::string actual_outfit="plr3";bool can_commit=false;
    native.begin_outfit=[](int32_t actor,const std::string& label){return actor==42 && label=="plr";};
    native.finish_outfit=[&](int32_t actor){if(actor!=42 || !can_commit)return false;actual_outfit="plr";return true;};
    native.current_outfit=[&](int32_t actor)->std::optional<std::string>{return actor==42?std::optional<std::string>(actual_outfit):std::nullopt;};
    c.clear();op(c,COMMAND_UNDRESS_CHAR);arg(c,42);c.insert(c.end(),{'b','a','d',0,0,0,0,0});
    const auto before_request=calls.size();
    check(run(c,t)==StepResult::unsupported,"Unknown native outfit fails before visibility changes");
    for(size_t i=before_request;i<calls.size();++i)check(calls[i].opcode!=0x0337,"Rejected outfit cannot hide the live player");
    c.clear();op(c,COMMAND_UNDRESS_CHAR);arg(c,42);c.insert(c.end(),{'p','l','r',0,0,0,0,0});
    check(run(c,t)==StepResult::continued && calls.back().opcode==0x0337 && calls.back().inputs[1]==0,"Actual outfit backend works without equivalent entity mode");
    c.clear();op(c,COMMAND_DRESS_CHAR);arg(c,42);
    check(run(c,t)==StepResult::unsupported && actual_outfit=="plr3" && calls.back().opcode==0x0337 && calls.back().inputs[1]==1,"Failed native commit restores visibility and retains the worn outfit");
    c.clear();op(c,COMMAND_IS_PLAYER_WEARING);arg(c,0);c.insert(c.end(),{'p','l','r',0,0,0,0,0});
    check(run(c,t)==StepResult::continued && !t.condition,"Pending clothes cannot satisfy the mission's worn-outfit check");
    can_commit=true;c.clear();op(c,COMMAND_DRESS_CHAR);arg(c,42);
    check(run(c,t)==StepResult::continued && actual_outfit=="plr","A pending native change can finish after a load retry");
    c.clear();op(c,COMMAND_IS_PLAYER_WEARING);arg(c,0);c.insert(c.end(),{'p','l','r',0,0,0,0,0});
    check(run(c,t)==StepResult::continued && t.condition,"Mission outfit identity follows the actual committed skin");
    native.begin_outfit={};native.finish_outfit={};native.current_outfit={};native.equivalent_entities=true;
    bool repeated=false;native.play_wait_animation=[&](int32_t actor,int32_t state,int32_t duration,bool repeat){repeated=repeat;return actor==42 && state==72 && duration==-1;};
    c.clear();op(c,COMMAND_SET_CHAR_WAIT_STATE_REPEAT);arg(c,42);arg(c,72);arg(c,-1);
    check(run(c,t)==StepResult::continued && repeated,"Original repeat request reaches the shared animation backend");
    repeated=false;ready=false;
    check(run(c,t)==StepResult::continued && !repeated,"Repeating wait state retains the original missing-ped no-action behavior");
    ready=true;
    std::istringstream zones("zone\nIND, 0, 1, 2, 3, 5, 6, 7, 1\nZ, 3, 1, 2, 3, 5, 6, 7, 1\nend\n");native.load_original_zones(zones);
    native.execute=[](const NativeCall& call){NativeResult r;r.executed=true;if(call.opcode==0x01F5)r.values[0]=100;return r;};
    c.clear();op(c,COMMAND_IS_PLAYER_IN_ZONE);arg(c,0);c.insert(c.end(),{'I','N','D',0,0,0,0,0});
    check(run(c,t)==StepResult::continued && !t.condition,"Actual position outside original bounds is false");
    check(angled_road_contains({0,0,0,10,10,10,3},3,5,5) && !angled_road_contains({0,0,0,10,10,10,3},5,3,5),"Angled road test excludes points inside bbox but on wrong side");
    check(hideout_entry_count(0,1)==0 && hideout_entry_count(1,1)==4 && hideout_entry_count(2,4)==0 && hideout_entry_count(4,4)==4 && hideout_entry_count(3,-1)==3,"Native hideout entrance enforces original 1/4 limits; unowned garages retain their actual count");
    check(hideout_storage_slots(4,1)==1 && hideout_storage_slots(4,4)==4 && hideout_storage_slots(4,-1)==4,"Native hideout storage uses the same limit as the entrance");
    native.scripted_pad_state=[](int32_t selector)->std::optional<int32_t>{return selector==2?-87:0;};
    c.clear();op(c,COMMAND_GET_PAD_BUTTON_STATE);arg(c,2);c.push_back(12);
    check(run(c,t)==StepResult::continued && t.locals[0]==-87,"Scripted control action preserves the actual signed analogue reading");
    native.scripted_pad_state=[](int32_t)->std::optional<int32_t>{return {};};
    check(run(c,t)==StepResult::unsupported,"Failed native input read does not become an invented unpressed button");
    for(const auto* invalid:{"109 290 vinc_01 special01 ped\n","152 124 kuruma vmaff1 vehicle\n","274 1000 glock17 object01 object\n","109 124 a a ped\n109 125 b b ped\n"}) {
        bool rejected=false;try{std::istringstream s(invalid);read_equivalents(s);}catch(const std::exception&){rejected=true;}check(rejected,"Invalid native class/streaming slot/duplicate profile rejected");
    }
    native.equivalent_entities=true;native.named_special_characters=true;
    native.bind_named_model(262,"VINC_02",ModelKind::ped);
    const auto legacy_executor=previous_executor;bool special_request_ok=false;
    native.execute=[&](const NativeCall& call){auto r=legacy_executor(call);if(call.opcode==0x0247)r.executed=special_request_ok;return r;};
    c.clear();op(c,COMMAND_LOAD_SPECIAL_CHARACTER);arg(c,1);c.insert(c.end(),{'v','i','n','c','_','0','2',0});
    check(run(c,t)==StepResult::unsupported && native.model(109)==124,"Failed native request cannot change the logical special slot");
    special_request_ok=true;
    check(run(c,t)==StepResult::continued && native.model(109)==262 && calls.back().inputs==std::vector<int32_t>{262},"Requested gameplay character remaps the original special slot to its canonical converted native skin");
    c.clear();op(c,COMMAND_HAS_SPECIAL_CHARACTER_LOADED);arg(c,1);
    check(run(c,t)==StepResult::continued && calls.back().inputs==std::vector<int32_t>{262},"Residency follows actual loaded character, not previous donor");
    c.clear();op(c,COMMAND_LOAD_SPECIAL_CHARACTER);arg(c,1);c.insert(c.end(),{'m','i','s','s','i','n','g',0});
    check(run(c,t)==StepResult::unsupported && native.model(109)==262,"Missing original gameplay resource cannot become an unrelated actor");
    std::cout<<"PASS: native equivalents, real residency, converted named special-slot lifecycle and failed/missing resource rejection; gameExecuted=false\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
