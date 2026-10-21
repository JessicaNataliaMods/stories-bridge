#include "lcs/native.hpp"
#include "ScriptCommands.h"
#include <iostream>
#include <stdexcept>
using namespace lcs;
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static Vm program(uint16_t op,std::vector<int32_t> args) {
    std::vector<uint8_t> bytes(64);bytes.push_back(uint8_t(op));bytes.push_back(uint8_t(op>>8));
    for(auto value:args){bytes.push_back(6);for(int i=0;i<4;++i)bytes.push_back(uint8_t(uint32_t(value)>>(i*8)));}
    return Vm(std::move(bytes),64,64);
}
int main(){try{
    ScmImage image;NativeAdapter adapter(image);adapter.world_ready=true;
    size_t walking_calls=0;int mode=0;NativeCall seen;
    adapter.execute=[](const NativeCall& call){NativeResult result;result.executed=true;result.condition=true;
        if(call.opcode==0x02CE)result.values[0]=entity_bits(3.f);return result;};
    adapter.execute_walking_objective=guard_native_executor([&](const NativeCall& call){
        ++walking_calls;seen=call;NativeResult result;result.executed=mode!=2 && mode!=3;result.native_entered=mode!=2;
        if(mode==3)result.diagnostic="entered fault";
        if(mode==0 || mode==4){result.task_capture=NativeResult::TaskCapture::captured;
            result.accepted_script_task=CapturedScriptTask{7,{int32_t(call.opcode),0x2000,0,0x1000,0}};
            if(mode==4)result.accepted_script_task->record.opcode^=1;}
        else result.task_capture=NativeResult::TaskCapture::no_unique_record;
        return result;
    });
    auto step=[&](uint16_t op,std::vector<int32_t> args){auto vm=program(op,std::move(args));Thread thread;thread.ip=64;
        vm.backend=[&](Vm& v,Thread& t,uint16_t code){return adapter.dispatch(v,t,code);};return vm.step(thread,0);};
    const std::vector<int32_t> goal{73,entity_bits(10.f),entity_bits(20.f)};
    check(step(COMMAND_SET_CHAR_OBJ_GOTO_COORD_ON_FOOT,goal)==StepResult::continued && walking_calls==1 &&
        seen.opcode==0x05D3 && seen.inputs==std::vector<int32_t>({73,entity_bits(10.f),entity_bits(20.f),entity_bits(3.f),4,-2}) &&
        adapter.captured_walking_task(73).has_value(),"Source goal routes its actual transformed packet through acceptance executor once");
    mode=2;check(step(COMMAND_SET_CHAR_OBJ_GOTO_COORD_ON_FOOT,goal)==StepResult::unsupported && adapter.captured_walking_task(73),"Pre-entry rejection preserves previous accepted goal metadata");
    mode=1;check(step(COMMAND_SET_CHAR_OBJ_GOTO_COORD_ON_FOOT,goal)==StepResult::continued && !adapter.captured_walking_task(73),"Capture loss after successful new goal discards old ownership without terminating original instruction");
    mode=0;check(step(COMMAND_SET_CHAR_OBJ_RUN_TO_COORD,goal)==StepResult::continued && seen.inputs[4]==6 && adapter.captured_walking_task(73),"Original run captures new task without retaining obsolete walking ownership");
    mode=3;check(step(COMMAND_SET_CHAR_OBJ_GOTO_COORD_ON_FOOT,goal)==StepResult::failed && !adapter.captured_walking_task(73),"Entered failure cannot leave previous task metadata claiming current objective");
    mode=4;check(step(COMMAND_SET_CHAR_OBJ_GOTO_COORD_ON_FOOT,goal)==StepResult::continued && !adapter.captured_walking_task(73),"Mismatched metadata is discarded without changing real command acceptance");
    mode=0;check(step(COMMAND_SET_CHAR_OBJ_GOTO_COORD_ON_FOOT,goal)==StepResult::continued && adapter.captured_walking_task(73),"New accepted objective restores only its own capture");
    check(step(COMMAND_SET_CHAR_OBJ_WAIT_ON_FOOT,{73})==StepResult::continued && !adapter.captured_walking_task(73),"Early source objective replacement clears walking acceptance metadata");
    check(step(COMMAND_SET_CHAR_OBJ_GOTO_COORD_ON_FOOT,goal)==StepResult::continued,"Owned goal can start before cleanup");adapter.stop_scripted_ped_updates();
    check(!adapter.captured_walking_task(73),"Stopping bridge updates releases copied acceptance metadata");
    check(step(COMMAND_SET_CHAR_OBJ_GOTO_COORD_ON_FOOT,goal)==StepResult::continued,"Goal accepts event for lifecycle trace");
    const auto event=*adapter.captured_walking_task(73);
    auto task=event.record;task.event=0;task.task=0x3000;task.status=1;
    adapter.walking_task_associated(8,event.record,task,0x3000);
    check(adapter.captured_walking_task(73)->record==event.record,"Foreign slot cannot associate our event");
    adapter.walking_task_associated(7,event.record,task,0x3000);
    check(adapter.captured_walking_task(73)->record==task,"Observed native association advances the copied owned event to its actual task");
    adapter.walking_task_invalidated(128);
    check(adapter.captured_walking_task(73).has_value(),"Out of range lifecycle notification cannot change metadata");
    adapter.walking_task_invalidated(7);
    check(!adapter.captured_walking_task(73),"Flush or inline Set replacement invalidates ownership without reporting completion");
    check(step(COMMAND_SET_CHAR_OBJ_GOTO_COORD_ON_FOOT,goal)==StepResult::continued,"Reused record can be accepted by a new command");
    adapter.walking_task_associated(7,event.record,task,0x4000);
    check(!adapter.captured_walking_task(73),"Unproven task argument invalidates observed slot rather than retaining obsolete event ownership");
    std::cout<<"PASS: source walking acceptance executor integration, transformed operands, refusal/entered fault, replacement and advisory capture loss; native engine mocked\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
