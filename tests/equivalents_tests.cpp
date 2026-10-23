#include "lcs/equivalents.hpp"
#include "lcs/radar_sprites.hpp"
#include "lcs/road_area.hpp"
#include "lcs/hideout_capacity.hpp"
#include "ScriptCommands.h"
#include "radar_sprite_origins.hpp"
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <cstring>
using namespace lcs;
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void op(std::vector<uint8_t>& c,uint16_t n){c.push_back(uint8_t(n));c.push_back(uint8_t(n>>8));}
static void arg(std::vector<uint8_t>& c,int32_t n){c.push_back(6);for(int i=0;i<4;++i)c.push_back(uint8_t(uint32_t(n)>>(8*i)));}
static void arg_float(std::vector<uint8_t>& c,float n){int32_t bits;std::memcpy(&bits,&n,4);arg(c,bits);}
static uint32_t radar_creator(ScmImage& image,int32_t sprite,float x,float y,float z,uint16_t opcode=COMMAND_ADD_SPRITE_BLIP_FOR_COORD){
    const auto at=uint32_t(image.bytes.size());op(image.bytes,opcode);arg_float(image.bytes,x);arg_float(image.bytes,y);arg_float(image.bytes,z);arg(image.bytes,sprite);
    image.bytes.push_back(204);image.bytes.push_back(4);return at;
}
int main(int argc,char** argv){try {
    if(argc==2){
        const auto source=ScmImage::load(argv[1]);std::set<int32_t> contacts;
        for(uint32_t at=source.entry_point;uint64_t(at)+2<source.main_size;++at){
            const auto command=read16(source.bytes,at);
            if(command!=COMMAND_ADD_SPRITE_BLIP_FOR_CONTACT_POINT && command!=COMMAND_ADD_SHORT_RANGE_SPRITE_BLIP_FOR_CONTACT_POINT)continue;
            try {
                radar_origin_detail::Cursor c{source,at+2};std::array<int,3> locals;
                for(auto& local:locals)local=c.number().local;
                const auto sprite=c.number();
                if(std::all_of(locals.begin(),locals.end(),[](int v){return v>=0&&v<96;}) && sprite.constant && sprite.value>=31 && sprite.value<=40 &&
                   radar_origin_detail::controller_coordinates(source,at,locals))contacts.insert(sprite.value);
            }catch(...){continue;}
        }
        check(contacts.count(40)!=0,"Original Vincenzo controller must have a proven stable coordinate preamble, including its shared calls");
        std::cout<<"PASS: original MAIN controller coordinate proof for "<<contacts.size()<<" distinct contact icons; no script executed\n";
        return 0;
    }
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
    check(run(c,t)==StepResult::continued && weapon==33,"LCS M60 has its own native weapon type (country rifle) since v94");
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
    std::string weapon_event;native.diagnostic_event=[&](const std::string& m){weapon_event=m;};
    weapon=46;
    c.clear();op(c,COMMAND_GET_CURRENT_CHAR_WEAPON);arg(c,77);c.push_back(12);
    check(run(c,t)==StepResult::continued && t.locals[0]==0 && weapon_event.find("weapon 46 ")!=std::string::npos,"SA-only native weapon (parachute) is reported to the original script as unarmed and logged");
    weapon=-1;
    c.clear();op(c,COMMAND_GET_CURRENT_CHAR_WEAPON);arg(c,77);c.push_back(12);
    check(run(c,t)==StepResult::unsupported,"An invalid native weapon query result still blocks explicitly");
    weapon=32;native.diagnostic_event=nullptr;
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
    // Original radar sprites: collapsed onto SA icons by default, one native slot each with the LCS HUD.
    const auto blip_sprite=[&](int32_t original)->int32_t{
        std::vector<uint8_t> b;op(b,COMMAND_ADD_SPRITE_BLIP_FOR_COORD);arg(b,0);arg(b,0);arg(b,0);arg(b,original);b.push_back(12);
        Thread bt;if(run(b,bt)!=StepResult::continued)return -999;
        return calls.back().inputs.back();
    };
    native.unique_radar_sprites=false;
    check(blip_sprite(1)==2 && blip_sprite(19)==35 && blip_sprite(36)==37,"Default radar sprite mapping keeps the SA icons");
    native.unique_radar_sprites=true;
    check(blip_sprite(0)==0 && blip_sprite(1)==2 && blip_sprite(3)==4 && blip_sprite(19)==20 && blip_sprite(36)==37 && blip_sprite(40)==42 && blip_sprite(41)==63 && blip_sprite(62)==41,"With the LCS HUD unique source icons avoid SA's reserved waypoint slot");
    check(blip_sprite(63)==-999,"Radar sprites beyond the original table are refused");
    std::array<bool,64> mapped{};
    for(int32_t source=0;source<=62;++source){const auto slot=source_radar_sprite(source,true);check(slot>=0 && slot<64 && !mapped[size_t(slot)],"Unique LCS radar mapping is in-range and collision-free");mapped[size_t(slot)]=true;}
    check(source_radar_sprite(1,true)==2 && source_radar_sprite(2,true)==3 && source_radar_sprite(3,true)==4 && source_radar_sprite(62,true)==41,"SA center/map/north and waypoint slots retain their native identities");
    check(source_radar_sprite(-1,true)==-1 && source_radar_sprite(63,true)==-1,"Invalid source radar IDs have no native slot");
    native.unique_radar_sprites=false;

    // Saved blips contain only native handles. Recover a changed source sprite
    // only when a MAIN creator's output global, literal coordinates and flags
    // identify the same generation-checked owned blip unambiguously.
    ScmImage recovery_image;recovery_image.variable_space=64;recovery_image.entry_point=64;recovery_image.bytes.resize(64);
    const auto creator=radar_creator(recovery_image,40,120.0f,-55.0f,8.0f);
    op(recovery_image.bytes,COMMAND_ADD_SPRITE_BLIP_FOR_COORD);recovery_image.bytes.push_back(6);recovery_image.bytes.push_back(0);recovery_image.bytes.push_back(0); // truncated literal
    recovery_image.main_size=uint32_t(recovery_image.bytes.size());
    NativeAdapter recovery(recovery_image);recovery.unique_radar_sprites=true;recovery.world_ready=true;
    int32_t next_blip=90;recovery.execute=[&](const NativeCall&){NativeResult r;r.executed=true;r.values[0]=next_blip++;return r;};
    Vm recovery_vm(recovery_image);recovery_vm.backend=[&](Vm& v,Thread& th,uint16_t command){return recovery.dispatch(v,th,command);};
    Thread recovery_thread;recovery_thread.ip=creator;
    check(recovery_vm.step(recovery_thread,0)==StepResult::continued && read32(recovery_vm.memory,16)==90,"Synthetic MAIN creates an owned radar blip");
    RadarSpriteBlip saved_blip{{120.0f,-55.0f,8.0f},37,false,false};int32_t changed_sprite=-1;int set_calls=0;
    const auto read_saved=[&](int32_t handle)->std::optional<RadarSpriteBlip>{return handle==90?std::optional<RadarSpriteBlip>(saved_blip):std::nullopt;};
    const auto set_saved=[&](int32_t handle,int32_t sprite){if(handle!=90)return false;++set_calls;changed_sprite=sprite;return true;};
    check(recovery.recover_source_blip_sprites(recovery_vm,read_saved,set_saved)==1 && changed_sprite==42,"Legacy collapsed Vincenzo icon recovers from a literal MAIN creator; truncated candidates are ignored");

    saved_blip.position[0]+=1.0f;set_calls=0;
    check(recovery.recover_source_blip_sprites(recovery_vm,read_saved,set_saved)==0 && set_calls==0,"Coordinate mismatch cannot rewrite an owned blip");
    saved_blip.position[0]=120.0f;saved_blip.sprite=58;
    check(recovery.recover_source_blip_sprites(recovery_vm,read_saved,set_saved)==0 && set_calls==0,"Unrecognized custom sprite is preserved");
    saved_blip.sprite=37;recovery_vm.memory[16]=0xE7;
    check(recovery.recover_source_blip_sprites(recovery_vm,read_saved,set_saved)==0 && set_calls==0,"A creator whose output global no longer owns the handle cannot rewrite it");

    ScmImage ambiguous_image;ambiguous_image.variable_space=64;ambiguous_image.entry_point=64;ambiguous_image.bytes.resize(64);
    const auto first_creator=radar_creator(ambiguous_image,40,3.0f,4.0f,5.0f);
    const auto second_creator=radar_creator(ambiguous_image,41,3.0f,4.0f,5.0f);
    ambiguous_image.main_size=uint32_t(ambiguous_image.bytes.size());
    NativeAdapter ambiguous(ambiguous_image);ambiguous.unique_radar_sprites=true;ambiguous.world_ready=true;
    int32_t ambiguous_handle=100;ambiguous.execute=[&](const NativeCall&){NativeResult r;r.executed=true;r.values[0]=ambiguous_handle++;return r;};
    Vm ambiguous_vm(ambiguous_image);ambiguous_vm.backend=[&](Vm& v,Thread& th,uint16_t command){return ambiguous.dispatch(v,th,command);};
    Thread first_thread;first_thread.ip=first_creator;Thread second_thread;second_thread.ip=second_creator;
    check(ambiguous_vm.step(first_thread,0)==StepResult::continued && ambiguous_vm.step(second_thread,0)==StepResult::continued,"Synthetic MAIN runs two creators sharing an output global");
    RadarSpriteBlip ambiguous_live{{3.0f,4.0f,5.0f},37,false,false};int ambiguous_sets=0;
    const auto read_ambiguous=[&](int32_t handle)->std::optional<RadarSpriteBlip>{return handle==101?std::optional<RadarSpriteBlip>(ambiguous_live):std::nullopt;};
    const auto set_ambiguous=[&](int32_t,int32_t){++ambiguous_sets;return true;};
    check(ambiguous.recover_source_blip_sprites(ambiguous_vm,read_ambiguous,set_ambiguous)==0 && ambiguous_sets==0,"Different source sprites targeting the same live output global are rejected as ambiguous");
    // Named controller with constant locals, a shared GOSUB and an isolated
    // CALL frame. This is the production pattern used for persistent contacts.
    ScmImage controller;controller.variable_space=64;controller.entry_point=64;controller.bytes.resize(64);
    op(controller.bytes,COMMAND_START_NEW_SCRIPT);arg(controller.bytes,72);controller.bytes.push_back(0);
    op(controller.bytes,COMMAND_SCRIPT_NAME);for(char ch:std::string("CONTACT\0",8))controller.bytes.push_back(uint8_t(ch));
    for(int i=0;i<3;++i){op(controller.bytes,COMMAND_SET_LVAR_FLOAT);controller.bytes.push_back(uint8_t(12+i));arg_float(controller.bytes,i==0?120.f:i==1?-55.f:-100.f);}
    op(controller.bytes,COMMAND_GOSUB);const auto shared_label=controller.bytes.size()+1;arg(controller.bytes,0);
    op(controller.bytes,COMMAND_CALL);controller.bytes.insert(controller.bytes.end(),{0,0,3});const auto call_label=controller.bytes.size()+1;arg(controller.bytes,0);
    const auto local_creator=uint32_t(controller.bytes.size());op(controller.bytes,COMMAND_ADD_SPRITE_BLIP_FOR_CONTACT_POINT);
    controller.bytes.insert(controller.bytes.end(),{12,13,14});arg(controller.bytes,40);controller.bytes.insert(controller.bytes.end(),{204,4});
    op(controller.bytes,COMMAND_TERMINATE_THIS_SCRIPT);const auto shared=uint32_t(controller.bytes.size());op(controller.bytes,COMMAND_NOP);op(controller.bytes,COMMAND_RETURN);
    for(const auto label:{shared_label,call_label})for(unsigned i=0;i<4;++i)controller.bytes[label+i]=uint8_t(shared>>(8*i));
    controller.main_size=uint32_t(controller.bytes.size());
    NativeAdapter controller_adapter(controller);controller_adapter.unique_radar_sprites=true;controller_adapter.world_ready=true;
    controller_adapter.execute=[](const NativeCall&){NativeResult r;r.executed=true;r.values[0]=70;return r;};
    Vm controller_vm(controller);controller_vm.backend=[&](Vm& v,Thread& th,uint16_t command){return controller_adapter.dispatch(v,th,command);};
    Thread controller_thread;controller_thread.ip=local_creator;
    check(controller_vm.step(controller_thread,0)==StepResult::continued,"Controller creator gives the adapter real ownership of its output handle");
    RadarSpriteBlip grounded{{120.f,-55.f,12.f},37,true,false};int controller_sets=0;
    const auto read_controller=[&](int32_t handle)->std::optional<RadarSpriteBlip>{return handle==70?std::optional<RadarSpriteBlip>(grounded):std::nullopt;};
    const auto set_controller=[&](int32_t handle,int32_t sprite){++controller_sets;return handle==70&&sprite==42;};
    check(controller_adapter.recover_source_blip_sprites(controller_vm,read_controller,set_controller)==1 && controller_sets==1,"Stable controller locals recover contact identity; below-map Z is resolved by the native engine");
    auto unsafe=controller;unsafe.bytes.resize(shared);op(unsafe.bytes,COMMAND_SET_LVAR_FLOAT);unsafe.bytes.push_back(12);arg_float(unsafe.bytes,999.f);op(unsafe.bytes,COMMAND_RETURN);unsafe.main_size=uint32_t(unsafe.bytes.size());
    check(!radar_origin_detail::controller_coordinates(unsafe,local_creator,{0,1,2}),"A shared callee that overwrites a coordinate local invalidates the source proof");
    std::cout<<"PASS: native equivalents, real residency, converted named special-slot lifecycle and failed/missing resource rejection; gameExecuted=false\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
