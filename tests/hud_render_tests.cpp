#include <windows.h>
#include "sa_hud.hpp"
#include "lcs/native_memory.hpp"
#include "ScriptCommands.h"
#include <map>
#include <vector>
#include <cstring>
#include <iostream>
#include <stdexcept>
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static std::map<uintptr_t,uintptr_t> pages;
struct Text {float x,y;std::string text;};
static std::vector<Text> texts;
static std::vector<uint32_t> rectangles,colours;
static size_t original_calls=0;static bool check_hidden=false,replace_owner=false;
static bool text_fault=false;
namespace lcs {
uintptr_t native_entity_address(uintptr_t address){
    const auto page=address&~uintptr_t(0xFFF);auto& memory=pages[page];
    if(!memory){memory=uintptr_t(VirtualAlloc(nullptr,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));if(!memory)throw std::runtime_error("Cannot allocate data-only fixture");}
    return memory+(address&0xFFF);
}
bool sa_executor_profile_matches(){return true;}
void native_hud_add_clock(uint32_t offset,const char*,uint32_t type){
    auto& id=*reinterpret_cast<uint32_t*>(native_entity_address(0xBA1788));
    if(!id){id=offset;*reinterpret_cast<bool*>(native_entity_address(0xBA1788+0x38))=true;
        *reinterpret_cast<uint8_t*>(native_entity_address(0xBA1788+0x39))=uint8_t(type);}
}
void native_hud_add_counter(uint32_t offset,uint16_t type,const char*,uint16_t slot){
    check(slot<3,"Only source-owned counter lines enter native creator");
    const auto address=0xBA17C8+slot*0x44;auto& id=*reinterpret_cast<uint32_t*>(native_entity_address(address));
    if(!id){id=offset;*reinterpret_cast<bool*>(native_entity_address(address+0x3E))=true;
        *reinterpret_cast<uint16_t*>(native_entity_address(address+0x12))=type;}
}
void native_hud_clear(uint32_t offset,bool clock){
    for(size_t i=clock?0:1;i<(clock?1:4);++i){const auto at=i?0xBA17C8+(i-1)*0x44:0xBA1788;
        auto& id=*reinterpret_cast<uint32_t*>(native_entity_address(at));
        if(id==offset){id=0;*reinterpret_cast<bool*>(native_entity_address(at+(i?0x3E:0x38)))=false;}}
}
void native_hud_font(float,float,uint32_t colour,bool){colours.push_back(colour);*reinterpret_cast<uint8_t*>(native_entity_address(0xC71A60))=17;}
void native_hud_text(float x,float y,const char* value){if(text_fault)RaiseException(0xE0485544,0,0,nullptr);texts.push_back({x,y,value});}
float native_hud_width(const char* value){return float(std::strlen(value))*8.f;}
const char* native_hud_lookup(const char* key){return key;}
void native_hud_rectangle(float,float,float,float,uint32_t colour){rectangles.push_back(colour);}
}
template<class T>static T& cell(uintptr_t address){return *reinterpret_cast<T*>(lcs::native_entity_address(address));}
static void __cdecl original(){
    ++original_calls;
    if(check_hidden)check(!cell<bool>(0xBA17C8+0x3E) && cell<bool>(0xBA180C+0x3E),"Only owned entry is hidden; foreign counter remains visible");
    cell<uint8_t>(0xC71A60)=0xA5; // A chained mod's font setting must survive.
    if(replace_owner)cell<uint32_t>(0xBA17C8)=900;
}
int main(){try{
    const uint8_t mission[]={0xA0,0xC0,0xAE,0xBA,0,0x81,0xEC,0xF0,0,0,0};
    const uint8_t colour[]={0xD9,0x05,0x80,0x1A,0xC7,0,0x8B,0x44,0x24,0x04};
    std::memcpy(reinterpret_cast<void*>(lcs::native_entity_address(0x58B180)),mission,sizeof(mission));
    std::memcpy(reinterpret_cast<void*>(lcs::native_entity_address(0x719430)),colour,sizeof(colour));
    check(lcs::sa_hud_render_profile() && !lcs::sa_hud_render_ready(),"Checked renderer requires an installed draw binding");lcs::sa_hud_render_bound(true);
    cell<int32_t>(0xC17044)=640;cell<int32_t>(0xC17048)=448;
    cell<uint32_t>(0xB7CB4C)=4;
    cell<uint32_t>(0xBA17C8)=0;
    cell<uint32_t>(0xBA180C)=800;cell<bool>(0xBA180C+0x3E)=true;
    check(lcs::sa_hud_write(36,-3),"Reserved native shadow cell can be initialized before accepting entry");
    lcs::HudRequest request{lcs::HudRequest::Action::add_counter,36,0,0,"LABEL"};
    request.total=120;request.heading="HEADING";request.custom=true;request.foreground={5,6,7,8};
    check(lcs::sa_hud_update(request) && lcs::sa_hud_read(36)==-3,"Actual accepted native entry and shadow value reach presentation storage");
    check(lcs::sa_hud_update({lcs::HudRequest::Action::counter_prefix,36,1}),"Prefix mutation reaches the same owned renderer");
    check_hidden=true;lcs::sa_hud_draw_owned(original);check_hidden=false;
    check(original_calls==1 && texts.size()==3 && texts[0].text=="HEADING" && texts[1].text=="$-3/99" && texts[2].text=="LABEL",
          "Real draw path renders both labels, signed prefix and original clamped total");
    check(texts[0].y==168.f && texts[1].y==199.f,"Owned panel reserves visible foreign rows and second heading line");
    check(cell<bool>(0xBA17C8+0x3E) && cell<uint32_t>(0xBA180C)==800 && cell<bool>(0xBA180C+0x3E) && cell<uint8_t>(0xC71A60)==0xA5,
          "Owned display flag restored; foreign native entry and chained font settings preserved");
    check(colours.back()==0xFF070605u,"Authored RGB reaches native font; source renderer uses HUD alpha rather than script alpha");
    check(cell<uint16_t>(0xBAA38C)==2 && cell<bool>(0xBAA394),"Owned draw advances original native flash state once despite hiding its built-in output");
    texts.clear();rectangles.clear();request.type=1;request.background={10,11,12,13};request.heading.clear();
    check(lcs::sa_hud_update({lcs::HudRequest::Action::clear_counter,36}) && lcs::sa_hud_update(request),"Bar presentation recreates only its owned native descriptor");
    lcs::sa_hud_draw_owned(original);check(rectangles.size()==3 && rectangles[1]==0xFF0C0B0Au && rectangles[2]==0xFF070605u,"Actual native bar path consumes foreground and background colours");
    texts.clear();rectangles.clear();cell<uint8_t>(0x96C014)=1;lcs::sa_hud_draw_owned(original);
    check(texts.empty() && rectangles.empty() && cell<bool>(0xBA17C8+0x3E),"Native HUD visibility gate suppresses owned output without losing entry");
    cell<uint8_t>(0x96C014)=0;replace_owner=true;lcs::sa_hud_draw_owned(original);replace_owner=false;
    check(texts.empty() && rectangles.empty() && cell<uint32_t>(0xBA17C8)==900,"Ownership changed by chained draw is not overwritten or rendered as ours");
    // Full source VM -> real HudBridge -> native creator/owned storage -> draw.
    cell<uint32_t>(0xBA17C8)=0;
    lcs::sa_hud_update({lcs::HudRequest::Action::clear_counter,36});
    std::vector<uint8_t> code(128);const auto append=[&](int32_t n){code.push_back(6);for(int j=0;j<4;++j)code.push_back(uint8_t(uint32_t(n)>>(j*8)));};
    code.push_back(uint8_t(COMMAND_DISPLAY_ONSCREEN_TIMER_WITH_NUMBER_AND_STRING));code.push_back(uint8_t(COMMAND_DISPLAY_ONSCREEN_TIMER_WITH_NUMBER_AND_STRING>>8));
    code.push_back(204);code.push_back(16);append(9);append(0);code.insert(code.end(),{'N','U','M',0,0,0,0,0});
    code.push_back(uint8_t(COMMAND_CHANGE_ONSCREEN_COUNTER_PREFIX));code.push_back(uint8_t(COMMAND_CHANGE_ONSCREEN_COUNTER_PREFIX>>8));code.push_back(204);code.push_back(16);append(1);
    lcs::Vm vm(code,128,uint32_t(code.size()));const int32_t four=4;std::memcpy(vm.memory.data()+64,&four,4);
    lcs::HudBridge bridge;bridge.update=lcs::sa_hud_update;bridge.read_cell=lcs::sa_hud_read;bridge.write_cell=lcs::sa_hud_write;
    vm.backend=[&](lcs::Vm& v,lcs::Thread& t,uint16_t op){return bridge.dispatch(v,t,op);};lcs::Thread thread;thread.ip=128;
    check(vm.step(thread,0)==lcs::StepResult::continued && vm.step(thread,0)==lcs::StepResult::continued,"Both source instructions accepted by actual native HUD bridge");
    texts.clear();rectangles.clear();lcs::sa_hud_draw_owned(original);
    check(texts.size()==2 && texts[0].text=="$4/9" && texts[1].text=="NUM","VM source operands reach actual renderer, not a second test-only formatter");
    check(bridge.clear(),"VM HUD cleanup retires actual owned native entry");
    request.type=0;check(lcs::sa_hud_update(request),"Create owned entry for entered render fault");
    text_fault=true;lcs::sa_hud_draw_owned(original);text_fault=false;
    check(!lcs::sa_hud_presentation_healthy() && cell<bool>(0xBA17C8+0x3E) && cell<uint8_t>(0xC71A60)==0xA5,
          "Entered renderer fault retains descriptor, restores flags/font and blocks campaign dependency without replay");
    check(lcs::sa_hud_update({lcs::HudRequest::Action::clear_counter,36}) && lcs::sa_hud_presentation_healthy(),"Explicit release retires quarantined presentation");
    const auto calls=original_calls;cell<uint8_t>(0x719430)=0;
    lcs::sa_hud_draw_owned(original);check(original_calls==calls+1 && !lcs::sa_hud_render_ready(),"Changed renderer ABI delegates foreign draw and refuses readiness");
    for(auto page:pages)VirtualFree(reinterpret_cast<void*>(page.second),0,MEM_RELEASE);
    std::cout<<"PASS: native owned HUD draw, labels/total/prefix/colours, bar consumer, visibility, binding, ownership and font preservation; no game code/device executed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
