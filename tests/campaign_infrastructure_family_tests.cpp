#include "lcs/native.hpp"
#include "ScriptCommands.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace lcs;
static void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
static Vm program(uint16_t opcode,const std::vector<int32_t>& inputs,size_t outputs=0,bool malformed=false) {
    std::vector<uint8_t> code(64);code.push_back(uint8_t(opcode));code.push_back(uint8_t(opcode>>8));
    for(auto n:inputs){code.push_back(6);for(int b=0;b<4;++b)code.push_back(uint8_t(uint32_t(n)>>(b*8)));}
    for(size_t i=0;i<outputs;++i){if(malformed && i+1==outputs){code.push_back(204);code.push_back(0);}else code.push_back(uint8_t(12+i));}
    return Vm(std::move(code),64,64);
}
int main(){try{
    ScmImage image;image.variable_space=image.main_size=64;image.bytes.resize(64);NativeAdapter adapter(image);adapter.world_ready=true;
    adapter.bind_model(12,105,"test_ped",ModelKind::ped);adapter.bind_model(123,445,"test_car",ModelKind::vehicle);
    NativeCall packet;size_t calls=0;bool accept=true,condition=true,entered=false;
    adapter.execute=guard_native_executor([&](const NativeCall& call){++calls;packet=call;NativeResult r;r.executed=accept;r.condition=condition;r.native_entered=entered;
        for(size_t i=0;i<7;++i)r.values[i]=int32_t(i+101);if(call.opcode==0x0298)r.values[0]=-1;return r;});
    auto step=[&](Vm& vm,Thread& t){t.ip=64;vm.backend=[&](Vm& v,Thread& thread,uint16_t op){return adapter.dispatch(v,thread,op);};return vm.step(t,0);};
    const auto x=entity_bits(20.f),y=entity_bits(-10.f),z=entity_bits(-100.f);
    struct Case{uint16_t source,native;std::vector<int32_t> inputs,expected;size_t outputs;};
    for(const auto& s:std::vector<Case>{
        {COMMAND_ADD_BLIP_FOR_COORD_OLD,0x0167,{x,y,z,2,3},{x,y,z,2,3},1},
        {COMMAND_START_SCRIPT_FIRE,0x02CF,{x,y,z},{x,y,z,0,1},1},
        {COMMAND_CREATE_PROTECTION_PICKUP,0x04A6,{x,y,z,10000,60},{x,y,z,10000,60},1},
        {COMMAND_GET_CLOSEST_STRAIGHT_ROAD,0x04B9,{x,y,z,entity_bits(1),entity_bits(50)},{x,y,z,entity_bits(1),entity_bits(50)},7},
        {COMMAND_RESET_NUM_OF_MODELS_KILLED_BY_PLAYER,0x0297,{},{0},0},
        {COMMAND_GET_NUM_OF_MODELS_KILLED_BY_PLAYER,0x0298,{12},{0,105},1}}) {
        auto vm=program(s.source,s.inputs,s.outputs);Thread t;const auto before=calls;
        const auto status=step(vm,t);
        if(status!=StepResult::continued || calls!=before+1 || packet.opcode!=s.native || packet.inputs!=s.expected || packet.outputs!=s.outputs || t.ip!=vm.memory.size()) {
            std::cerr<<opcode_name(s.source)<<" status="<<int(status)<<" calls="<<(calls-before)<<" native="<<packet.opcode<<" ip="<<t.ip<<" diagnostic="<<t.diagnostic<<'\n';
            check(false,"Source operands form exact native packet and are consumed once");
        }
        if(s.source==COMMAND_GET_NUM_OF_MODELS_KILLED_BY_PLAYER)check(t.locals[0]==65535,"Source unsigned sixteen-bit kill count survives native sign extension");
        if(s.outputs==7)for(size_t i=0;i<7;++i)check(t.locals[i]==int32_t(i+101),"All native road endpoints and heading are stored in original destinations");
        accept=false;Thread rejected;rejected.locals[0]=321;
        check(step(vm,rejected)==StepResult::unsupported && rejected.ip==64 && rejected.locals[0]==321,"Rejected packet preserves original IP and outputs");accept=true;
        adapter.world_ready=false;Thread waiting;const auto blocked=calls;check(step(vm,waiting)==StepResult::unsupported && calls==blocked,"Unmounted world cannot create blips/fires/pickups or query native paths");adapter.world_ready=true;
        if(s.outputs){auto bad=program(s.source,s.inputs,s.outputs,true);Thread malformed;const auto no_call=calls;check(step(bad,malformed)==StepResult::failed && calls==no_call,"Malformed last destination rejects before native mutation");}
    }
    for(const auto& row:std::vector<std::pair<uint16_t,EntityCommand>>{{COMMAND_DIM_BLIP,EntityCommand::radar_brightness},{COMMAND_IS_BOAT,EntityCommand::car_is_boat},
        {COMMAND_IS_CAR_BIKE,EntityCommand::car_is_bike},{COMMAND_IS_CAR_DRIVER_BEING_JACKED,EntityCommand::car_being_jacked}}) {
        const bool predicate=row.second!=EntityCommand::radar_brightness;
        const auto args=predicate?std::vector<int32_t>{0x107}:std::vector<int32_t>{-1,1};
        for(bool invert:{false,true}) {
            auto vm=program(uint16_t(row.first|(invert?0x8000:0)),args);Thread t;
            check(step(vm,t)==StepResult::continued && packet.source_entity_command==row.second && packet.inputs==args && packet.predicate==predicate && !packet.opcode,"Explicit radar/vehicle family retains source handles, flags and predicate mode");
            if(predicate)check(t.condition==!invert,"Negated source predicate inverts the actual backend result exactly once");
        }
    }
    for(const auto& row:std::vector<std::pair<uint16_t,EntityCommand>>{{COMMAND_IS_BOAT_IN_WATER,EntityCommand::boat_in_water},
        {COMMAND_SET_HELI_ROTOR_BLADES_FULLSPEED,EntityCommand::heli_rotor_full_speed},{COMMAND_GET_PICKUP_VALUE,EntityCommand::get_pickup_value},{COMMAND_SET_PICKUP_VALUE,EntityCommand::set_pickup_value}}) {
        const bool query=row.second==EntityCommand::boat_in_water,get=row.second==EntityCommand::get_pickup_value;
        const auto args=row.second==EntityCommand::set_pickup_value?std::vector<int32_t>{0x107,-100}:std::vector<int32_t>{0x107};
        auto vm=program(row.first,args,get?1:0);Thread t;const auto before=calls;
        check(step(vm,t)==StepResult::continued && calls==before+1 && packet.source_entity_command==row.second && packet.inputs==args && packet.outputs==(get?1:0) && packet.predicate==query,"Pickup/subclass operations preserve source integer parameters and output contracts");
        if(get){check(t.locals[0]==101,"Pickup integer result is stored once");auto bad=program(row.first,args,1,true);Thread invalid;const auto no_call=calls;
            check(step(bad,invalid)==StepResult::failed && calls==no_call,"Malformed pickup result rejects before backend query");}
        accept=false;Thread refused;check(step(vm,refused)==StepResult::unsupported && refused.ip==64,"Rejected subclass operation keeps source instruction");accept=true;
        if(query){auto inverse=program(uint16_t(row.first|0x8000),args);Thread negated;check(step(inverse,negated)==StepResult::continued && !negated.condition,"Boat predicate respects source negation once");}
    }
    for(uint16_t op:{uint16_t(COMMAND_START_SCRIPT_FIRE),uint16_t(COMMAND_CREATE_PROTECTION_PICKUP),uint16_t(COMMAND_ADD_BLIP_FOR_COORD_OLD),uint16_t(COMMAND_GET_CLOSEST_STRAIGHT_ROAD)}) {
        auto args=std::vector<int32_t>{entity_bits(std::numeric_limits<float>::quiet_NaN()),y,z};
        if(op!=COMMAND_START_SCRIPT_FIRE){args.push_back(entity_bits(1));args.push_back(entity_bits(2));}
        auto vm=program(op,args,op==COMMAND_GET_CLOSEST_STRAIGHT_ROAD?7:1);Thread t;const auto before=calls;
        check(step(vm,t)==StepResult::unsupported && calls==before,"Non-finite coordinates cannot reach a native constructor/query");
    }
    auto fire=program(COMMAND_START_SCRIPT_FIRE,{x,y,z},1);Thread partial;accept=false;entered=true;
    check(step(fire,partial)==StepResult::failed && partial.ip==64 && partial.diagnostic.find("requires repair")!=std::string::npos,"Partially entered constructor cannot be retried as if no fire had been created");
    accept=true;entered=false;adapter.bind_model(124,800,"out_of_bank",ModelKind::vehicle);
    auto bad_counter=program(COMMAND_GET_NUM_OF_MODELS_KILLED_BY_PLAYER,{124},1);Thread invalid_counter;const auto before_counter=calls;
    check(step(bad_counter,invalid_counter)==StepResult::unsupported && calls==before_counter && invalid_counter.ip==64,"Valid streaming model outside actual 800-row kill bank cannot index native memory");
    std::cout<<"PASS: fourteen original radar/fire/path/kill/vehicle/pickup operations, exact parameter expansion, source unsigned counter and no-replay faults; engine mocked\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
