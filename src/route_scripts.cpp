#include "lcs/route_scripts.hpp"
#include "ScriptCommands.h"
#include <cstring>
namespace lcs {
bool RouteScripts::handles(uint16_t op){return op==COMMAND_SET_CHAR_OBJ_FOLLOW_ROUTE || op==COMMAND_ADD_ROUTE_POINT || op==COMMAND_REMOVE_ROUTE;}
StepResult RouteScripts::dispatch(Vm& vm,Thread& thread,uint16_t op) {
    if(!handles(op))return StepResult::unsupported;
    if(op==COMMAND_SET_CHAR_OBJ_FOLLOW_ROUTE) {
        const auto actor=vm.parameter(thread),route=vm.parameter(thread),mode=vm.parameter(thread);
        if(!follow_route_) {
            thread.diagnostic="FOLLOW_ROUTE source route service is unavailable";
            return StepResult::unsupported;
        }
        return follow_route_(actor,route,mode,thread);
    }
    const auto id=vm.parameter(thread);
    if(op==COMMAND_ADD_ROUTE_POINT) {
        std::array<float,3> point{};
        for(auto& value:point){const auto bits=vm.parameter(thread);std::memcpy(&value,&bits,4);}
        // Collect every operand before touching the shared original table.
        if(!table_.add(id,point)) {
            // Retail2F58E8 branches to its writer with index200 when full.
            // Contain that OOB write; never claim a successful no-op/invented slot.
            thread.diagnostic="Original route pool full or point nonfinite; unsafe write refused";
            return StepResult::unsupported;
        }
        return StepResult::continued;
    }
    if(!table_.remove(id)) {
        thread.diagnostic="Original route block-copy removal would read past its pool";
        return StepResult::unsupported;
    }
    return StepResult::continued;
}
}
