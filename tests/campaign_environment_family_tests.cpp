#include "lcs/native.hpp"
#include "ScriptCommands.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace lcs;
static void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
static Vm program(uint16_t opcode,std::vector<int32_t> inputs={}) {
    std::vector<uint8_t> code(64);code.push_back(uint8_t(opcode));code.push_back(uint8_t(opcode>>8));
    for(auto value:inputs){code.push_back(6);for(int i=0;i<4;++i)code.push_back(uint8_t(uint32_t(value)>>(i*8)));}
    return Vm(std::move(code),64,64);
}
int main(){try{
    ScmImage image;image.main_size=image.variable_space=64;image.bytes.resize(64);NativeAdapter adapter(image);adapter.world_ready=true;
    const std::array<int32_t,4> original_weather{{entity_bits(.625f),entity_bits(.75f),8,9}};
    const std::array<int32_t,3> original_clock{{22,59,321}};
    auto weather=original_weather;auto clock=original_clock;NativeCall packet;size_t calls=0;bool accept=true,entered=false;
    adapter.execute=guard_native_executor([&](const NativeCall& call){++calls;packet=call;NativeResult result;result.executed=accept;result.native_entered=entered;
        if(!accept)return result;
        if(call.source_environment) switch(*call.source_environment) {
        case EnvironmentCommand::read_weather:std::copy(weather.begin(),weather.end(),result.values.begin());break;
        case EnvironmentCommand::restore_weather_and_store_clock:
            std::copy(clock.begin(),clock.end(),result.values.begin());std::copy_n(call.inputs.begin(),4,weather.begin());break;
        case EnvironmentCommand::read_clock:std::copy(clock.begin(),clock.end(),result.values.begin());break;
        case EnvironmentCommand::restore_clock:std::copy_n(call.inputs.begin(),3,clock.begin());break;
        }return result;
    });
    auto step=[&](uint16_t opcode,std::vector<int32_t> inputs={}){auto vm=program(opcode,inputs);Thread t;t.ip=64;
        vm.backend=[&](Vm& v,Thread& th,uint16_t op){return adapter.dispatch(v,th,op);};const auto result=vm.step(t,0);
        if(result==StepResult::continued)check(t.ip==vm.memory.size(),"Original instruction consumed exactly once");
        else check(t.ip==64,"Rejected source instruction preserved");return std::pair<StepResult,Thread>(result,t);};
    check(step(COMMAND_STORE_WEATHER).first==StepResult::continued && packet.source_environment==EnvironmentCommand::read_weather && packet.outputs==4 && packet.inputs.empty(),"Store all four actual weather fields");
    check(step(COMMAND_STORE_CLOCK).first==StepResult::continued && packet.source_environment==EnvironmentCommand::read_clock && packet.outputs==3,"Clock stores exactly source hours/minutes/seconds");
    clock={{1,2,3}};
    check(step(COMMAND_RESTORE_CLOCK).first==StepResult::continued && clock==original_clock && packet.source_environment==EnvironmentCommand::restore_clock,"Restore actual stored source clock");
    weather={{entity_bits(.1f),entity_bits(0),0,1}};clock={{6,7,8}};
    check(step(COMMAND_RESTORE_WEATHER).first==StepResult::continued && weather==original_weather && packet.outputs==3 && packet.inputs==std::vector<int32_t>(original_weather.begin(),original_weather.end()),"Weather restore preserves Script3 fallthrough capture of current clock");
    clock={{15,16,17}};
    check(step(COMMAND_RESTORE_CLOCK).first==StepResult::continued && clock==std::array<int32_t,3>{{6,7,8}},"RESTORE_WEATHER stored the intervening actual clock, not the earlier clock");
    bool encoded=false;try{packet.encode();}catch(const std::runtime_error&){encoded=true;}
    check(encoded,"Typed environment operation cannot enter empty native weather opcodes");
    NativeAdapter fresh(image);fresh.world_ready=true;fresh.execute=adapter.execute;
    auto initial=program(COMMAND_RESTORE_WEATHER);Thread initial_thread;initial_thread.ip=64;
    initial.backend=[&](Vm& v,Thread& t,uint16_t op){return fresh.dispatch(v,t,op);};
    check(initial.step(initial_thread,0)==StepResult::continued && packet.inputs==std::vector<int32_t>({0,0,1,1}),"Another VM owns its source-initial sunny snapshot, not the preceding campaign snapshot");
    // A rejected capture must not replace the prior source snapshot.
    weather={{entity_bits(.5f),entity_bits(.2f),1,4}};accept=false;
    check(step(COMMAND_STORE_WEATHER).first==StepResult::unsupported,"Failed capture is observable");accept=true;
    check(step(COMMAND_RESTORE_WEATHER).first==StepResult::continued && weather==original_weather,"Failed capture retains prior weather");
    for(auto opcode:{COMMAND_STORE_WEATHER,COMMAND_RESTORE_WEATHER,COMMAND_STORE_CLOCK,COMMAND_RESTORE_CLOCK}) {
        adapter.world_ready=false;const auto before=calls;check(step(uint16_t(opcode)).first==StepResult::unsupported && calls==before,"Unmounted environment never reads or writes native fields");adapter.world_ready=true;
        accept=false;entered=true;const auto partial=step(uint16_t(opcode));
        check(partial.first==StepResult::failed && partial.second.diagnostic.find("requires repair")!=std::string::npos,"Entered failure cannot be repeated or treated as success");entered=false;accept=true;
    }
    for(int32_t source:{0,1,2,3,4,5,6,7,10,11,12,13,14}) {
        check(step(COMMAND_SET_CAR_STATUS,{0x107,source}).first==StepResult::continued && packet.opcode==0x03A2 &&
            packet.inputs==std::vector<int32_t>({0x107,source>=10?source-2:source}),"Vehicle status uses real native handler with explicit source enum translation");
    }
    for(int32_t source:{-1,8,9,15,255}){const auto before=calls;check(step(COMMAND_SET_CAR_STATUS,{0x107,source}).first==StepResult::unsupported && calls==before,"Ferry/unknown status cannot become an unrelated native status");}
    std::cout<<"PASS: original weather/clock snapshots, exact fallthrough and restoration, isolated snapshot ownership, vehicle-status enum translation and no-replay failure; engine mocked\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
