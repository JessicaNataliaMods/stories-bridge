#define LCS_ISOLATED_MARKER_RENDER_TEST
#include "../src/sa_script_marker_render.cpp"
#include <fstream>
#include <iostream>
#include <thread>
#include <stdexcept>
namespace {
void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
template<class T>void put(void* p,size_t off,T value){std::memcpy(static_cast<uint8_t*>(p)+off,&value,sizeof(value));}
alignas(16) uint8_t device[128]{},ambient[64]{},directional[64]{},colours[32]{};
void* engine=device;void* ambient_pointer=ambient;void* directional_pointer=directional;
int dictionary_token=0,foreign_token=0,stolen_token=0;
void* current=&foreign_token;
uint8_t frame_call[5]{},shutdown_call[5]{};
uint32_t render_states[32]{};
bool valid_profile=true,raise_draw=false,steal_dictionary=false;
size_t loaded=0,destroyed=0,draws=0,updates=0;
size_t centre_calls=0,collision_calls=0,floor_calls=0;
uint32_t native_time=1000;bool collision_loaded=true;
struct FakeMesh {uint8_t source[96]{},atomic[96]{},frame[192]{},geometry[64]{},material[32]{};void* materials[1]{};} meshes[3];
int __cdecl state_get(int id,void* out){if(id<1 || id>30 || id==18 || id==19)return 0;*static_cast<uint32_t*>(out)=render_states[id];return 1;}
int __cdecl state_set(int id,void* value){if(id<1 || id>30 || id==18 || id==19)return 0;render_states[id]=uint32_t(reinterpret_cast<uintptr_t>(value));return 1;}
void* __cdecl render(void* atomic){
    ++draws;auto* frame=lcs::field<void*>(atomic,4);auto* matrix=reinterpret_cast<float*>(static_cast<uint8_t*>(frame)+0x10);
    check(matrix[0]==7 && matrix[5]==7 && matrix[10]==100 && matrix[12]==200,"Source XYZ scale reaches actual atomic frame without native marker alias");
    auto* geometry=lcs::field<void*>(atomic,0x18);auto table=lcs::field<void**>(geometry,0x20);
    check(lcs::field<uint32_t>(table[0],4)==0xFF1E140A && render_states[8]==0,"Source RGBA and cylinder Z-write reach draw callback");
    render_states[20]=1;render_states[9]=6; // Native/mod callback state changes scoped by pass.
    if(raise_draw)RaiseException(0xE0434343,0,0,nullptr);
    return atomic;
}
void bind(){
    for(auto* slot:{frame_call,shutdown_call}){slot[0]=0xE8;const auto relative=int32_t(reinterpret_cast<uintptr_t>(&render)-reinterpret_cast<uintptr_t>(slot+5));put(slot,1,relative);}
    lcs::sa_script_marker_render_bound(reinterpret_cast<uintptr_t>(&render),reinterpret_cast<uintptr_t>(&render));
}
void reset(){
    lcs::resources={};valid_profile=true;raise_draw=false;steal_dictionary=false;
    loaded=destroyed=draws=updates=0;current=&foreign_token;engine=device;
    centre_calls=collision_calls=floor_calls=0;collision_loaded=true;
    std::memset(device,0,sizeof(device));std::memset(ambient,0x31,sizeof(ambient));std::memset(directional,0x42,sizeof(directional));std::memset(colours,0x53,sizeof(colours));
    put(device,0x20,&state_set);put(device,0x24,&state_get);
    for(size_t i=0;i<32;++i)render_states[i]=uint32_t(i+1);
    for(auto& mesh:meshes){mesh={};mesh.materials[0]=mesh.material;mesh.source[0]=mesh.atomic[0]=1;
        put(mesh.source,0x18,static_cast<void*>(mesh.geometry));put(mesh.atomic,0x18,static_cast<void*>(mesh.geometry));
        put(mesh.atomic,0x48,&render);put(mesh.geometry,0x20,mesh.materials+0);put(mesh.geometry,0x24,int32_t(1));put(mesh.material,4,uint32_t(0xFFFFFFFF));}
    lcs::sa_script_marker_render_bound(false,false);
}
lcs::ScriptMarkerPool pool(){
    lcs::ScriptMarkerPool p;lcs::ScriptMarkerCommand c;auto& r=c.placement;
    r.identity=100;r.type=5;r.position={200,0,20};r.size=7;r.vertical_size=100;r.pulse_period=128;r.colour={10,20,30,255};
    check(p.submit(c,{}),"Source pool fixture accepted without near-ground query");return p;
}
}
namespace lcs {
bool sa_executor_profile_matches(){return valid_profile;}
namespace marker_rw {
bool profile(){return valid_profile;}
uintptr_t address(uintptr_t value){
    switch(value){case 0xC97B24:return reinterpret_cast<uintptr_t>(&engine);case 0xC88694:return reinterpret_cast<uintptr_t>(colours);
    case 0xC886E8:return reinterpret_cast<uintptr_t>(&ambient_pointer);case 0xC886EC:return reinterpret_cast<uintptr_t>(&directional_pointer);
    case 0x725067:return reinterpret_cast<uintptr_t>(frame_call);case 0x723391:return reinterpret_cast<uintptr_t>(shutdown_call);
    case 0xB7CB84:return reinterpret_cast<uintptr_t>(&native_time);
    default:throw std::runtime_error("Unexpected native data access");}
}
void* load_dictionary(const char*){return &dictionary_token;}
void* current_dictionary(){return current;}
void set_dictionary(void* p){current=p;}
void* load_clump(const char*){check(current==&dictionary_token,"Clump textures resolved in private dictionary");auto* result=&meshes[loaded++];if(steal_dictionary)current=&stolen_token;return result;}
void visit(void* clump,void*(__cdecl* fn)(void*,void*),void* data){fn(static_cast<FakeMesh*>(clump)->source,data);}
void* clone(void* p){for(auto& mesh:meshes)if(p==mesh.source)return mesh.atomic;return nullptr;}
void* frame_create(){return meshes[loaded-1].frame;}
void frame_set(void* atomic,void* frame){put(atomic,4,frame);}
void atomic_destroy(void*){++destroyed;}void frame_destroy(void*){++destroyed;}void clump_destroy(void*){++destroyed;}void dictionary_destroy(void*){++destroyed;}
void matrix_update(void*){++updates;}void frame_update(void*){++updates;}
void bright(){std::memset(colours,0x64,sizeof(colours));std::memset(ambient+0x18,0x75,16);std::memset(directional+0x18,0x86,16);ambient[3]=directional[3]=0;}
std::array<float,3> centre(){++centre_calls;return {0,0,0};}
bool collision(const std::array<float,3>&){++collision_calls;return collision_loaded;}
float ground(float x,float y){check(x==10 && y==20,"Ground receives original XY");return 88.f;}
std::optional<float> floor(const std::array<float,3>& p){++floor_calls;check(p[0]==10 && p[1]==20 && p[2]==20,"Real floor path receives original placement");return 42.f;}
}
}
int main(int argc,char** argv){try{
    check(argc==2,"Pass isolated scratch directory");const auto directory=std::filesystem::path(argv[1]);
    std::filesystem::create_directories(directory/"rw-assets");std::filesystem::create_directories(directory/"texture-assets");
    for(const auto* name:{"texture-assets/lcs_markers.txd","rw-assets/lcs_marker1.dff","rw-assets/lcs_marker2.dff","rw-assets/lcs_marker5.dff"})std::ofstream(directory/name)<<"isolated native-loader mock fixture; not a game resource";
    reset();check(!lcs::sa_script_marker_resources_load(directory) && loaded==0,"Missing actual lifecycle hooks refuses before native allocation");
    bind();valid_profile=false;
    check(!lcs::sa_script_marker_resources_load(directory) && loaded==0,"Unsupported native code refuses before allocation");
    valid_profile=true;check(lcs::sa_script_marker_resources_load(directory) && loaded==3 && current==&foreign_token,"Owned resources loaded and previous dictionary restored");
    check(!lcs::sa_script_marker_resources_load(directory/"another-campaign") && loaded==3,"Different campaign cannot silently borrow prior resources");
    check(lcs::sa_script_marker_ground(10,20)==88.f,"Source ground sentinel uses actual native query output");
    lcs::ScriptMarkerCommand near_marker;near_marker.placement.identity=123;near_marker.placement.type=5;near_marker.placement.position={10,20,20};near_marker.placement.size=7;near_marker.placement.vertical_size=100;near_marker.placement.pulse_period=128;
    lcs::ScriptMarkerPool native_pool;
    collision_loaded=false;check(lcs::sa_script_marker_submit(native_pool,near_marker,1) && collision_calls==1 && floor_calls==0 && native_pool.entries()[0].find_z,"Actual unloaded collision result retains retry instead of fabricated readiness");
    native_pool.frame([](size_t,const auto&){return true;});collision_loaded=true;
    check(lcs::sa_script_marker_submit(native_pool,near_marker,.5f) && centre_calls==2 && floor_calls==1 && native_pool.entries()[0].matrix[11]==41.65f && !native_pool.entries()[0].find_z,"Actual player/clock/collision/floor consumers complete source placement");
    bool wrong_thread=true;std::thread other([&]{wrong_thread=lcs::sa_script_marker_render_ready();});other.join();check(!wrong_thread,"Foreign thread cannot use owned native context");
    uint8_t saved_ambient[64],saved_directional[64],saved_colours[32];uint32_t saved_states[32];
    std::memcpy(saved_ambient,ambient,64);std::memcpy(saved_directional,directional,64);std::memcpy(saved_colours,colours,32);std::memcpy(saved_states,render_states,sizeof(saved_states));
    auto p=pool();check(lcs::sa_script_marker_frame(p) && draws==1 && updates==2,"Source pool reaches actual native atomic callback once");
    check(!std::memcmp(ambient,saved_ambient,64) && !std::memcmp(directional,saved_directional,64) && !std::memcmp(colours,saved_colours,32) && !std::memcmp(render_states,saved_states,sizeof(saved_states)),"Lights/alpha/flags, global colours and render states restored around callback");
    check(lcs::field<uint32_t>(meshes[2].material,4)==0xFFFFFFFF,"Private material colour restored");
    check(lcs::sa_script_marker_resources_release() && destroyed==10 && current==&foreign_token,"Shutdown destroys only3owned atomics/frames/clumps and dictionary");
    reset();bind();check(lcs::sa_script_marker_resources_load(directory),"Reload native fixture");
    p=pool();raise_draw=true;check(!lcs::sa_script_marker_frame(p),"Entered native draw exception observable");
    check(!std::memcmp(ambient,saved_ambient,64) && !std::memcmp(render_states,saved_states,sizeof(saved_states)),"Native exception still restores global graphics state");
    const auto entered_draws=draws;check(!lcs::sa_script_marker_frame(p) && draws==entered_draws && !lcs::sa_script_marker_resources_release() && destroyed==0,"Entered fault quarantined without replay or unsafe destruction");
    reset();bind();steal_dictionary=true;
    check(!lcs::sa_script_marker_resources_load(directory) && current==&stolen_token && destroyed==0,"Foreign dictionary replacement preserved and partial resources quarantined");
    reset();bind();check(lcs::sa_script_marker_resources_load(directory),"Hook replacement fixture loaded");
    frame_call[1]^=1;
    check(!lcs::sa_script_marker_render_ready() && !lcs::sa_script_marker_resources_release() && destroyed==0,"Lost actual frame callback quarantines lifetime; bound flag alone cannot grant ownership");
    std::cout<<"PASS: owned native marker loading/draw/scaling/colour/state/shutdown and native-fault quarantine; isolated engine mocks, no game or graphics device executed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
