#include <windows.h>
#include "sa_script_marker_render.hpp"
#include "lcs/sa_executor.hpp"
#include <cstring>
#include <iterator>
#include <utility>
#include <cmath>

namespace lcs {
namespace marker_rw {
using Visit=void*(__cdecl*)(void*,void*(__cdecl*)(void*,void*),void*);
using State=int(__cdecl*)(int,void*);
#ifdef LCS_ISOLATED_MARKER_RENDER_TEST
bool profile();
uintptr_t address(uintptr_t);
void* load_dictionary(const char*);void* load_clump(const char*);
void* current_dictionary();void set_dictionary(void*);
void visit(void*,void*(__cdecl*)(void*,void*),void*);
void* clone(void*);void* frame_create();void frame_set(void*,void*);
void atomic_destroy(void*);void frame_destroy(void*);void clump_destroy(void*);void dictionary_destroy(void*);
void matrix_update(void*);void frame_update(void*);void bright();
std::array<float,3> centre();bool collision(const std::array<float,3>&);
float ground(float,float);std::optional<float> floor(const std::array<float,3>&);
#else
uintptr_t address(uintptr_t value){return value;}
bool profile(){
    struct Entry {uintptr_t address;uint8_t bytes[8];};
    constexpr Entry entries[]={
        {0x5B3860,{0x8b,0x44,0x24,0x04,0x56,0x57,0x50,0x6a}},
        {0x537060,{0x8b,0x44,0x24,0x04,0x56,0x57,0x50,0x6a}},
        {0x7F3A90,{0xa1,0x24,0x7b,0xc9,0,0x8b,0x0d,0x4c}},
        {0x7F3A70,{0x8b,0x0d,0x24,0x7b,0xc9,0,0x8b,0x15}},
        {0x749B70,{0x8b,0x44,0x24,0x04,0x53,0x55,0x56,0x57}},
        {0x749E60,{0x53,0xe8,0xea,0xfd,0xff,0xff,0x8b,0xd8}},
        {0x7F0410,{0xa1,0x24,0x7b,0xc9,0,0x8b,0x0d,0x6c}},
        {0x74BF20,{0x8b,0x44,0x24,0x08,0x56,0x8b,0x74,0x24}},
        {0x749DC0,{0x56,0x8b,0x74,0x24,0x08,0x57,0x56,0x68}},
        {0x7F05A0,{0x56,0x8b,0x74,0x24,0x08,0x56,0x68,0x70}},
        {0x74A310,{0x53,0x55,0x56,0x8b,0x74,0x24,0x10,0x57}},
        {0x7F36A0,{0xa1,0x4c,0x7b,0xc9,0,0x8b,0x0d,0x24}},
        {0x7F18A0,{0x8b,0x44,0x24,0x04,0x81,0x60,0x0c,0xfc}},
        {0x7F0910,{0x8b,0x44,0x24,0x04,0x33,0xc9,0x8b,0x90}},
        {0x735BD0,{0xa1,0xe8,0x86,0xc8,0,0x68,0xa4,0x86}}
        ,{0x56E250,{0x8b,0x44,0x24,0x04,0x85,0xc0,0x8a,0x15}}
        ,{0x569660,{0x83,0xec,0x38,0x8b,0x44,0x24,0x3c,0x8b}}
        ,{0x5696C0,{0x83,0xec,0x38,0x8b,0x44,0x24,0x3c,0x8b}}
        ,{0x410CE0,{0x8b,0x44,0x24,0x08,0x83,0xec,0x08,0x53}}
    };
    __try{for(const auto& entry:entries)if(std::memcmp(reinterpret_cast<const void*>(entry.address),entry.bytes,8))return false;return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void* load_dictionary(const char* path){return reinterpret_cast<void*(__cdecl*)(const char*)>(0x5B3860)(path);}
void* load_clump(const char* path){return reinterpret_cast<void*(__cdecl*)(const char*)>(0x537060)(path);}
void* current_dictionary(){return reinterpret_cast<void*(__cdecl*)()>(0x7F3A90)();}
void set_dictionary(void* p){reinterpret_cast<void*(__cdecl*)(void*)>(0x7F3A70)(p);}
void visit(void* p,void*(__cdecl* fn)(void*,void*),void* data){reinterpret_cast<Visit>(0x749B70)(p,fn,data);}
void* clone(void* p){return reinterpret_cast<void*(__cdecl*)(void*)>(0x749E60)(p);}
void* frame_create(){return reinterpret_cast<void*(__cdecl*)()>(0x7F0410)();}
void frame_set(void* a,void* f){reinterpret_cast<void*(__cdecl*)(void*,void*)>(0x74BF20)(a,f);}
void atomic_destroy(void* p){reinterpret_cast<int(__cdecl*)(void*)>(0x749DC0)(p);}
void frame_destroy(void* p){reinterpret_cast<int(__cdecl*)(void*)>(0x7F05A0)(p);}
void clump_destroy(void* p){reinterpret_cast<int(__cdecl*)(void*)>(0x74A310)(p);}
void dictionary_destroy(void* p){reinterpret_cast<int(__cdecl*)(void*)>(0x7F36A0)(p);}
void matrix_update(void* p){reinterpret_cast<void*(__cdecl*)(void*)>(0x7F18A0)(p);}
void frame_update(void* p){reinterpret_cast<void*(__cdecl*)(void*)>(0x7F0910)(p);}
void bright(){reinterpret_cast<void(__cdecl*)()>(0x735BD0)();}
std::array<float,3> centre(){
    // FindPlayerCentreOfWorld(0), including native camera/remote-car branches.
    return reinterpret_cast<const std::array<float,3>&(__cdecl*)(int32_t)>(0x56E250)(0);
}
bool collision(const std::array<float,3>& p){
    // Real CColStore producer/consumer: default current native area, including
    // SetCollisionRequired side effects. No constant "map ready" predicate.
    return reinterpret_cast<bool(__cdecl*)(const std::array<float,3>&,int32_t)>(0x410CE0)(p,-1);
}
float ground(float x,float y){return reinterpret_cast<float(__cdecl*)(float,float)>(0x569660)(x,y);}
std::optional<float> floor(const std::array<float,3>& p){
    bool found=false;
    const float height=reinterpret_cast<float(__cdecl*)(float,float,float,bool*,void**)>(0x5696C0)(p[0],p[1],p[2]+1.f,&found,nullptr);
    return found?std::optional<float>(height):std::optional<float>{};
}
#endif
}
namespace {
template<class F>bool guard(const F& fn){__try{fn();return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}}
template<class T>T& field(void* p,size_t offset){return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(p)+offset);}
template<class T>T& global(uintptr_t p){return *reinterpret_cast<T*>(marker_rw::address(p));}
struct Mesh {void* clump=nullptr;void* atomic=nullptr;void* frame=nullptr;};
struct Resources {
    void* dictionary=nullptr;void* engine=nullptr;
    std::array<Mesh,3> meshes{};
    DWORD thread=0;
    std::filesystem::path directory;
    bool complete=false,faulted=false;
} resources;
uintptr_t frame_bound=0,shutdown_bound=0;
const char* failure="none";
void (*logger)(const std::string&)=nullptr;
int logged_draws=0,logged_submits=0;
bool why(const char* reason){failure=reason;return false;}
bool current_binding(uintptr_t slot,uintptr_t callback){
    if(!callback)return false;
    const auto code=reinterpret_cast<const uint8_t*>(marker_rw::address(slot));
    int32_t relative=0;if(*code!=0xE8)return false;
    std::memcpy(&relative,code+1,4);
    return reinterpret_cast<uintptr_t>(code+5)+relative==callback;
}
bool bindings_current(){return current_binding(0x725067,frame_bound) && current_binding(0x723391,shutdown_bound);}
bool owner(){return resources.thread==GetCurrentThreadId() && resources.engine && resources.engine==global<void*>(0xC97B24);}
void* __cdecl find_atomic(void* atomic,void* data){auto& result=*static_cast<std::pair<void*,size_t>*>(data);result.first=atomic;++result.second;return atomic;}
bool executable(void* p){MEMORY_BASIC_INFORMATION info{};return p && VirtualQuery(p,&info,sizeof(info)) && info.State==MEM_COMMIT && !(info.Protect&PAGE_GUARD) && (info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));}
// RW SDK RwRenderState: U/V addressing=3/4, alpha function/ref=29/30.
// State31 is not valid. Do not assume that a permissive mock grants support.
constexpr int states[]={1,3,4,6,7,8,9,10,11,12,14,15,16,17,20,29,30};
struct FrameState {
    marker_rw::State get=nullptr,set=nullptr;
    std::array<uint32_t,std::size(states)> values{};
    size_t captured=0;
    uint32_t z_write=0;
    void* ambient=nullptr;void* directional=nullptr;
    uint8_t globals[32]{},ambient_colour[16]{},directional_colour[16]{},ambient_flag=0,directional_flag=0;
    uint8_t expected_globals[32]{},expected_ambient[16]{},expected_directional[16]{};
    bool bright_entered=false;
    uint8_t expected_ambient_flag=0,expected_directional_flag=0;
    bool lights=false;
};
bool capture(FrameState& f){
    auto engine=reinterpret_cast<uintptr_t>(resources.engine);
    f.set=*reinterpret_cast<marker_rw::State*>(engine+0x20);f.get=*reinterpret_cast<marker_rw::State*>(engine+0x24);
    if(!executable(reinterpret_cast<void*>(f.set)) || !executable(reinterpret_cast<void*>(f.get)))return false;
    for(size_t i=0;i<std::size(states);++i){if(!f.get(states[i],&f.values[i]))return false;if(states[i]==8)f.z_write=f.values[i];++f.captured;}
    f.ambient=global<void*>(0xC886E8);f.directional=global<void*>(0xC886EC);
    if(!f.ambient || !f.directional)return false;
    std::memcpy(f.globals,&global<uint8_t>(0xC88694),32);
    std::memcpy(f.ambient_colour,&field<uint8_t>(f.ambient,0x18),16);
    std::memcpy(f.directional_colour,&field<uint8_t>(f.directional,0x18),16);
    f.ambient_flag=field<uint8_t>(f.ambient,3);f.directional_flag=field<uint8_t>(f.directional,3);f.lights=true;
    return true;
}
bool restore(FrameState& f){
    if(!owner())return why("restore: engine or thread changed");
    bool ok=true;
    for(size_t i=0;i<f.captured;++i)if(!f.set(states[i],reinterpret_cast<void*>(uintptr_t(f.values[i]))))ok=why("restore: render state refused");
    if(f.lights){
        if(global<void*>(0xC886E8)!=f.ambient || global<void*>(0xC886EC)!=f.directional)return why("restore: a light object was replaced");
        // Like the native C3dMarkers::Render, put the captured light values back even when a callback touched them.
        std::memcpy(&global<uint8_t>(0xC88694),f.globals,32);
        std::memcpy(&field<uint8_t>(f.ambient,0x18),f.ambient_colour,16);
        std::memcpy(&field<uint8_t>(f.directional,0x18),f.directional_colour,16);
        field<uint8_t>(f.ambient,3)=f.ambient_flag;field<uint8_t>(f.directional,3)=f.directional_flag;
    }
    return ok;
}
bool draw(const ScriptMarkerInstance& entry,FrameState& state){
    if(logger && logged_draws<12){++logged_draws;logger("Marker draw: type="+std::to_string(entry.source.type)+" size="+std::to_string(entry.current_size)+" vertical="+std::to_string(entry.vertical_size)+" pos="+std::to_string(entry.matrix[9])+","+std::to_string(entry.matrix[10])+","+std::to_string(entry.matrix[11])+" colour="+std::to_string(entry.source.colour[0])+","+std::to_string(entry.source.colour[1])+","+std::to_string(entry.source.colour[2])+","+std::to_string(entry.source.colour[3])+" range="+std::to_string(entry.camera_range));}
    const auto index=entry.source.type==1?0:entry.source.type==2?1:entry.source.type==5?2:3;
    if(index>=3)return why("draw: unknown marker type");
    auto& mesh=resources.meshes[index];
    if(!mesh.atomic || field<uint8_t>(mesh.atomic,0)!=1 || field<void*>(mesh.atomic,4)!=mesh.frame)return why("draw: atomic changed");
    const auto geometry=field<void*>(mesh.atomic,0x18);
    if(!geometry || field<int32_t>(geometry,0x24)<1)return why("draw: geometry has no materials");
    const auto table=field<void**>(geometry,0x20);
    if(!table || !table[0])return why("draw: material table missing");
    // Source Render changes the first material, then scales source X/Y and Z
    // independently on a fresh frame. No native CONE/TUBE or world model ID.
    auto material=table[0];const auto colour=field<uint32_t>(material,4);
    uint32_t packed=0;std::memcpy(&packed,entry.source.colour.data(),4);field<uint32_t>(material,4)=packed;
    auto matrix=reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(mesh.frame)+0x10);
    for(size_t row=0;row<4;++row)for(size_t axis=0;axis<3;++axis)
        matrix[row*4+axis]=entry.matrix[row*3+axis]*(row==0 || row==1?entry.current_size:row==2?entry.vertical_size:1.f);
    marker_rw::matrix_update(matrix);marker_rw::frame_update(mesh.frame);
    const auto callback=field<void*(__cdecl*)(void*)>(mesh.atomic,0x48);
    if(!executable(reinterpret_cast<void*>(callback))){field<uint32_t>(material,4)=colour;return why("draw: atomic render callback not executable");}
    const auto previous_z=state.z_write;
    if(entry.source.type==5 && !state.set(8,nullptr)){field<uint32_t>(material,4)=colour;return why("draw: z-write state refused");}
    callback(mesh.atomic);
    field<uint32_t>(material,4)=colour;
    if(entry.source.type==5 && !state.set(8,reinterpret_cast<void*>(uintptr_t(previous_z))))return why("draw: z-write restore refused");
    return true;
}
}
void sa_script_marker_render_bound(uintptr_t frame,uintptr_t shutdown){frame_bound=frame;shutdown_bound=shutdown;}
bool sa_script_marker_render_profile(){return sa_executor_profile_matches() && marker_rw::profile();}
bool sa_script_marker_phase_profile(){
#ifdef LCS_ISOLATED_MARKER_RENDER_TEST
    return false; // Real CALL locations are verified independently from PE data.
#else
    if(!sa_script_marker_render_profile())return false;
    __try {
        const uint8_t render[]={0x53,0x56,0x33,0xDB,0x57,0x89,0x1D,0xD8,0xC6,0xC7,0};
        const uint8_t after[]={0xBE,0x88,0xDD,0xC7,0};
        const uint8_t shutdown_after[]={0xA1,0x18,0xC7,0xC7,0};
        // Preserve/chains CALL targets installed by another plugin. The
        // reviewed surrounding source phase, not that plugin's bytes, admits it.
        return !std::memcmp(reinterpret_cast<void*>(0x725040),render,sizeof(render)) &&
            !std::memcmp(reinterpret_cast<void*>(0x72506C),after,sizeof(after)) &&
            *reinterpret_cast<uint8_t*>(0x725067)==0xE8 &&
            *reinterpret_cast<uint8_t*>(0x723390)==0x56 &&
            *reinterpret_cast<uint8_t*>(0x723391)==0xE8 &&
            !std::memcmp(reinterpret_cast<void*>(0x723396),shutdown_after,sizeof(shutdown_after));
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
#endif
}
bool sa_script_marker_render_ready(){
    bool result=false;
    return frame_bound && shutdown_bound && !resources.faulted && guard([&]{
        if(!bindings_current()){if(resources.engine)resources.faulted=true;return;}
        if(resources.thread==GetCurrentThreadId() && resources.engine && resources.engine!=global<void*>(0xC97B24)){resources.faulted=true;return;}
        result=resources.complete && owner() && sa_script_marker_render_profile();
    }) && result;
}
bool sa_script_marker_health(){return !resources.engine || sa_script_marker_render_ready();}
bool sa_script_marker_resources_load(const std::filesystem::path& directory){
    if(resources.complete)return resources.directory==directory.lexically_normal() && sa_script_marker_render_ready();
    if(!frame_bound || !shutdown_bound || resources.faulted || !sa_script_marker_render_profile())return false;
    bool bindings=false;if(!guard([&]{bindings=bindings_current();}) || !bindings)return false;
    std::array<std::string,4> paths;
    const char* files[] = {"texture-assets/lcs_markers.txd","rw-assets/lcs_marker1.dff","rw-assets/lcs_marker2.dff","rw-assets/lcs_marker5.dff"};
    for(size_t i=0;i<4;++i){const auto p=directory/files[i];if(!std::filesystem::is_regular_file(p))return false;paths[i]=p.string();}
    void* previous=nullptr;bool bound=false,ok=false;
    const bool safe=guard([&]{
        resources.engine=global<void*>(0xC97B24);resources.thread=GetCurrentThreadId();
        if(!resources.engine)return;
        previous=marker_rw::current_dictionary();
        resources.dictionary=marker_rw::load_dictionary(paths[0].c_str());
        if(!resources.dictionary)return;
        if(marker_rw::current_dictionary()!=previous)return;
        marker_rw::set_dictionary(resources.dictionary);bound=true;
        if(marker_rw::current_dictionary()!=resources.dictionary)return;
        for(size_t i=0;i<3;++i){
            auto& mesh=resources.meshes[i];mesh.clump=marker_rw::load_clump(paths[i+1].c_str());if(!mesh.clump)return;
            std::pair<void*,size_t> found{};marker_rw::visit(mesh.clump,&find_atomic,&found);
            if(found.second!=1 || !found.first)return;
            mesh.atomic=marker_rw::clone(found.first);if(!mesh.atomic)return;
            mesh.frame=marker_rw::frame_create();if(!mesh.frame)return;
            marker_rw::frame_set(mesh.atomic,mesh.frame);
            auto geometry=field<void*>(mesh.atomic,0x18);if(!geometry)return;
            field<uint32_t>(geometry,8)|=0x40;
            if(marker_rw::current_dictionary()!=resources.dictionary)return;
        }
        ok=true;
    });
    bool restored=!bound;
    if(bound)guard([&]{if(owner() && marker_rw::current_dictionary()==resources.dictionary){marker_rw::set_dictionary(previous);restored=marker_rw::current_dictionary()==previous;}});
    if(!safe || !ok || !restored){resources.faulted=true;return false;}
    resources.directory=directory.lexically_normal();resources.complete=true;return true;
}
const char* sa_script_marker_last_failure(){return failure;}
void sa_script_marker_forgive(){resources.faulted=false;}
bool sa_script_marker_frame(ScriptMarkerPool& pool){
    if(!sa_script_marker_render_ready())return why("frame: renderer not ready (bindings, thread, engine or profile changed)");
    FrameState state;bool ok=false;
    const bool safe=guard([&]{
        if(!capture(state)){why("frame: render state capture failed");return;}
        marker_rw::bright();state.bright_entered=true;
        std::memcpy(state.expected_globals,&global<uint8_t>(0xC88694),32);
        std::memcpy(state.expected_ambient,&field<uint8_t>(state.ambient,0x18),16);
        std::memcpy(state.expected_directional,&field<uint8_t>(state.directional,0x18),16);
        state.expected_ambient_flag=field<uint8_t>(state.ambient,3);
        state.expected_directional_flag=field<uint8_t>(state.directional,3);
        ok=pool.frame([&](size_t,const auto& entry){return draw(entry,state);});
    });
    bool restored=false;const bool cleanup=guard([&]{restored=restore(state);});
    if(!safe)why("frame: native exception while drawing");
    else if(ok && !cleanup)why("frame: native exception while restoring");
    if(!safe || !ok || !cleanup || !restored){resources.faulted=true;return false;}
    return true;
}
bool sa_script_marker_resources_release(){
    if(!resources.engine)return true;
    if(resources.faulted || !sa_script_marker_render_profile())return false;
    bool ok=false;const bool safe=guard([&]{
        if(!owner() || marker_rw::current_dictionary()==resources.dictionary)return;
        for(auto& mesh:resources.meshes){
            if(mesh.atomic){marker_rw::atomic_destroy(mesh.atomic);mesh.atomic=nullptr;}
            if(mesh.frame){marker_rw::frame_destroy(mesh.frame);mesh.frame=nullptr;}
            if(mesh.clump){marker_rw::clump_destroy(mesh.clump);mesh.clump=nullptr;}
        }
        if(resources.dictionary){marker_rw::dictionary_destroy(resources.dictionary);resources.dictionary=nullptr;}
        ok=true;
    });
    if(!safe || !ok){resources.faulted=true;return false;}
    resources={};return true;
}
std::optional<float> sa_script_marker_ground(float x,float y){
    if(!sa_script_marker_render_ready() || !std::isfinite(x) || !std::isfinite(y))return {};
    float height=0;const bool safe=guard([&]{height=marker_rw::ground(x,y);});
    if(!safe || !std::isfinite(height)){resources.faulted=true;return {};}
    return height;
}
void sa_script_marker_set_logger(void (*function)(const std::string&)){logger=function;}
bool sa_script_marker_submit(ScriptMarkerPool& pool,const ScriptMarkerCommand& command,float time_step){
    if(logger && logged_submits<12){++logged_submits;const auto& p=command.placement;logger("Marker submit: type="+std::to_string(p.type)+" size="+std::to_string(p.size)+" pos="+std::to_string(p.position[0])+","+std::to_string(p.position[1])+","+std::to_string(p.position[2])+" colour="+std::to_string(p.colour[0])+","+std::to_string(p.colour[1])+","+std::to_string(p.colour[2])+" vertical="+std::to_string(p.vertical_size)+" ready="+std::to_string(sa_script_marker_render_ready()));}
    if(!sa_script_marker_render_ready() || !std::isfinite(time_step))return false;
    ScriptMarkerEnvironment env;env.time_step=time_step;
    env.floor=[](const std::array<float,3>& position)->std::optional<ScriptMarkerEnvironment::Floor>{
        if(!marker_rw::collision(position))return ScriptMarkerEnvironment::Floor{false,{}};
        return ScriptMarkerEnvironment::Floor{true,marker_rw::floor(position)};
    };
    bool ok=false;const bool safe=guard([&]{
        env.now=global<uint32_t>(0xB7CB84);env.player=marker_rw::centre();
        ok=pool.submit(command,env);
    });
    if(!safe || !ok){resources.faulted=true;return false;}
    return true;
}
bool sa_script_marker_cylinder_set(ScriptMarkerPool& pool,uintptr_t identity,const std::array<float,3>& position,
                                   float size,float pulse_fraction,uint16_t pulse_period,float time_step){
    if(!identity || !pulse_period || (pulse_period&(pulse_period-1)) || !std::isfinite(size) || !std::isfinite(pulse_fraction))return false;
    // Retail PlaceMarkerSet (PS2 0x24B140): the same placement at 1.00, 0.93 and
    // 0.86 of the size with rotation rates 1, 2 and -1; colour 255,255,128,228.
    static constexpr struct {float scale;int16_t rotate;} layers[]={{1.f,1},{.93f,2},{.86f,-1}};
    for(const auto& layer:layers){
        ScriptMarkerCommand command;auto& p=command.placement;
        p.identity=identity;p.type=5;p.pulse_period=pulse_period;p.rotate_rate=layer.rotate;
        p.position=position;p.size=size*layer.scale;p.pulse_fraction=pulse_fraction;p.vertical_size=0;
        p.colour={255,255,128,228};
        if(!sa_script_marker_submit(pool,command,time_step))return false;
    }
    return true;
}
}
