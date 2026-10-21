#include "lcs/native.hpp"
#include "ScriptCommands.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
using namespace lcs;
static void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
static Vm program(uint16_t opcode,const std::vector<int32_t>& operands,size_t outputs=0,bool malformed=false) {
    std::vector<uint8_t> bytes(64);bytes.push_back(uint8_t(opcode));bytes.push_back(uint8_t(opcode>>8));
    for(auto n:operands){bytes.push_back(6);for(int b=0;b<4;++b)bytes.push_back(uint8_t(uint32_t(n)>>(b*8)));}
    for(size_t i=0;i<outputs;++i){if(malformed && i+1==outputs){bytes.push_back(204);bytes.push_back(0);}else bytes.push_back(uint8_t(12+i));}
    return Vm(std::move(bytes),64,64);
}
static bool close(float a,float b){return std::fabs(a-b)<.0002f;}
int main(){try{
    {
        ScmImage image;NativeAdapter adapter(image);adapter.world_ready=true;
        const std::vector<int32_t> position{entity_bits(10),entity_bits(20),entity_bits(7),entity_bits(90)};
        unsigned calls=0;bool entered_failure=false;
        adapter.execute=[&](const NativeCall& call){
            ++calls;check(call.source_entity_command==EntityCommand::restart_critical_mission && !call.opcode && call.inputs==position && !call.outputs && !call.predicate,
                          "Campaign failure dispatches four original operands to the real restart service");
            NativeResult r;r.executed=!entered_failure;r.native_entered=entered_failure;return r;
        };
        auto run=[&](Vm& vm,Thread& t){t.ip=64;vm.backend=[&](Vm& v,Thread& th,uint16_t op){return adapter.dispatch(v,th,op);};return vm.step(t,0);};
        auto vm=program(COMMAND_RESTART_CRITICAL_MISSION,position);Thread t;
        check(run(vm,t)==StepResult::continued && calls==1,"Full campaign accepts critical mission recovery, without restarting MAIN");
        entered_failure=true;t={};check(run(vm,t)==StepResult::failed && calls==2,"Entered restart failure cannot be resumed/replayed");
        auto bad=position;bad[0]=entity_bits(NAN);auto invalid=program(COMMAND_RESTART_CRITICAL_MISSION,bad);t={};
        check(run(invalid,t)==StepResult::unsupported && calls==2,"Non-finite restart rejected before native invocation");
    }
    {
        ScmImage image;NativeAdapter adapter(image);adapter.world_ready=true;
        bool active=false;int32_t bank=-1;
        adapter.start_credits=[&](int32_t group){bank=group;active=true;NativeResult r;r.executed=true;return r;};
        adapter.stop_credits=[&](){active=false;NativeResult r;r.executed=true;return r;};
        adapter.original_credits_finished=[&](){return !active;};
        auto dispatch=[&](Vm& vm,Thread& thread){thread.ip=64;vm.backend=[&](Vm& v,Thread& t,uint16_t op){return adapter.dispatch(v,t,op);};return vm.step(thread,0);};
        auto start=program(COMMAND_START_CREDITS,{2});Thread t;
        check(dispatch(start,t)==StepResult::continued && bank==2 && active,"1081 consumes the actual bank selector before the owned renderer starts");
        auto query=program(COMMAND_ARE_CREDITS_FINISHED,{});t={};check(dispatch(query,t)==StepResult::continued && !t.condition,"Credits predicate observes the source renderer, not native SA credits");
        auto stop=program(COMMAND_STOP_CREDITS,{});t={};check(dispatch(stop,t)==StepResult::continued && !active,"STOP retires the same credits state");
        t={};check(dispatch(query,t)==StepResult::continued && t.condition,"Stopped source credits are finished");
        adapter.start_credits=[](int32_t){return NativeResult{};};t={};
        check(dispatch(start,t)==StepResult::unsupported && !active,"Missing credits resources refuse before publication");
    }
    {
        // 1494 adapter: shared LastRandomPedId, real wait-state owners, receipts.
        ScmImage image;NativeAdapter adapter(image);adapter.world_ready=true;
        auto dispatch=[&](Vm& vm,Thread& thread){thread.ip=64;vm.backend=[&](Vm& v,Thread& t,uint16_t op){return adapter.dispatch(v,t,op);};return vm.step(thread,0);};
        const std::vector<int32_t> area{entity_bits(10),entity_bits(20),entity_bits(-10),entity_bits(-20)};
        auto unchecked=program(COMMAND_GET_RANDOM_CHAR_IN_AREA_NO_CHECKS,area,1);Thread t;
        check(dispatch(unchecked,t)==StepResult::unsupported,"1494 without the native backend is not a NOP");
        size_t calls=0;bool mission=false;std::optional<bool> waited;
        adapter.mission_script=[&](const Thread&){return mission;};
        adapter.random_char_unchecked=[&](const std::array<float,4>& box,bool cleanup,const auto& waiting,const auto& commit) {
            ++calls;check(box==std::array<float,4>{{10,20,-10,-20}} && cleanup==mission,"1494 forwards raw source area and mission-cleanup context");
            waited=waiting(27);NativeResult r;r.executed=true;
            if(*waited){r.values[0]=-1;return r;}
            check(commit(27),"Selection commits shared identity");r.values[0]=27;return r;
        };
        t={};check(dispatch(unchecked,t)==StepResult::continued && t.locals[0]==27 && calls==1 && waited==false,"Selected civilian handle is stored");
        int32_t excluded=-2;
        adapter.random_char_in_area=[&](const std::array<float,4>&,const std::array<int32_t,3>&,int32_t ex,bool)->std::optional<int32_t>{excluded=ex;return -1;};
        auto other=program(COMMAND_GET_RANDOM_CHAR_IN_AREA,{area[0],area[1],area[2],area[3],1,1,1},1);
        t={};check(dispatch(other,t)==StepResult::continued && excluded==27,"1494 updates the LastRandomPedId consumed by the other random-character query");
        t={};mission=true;check(dispatch(unchecked,t)==StepResult::continued && t.locals[0]==27 && calls==2,"1494 never excludes the previous LastRandomPedId");
        mission=false;
        // Script traffic wait (WAITSTATE_TRAFFIC_LIGHTS) is the adapter-held wait owner.
        adapter.execute=[&](const NativeCall&){NativeResult r;r.executed=true;r.condition=true;r.values[0]=1000;return r;};
        auto wait=program(COMMAND_SET_CHAR_WAIT_STATE,{27,1,0});t={};
        check(dispatch(wait,t)==StepResult::continued,"Traffic wait is produced through the original command");
        t={};check(dispatch(unchecked,t)==StepResult::continued && t.locals[0]==-1 && waited==true,"Ped in traffic wait state is excluded");
        auto clear=program(COMMAND_CLEAR_CHAR_WAIT_STATE,{27});t={};
        check(dispatch(clear,t)==StepResult::continued,"Wait state can be cleared");
        t={};check(dispatch(unchecked,t)==StepResult::continued && t.locals[0]==27 && waited==false,"Cleared wait state returns to WAITSTATE_FALSE");
        // Gesture/RIOT owner: the same runtime records play/clear mutate.
        bool gesture=false;
        adapter.play_wait_animation=[&](int32_t,int32_t,int32_t,bool){gesture=true;return true;};
        adapter.clear_wait_animation=[&](int32_t){gesture=false;return true;};
        const auto before=calls;t={};
        check(dispatch(unchecked,t)==StepResult::unsupported && calls==before,"Mounted gesture owner without its query refuses rather than assuming FALSE");
        adapter.wait_animation_active=[&](int32_t actor)->std::optional<bool>{return actor==27 && gesture;};
        auto pose=program(COMMAND_SET_CHAR_WAIT_STATE,{27,29,0});t={};
        check(dispatch(pose,t)==StepResult::continued && gesture,"RIOT wait state is held by the gesture runtime");
        t={};check(dispatch(unchecked,t)==StepResult::continued && t.locals[0]==-1 && waited==true,"Ped in gesture wait state is excluded");
        t={};check(dispatch(clear,t)==StepResult::continued && !gesture,"Gesture wait cleared");
        adapter.wait_animation_active=[](int32_t)->std::optional<bool>{return {};};
        adapter.random_char_unchecked=[&](const auto&,bool,const auto& waiting,const auto&) {
            NativeResult r;if(!waiting(27))r.diagnostic="wait-state authority unavailable";return r;
        };
        t={};check(dispatch(unchecked,t)==StepResult::unsupported,"Unavailable wait-state authority is unsupported, not FALSE");
        adapter.wait_animation_active=[](int32_t)->std::optional<bool>{return false;};
        adapter.random_char_unchecked=[&](const auto&,bool,const auto&,const auto& commit) {
            check(commit(42),"Failure fixture commits a new shared identity");NativeResult r;r.native_entered=true;r.diagnostic="partial promotion";return r;
        };
        t={};check(dispatch(unchecked,t)==StepResult::failed,"Entered promotion failure cannot be replayed or downgraded");
        t={};check(dispatch(other,t)==StepResult::continued && excluded==42,"Partial acceptance keeps the source LastRandomPedId store order");
        adapter.random_char_unchecked=[&](const auto&,bool,const auto&,const auto&){NativeResult r;r.executed=true;r.values[0]=27;return r;};
        t={};check(dispatch(unchecked,t)==StepResult::failed,"Handle without shared commit is a receipt mismatch");
        auto malformed=program(COMMAND_GET_RANDOM_CHAR_IN_AREA_NO_CHECKS,area,1,true);size_t probe=0;
        adapter.random_char_unchecked=[&](const auto&,bool,const auto&,const auto&){++probe;return NativeResult{};};
        t={};check(dispatch(malformed,t)!=StepResult::continued && !probe,"Malformed output refuses before selection effects");
        auto infinite=area;infinite[0]=entity_bits(std::numeric_limits<float>::infinity());
        auto bad=program(COMMAND_GET_RANDOM_CHAR_IN_AREA_NO_CHECKS,infinite,1);t={};
        check(dispatch(bad,t)==StepResult::failed && !probe,"Nonfinite area is rejected before native data access");
    }
    {
        // 738 adapter: loaded LCS navigation zone, three kind flags, shared LastRandomPedId, receipts.
        ScmImage image;NativeAdapter adapter(image);adapter.world_ready=true;
        std::istringstream zones("zone\nIND_ZON, 0, 300, 400, 50, -100, -200, -50, 1\nOTHER, 3, 0, 0, 0, 1, 1, 1, 1\nend\n");
        adapter.load_original_zones(zones);
        auto dispatch=[&](Vm& vm,Thread& thread){thread.ip=64;vm.backend=[&](Vm& v,Thread& t,uint16_t op){return adapter.dispatch(v,t,op);};return vm.step(thread,0);};
        auto zone_program=[&](const std::string& label,std::array<int32_t,3> kinds){
            std::vector<uint8_t> bytes(64);bytes.push_back(uint8_t(COMMAND_GET_RANDOM_CHAR_IN_ZONE));bytes.push_back(uint8_t(COMMAND_GET_RANDOM_CHAR_IN_ZONE>>8));
            for(size_t i=0;i<8;++i)bytes.push_back(i<label.size()?uint8_t(label[i]):uint8_t(0));
            for(auto n:kinds){bytes.push_back(6);for(int b=0;b<4;++b)bytes.push_back(uint8_t(uint32_t(n)>>(b*8)));}
            bytes.push_back(12);return Vm(std::move(bytes),64,64);
        };
        auto query=zone_program("IND_ZON",{1,0,1});Thread t;
        check(dispatch(query,t)==StepResult::unsupported,"738 without the native zone backend is not a NOP");
        size_t calls=0;int32_t seen_excluded=-2;bool seen_mission=true,control=false;bool commit_it=true;int32_t answer=284;
        adapter.mission_script=[&](const Thread&){return true;};
        adapter.char_in_control=[](int32_t handle)->std::optional<bool>{return handle==284;};
        adapter.random_char_in_zone=[&](const std::array<float,6>& zone,const std::array<int32_t,3>& kinds,int32_t excluded,bool mission,const auto& in_control,const auto& commit) {
            ++calls;seen_excluded=excluded;seen_mission=mission;
            check(zone==std::array<float,6>{{-100,-200,-50,300,400,50}},"738 forwards the loaded LCS zone box, normalized");
            check(kinds==std::array<int32_t,3>{{1,0,1}},"738 forwards the three raw kind flags");
            control=in_control(284).value_or(false)&&!in_control(5).value_or(true);
            NativeResult r;r.executed=true;r.values[0]=answer;if(answer>=0&&commit_it)check(commit(answer),"738 commits the shared identity once");
            return r;
        };
        t={};check(dispatch(query,t)==StepResult::continued && t.locals[0]==284 && calls==1 && seen_excluded==-1 && seen_mission && control,"738 stores the selected handle, passes mission context and the native control query");
        t={};check(dispatch(query,t)==StepResult::continued && seen_excluded==284,"738 excludes the previously stored LastRandomPedId on the next query");
        auto other_zone=program(COMMAND_GET_RANDOM_CHAR_IN_AREA,{entity_bits(10),entity_bits(20),entity_bits(-10),entity_bits(-20),1,1,1},1);
        int32_t area_excluded=-2;adapter.random_char_in_area=[&](const std::array<float,4>&,const std::array<int32_t,3>&,int32_t ex,bool)->std::optional<int32_t>{area_excluded=ex;return -1;};
        t={};check(dispatch(other_zone,t)==StepResult::continued && area_excluded==284,"738 and the area query share one LastRandomPedId");
        const auto before=calls;auto unknown=zone_program("NOWHERE",{1,0,1});t={};
        check(dispatch(unknown,t)==StepResult::unsupported && calls==before,"738 with an unknown zone label refuses before selection");
        auto wrong_type=zone_program("OTHER",{1,0,1});t={};
        check(dispatch(wrong_type,t)==StepResult::unsupported && calls==before,"738 only resolves navigation (type zero) zones");
        answer=-1;t={};check(dispatch(query,t)==StepResult::continued && t.locals[0]==-1,"738 absence is the genuine -1 sentinel");
        t={};check(dispatch(other_zone,t)==StepResult::continued && area_excluded==284,"Absence does not disturb LastRandomPedId");
        answer=300;commit_it=false;t={};check(dispatch(query,t)==StepResult::failed,"738 handle without the shared commit is a receipt mismatch");
        answer=-1;commit_it=true;
        adapter.random_char_in_zone=[&](const auto&,const auto&,int32_t,bool,const auto&,const auto& commit) {
            check(commit(7),"fixture commit");NativeResult r;r.native_entered=true;r.diagnostic="partial promotion";return r;
        };
        t={};check(dispatch(query,t)==StepResult::failed,"738 entered failure cannot be replayed or downgraded");
        t={};check(dispatch(other_zone,t)==StepResult::continued && area_excluded==7,"738 partial acceptance keeps the LastRandomPedId store order");
        adapter.random_char_in_zone=[&](const auto&,const auto&,int32_t,bool,const auto&,const auto&){NativeResult r;r.diagnostic="player moved";return r;};
        t={};check(dispatch(query,t)==StepResult::unsupported,"738 unavailable state refuses without storing");
        adapter.random_char_in_zone=[&](const auto&,const auto&,int32_t,bool,const auto&,const auto&){++calls;NativeResult r;r.executed=true;r.values[0]=-1;return r;};
        adapter.char_in_control=nullptr;const auto calls_before_control=calls;t={};
        check(dispatch(query,t)==StepResult::unsupported && calls==calls_before_control,"738 without the native control query refuses before selection");
    }
    {
        // 1486 adapter: native turn force then the SOUND_CAR_JERK consumer, both required.
        ScmImage image;NativeAdapter adapter(image);adapter.world_ready=true;
        auto dispatch=[&](Vm& vm,Thread& thread){thread.ip=64;vm.backend=[&](Vm& v,Thread& t,uint16_t op){return adapter.dispatch(v,t,op);};return vm.step(thread,0);};
        auto tilt=program(COMMAND_SET_CAR_TILTED_BY_CHAR,{7,27});Thread t;
        check(dispatch(tilt,t)==StepResult::unsupported,"1486 without its force and sound backends is not a NOP");
        std::vector<std::string> order;int random_value=500;bool vehicle_alive=true,person_alive=true,finite=true;
        adapter.live_ped_identity=[&](int32_t handle)->std::optional<ObjectiveActor>{if(handle==27 && person_alive)return ObjectiveActor{27,0x1000};return {};};
        adapter.execute=[&](const NativeCall& call){
            NativeResult r;r.executed=true;
            switch(call.opcode) {
            case 0x056E:r.condition=vehicle_alive && call.inputs[0]==7;return r;
            case 0x00AA:r.values[0]=entity_bits(finite?10.f:std::numeric_limits<float>::infinity());r.values[1]=entity_bits(20.f);r.values[2]=entity_bits(3.f);return r;
            case 0x0209:check(call.inputs==std::vector<int32_t>({0,1000}),"Random factor comes from the native 0..1000 generator");r.values[0]=random_value;return r;
            default:r.executed=false;return r;
            }
        };
        float seen_factor=0;int32_t seen_car=-1,seen_ped=-1;bool tilt_ok=true,tilt_entered=false,sound_ok=true;std::array<float,3> sound_at{};
        adapter.apply_car_tilt=[&](int32_t car,int32_t ped,float factor){order.push_back("tilt");seen_car=car;seen_ped=ped;seen_factor=factor;NativeResult r;r.executed=tilt_ok;r.native_entered=tilt_entered;if(!tilt_ok)r.diagnostic="refused";return r;};
        adapter.car_jerk_sound=[&](const std::array<float,3>& at){order.push_back("sound");sound_at=at;return sound_ok;};
        check(dispatch(tilt,t)==StepResult::continued && order==std::vector<std::string>({"tilt","sound"}) && seen_car==7 && seen_ped==27 && close(seen_factor,-1.0f) && sound_at==std::array<float,3>{{10.f,20.f,3.f}},"1486 applies the force first, then plays the jerk at the vehicle position, with the original random factor");
        for(const auto& row:std::vector<std::pair<int,float>>{{0,-0.8f},{1000,-1.2f},{250,-0.9f}}){order.clear();random_value=row.first;t={};dispatch(tilt,t);check(close(seen_factor,row.second),"Random 0..1000 maps linearly onto the original -0.8..-1.2");}
        random_value=500;order.clear();vehicle_alive=false;t={};
        check(dispatch(tilt,t)==StepResult::unsupported && order.empty(),"1486 with a missing vehicle refuses before any effect");vehicle_alive=true;
        person_alive=false;t={};check(dispatch(tilt,t)==StepResult::unsupported && order.empty(),"1486 with a stale character refuses before any effect");person_alive=true;
        finite=false;t={};check(dispatch(tilt,t)==StepResult::unsupported && order.empty(),"1486 with a non-finite vehicle position refuses");finite=true;
        random_value=1001;t={};check(dispatch(tilt,t)==StepResult::unsupported && order.empty(),"1486 with an invalid native random value refuses");random_value=500;
        tilt_ok=false;order.clear();t={};check(dispatch(tilt,t)==StepResult::unsupported && order==std::vector<std::string>({"tilt"}),"A force refused before entry produces no sound");
        tilt_entered=true;order.clear();t={};check(dispatch(tilt,t)==StepResult::failed && order==std::vector<std::string>({"tilt"}),"A force failing after entry is never replayed and produces no sound");
        tilt_ok=true;tilt_entered=false;sound_ok=false;order.clear();t={};
        check(dispatch(tilt,t)==StepResult::failed && order==std::vector<std::string>({"tilt","sound"}),"A sound failure after the force is an entered failure, not a fabricated success");
    }    {
        ScmImage image;NativeAdapter adapter(image);size_t calls=0;
        adapter.sniper_bullet_in_area=[&](const std::array<float,6>& box)->std::optional<bool>{
            ++calls;check(box==std::array<float,6>{{-1,-2,-3,1,2,3}},"899 normalizes XYZ independently before the actual sniper query");return true;
        };
        const std::vector<int32_t> bounds{entity_bits(1),entity_bits(2),entity_bits(3),entity_bits(-1),entity_bits(-2),entity_bits(-3)};
        auto dispatch=[&](Vm& vm,Thread& thread){thread.ip=64;vm.backend=[&](Vm& v,Thread& t,uint16_t op){return adapter.dispatch(v,t,op);};return vm.step(thread,0);};
        auto command=program(COMMAND_IS_SNIPER_BULLET_IN_AREA,bounds);Thread step;
        check(dispatch(command,step)==StepResult::unsupported && calls==0,"899 world gate prevents querying an unmounted native producer");adapter.world_ready=true;
        step={};check(dispatch(command,step)==StepResult::continued && step.condition && calls==1,"899 returns the native sniper predicate");
        auto negated=program(COMMAND_IS_SNIPER_BULLET_IN_AREA|0x8000,bounds);step={};
        check(dispatch(negated,step)==StepResult::continued && !step.condition && calls==2,"899 applies SCM negation exactly once");
        adapter.sniper_bullet_in_area=[](const auto&)->std::optional<bool>{return false;};step={};
        check(dispatch(command,step)==StepResult::continued && !step.condition,"No active sniper segment is a genuine false result");
        adapter.sniper_bullet_in_area=[](const auto&)->std::optional<bool>{return {};};step={};
        check(dispatch(command,step)==StepResult::unsupported,"Lost sniper producer cannot fabricate a false result");
        auto invalid_bounds=bounds;invalid_bounds[0]=entity_bits(std::numeric_limits<float>::infinity());
        auto invalid=program(COMMAND_IS_SNIPER_BULLET_IN_AREA,invalid_bounds);step={};
        check(dispatch(invalid,step)==StepResult::failed,"899 rejects nonfinite bounds before accessing native data");
    }
    std::array<uint8_t,0xA18+32> storage;storage.fill(0xCD);auto* entity=storage.data()+16;
    const std::array<int32_t,4> velocity{{77,entity_bits(50.f),entity_bits(-125.f),entity_bits(0.f)}};
    for(const auto action:{EntityCommand::car_move_speed,EntityCommand::car_turn_speed,EntityCommand::object_turn_speed}) {
        const auto before=storage;const auto mutation=entity_field_mutation(action,velocity.data(),4,entity,0xA18);
        check(mutation && apply_entity_field_mutation(entity,0xA18,*mutation),"A source vector must update native physical storage");
        std::array<float,3> actual{};std::memcpy(actual.data(),entity+mutation->offset,12);
        check(actual==std::array<float,3>{{1.f,-2.5f,0.f}},"Source speed is world XYZ divided by fifty, without native angular/local-axis conversion");
        for(size_t i=0;i<storage.size();++i)if(i<16+mutation->offset || i>=16+mutation->offset+12)
            check(before[i]==storage[i],"Physical vector update must preserve neighboring fields and both allocation canaries");
    }
    const int32_t one[]={77};entity[0x1D]=0xAD;
    auto damaged=entity_field_mutation(EntityCommand::object_render_damaged,one,1,entity,0x19C);
    check(damaged && apply_entity_field_mutation(entity,0x19C,*damaged) && entity[0x1D]==0xAF,"Render-damaged toggles only its own flag and preserves culling flags");
    auto alarm=entity_field_mutation(EntityCommand::car_alarm,one,1,entity,0xA18);
    check(alarm && apply_entity_field_mutation(entity,0xA18,*alarm) && entity[0x45C]==255 && entity[0x45D]==255,"Car alarm uses the source armed sentinel and ushort width");
    for(auto action:{EntityCommand::player_fireproof,EntityCommand::get_out_of_jail_free})for(int value:{-9,0,1,1000}) {
        const int32_t args[]={0,value};auto policy=entity_field_mutation(action,args,2,entity,0x190);
        check(policy && apply_entity_field_mutation(entity,0x190,*policy) && entity[policy->offset]==uint8_t(value!=0),"Player policy uses boolean normalization in its exact field");
    }
    for(auto action:{EntityCommand::player_max_health,EntityCommand::player_max_armour}) {
        const auto offset=action==EntityCommand::player_max_health?0x14F:0x150;
        entity[offset]=100;const int32_t args[]={0,180};auto upgrade=entity_field_mutation(action,args,2,entity,0x190);
        check(upgrade && apply_entity_field_mutation(entity,0x190,*upgrade) && entity[offset]==24,"Original maximum retains byte wrap instead of inventing clamped health");
    }
    check(source_player_maximum(100,-200)==156 && source_player_maximum(255,1)==0,"Signed reward arithmetic keeps the original eight-bit maximum");
    auto invalid=velocity;invalid[2]=entity_bits(std::numeric_limits<float>::quiet_NaN());const auto before_invalid=storage;
    check(!entity_field_mutation(EntityCommand::car_move_speed,invalid.data(),4,entity,0xA18) && storage==before_invalid,"Non-finite speed rejects before any write");
    check(!entity_field_mutation(EntityCommand::car_alarm,one,1,entity,0x45D) &&
          !entity_field_mutation(EntityCommand::object_render_damaged,one,1,entity,0x1D),"Short entity allocations cannot receive an out-of-range mutation");
    EntityFieldMutation forged;forged.offset=0xA18;forged.length=12;
    check(!apply_entity_field_mutation(entity,0xA18,forged) && storage==before_invalid,"Final mutation validates its entire destination before memcpy");
    const std::array<uint8_t,3> generations{{7,0x8A,12}};
    check(valid_entity_slot(7,3,generations.data()) && valid_entity_slot((2<<8)|12,3,generations.data()),"Live pool slots require index and matching generation");
    for(int32_t handle:{-1,8,(1<<8)|0x8A,(2<<8)|11,(3<<8)|7,0x7FFFFF07})
        check(!valid_entity_slot(handle,3,generations.data()),"Free, removed, recycled and out-of-bounds handles cannot reach pool storage");
    check(!valid_entity_slot(7,0,generations.data()) && !valid_entity_slot(7,3,nullptr),"Absent generation table is never an entity proof");
    check(source_car_in_area(-100,200,{{100,200,-100,-200}}) && !source_car_in_area(100.1f,0,{{100,200,-100,-200}}),
          "Source area query sorts both bounds and includes the edges, even when the original corner order is reversed");
    const auto upright=source_orientation({{1,0,0}},{{0,1,0}},{{0,0,1}});
    const auto east=source_orientation({{0,1,0}},{{-1,0,0}},{{0,0,1}});
    const auto rolled=source_orientation({{.8660254f,0,.5f}},{{0,.8660254f,.5f}},{{0,0,.8660254f}});
    check(upright && close((*upright)[0],0) && close((*upright)[1],0) && close((*upright)[2],0) && east && close((*east)[2],90),"Source orientation retains axis and degree order");
    check(rolled && close((*rolled)[0],330) && close((*rolled)[1],30),"Negative original orientation wraps once into the source circle");
    check(!source_orientation({{1,0,0}},{{0,0,1.1f}},{{0,0,1}}),"Invalid matrix sine cannot enter asin");
    check(close(*source_angle_between_points({{0,0,0,1}}),-180) && close(*source_angle_between_points({{0,0,1,0}}),90.00573f) &&
          close(*source_angle_between_points({{0,0,-1,0}}),-90.00573f),"Angle query uses the original quadrants and zero-Y epsilon");
    ScmImage image;NativeAdapter adapter(image);adapter.world_ready=true;adapter.bind_model(123,445,"fixture-car",ModelKind::vehicle);
    adapter.bind_model(169,441,"rcbandit",ModelKind::vehicle);
    NativeCall packet;size_t calls=0;bool accept=true,entered=false,condition=true;
    adapter.execute=guard_native_executor([&](const NativeCall& call){packet=call;++calls;NativeResult r;r.executed=accept;r.native_entered=entered;r.condition=condition;
        r.values={{entity_bits(330.f),entity_bits(30.f),entity_bits(90.f)}};
        if(call.source_entity_command && (*call.source_entity_command==EntityCommand::random_car_no_save || *call.source_entity_command==EntityCommand::random_car_unchecked))r.values[0]=0x207;
        return r;});
    auto step=[&](Vm& vm,Thread& t){t.ip=64;vm.backend=[&](Vm& v,Thread& thread,uint16_t op){return adapter.dispatch(v,thread,op);};return vm.step(t,0);};
    struct Scenario{uint16_t opcode;EntityCommand action;std::vector<int32_t> inputs;size_t outputs=0;};
    const std::vector<Scenario> scenarios={
        {COMMAND_GIVE_REMOTE_CONTROLLED_CAR_TO_PLAYER,EntityCommand::create_remote_car,{99,entity_bits(10),entity_bits(20),entity_bits(-100),entity_bits(90)},1},
        {COMMAND_OVERRIDE_CAR_REMOTE_CONTROL,EntityCommand::override_remote_car,{88,1}},
        {COMMAND_CLEAR_AREA_OF_OBJECTS,EntityCommand::clear_area_objects,{entity_bits(10.f),entity_bits(20.f),entity_bits(30.f),entity_bits(-10.f),entity_bits(-20.f),entity_bits(-30.f)}},
        {COMMAND_BLOW_UP_RC_BUGGY,EntityCommand::blow_up_rc_buggy,{}},
        {COMMAND_IS_CHAR_WANDER_PATH_CLEAR,EntityCommand::char_wander_path,{77,entity_bits(10),entity_bits(20),entity_bits(3),entity_bits(1)}},
        {COMMAND_STORE_CAR_COLLIDED_WITH_NO_SAVE,EntityCommand::car_collision,{88},1},
        {COMMAND_WRECK_CAR,EntityCommand::car_wreck,{88}},
        {COMMAND_ATTACH_OBJECT_TO_CAR,EntityCommand::attach_object_car,{77,88,entity_bits(1),entity_bits(-2),entity_bits(3)}},
        {COMMAND_DETACH_OBJECT_FROM_CAR,EntityCommand::detach_object_car,{77}},
        {COMMAND_HELI_GOTO_COORDS,EntityCommand::heli_goto,{88,entity_bits(100),entity_bits(-200),entity_bits(80),300}},
        {COMMAND_SET_CAR_RAM_CAR,EntityCommand::car_ram_car,{88,99}},
        {COMMAND_ATTACH_CHAR_TO_OBJECT,EntityCommand::char_attach_object,{77,99,entity_bits(1),entity_bits(2),entity_bits(3),0x10005,entity_bits(90),17}},
        {COMMAND_SET_ANIM_GROUP_FOR_CHAR,EntityCommand::char_locomotion_group,{77,43}},
        {COMMAND_RESET_HAVOC_CAUSED_BY_PLAYER,EntityCommand::reset_player_havoc,{0}},
        {COMMAND_GET_HAVOC_CAUSED_BY_PLAYER,EntityCommand::get_player_havoc,{1},1},
        {COMMAND_SET_CAR_MOVE_SPEED,EntityCommand::car_move_speed,{77,velocity[1],velocity[2],velocity[3]}},
        {COMMAND_SET_CAR_TURN_SPEED,EntityCommand::car_turn_speed,{77,velocity[1],velocity[2],velocity[3]}},
        {COMMAND_SET_OBJECT_TURN_SPEED,EntityCommand::object_turn_speed,{77,velocity[1],velocity[2],velocity[3]}},
        {COMMAND_GET_CAR_ORIENTATION,EntityCommand::get_car_orientation,{77},3},
        {COMMAND_SET_CAR_ORIENTATION,EntityCommand::set_car_orientation,{77,velocity[1],velocity[2],velocity[3]}},
        {COMMAND_GET_OBJECT_ORIENTATION,EntityCommand::get_object_orientation,{77},3},
        {COMMAND_SET_OBJECT_ORIENTATION,EntityCommand::set_object_orientation,{77,velocity[1],velocity[2],velocity[3]}},
        {COMMAND_GIVE_CAR_ALARM,EntityCommand::car_alarm,{77}},
        {COMMAND_SET_OBJECT_RENDERED_DAMAGED,EntityCommand::object_render_damaged,{77}},
        {COMMAND_SET_GET_OUT_OF_JAIL_FREE,EntityCommand::get_out_of_jail_free,{0,1}},
        {COMMAND_GET_RANDOM_CAR_IN_AREA_NO_CHECKS_NO_SAVE,EntityCommand::random_car_unchecked,{entity_bits(-100),entity_bits(-200),entity_bits(100),entity_bits(200)},1},
        {COMMAND_GET_RANDOM_CAR_IN_AREA_NO_SAVE,EntityCommand::random_car_no_save,{entity_bits(-100),entity_bits(-200),entity_bits(100),entity_bits(200),123},1},
        {COMMAND_MAKE_PLAYER_FIRE_PROOF,EntityCommand::player_fireproof,{0,1}},
        {COMMAND_INCREASE_PLAYER_MAX_HEALTH,EntityCommand::player_max_health,{0,20}},
        {COMMAND_INCREASE_PLAYER_MAX_ARMOUR,EntityCommand::player_max_armour,{0,20}},
        {COMMAND_SET_PLAYER_CURRENT_WEAPON_AMMO_IN_CLIP,EntityCommand::player_clip_ammo,{2147483647}},
        {COMMAND_REMOVE_CAR_BOOT,EntityCommand::remove_car_boot,{77}},
        {COMMAND_ACTIVATE_SAVE_MENU,EntityCommand::activate_save_menu,{}},
        {COMMAND_ENABLE_PLAYER_CONTROL_CAMERA,EntityCommand::enable_control_camera,{}}
    };
    for(const auto& s:scenarios) {
        auto vm=program(s.opcode,s.inputs,s.outputs);Thread t;const auto before=calls;
        check(step(vm,t)==StepResult::continued && calls==before+1 && packet.source_entity_command==s.action && !packet.source_statistic && !packet.opcode && packet.outputs==s.outputs,
              "Every source entity opcode selects the explicit operation rather than a native alias/NOP");
        auto expected=s.inputs;if(s.action==EntityCommand::random_car_no_save)expected[4]=445;
        if(s.action==EntityCommand::char_attach_object)expected[7]=22;
        if(s.action==EntityCommand::car_wreck || s.action==EntityCommand::create_remote_car)expected.push_back(441);
        check(packet.inputs==expected && t.ip==vm.memory.size(),"All original operands are consumed once and model filters bind to the native vehicle");
        if(s.outputs==3)check(t.locals[0]==entity_bits(330.f) && t.locals[1]==entity_bits(30.f) && t.locals[2]==entity_bits(90.f),"All source orientation outputs are committed together");
        if(s.action==EntityCommand::car_collision)check(t.locals[0]==entity_bits(330.f),"Consumed collision writes the backend handle as integer bits without float conversion");
        bool encode_rejected=false;try{packet.encode();}catch(const std::runtime_error&){encode_rejected=true;}
        check(encode_rejected,"Family packets cannot silently execute native opcode zero");
        accept=false;Thread rejected;rejected.locals={{11,22,33}};
        check(step(vm,rejected)==StepResult::unsupported && rejected.ip==64 && rejected.locals[0]==11,"Pre-entry family rejection keeps the exact original instruction and outputs");accept=true;
        adapter.world_ready=false;Thread waiting;const auto gated=calls;
        check(step(vm,waiting)==StepResult::unsupported && calls==gated,"Unverified mounted world cannot access native entity storage");adapter.world_ready=true;
        if(s.outputs){auto bad=program(s.opcode,s.inputs,s.outputs,true);Thread malformed;const auto no_call=calls;
            check(step(bad,malformed)==StepResult::failed && malformed.ip==64 && calls==no_call,"The final output variable must validate before native entry");}
        if(s.action==EntityCommand::clear_area_objects || s.action==EntityCommand::attach_object_car || s.action==EntityCommand::detach_object_car || s.action==EntityCommand::heli_goto ||
           s.action==EntityCommand::car_ram_car || s.action==EntityCommand::char_attach_object || s.action==EntityCommand::car_wreck ||
           s.action==EntityCommand::car_collision || s.action==EntityCommand::char_wander_path) {
            for(size_t cut=1;cut<=s.inputs.size()*5;++cut) {
                auto short_vm=program(s.opcode,s.inputs,s.outputs);short_vm.memory.resize(short_vm.memory.size()-cut);
                Thread malformed;const auto before_short=calls;
                const auto short_result=step(short_vm,malformed);
                if(short_result!=StepResult::failed || malformed.ip!=64 || calls!=before_short)
                    std::cerr<<"Truncated opcode "<<s.opcode<<" cut="<<cut<<" result="<<int(short_result)<<" ip="<<malformed.ip<<" calls="<<(calls-before_short)<<" diagnostic="<<malformed.diagnostic<<'\n';
                check(short_result==StepResult::failed && malformed.ip==64 && calls==before_short,
                    "Every truncated vehicle/attachment operand rejects before calling engine or consuming instruction");
            }
            accept=false;entered=true;Thread partial;
            check(step(vm,partial)==StepResult::failed && partial.ip==64 && partial.diagnostic.find("replay rejected after entry")!=std::string::npos,
                "Vehicle/attachment failure after mutation cannot replay, skip instruction or fabricate progress");
            accept=true;entered=false;
        }
    }
    {
        NativeAdapter no_rc(image);no_rc.world_ready=true;
        no_rc.execute=adapter.execute;
        auto vm=program(COMMAND_WRECK_CAR,{88});Thread missing;missing.ip=64;
        vm.backend=[&](Vm& v,Thread& thread,uint16_t op){return no_rc.dispatch(v,thread,op);};
        const auto before=calls;
        check(vm.step(missing,0)==StepResult::unsupported && missing.ip==64 && calls==before,
            "Missing original RC model binding cannot enter wreck visual exception");
    }
    for(const auto& s:scenarios) {
        if(s.action!=EntityCommand::attach_object_car && s.action!=EntityCommand::heli_goto && s.action!=EntityCommand::char_attach_object)continue;
        for(size_t axis:s.action==EntityCommand::heli_goto?std::vector<size_t>{1,2,3}:s.action==EntityCommand::char_attach_object?std::vector<size_t>{2,3,4,6}:std::vector<size_t>{2,3,4}) {
            auto args=s.inputs;args[axis]=entity_bits(std::numeric_limits<float>::quiet_NaN());auto bad=program(s.opcode,args);Thread t;const auto before=calls;
            check(step(bad,t)==StepResult::unsupported && t.ip==64 && calls==before,"Non-finite source attachment/flight operands cannot reach native service");
        }
    }
    for(int32_t weapon:{-1,37,100,INT32_MAX}) {
        auto bad=program(COMMAND_ATTACH_CHAR_TO_OBJECT,{77,99,0,0,0,0,entity_bits(90),weapon});Thread t;const auto before=calls;
        check(step(bad,t)==StepResult::unsupported && t.ip==64 && calls==before,"Unbound attachment weapon refuses before native mutation");
    }
    auto speed=program(COMMAND_SET_CAR_MOVE_SPEED,{77,invalid[1],invalid[2],invalid[3]});Thread nonfinite;const auto no_call=calls;
    check(step(speed,nonfinite)==StepResult::unsupported && calls==no_call,"A bad vector is not handed to the engine");
    for(int32_t player:{-1,2,1234})for(uint16_t opcode:{uint16_t(COMMAND_SET_GET_OUT_OF_JAIL_FREE),uint16_t(COMMAND_MAKE_PLAYER_FIRE_PROOF),uint16_t(COMMAND_INCREASE_PLAYER_MAX_HEALTH),uint16_t(COMMAND_INCREASE_PLAYER_MAX_ARMOUR),uint16_t(COMMAND_RESET_HAVOC_CAUSED_BY_PLAYER),uint16_t(COMMAND_GET_HAVOC_CAUSED_BY_PLAYER)}) {
        const bool havoc=opcode==COMMAND_RESET_HAVOC_CAUSED_BY_PLAYER || opcode==COMMAND_GET_HAVOC_CAUSED_BY_PLAYER;
        auto vm=program(opcode,havoc?std::vector<int32_t>{player}:std::vector<int32_t>{player,1},opcode==COMMAND_GET_HAVOC_CAUSED_BY_PLAYER?1:0);Thread t;const auto before=calls;
        check(step(vm,t)==StepResult::unsupported && calls==before,"Invalid player selector cannot index the native player array");
    }
    auto random=program(COMMAND_GET_RANDOM_CAR_IN_AREA_NO_SAVE,{0,0,entity_bits(100),entity_bits(100),999},1);Thread unbound;const auto before_unbound=calls;
    check(step(random,unbound)==StepResult::unsupported && calls==before_unbound,"An unbound model cannot be mistaken for a native random vehicle filter");
    auto any=program(COMMAND_GET_RANDOM_CAR_IN_AREA_NO_SAVE,{0,0,entity_bits(100),entity_bits(100),-1},1);Thread wildcard;
    check(step(any,wildcard)==StepResult::continued && packet.inputs[4]==-1,"Source wildcard filter remains a wildcard");
    auto camera=program(COMMAND_GET_DEBUG_CAMERA_POINT_AT,{},3);Thread focus;
    check(step(camera,focus)==StepResult::continued && packet.opcode==0x0463 && packet.outputs==3 && !packet.source_entity_command && focus.locals[2]==entity_bits(90),"Debug point queries native source-plus-front, preserving three float outputs");
    auto angle=program(COMMAND_GET_ANGLE_BETWEEN_POINTS,{0,0,entity_bits(1),0},1);Thread a;const auto before_angle=calls;
    check(step(angle,a)==StepResult::continued && calls==before_angle && close(entity_float(a.locals[0]),90.00573f),"Pure source angle query does not depend on native state");
    auto remote=program(COMMAND_IS_PLAYER_IN_REMOTE_MODE,{0});Thread r;
    check(step(remote,r)==StepResult::continued && packet.opcode==0x0241 && packet.predicate && r.condition,"Remote query uses actual native state");
    auto inverse=program(uint16_t(COMMAND_IS_PLAYER_IN_REMOTE_MODE)|0x8000,{0});Thread negated;
    check(step(inverse,negated)==StepResult::continued && !negated.condition,"Source negation is applied exactly once");
    for(const auto pair:{std::pair<uint16_t,EntityCommand>{COMMAND_IS_CHAR_DROWNING_IN_WATER,EntityCommand::char_drowning},
                       std::pair<uint16_t,EntityCommand>{COMMAND_IS_CHAR_LEAVING_VEHICLE_TO_DIE,EntityCommand::char_leaving_vehicle_to_die},
                       std::pair<uint16_t,EntityCommand>{COMMAND_IS_PLAYER_USING_FIRST_PERSON_WEAPON_CAMERA,EntityCommand::first_person_weapon_camera}}) {
        for(bool state:{false,true})for(bool inverse:{false,true}) {
            condition=state;auto query=program(uint16_t(pair.first|(inverse?0x8000:0)),{77});Thread q;
            check(step(query,q)==StepResult::continued && packet.source_entity_command==pair.second && packet.predicate &&
                packet.inputs==std::vector<int32_t>{77} && q.condition==(inverse?!state:state),"New source predicates preserve operands and apply true/false/negation exactly once");
            accept=false;Thread rejected;rejected.condition=!state;
            check(step(query,rejected)==StepResult::unsupported && rejected.ip==64 && rejected.condition==!state,"Predicate refusal preserves the original instruction and comparison");accept=true;
        }
    }
    for(bool state:{false,true})for(bool inverse:{false,true}) {
        condition=state;auto query=program(uint16_t(COMMAND_IS_CHAR_IN_CHARS_GROUP|(inverse?0x8000:0)),{77,88});Thread q;
        check(step(query,q)==StepResult::continued && packet.source_entity_command==EntityCommand::char_in_chars_group && packet.predicate &&
            packet.inputs==std::vector<int32_t>{77,88} && packet.outputs==0 && q.ip==query.memory.size() && q.condition==(inverse?!state:state),
            "Group query preserves both operands and applies true/false/negation exactly once");
        accept=false;Thread rejected;rejected.condition=!state;
        check(step(query,rejected)==StepResult::unsupported && rejected.ip==64 && rejected.condition==!state,"Unavailable group query keeps instruction and comparison instead of fabricating false");accept=true;
    }
    {
        auto truncated=program(COMMAND_IS_CHAR_IN_CHARS_GROUP,{77,88});truncated.memory.resize(truncated.memory.size()-3);Thread t;const auto before=calls;
        check(step(truncated,t)==StepResult::failed && t.ip==64 && calls==before,"Truncated group query operands reject before native entry");
    }
    auto ignored_player=program(COMMAND_IS_PLAYER_USING_FIRST_PERSON_WEAPON_CAMERA,{-999});Thread global_camera;
    check(step(ignored_player,global_camera)==StepResult::continued && packet.inputs[0]==-999,"Camera predicate retains the consumed argument which the original global-camera query does not use");
    for(const auto pair:{std::pair<uint16_t,EntityCommand>{COMMAND_HAS_PHOTOGRAPH_BEEN_TAKEN,EntityCommand::photograph_taken},
                        std::pair<uint16_t,EntityCommand>{COMMAND_ARE_CREDITS_FINISHED,EntityCommand::credits_finished}}) {
        for(bool state:{false,true})for(bool inverse:{false,true}) {
            condition=state;auto query=program(uint16_t(pair.first|(inverse?0x8000:0)),{});Thread q;
            check(step(query,q)==StepResult::continued && packet.source_entity_command==pair.second && packet.predicate &&
                packet.inputs.empty() && q.condition==(inverse?!state:state),"Zero-operand source queries preserve true/false/negation exactly once");
            accept=false;Thread rejected;rejected.condition=!state;
            check(step(query,rejected)==StepResult::unsupported && rejected.ip==64 && rejected.condition==!state,"Refused zero-operand predicate cannot consume instruction or change comparison");accept=true;
            entered=true;accept=false;Thread damaged;damaged.condition=!state;
            check(step(query,damaged)==StepResult::failed && damaged.condition==!state && damaged.diagnostic.find("replay rejected after entry")!=std::string::npos,"Entered latch failure requires repair instead of replay or false success");
            entered=false;accept=true;
        }
    }
    for(auto value:{0,-1,2147483647}) {
        auto clip=program(COMMAND_SET_PLAYER_CURRENT_WEAPON_AMMO_IN_CLIP,{value});Thread ammo;
        check(step(clip,ammo)==StepResult::continued && packet.source_entity_command==EntityCommand::player_clip_ammo && packet.inputs[0]==value,"Clip setter keeps original zero/signed 32-bit operand without capacity invention");
    }
    accept=false;entered=true;auto upgrade=program(COMMAND_INCREASE_PLAYER_MAX_HEALTH,{0,20});Thread partial;
    check(step(upgrade,partial)==StepResult::failed && partial.ip==64 && partial.diagnostic.find("replay rejected after entry")!=std::string::npos,
          "A partially entered reward must require repair, never increment maximum health again on replay");
    auto boot=program(COMMAND_REMOVE_CAR_BOOT,{77});Thread damaged_boot;
    check(step(boot,damaged_boot)==StepResult::failed && damaged_boot.ip==64 && damaged_boot.diagnostic.find("replay rejected after entry")!=std::string::npos,
          "Visual removal fault after the damage write cannot be retried as an unentered instruction");
    auto style=program(COMMAND_SET_ANIM_GROUP_FOR_CHAR,{77,43});Thread damaged_style;
    check(step(style,damaged_style)==StepResult::failed && damaged_style.ip==64 && damaged_style.diagnostic.find("replay rejected after entry")!=std::string::npos,
          "Style write fault after entry requires repair and never falls through to another opcode");
    auto havoc=program(COMMAND_RESET_HAVOC_CAUSED_BY_PLAYER,{0});Thread damaged_havoc;
    check(step(havoc,damaged_havoc)==StepResult::failed && damaged_havoc.ip==64 && damaged_havoc.diagnostic.find("replay rejected after entry")!=std::string::npos,
          "A partially entered havoc reset cannot replay or fabricate successful output");
    auto save_menu=program(COMMAND_ACTIVATE_SAVE_MENU,{});Thread damaged_save;
    check(step(save_menu,damaged_save)==StepResult::failed && damaged_save.ip==64 && damaged_save.diagnostic.find("replay rejected after entry")!=std::string::npos,
          "A partially entered save-menu activation cannot replay SafeHouseVisits increment or fabricate completion");
    accept=true;entered=false;
    for(size_t cut=1;cut<=5;++cut) {
        auto truncated=program(COMMAND_SET_ANIM_GROUP_FOR_CHAR,{77,43});truncated.memory.resize(truncated.memory.size()-cut);
        Thread malformed;malformed.condition=true;const auto before=calls;
        check(step(truncated,malformed)==StepResult::failed && malformed.ip==64 && malformed.condition && calls==before,
              "All style operands validate before native mutation and preserve the comparison on truncation");
    }
    {
        // 1084 / 930 / 947: one finite particle, recurring moving emitters, removal by area.
        ScmImage image;NativeAdapter adapter(image);adapter.world_ready=true;
        auto dispatch=[&](Vm& vm,Thread& thread){thread.ip=64;vm.backend=[&](Vm& v,Thread& t,uint16_t op){return adapter.dispatch(v,t,op);};return vm.step(thread,0);};
        const auto f=[](float v){return entity_bits(v);};
        auto single=program(COMMAND_CREATE_SINGLE_PARTICLE,{7,f(1),f(2),f(3),f(.1f),f(.2f),f(.3f),f(.5f)});Thread t;
        check(dispatch(single,t)==StepResult::unsupported,"1084 without a particle backend is not a NOP");
        std::vector<FxParticleRequest> emitted;bool emit_ok=true,emit_entered=false;
        std::vector<uint32_t> started,stopped;bool stop_ok=true;bool camera_ok=true;FxVec3 camera{{0,0,0}};bool fire_ok=true;
        adapter.fx.callbacks.emit=[&](const FxParticleRequest& r){FxStatus s;s.ok=emit_ok;s.native_entered=emit_entered;if(!emit_ok)s.diagnostic="refused";if(emit_ok)emitted.push_back(r);return s;};
        adapter.fx.callbacks.fire_start=[&](const FxVec3&,float size,FxStatus& s){if(!fire_ok){s.diagnostic="no fire";return uint32_t(0);}started.push_back(uint32_t(size*10));return uint32_t(started.size());};
        adapter.fx.callbacks.fire_stop=[&](uint32_t token){if(stop_ok)stopped.push_back(token);return stop_ok;};
        adapter.fx.callbacks.camera=[&]()->std::optional<FxVec3>{if(!camera_ok)return {};return camera;};
        uint32_t now=1000;float ground=5.0f;std::vector<std::array<int32_t,2>> ground_queries;
        adapter.execute=[&](const NativeCall& call){
            NativeResult r;r.executed=true;
            switch(call.opcode) {
            case 0x02CE:ground_queries.push_back({{call.inputs[0],call.inputs[1]}});r.values[0]=f(ground);return r;
            case 0x01BD:r.values[0]=int32_t(now);return r;
            default:r.executed=false;return r;
            }
        };
        t={};check(dispatch(single,t)==StepResult::continued && emitted.size()==1 && emitted[0].kind==FxSystemKind::blood &&
            emitted[0].position==FxVec3{{1,2,3}} && emitted[0].velocity==FxVec3{{.1f,.2f,.3f}} && close(emitted[0].size,.5f),"1084 consumes type, position, velocity and size into one SA particle");
        emitted.clear();auto unknown=program(COMMAND_CREATE_SINGLE_PARTICLE,{3,f(1),f(2),f(3),0,0,0,f(.5f)});
        t={};check(dispatch(unknown,t)==StepResult::unsupported && emitted.empty(),"1084 refuses a particle type without a verified SA system");
        auto nan=program(COMMAND_CREATE_SINGLE_PARTICLE,{7,f(1),f(2),f(3),0,0,0,f(std::numeric_limits<float>::quiet_NaN())});
        t={};check(dispatch(nan,t)==StepResult::unsupported && emitted.empty(),"1084 refuses non-finite operands before any native call");
        emit_ok=false;t={};check(dispatch(single,t)==StepResult::unsupported,"1084 refused before native entry stays recoverable");
        emit_entered=true;t={};check(dispatch(single,t)==StepResult::failed,"1084 failing after native entry is never replayed");
        emit_ok=true;emit_entered=false;emitted.clear();

        const auto moving=[&](int32_t type,float z,std::array<int32_t,3> rgb={0,0,0},int32_t life=0,float size=1.5f){
            return program(COMMAND_ADD_MOVING_PARTICLE_EFFECT,{type,f(10),f(20),f(z),f(.01f),f(.02f),f(.03f),f(size),rgb[0],rgb[1],rgb[2],life});};
        auto ice=moving(12,-100.0f,{0,0,0},500);
        t={};check(dispatch(ice,t)==StepResult::continued && ground_queries.size()==1 && ground_queries[0][0]==f(10) && ground_queries[0][1]==f(20) && adapter.fx.live()==1,"930 resolves the -100 ground sentinel with a native terrain query and registers one emitter");
        check(emitted.empty(),"930 creates a recurring emitter, not a single immediate particle");
        for(int frame=1;frame<=10;++frame){now+=33;adapter.fx.tick(now);}
        check(emitted.size()==10 && emitted[0].kind==FxSystemKind::smoke_light && close(emitted[0].position[2],5.0f) && emitted[0].velocity==FxVec3{{0,0,0}} && close(emitted[0].size,1.5f) && !emitted[0].has_color,
              "DRY_ICE emits every frame at the grounded position; the source resets its movement to zero");
        for(int frame=0;frame<20;++frame){now+=33;adapter.fx.tick(now);}
        check(adapter.fx.live()==0,"930 lifetime operand ends the emitter once it expires");
        const auto count_after_expiry=emitted.size();now+=33;adapter.fx.tick(now);check(emitted.size()==count_after_expiry,"An expired emitter emits nothing more");
        auto dark=moving(4,7.0f);emitted.clear();t={};check(dispatch(dark,t)==StepResult::continued && adapter.fx.live()==1,"DARK_SMOKE registers");
        for(int frame=0;frame<300;++frame){now+=33;adapter.fx.tick(now);}
        check(emitted.size()>60 && emitted.size()<=100 && emitted[0].kind==FxSystemKind::smoke_dark && close(emitted[0].velocity[1],.02f) && emitted[0].has_color && emitted[0].red==16 && emitted[0].alpha==255 && ground_queries.size()==1,"DARK_SMOKE keeps the source cadence (every third frame, seven of eight chances) and its fixed dark colour; no ground query for a real Z");
        adapter.fx.reset();emitted.clear();
        auto trail=moving(17,3.0f,{0x1FF,2,3},0,-4.0f);t={};check(dispatch(trail,t)==StepResult::continued,"SMOKE_TRAIL registers");
        for(int frame=0;frame<40;++frame){now+=33;adapter.fx.tick(now);}
        check(!emitted.empty() && emitted[0].has_color && emitted[0].red==255 && emitted[0].green==2 && emitted[0].blue==3 && emitted[0].alpha==255 && emitted[0].size==0.0f,"SMOKE_TRAIL keeps the byte colour with alpha 255 and clamps a negative size to zero");
        adapter.fx.reset();emitted.clear();
        auto rocket=moving(19,3.0f,{0,0,0},0,2.0f);t={};check(dispatch(rocket,t)==StepResult::continued,"ROCKET_TRAIL registers");
        for(int frame=0;frame<200;++frame){now+=33;adapter.fx.tick(now);}
        check(emitted.size()>5 && close(emitted.back().size,3.0f) && close(emitted.back().velocity[0],-.008f) && emitted.back().position[0]>10.0f,"ROCKET_TRAIL trails behind the advancing head with 1.5x size and -0.8x velocity");
        adapter.fx.reset();emitted.clear();
        auto weird=moving(1,3.0f);t={};check(dispatch(weird,t)==StepResult::unsupported && adapter.fx.live()==0,"930 refuses an object type without a verified recipe before registering anything");
        auto fire=moving(10,3.0f,{0,0,0},0,2.0f);t={};
        fire_ok=false;check(dispatch(fire,t)==StepResult::unsupported && adapter.fx.live()==0,"SMALL_FIRE that cannot start its SA fire registers nothing");fire_ok=true;
        t={};check(dispatch(fire,t)==StepResult::continued && started==std::vector<uint32_t>({20}) && adapter.fx.live()==1,"SMALL_FIRE starts one SA fire with the size operand");
        for(int frame=0;frame<10;++frame){now+=33;adapter.fx.tick(now);}check(emitted.empty(),"The SA fire replaces per-frame puffs");
        adapter.fx.reset();check(stopped==std::vector<uint32_t>({1}) && adapter.fx.live()==0,"Reset stops the owned fire exactly once");

        // 947: inclusive, normalised box; only bridge-owned emitters; removal ends future emission and the fire.
        emitted.clear();stopped.clear();started.clear();
        const auto at=[&](int32_t type,float x,float y,float z){return program(COMMAND_ADD_MOVING_PARTICLE_EFFECT,{type,f(x),f(y),f(z),0,0,0,f(1.f),0,0,0,0});};
        auto a=at(12,10,10,10),b=at(12,50,50,50),c=at(10,12,12,12),d=at(12,10,10,40);
        for(auto* p:{&a,&b,&c,&d}){t={};check(dispatch(*p,t)==StepResult::continued,"fixture emitter");}
        check(adapter.fx.live()==4,"fixture has four owned emitters");
        auto remove=program(COMMAND_REMOVE_PARTICLE_EFFECTS_IN_AREA,{f(20),f(20),f(20),f(10),f(10),f(10)});
        t={};check(dispatch(remove,t)==StepResult::continued && adapter.fx.live()==2 && stopped.size()==1,"947 removes owned emitters inside the swapped, inclusive box and stops their fire");
        emitted.clear();adapter.fx.tick(now+=33);
        check(emitted.size()==2 && emitted[0].position[0]==50 && emitted[1].position[2]==40,"Only emitters outside the area keep emitting; removed ones end");
        auto everything=program(COMMAND_REMOVE_PARTICLE_EFFECTS_IN_AREA,{f(0),f(0),f(0),f(60),f(60),f(60)});
        t={};check(dispatch(everything,t)==StepResult::continued && adapter.fx.live()==0,"947 ends every owned emitter in the area");
        check(adapter.fx.pending_fire_stops()==0,"Emitters without a fire have nothing to retry");
        auto f2=at(10,1,1,1);t={};dispatch(f2,t);stop_ok=false;
        auto small=program(COMMAND_REMOVE_PARTICLE_EFFECTS_IN_AREA,{f(0),f(0),f(0),f(2),f(2),f(2)});t={};
        check(dispatch(small,t)==StepResult::continued && adapter.fx.live()==0 && adapter.fx.pending_fire_stops()==1,"A refused fire stop is retained for retry, not forgotten");
        stop_ok=true;stopped.clear();adapter.fx.tick(now+=33);check(stopped.size()==1 && adapter.fx.pending_fire_stops()==0,"The retained fire stop completes on the next frame");
        // Pool of 70 and camera removal.
        for(int i=0;i<75;++i){auto p=at(12,float(i),0,0);t={};check(dispatch(p,t)==StepResult::continued,"pool fill never fails the script");}
        check(adapter.fx.live()==70,"The 70-object pool drops extra objects silently like the original");
        camera={{1000,1000,0}};emitted.clear();adapter.fx.tick(now+=33);
        check(adapter.fx.live()==0 && emitted.empty(),"Objects farther than 100 m (2D) from the camera are removed without emitting");
        camera={{0,0,0}};camera_ok=false;auto keep=at(12,500,500,0);t={};dispatch(keep,t);adapter.fx.tick(now+=33);
        check(adapter.fx.live()==1,"An unknown camera never removes objects");
        adapter.fx.reset();
        emit_ok=false;auto failing=at(12,1,1,1);t={};dispatch(failing,t);adapter.fx.tick(now+=33);
        check(adapter.fx.live()==0,"An emitter whose SA particle fails is ended, not retried forever");
    }
    {
        // 1110 LOAD_END_OF_GAME_TUNE, 1453 CLEAR_NAVIGATION_ARROW, 1398 IS_CHAR_STUCK.
        ScmImage image;NativeAdapter adapter(image);adapter.world_ready=true;
        auto dispatch=[&](Vm& vm,Thread& thread){thread.ip=64;vm.backend=[&](Vm& v,Thread& t,uint16_t op){return adapter.dispatch(v,t,op);};return vm.step(thread,0);};
        auto load=program(COMMAND_LOAD_END_OF_GAME_TUNE,{});Thread t;
        adapter.world_ready=false;
        for(bool condition:{false,true}){
            auto log_integer=program(COMMAND_SAVE_INT_TO_DEBUG_FILE,{int32_t(0x87654321u)});
            Thread logged;logged.condition=condition;
            check(dispatch(log_integer,logged)==StepResult::continued && logged.ip==71 && logged.condition==condition,
                  "Mission debug integer compatibility consumes its full operand without a world/backend dependency");
            auto log_newline=program(COMMAND_SAVE_NEWLINE_TO_DEBUG_FILE,{});
            logged={};logged.condition=condition;
            check(dispatch(log_newline,logged)==StepResult::continued && logged.ip==66 && logged.condition==condition,
                  "Mission newline compatibility preserves condition and exact instruction length without I/O");
        }
        adapter.world_ready=true;

        check(dispatch(load,t)==StepResult::continued && t.ip==66,"Retail 1110 has no operands or backend dependency");
        int started=0;
        adapter.ending_tune=[&](bool){++started;NativeResult r;r.executed=true;return r;};
        t={};t.condition=true;
        check(dispatch(load,t)==StepResult::continued && started==0 && t.ip==66 && t.condition,"1110 compatibility starts nothing and preserves the condition");
        adapter.world_ready=false;t={};check(dispatch(load,t)==StepResult::continued,"1110 compatibility does not invent a mounted-world gate");
        adapter.world_ready=true;
        auto line_compat=program(COMMAND_LINE,{0,1,-1,int32_t(0x7FC00000),123,456});
        t={};t.condition=true;
        check(dispatch(line_compat,t)==StepResult::continued && t.ip==96 && t.condition,
              "Retail LINE collects six raw operands without inventing a drawing effect");
        for(uint16_t empty:{uint16_t(COMMAND_1476),uint16_t(COMMAND_1627)}){
            auto empty_compat=program(empty,{});t={};t.condition=true;
            check(dispatch(empty_compat,t)==StepResult::continued && t.ip==66 && t.condition,
                  "Retail1476/1627 return without consuming the following instruction or changing the condition");
        }

        {
            auto sticks=program(COMMAND_SET_PAD_STICKS_MULTIPLIER,{entity_bits(1.2f),entity_bits(0.5f)});
            t={};t.condition=true;
            check(dispatch(sticks,t)==StepResult::continued && t.ip==76 && t.condition,
                  "1620 compatibility consumes both floats and forwards nothing to SA pad shorts");
            auto bad_sticks=program(COMMAND_SET_PAD_STICKS_MULTIPLIER,{entity_bits(std::numeric_limits<float>::infinity()),entity_bits(0.5f)});
            t={};check(dispatch(bad_sticks,t)==StepResult::unsupported,"1620 rejects non-finite operands");
            for(int32_t value:{0,1}) {
                auto gang=program(COMMAND_SET_GANG_ATTACK_PLAYER_WITH_COPS,{5,value});
                t={};t.condition=true;
                check(dispatch(gang,t)==StepResult::continued && t.condition,"1431 consumes both operands for TRUE and the FALSE reset");
            }
            auto gang_bad=program(COMMAND_SET_GANG_ATTACK_PLAYER_WITH_COPS,{9,1});
            t={};check(dispatch(gang_bad,t)==StepResult::unsupported,"1431 rejects an index outside the nine retail gangs");
        }
        auto clear_arrow=program(COMMAND_CLEAR_NAVIGATION_ARROW,{});
        t={};check(dispatch(clear_arrow,t)==StepResult::continued && t.ip==66,
                   "1453 compatibility consumes no operands; the current content has no navigation-arrow producer");
        adapter.world_ready=false;t={};t.condition=true;
        check(dispatch(clear_arrow,t)==StepResult::continued && t.condition,"1453 is independent of the mounted world and preserves the condition flag");
        adapter.world_ready=true;

        {
            auto allowed_collision=program(COMMAND_SET_ALLOWED_COLLISION,{0,3});
            t={};t.condition=true;
            check(dispatch(allowed_collision,t)==StepResult::unsupported && t.ip==64 && t.condition,
                  "1642 refuses without the real native streaming consumer");
            std::array<int32_t,2> allowed{{-1,-1}};size_t collision_calls=0;
            adapter.set_allowed_collision=[&](int32_t first,int32_t second) {
                ++collision_calls;allowed={{first,second}};NativeResult result;
                result.executed=true;return result;
            };
            t={};t.condition=true;
            check(dispatch(allowed_collision,t)==StepResult::continued && t.ip==76 && t.condition &&
                  allowed==std::array<int32_t,2>{{0,3}} && collision_calls==1,
                  "1642 forwards both integer levels once and preserves the comparison flag");
            adapter.set_allowed_collision=[](int32_t,int32_t){return NativeResult{};};
            t={};t.condition=true;
            check(dispatch(allowed_collision,t)==StepResult::unsupported && t.ip==64 && t.condition,
                  "1642 refuses an unadmitted native policy without advancing");
            adapter.set_allowed_collision={};
            auto entry_range=program(COMMAND_SET_ENTER_CAR_RANGE_MULTIPLIER,{entity_bits(2.9f)});
            t={};t.condition=true;
            check(dispatch(entry_range,t)==StepResult::unsupported && t.ip==64 && t.condition,
                  "1158 refuses without the actual entry controller");
            uint16_t stored_range=1;size_t range_calls=0;bool range_ok=true;
            adapter.set_enter_car_range_multiplier=[&](uint16_t value){
                ++range_calls;if(!range_ok)return false;stored_range=value;return true;
            };
            adapter.world_ready=false;t={};t.condition=true;
            check(dispatch(entry_range,t)==StepResult::continued && t.ip==71 && t.condition && stored_range==2,
                  "1158 initializes the global controller before world admission and quantizes the source float");
            range_ok=false;t={};
            check(dispatch(entry_range,t)==StepResult::unsupported && t.ip==64 && stored_range==2,
                  "1158 pre-entry controller refusal preserves range and script position");
            range_ok=true;
            auto bad_range=program(COMMAND_SET_ENTER_CAR_RANGE_MULTIPLIER,{entity_bits(std::numeric_limits<float>::quiet_NaN())});
            t={};check(dispatch(bad_range,t)==StepResult::unsupported && t.ip==64 && range_calls==2,
                       "1158 rejects non-finite input before invoking its controller");
            adapter.set_enter_car_range_multiplier={};adapter.world_ready=true;
        }
        {
            auto crushed=program(COMMAND_IS_CAR_CRUSHED,{42});
            t={};t.condition=true;
            check(dispatch(crushed,t)==StepResult::unsupported && t.ip==64 && t.condition,
                  "334 refuses without its physical production consumers");
            auto reward_multiplier=program(COMMAND_SET_CRUSHER_REWARD_MULTIPLIER,{257});
            adapter.world_ready=false;t={};t.condition=true;
            check(dispatch(reward_multiplier,t)==StepResult::continued && t.ip==71 && t.condition,
                  "1588 initializes the actual crusher controller before world streaming");
            adapter.world_ready=true;
            adapter.exclusive_garages.world.crusher_door=[](int32_t,const std::array<float,8>&,float){return NativeResult{};};
            adapter.exclusive_garages.world.entirely_inside=[](const std::array<float,8>&,const RouteVehicleSnapshot&)->std::optional<bool>{return {};};
            t={};t.condition=true;
            check(dispatch(crushed,t)==StepResult::continued && !t.condition,
                  "334 reads the real controller's last-crushed state without inventing success");
            adapter.exclusive_garages.world={};adapter.exclusive_garages.clear();
            auto objective_done=program(COMMAND_IS_CHAR_OBJECTIVE_PASSED,{27});
            t={};t.condition=true;
            check(dispatch(objective_done,t)==StepResult::unsupported && t.ip==64 && t.condition,
                  "299 refuses without its production completion consumer");
            std::optional<bool> completion=false;size_t completion_reads=0;
            adapter.char_objective_passed=[&](int32_t ped)->std::optional<bool>{
                ++completion_reads;check(ped==27,"299 preserves the exact actor");return completion;
            };
            t={};t.condition=true;
            check(dispatch(objective_done,t)==StepResult::continued && !t.condition && completion_reads==1,
                  "299 propagates a confirmed incomplete objective");
            completion=true;t={};
            check(dispatch(objective_done,t)==StepResult::continued && t.condition && completion_reads==2,
                  "299 propagates confirmed completion");
            auto objective_not_done=program(uint16_t(COMMAND_IS_CHAR_OBJECTIVE_PASSED|0x8000),{27});
            t={};check(dispatch(objective_not_done,t)==StepResult::continued && !t.condition,
                       "299 respects the SCM NOT comparison flag");
            completion.reset();t={};t.condition=true;
            check(dispatch(objective_done,t)==StepResult::unsupported && t.ip==64 && t.condition,
                  "299 cannot fabricate FALSE when completion evidence is unavailable");
            adapter.char_objective_passed={};
            auto hostage=program(COMMAND_SET_CHAR_FRIGHTENED_IN_JACKED_CAR,{27,2});
            t={};t.condition=true;
            check(dispatch(hostage,t)==StepResult::unsupported && t.ip==64 && t.condition,
                  "1432 refuses without its native passenger consumer");
            bool protection_ok=true,protection_active=false;size_t protection_calls=0;
            adapter.set_char_jack_protection=[&](int32_t actor,bool active){
                ++protection_calls;check(actor==27,"1432 preserves the exact actor operand");
                protection_active=active;NativeResult result;result.executed=protection_ok;return result;
            };
            t={};t.condition=true;
            check(dispatch(hostage,t)==StepResult::continued && t.ip==76 && t.condition &&
                  protection_calls==1 && protection_active,
                  "1432 uses retail nonzero normalization including2 and preserves comparison state");
            auto clear_hostage=program(COMMAND_SET_CHAR_FRIGHTENED_IN_JACKED_CAR,{27,0});
            t={};check(dispatch(clear_hostage,t)==StepResult::continued && !protection_active && protection_calls==2,
                      "1432 zero retires the independent source protection");
            protection_ok=false;t={};t.condition=true;
            check(dispatch(hostage,t)==StepResult::unsupported && t.ip==64 && t.condition,
                  "1432 native refusal cannot advance the SCM");
            adapter.set_char_jack_protection={};
            auto move_override=program(COMMAND_OVERRIDE_CHAR_MOVE_ANIM,{27});
            t={};check(dispatch(move_override,t)==StepResult::unsupported && t.ip==64,
                       "1626 refuses without a native locomotion consumer");
            bool move_ok=true,move_entered=false,clear_ok=true,style_ok=true;
            size_t moves=0,clears=0,style_writes=0;
            adapter.override_char_move_anim=[&](int32_t ped){
                ++moves;check(ped==27,"1626 passes the exact source actor");
                NativeResult r;r.executed=move_ok;r.native_entered=move_entered;return r;
            };
            t={};check(dispatch(move_override,t)==StepResult::unsupported && moves==0,
                       "1626 requires its reset consumer before enabling override");
            adapter.clear_char_move_anim_override=[&](int32_t ped){
                ++clears;check(ped==27,"Animation-group setter clears override for its own actor");
                NativeResult r;r.executed=clear_ok;return r;
            };
            t={};t.condition=true;
            check(dispatch(move_override,t)==StepResult::continued && t.ip==71 && t.condition && moves==1,
                  "1626 consumes one operand and preserves the comparison flag");
            const auto saved_style_executor=adapter.execute;
            adapter.execute=[&](const NativeCall& call){
                check(call.source_entity_command==EntityCommand::char_locomotion_group,
                      "Style reset follows the actual native animation-group setter");
                NativeResult r;r.executed=style_ok;r.native_entered=style_ok;
                if(style_ok)++style_writes;return r;
            };
            auto style_reset=program(COMMAND_SET_ANIM_GROUP_FOR_CHAR,{27,43});
            t={};check(dispatch(style_reset,t)==StepResult::continued && style_writes==1 && clears==1,
                       "Successful style change clears the source move override");
            style_ok=false;t={};
            check(dispatch(style_reset,t)==StepResult::unsupported && t.ip==64 && clears==1,
                  "A refused style write cannot clear the existing override");
            style_ok=true;clear_ok=false;t={};
            check(dispatch(style_reset,t)==StepResult::failed && style_writes==2 && clears==2,
                  "Clear failure after a real style write never permits native replay");
            adapter.execute=saved_style_executor;
            move_ok=false;move_entered=true;t={};
            check(dispatch(move_override,t)==StepResult::failed && moves==2,
                  "1626 uncertain native reference registration cannot replay");
            adapter.override_char_move_anim={};adapter.clear_char_move_anim_override={};
        }
        auto no_objective=program(COMMAND_IS_CHAR_OBJ_NO_OBJ,{27});
        t={};t.condition=true;
        check(dispatch(no_objective,t)==StepResult::unsupported && t.ip==64 && t.condition,
              "1284 has no fabricated FALSE when its native query is absent");
        std::optional<bool> no_objective_value=true;size_t no_objective_calls=0;
        adapter.char_has_no_objective=[&](int32_t ped){
            ++no_objective_calls;check(ped==27,"1284 preserves the source actor handle");
            return no_objective_value;
        };
        t={};check(dispatch(no_objective,t)==StepResult::continued && t.ip==71 && t.condition,
                   "1284 consumes one operand and reports absence of native script objectives");
        no_objective_value=false;t={};t.condition=true;
        check(dispatch(no_objective,t)==StepResult::continued && !t.condition,
              "1284 reports current or suspended native script work as FALSE");
        auto negated_no_objective=program(COMMAND_IS_CHAR_OBJ_NO_OBJ|0x8000,{27});
        t={};check(dispatch(negated_no_objective,t)==StepResult::continued && t.condition,
                   "1284 uses VM comparison semantics for negation");
        no_objective_value.reset();t={};t.condition=true;
        check(dispatch(no_objective,t)==StepResult::unsupported && t.ip==64 && t.condition && no_objective_calls==4,
              "1284 read failure preserves condition and instruction position");
        auto car_reward=program(COMMAND_SET_CAR_IS_REWARD,{91,-7});
        t={};check(dispatch(car_reward,t)==StepResult::unsupported && t.ip==64,
                   "Reward vehicle refuses without its native storage consumer");
        size_t reward_calls=0;bool reward_ok=true,reward_entered=false;
        adapter.set_car_reward=[&](int32_t car,bool enabled){
            ++reward_calls;check(car==91 && enabled,"Reward flag preserves the source vehicle and nonzero boolean");
            NativeResult result;result.executed=reward_ok;result.native_entered=reward_entered;return result;
        };
        t={};t.condition=true;
        check(dispatch(car_reward,t)==StepResult::continued && t.ip==76 && t.condition && reward_calls==1,
              "1655 reaches native storage eligibility without changing the compare flag");
        reward_ok=false;t={};
        check(dispatch(car_reward,t)==StepResult::unsupported && t.ip==64 && reward_calls==2,
              "Reward setup refused before entry does not advance the script");
        reward_entered=true;t={};
        check(dispatch(car_reward,t)==StepResult::failed && reward_calls==3,
              "Uncertain reward hook mutation cannot replay the script instruction");

        auto player_friend=program(COMMAND_SET_CHAR_AS_PLAYER_FRIEND,{27,-891,7});
        t={};check(dispatch(player_friend,t)==StepResult::unsupported && t.ip==64,
                   "Player friend requires its native relationship consumer");
        size_t friend_calls=0;bool friend_ok=true,friend_entered=false;
        adapter.set_char_player_friend=[&](int32_t ped,bool enabled){
            ++friend_calls;check(ped==27 && enabled,"Player friend preserves actor and source boolean, ignoring operand two");
            NativeResult result;result.executed=friend_ok;result.native_entered=friend_entered;return result;
        };
        t={};t.condition=true;
        check(dispatch(player_friend,t)==StepResult::continued && t.ip==81 && t.condition && friend_calls==1,
              "1274 consumes all three operands and reaches SA relationship perception");
        friend_ok=false;t={};
        check(dispatch(player_friend,t)==StepResult::unsupported && t.ip==64 && friend_calls==2,
              "Refused relationship write cannot advance the source thread");
        friend_entered=true;t={};
        check(dispatch(player_friend,t)==StepResult::failed && friend_calls==3,
              "Relationship mutation followed by failure cannot replay");
        adapter.world_ready=false;t={};
        check(dispatch(player_friend,t)==StepResult::unsupported && friend_calls==3,
              "World gate refuses before the relationship backend");
        adapter.world_ready=true;

        auto shoot_timer=program(COMMAND_SET_CHAR_SHOOT_TIMER,{27,int32_t(0x87654321u)});
        t={};check(dispatch(shoot_timer,t)==StepResult::unsupported && t.ip==64,"Shoot timer needs its real native consumer");
        size_t shoot_timer_calls=0;bool shoot_timer_ok=true;
        adapter.set_char_shoot_timer=[&](int32_t ped,uint32_t ms){++shoot_timer_calls;check(ped==27 && ms==0x87654321u,"Shoot timer preserves raw unsigned milliseconds");return shoot_timer_ok;};
        t={};t.condition=true;
        check(dispatch(shoot_timer,t)==StepResult::continued && t.ip==76 && t.condition && shoot_timer_calls==1,
              "1585 reaches its duration consumer and preserves condition");
        shoot_timer_ok=false;t={};check(dispatch(shoot_timer,t)==StepResult::unsupported && t.ip==64,"Refused duration setup never advances source execution");
        auto threat_query=program(COMMAND_GET_CHAR_THREAT_CHAR,{27},1);
        t={};t.locals[0]=91;t.condition=true;
        check(dispatch(threat_query,t)==StepResult::unsupported && t.locals[0]==91 && t.condition,
              "1554 without a native event consumer preserves output and condition");
        size_t threat_calls=0;std::optional<int32_t> threat_value=77;
        adapter.get_char_threat_char=[&](int32_t ped){++threat_calls;check(ped==27,"Threat query receives the source actor");return threat_value;};
        t={};t.condition=true;
        check(dispatch(threat_query,t)==StepResult::continued && t.locals[0]==77 && t.ip==72 && t.condition,
              "1554 stores the actual native event source handle and preserves the condition");
        threat_value=0;t={};check(dispatch(threat_query,t)==StepResult::continued && t.locals[0]==0,
              "1554 retains the retail zero sentinel for an absent threat");
        threat_value=std::nullopt;t={};t.locals[0]=91;
        check(dispatch(threat_query,t)==StepResult::unsupported && t.locals[0]==91,
              "A failed native event read cannot be presented as no threat");
        auto malformed_threat=program(COMMAND_GET_CHAR_THREAT_CHAR,{27},1,true);
        const auto calls_before_bad_output=threat_calls;t={};
        check(dispatch(malformed_threat,t)!=StepResult::continued && threat_calls==calls_before_bad_output,
              "A malformed threat output rejects before querying the native event");
        auto stuck=program(COMMAND_IS_CHAR_STUCK,{27});
        t={};check(dispatch(stuck,t)==StepResult::unsupported,"1398 without the native stuck backend is not FALSE");
        bool alive=true;std::optional<bool> answer=true;std::vector<ObjectiveActor> seen;
        adapter.live_ped_identity=[&](int32_t handle)->std::optional<ObjectiveActor>{if(handle==27 && alive)return ObjectiveActor{27,0x1000};return {};};
        adapter.char_stuck=[&](const ObjectiveActor& actor){seen.push_back(actor);return answer;};
        t={};check(dispatch(stuck,t)==StepResult::continued && t.condition && seen.size()==1 && seen[0].handle==27 && seen[0].native_ped==0x1000,"1398 reports SA's stuck evidence for the live actor");
        answer=false;t={};check(dispatch(stuck,t)==StepResult::continued && !t.condition,"1398 reports FALSE only when SA's evidence says not stuck");
        answer=std::nullopt;t={};t.condition=true;
        check(dispatch(stuck,t)==StepResult::unsupported && t.condition,"1398 with unreadable native state refuses instead of inventing FALSE");
        alive=false;seen.clear();answer=true;t={};
        check(dispatch(stuck,t)==StepResult::unsupported && seen.empty(),"1398 with a stale actor refuses before reading native state");
    }
    {
        ScmImage image;NativeAdapter adapter(image);adapter.world_ready=true;
        auto run=[&](uint16_t opcode,std::vector<int32_t> args,Thread& t){auto vm=program(opcode,args);t.ip=64;vm.backend=[&](Vm& v,Thread& s,uint16_t op){return adapter.dispatch(v,s,op);};return vm.step(t,0);};
        Thread t;t.condition=true;
        check(run(COMMAND_IS_WORLD_STREAMING_COMPLETE,{},t)==StepResult::unsupported && t.condition,"A missing sector consumer cannot invent complete/FALSE");
        std::optional<bool> ready=false;adapter.world_streaming_complete=[&]{return ready;};
        t={};check(run(COMMAND_IS_WORLD_STREAMING_COMPLETE,{},t)==StepResult::continued && !t.condition,"Sector readiness preserves a real pending request");
        ready=true;t={};check(run(COMMAND_IS_WORLD_STREAMING_COMPLETE,{},t)==StepResult::continued && t.condition,"Sector readiness reports the production consumer result");
        ready=std::nullopt;t={};t.condition=true;check(run(COMMAND_IS_WORLD_STREAMING_COMPLETE,{},t)==StepResult::unsupported && t.condition,"Unreadable sector resources preserve the script condition");
        bool applied=false,entered=false,failed=false;int commits=0;
        adapter.commit_world_sector=[&]{++commits;NativeResult r;r.executed=!failed;r.condition=applied;r.native_entered=entered;return r;};
        t={};check(run(COMMAND_SWAP_TO_STREAMED_SECTOR,{},t)==StepResult::yielded && t.ip==64 && commits==1,"A sector awaiting resources yields before mutation");
        applied=true;t={};check(run(COMMAND_SWAP_TO_STREAMED_SECTOR,{},t)==StepResult::continued && commits==2,"A completed real sector commit continues");
        failed=true;entered=true;t={};check(run(COMMAND_SWAP_TO_STREAMED_SECTOR,{},t)==StepResult::failed,"A failed commit after mutation is never replayed");
        int bridge_calls=0;adapter.set_bridge_state=[&](int32_t state){++bridge_calls;NativeResult r;r.executed=true;check(state==1,"Bridge state reaches the backend unchanged");return r;};
        t={};check(run(COMMAND_SET_BRIDGE_STATE,{3},t)==StepResult::unsupported && !bridge_calls,"Invalid bridge mode does not mutate the world");
        t={};check(run(COMMAND_SET_BRIDGE_STATE,{1},t)==StepResult::continued && bridge_calls==1,"Bridge handler reaches the actual state consumer");
        adapter.set_bridge_state=[](int32_t){NativeResult r;r.native_entered=true;return r;};
        t={};check(run(COMMAND_SET_BRIDGE_STATE,{0},t)==StepResult::failed,"Uncertain bridge mutation requires repair without replay");
        t={};check(run(COMMAND_SET_CHAR_OBJ_HAIL_TAXI,{27},t)==StepResult::unsupported,"A missing hail-cycle consumer is not an isolated wave/NOP");
        int hails=0;adapter.hail_taxi=[&](int32_t ped,bool){++hails;check(ped==27,"Hail actor reaches the owner unchanged");NativeResult r;r.executed=true;return r;};
        t={};check(run(COMMAND_SET_CHAR_OBJ_HAIL_TAXI,{27},t)==StepResult::continued && hails==1,"874 dispatches to the owned production hail cycle");
        adapter.hail_taxi=[](int32_t,bool){NativeResult r;r.native_entered=true;return r;};
        t={};check(run(COMMAND_SET_CHAR_OBJ_HAIL_TAXI,{27},t)==StepResult::failed,"874 cannot replay an accepted stand task after a later hail-cycle failure");
    }
    {
        ScmImage image;NativeAdapter adapter(image);adapter.world_ready=true;
        auto run=[&](uint16_t op,std::initializer_list<int32_t> args,Thread& thread){auto vm=program(op,args);thread.ip=64;vm.backend=[&](Vm& v,Thread& t,uint16_t command){return adapter.dispatch(v,t,command);};return vm.step(thread,0);};
        Thread t;t.condition=true;
        check(run(COMMAND_SWITCH_SECURITY_CAMERA,{1},t)==StepResult::unsupported && t.ip==64 && t.condition,"Security effects require a real consumer before advancing");
        std::vector<bool> effects;adapter.switch_security_camera=[&](bool enabled){effects.push_back(enabled);NativeResult r;r.executed=true;return r;};
        t={};t.condition=true;check(run(COMMAND_SWITCH_SECURITY_CAMERA,{-7},t)==StepResult::continued && t.ip==71 && t.condition && effects==std::vector<bool>{true},"Security camera treats any nonzero value as enabled without changing the compare flag");
        t={};check(run(COMMAND_SWITCH_SECURITY_CAMERA,{0},t)==StepResult::continued && effects==std::vector<bool>({true,false}),"Security effect disable reaches its production owner");
        adapter.switch_security_camera=[](bool){NativeResult r;r.native_entered=true;return r;};
        t={};check(run(COMMAND_SWITCH_SECURITY_CAMERA,{1},t)==StepResult::failed,"An uncertain effect write must not replay the command");
        t={};t.condition=true;
        check(run(COMMAND_POLICE_RADIO_MESSAGE,{0,0,0},t)==StepResult::continued && t.ip==81 && t.condition,
              "943 consumes the retail queue-only command without emitting an unrelated SA last-seen event");
        {
            // Ferry commands: FerryService decides the pose; every observable answer needs the real boat.
            auto run_out=[&](uint16_t op,std::vector<int32_t> args,size_t outputs,Thread& thread){
                auto vm=program(op,args,outputs);thread.ip=64;
                vm.backend=[&](Vm& v,Thread& th,uint16_t command){return adapter.dispatch(v,th,command);};
                return vm.step(thread,0);
            };
            t={};check(run(COMMAND_ENABLE_FERRY_PATH,{0},t)==StepResult::unsupported && t.ip==64,"Without a mounted track the ferry cannot be enabled");
            std::ostringstream text;text<<40<<char(10);
            for(int i=0;i<40;++i){const double a=6.283185307179586*i/40+1.6;text<<(500.0+300.0*std::cos(a))<<' '<<(100.0*std::sin(a))<<" 0"<<char(10);}
            std::istringstream mounted(text.str());
            check(adapter.ferry_service.load_path(0,mounted),"Test track mounts");
            t={};check(run(COMMAND_ENABLE_FERRY_PATH,{0},t)==StepResult::unsupported && t.ip==64 && !adapter.ferry_service.exists(0),"Enable is refused while no physical producer exists, and creates no virtual ferry");
            t={};check(run(COMMAND_ENABLE_FERRY_PATH,{3},t)==StepResult::unsupported,"A track that does not exist cannot be enabled");
            t={};check(run(COMMAND_SET_FERRIES_DISABLED,{0,1},t)==StepResult::continued && adapter.ferry_service.disabled() &&
                       run(COMMAND_SET_FERRIES_DISABLED,{0,0},t)==StepResult::continued && !adapter.ferry_service.disabled(),"The disabled flag follows the second operand");
            bool boats[2]={false,false};int streaming=3;
            adapter.ferry_service.world.producer_available=[]()->std::optional<bool>{return true;};
            adapter.ferry_service.world.physical_ready=[&]()->std::optional<bool>{return boats[0] && boats[1];};
            adapter.ferry_service.world.apply_pose=[&](int32_t id,const FerryPose&,const std::array<float,3>&){
                FerryApply r;
                if(!boats[id]){if(streaming>0){--streaming;r.status=FerryApply::Status::pending;return r;}boats[id]=true;}
                r.status=FerryApply::Status::applied;return r;
            };
            t={};check(run(COMMAND_ENABLE_FERRY_PATH,{0},t)==StepResult::yielded && t.ip==64 && !adapter.ferry_service.physical(),"Enable retries the same instruction while the boats are being produced");
            t={};check(run(COMMAND_SKIP_FERRY_TO_NEXT_DOCK,{1},t)==StepResult::unsupported && t.ip==64,"Skipping a dock needs the physical ferry");
            t={};check(run(COMMAND_SWITCH_FERRY_COLLISION,{1},t)==StepResult::unsupported && t.ip==64,"Collision switching needs the physical ferry");
            {   Thread h;check(run_out(COMMAND_GET_FERRY_HEADING,{0},1,h)==StepResult::unsupported,"Heading is read from the physical ferry, never a virtual one");
                Thread c;check(run_out(COMMAND_GET_CLOSEST_DOCKED_FERRY,{entity_bits(0),entity_bits(0)},1,c)==StepResult::unsupported,
                    "Finding a docked ferry needs the physical ferry, otherwise the ride would start on nothing");
                Thread b;check(run_out(COMMAND_GET_FERRY_BOARDING_SPACE,{0,1,0,0},2,b)==StepResult::unsupported,"Boarding space needs the physical ferry");
            }
            for(int i=0;i<10;++i)adapter.ferry_service.tick(1000+uint32_t(i)*20,20);
            t={};check(run(COMMAND_ENABLE_FERRY_PATH,{0},t)==StepResult::continued && adapter.ferry_service.exists(1) && adapter.ferry_service.physical(),"Enable completes only once both boats exist");
            {   Thread h;check(run_out(COMMAND_GET_FERRY_HEADING,{0},1,h)==StepResult::continued,"A query right after Enable is answered from the physical ferry");
            }
            t={};check(run(COMMAND_ENABLE_FERRY_PATH,{0},t)==StepResult::continued,"Repeating Enable replays nothing");
            bool collision_ok=true;
            adapter.ferry_service.world.physical_ready=[]()->std::optional<bool>{return true;};
            adapter.ferry_service.world.set_collision=[&](int32_t,int32_t){NativeResult r;r.executed=collision_ok;return r;};
            t={};check(run(COMMAND_SKIP_FERRY_TO_NEXT_DOCK,{1},t)==StepResult::continued && run(COMMAND_SKIP_FERRY_TO_NEXT_DOCK,{9},t)==StepResult::unsupported,"Skip addresses an existing physical ferry");
            t={};check(run(COMMAND_SWITCH_FERRY_COLLISION,{1},t)==StepResult::continued && run(COMMAND_SWITCH_FERRY_COLLISION,{2},t)==StepResult::unsupported,"Collision choice is validated");
            collision_ok=false;t={};check(run(COMMAND_SWITCH_FERRY_COLLISION,{0},t)==StepResult::unsupported,"A collision switch the model cannot perform is a refusal");
            {   Thread h;check(run_out(COMMAND_GET_FERRY_HEADING,{0},1,h)==StepResult::continued,"Heading of an existing physical ferry is answered");
                Thread bad;check(run_out(COMMAND_GET_FERRY_HEADING,{9},1,bad)==StepResult::unsupported,"A missing ferry has no heading");
                Thread c;const auto r=run_out(COMMAND_GET_CLOSEST_DOCKED_FERRY,{entity_bits(0),entity_bits(0)},1,c);
                check(r==StepResult::continued && c.locals[0]==-1,"No ferry is docked at the start of the lap");
                Thread d;check(run_out(COMMAND_GET_FERRY_BOARDING_SPACE,{0,1,0,0},2,d)==StepResult::unsupported,"Without model offsets there is no boarding space even when physical");
                adapter.ferry_service.world.offsets=[]()->std::optional<FerryOffsets>{FerryOffsets o;o.car[0]={1,2,3};return o;};
                Thread e;check(run_out(COMMAND_GET_FERRY_BOARDING_SPACE,{0,1,0,0},2,e)==StepResult::continued,"With the hull points the boarding space is computed from the ferry pose");
            }
            {   // Door/ramp transitions have production producers: the script commands.
                int applied_doors=0;
                adapter.ferry_service.world.apply_door=[&](int32_t id,bool,float ratio){
                    NativeResult r;r.executed=id==0 && ratio==1.0f;++applied_doors;return r;
                };
                Thread open_t;open_t.condition=true;
                check(run_out(COMMAND_IS_FERRY_DOOR_CLOSED,{0},0,open_t)==StepResult::continued && open_t.condition,"IS_FERRY_DOOR_CLOSED reads the physical ferry (closed at the start)");
                Thread o;check(run_out(COMMAND_OPEN_FERRY_DOOR,{0},0,o)==StepResult::continued,"OPEN_FERRY_DOOR starts the transition");
                Thread q;q.condition=true;check(run_out(COMMAND_IS_FERRY_DOOR_OPEN,{0},0,q)==StepResult::continued && !q.condition,"The door is not open before the 10 s timer");
                Thread done;check(run_out(COMMAND_COMPLETE_FERRY_DOOR_MOVEMENT,{0},0,done)==StepResult::continued,"COMPLETE_FERRY_DOOR_MOVEMENT finishes the movement");
                adapter.ferry_service.tick(5000,20);
                Thread q2;check(run_out(COMMAND_IS_FERRY_DOOR_OPEN,{0},0,q2)==StepResult::continued && q2.condition && applied_doors==1,"After completion the physical endpoint has been applied and the door is open");
                Thread c;check(run_out(COMMAND_CLOSE_FERRY_DOOR,{0},0,c)==StepResult::continued,"CLOSE_FERRY_DOOR starts closing");
                Thread bad;check(run_out(COMMAND_OPEN_FERRY_DOOR,{9},0,bad)==StepResult::unsupported,"A missing ferry has no door");
            }
            adapter.ferry_service.clear();adapter.ferry_service.world={};
        }
        {
            // IS_CRANE_LIFTING_CAR is answered only by the physical crane service, never by a handle or a flag.
            t={};t.condition=true;
            check(run(COMMAND_IS_CRANE_LIFTING_CAR,{entity_bits(1.0f),entity_bits(2.0f),42},t)==StepResult::unsupported && t.ip==64,
                "Without the physical crane consumer the lift query stays unsupported");
            adapter.crane_service.declare({0,0,0});
            adapter.crane_service.world.crane_ready=[](size_t)->std::optional<bool>{return true;};
            bool lookup_available=true;
            adapter.crane_service.world.vehicle_lookup=[&](int32_t){RouteVehicleLookup result;result.status=lookup_available?VehicleLookupStatus::absent:VehicleLookupStatus::unavailable;return result;};
            t={};t.condition=true;
            check(run(COMMAND_IS_CRANE_LIFTING_CAR,{entity_bits(1.0f),entity_bits(2.0f),42},t)==StepResult::continued && t.ip==81 && !t.condition,
                "A car the crane never took is a known FALSE with the compare flag written by the service");
            lookup_available=false;t={};
            check(run(COMMAND_IS_CRANE_LIFTING_CAR,{entity_bits(1.0f),entity_bits(2.0f),42},t)==StepResult::unsupported,
                "An unreadable vehicle pool is not FALSE");
        }
        size_t paths=0;adapter.mission_script=[](const Thread&){return true;};
        adapter.char_follow_path=[&](int32_t ped,const std::array<float,3>& at,float radius,int32_t mode,bool mission){
            ++paths;check(ped==17 && at==std::array<float,3>{1,2,3} && radius==7 && mode==1 && mission,"All six original path operands and mission context reach the native consumer");
            NativeResult r;r.executed=true;r.native_entered=true;return r;
        };
        t={};t.condition=true;check(run(COMMAND_CHAR_FOLLOW_PATH,{17,entity_bits(1),entity_bits(2),entity_bits(3),entity_bits(7),1},t)==StepResult::continued && t.ip==96 && t.condition && paths==1,"Path instruction consumes six operands and preserves the compare flag");
        t={};check(run(COMMAND_CHAR_FOLLOW_PATH,{17,0,0,0,int32_t(0x7F800000),0},t)==StepResult::unsupported && paths==1 && t.ip==64,"Invalid path radius refuses before replacing any task");
        adapter.char_follow_path=[](int32_t,const auto&,float,int32_t,bool){NativeResult r;r.native_entered=true;return r;};
        t={};check(run(COMMAND_CHAR_FOLLOW_PATH,{17,0,0,0,0,0},t)==StepResult::failed,"Path failure after native clear must never replay the instruction");
        int32_t observed_area=-1;bool area_accepted=true;
        adapter.flat_native_world=true;adapter.source_music_area_changed=[&](int32_t area){observed_area=area;};
        adapter.execute=[&](const NativeCall& call){check(call.opcode==0x04BB && call.inputs==std::vector<int32_t>{0},"Native world remains flat while music observes the original area");NativeResult r;r.executed=area_accepted;return r;};
        t={};check(run(COMMAND_SET_AREA_VISIBLE,{17},t)==StepResult::continued && observed_area==17,
                  "Accepted area setter publishes the raw source area to the CITY selection consumer");
        area_accepted=false;t={};check(run(COMMAND_SET_AREA_VISIBLE,{5},t)==StepResult::unsupported && observed_area==17,
                  "A failed native area setter never publishes an invented music selection");
    }
    std::cout<<"PASS: source entity family units, fields, generations, outputs and failure boundaries; GTA not executed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
