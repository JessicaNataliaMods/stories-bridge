#include "lcs/native.hpp"
#include "ScriptCommands.h"
#include <array>
#include <cstring>
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
    adapter.bind_garage(12,"lcsG12");NativeCall packet;size_t calls=0;bool accepted=true,condition=false,entered=false;
    adapter.execute=guard_native_executor([&](const NativeCall& call){++calls;packet=call;NativeResult result;result.executed=accepted;result.condition=condition;result.native_entered=entered;return result;});
    auto step=[&](uint16_t op,std::vector<int32_t> inputs,bool initial=false){auto vm=program(op,inputs);Thread t;t.ip=64;t.condition=initial;
        vm.backend=[&](Vm& v,Thread& th,uint16_t code){return adapter.dispatch(v,th,code);};const auto result=vm.step(t,0);
        check(t.ip==(result==StepResult::continued?vm.memory.size():64),"Original garage instruction consumption/rollback");return std::pair<StepResult,Thread>(result,t);};
    for(auto op:{COMMAND_IS_CAR_IN_MISSION_GARAGE,COMMAND_HAS_RESPRAY_HAPPENED}) {
        for(bool native:{false,true}){condition=native;const auto r=step(uint16_t(op),{12},!native);
            check(r.first==StepResult::continued && r.second.condition==native && packet.text=="LCSG12" && packet.inputs.empty() && packet.opcode==0 && packet.predicate,"Native garage result and registered name, without legacy opcode");}
    }
    condition=false;auto contains=step(COMMAND_DOES_GARAGE_CONTAIN_CAR,{12,0x10c},true);
    check(contains.first==StepResult::continued && !contains.second.condition && packet.source_garage==GarageCommand::contains_car && packet.inputs==std::vector<int32_t>({0x10c}),"Containment uses actual vehicle generation and original garage binding");
    const auto before=calls;check(step(COMMAND_DOES_GARAGE_CONTAIN_CAR,{13,0x10c}).first==StepResult::unsupported && calls==before,"Undeclared garage cannot address native array by LCS index");
    check(step(COMMAND_SET_FREE_BOMBS,{-123}).first==StepResult::continued && packet.inputs[0]==-123 && !packet.predicate,"Free-bomb policy passes the original nonzero boolean operand");
    for(int32_t enabled:{0,1,-123})check(step(COMMAND_SET_FREE_RESPRAYS,{enabled}).first==StepResult::continued &&
        packet.source_garage==GarageCommand::free_resprays && packet.opcode==0 && packet.inputs==std::vector<int32_t>({enabled}) &&
        !packet.predicate && !packet.text,"Pay and Spray free policy uses typed source service and preserves boolean operand");
    check(step(COMMAND_ARM_CAR_WITH_BOMB,{0x10c,5}).first==StepResult::continued && packet.source_garage==GarageCommand::arm_car_bomb && packet.inputs==std::vector<int32_t>({0x10c,5}),"Arming preserves handle and source bomb selector");
    condition=true;check(step(COMMAND_IS_CAR_ARMED_WITH_BOMB,{0x10c,3}).second.condition && packet.source_garage==GarageCommand::car_bomb_type && packet.predicate,"Exact bomb type result comes from backend");
    check(step(COMMAND_DISARM_CAR_BOMB,{0x10c}).first==StepResult::continued && packet.source_garage==GarageCommand::disarm_car_bomb && !packet.predicate,"Disarm uses the original typed service");
    bool encoded=false;try{packet.encode();}catch(const std::runtime_error&){encoded=true;}check(encoded,"Garage service cannot serialize as an unsupported native opcode");
    for(auto op:{COMMAND_IS_CAR_ARMED_WITH_BOMB,COMMAND_ARM_CAR_WITH_BOMB})for(int32_t invalid:{-1,6,7,256}) {
        const auto count=calls;check(step(uint16_t(op),{0x10c,invalid}).first==StepResult::unsupported && calls==count,"Unknown bomb type refused before native entry");
    }
    adapter.world_ready=false;const auto unmounted=calls;
    check(step(COMMAND_SET_FREE_BOMBS,{1}).first==StepResult::unsupported && calls==unmounted,"Unmounted campaign cannot change native garage policy");adapter.world_ready=true;
    accepted=false;auto refusal=step(COMMAND_HAS_RESPRAY_HAPPENED,{12},true);
    check(refusal.first==StepResult::unsupported && refusal.second.condition,"Refused access cannot invent a mission predicate");
    entered=true;const auto failure=step(COMMAND_HAS_RESPRAY_HAPPENED,{12});
    check(failure.first==StepResult::failed && failure.second.diagnostic.find("requires repair")!=std::string::npos,"Consumed respray/fault cannot be retried as a successful predicate");
    {
        // Import/Export collector: LCS-owned service garage, answered from SA's saved CarTypesCollected.
        // A real native garage is never created for an exclusive type; this producer must stay untouched.
        adapter.create_garage=[&](const std::array<float,8>&,int32_t,int32_t)->std::optional<std::string>{check(false,"Native garage created for an exclusive type");return std::nullopt;};
        auto declare=[&](int32_t type){
            std::vector<uint8_t> bytes(64);bytes.push_back(uint8_t(COMMAND_SET_GARAGE));bytes.push_back(uint8_t(COMMAND_SET_GARAGE>>8));
            const float bounds[8]={-5,-5,0,5,-5,-5,5,5};
            for(const float value:bounds){uint32_t bits;std::memcpy(&bits,&value,4);bytes.push_back(6);for(int i=0;i<4;++i)bytes.push_back(uint8_t(bits>>(8*i)));}
            bytes.push_back(6);for(int i=0;i<4;++i)bytes.push_back(uint8_t(uint32_t(type)>>(8*i)));
            bytes.push_back(12);
            Vm vm(std::move(bytes),64,64);Thread t;t.ip=64;
            vm.backend=[&](Vm& v,Thread& th,uint16_t code){return adapter.dispatch(v,th,code);};
            const auto result=vm.step(t,0);check(result==StepResult::continued,"SET_GARAGE of an exclusive service type is accepted");
            return t.locals[0];
        };
        const auto before_declare=calls;
        const int32_t import_garage=declare(8);
        check(adapter.exclusive_garages.has(import_garage) && calls==before_declare,
            "A collector type is declared in the LCS-owned service and never as a native hideout");
        check(step(COMMAND_HAS_IMPORT_GARAGE_SLOT_BEEN_FILLED,{import_garage,1}).first==StepResult::unsupported,
            "Without the saved SA list the command is unsupported, not FALSE");
        std::array<uint32_t,4> saved{};bool readable=true;
        adapter.exclusive_garages.world.collected_read=[&](uint32_t index)->std::optional<uint32_t>{
            if(!readable || index>=saved.size())return std::nullopt;return saved[index];
        };
        const auto before_query=calls;
        auto empty=step(COMMAND_HAS_IMPORT_GARAGE_SLOT_BEEN_FILLED,{import_garage,1},true);
        check(empty.first==StepResult::continued && !empty.second.condition && calls==before_query,
            "An empty saved list answers FALSE from SA memory without a native script packet");
        saved[0]=1u|(1u<<15);
        check(step(COMMAND_HAS_IMPORT_GARAGE_SLOT_BEEN_FILLED,{import_garage,1}).second.condition &&
              step(COMMAND_HAS_IMPORT_GARAGE_SLOT_BEEN_FILLED,{import_garage,16}).second.condition &&
              !step(COMMAND_HAS_IMPORT_GARAGE_SLOT_BEEN_FILLED,{import_garage,2},true).second.condition,
            "The script slot is one-based over the saved list bits");
        for(const int32_t slot:{0,33,-1})
            check(step(COMMAND_HAS_IMPORT_GARAGE_SLOT_BEEN_FILLED,{import_garage,slot}).first==StepResult::unsupported,"Impossible slot has no answer");
        check(step(COMMAND_HAS_IMPORT_GARAGE_SLOT_BEEN_FILLED,{import_garage+5,1}).first==StepResult::unsupported,"Unknown garage has no answer");
        readable=false;
        check(step(COMMAND_HAS_IMPORT_GARAGE_SLOT_BEEN_FILLED,{import_garage,1}).first==StepResult::unsupported,"An unreadable saved list is not FALSE");
        readable=true;
        // CHANGE_GARAGE_TYPE: NONE retires the service; a type that needs a native garage is refused untouched.
        const int32_t second_garage=declare(8);
        const auto before_change=calls;
        check(step(COMMAND_CHANGE_GARAGE_TYPE,{second_garage,5}).first==StepResult::unsupported &&
              adapter.exclusive_garages.has(second_garage) && calls==before_change,
            "A native hideout type cannot replace an exclusive service without a native garage");
        check(step(COMMAND_CHANGE_GARAGE_TYPE,{import_garage,0}).first==StepResult::continued &&
              !adapter.exclusive_garages.has(import_garage) && calls==before_change &&
              step(COMMAND_HAS_IMPORT_GARAGE_SLOT_BEEN_FILLED,{import_garage,1}).first==StepResult::unsupported,
            "NONE retires the collector service exactly like retail ChangeGarageType, with no native garage packet");
    }
    std::cout<<"PASS: original garage name binding, delivery/respray/containment predicates, bomb selectors, preflight and entered-failure isolation; engine mocked\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
