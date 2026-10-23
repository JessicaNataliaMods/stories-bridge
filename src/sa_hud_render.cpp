#include <windows.h>
#include "sa_hud.hpp"
#include "lcs/hud_presentation.hpp"
#include "lcs/ui_font.hpp"
#include "lcs/ui_hud.hpp"
#include "lcs/ui_text.hpp"
#include "sa_ui_draw.hpp"
#include "sa_ui_radar.hpp"
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
// Read by the naked call-site stubs in asi.cpp, which skip SA's own health, armour,
// money and clock drawing while the LCS versions are on screen.
extern "C" volatile uint8_t lcs_player_info_skip=0;
extern "C" volatile uint8_t lcs_weapon_skip=0;   // weapon icon + ammunition
extern "C" volatile uint8_t lcs_wanted_skip=0;   // wanted stars
namespace {
bool lcs_look=false,lcs_resources=false,lcs_loaded=false;
std::filesystem::path lcs_directory;
void(*lcs_log)(const std::string&)=nullptr;
// RenderWare is not running while the ASI installs its hooks, so the UI layer is
// loaded on the first frame that needs it. A failure is reported once and the SA
// HUD keeps drawing.
bool lcs_resources_active(){
    if(!lcs_resources)return false;
    if(!lcs_loaded){
        lcs_loaded=true;
        std::string diagnostic;
        const bool ok=sa_ui_load(lcs_directory,diagnostic);
        if(lcs_log)lcs_log(std::string(ok?"LCS UI: ":"LCS UI unavailable, SA HUD kept: ")+diagnostic);
    }
    return sa_ui_ready();
}
bool lcs_active(){return lcs_look && lcs_resources_active();}
ui::Rgba opaque(std::array<uint8_t,4> c){return ui::Rgba{c[0],c[1],c[2],255};}
// UseTimerCounterFontSettings: standard font, right justified, PSP layout scale.
ui::TextStyle counter_style(const ui::Screen& screen,ui::Rgba colour,bool proportional){
    ui::TextStyle style;
    style.style=ui::FontStyle::standard;
    style.scale_x=screen.psp_x(0.4048f);style.scale_y=screen.psp_y(0.88f);
    style.colour=colour;style.proportional=proportional;style.right_justify=true;
    return style;
}
void lcs_print(const ui::TextStyle& style,float x,float y,const char* text){
    std::vector<ui::GlyphQuad> quads;
    ui::print_line(style,x,y,text,quads);
    if(!sa_ui_draw_glyphs(quads))throw std::runtime_error("LCS UI glyph draw fault");
}
void lcs_rect(float l,float t,float r,float b,ui::Rgba colour){
    if(!sa_ui_draw_rect(l,t,r,b,colour))throw std::runtime_error("LCS UI rectangle draw fault");
}
// SA draws its player information only when these two flags are set; keep the same gate.
bool player_info_wanted(){
    __try {
        return *reinterpret_cast<const uint8_t*>(native_entity_address(0xBA6769)) && *reinterpret_cast<const uint8_t*>(native_entity_address(0xA444A0));
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool read_player_info(ui::PlayerHudInputs* in){
    __try {
        const auto ped=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB7CD98));
        if(!ped)return false;
        in->health=*reinterpret_cast<const float*>(ped+0x540);
        in->max_health=*reinterpret_cast<const float*>(ped+0x544);
        in->armour=*reinterpret_cast<const float*>(ped+0x548);
        in->money=*reinterpret_cast<const int32_t*>(native_entity_address(0xB7CE54));
        in->hour=*reinterpret_cast<const uint8_t*>(native_entity_address(0xB70153));
        in->minute=*reinterpret_cast<const uint8_t*>(native_entity_address(0xB70152));
        return in->max_health>0.f && in->max_health<=10000.f && std::isfinite(in->health) && std::isfinite(in->armour);
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// Weapon in hand and wanted level, read the way SA's own DrawAmmo/DrawWanted do:
// weapon table at ped+0x5A0 (0x1C each, active slot at +0x718), clip size from
// CWeaponInfo (+0x20) through CPed::GetWeaponSkill (0x5E6580) and
// CWeaponInfo::GetWeaponInfo (0x743C60), CWanted through FindPlayerWanted (0x56E230).
bool read_combat_info(ui::PlayerHudInputs* in){
    __try {
        const auto ped=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB7CD98));
        if(!ped)return false;
        const int slot=*reinterpret_cast<const int8_t*>(ped+0x718);
        if(slot<0 || slot>=13)return false;
        const auto entry=ped+0x5A0+uintptr_t(slot)*0x1C;
        in->weapon_type=*reinterpret_cast<const int32_t*>(entry);
        in->ammo_clip=*reinterpret_cast<const int32_t*>(entry+8);
        in->ammo_total=*reinterpret_cast<const int32_t*>(entry+0xC);
        if(in->weapon_type<0 || in->weapon_type>46)return false;
        const int skill=int8_t(reinterpret_cast<int(__thiscall*)(void*)>(native_entity_address(0x5E6580))(reinterpret_cast<void*>(ped))&0xFF);
        const auto info=reinterpret_cast<uintptr_t(__cdecl*)(int,int)>(native_entity_address(0x743C60))(in->weapon_type,skill);
        in->clip_size=info?int(*reinterpret_cast<const int16_t*>(info+0x20)):0;
        const auto wanted=reinterpret_cast<uintptr_t(__cdecl*)(int)>(native_entity_address(0x56E230))(-1);
        in->wanted=0;in->parole=0;
        if(wanted){
            const auto level=*reinterpret_cast<const uint32_t*>(wanted+0x2C),parole=*reinterpret_cast<const uint32_t*>(wanted+0x30);
            if(level>6 || parole>6)return false;
            in->wanted=int(level);in->parole=int(parole);
        }
        in->now_ms=*reinterpret_cast<const uint32_t*>(native_entity_address(0xB7CB84));
        return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// DrawWeaponIcon/DrawAmmo/DrawWanted. Each SA call site is only skipped for what was
// really drawn: a weapon without a readable icon keeps SA's icon and ammunition.
void draw_lcs_combat_info(const Frame& frame,ui::PlayerHudInputs& in){
    lcs_weapon_skip=0;lcs_wanted_skip=0;
    if(!read_combat_info(&in))return;
    static int last_wanted=-1;static uint32_t changed=0;
    if(in.wanted!=last_wanted){last_wanted=in.wanted;changed=in.now_ms;}
    in.wanted_changed_ms=changed;
    const auto layout=ui::layout_weapon_and_wanted(in);
    bool icon=false;
    if(!layout.sprites.empty()){
        const auto& s=layout.sprites.front();
        icon=s.weapon>=0?sa_ui_draw_weapon_icon(s.weapon,s.left,s.top,s.right,s.bottom,s.colour):
                         sa_ui_draw_sprite(s.texture.c_str(),s.left,s.top,s.right,s.bottom,s.colour,s.u0,s.v0,s.u1,s.v1);
    }
    if(icon){
        for(const auto& t:layout.texts)lcs_print(t.style,t.x,t.y,t.text.c_str());
        lcs_weapon_skip=1;
    }
    for(size_t i=1;i<layout.sprites.size();++i){
        const auto& s=layout.sprites[i];
        if(!sa_ui_draw_sprite(s.texture.c_str(),s.left,s.top,s.right,s.bottom,s.colour,s.u0,s.v0,s.u1,s.v1))
            throw std::runtime_error("LCS UI star draw fault");
    }
    lcs_wanted_skip=1;
}
// Hud.cpp DrawHealth/DrawArmour/DrawCash/DrawTime over the PS2 textures.
void draw_lcs_player_info(const Frame& frame){
    lcs_player_info_skip=0;lcs_weapon_skip=0;lcs_wanted_skip=0;
    if(!player_info_wanted())return;
    ui::PlayerHudInputs in;
    if(!read_player_info(&in))return;
    in.frame=frame.frame_number;in.screen.width=float(frame.width);in.screen.height=float(frame.height);
    for(const auto& s:ui::layout_player_info(in))
        if(!sa_ui_draw_sprite(s.texture.c_str(),s.left,s.top,s.right,s.bottom,s.colour,s.u0,s.v0,s.u1,s.v1))
            throw std::runtime_error("LCS UI sprite draw fault");
    lcs_player_info_skip=1;
    draw_lcs_combat_info(frame,in);
}
// Hud.cpp DrawOnScreenTimer, PSP layout: heading lines are 31 units tall, number
// and bar lines 20, all anchored 12 units from the right edge at y=100.
void draw_lcs_counters(const Frame& frame,const std::array<std::optional<HudPresentation>,4>& items,const bool* show){
    const ui::Screen screen{float(frame.width),float(frame.height)};
    const float right=screen.from_right(12.f);
    const ui::Rgba white{255,255,255,255};
    int big_lines=0;
    for(size_t i=1;i<4;++i){
        if(!frame.owned[i] || !frame.draw[i] || !items[i] || !live(i) || items[i]->request.heading.empty())continue;
        if(show[i])if(const char* title=lookup(items[i]->request.heading.c_str())){
            auto style=counter_style(screen,white,true);
            style.scale_x=screen.psp_x(0.7f);style.scale_y=screen.psp_y(1.5217391f);
            lcs_print(style,right,screen.psp_y(31.f*float(big_lines))+screen.psp_y(100.f),title);
        }
        ++big_lines;
    }
    int lines=0;
    const float base=screen.psp_y(100.f)+screen.psp_y(31.f*float(big_lines));
    if(frame.owned[0] && frame.draw[0] && items[0] && live(0)){
        if(show[0]){
            const auto& r=items[0]->request;
            const auto seconds=uint32_t(frame.values[0])/1000u%60u;
            const auto number=std::to_string(uint32_t(frame.values[0])/60000u%100u)+":"+(seconds<10?"0":"")+std::to_string(seconds);
            const auto style=counter_style(screen,opaque(r.foreground),false);
            const float y=screen.psp_y(31.f*float(big_lines))+screen.psp_y(100.f);
            lcs_print(style,right,y,number.c_str());
            if(!r.label.empty())if(const char* label=lookup(r.label.c_str()))
                lcs_print(counter_style(screen,opaque(r.foreground),true),right-(ui::string_width(style,number)+screen.psp_x(4.f)),y,label);
        }
        lines=1;
    }
    for(size_t i=1;i<4;++i){
        if(!frame.owned[i] || !frame.draw[i] || !items[i] || !live(i))continue;
        const auto& item=*items[i];const auto& r=item.request;
        if(!show[i] && r.heading.empty())continue;
        const float y=base+screen.psp_y(20.f*float(lines));
        float size=0.f;
        if(r.type==1){
            const float width=screen.psp_x(46.f),left=right-width;
            const float top=y+screen.psp_y(3.f),bottom=top+screen.psp_y(11.f);
            const float dx=screen.psp_x(1.f),dy=screen.psp_y(1.f);
            lcs_rect(left-dx,top-dy,right+dx,bottom+dy,ui::Rgba{0,0,0,255});
            lcs_rect(left,top,right,bottom,opaque(r.background));
            // The source bar renderer uses atoi(formatted buffer); keep that.
            const float amount=float(std::atoi(item.number(frame.values[i]).c_str()))/100.f;
            lcs_rect(left,top,left+amount*width,bottom,opaque(r.foreground));
            size=width+screen.psp_x(4.f);
        }else{
            const auto number=item.number(frame.values[i]);
            const auto style=counter_style(screen,opaque(r.foreground),false);
            lcs_print(style,right,y,number.c_str());
            size=ui::string_width(style,number)+screen.psp_x(4.f);
        }
        if(!r.label.empty())if(const char* label=lookup(r.label.c_str()))
            lcs_print(counter_style(screen,opaque(r.foreground),true),right-size,y,label);
        ++lines;
    }
}
}
namespace {
// Effective alpha of the colour SA just set for its print (CFont details at 0xC71A60, alpha byte +3,
// already scaled by the font fade) and the camera's widescreen flag.
bool read_text_state(uint8_t* alpha,bool* widescreen){
    __try {
        *alpha=*reinterpret_cast<const uint8_t*>(native_entity_address(0xC71A63));
        *widescreen=*reinterpret_cast<const uint8_t*>(native_entity_address(0xB6F065))!=0;
        return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
// Called by the naked stubs on SA's text print sites (CFont::PrintString/FromBottom
// inside the HUD functions). SA keeps deciding what is shown and how it fades; when
// the LCS UI is ready the text is drawn here with the original layout instead and
// SA's own print is skipped. Returning 0 lets SA print as usual.
extern "C" int __cdecl lcs_text_hook(int element,float,float,const char* text){
    if(!lcs_look || !lcs_loaded || !sa_ui_ready() || !sa_hud_render_ready() || element<0 || element>9 || !text || !*text)return 0;
    try {
        ui::TextRequest request;
        request.element=ui::TextElement(element);
        size_t length=0;while(length<512 && text[length])++length;
        request.text.assign(text,length);
        bool wide_camera=false;
        if(!read_text_state(&request.alpha,&wide_camera))return 0;
        request.widescreen_cutscene=wide_camera;
        float width=0,height=0;
        if(!sa_ui_screen(width,height))return 0;
        request.screen.width=width;request.screen.height=height;
        if(request.alpha==0)return 1;
        const auto layout=ui::layout_text(request);
        if(layout.has_box){
            const auto& b=layout.box;
            if(!sa_ui_draw_rect(b.left,b.top,b.right,b.bottom,layout.box_colour))return 0;
        }
        if(!sa_ui_draw_glyphs(layout.quads))return 0;
        return 1;
    }catch(...){return 0;}
}
void sa_hud_use_lcs_ui(const std::filesystem::path& directory,void(*log)(const std::string&),bool hud_enabled){
    lcs_resources=true;lcs_look=hud_enabled;lcs_loaded=false;lcs_directory=directory;lcs_log=log;
}
bool sa_hud_lcs_ui_active(){return lcs_resources_active();}
namespace {
bool read_breath(int player,float* fraction,bool* flash){
    __try {
        if(player!=0)return false;
        const auto ped=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB7CD98));
        if(!ped)return false;
        const auto data=*reinterpret_cast<const uintptr_t*>(ped+0x480);
        if(!data)return false;
        const float value=*reinterpret_cast<const float*>(data+0x44);
        const float maximum=reinterpret_cast<float(__cdecl*)(int)>(native_entity_address(0x559AF0))(8);
        if(!std::isfinite(value)||!std::isfinite(maximum)||maximum<=0.f)return false;
        *fraction=value/maximum;
        *flash=*reinterpret_cast<const int16_t*>(native_entity_address(0xBAB1DC))==10 &&
            (*reinterpret_cast<const uint32_t*>(native_entity_address(0xB7CB4C))&8u)!=0;
        return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
bool sa_hud_draw_lcs_breath(int player){
    // Called only when SA wants its breath bar: keep native visibility and timing.
    if(!lcs_player_info_skip || !sa_ui_ready())return false;
    float fraction=0,width=0,height=0;bool flash=false;
    if(!read_breath(player,&fraction,&flash)||!sa_ui_screen(width,height))return false;
    if(flash)return true;
    try {
        for(const auto& s:ui::layout_breath_bar(ui::Screen{width,height},fraction)){
            const bool ok=s.texture.empty()?sa_ui_draw_rect(s.left,s.top,s.right,s.bottom,s.colour):
                sa_ui_draw_sprite(s.texture.c_str(),s.left,s.top,s.right,s.bottom,s.colour,s.u0,s.v0,s.u1,s.v1);
            if(!ok)return false;
        }
        return true;
    }catch(...){return false;}
}
void sa_hud_draw_owned(void(__cdecl* original)()){
    if(!original)return;
    sa_radar_frame(false,0.f,0.f);   // SA draws its own radar unless the LCS branch below turns it on
    if((!presentations.custom() && !lcs_active()) || !sa_hud_render_ready()){original();return;}
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
    if(lcs_active()){
        sa_radar_frame(true,float(frame.width),float(frame.height));
        sa_ui_apply_radar_sprites();
        draw_lcs_player_info(frame);draw_lcs_counters(frame,items,show);return;
    }
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
    }catch(...){lcs_player_info_skip=0;lcs_weapon_skip=0;lcs_wanted_skip=0;sa_radar_frame(false,0.f,0.f);faulted=true;OutputDebugStringA("LcsSaBridge: owned HUD renderer quarantined; campaign dependency will hold\n");}
}
}
