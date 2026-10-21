#include "lcs/script_markers.hpp"
#include "ScriptCommands.h"
#include <cmath>
#include <cstring>

namespace lcs {
namespace {
float floating(int32_t bits){float value;std::memcpy(&value,&bits,4);return value;}
}
bool ScriptMarkers::handles(uint16_t op){return op==COMMAND_ADD_POINT_3D_MARKER || op==COMMAND_ADD_ARROW_3D_MARKER;}
StepResult ScriptMarkers::dispatch(Vm& vm,Thread& t,uint16_t op){
    if(!handles(op))return StepResult::unsupported;
    ScriptMarkerCommand command;
    auto& p=command.placement;
    p.identity=vm.peek_variable_identity(t);
    for(auto& v:p.position)v=floating(vm.parameter(t));
    const bool point=op==COMMAND_ADD_POINT_3D_MARKER;
    if(point){
        p.type=5;p.size=floating(vm.parameter(t))*.7f;
        p.pulse_period=128;p.pulse_fraction=0;p.vertical_size=100;
        p.colour[3]=255;p.force_position=true;
    }else{
        p.type=2;p.target.emplace();
        for(auto& v:*p.target)v=floating(vm.parameter(t));
        p.size=3.2f;p.pulse_period=1;p.pulse_fraction=1;
        p.vertical_size=0;p.colour[3]=200;
    }
    for(size_t i=0;i<3;++i)p.colour[i]=uint8_t(vm.parameter(t));
    if(!point){
        const float height=floating(vm.parameter(t));
        if(!std::isfinite(height)){t.diagnostic="Nonfinite original marker height";return StepResult::unsupported;}
        // Preserve the source addition order, including the seven-unit lift.
        p.position[2]+=height+7.f;
    }
    p.rotate_rate=1;
    if(!std::isfinite(p.size)){t.diagnostic="Nonfinite original marker size";return StepResult::unsupported;}
    for(auto v:p.position)if(!std::isfinite(v)){t.diagnostic="Nonfinite original marker position";return StepResult::unsupported;}
    if(p.target)for(auto v:*p.target)if(!std::isfinite(v)){t.diagnostic="Nonfinite original marker target";return StepResult::unsupported;}
    if(!available || !available() || !submit){t.diagnostic="Original marker renderer unavailable";return StepResult::unsupported;}
    if(point && p.position[2]<=-100.f){
        const auto z=ground?ground(p.position[0],p.position[1]):std::optional<float>{};
        if(!z || !std::isfinite(*z)){t.diagnostic="Original marker ground query unavailable";return StepResult::unsupported;}
        p.position[2]=*z;
    }
    if(point && p.position!=process_.previous_point)
        command.copy_before=ScriptMarkerCommand::Copy{p.identity,p.identity-1,true,true};
    if(!submit(command)){t.diagnostic="Original marker renderer rejected operation";return StepResult::unsupported;}
    if(point)process_.previous_point=p.position;
    return StepResult::continued;
}
}
