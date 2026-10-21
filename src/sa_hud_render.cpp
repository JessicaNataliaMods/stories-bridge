#include <windows.h>
#include "sa_hud.hpp"
#include "lcs/hud_presentation.hpp"
#include "lcs/native_memory.hpp"
#include "lcs/sa_executor.hpp"
#include <cstring>
#include <cmath>
#include <cstdlib>
namespace lcs {
#ifdef LCS_ISOLATED_HUD_RENDER_TEST
void native_hud_font(float,float,uint32_t,bool);
void native_hud_text(float,float,const char*);
float native_hud_width(const char*);
const char* native_hud_lookup(const char*);
void native_hud_rectangle(float,float,float,float,uint32_t);
#else
// Font ABI from Plugin-SDK CFont.{h,cpp}, independently checked against the
// user's PE below. No game binary is executed by the offline tests.
static void native_hud_font(float x,float y,uint32_t colour,bool proportional){
    reinterpret_cast<void(__cdecl*)(float,float)>(0x719380)(x,y);
    reinterpret_cast<void(__cdecl*)(uint32_t)>(0x719430)(colour);
    reinterpret_cast<void(__cdecl*)(int16_t)>(0x719490)(2);
    reinterpret_cast<void(__cdecl*)(bool)>(0x7195B0)(proportional);
    reinterpret_cast<void(__cdecl*)(bool,bool)>(0x7195C0)(false,false);
    reinterpret_cast<void(__cdecl*)(uint8_t)>(0x719610)(2);
    reinterpret_cast<void(__cdecl*)(float)>(0x7194F0)(0.f);
    reinterpret_cast<void(__cdecl*)(float)>(0x7194D0)(*reinterpret_cast<const int32_t*>(0xC17044)*1.f);
    reinterpret_cast<void(__cdecl*)(int16_t)>(0x719590)(2);
    reinterpret_cast<void(__cdecl*)(uint32_t)>(0x719510)(0xFF000000u);
}
static void native_hud_text(float x,float y,const char* text){reinterpret_cast<void(__cdecl*)(float,float,const char*)>(0x71A700)(x,y,text);}
static float native_hud_width(const char* text){return reinterpret_cast<float(__cdecl*)(const char*,bool,bool)>(0x71A0E0)(text,true,false);}
static const char* native_hud_lookup(const char* key){return reinterpret_cast<const char*(__thiscall*)(uintptr_t,const char*)>(0x6A0050)(0xC1B340,key);}
static void native_hud_rectangle(float left,float top,float right,float bottom,uint32_t colour){
    // CRect native storage is left,bottom,right,top; both arguments by reference.
    const std::array<float,4> rectangle{{left,bottom,right,top}};
    reinterpret_cast<void(__cdecl*)(const std::array<float,4>&,const uint32_t&)>(0x728640)(rectangle,colour);
}
#endif
namespace {
HudPresentations presentations;
bool bound=false,faulted=false;
template<class Call> bool guarded(Call const& call){
    __try{call();return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
template<class Call> void checked(Call const& call){if(!guarded(call))throw std::runtime_error("Owned native HUD draw fault");}
void font(float x,float y,uint32_t c,bool p){checked([&]{native_hud_font(x,y,c,p);});}
void print(float x,float y,const char* s){checked([&]{native_hud_text(x,y,s);});}
const char* lookup(const char* s){const char* result=nullptr;checked([&]{result=native_hud_lookup(s);});return result;}
float width(const char* s){float result=0;checked([&]{result=native_hud_width(s);});if(!std::isfinite(result) || result<0)throw std::runtime_error("Invalid native HUD text width");return result;}
void rectangle(float l,float t,float r,float b,uint32_t c){checked([&]{native_hud_rectangle(l,t,r,b,c);});}
constexpr uintptr_t hud=0xBA1788;
uintptr_t entry(size_t slot){return native_entity_address(slot?hud+0x40+(slot-1)*0x44:hud);}
uintptr_t enabled(size_t slot){return entry(slot)+(slot?0x3E:0x38);}
struct Frame {
    bool owned[4]{},draw[4]{},visible=false,font_saved=false;
    int32_t values[4]{},width=0,height=0;size_t foreign_rows=0;
    uint16_t flash[4]{};bool last[4]{};uint32_t frame_number=0;
    uint8_t font[0x40]{};
};
bool capture(Frame* f,const bool* desired){
    __try {
        f->width=*reinterpret_cast<const int32_t*>(native_entity_address(0xC17044));
        f->height=*reinterpret_cast<const int32_t*>(native_entity_address(0xC17048));
        if(f->width<=0 || f->height<=0 || f->width>32768 || f->height>32768)return false;
        f->visible=!(*reinterpret_cast<const uint8_t*>(native_entity_address(0xBAAEC0)) &&
                     !*reinterpret_cast<const uint8_t*>(native_entity_address(0xBAA3FA))) &&
                    !*reinterpret_cast<const uint8_t*>(native_entity_address(0x96C014));
        f->frame_number=*reinterpret_cast<const uint32_t*>(native_entity_address(0xB7CB4C));
        for(size_t i=0;i<4;++i){
            f->owned[i]=desired[i] && *reinterpret_cast<const uint32_t*>(entry(i))==HudBridge::native_cell(i);
            f->draw[i]=*reinterpret_cast<const bool*>(enabled(i));
            f->flash[i]=*reinterpret_cast<const uint16_t*>(native_entity_address(i?0xBAA38C+(i-1)*2:0xBAA388));
            f->last[i]=*reinterpret_cast<const bool*>(native_entity_address(i?0xBAA394+(i-1):0xBAA38A));
            if(f->owned[i])f->values[i]=*reinterpret_cast<const int32_t*>(native_entity_address(0xA49960+HudBridge::native_cell(i)));
            else if(f->draw[i])++f->foreign_rows;
        }
        // SA has a fourth counter beyond LCS's three. Reserve its row too.
        if(*reinterpret_cast<const bool*>(native_entity_address(hud+0x40+3*0x44+0x3E)))++f->foreign_rows;
        for(size_t i=0;i<4;++i)if(f->owned[i])*reinterpret_cast<bool*>(enabled(i))=false;
        return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool save_font(Frame* f){
    __try{std::memcpy(f->font,reinterpret_cast<const void*>(native_entity_address(0xC71A60)),sizeof(f->font));f->font_saved=true;return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void restore(const Frame* f,bool font){
    __try {
        for(size_t i=0;i<4;++i)if(f->owned[i] && *reinterpret_cast<const uint32_t*>(entry(i))==HudBridge::native_cell(i))
            *reinterpret_cast<bool*>(enabled(i))=f->draw[i];
        if(font && f->font_saved)std::memcpy(reinterpret_cast<void*>(native_entity_address(0xC71A60)),f->font,sizeof(f->font));
        // Do not restore CFont::RenderState: it describes the last queued text
        // run and must differ when the following draw changes font settings.
    }__except(EXCEPTION_EXECUTE_HANDLER){}
}
// Retail Hud.cpp uses the HUD fade for alpha, not the script RGBA alpha.
// Native CFont::SetColor applies the observed SA font alpha fade itself.
uint32_t rgba(std::array<uint8_t,4> c){return uint32_t(c[0])|uint32_t(c[1])<<8|uint32_t(c[2])<<16|0xFF000000u;}
bool live(size_t slot){
    __try{return *reinterpret_cast<const uint32_t*>(entry(slot))==HudBridge::native_cell(slot);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool advance_flash(Frame* f,size_t slot){
    if(!f->owned[slot] || !live(slot))return false;
    __try {
        auto age=f->last[slot]?f->flash[slot]:uint16_t(1);
        if(age && ++age>50)age=0;
        *reinterpret_cast<uint16_t*>(native_entity_address(slot?0xBAA38C+(slot-1)*2:0xBAA388))=age;
        *reinterpret_cast<bool*>(native_entity_address(slot?0xBAA394+(slot-1):0xBAA38A))=f->draw[slot];
        f->flash[slot]=age;return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
bool sa_hud_present(const HudRequest& r){const bool accepted=presentations.apply(r);if(!presentations.custom())faulted=false;return accepted;}
void sa_hud_render_bound(bool value){bound=value;}
bool sa_hud_render_ready(){return bound && !faulted && sa_hud_render_profile();}
bool sa_hud_presentation_healthy(){return !presentations.custom() || sa_hud_render_ready();}
bool sa_hud_render_profile(){
    if(!sa_executor_profile_matches())return false;
    __try {
        const uint8_t mission[]={0xA0,0xC0,0xAE,0xBA,0,0x81,0xEC,0xF0,0,0,0};
        const uint8_t colour[]={0xD9,0x05,0x80,0x1A,0xC7,0,0x8B,0x44,0x24,0x04};
        return !std::memcmp(reinterpret_cast<const void*>(native_entity_address(0x58B180)),mission,sizeof(mission)) &&
               !std::memcmp(reinterpret_cast<const void*>(native_entity_address(0x719430)),colour,sizeof(colour));
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void sa_hud_draw_owned(void(__cdecl* original)()){
    if(!original)return;
    if(!presentations.custom() || !sa_hud_render_ready()){original();return;}
    const auto items=presentations.entries();bool desired[4]{};for(size_t i=0;i<4;++i)desired[i]=bool(items[i]);
    Frame frame;if(!capture(&frame,desired)){restore(&frame,false);original();return;}
    struct Restore {Frame& frame;~Restore(){restore(&frame,true);}} cleanup{frame};
    original();restore(&frame,false);
    if(!frame.visible || !save_font(&frame))return;
    try {
    const float sx=float(frame.width)/640.f,sy=float(frame.height)/448.f;
    const float right=float(frame.width)-32.f*sx;
    float y=(148.f+20.f*float(frame.foreign_rows))*sy;
    bool show[4]{};
    for(size_t i=0;i<4;++i)if(frame.owned[i] && frame.draw[i] && advance_flash(&frame,i))
        show[i]=bool(frame.frame_number&4u) || frame.flash[i]==0;
    // Original two-string headers reserve their own lines above both clock
    // and numeric/bar counters. Their body remains visible during the flash.
    for(size_t i=1;i<4;++i)if(frame.owned[i] && frame.draw[i] && items[i] && live(i) && !items[i]->request.heading.empty()){
        if(show[i]){
            const auto title=lookup(items[i]->request.heading.c_str());
            if(title){font(.5f*sx,sy,0xFFFFFFFFu,true);print(right,y,title);}
        }
        y+=31.f*sy;
    }
    for(size_t i=0;i<4;++i){
        if(!frame.owned[i] || !frame.draw[i] || !items[i] || !live(i))continue;
        const auto& item=*items[i];const auto& r=item.request;
        if(!show[i] && r.heading.empty())continue;
        const auto text=[&](const std::string& key,float x,float at,uint32_t colour){
            if(key.empty())return;
            const auto value=lookup(key.c_str());if(!value)return;
            font(.5f*sx,sy,colour,true);print(x,at,value);
        };
        font(.5f*sx,sy,rgba(r.foreground),false);
        float content_width=90.f*sx;
        if(i && r.type==1){
            const float left=right-46.f*sx,top=y+3.f*sy,bottom=top+11.f*sy;
            rectangle(left-sx,top-sy,right+sx,bottom+sy,0xFF000000u);
            rectangle(left,top,right,bottom,rgba(r.background));
            // Source bar renderer uses atoi(formatted buffer), including the
            // monetary-prefix behavior; do not reinterpret it as a percentage
            // of the displayed total or clamp its authored value to100.
            const auto number=item.number(frame.values[i]);const float amount=float(std::atoi(number.c_str()))/100.f;
            rectangle(left,top,left+amount*46.f*sx,bottom,rgba(r.foreground));content_width=50.f*sx;
        }else{
            auto number=i?item.number(frame.values[i]):
                std::to_string(uint32_t(frame.values[i])/60000u%100u)+":"+
                (uint32_t(frame.values[i])/1000u%60u<10?"0":"")+std::to_string(uint32_t(frame.values[i])/1000u%60u);
            content_width=width(number.c_str())+4.f*sx;print(right,y,number.c_str());
        }
        text(r.label,right-content_width,y,rgba(r.foreground));y+=20.f*sy;
    }
    }catch(...){faulted=true;OutputDebugStringA("LcsSaBridge: owned HUD renderer quarantined; campaign dependency will hold\n");}
}
}
