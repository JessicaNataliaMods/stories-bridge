#include "lcs/hud.hpp"
#include "ScriptCommands.h"
#include <cstring>
#include <stdexcept>

namespace lcs {
static int32_t value(const Vm& vm,uint32_t offset) {
    if(offset<8 || offset%4 || uint64_t(offset)+4>vm.variable_space)
        throw std::runtime_error("HUD variable exceeds original LCS global bank");
    int32_t result=0;std::memcpy(&result,vm.memory.data()+offset,4);return result;
}
bool HudBridge::handles(uint16_t opcode) {
    switch(opcode) {
    case COMMAND_SET_CLOCK_EVENT_WARNING:case COMMAND_DISPLAY_ONSCREEN_TIMER:
    case COMMAND_DISPLAY_ONSCREEN_TIMER_WITH_STRING:case COMMAND_CLEAR_ONSCREEN_TIMER:
    case COMMAND_DISPLAY_ONSCREEN_COUNTER:case COMMAND_DISPLAY_ONSCREEN_COUNTER_WITH_STRING:
    case COMMAND_DISPLAY_NTH_ONSCREEN_COUNTER_WITH_STRING:case COMMAND_CLEAR_ONSCREEN_COUNTER:
    case COMMAND_FREEZE_ONSCREEN_TIMER:return true;
    case COMMAND_SET_ONSCREEN_TIMER_COLOUR:case COMMAND_SET_ONSCREEN_TIMER_BACKGROUND_COLOUR:
    case COMMAND_CHANGE_ONSCREEN_COUNTER_PREFIX:case COMMAND_DISPLAY_ONSCREEN_TIMER_WITH_NUMBER_AND_STRING:
    case COMMAND_DISPLAY_ONSCREEN_TIMER_WITH_NUMBER_AND_2_STRINGS:return true;
    default:return false;
    }
}
StepResult HudBridge::dispatch(Vm& vm,Thread& t,uint16_t opcode) {
    bool clock=false,adding=false,label=false,nth=false;
    bool numbered=false,two_labels=false;
    if(opcode==COMMAND_SET_ONSCREEN_TIMER_COLOUR || opcode==COMMAND_SET_ONSCREEN_TIMER_BACKGROUND_COLOUR){
        std::array<uint8_t,4> colour;
        for(auto& channel:colour)channel=uint8_t(vm.parameter(t));
        (opcode==COMMAND_SET_ONSCREEN_TIMER_COLOUR?pending_foreground_:pending_background_)=colour;
        return StepResult::continued;
    }
    if(opcode==COMMAND_CHANGE_ONSCREEN_COUNTER_PREFIX){
        const auto original=vm.global_reference(t);value(vm,original);
        const auto prefix=vm.parameter(t)!=0;
        for(size_t i=1;i<entries_.size();++i)if(entries_[i].active && entries_[i].original==original){
            if(!update || !update({HudRequest::Action::counter_prefix,native_cell(i),int32_t(prefix),i-1})){
                t.diagnostic="Owned source counter prefix renderer unavailable";return StepResult::unsupported;
            }
            saved_prefixes_[i-1]=prefix;break;
        }
        return StepResult::continued;
    }
    switch(opcode) {
    case COMMAND_SET_CLOCK_EVENT_WARNING: {
        // The warning is only a flashing clock on the HUD. The reLCS command
        // (Script10.cpp) consumes its three operands and does nothing, and no
        // mission progress depends on it, so a mission that enables it must
        // not be suspended for lack of the PS2 clock renderer.
        (void)vm.parameter(t);(void)vm.parameter(t);(void)vm.parameter(t);
        return StepResult::continued;
    }
    case COMMAND_DISPLAY_ONSCREEN_TIMER:clock=true;adding=true;break;
    case COMMAND_DISPLAY_ONSCREEN_TIMER_WITH_STRING:clock=true;adding=true;label=true;break;
    case COMMAND_CLEAR_ONSCREEN_TIMER:clock=true;break;
    case COMMAND_DISPLAY_ONSCREEN_COUNTER:adding=true;break;
    case COMMAND_DISPLAY_ONSCREEN_COUNTER_WITH_STRING:adding=true;label=true;break;
    case COMMAND_DISPLAY_NTH_ONSCREEN_COUNTER_WITH_STRING:adding=true;label=true;nth=true;break;
    case COMMAND_DISPLAY_ONSCREEN_TIMER_WITH_NUMBER_AND_STRING:adding=true;label=true;numbered=true;break;
    case COMMAND_DISPLAY_ONSCREEN_TIMER_WITH_NUMBER_AND_2_STRINGS:adding=true;label=true;numbered=true;two_labels=true;break;
    case COMMAND_CLEAR_ONSCREEN_COUNTER:break;
    case COMMAND_FREEZE_ONSCREEN_TIMER: {
        const auto flag=vm.parameter(t)!=0;
        if(!update || !update({HudRequest::Action::freeze,0,int32_t(flag)})) {
            t.diagnostic="Native mission clock freeze is unavailable";return StepResult::unsupported;
        }
        frozen_=flag;return StepResult::continued;
    }
    default:return StepResult::unsupported;
    }
    const auto original=vm.global_reference(t);
    const auto current=value(vm,original);
    const auto total=numbered?vm.parameter(t):-1;
    const auto raw_type=adding?vm.parameter(t):0;
    // Retail stores a halfword and its renderer chooses NUMBER only for0;
    // every other stored type follows the bar branch.
    const auto type=numbered?int32_t(uint16_t(raw_type)!=0):raw_type;
    const auto total_type=two_labels?uint16_t(vm.parameter(t)):uint16_t(0);
    const auto line_number=nth?vm.parameter(t):1;
    const int64_t line=int64_t(line_number)-1;
    std::string text=label?vm.text_label(t):std::string{};
    std::string heading;
    if(two_labels){heading=std::move(text);text=vm.text_label(t);}
    for(auto& c:text)if(c>='a' && c<='z')c-=32;
    for(auto& c:heading)if(c>='a' && c<='z')c-=32;
    if(adding && ((!clock && (type<0 || type>1 || line<0 || line>=3)) || text.size()>8)) {
        t.diagnostic="Original HUD counter type or line requires another renderer";return StepResult::unsupported;
    }
    if(!update || !read_cell || !write_cell) {
        t.diagnostic="Original mission HUD has no native variable bridge";return StepResult::unsupported;
    }
    if(!adding) {
        for(size_t i=clock?0:1;i<(clock?1:entries_.size());++i) {
            auto& entry=entries_[i];
            if(!entry.active || entry.original!=original)continue;
            if(!update({clock?HudRequest::Action::clear_clock:HudRequest::Action::clear_counter,native_cell(i)})) {
                t.diagnostic="Cannot clear owned native mission HUD entry";return StepResult::unsupported;
            }
            entry={};saved_requests_[i].reset();if(i)saved_prefixes_[i-1].reset();
        }
        return StepResult::continued;
    }
    const size_t slot=clock?0:size_t(line)+1;
    // Original AddClock/AddCounter preserve an occupied entry until CLEAR.
    if(entries_[slot].active)return StepResult::continued;
    HudRequest request{clock?HudRequest::Action::add_clock:HudRequest::Action::add_counter,
                       native_cell(slot),clock?int32_t(type!=0):type,size_t(line),text};
    constexpr std::array<uint8_t,4> white{{255,255,255,255}};
    request.total=total;request.heading=heading;request.total_type=total_type;
    request.custom=numbered || pending_foreground_!=white || (!clock && pending_background_!=white);
    request.foreground=pending_foreground_==white?(clock?std::array<uint8_t,4>{{244,225,91,255}}:request.foreground):pending_foreground_;
    request.background=pending_background_==white?request.background:pending_background_;
    if(!write_cell(native_cell(slot),current) || !update(request)) {
        t.diagnostic="Native mission HUD slot is unavailable or owned by another script";return StepResult::unsupported;
    }
    saved_requests_[slot]=request;entries_[slot]={original,true};pending_foreground_=white;if(!clock)pending_background_=white;
    return StepResult::continued;
}
bool HudBridge::pull(Vm& vm) {
    if(!entries_[0].active)return true;
    if(!read_cell)return false;
    const auto timer=read_cell(native_cell(0));if(!timer)return false;
    value(vm,entries_[0].original); // validate before writing the original bank
    std::memcpy(vm.memory.data()+entries_[0].original,&*timer,4);return true;
}
bool HudBridge::push(const Vm& vm) {
    for(size_t i=0;i<entries_.size();++i)if(entries_[i].active &&
        (!write_cell || !write_cell(native_cell(i),value(vm,entries_[i].original))))return false;
    return true;
}
bool HudBridge::clear() {
    bool ok=true;
    for(size_t i=0;i<entries_.size();++i)if(entries_[i].active) {
        if(update && update({i?HudRequest::Action::clear_counter:HudRequest::Action::clear_clock,native_cell(i)})){entries_[i]={};saved_requests_[i].reset();if(i)saved_prefixes_[i-1].reset();}
        else ok=false;
    }
    if(frozen_) {
        if(update && update({HudRequest::Action::freeze,0,0}))frozen_=false;
        else ok=false;
    }
    return ok;
}
}
