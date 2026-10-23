#include <windows.h>
#include "sa_ui_draw.hpp"
#include "lcs/native_memory.hpp"
#include "lcs/sa_executor.hpp"
#include "lcs/radar_sprites.hpp"
#include <array>
#include <cstring>
#include <map>
#include <set>
namespace lcs {
namespace {
// RenderWare 3.6 / SA 1.0 US entry points, each confirmed against the user's
// executable: stream open/close, TXD read and lookup, and the 2D immediate-mode
// primitive that CSprite2d submits.
struct RwImVertex {float x,y,z,rhw;uint32_t colour;float u,v;};
static_assert(sizeof(RwImVertex)==0x1C,"RwIm2DVertex layout");
using StreamOpen=void*(__cdecl*)(int,int,const char*);
using StreamClose=int(__cdecl*)(void*,void*);
using StreamFind=int(__cdecl*)(void*,uint32_t,uint32_t*,uint32_t*);
using DictRead=void*(__cdecl*)(void*);
using DictFind=void*(__cdecl*)(void*,const char*);
using RenderPrimitive=int(__cdecl*)(int,RwImVertex*,int);
using RenderState=int(__cdecl*)(int,void*);
using RenderStateGet=int(__cdecl*)(int,void*);
constexpr uintptr_t open_stream=0x7ECEF0,close_stream=0x7ECE20,find_chunk=0x7ED2D0,read_dict=0x804C30,find_texture=0x7F39F0,
    render_primitive=0x734E90,engine_instance=0xC97B24,near_screen_z=0xC80460,recip_near_clip=0xC80464,
    screen_width=0xC17044,screen_height=0xC17048;
enum RenderStateId:int {
    texture_raster=1,z_test_enable=6,shade_mode=7,z_write_enable=8,texture_filter=9,
    source_blend=10,destination_blend=11,vertex_alpha_enable=12
};
constexpr int primitive_triangle_strip=4;
constexpr std::array<int,8> saved_render_states={texture_raster,z_test_enable,shade_mode,z_write_enable,
    texture_filter,source_blend,destination_blend,vertex_alpha_enable};
void* fonts_dictionary=nullptr;
void* hud_dictionary=nullptr;
std::array<void*,3> font_pages{};
std::map<std::string,void*> hud_textures;
bool ready=false,faulted=false;
std::filesystem::path weapons_directory;
// SA weapon type -> weapon model TXD and the icon texture inside it (the original
// LCS model name), as mounted by tools/weapon_slots.py. Types without a row (flowers,
// cane, shovel, pool cue, spray can, extinguisher...) have no LCS icon.
struct WeaponIcon {int type;const char* file;const char* texture;};
constexpr WeaponIcon weapon_icons[]={
    {1,"brassknuckle","brassknuckle"},{2,"golfclub","hockeystick"},{3,"nitestick","nitestick"},{4,"knifecur","knifecur"},
    {5,"bat","bat"},{8,"katana","katana"},{9,"chnsaw","chnsaw"},{10,"gun_dildo1","handaxe"},{11,"gun_dildo2","chisel"},
    {12,"gun_vibe1","machete"},{13,"gun_vibe2","cleaver"},{16,"grenade","grenade"},{17,"teargas","teargas"},
    {18,"molotov","molotov"},{22,"colt45","glock17"},{23,"silenced","ingramsl"},{24,"desert_eagle","python"},
    {25,"chromegun","chromegun"},{26,"sawnoff","buddyshot"},{27,"shotgspa","shotgspa"},{28,"micro_uzi","uzi"},
    {29,"mp5lng","mp5k"},{30,"ak47","ak47"},{31,"m4","m4"},{32,"tec9","tec9"},{33,"cuntgun","m60"},
    {34,"sniper","sniper"},{35,"rocketla","rocketla"},{36,"rocketla","rocketla"},{37,"flame","flame"},
    {38,"minigun","minigun"},{39,"satchel","bomb"},{40,"satchel","detonator"},{43,"camera","camera"}};
std::map<int,void*> weapon_textures;       // loaded icon per type (nullptr: unavailable)
std::map<std::string,void*> weapon_dictionaries;
template<class Call> bool guarded(Call const& call){__try{call();return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
void* load_dictionary(const std::filesystem::path& file){
    void* result=nullptr;
    const auto narrow=file.string();
    guarded([&]{
        void* stream=reinterpret_cast<StreamOpen>(open_stream)(2,1,narrow.c_str());
        if(!stream)return;
        uint32_t length=0,version=0;
        if(reinterpret_cast<StreamFind>(find_chunk)(stream,0x16,&length,&version))
            result=reinterpret_cast<DictRead>(read_dict)(stream);
        reinterpret_cast<StreamClose>(close_stream)(stream,nullptr);
    });
    return result;
}
void* find_named(void* dictionary,const char* name){
    void* result=nullptr;
    guarded([&]{result=reinterpret_cast<DictFind>(find_texture)(dictionary,name);});
    return result;
}
struct RenderStateSnapshot {
    RenderState set=nullptr;
    RenderStateGet get=nullptr;
    std::array<uintptr_t,saved_render_states.size()> values{};
    size_t captured=0;
};
bool capture_render_states(RenderStateSnapshot& snapshot){
    const auto engine=*reinterpret_cast<const uintptr_t*>(engine_instance);
    if(!engine)return false;
    snapshot.set=*reinterpret_cast<RenderState*>(engine+0x20);
    snapshot.get=*reinterpret_cast<RenderStateGet*>(engine+0x24);
    if(!snapshot.set || !snapshot.get)return false;
    for(size_t i=0;i<saved_render_states.size();++i){
        uintptr_t value=0;
        if(!snapshot.get(saved_render_states[i],&value))return false;
        snapshot.values[i]=value;
        ++snapshot.captured;
    }
    return true;
}
bool restore_render_states(const RenderStateSnapshot& snapshot){
    bool ok=true;
    for(size_t i=0;i<snapshot.captured;++i)
        if(!snapshot.set(saved_render_states[i],reinterpret_cast<void*>(snapshot.values[i])))ok=false;
    return ok;
}
bool set_state(const RenderStateSnapshot& snapshot,int state,uintptr_t value){
    return snapshot.set(state,reinterpret_cast<void*>(value))!=0;
}
void* raster_of(void* texture){return texture?*reinterpret_cast<void**>(texture):nullptr;}
uint32_t argb(const ui::Rgba& c){return uint32_t(c.a)<<24|uint32_t(c.r)<<16|uint32_t(c.g)<<8|c.b;}
bool begin(const RenderStateSnapshot& snapshot,void* raster){
    // The states CSprite2d relies on while the HUD is drawing: no depth, alpha
    // blending, per-vertex alpha, flat shading and the quad's own raster.
    return set_state(snapshot,texture_raster,reinterpret_cast<uintptr_t>(raster)) &&
        set_state(snapshot,z_test_enable,0) && set_state(snapshot,z_write_enable,0) &&
        set_state(snapshot,shade_mode,1) && set_state(snapshot,texture_filter,2) &&
        set_state(snapshot,source_blend,5) && set_state(snapshot,destination_blend,6) &&
        set_state(snapshot,vertex_alpha_enable,1);
}
// Each group follows the TL, TR, BL, BR order emitted by GlyphQuad.
bool submit(void* raster,const RwImVertex* vertices,size_t count){
    if(!vertices || count==0 || count%4!=0)return false;
    float z=0.f,rhw=0.f;
    if(!guarded([&]{
        z=*reinterpret_cast<const float*>(near_screen_z);
        rhw=*reinterpret_cast<const float*>(recip_near_clip);
    }))return false;
    RenderStateSnapshot snapshot{};
    bool captured=false;
    if(!guarded([&]{captured=capture_render_states(snapshot);}) || !captured)return false;
    bool drew=false;
    const bool draw_call_survived=guarded([&]{
        if(!begin(snapshot,raster))return;
        drew=true;
        for(size_t i=0;i<count;i+=4){
            RwImVertex strip[4];
            std::memcpy(strip,vertices+i,sizeof(strip));
            for(auto& v:strip){v.z=z;v.rhw=rhw;}
            if(!reinterpret_cast<RenderPrimitive>(render_primitive)(primitive_triangle_strip,strip,4)){
                drew=false;break;
            }
        }
    });
    bool restored=false;
    const bool restore_call_survived=guarded([&]{restored=restore_render_states(snapshot);});
    return draw_call_survived && drew && restore_call_survived && restored;
}
}
bool sa_ui_load(const std::filesystem::path& directory,std::string& diagnostic){
    ready=false;faulted=false;
    if(!sa_executor_profile_matches()){diagnostic="Unsupported SA executable for the LCS UI layer";return false;}
    // Not fonts.txd/hud.txd: the modloader would install those names over SA's own dictionaries.
    const auto fonts=directory/L"lcs_fonts.txd",hud=directory/L"lcs_hud.txd";
    std::error_code ec;
    if(!std::filesystem::is_regular_file(fonts,ec) || !std::filesystem::is_regular_file(hud,ec)){
        diagnostic="LCS lcs_fonts.txd/lcs_hud.txd are missing from "+directory.string();return false;
    }
    if(!fonts_dictionary)fonts_dictionary=load_dictionary(fonts);
    if(!hud_dictionary)hud_dictionary=load_dictionary(hud);
    if(!fonts_dictionary || !hud_dictionary){diagnostic="RenderWare could not read the LCS UI texture dictionaries";return false;}
    // Original order: Sprite[0]=font2, [1]=font1, [2]=font3.
    const char* names[3]={"font2","font1","font3"};
    for(size_t i=0;i<3;++i){
        font_pages[i]=find_named(fonts_dictionary,names[i]);
        if(!raster_of(font_pages[i])){diagnostic=std::string("LCS font page missing: ")+names[i];return false;}
    }
    hud_textures.clear();weapon_textures.clear();weapon_dictionaries.clear();
    // directory is <package>/models/lcs; the weapon TXDs live in <package>/weapons.
    weapons_directory=directory.parent_path().parent_path()/L"weapons";
    for(const char* name:{"bar_inside1","bar_inside2","bar_inside1dark","bar_inside2dark","bar_outline","radardisc","radardiscback","hudnumbers","fist"}){
        if(auto* texture=find_named(hud_dictionary,name))hud_textures[name]=texture;
    }
    ready=true;
    diagnostic="LCS UI textures loaded: fontPages=3 hudSprites="+std::to_string(hud_textures.size());
    return true;
}
bool sa_ui_ready(){return ready && !faulted;}
namespace {
void* weapon_icon_texture(int type){
    const auto cached=weapon_textures.find(type);
    if(cached!=weapon_textures.end())return cached->second;
    void* texture=nullptr;
    for(const auto& row:weapon_icons){
        if(row.type!=type)continue;
        auto dictionary=weapon_dictionaries.find(row.file);
        if(dictionary==weapon_dictionaries.end())
            dictionary=weapon_dictionaries.emplace(row.file,load_dictionary(weapons_directory/(std::string(row.file)+".txd"))).first;
        if(dictionary->second)texture=find_named(dictionary->second,row.texture);
        if(!raster_of(texture))texture=nullptr;
        break;
    }
    weapon_textures[type]=texture;
    return texture;
}
}
bool sa_ui_draw_weapon_icon(int type,float left,float top,float right,float bottom,ui::Rgba c){
    if(!sa_ui_ready())return false;
    void* texture=weapon_icon_texture(type);
    if(!texture)return false;
    const uint32_t colour=argb(c);
    const RwImVertex v[4]={{left,top,0,0,colour,0,0},{right,top,0,0,colour,1,0},{left,bottom,0,0,colour,0,1},{right,bottom,0,0,colour,1,1}};
    if(!submit(raster_of(texture),v,4)){faulted=true;return false;}
    return true;
}
bool sa_ui_screen(float& width,float& height){
    bool ok=false;
    guarded([&]{
        const auto w=*reinterpret_cast<const int32_t*>(native_entity_address(screen_width));
        const auto h=*reinterpret_cast<const int32_t*>(native_entity_address(screen_height));
        if(w>0 && h>0 && w<=32768 && h<=32768){width=float(w);height=float(h);ok=true;}
    });
    return ok;
}
bool sa_ui_draw_glyphs(const std::vector<ui::GlyphQuad>& quads){
    if(!sa_ui_ready())return false;
    for(size_t page=0;page<3;++page){
        std::vector<RwImVertex> vertices;
        for(const auto& q:quads){
            if(q.page!=int(page))continue;
            for(int i=0;i<4;++i)vertices.push_back({q.x[i],q.y[i],0.f,0.f,argb(q.colour),q.u[i],q.v[i]});
        }
        if(!vertices.empty() && !submit(raster_of(font_pages[page]),vertices.data(),vertices.size())){faulted=true;return false;}
    }
    return true;
}
bool sa_ui_draw_rect(float left,float top,float right,float bottom,ui::Rgba c){
    if(!sa_ui_ready())return false;
    const uint32_t colour=argb(c);
    const RwImVertex v[4]={{left,top,0,0,colour,0,0},{right,top,0,0,colour,1,0},{left,bottom,0,0,colour,0,1},{right,bottom,0,0,colour,1,1}};
    if(!submit(nullptr,v,4)){faulted=true;return false;}
    return true;
}
bool sa_ui_draw_sprite(const char* name,float left,float top,float right,float bottom,ui::Rgba c,float u0,float v0,float u1,float v1){
    if(!sa_ui_ready())return false;
    const auto found=hud_textures.find(name);
    if(found==hud_textures.end())return false;
    const uint32_t colour=argb(c);
    const RwImVertex v[4]={{left,top,0,0,colour,u0,v0},{right,top,0,0,colour,u1,v0},{left,bottom,0,0,colour,u0,v1},{right,bottom,0,0,colour,u1,v1}};
    if(!submit(raster_of(found->second),v,4)){faulted=true;return false;}
    return true;
}

namespace {
// LCS radar sprite id -> texture in the PS2 hud.txd (Radar.cpp LoadTextures). Icons the PS2 file
// does not carry (leftovers of the Vice City code) keep SA's texture in their slot.
struct RadarIcon {int lcs_id;const char* texture;};
constexpr RadarIcon radar_icons[]={
    {1,"radar_centre"},{2,"arrow"},{3,"radar_north"},{6,"radar_cortez"},{7,"radar_diaz"},{8,"radar_kent"},
    {9,"radar_lawyer"},{10,"radar_phil"},{16,"gun"},{19,"radar_save"},{25,"property"},{27,"spray"},{30,"phone"},
    {31,"radar_8Ball"},{32,"radar_Burner"},{33,"radar_Cipriani"},{34,"radar_Donald"},{35,"radar_Joseph"},
    {36,"radar_Leone"},{37,"radar_Maria"},{38,"radar_McRay"},{39,"radar_Toshiko"},{40,"radar_Vincenzo"},
    {41,"radar_Bomb"},{42,"radar_cShop"},{43,"radar_propertyR"},{44,"radar_flag"},{45,"radar_powerup"},
    {46,"mp_base"},{47,"mp_checkpoint"},{48,"mp_player"},{49,"mp_objective"},{50,"mp_car"},{51,"mp_tank"},
    {52,"mp_carlockup"},{53,"mp_targetplayer"},{54,"arrow_00"},{55,"arrow_01"},{56,"arrow_02"},{57,"arrow_03"},
    {58,"arrow_04"},{59,"arrow_05"},{60,"arrow_06"},{61,"arrow_07"}};
constexpr uintptr_t radar_blip_sprites=0xBAA250;   // CSprite2d[64]: one RwTexture* each
std::map<std::string,void*> radar_textures;
std::set<void*> retained_radar_textures;
}
bool sa_ui_apply_radar_sprites(){
    if(!sa_ui_ready() || !hud_dictionary)return false;
    for(const auto& icon:radar_icons){
        void*& texture=radar_textures[icon.texture];
        if(!texture){texture=find_named(hud_dictionary,icon.texture);if(!raster_of(texture)){texture=nullptr;continue;}}
        const bool applied=guarded([&]{
            const auto native_slot=source_radar_sprite(icon.lcs_id,true);
            auto* slot=reinterpret_cast<void**>(native_entity_address(radar_blip_sprites)+size_t(native_slot)*4);
            if(*slot==texture)return;
            // SA destroys these textures when its radar shuts down: hold one reference for it.
            if(retained_radar_textures.insert(texture).second)++*reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(texture)+0x54);
            *slot=texture;
        });
        if(!applied){faulted=true;return false;}
    }
    return true;
}
}
