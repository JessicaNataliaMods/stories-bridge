#include "lcs/native.hpp"
#include "ScriptCommands.h"
#include <iostream>
#include <stdexcept>
using namespace lcs;
static void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
static Vm program(uint16_t opcode,std::vector<int32_t> numbers,const std::string* text=nullptr,size_t outputs=0) {
    std::vector<uint8_t> code(64);code.push_back(uint8_t(opcode));code.push_back(uint8_t(opcode>>8));
    if(text)for(size_t i=0;i<8;++i)code.push_back(i<text->size()?uint8_t((*text)[i]):0);
    for(auto n:numbers){code.push_back(6);for(int i=0;i<4;++i)code.push_back(uint8_t(uint32_t(n)>>(i*8)));}
    for(size_t i=0;i<outputs;++i)code.push_back(uint8_t(12+i));
    return Vm(std::move(code),64,64);
}
int main(){try{
    ScmImage image;image.main_size=image.variable_space=64;image.bytes.resize(64);NativeAdapter adapter(image);
    adapter.world_ready=adapter.text_ready=adapter.equivalent_entities=true;
    adapter.bind_model(12,105,"ped",ModelKind::ped);adapter.bind_model(123,445,"vehicle",ModelKind::vehicle);adapter.bind_model(66,10000,"object",ModelKind::object);
    NativeCall packet;int32_t status=0;size_t calls=0,starts=0;bool accept=true,entered=false;int32_t current_weapon=31;
    adapter.execute=guard_native_executor([&](const NativeCall& call){++calls;NativeResult result;result.executed=true;
        if(call.opcode==0x01FA){result.values[0]=status;return result;}
        if(call.opcode==0x01F5){result.values[0]=0x107;return result;}
        if(call.source_frenzy_headshots){packet=call;++starts;result.executed=accept;result.native_entered=entered;if(accept)status=1;return result;}
        if(call.outputs)result.values[0]=current_weapon;return result;
    });
    auto step=[&](Vm& vm,Thread& thread){thread.ip=64;vm.backend=[&](Vm& v,Thread& t,uint16_t op){return adapter.dispatch(v,t,op);};return vm.step(thread,0);};
    const std::string label="rampage";
    for(auto opcode:{uint16_t(COMMAND_START_KILL_FRENZY),uint16_t(COMMAND_START_KILL_FRENZY_HEADSHOT)}) {
        status=0;auto vm=program(opcode,{17,120000,65537,-1,12,123,-2,238},&label);Thread thread;
        check(step(vm,thread)==StepResult::continued && thread.ip==vm.memory.size() && packet.opcode==0 && packet.text=="RAMPAGE" &&
            packet.source_frenzy_headshots==std::optional<bool>(opcode==COMMAND_START_KILL_FRENZY_HEADSHOT) &&
            packet.inputs==std::vector<int32_t>({22,120000,65537,-1,105,445,-2,238}),"Both variants preserve exact original arguments and require a typed headshot flag");
        bool encode_rejected=false;try{packet.encode();}catch(const std::runtime_error&){encode_rejected=true;}
        check(encode_rejected,"Typed rampage cannot fall through empty native 0367 or ordinary opcode serialization");
        status=2;
    }
    for(int32_t invalid:{-1,37,38,100}) {auto vm=program(COMMAND_START_KILL_FRENZY,{invalid,1000,5,-1,-1,-1,-1,0},&label);Thread t;const auto before=starts;
        check(step(vm,t)==StepResult::unsupported && t.ip==64 && starts==before,"Unmapped source weapon cannot start a substitute rampage");}
    for(int32_t target:{66,777}) {auto vm=program(COMMAND_START_KILL_FRENZY,{17,1000,5,target,-1,-1,-1,0},&label);Thread t;const auto before=starts;
        check(step(vm,t)==StepResult::unsupported && starts==before,"Missing/object target cannot silently become any ped");}
    status=2;
    for(const auto& pair:std::vector<std::pair<int32_t,int32_t>>{{39,49},{40,50},{41,51},{42,52},{43,53},{44,54},{45,55},{46,56},{47,57}}) {
        auto vm=program(COMMAND_START_KILL_FRENZY,{pair.first,-1,10,-2,-1,-1,-1,0},&label);Thread t;
        check(step(vm,t)==StepResult::continued && packet.inputs[0]==pair.second && packet.inputs[1]==-1,"Damage-class selector/time sentinel retain original meaning");status=2;
    }
    // The deferred LCS M60 starts the rampage with its own SA type (country rifle, docs/weapons.md).
    // The source identity must follow the temporary rampage then be restored.
    auto give=program(COMMAND_GIVE_WEAPON_TO_PLAYER,{0,26,100});Thread given;check(step(give,given)==StepResult::continued,"Set original interrupted M4 identity");
    auto frenzy=program(COMMAND_START_KILL_FRENZY,{32,10000,5,-1,-1,-1,-1,1},&label);Thread started;
    check(step(frenzy,started)==StepResult::continued && packet.inputs[0]==33,"Deferred LCS M60 uses its own SA weapon type");
    auto query=program(COMMAND_GET_CURRENT_PLAYER_WEAPON,{0},nullptr,1);Thread during;
    auto malformed=query;malformed.memory.back()=204;Thread invalid;const auto no_query=calls;
    check(step(malformed,invalid)==StepResult::failed && calls==no_query,"Malformed weapon output rejects before any rampage status query");
    current_weapon=33; // the rampage hands the player the M60's own SA type
    check(step(query,during)==StepResult::continued && during.locals[0]==32,"Actual native rampage weapon keeps its original LCS identity");
    current_weapon=31;status=2;Thread after;check(step(query,after)==StepResult::continued && after.locals[0]==26,"Finished rampage restores interrupted source identity instead of retaining temporary M60 alias");
    accept=false;status=0;Thread refused;check(step(frenzy,refused)==StepResult::unsupported && refused.ip==64,"Rejected native start retains the original instruction");
    entered=true;Thread partial;check(step(frenzy,partial)==StepResult::failed && partial.ip==64 && partial.diagnostic.find("requires repair")!=std::string::npos,"Partial native start cannot be retried or counted as mission success");
    entered=false;accept=true;adapter.world_ready=false;Thread unmounted;const auto before=calls;
    check(step(frenzy,unmounted)==StepResult::unsupported && calls==before,"Unmounted world cannot create a rampage");adapter.world_ready=true;
    adapter.text_ready=false;Thread untranslated;check(step(frenzy,untranslated)==StepResult::unsupported && calls==before,"Unmounted text refuses without a native start");
    std::cout<<"PASS: original ordinary/headshot rampage packets, typed dispatch, weapon/model selectors, native-status alias restoration and no-replay faults; engine mocked\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
