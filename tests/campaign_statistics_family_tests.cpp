#include "lcs/native.hpp"
#include "ScriptCommands.h"
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace lcs;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static Vm program(uint16_t opcode,std::optional<int32_t> operand) {
    std::vector<uint8_t> b(64);b.push_back(uint8_t(opcode));b.push_back(uint8_t(opcode>>8));
    if(operand){b.push_back(6);for(int n=0;n<4;++n)b.push_back(uint8_t(uint32_t(*operand)>>(n*8)));}
    return Vm(std::move(b),64,64);
}
int main(){try{
    ScmImage image;NativeAdapter adapter(image);adapter.world_ready=true;std::array<int32_t,223> stats{};NativeCall packet;
    size_t calls=0;bool accepted=true,entered=false;
    adapter.execute=guard_native_executor([&](const NativeCall& call){packet=call;++calls;NativeResult r;r.executed=accepted;r.native_entered=entered;
        check(call.source_statistic && !call.source_entity_command && !call.opcode && call.inputs.size()==1 && !call.outputs && !call.predicate,"Statistics have a dedicated typed packet");
        if(accepted){const auto slot=source_stat_slot(*call.source_statistic);check(bool(slot),"Statistic must have a matching native counter");stats[*slot]=source_stat_update(*call.source_statistic,stats[*slot],call.inputs[0]);}return r;});
    auto step=[&](Vm& vm,Thread& t){t.ip=64;vm.backend=[&](Vm& v,Thread& thread,uint16_t op){return adapter.dispatch(v,thread,op);};return vm.step(t,0);};
    struct Scenario{uint16_t opcode;SourceStatistic stat;bool operand;};
    const Scenario commands[]={
        {COMMAND_REGISTER_MONEY_MADE_TAXI,SourceStatistic::taxi_money,true},
        {COMMAND_REGISTER_PASSENGER_DROPPED_OFF_TAXI,SourceStatistic::taxi_passengers,false},
        {COMMAND_REGISTER_LIFE_SAVED,SourceStatistic::ambulance_lives,false},
        {COMMAND_REGISTER_CRIMINAL_CAUGHT,SourceStatistic::vigilante_criminals,false},
        {COMMAND_REGISTER_FIRE_EXTINGUISHED,SourceStatistic::extinguished_fires,false},
        {COMMAND_REGISTER_VIGILANTE_LEVEL,SourceStatistic::vigilante_level,true},
        {COMMAND_REGISTER_AMBULANCE_LEVEL,SourceStatistic::ambulance_level,true},
        {COMMAND_REGISTER_FIRE_LEVEL,SourceStatistic::fire_level,true}
    };
    for(const auto& command:commands) {
        const auto slot=*source_stat_slot(command.stat);stats[slot]=1000001;const auto before=stats;
        auto vm=program(command.opcode,command.operand?std::optional<int32_t>(1000002):std::nullopt);Thread t;
        check(step(vm,t)==StepResult::continued && packet.source_statistic==command.stat && packet.inputs[0]==(command.operand?1000002:1) && t.ip==vm.memory.size(),"Source counter and operand count must survive translation");
        const auto expected=source_stat_record(command.stat)?1000002:1000001+(command.operand?1000002:1);
        check(stats[slot]==expected,"Integer totals cannot truncate at 32767 or use float bits");
        for(size_t n=0;n<stats.size();++n)if(n!=slot)check(stats[n]==before[n],"An original counter update cannot corrupt another native statistic");
        accepted=false;Thread rejected;const auto old=stats;
        check(step(vm,rejected)==StepResult::unsupported && rejected.ip==64 && stats==old,"Rejected statistic keeps instruction and counters unchanged");accepted=true;
        adapter.world_ready=false;Thread gated;const auto no_call=calls;
        check(step(vm,gated)==StepResult::unsupported && calls==no_call,"Stats cannot write before native initialization");adapter.world_ready=true;
        bool encoding_failed=false;try{packet.encode();}catch(const std::runtime_error&){encoding_failed=true;}
        check(encoding_failed,"Typed stat update cannot enter the native zero opcode");
        if(command.operand){auto lower=program(command.opcode,-7);Thread signed_value;const auto previous=stats[slot];
            check(step(lower,signed_value)==StepResult::continued && stats[slot]==(source_stat_record(command.stat)?previous:previous-7),"Signed taxi increments subtract and smaller level records preserve the maximum");}
    }
    check(source_stat_update(SourceStatistic::taxi_money,20000000,1)==20000001,"Counter update must preserve integer precision beyond float's exact range");
    check(source_stat_update(SourceStatistic::taxi_money,std::numeric_limits<int32_t>::max(),1)==std::numeric_limits<int32_t>::min(),"Retail counter overflow is defined through uint32 rather than C++ signed overflow");
    check(!source_stat_slot(SourceStatistic(154)) && !source_stat_slot(SourceStatistic(342)),"Unrelated native stats do not grant support for original-only activities");
    accepted=false;entered=true;auto vm=program(COMMAND_REGISTER_MONEY_MADE_TAXI,100);Thread partial;
    check(step(vm,partial)==StepResult::failed && partial.diagnostic.find("replay rejected after entry")!=std::string::npos,"A partially written statistic cannot be replayed and double counted");
    std::cout<<"PASS: eight original service statistics retain signed integer totals, maxima, isolation and repair boundary; GTA not executed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
