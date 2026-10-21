#include "lcs/collectives.hpp"
#include "ScriptCommands.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
namespace lcs {
bool Collectives::handles(uint16_t op){
    return op==COMMAND_ADD_PEDS_IN_AREA_TO_COLL || op==COMMAND_CLEAR_COLL || op==COMMAND_SET_COLL_OBJ_FLEE_CHAR_ON_FOOT_ALWAYS ||
        op==COMMAND_SET_COLL_OBJ_FLEE_ON_FOOT_TILL_SAFE || op==COMMAND_SET_COLL_OBJ_FLEE_CHAR_ON_FOOT_TILL_SAFE;
}
StepResult Collectives::dispatch(Vm& vm,Thread& thread,uint16_t op){
    if(!handles(op))return StepResult::unsupported;
    if(poisoned_)throw std::runtime_error("Collective native operation faulted; replay prohibited");
    if(op==COMMAND_CLEAR_COLL){
        const auto group=vm.parameter(thread);
        for(auto& entry:entries_)if(entry.group==group)entry={};
        return StepResult::continued;
    }
    if(op==COMMAND_ADD_PEDS_IN_AREA_TO_COLL){
        std::array<float,3> area{};
        for(auto& f:area){const auto value=vm.parameter(thread);std::memcpy(&f,&value,4);}
        auto cursor=thread;vm.variable(cursor);
        if(!available || !available() || !candidates || !live){thread.diagnostic="Collective world query unavailable";return StepResult::unsupported;}
        for(auto value:area)if(!std::isfinite(value)){thread.diagnostic="Collective area nonfinite";return StepResult::unsupported;}
        const auto found=candidates(area[0],area[1],area[2]);
        if(!found || !available()){thread.diagnostic="Collective world query failed";return StepResult::unsupported;}
        // Snapshot identity is revalidated before the source table/output commits.
        for(const auto& item:*found){const auto actor=live(item.actor.handle);if(!actor || !(*actor==item.actor)){
            thread.diagnostic="Collective candidate changed during query";return StepResult::unsupported;}}
        const auto group=next_;next_=next_==std::numeric_limits<int32_t>::max()?0:next_+1;
        for(const auto& item:*found){
            if((item.type>=0 && item.type<=3) || item.type==6 || item.created_by==2)continue;
            for(auto& entry:entries_)if(entry.group==-1){entry={group,item.actor.handle};break;}
        }
        vm.store(thread,group);return StepResult::continued;
    }
    // 606 consumes only the group; ALWAYS and 614 also consume the threat character.
    const bool point=op==COMMAND_SET_COLL_OBJ_FLEE_ON_FOOT_TILL_SAFE;
    const auto group=vm.parameter(thread);
    const auto target_handle=point?-1:vm.parameter(thread);
    const auto& character_flee=op==COMMAND_SET_COLL_OBJ_FLEE_CHAR_ON_FOOT_TILL_SAFE?flee_till_safe:flee;
    if(!available || !available() || !live || (point?!flee_point_till_safe:!character_flee)){thread.diagnostic="Collective flee backend unavailable";return StepResult::unsupported;}
    std::optional<ObjectiveActor> target;
    if(!point){
        target=live(target_handle);
        if(!target){thread.diagnostic="Collective flee target unavailable";return StepResult::unsupported;}
    }
    bool entered=false;
    for(auto& entry:entries_)if(entry.group==group){
        if(!available()){
            if(entered){poisoned_=true;throw std::runtime_error("Collective pool unavailable after partial native acceptance");}
            thread.diagnostic="Collective pool unavailable";return StepResult::unsupported;
        }
        const auto actor=live(entry.ped);
        if(!actor){entry={};continue;} // GetAt stale generation retires only this entry.
        if(!point){
            const auto current_target=live(target_handle);
            if(!current_target || !(*current_target==*target)){
                if(entered){poisoned_=true;throw std::runtime_error("Collective target changed after partial native acceptance");}
                thread.diagnostic="Collective target changed";return StepResult::unsupported;
            }
        }
        NativeResult result;
        try {result=point?flee_point_till_safe(*actor):character_flee(*actor,*target);}catch(...){poisoned_=true;throw;}
        if(!result.executed){
            if(entered || result.native_entered){poisoned_=true;throw std::runtime_error("Collective partial native failure; replay prohibited");}
            thread.diagnostic=result.diagnostic;return StepResult::unsupported;
        }
        entered=true;
    }
    return StepResult::continued;
}
}
