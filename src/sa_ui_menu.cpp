#include <windows.h>
#include <intrin.h>
#include "sa_ui_menu.hpp"
#include "sa_ui_draw.hpp"
#include "sa_hud.hpp"
#include "lcs/sa_executor.hpp"
#include "lcs/ui_menu.hpp"
#include <array>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>
#pragma intrinsic(_ReturnAddress)
namespace lcs {
namespace {
enum class Kind {print,bottom,width,flush};
struct Site {uintptr_t address;Kind kind;uintptr_t original=0;std::array<uint8_t,5> bytes{};};
// CMenuManager's window, stats, briefs, map, standard menus, helper and controls
// calls, plus the outer Render2D font flushes. Most menu text is queued AFTER
// DrawBackground's flushes and must be submitted at the same frame's outer
// flush, rather than accumulating until the queue's native fallback barrier.
// Each E8 and target is checked offline against the supported SA PE.
Site sites[]={
    {0x574001,Kind::print},{0x574313,Kind::print},{0x574DC1,Kind::print},{0x574E4D,Kind::print},
    {0x5750BA,Kind::print},{0x57511B,Kind::print},{0x575F89,Kind::print},{0x5764E7,Kind::print},
    {0x5791C4,Kind::print},{0x579311,Kind::print},{0x579698,Kind::print},{0x579878,Kind::print},
    {0x57A1FA,Kind::print},{0x57A405,Kind::print},{0x57D9A1,Kind::print},{0x57DA52,Kind::print},
    {0x57DB00,Kind::print},{0x57EBAF,Kind::print},{0x57EC9A,Kind::print},{0x57ED53,Kind::print},
    {0x57F737,Kind::print},{0x57F78C,Kind::print},{0x57F7CB,Kind::print},{0x57FAD1,Kind::print},{0x57FD5F,Kind::print},
    {0x5811EF,Kind::print},{0x58165B,Kind::print},{0x582DEE,Kind::print},
    {0x57E3AE,Kind::bottom},
    {0x57A49B,Kind::width},{0x57FB52,Kind::width},{0x57FE35,Kind::width},
    {0x5814A7,Kind::width},{0x581512,Kind::width},
    {0x57942A,Kind::flush},{0x57BBA5,Kind::flush},{0x57BBAA,Kind::flush},
    {0x57BC91,Kind::flush},{0x57BE33,Kind::flush},{0x57C61C,Kind::flush},
    {0x57BF70,Kind::flush}, // direct RenderFontBuffer inside the frontend
    {0x53E55F,Kind::flush},{0x53E84B,Kind::flush},{0x53E867,Kind::flush},{0x53EBB1,Kind::flush}
};
using Print=void(__cdecl*)(float,float,const char*);
using Width=float(__cdecl*)(const char*,uint8_t,uint8_t);
using Flush=void(__cdecl*)();
struct NativeFont {std::array<uint8_t,0x40> bytes;};
struct Pending {ui::MenuText text;NativeFont font;std::string source;float x,y;uintptr_t original;bool native_only=false;};
std::vector<Pending> pending;
size_t pending_quads=0;
bool flushing=false,installed=false,native_batch=false;
template<class F> bool guarded(const F& f){__try {f();return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
uintptr_t original(uintptr_t return_address,Kind kind){
    for(const auto& site:sites)if(site.address+5==return_address && site.kind==kind)return site.original;
    switch(kind){case Kind::print:return 0x71A700;case Kind::bottom:return 0x71A820;case Kind::width:return 0x71A0E0;default:return 0x71A210;}
}
bool active(){
    bool on=false;
    return !flushing && !native_batch && guarded([&]{on=*reinterpret_cast<const uint8_t*>(0xBA67A4)!=0;}) && on && sa_hud_lcs_ui_active();
}
float field(const NativeFont& font,size_t at){float value;std::memcpy(&value,font.bytes.data()+at,4);return value;}
ui::Rgba colour(const NativeFont& f,size_t at){return {f.bytes[at],f.bytes[at+1],f.bytes[at+2],f.bytes[at+3]};}
bool snapshot(NativeFont& native,ui::MenuFont& font,bool* captured=nullptr){
    if(captured)*captured=false;
    if(!guarded([&]{std::memcpy(native.bytes.data(),reinterpret_cast<const void*>(0xC71A60),native.bytes.size());}))return false;
    if(captured)*captured=true;
    const auto& b=native.bytes;
    if(b[0x34]>1 || b[0x35]>2)return false;
    auto& s=font.text;
    // Native Gothic titles use page 0/variant 0; menu rows use variant 2.
    s.style=b[0x34]==0 && b[0x35]==0?ui::FontStyle::heading:ui::FontStyle::standard;
    s.scale_x=field(native,4);font.line_height=18.f*field(native,8);
    if(!std::isfinite(s.scale_x)||s.scale_x<=0.f||s.scale_x>128.f || !std::isfinite(font.line_height)||font.line_height<=0.f||font.line_height>2048.f)return false;
    s.slant=field(native,0xC);s.slant_ref_x=field(native,0x10);s.slant_ref_y=field(native,0x14);
    s.centre=b[0x19]!=0;s.right_justify=b[0x1A]!=0;s.proportional=b[0x1D]!=0;
    s.colour=colour(native,0);s.drop_shadow=b[0x36];s.drop_colour=colour(native,0x37);
    s.drop_colour.a=std::min(s.drop_colour.a,s.colour.a);
    font.background=b[0x1B]!=0;font.background_colour=colour(native,0x24);
    font.wrap_x=field(native,0x28);s.centre_size=field(native,0x2C);s.right_wrap=field(native,0x30);
    font.outline=std::min(int(b[0x3B]),4);
    return std::isfinite(font.wrap_x)&&std::isfinite(s.centre_size)&&std::isfinite(s.right_wrap)&&std::isfinite(s.slant)&&std::isfinite(s.slant_ref_x)&&std::isfinite(s.slant_ref_y);
}
bool read_text(const char* source,std::string& text,bool* copied=nullptr){
    if(copied)*copied=false;
    if(!source)return false;
    bool complete=false;
    if(!guarded([&]{size_t n=0;while(n<4096 && source[n])++n;if(n<4096){text.assign(source,n);complete=true;}}) || !complete)return false;
    if(copied)*copied=true;
    // Keep native button/key tokens and foreign extensions with their own
    // renderer, rather than dropping a symbol the LCS glyph layer cannot draw.
    for(size_t p=0;(p=text.find('~',p))!=std::string::npos;){
        if(p+2>=text.size() || text[p+2]!='~' || std::strchr("BYbghlopqrtwxyn",text[p+1])==nullptr)return false;
        p+=3;
    }
    return true;
}
bool enqueue(uintptr_t target,float x,float y,const char* source,bool bottom){
    if(!active() || !std::isfinite(x)||!std::isfinite(y))return false;
    NativeFont native;ui::MenuFont font;std::string text;bool captured=false,copied=false;
    const bool known_font=snapshot(native,font,&captured),supported_text=read_text(source,text,&copied);
    if(!captured || !copied)return false;
    if(native.bytes[3]==0 || text.empty())return true;
    const bool native_only=!known_font||!supported_text;
    auto layout=native_only?ui::MenuText{}:ui::layout_menu_text(font,x,y,text,bottom);
    if(pending_quads+layout.quads.size()>65536 || pending.size()>=512)return false;
    const auto quads=layout.quads.size();
    pending.push_back({std::move(layout),native,std::move(text),x,y,target,native_only});pending_quads+=quads;
    return true;
}
bool replay(const Pending& item){
    NativeFont saved;
    if(!guarded([&]{std::memcpy(saved.bytes.data(),reinterpret_cast<const void*>(0xC71A60),saved.bytes.size());}))return false;
    const bool printed=guarded([&]{
        std::memcpy(reinterpret_cast<void*>(0xC71A60),item.font.bytes.data(),item.font.bytes.size());
        reinterpret_cast<Print>(item.original)(item.x,item.y,item.source.c_str());
    });
    const bool restored=guarded([&]{std::memcpy(reinterpret_cast<void*>(0xC71A60),saved.bytes.data(),saved.bytes.size());});
    return printed&&restored;
}
void native_barrier(){
    // An unqueueable call must not overtake earlier text. Put the earlier
    // requests into SA's real font buffer and keep this batch native.
    if(flushing || pending.empty())return;
    flushing=true;native_batch=true;
    for(const auto& item:pending)replay(item);
    pending.clear();pending_quads=0;flushing=false;
}
void __cdecl menu_print(float x,float y,const char* text){
    const auto target=original(reinterpret_cast<uintptr_t>(_ReturnAddress()),Kind::print);
    try {if(enqueue(target,x,y,text,false))return;}catch(...){}
    native_barrier();
    reinterpret_cast<Print>(target)(x,y,text);
}
void __cdecl menu_bottom(float x,float y,const char* text){
    const auto target=original(reinterpret_cast<uintptr_t>(_ReturnAddress()),Kind::bottom);
    try {if(enqueue(target,x,y,text,true))return;}catch(...){}
    native_barrier();
    reinterpret_cast<Print>(target)(x,y,text);
}
float __cdecl menu_width(const char* source,uint8_t full,uint8_t script_text){
    const auto target=original(reinterpret_cast<uintptr_t>(_ReturnAddress()),Kind::width);
    try {
        NativeFont native;ui::MenuFont font;std::string text;
        if(active() && snapshot(native,font) && read_text(source,text))return ui::menu_text_width(font,text,full!=0);
    }catch(...){}
    return reinterpret_cast<Width>(target)(source,full,script_text);
}
void __cdecl menu_flush(){
    const auto target=original(reinterpret_cast<uintptr_t>(_ReturnAddress()),Kind::flush);
    reinterpret_cast<Flush>(target)();
    if(flushing)return;
    native_batch=false;if(pending.empty())return;
    flushing=true;
    for(size_t i=0;i<pending.size();++i){
        const auto& item=pending[i];
        if(item.native_only){replay(item);reinterpret_cast<Flush>(target)();continue;}
        bool ok=false;
        try {
            const auto& t=item.text;
            ok=(!t.has_box || sa_ui_draw_rect(t.box.left,t.box.top,t.box.right,t.box.bottom,t.box_colour)) && sa_ui_draw_glyphs(t.quads);
        }catch(...){ok=false;}
        if(ok)continue;
        // A failed draw may already have submitted part of this item: never
        // replay it or earlier items over the same pixels. Native rendering
        // takes the untouched remainder and subsequent frames (RW is faulted).
        for(size_t j=i+1;j<pending.size();++j)replay(pending[j]);
        reinterpret_cast<Flush>(target)();
        OutputDebugStringA("LcsSaBridge: frontend font draw failed; native remainder retained\n");
        break;
    }
    pending.clear();pending_quads=0;flushing=false;
}
uintptr_t hook(Kind kind){switch(kind){case Kind::print:return reinterpret_cast<uintptr_t>(&menu_print);case Kind::bottom:return reinterpret_cast<uintptr_t>(&menu_bottom);case Kind::width:return reinterpret_cast<uintptr_t>(&menu_width);default:return reinterpret_cast<uintptr_t>(&menu_flush);}}
bool writable_call(Site& site,bool restore){
    bool result=false;
    guarded([&]{
        auto* code=reinterpret_cast<uint8_t*>(site.address);DWORD old=0;
        if(!VirtualProtect(code,5,PAGE_EXECUTE_READWRITE,&old))return;
        if(restore)std::memcpy(code,site.bytes.data(),5);
        else {code[0]=0xE8;const int32_t relative=int32_t(hook(site.kind)-(site.address+5));std::memcpy(code+1,&relative,4);}
        FlushInstructionCache(GetCurrentProcess(),code,5);DWORD ignored=0;result=VirtualProtect(code,5,old,&ignored)!=0;
        if(!result && !restore)std::memcpy(code,site.bytes.data(),5);
    });
    return result;
}
}
bool sa_ui_menu_install(std::string& diagnostic){
    if(installed)return true;
    if(!sa_executor_profile_matches()){diagnostic="Unsupported SA executable for menu fonts";return false;}
    for(auto& site:sites){
        bool valid=false;
        if(!guarded([&]{
            const auto* code=reinterpret_cast<const uint8_t*>(site.address);
            if(code[0]!=0xE8)return;
            std::memcpy(site.bytes.data(),code,5);int32_t relative;std::memcpy(&relative,code+1,4);
            site.original=site.address+5+relative;MEMORY_BASIC_INFORMATION info{};
            valid=VirtualQuery(reinterpret_cast<const void*>(site.original),&info,sizeof(info)) && info.State==MEM_COMMIT &&
                !(info.Protect&(PAGE_GUARD|PAGE_NOACCESS)) && (info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));
        }) || !valid){diagnostic="SA frontend font call site differs";return false;}
    }
    size_t done=0;
    for(auto& site:sites){
        if(!writable_call(site,false)){
            for(size_t i=0;i<done;++i)writable_call(sites[i],true);
            diagnostic="Cannot install frontend font calls";return false;
        }
        ++done;
    }
    installed=true;diagnostic="LCS frontend fonts installed (menu text, metrics and deferred flush)";return true;
}
}
