#include "lcs/script_coronas.hpp"
#include "ScriptCommands.h"
#include <cmath>
#include <cstring>
namespace lcs {
bool ScriptCoronas::handles(uint16_t op){return op==COMMAND_CREATE_SCRIPT_CORONA || op==COMMAND_REMOVE_SCRIPT_CORONA;}
StepResult ScriptCoronas::dispatch(Vm& vm,Thread& thread,uint16_t op){
    if(op==COMMAND_REMOVE_SCRIPT_CORONA){
        const auto id=uint32_t(vm.parameter(thread));
        for(auto& object:objects_)if(object.active && object.id==id){object.active=false;break;}
        return StepResult::continued;
    }
    if(op!=COMMAND_CREATE_SCRIPT_CORONA)return StepResult::unsupported;
    ScriptCorona object;
    for(auto& coordinate:object.position){const auto bits=vm.parameter(thread);std::memcpy(&coordinate,&bits,4);}
    const auto bits=vm.parameter(thread);std::memcpy(&object.size,&bits,4);
    object.type=uint8_t(vm.parameter(thread));object.flare=uint8_t(vm.parameter(thread));
    object.r=uint8_t(vm.parameter(thread));object.g=uint8_t(vm.parameter(thread));object.b=uint8_t(vm.parameter(thread));
    auto cursor=thread;vm.variable(cursor); // validate output before allocation/ground effects
    if(!available || !available() || !render || object.type>8 || object.flare>2 || !std::isfinite(object.size)){
        thread.diagnostic="Original script corona renderer unavailable or invalid type";return StepResult::unsupported;
    }
    for(auto value:object.position)if(!std::isfinite(value)){
        thread.diagnostic="Original script corona position is nonfinite";return StepResult::unsupported;
    }
    if(object.position[2]<=-100.f){
        const auto height=ground?ground(object.position[0],object.position[1]):std::optional<float>{};
        if(!height || !std::isfinite(*height)){thread.diagnostic="Original script corona ground query unavailable";return StepResult::unsupported;}
        object.position[2]=*height;
    }
    object.id=next_id_;objects_.push_front(object);++next_id_;
    int32_t output;std::memcpy(&output,&object.id,4);vm.store(thread,output);
    return StepResult::continued;
}
bool ScriptCoronas::tick()const{
    for(const auto& object:objects_)if(object.active &&
        (!available || !available() || !render || !render(reinterpret_cast<uintptr_t>(&object),object)))return false;
    return true;
}
}
