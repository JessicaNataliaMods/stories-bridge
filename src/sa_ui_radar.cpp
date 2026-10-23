#include <windows.h>
#include "sa_ui_radar.hpp"
#include "sa_ui_draw.hpp"
#include "lcs/native_memory.hpp"
#include "lcs/sa_executor.hpp"
#include "lcs/ui_hud.hpp"
#include <cstring>
#include <cmath>
extern "C" volatile uint8_t lcs_radar_skip=0;
namespace lcs {
namespace {
// CRadar::TransformRadarPointToScreenSpace(CVector2D* out, CVector2D* in), SA 1.0 US, cdecl.
constexpr uintptr_t transform_address=0x583480;
constexpr size_t transform_size=0xA3;            // up to and including its only ret (0x583522)
constexpr uint8_t transform_prefix[]={0xA0,0xA1,0x67,0xBA,0x00,0x8B,0x4C,0x24,0x04,0x84,0xC0,0x8B,0x44,0x24,0x08,0x74};
constexpr uintptr_t menu_map_flag=0xBA67A1;      // set while the pause-menu map uses the same transform
using TransformFn=void(__cdecl*)(float*,const float*);
TransformFn original_transform=nullptr;
ui::RadarLayout layout;
volatile bool active=false;
bool menu_map_active(){
    __try {return *reinterpret_cast<const uint8_t*>(native_entity_address(menu_map_flag))!=0;}
    __except(EXCEPTION_EXECUTE_HANDLER){return true;}
}
bool read_sprite_blip(int32_t handle,RadarSpriteBlip& result){
    // tRadarTrace: 175 records of 0x28 bytes at 0xBA86F0. The generation,
    // tracking and sprite offsets are confirmed by SetBlipSprite 0x583D70.
    const uint32_t slot=uint32_t(handle)&0xFFFFu;
    if(slot>=175 || handle==-1)return false;
    __try {
        const auto* trace=reinterpret_cast<const uint8_t*>(native_entity_address(0xBA86F0)+slot*0x28);
        if(*reinterpret_cast<const uint16_t*>(trace+0x14)!=uint16_t(uint32_t(handle)>>16) || !(trace[0x25]&2))return false;
        const uint8_t type=(trace[0x26]>>2)&15;
        if(type!=4 && type!=5)return false;
        for(size_t i=0;i<3;++i){result.position[i]=*reinterpret_cast<const float*>(trace+8+4*i);if(!std::isfinite(result.position[i]))return false;}
        result.sprite=trace[0x24];result.contact=type==5;result.short_range=(trace[0x25]&4)!=0;
        return result.sprite<64;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void __cdecl lcs_transform(float* out,const float* in){
    if(!active || menu_map_active()){original_transform(out,in);return;}
    layout.to_screen(in[0],in[1],out[0],out[1]);
}
}
bool sa_radar_install(std::string& diagnostic){
    if(original_transform)return true;
    if(!sa_executor_profile_matches()){diagnostic="Unsupported SA executable for the LCS radar";return false;}
    auto* code=reinterpret_cast<uint8_t*>(transform_address);
    __try {
        if(std::memcmp(code,transform_prefix,sizeof(transform_prefix))!=0 || code[transform_size-1]!=0xC3){
            diagnostic="CRadar::TransformRadarPointToScreenSpace differs (another mod patched it?)";return false;
        }
    }__except(EXCEPTION_EXECUTE_HANDLER){diagnostic="CRadar::TransformRadarPointToScreenSpace unreadable";return false;}
    auto* copy=static_cast<uint8_t*>(VirtualAlloc(nullptr,transform_size,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if(!copy){diagnostic="No memory for the radar trampoline";return false;}
    std::memcpy(copy,code,transform_size);
    DWORD old=0;
    if(!VirtualProtect(code,5,PAGE_EXECUTE_READWRITE,&old)){VirtualFree(copy,0,MEM_RELEASE);diagnostic="Cannot unprotect the radar transform";return false;}
    original_transform=reinterpret_cast<TransformFn>(copy);
    code[0]=0xE9;
    const int32_t relative=int32_t(reinterpret_cast<uintptr_t>(&lcs_transform)-(transform_address+5));
    std::memcpy(code+1,&relative,4);
    FlushInstructionCache(GetCurrentProcess(),code,5);
    DWORD ignored=0;VirtualProtect(code,5,old,&ignored);
    diagnostic="LCS radar geometry installed";
    return true;
}
void sa_radar_frame(bool on,float width,float height){
    const bool ready=on && original_transform && sa_ui_ready();
    if(ready){
        ui::Screen screen;screen.width=width;screen.height=height;
        layout=ui::layout_radar(screen);
    }
    active=ready;
    lcs_radar_skip=ready?1:0;
}
std::optional<RadarSpriteBlip> sa_radar_sprite_blip(int32_t handle){
    if(!sa_executor_profile_matches())return {};
    RadarSpriteBlip result;
    return read_sprite_blip(handle,result)?std::optional<RadarSpriteBlip>(result):std::nullopt;
}
bool sa_radar_set_sprite_blip(int32_t handle,int32_t sprite){
    if(sprite<0 || sprite>=64 || !sa_executor_profile_matches())return false;
    RadarSpriteBlip before;
    if(!read_sprite_blip(handle,before))return false;
    __try {
        reinterpret_cast<void(__cdecl*)(int32_t,int32_t)>(0x583D70)(handle,sprite);
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    RadarSpriteBlip after;
    return read_sprite_blip(handle,after) && after.sprite==sprite;
}
}
extern "C" void __cdecl lcs_radar_disc_draw(){
    using namespace lcs;
    // Hud.cpp: a black copy two units lower, then the disc itself.
    try {
        const auto& r=layout;
        if(!sa_ui_draw_sprite("radardisc",r.disc_left,r.disc_top+r.shadow_offset,r.disc_right,r.disc_bottom+r.shadow_offset,ui::Rgba{0,0,0,255}) ||
           !sa_ui_draw_sprite("radardisc",r.disc_left,r.disc_top,r.disc_right,r.disc_bottom,ui::Rgba{255,255,255,255}))
            {active=false;lcs_radar_skip=0;}
    }catch(...){active=false;lcs_radar_skip=0;}
}
