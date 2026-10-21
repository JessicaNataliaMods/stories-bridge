#include "lcs/hud.hpp"
#include "lcs/hud_presentation.hpp"
#include "ScriptCommands.h"
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
void check(bool v,const char* message) {if(!v)throw std::runtime_error(message);}
void op(std::vector<uint8_t>& b,uint16_t value){b.push_back(uint8_t(value));b.push_back(uint8_t(value>>8));}
void integer(std::vector<uint8_t>& b,int32_t value){b.push_back(6);for(int i=0;i<4;++i)b.push_back(uint8_t(uint32_t(value)>>(8*i)));}
void global(std::vector<uint8_t>& b,uint32_t offset){b.push_back(uint8_t(204+(offset/4>>8)));b.push_back(uint8_t(offset/4));}
lcs::Vm script(std::vector<uint8_t> code){std::vector<uint8_t> bytes(128);bytes.insert(bytes.end(),code.begin(),code.end());return lcs::Vm(bytes,128,uint32_t(bytes.size()));}
void assign(lcs::Vm& vm,uint32_t offset,int32_t value){std::memcpy(vm.memory.data()+offset,&value,4);}
int32_t value(lcs::Vm& vm,uint32_t offset){int32_t v=0;std::memcpy(&v,vm.memory.data()+offset,4);return v;}
int main(){try {
    lcs::HudBridge bridge;std::map<uint32_t,int32_t> cells;std::vector<lcs::HudRequest> history;bool available=true;
    bridge.update=[&](const lcs::HudRequest& r){if(!available)return false;history.push_back(r);return true;};
    bridge.read_cell=[&](uint32_t r)->std::optional<int32_t>{return available?std::optional<int32_t>(cells.at(r)):std::nullopt;};
    bridge.write_cell=[&](uint32_t r,int32_t v){if(!available)return false;cells[r]=v;return true;};
    auto step=[&](lcs::Vm& vm){vm.backend=[&](lcs::Vm& v,lcs::Thread& t,uint16_t code){return bridge.dispatch(v,t,code);};lcs::Thread t;t.ip=128;return vm.step(t,0);};
    std::vector<uint8_t> b;op(b,COMMAND_DISPLAY_ONSCREEN_TIMER_WITH_STRING);global(b,64);integer(b,1);b.insert(b.end(),{'V','I','C','2','_','T',0,0});auto timer=script(b);assign(timer,64,60000);
    check(step(timer)==lcs::StepResult::continued && cells[32]==60000 && history.back().label=="VIC2_T" && history.back().type==1,"original timer binds actual global reference and label");
    cells[32]=59980;check(bridge.pull(timer) && value(timer,64)==59980,"native elapsed time reaches original SCM branch variable");
    assign(timer,64,10000);check(bridge.push(timer) && cells[32]==10000,"original time penalty/reset reaches native clock");
    b.clear();op(b,COMMAND_DISPLAY_ONSCREEN_TIMER);global(b,68);integer(b,0);auto occupied=script(b);assign(occupied,68,900);
    const auto count=history.size();check(step(occupied)==lcs::StepResult::continued && history.size()==count && cells[32]==10000,"repeated display preserves original occupied clock semantics");
    b.clear();op(b,COMMAND_DISPLAY_NTH_ONSCREEN_COUNTER_WITH_STRING);global(b,72);integer(b,1);integer(b,3);b.insert(b.end(),{'H','E','A','L','T','H',0,0});auto counter=script(b);assign(counter,72,75);
    check(step(counter)==lcs::StepResult::continued && cells[44]==75 && history.back().line==2 && history.back().type==1,"third LCS counter uses independent reserved native cell");
    assign(counter,72,10);check(bridge.push(counter) && cells[44]==10,"counter tracks original objective state");
    b.clear();op(b,COMMAND_FREEZE_ONSCREEN_TIMER);integer(b,1);auto freeze=script(b);check(step(freeze)==lcs::StepResult::continued && history.back().action==lcs::HudRequest::Action::freeze && history.back().type==1,"timer freeze reaches native frame-loop clock");
    b.clear();op(b,COMMAND_CLEAR_ONSCREEN_TIMER);global(b,64);auto clear=script(b);check(step(clear)==lcs::StepResult::continued && history.back().native_offset==32,"clear uses original identity mapped to owned native clock");
    check(bridge.pull(clear),"cleared timer no longer overwrites original global");
    check(bridge.clear() && history.back().action==lcs::HudRequest::Action::freeze && history.back().type==0,"abort clears owned counters and releases clock freeze");
    b.clear();op(b,COMMAND_DISPLAY_ONSCREEN_TIMER);b.push_back(12);integer(b,1);auto local=script(b);const auto prior=history.size();check(step(local)==lcs::StepResult::failed && history.size()==prior,"local reference cannot enter native global HUD API");
    b.clear();op(b,COMMAND_DISPLAY_NTH_ONSCREEN_COUNTER_WITH_STRING);global(b,64);integer(b,0);integer(b,0);b.insert(b.end(),8,0);auto badline=script(b);check(step(badline)==lcs::StepResult::unsupported && history.size()==prior,"invalid original counter line causes no native mutation");
    b.clear();op(b,COMMAND_DISPLAY_ONSCREEN_TIMER);global(b,64);integer(b,1);auto missing=script(b);available=false;check(step(missing)==lcs::StepResult::unsupported && history.size()==prior,"unavailable native HUD stops rather than simulating a timer");
    b.clear();op(b,COMMAND_SET_CLOCK_EVENT_WARNING);integer(b,0);integer(b,0);integer(b,0);auto no_warning=script(b);check(step(no_warning)==lcs::StepResult::continued,"original MAIN may disable an inactive optional clock warning");
    b.clear();op(b,COMMAND_SET_CLOCK_EVENT_WARNING);integer(b,12);integer(b,0);integer(b,15);auto warning=script(b);check(step(warning)==lcs::StepResult::unsupported,"active PS2 clock warning cannot silently become a disabled warning");
    available=true;
    b.clear();op(b,COMMAND_SET_ONSCREEN_TIMER_COLOUR);for(auto n:{261,-1,7,80})integer(b,n);auto fg=script(b);
    check(step(fg)==lcs::StepResult::continued,"Source foreground colour stages actual low bytes");
    b.clear();op(b,COMMAND_SET_ONSCREEN_TIMER_BACKGROUND_COLOUR);for(auto n:{10,11,12,13})integer(b,n);auto bg=script(b);
    check(step(bg)==lcs::StepResult::continued,"Source background colour stages independently");
    b.clear();op(b,COMMAND_DISPLAY_ONSCREEN_TIMER_WITH_NUMBER_AND_2_STRINGS);global(b,72);
    integer(b,120);integer(b,0);integer(b,3);b.insert(b.end(),{'H','E','A','D',0,0,0,0});b.insert(b.end(),{'L','A','B','E','L',0,0,0});
    auto expanded=script(b);assign(expanded,72,-3);
    check(step(expanded)==lcs::StepResult::continued && history.back().action==lcs::HudRequest::Action::add_counter &&
          history.back().native_offset==36 && history.back().total==120 && history.back().total_type==3 &&
          history.back().label=="LABEL" && history.back().heading=="HEAD" && history.back().foreground==std::array<uint8_t,4>{{5,255,7,80}} &&
          history.back().background==std::array<uint8_t,4>{{10,11,12,13}},"Both original labels, total/type and consumed colours reach accepted native counter request");
    lcs::HudPresentations presentation;check(presentation.apply(history.back()),"Actual presentation consumer accepts VM-produced request");
    b.clear();op(b,COMMAND_CHANGE_ONSCREEN_COUNTER_PREFIX);global(b,72);integer(b,1);auto prefix=script(b);
    check(step(prefix)==lcs::StepResult::continued && presentation.apply(history.back()) &&
          presentation.entries()[1]->number(-3)=="$-3/99","VM prefix reaches same real presentation and original signed/clamped format");
    bridge.clear();
    b.clear();op(b,COMMAND_DISPLAY_ONSCREEN_TIMER_WITH_NUMBER_AND_STRING);global(b,72);integer(b,9);integer(b,0);b.insert(b.end(),{'O','N','E',0,0,0,0,0});
    auto single=script(b);assign(single,72,4);check(step(single)==lcs::StepResult::continued && history.back().total==9 && history.back().heading.empty() &&
          history.back().foreground==std::array<uint8_t,4>{{112,132,157,255}} && history.back().background==std::array<uint8_t,4>{{42,58,81,255}},
          "Single label/total uses same native counter consumer; previously consumed colours reset to authored defaults");
    check(presentation.apply(history.back()) && presentation.entries()[1]->number(4)=="4/9","New counter clears previous monetary prefix");
    check(presentation.entries()[1]->number(120)=="99/9","Retail caps current as well as total when denominator exists");
    auto negative_total=history.back();negative_total.total=-2;presentation.apply(negative_total);
    check(presentation.entries()[1]->number(-3)=="-3/99","Retail unsigned denominator clamp differs from reconstructed signed Min");
    bridge.clear();
    std::cout<<"PASS: shared campaign HUD globals, elapsed clock, five expanded source operations, actual presentation values, consumed colours, ownership and cleanup; no game executed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
