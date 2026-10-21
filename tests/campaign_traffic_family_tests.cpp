#include "lcs/native.hpp"
#include "ScriptCommands.h"
#include <iostream>
#include <stdexcept>
using namespace lcs;
static void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
static Vm program(uint16_t op,std::vector<int32_t> inputs) {
    std::vector<uint8_t> bytes(64);bytes.push_back(uint8_t(op));bytes.push_back(uint8_t(op>>8));
    for(auto value:inputs){bytes.push_back(6);for(int i=0;i<4;++i)bytes.push_back(uint8_t(uint32_t(value)>>(8*i)));}
    return Vm(std::move(bytes),64,64);
}
int main(){try{
    ScmImage image;image.main_size=image.variable_space=64;image.bytes.resize(64);NativeAdapter adapter(image);adapter.world_ready=true;
    NativeCall packet;size_t calls=0;bool accepted=true,entered=false;
    adapter.execute=guard_native_executor([&](const NativeCall& call){++calls;packet=call;NativeResult r;r.executed=accepted;r.native_entered=entered;return r;});
    auto step=[&](uint16_t op,std::vector<int32_t> inputs){auto vm=program(op,inputs);Thread t;t.ip=64;t.condition=true;
        vm.backend=[&](Vm& v,Thread& th,uint16_t code){return adapter.dispatch(v,th,code);};const auto result=vm.step(t,0);
        check(t.ip==(result==StepResult::continued?vm.memory.size():64),"Traffic operand consumption/rollback");
        check(t.condition,"Traffic non-predicate must preserve compare flag");return std::pair<StepResult,Thread>(result,t);};
    struct Scenario{uint16_t original;TrafficCommand action;std::vector<int32_t> inputs;};
    const Scenario scenarios[]={
        {COMMAND_ENABLE_EMERGENCY_VEHICLES,TrafficCommand::emergency_vehicles,{-123}},
        {COMMAND_SET_GENERATE_CARS_AROUND_CAMERA,TrafficCommand::cars_around_camera,{0}},
        {COMMAND_DONT_REMOVE_CAR,TrafficCommand::release_vehicle_cleanup,{0x10c}},
        {COMMAND_POP_CAR_BOOT_USING_PHYSICS,TrafficCommand::pop_boot_physics,{0x10c}},
        {COMMAND_SET_TANK_DETONATE_CARS,TrafficCommand::tank_detonates_cars,{0x10c,-1}},
        {COMMAND_SET_DRUNK_INPUT_DELAY,TrafficCommand::drunk_input_delay,{1,9}}
    };
    for(const auto& s:scenarios) {
        check(step(s.original,s.inputs).first==StepResult::continued && packet.source_traffic==s.action && packet.inputs==s.inputs &&
            !packet.opcode && !packet.predicate && !packet.outputs && !packet.text,"Traffic uses reviewed typed operation and preserves original inputs");
        bool refused=false;try{packet.encode();}catch(const std::runtime_error&){refused=true;}check(refused,"Typed traffic cannot enter legacy opcode dispatcher");
        adapter.world_ready=false;const auto before=calls;check(step(s.original,s.inputs).first==StepResult::unsupported && calls==before,"Unmounted traffic operation refuses before entry");adapter.world_ready=true;
        accepted=false;check(step(s.original,s.inputs).first==StepResult::unsupported,"Native refusal retains original instruction, without fabricated completion");accepted=true;
    }
    for(int32_t pad:{-1,2,1234}){const auto before=calls;check(step(COMMAND_SET_DRUNK_INPUT_DELAY,{pad,1}).first==StepResult::unsupported && calls==before,"Pad selector cannot escape two native pads");}
    for(int32_t delay:{-1,10,1234}){const auto before=calls;check(step(COMMAND_SET_DRUNK_INPUT_DELAY,{0,delay}).first==StepResult::unsupported && calls==before,"Drunk steering cannot exceed ten-sample native buffer");}
    accepted=false;entered=true;const auto partial=step(COMMAND_DONT_REMOVE_CAR,{0x10c});
    check(partial.first==StepResult::failed && partial.second.diagnostic.find("requires repair")!=std::string::npos,"Entered cleanup fault cannot remove/recount entities on replay");
    std::cout<<"PASS: original traffic flags, vehicle cleanup, boot, tank policy and steering operand boundaries; engine mocked\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
