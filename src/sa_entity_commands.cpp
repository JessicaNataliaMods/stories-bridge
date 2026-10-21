#include <windows.h>
#include "lcs/sa_executor.hpp"
#include "lcs/route_vehicle_native.hpp"
#include "lcs/entity_commands.hpp"
#include "lcs/native_memory.hpp"
#include "lcs/sa_task_memory.hpp"
#include "lcs/locomotion_groups.hpp"
#include "lcs/wander_path.hpp"
#include "lcs/sa_script.hpp"
#include "lcs/cop_native_translation.hpp"
#include "lcs/random_char_policy.hpp"
#include "lcs/owned_route_task.hpp"
#include "sa_remote_door_policy.hpp"
#include <cmath>
#include <cstring>
#include <map>
#include <sstream>

namespace lcs {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
float native_collective_ground(float,float);
void native_override_restart(const std::array<float,3>&,float);
void native_failed_critical(uintptr_t);
void native_collective_query(const std::array<float,3>&,float,bool,int16_t*,int16_t,uintptr_t*,bool,bool,bool,bool,bool);
uintptr_t native_route_allocate();
void native_cop_mission_creation(uintptr_t ped);
void native_cop_cleanup(int32_t handle);
uint32_t native_phone_random_sample();
bool native_phone_tls_ready();
void native_phone_copy_matrix(uintptr_t matrix,uintptr_t rw_matrix);
void native_phone_update_frame(uintptr_t entity);
bool native_route_effect_is_on_screen(uintptr_t entity);
uintptr_t native_route_effect_get_pad(int32_t selector);
void native_route_construct(uintptr_t task,int32_t move,const std::array<float,3>& target,float radius);
void native_route_target(uintptr_t task,const std::array<float,3>& target,float radius,bool force);
void native_route_assign(SaScript* script,int32_t actor,uintptr_t task);
void native_route_discard(uintptr_t task);
void native_clear_object_rw(uintptr_t entity);
void native_clear_object_world(uintptr_t entity);
void native_clear_object_delete(uintptr_t entity,bool dummy);
void native_blow_up_rc(uintptr_t player);
void native_create_remote_car(std::array<float,3> position,float rotation,int16_t model);
void native_remote_camera(uintptr_t car,uint16_t mode,uint16_t transition);
void native_remote_zoom(int16_t mode);
int32_t native_remote_appearance(uintptr_t car);
void native_remote_cleanup(int32_t handle);
void native_cancel_remote_release(bool explode);
void native_cancel_camera_restore();
void native_remove_boot_visual(uintptr_t car);
void native_attach_object(uintptr_t object,uintptr_t parent,std::array<float,3> offset);
void native_heli_goto(uintptr_t heli,std::array<float,3> position,float min_altitude,float max_altitude);
void native_cleanup_reference(uintptr_t entity,uintptr_t* slot);
void native_register_reference(uintptr_t entity,uintptr_t* slot);
uintptr_t native_attach_character(uintptr_t ped,uintptr_t object,std::array<float,3> offset,uint16_t facing,float angle,int32_t weapon);
void native_wreck_bumper(uintptr_t car,int32_t panel);
void native_wreck_door(uintptr_t car,int32_t door);
bool native_wander_los(const WanderPoint& from,const WanderPoint& to);
WanderHeight native_wander_water(const WanderPoint& point);
WanderHeight native_wander_vertical(const WanderPoint& point,float end_z);
bool native_roof_found(float x,float y,float z);
void native_apply_turn_force(uintptr_t vehicle,float fx,float fy,float fz,float px,float py,float pz);
#else
static void native_apply_turn_force(uintptr_t vehicle,float fx,float fy,float fz,float px,float py,float pz) {
    // CPhysical::ApplyTurnForce(CVector force, CVector point), thiscall, retn 0x18 (0x542A50).
    reinterpret_cast<void(__thiscall*)(uintptr_t,float,float,float,float,float,float)>(0x542A50)(vehicle,fx,fy,fz,px,py,pz);
}
static bool native_roof_found(float x,float y,float z) {
    // CWorld::FindRoofZFor3DCoord: the 02DD script handler (0x4802D0) calls it with the candidate's position.
    bool found=false;
    reinterpret_cast<float(__cdecl*)(float,float,float,bool*)>(0x569750)(x,y,z,&found);
    return found;
}
static float native_collective_ground(float x,float y){return reinterpret_cast<float(__cdecl*)(float,float)>(0x569660)(x,y);}
static void native_override_restart(const std::array<float,3>& position,float heading){
    reinterpret_cast<void(__cdecl*)(const std::array<float,3>&,float)>(0x4607D0)(position,heading);
}
static void native_failed_critical(uintptr_t player){reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x56E830)(player);}
static void native_collective_query(const std::array<float,3>& point,float radius,bool two_d,int16_t* count,int16_t limit,uintptr_t* entities,bool buildings,bool vehicles,bool peds,bool objects,bool dummies){
    using Query=void(__cdecl*)(const std::array<float,3>&,float,bool,int16_t*,int16_t,uintptr_t*,bool,bool,bool,bool,bool);
    reinterpret_cast<Query>(0x564A20)(point,radius,two_d,count,limit,entities,buildings,vehicles,peds,objects,dummies);
}
static void native_phone_copy_matrix(uintptr_t matrix,uintptr_t target) {
    reinterpret_cast<void(__thiscall*)(uintptr_t,uintptr_t)>(0x59AD70)(matrix,target);
}
static void native_phone_update_frame(uintptr_t entity) {
    reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x532B00)(entity);
}
static uintptr_t native_route_effect_get_pad(int32_t selector) {
    return reinterpret_cast<uintptr_t(__cdecl*)(int32_t)>(0x53FB70)(selector);
}
static uint32_t native_phone_random_sample() {
    return reinterpret_cast<uint32_t(__cdecl*)()>(0x821B1E)();
}
static void native_cop_cleanup(int32_t handle) {
    reinterpret_cast<void(__thiscall*)(uintptr_t,int32_t,int32_t)>(0x4637E0)(0xA90850,handle,2);
}
static void native_cop_mission_creation(uintptr_t ped) {
    reinterpret_cast<void(__thiscall*)(uintptr_t,uint8_t)>(0x5E47E0)(ped,2);
}
static void native_clear_object_rw(uintptr_t entity) {
    reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x534030)(entity);
}
static void native_clear_object_world(uintptr_t entity) {
    reinterpret_cast<void(__cdecl*)(uintptr_t)>(0x563280)(entity);
}
static void native_clear_object_delete(uintptr_t entity,bool dummy) {
    reinterpret_cast<void(__thiscall*)(uintptr_t,uint32_t)>(dummy?0x59EB50:0x5A2BD0)(entity,1);
}
static uintptr_t native_route_allocate() {
    return reinterpret_cast<uintptr_t(__cdecl*)(uint32_t)>(0x61A5A0)(0x28);
}
static void native_route_construct(uintptr_t task,int32_t move,const std::array<float,3>& target,float radius) {
    reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t,int32_t,const std::array<float,3>&,float,float,bool,bool)>(0x668120)(task,move,target,radius,0.f,false,false);
}
static void native_route_target(uintptr_t task,const std::array<float,3>& target,float radius,bool force) {
    reinterpret_cast<void(__thiscall*)(uintptr_t,const std::array<float,3>&,float,float,bool)>(0x46FE60)(task,target,radius,0.f,force);
}
static void native_route_discard(uintptr_t task) {
    // Reviewed scalar deleting destructor, not an arbitrary task vtable call.
    reinterpret_cast<void(__thiscall*)(uintptr_t,uint32_t)>(0x66DB80)(task,1);
}
static void native_route_assign(SaScript* script,int32_t actor,uintptr_t task) {
    reinterpret_cast<void(__thiscall*)(SaScript*,int32_t,uintptr_t,int32_t)>(0x465C20)(script,actor,task,0x05D3);
}
static void native_blow_up_rc(uintptr_t player) {
    // 0409 is a native release NOP. Call the real PlayerInfo service instead.
    reinterpret_cast<void(__thiscall*)(uintptr_t,bool)>(0x56EA30)(player,true);
}
static void native_create_remote_car(std::array<float,3> position,float rotation,int16_t model) {
    reinterpret_cast<void(__cdecl*)(std::array<float,3>,float,int16_t)>(0x45AB10)(position,rotation,model);
}
static void native_remote_camera(uintptr_t car,uint16_t mode,uint16_t transition) {
    reinterpret_cast<void(__thiscall*)(uintptr_t,uintptr_t,uint16_t,uint16_t,int32_t)>(0x50C7C0)(0xB6F028,car,mode,transition,1);
}
static void native_remote_zoom(int16_t mode) {
    reinterpret_cast<void(__thiscall*)(uintptr_t,int16_t)>(0x50C1B0)(0xB6F028,mode);
}
static int32_t native_remote_appearance(uintptr_t car) {
    return reinterpret_cast<int32_t(__thiscall*)(uintptr_t)>(0x6D1080)(car);
}
static void native_remote_cleanup(int32_t handle) {
    reinterpret_cast<void(__thiscall*)(uintptr_t,int32_t,int32_t)>(0x4654B0)(0xA90850,handle,1);
}
static void native_cancel_remote_release(bool explode) {
    // CRemote::TakeRemoteControlledCarFromPlayer, cdecl; never the empty 0409.
    reinterpret_cast<void(__cdecl*)(bool)>(0x45AE80)(explode);
}
static void native_cancel_camera_restore() {
    reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x50B930)(0xB6F028);
}
static void native_remove_boot_visual(uintptr_t car) {
    reinterpret_cast<void(__thiscall*)(uintptr_t,int32_t,bool)>(0x6B1600)(car,1,true);
}
static void native_attach_object(uintptr_t object,uintptr_t parent,std::array<float,3> offset) {
    reinterpret_cast<void(__thiscall*)(uintptr_t,uintptr_t,std::array<float,3>,std::array<float,3>)>(0x54D570)(object,parent,offset,{{0,0,0}});
}
static void native_heli_goto(uintptr_t heli,std::array<float,3> position,float min_altitude,float max_altitude) {
    reinterpret_cast<void(__thiscall*)(uintptr_t,float,float,float,float,float)>(0x6A2390)(heli,
        position[0],position[1],position[2],min_altitude,max_altitude);
}
static void native_cleanup_reference(uintptr_t entity,uintptr_t* slot) {
    reinterpret_cast<void(__thiscall*)(uintptr_t,uintptr_t*)>(0x571A00)(entity,slot);
}
static void native_register_reference(uintptr_t entity,uintptr_t* slot) {
    reinterpret_cast<void(__thiscall*)(uintptr_t,uintptr_t*)>(0x571B70)(entity,slot);
}
static uintptr_t native_attach_character(uintptr_t ped,uintptr_t object,std::array<float,3> offset,uint16_t facing,float angle,int32_t weapon) {
    return reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t,uintptr_t,std::array<float,3>,uint16_t,float,int32_t)>(0x5E7CB0)(ped,object,offset,facing,angle,weapon);
}
static void native_wreck_bumper(uintptr_t car,int32_t panel) {
    reinterpret_cast<void(__thiscall*)(uintptr_t,int32_t,bool)>(0x6B1350)(car,panel,false);
}
static void native_wreck_door(uintptr_t car,int32_t door) {
    reinterpret_cast<void(__thiscall*)(uintptr_t,int32_t,bool)>(0x6B1600)(car,door,false);
}
static bool native_wander_los(const WanderPoint& from,const WanderPoint& to) {
    return reinterpret_cast<bool(__cdecl*)(const WanderPoint&,const WanderPoint&,bool,bool,bool,bool,bool,bool,bool)>(0x56A490)(
        from,to,true,false,false,false,false,false,false);
}
static WanderHeight native_wander_water(const WanderPoint& point) {
    float height=0;
    const bool found=reinterpret_cast<bool(__cdecl*)(float,float,float,float*,uint8_t,void*)>(0x6EB690)(
        point[0],point[1],point[2],&height,0,nullptr);
    return {true,found?std::optional<float>(height):std::nullopt};
}
static WanderHeight native_wander_vertical(const WanderPoint& point,float end_z) {
    // Native CColPoint is 0x2C bytes; point XYZ starts at offset zero.
    alignas(4) std::array<uint8_t,0x2C> collision{};uintptr_t entity=0;
    const bool found=reinterpret_cast<bool(__cdecl*)(const WanderPoint&,float,void*,uintptr_t*,bool,bool,bool,bool,bool,bool,void*)>(0x5674E0)(
        point,end_z,collision.data(),&entity,true,false,false,false,false,false,nullptr);
    float height=0;std::memcpy(&height,collision.data()+8,4);
    return {true,found?std::optional<float>(height):std::nullopt};
}
#endif
namespace {
struct WanderQueries {
    bool* entered;
    std::optional<bool> building_los(const WanderPoint& from,const WanderPoint& to) {
        *entered=true;return native_wander_los(from,to);
    }
    WanderHeight water_level(const WanderPoint& point) {*entered=true;return native_wander_water(point);}
    WanderHeight building_vertical(const WanderPoint& point,float end_z) {*entered=true;return native_wander_vertical(point,end_z);}
};
template<size_t N> bool profile(uintptr_t address,const uint8_t (&bytes)[N]) {
    return !std::memcmp(reinterpret_cast<const void*>(native_entity_address(address)),bytes,N);
}
bool ped_pool_profile() {
    const uint8_t bytes[]={0x8B,0x4C,0x24,0x04,0x8B,0x15,0x90,0x44,0xB7,0,0x53,0x56,0x8B,0x72,0x04,0x8B,0xC1,0xC1,0xF8,0x08,0x8A,0x1C,0x30,0x5E,0x3A,0xD9,0x5B,0x75,0x0B,0x8B,0x0A,0x69,0xC0,0xC4,0x07,0,0};
    return profile(0x54FF90,bytes);
}
bool entity_profile(bool objects) {
    const uint8_t vehicle[]={0x8B,0x4C,0x24,0x04,0x8B,0x15,0x94,0x44,0xB7,0x00,0x53,0x56,0x8B,0x72,0x04,0x8B,0xC1,0xC1,0xF8,0x08,0x8A,0x1C,0x30,0x5E,0x3A,0xD9,0x5B,0x75,0x0B,0x8B,0x0A,0x69,0xC0,0x18,0x0A,0,0};
    const uint8_t object[]={0x8B,0x4C,0x24,0x04,0x8B,0x15,0x9C,0x44,0xB7,0x00,0x53,0x56,0x8B,0x72,0x04,0x8B,0xC1,0xC1,0xF8,0x08,0x8A,0x1C,0x30,0x5E,0x3A,0xD9,0x5B,0x75,0x0B,0x8B,0x0A,0x69,0xC0,0x9C,0x01,0,0};
    return std::memcmp(reinterpret_cast<const void*>(native_entity_address(objects?0x550050:0x54FFF0)),objects?object:vehicle,sizeof(vehicle))==0;
}
uintptr_t entity_at(uintptr_t pool,int32_t handle,size_t stride,uint8_t type) {
    if(!pool)return 0;
    const auto data=*reinterpret_cast<const uintptr_t*>(pool),flags=*reinterpret_cast<const uintptr_t*>(pool+4);
    const auto count=*reinterpret_cast<const int32_t*>(pool+8);
    if(!data || !flags || count>0x7FFFFF || !valid_entity_slot(handle,count,reinterpret_cast<const uint8_t*>(flags)))return 0;
    const auto offset=uintptr_t(uint32_t(handle)>>8)*stride;
    if(offset/stride!=(uint32_t(handle)>>8) || data+offset<data)return 0;
    const auto entity=data+offset;
    return (*reinterpret_cast<const uint8_t*>(entity+0x36)&7)==type?entity:0;
}
// Prove a reference belongs to a live pool allocation before dereferencing it.
// An old native target may be a ped, car or object; never assume its type from
// the new SCM command or call a virtual/reference method on an arbitrary ptr.
uintptr_t physical_pointer(uintptr_t pointer) {
    if(!pointer)return 0;
    constexpr uintptr_t globals[]={0xB74494,0xB7449C,0xB74490};
    constexpr size_t strides[]={0xA18,0x19C,0x7C4};
    constexpr uint8_t types[]={2,4,3};
    for(size_t n=0;n<3;++n) {
        if(n==2?!ped_pool_profile():!entity_profile(n==1))continue;
        const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(globals[n]));if(!pool)continue;
        const auto first=*reinterpret_cast<const uintptr_t*>(pool),flags=*reinterpret_cast<const uintptr_t*>(pool+4);
        const auto count=*reinterpret_cast<const int32_t*>(pool+8);
        if(!first || !flags || count<=0 || count>0x7FFFFF || pointer<first)continue;
        const auto delta=pointer-first,slot=delta/strides[n];
        if(delta%strides[n] || slot>=uint32_t(count))continue;
        const auto generation=*reinterpret_cast<const uint8_t*>(flags+slot);
        if(entity_at(pool,int32_t((uint32_t(slot)<<8)|generation),strides[n],types[n])==pointer)return pointer;
    }
    return 0;
}
bool reference_profile() {
    const uint8_t cleanup[]={0x8B,0x41,0x24,0x83,0xC1,0x24,0x85,0xC0,0x74,0x2E,0x8B,0x54,0x24,0x04};
    const uint8_t register_ref[]={0x55,0x8B,0xE9,0x8A,0x45,0x36,0x24,0x07,0x3C,0x01,0x75,0x14,0xF7,0x45,0x1C,0x00,0x00,0x40,0x04,0x75,0x0B,0x8A,0x45,0x2E,0x84,0xC0};
    return profile(0x571A00,cleanup) && profile(0x571B70,register_ref);
}
bool orientation_profile() {
    const uint8_t rotate[]={0x83,0xEC,0x0C,0x56,0x8B,0xF1,0x8B,0x4E,0x14,0x85,0xC9,0x74,0x51};
    const uint8_t get_matrix[]={0x56,0x8B,0xF1,0x8B,0x46,0x14,0x85,0xC0,0x75,0x11,0xE8,0xC1,0xDB,0x13,0,0x8B,0x46,0x14,0x50,0x8D,0x4E,0x04,0xE8,0x05,0xD8,0x13,0,0x8B,0x46,0x14,0x5E,0xC3};
    return !std::memcmp(reinterpret_cast<const void*>(native_entity_address(0x439A80)),rotate,sizeof(rotate)) &&
        !std::memcmp(reinterpret_cast<const void*>(native_entity_address(0x411990)),get_matrix,sizeof(get_matrix));
}
bool appearance_profile() {
    const uint8_t appearance[]={0x8B,0x81,0x84,0x03,0,0,0x8B,0x80,0xCC,0,0,0};
    return !std::memcmp(reinterpret_cast<const void*>(native_entity_address(0x6D1080)),appearance,sizeof(appearance));
}
struct EntityReceipt {bool ok=false,entered=false,condition=false;std::array<int32_t,32> values{};DWORD exception=0;uint64_t route_epoch=0;};
void invoke_route_motion(const RouteMotionRequest* request,EntityReceipt* receipt) {
    __try {
        if(request->track_owned_lifetime && !request->authorized)return;
        const uint8_t allocation[]={0x8B,0x0D,0xA8,0x44,0xB7,0,0xE9,0x55,0xFF,0xFF,0xFF};
        const uint8_t construction[]={0x53,0x55,0x56,0x57,0x8B,0xF1,0xE8,0x85,0x22,0xFB,0xFF,0x8A,0x5C,0x24,0x28};
        const uint8_t assignment[]={0x64,0xA1,0,0,0,0,0x6A,0xFF,0x68,0x18,0x7D,0x83,0};
        const uint8_t target_update[]={0x8A,0x44,0x24,0x10,0x84,0xC0,0x8B,0x54,0x24,0x04,0x75,0x40,0xD9,0x41,0x10,0xD8,0x1A,0xDF,0xE0};
        if(!ped_pool_profile() || !sa_task_allocator_profile_matches() || !profile(0x668120,construction) || !profile(0x465C20,assignment) || !profile(0x46FE60,target_update))return;
        const auto ped=entity_at(*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74490)),request->actor.handle,0x7C4,3);
        if(!ped || ped!=request->actor.native_ped || !*reinterpret_cast<const uintptr_t*>(ped+0x47C))return;
        const auto health=*reinterpret_cast<const float*>(ped+0x540);
        const auto state=*reinterpret_cast<const int32_t*>(ped+0x530);
        // Never turn source foot seek into native auto-exit-car, nor allocate
        // new route movement while airborne/landing/dead/arrested. Source
        // objective ownership/control remains the controller's prerequisite.
        if((*reinterpret_cast<const uint8_t*>(ped+0x46D)&7) || !std::isfinite(health) || health<=0 ||
           state==11 || state==54 || state==55 || state==63)return;
        // GivePedScriptedTask silently deletes a non-brain caller's task for
        // these peds. Refuse before allocation instead of claiming movement.
        if(*reinterpret_cast<const uint32_t*>(ped+0x474)&0x800000u)return;
        const uint8_t discard[]={0x56,0x8B,0xF1,0xE8,0xB8,0xA6,0xFF,0xFF,0xF6,0x44,0x24,0x08,0x01,0x74,0x09,0x56,0xE8,0x1B,0xCA,0xFA,0xFF,0x83,0xC4,0x04,0x8B,0xC6,0x5E,0xC2,0x04,0x00};
        if(request->authorized && !profile(0x66DB80,discard))return;
        if(request->authorized && !request->authorized(request->authorization_context))return;
        SaScript script(nullptr,request->mission_cleanup); // private C6; DC stays false
        receipt->entered=true;
        const auto task=native_route_allocate();if(!task)return;
        native_route_construct(task,request->move_state,request->target,0.8f);
        // The retail constructor can retain a previous radius when this pool
        // slot already contains an identical target. Force the real setter on
        // OUR new unassigned task before any engine/group ownership transfer.
        native_route_target(task,request->target,0.8f,true);
        // Allocation/construction/target calls may execute another observer or
        // mod. Never overwrite the replacement it installed in that interval.
        if(request->authorized && !request->authorized(request->authorization_context)) {
            // Still private/unassigned: delete only the exact reviewed class
            // with no parent or subtask. An unexpected shape/ABI stays refused
            // and is not passed through a foreign virtual destructor.
            if(profile(0x66DB80,discard) && *reinterpret_cast<const uint32_t*>(task)==0x86FDA8 &&
               !*reinterpret_cast<const uint32_t*>(task+4) && !*reinterpret_cast<const uint32_t*>(task+8))native_route_discard(task);
            return;
        }
        if(request->track_owned_lifetime)receipt->route_epoch=sa_track_private_route_task(task,request->actor);
        if(request->track_owned_lifetime && !receipt->route_epoch) {
            if(profile(0x66DB80,discard) && *reinterpret_cast<const uint32_t*>(task)==0x86FDA8 &&
               !*reinterpret_cast<const uint32_t*>(task+4) && !*reinterpret_cast<const uint32_t*>(task+8))native_route_discard(task);
            return;
        }
        // Tracking allocates/pins metadata. A mod/allocator callback there must
        // not let a now-stale source plan cross the actual assignment boundary.
        if(request->track_owned_lifetime && !request->authorized(request->authorization_context)) {
            sa_discard_private_route_task(task,request->actor,receipt->route_epoch);return;
        }
        native_route_assign(&script,request->actor.handle,task);
        receipt->ok=true;
    } __except(EXCEPTION_EXECUTE_HANDLER){receipt->exception=GetExceptionCode();}
}
uintptr_t player_ped_at(uintptr_t player) {
    const uint8_t ped_profile[]={0x8B,0x4C,0x24,0x04,0x8B,0x15,0x90,0x44,0xB7,0,0x53,0x56,0x8B,0x72,0x04,0x8B,0xC1,0xC1,0xF8,0x08,0x8A,0x1C,0x30,0x5E,0x3A,0xD9,0x5B,0x75,0x0B,0x8B,0x0A,0x69,0xC0,0xC4,0x07,0,0};
    if(std::memcmp(reinterpret_cast<const void*>(native_entity_address(0x54FF90)),ped_profile,sizeof(ped_profile)))return 0;
    const auto ped=*reinterpret_cast<const uintptr_t*>(player),pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74490));if(!ped || !pool)return 0;
    const auto first=*reinterpret_cast<const uintptr_t*>(pool),flags=*reinterpret_cast<const uintptr_t*>(pool+4);
    const auto count=*reinterpret_cast<const int32_t*>(pool+8);if(!first || !flags || count<=0 || count>0x7FFFFF || ped<first)return 0;
    const auto delta=ped-first,slot=delta/0x7C4;if(delta%0x7C4 || slot>=uint32_t(count))return 0;
    const auto generation=*reinterpret_cast<const uint8_t*>(flags+slot);
    const auto handle=int32_t((uint32_t(slot)<<8)|generation);
    return entity_at(pool,handle,0x7C4,3)==ped && *reinterpret_cast<const uintptr_t*>(ped+0x480)?ped:0;
}
// Read native objective-task presence, not health or task disappearance. No
// virtual call through a mod's vtable is made by this inspection.
int leaving_vehicle_to_die(uintptr_t ped) {
    const uint8_t primary_count[]={0x33,0xC0,0x83,0x3C,0x81,0x00,0x75,0x09,0x40,0x83,0xF8,0x05};
    const uint8_t allocator[]={0x8B,0x0D,0xA8,0x44,0xB7,0,0xE9,0x55,0xFF,0xFF,0xFF};
    const uint8_t stride[]={0xC1,0xE0,0x07};
    const uint8_t die_type[]={0xB8,0xC1,0x02,0,0,0xC3};
    if(!profile(0x681720,primary_count) || !sa_task_allocator_profile_matches() || !profile(0x61A55A,stride) || !profile(0x62F240,die_type))return -1;
    const auto intelligence=*reinterpret_cast<const uintptr_t*>(ped+0x47C);
    if(!intelligence)return -1;
    bool unknown=false;
    // Inspect all five primary trees: an objective suspended by a physical
    // response still exists. Secondary facial/IK/attack tasks are not goals.
    for(size_t slot=0;slot<5;++slot) {
        auto task=*reinterpret_cast<const uintptr_t*>(intelligence+4+slot*4);
        uintptr_t parent=0;
        size_t depth=0;
        for(;task && depth<32;++depth) {
            if(!sa_task_slot_live(task) ||
               *reinterpret_cast<const uintptr_t*>(task+4)!=parent){unknown=true;break;}
            const auto table=*reinterpret_cast<const uintptr_t*>(task);
            if(!table){unknown=true;break;}
            const auto getter=*reinterpret_cast<const uintptr_t*>(table+16);
            const auto* code=reinterpret_cast<const uint8_t*>(native_entity_address(getter));
            if(code[0]!=0xB8 || code[5]!=0xC3){unknown=true;break;}
            uint32_t type=0;std::memcpy(&type,code+1,4);
            if(type==705)return 1; // TASK_COMPLEX_LEAVE_CAR_AND_DIE
            const auto child_getter=*reinterpret_cast<const uintptr_t*>(table+8);
            const uint8_t complex[]={0x8B,0x41,0x08,0xC3},simple[]={0x33,0xC0,0xC3};
            if(profile(child_getter,simple)){task=0;break;}
            if(!profile(child_getter,complex)){unknown=true;break;}
            parent=task;task=*reinterpret_cast<const uintptr_t*>(task+8);
        }
        if(task && depth==32)unknown=true;
    }
    return unknown?-1:0;
}
bool clear_object_profiles() {
    const uint8_t rw[]={0x56,0x8b,0xf1,0x57,0x8b,0x7e,0x18,0x85,0xff,0xf,0x84,0xd4,0x0,0x0,0x0,0x8a,0x7,0x3c,0x1,0x75};
    const uint8_t world[]={0x56,0x8b,0x74,0x24,0x8,0x8b,0x6,0x8b,0xce,0xff,0x50,0xc,0x8a,0x46,0x36,0x24,0x7,0x3c,0x1,0x76,0xc,0x3c,0x5,0x73,0x8,0x8b,0xce,0x5e,0xe9,0xbf,0xf5,0xfd,0xff,0x5e,0xc3};
    const uint8_t object_delete[]={0x56,0x57,0x8b,0xf9,0xe8,0x87,0xca,0xff,0xff,0xf6,0x44,0x24,0xc,0x1};
    const uint8_t dummy_delete[]={0x56,0x8b,0xf1,0xe8,0x58,0xff,0xff,0xff,0xf6,0x44,0x24,0x8,0x1,0x74,0x9};
    const uint8_t object_table[]={0xd0,0x2b,0x5a,0x0,0xd0,0x47,0x53,0x0,0x30,0x4a,0x54,0x0,0xc0,0x24,0x54,0x0,0x60,0x7,0x5a,0x0,0xe0,0x2a,0x53,0x0,0x0,0x37,0x53,0x0,0x10,0xf1,0x59,0x0,0x30,0x40,0x53,0x0,0xb0,0x49,0x54,0x0};
    const uint8_t dummy_table[]={0x50,0xeb,0x59,0x0,0xd0,0x47,0x53,0x0,0x20,0x30,0x53,0x0,0xe0,0x4a,0x53,0x0,0x20,0x3e,0x40,0x0,0xe0,0x2a,0x53,0x0,0x0,0x37,0x53,0x0,0x30,0x3d,0x53,0x0,0x30,0x40,0x53,0x0,0x20,0x41,0x53,0x0};
    const uint8_t deletable[]={0xf,0xb6,0x81,0x3c,0x1,0x0,0x0,0x48,0x83,0xf8,0x5,0x77,0xa,0xff,0x24,0x85,0x3c,0xf1,0x59,0x0,0x32,0xc0,0xc3,0xb0,0x1,0xc3,0x8b,0xff,0x37,0xf1,0x59};
    const uint8_t dummy_release[]={0x8b,0x4c,0x24,0x4,0x56,0x8b,0x35,0xa0,0x44,0xb7,0x0,0x2b,0xe,0xb8,0x93,0x24,0x49,0x92,0xf7,0xe9};
    return entity_profile(true) && profile(0x534030,rw) && profile(0x563280,world) &&
        profile(0x5A2BD0,object_delete) && profile(0x59EB50,dummy_delete) &&
        profile(0x866F60,object_table) && profile(0x866E78,dummy_table) &&
        profile(0x59F120,deletable) && profile(0x5326F0,dummy_release);
}
struct ClearObjectIdentity {uintptr_t pool=0,data=0,tags=0,entity=0;int32_t slot=0,count=0;uint8_t generation=0;bool dummy=false;uint8_t created=0;};
bool clear_object_alive(const ClearObjectIdentity& id) {
    return clear_object_profiles() && *reinterpret_cast<const uintptr_t*>(id.pool)==id.data && *reinterpret_cast<const uintptr_t*>(id.pool+4)==id.tags &&
        *reinterpret_cast<const int32_t*>(id.pool+8)==id.count && *reinterpret_cast<const uint8_t*>(id.tags+id.slot)==id.generation &&
        !(id.generation&0x80) && *reinterpret_cast<const uintptr_t*>(id.entity)==(id.dummy?0x866E78:0x866F60) &&
        (id.dummy || *reinterpret_cast<const uint8_t*>(id.entity+0x13C)==id.created);
}
int clear_object_candidate(uintptr_t pool,int32_t slot,bool dummy,const std::array<float,6>& box,ClearObjectIdentity* out) {
    const auto data=*reinterpret_cast<const uintptr_t*>(pool),tags=*reinterpret_cast<const uintptr_t*>(pool+4);
    const auto count=*reinterpret_cast<const int32_t*>(pool+8);
    if(!data || !tags || count<0 || count>0x7FFFFF || slot<0 || slot>=count)return -1;
    const auto stride=dummy?uintptr_t(0x38):uintptr_t(0x19C),offset=uintptr_t(slot)*stride;
    if(data>UINTPTR_MAX-offset-stride || tags>UINTPTR_MAX-uint32_t(count))return -1;
    const auto generation=*reinterpret_cast<const uint8_t*>(tags+slot);if(generation&0x80)return 0;
    const auto entity=data+offset;
    if(!dummy) {
        if((*reinterpret_cast<const uint8_t*>(entity+0x36)&7)!=4)return -1;
        // SA mission2 is an additional native protected mission class. Never
        // delete a native mod's mission object while porting the source filter.
        const auto created=*reinterpret_cast<const uint8_t*>(entity+0x13C);
        if(created==2 || created==4 || created==5 || created==6)return 0;
    }
    const auto matrix=*reinterpret_cast<const uintptr_t*>(entity+0x14);
    if(matrix && matrix>UINTPTR_MAX-0x3C)return -1;
    const auto position=reinterpret_cast<const float*>(matrix?matrix+0x30:entity+4);
    const std::array<float,3> point{{position[0],position[1],position[2]}};
    for(float value:point)if(!std::isfinite(value))return -1;
    if(!source_clear_object_contains(box,point))return 0;
    const auto table=*reinterpret_cast<const uintptr_t*>(entity);
    if(table!=(dummy?0x866E78:0x866F60))return -1; // class/ABI unrecognized in the affected area
    *out={pool,data,tags,entity,slot,count,generation,dummy,uint8_t(dummy?0:*reinterpret_cast<const uint8_t*>(entity+0x13C))};return 1;
}
void invoke_clear_objects(const NativeCall* call,EntityReceipt* receipt) {
    __try {
        if(!clear_object_profiles())return;
        const auto box=source_clear_object_box(call->inputs.data());if(!box)return;
        uintptr_t pools[2]{};int32_t counts[2]{};
        for(size_t kind=0;kind<2;++kind) {
            pools[kind]=*reinterpret_cast<const uintptr_t*>(native_entity_address(kind?0xB744A0:0xB7449C));
            if(!pools[kind])return;
            counts[kind]=*reinterpret_cast<const int32_t*>(pools[kind]+8);
            if(counts[kind]<0 || counts[kind]>0x7FFFFF)return;
        }
        // Complete read-only preflight before the first destructive engine
        // call. Iterate objects then dormant objects, descending as retail.
        for(size_t kind=0;kind<2;++kind)for(int32_t slot=counts[kind];slot--;) {
            ClearObjectIdentity id;
            if(clear_object_candidate(pools[kind],slot,kind!=0,*box,&id)<0)return;
        }
        for(size_t kind=0;kind<2;++kind)for(int32_t slot=counts[kind];slot--;) {
            if(*reinterpret_cast<const int32_t*>(pools[kind]+8)!=counts[kind])return;
            ClearObjectIdentity id;const auto candidate=clear_object_candidate(pools[kind],slot,kind!=0,*box,&id);
            if(candidate<0)return;if(!candidate)continue;
            if(!clear_object_alive(id))return;
            receipt->entered=true;native_clear_object_rw(id.entity);
            if(!clear_object_alive(id))return;
            native_clear_object_world(id.entity);
            if(!clear_object_alive(id))return;
            native_clear_object_delete(id.entity,id.dummy);
        }
        receipt->ok=true;
    } __except(EXCEPTION_EXECUTE_HANDLER){receipt->exception=GetExceptionCode();}
}
// Bound vehicle identity: pointer alone cannot show that a pool slot has not
// been recycled while a native service or another mod is running.
struct CreatedRemoteCar {uintptr_t vehicle=0;int32_t handle=-1;};
// Stable native weak-reference cells: engine destruction clears ownership even
// if a pool handle eventually wraps back to the same pointer/generation.
std::array<CreatedRemoteCar,2> created_remote_cars{};
int32_t remote_vehicle_key(uintptr_t car) {
    if(!car || !entity_profile(false))return -1;
    const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74494));
    if(!pool)return -1;
    const auto first=*reinterpret_cast<const uintptr_t*>(pool),tags=*reinterpret_cast<const uintptr_t*>(pool+4);
    const auto count=*reinterpret_cast<const int32_t*>(pool+8);
    if(!first || !tags || count<=0 || count>0x7FFFFF || car<first)return -1;
    const auto delta=car-first,slot=delta/0xA18;
    if(delta%0xA18 || slot>=uint32_t(count))return -1;
    const auto generation=*reinterpret_cast<const uint8_t*>(tags+slot);
    const auto key=int32_t((uint32_t(slot)<<8)|generation);
    return entity_at(pool,key,0xA18,2)==car?key:-1;
}
// CEntity reference list: each node stores the registered slot at +4.
// A focused player's pointer can remain unchanged after its CPed pool slot is
// recycled during a native callback. Pin the full handle across that boundary.
int32_t remote_ped_key(uintptr_t ped) {
    if(!ped || !ped_pool_profile())return -1;
    const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74490));
    if(!pool)return -1;
    const auto first=*reinterpret_cast<const uintptr_t*>(pool),tags=*reinterpret_cast<const uintptr_t*>(pool+4);
    const auto count=*reinterpret_cast<const int32_t*>(pool+8);
    if(!first || !tags || count<=0 || count>0x7FFFFF || ped<first)return -1;
    const auto delta=ped-first,slot=delta/0x7C4;
    if(delta%0x7C4 || slot>=uint32_t(count))return -1;
    const auto generation=*reinterpret_cast<const uint8_t*>(tags+slot);
    const auto key=int32_t((uint32_t(slot)<<8)|generation);
    return entity_at(pool,key,0x7C4,3)==ped?key:-1;
}

// Refusing a missing registration prevents a recycled pool object from being
// mistaken for the former RC merely because its raw address is unchanged.
bool remote_reference_live(uintptr_t car,const uintptr_t* slot) {
    auto node=*reinterpret_cast<const uintptr_t*>(car+0x24);
    for(size_t depth=0;node && depth<64;++depth) {
        if(*reinterpret_cast<const uintptr_t*>(node+4)==reinterpret_cast<uintptr_t>(slot))return true;
        const auto next=*reinterpret_cast<const uintptr_t*>(node);
        if(next==node)return false;
        node=next;
    }
    return false;
}

} // namespace

// One weak cell per native vehicle pool slot. RegisterReference clears the
// cell on deletion, so a reused slot cannot inherit an earlier vehicle's policy.
struct RemoteDoorPolicyVehicle {uintptr_t vehicle=0;int32_t handle=-1;};
namespace {
std::map<uint32_t,RemoteDoorPolicyVehicle> remote_door_policy_vehicles;

RemoteDoorPolicyVehicle* remote_door_policy_entry(uint32_t slot) noexcept {
    try {
        return &remote_door_policy_vehicles.try_emplace(slot).first->second;
    } catch(...) {
        return nullptr;
    }
}
}

bool sa_remote_door_policy_track_vehicle(uintptr_t vehicle,int32_t handle) noexcept {
    __try {
        if(!vehicle || handle<0 || !entity_profile(false) || !reference_profile() ||
           remote_vehicle_key(vehicle)!=handle)return false;
        const auto slot=uint32_t(handle)>>8;
        auto* entry_ptr=remote_door_policy_entry(slot);
        if(!entry_ptr)return false;
        auto& entry=*entry_ptr;
        if(entry.vehicle) {
            return entry.vehicle==vehicle && entry.handle==handle &&
                remote_reference_live(vehicle,&entry.vehicle);
        }
        entry.handle=handle;
        entry.vehicle=vehicle;
        native_register_reference(vehicle,&entry.vehicle);
        if(entry.vehicle!=vehicle || entry.handle!=handle || remote_vehicle_key(vehicle)!=handle ||
           !remote_reference_live(vehicle,&entry.vehicle)) {
            // If registration did not reach the reference list, release only
            // our unregistered cell. An uncertain native cell stays quarantined.
            if(entry.vehicle==vehicle && !remote_reference_live(vehicle,&entry.vehicle)) {
                entry.vehicle=0;entry.handle=-1;
            }
            return false;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool sa_remote_door_policy_vehicle_live(uintptr_t vehicle) noexcept {
    __try {
        if(!vehicle || !entity_profile(false) || !reference_profile())return false;
        const auto handle=remote_vehicle_key(vehicle);if(handle<0)return false;
        const auto slot=uint32_t(handle)>>8;
        const auto found=remote_door_policy_vehicles.find(slot);
        if(found==remote_door_policy_vehicles.end())return false;
        const auto& entry=found->second;
        return entry.vehicle==vehicle && entry.handle==handle &&
            remote_reference_live(vehicle,&entry.vehicle);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool sa_remote_door_policy_runtime_healthy() noexcept {
    __try {
        bool active=false;
        for(const auto& entry:remote_door_policy_vehicles) {
            if(!entry.second.vehicle)continue; // native weak cell retired on deletion
            active=true;
            if(!entity_profile(false) || !reference_profile() || entry.second.handle<0 ||
               remote_vehicle_key(entry.second.vehicle)!=entry.second.handle ||
               !remote_reference_live(entry.second.vehicle,&entry.second.vehicle))return false;
        }
        return !active || sa_remote_door_policy_calls_healthy();
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

namespace {
bool remote_cancel_profile() {
    // All bytes are reviewed from the supplied US PE, including the genuine
    // release helper's mission cleanup/ownership dependencies, reference list
    // and camera Restore. A replaced detour is not this ABI.
    const uint8_t release[]={0x0F,0xB6,0x05,0x74,0xCD,0xB7,0,0x69,0xC0,0x90,0x01,0,0,0x8B,0x88,0x48,0xCE,0xB7,0,0x80,0xB9,0xA4,0x04,0,0,0x02,0x75};
    const uint8_t tail[]={0x0F,0xB6,0x0D,0x74,0xCD,0xB7,0,0x69,0xC9,0x90,0x01,0,0,0x8B,0x81,0x48,0xCE,0xB7,0,0x80,0xA0,0x28,0x04,0,0,0xF7};
    const uint8_t state[]={0x8A,0x4C,0x24,0x04,0x89,0x90,0x78,0xCE,0xB7,0,0xC6,0x80,0x75,0xCE,0xB7,0,0x01,0x88,0x88,0x76,0xCE,0xB7,0,0xC6,0x80,0x77,0xCE,0xB7,0,0x01,0xC3};
    const uint8_t trampoline[]={0xE9,0x5B,0x93,0x10,0x01};
    const uint8_t ownership[]={0x53,0x8B,0x5C,0x24,0x08,0x56,0x8B,0xF1,0x0F,0xB6,0x86,0xA4,0x04,0,0,0x3B,0xC3};
    const uint8_t cleanup[]={0xE9,0x5B,0x93,0x10,0x01};
    const uint8_t cleanup_body[]={0x51,0x55,0x56,0x8B,0xE9,0x57,0x8D,0x7D,0x04,0xC7,0x44,0x24,0x0C,0x4B,0,0,0};
    const uint8_t transition[]={0xE9,0xDB,0x72,0x10,0x01};
    const uint8_t damage[]={0x80,0x8E,0x2A,0x04,0,0,0x20};
    const uint8_t physical[]={0xF6,0xC5,0x01,0x56,0x74,0x1B,0x8B,0x88,0x8C,0x05,0,0};
    const uint8_t restore_call[]={0xB9,0x28,0xF0,0xB6,0,0xE8,0x5F,0xF4,0x08,0,0x32,0xC0};
    const uint8_t restore[]={0x53,0x33,0xDB,0x56,0x8B,0xF1,0x57,0x53,0x6A,0xFF,0xC6,0x46,0x2B,0x01};
    return entity_profile(false) && ped_pool_profile() && reference_profile() &&
      profile(0x1562160,release) && profile(0x15621CA,tail) && profile(0x15621F7,state) &&
      profile(0x45AE80,transition) && profile(0x6D5D70,ownership) &&
      profile(0x4654B0,cleanup) && profile(0x156E810,cleanup_body) &&
      profile(0x48970E,damage) && profile(0x56E044,physical) &&
      profile(0x47C4C7,restore_call) && profile(0x50B930,restore);
}
void invoke_entity(const NativeCall* call,EntityReceipt* receipt) {
    __try {
        const auto action=*call->source_entity_command;
        if(action==EntityCommand::restart_critical_mission) {
            // SA 0255 is unimplemented in retail. Use its actual restart and
            // CPlayerInfo failure consumers, in the same order as LCS Script3.
            const uint8_t failure[]={0x8A,0x81,0xDC,0,0,0,0x84,0xC0,0x75,0x1C,0xB0,0x03,0x88,0x81,0xDC,0,0,0,
                0xA2,0xB0,0xA8,0x96,0,0xA1,0x84,0xCB,0xB7,0,0xA3,0xAC,0xA8,0x96,0,0xE9,0xBA,0xF3,0xEC,0xFF,0xC3};
            const uint8_t override_handler[]={0x6A,0x04,0x8B,0xCE,0xE8,0x19,0x77,0xFE,0xFF};
            const uint8_t override_call[]={0xE8,0xFE,0x3D,0xFE,0xFF};
            const uint8_t ground[]={0x83,0xEC,0x38,0x8B,0x44,0x24,0x3C,0x8B,0x4C,0x24,0x40};
            if(!profile(0x56E830,failure) || !profile(0x47C95E,override_handler) || !profile(0x47C9CD,override_call))return;
            std::array<float,3> position{};
            for(size_t i=0;i<3;++i){position[i]=entity_float(call->inputs[i]);if(!std::isfinite(position[i]))return;}
            const float heading=entity_float(call->inputs[3]);if(!std::isfinite(heading))return;
            const auto focus=*reinterpret_cast<const uint8_t*>(native_entity_address(0xB7CD74));
            if(focus>1)return;
            const auto player=native_entity_address(0xB7CD98)+uintptr_t(focus)*0x190;
            if(*reinterpret_cast<const uint8_t*>(player+0xDC)>4)return;
            if(position[2]<=-100.0f) {
                if(!profile(0x569660,ground))return;
                position[2]=native_collective_ground(position[0],position[1]);
                if(!std::isfinite(position[2]))return;
            }
            receipt->entered=true;
            native_override_restart(position,heading);
            native_failed_critical(player);
            receipt->ok=true;return;
        }
        if(action==EntityCommand::clear_area_objects) {invoke_clear_objects(call,receipt);return;}
        if(action==EntityCommand::create_remote_car) {
            const uint8_t spawn[]={0x6A,0xFF,0x68,0x11,0x7A,0x83,0,0x64,0xA1,0,0,0};
            const uint8_t camera[]={0x56,0x8B,0xF1,0x8B,0x4C,0x24,0x14,0x83,0xF9,0x02,0xB0,0x01};
            const uint8_t ground[]={0x83,0xEC,0x38,0x8B,0x44,0x24,0x3C,0x8B,0x4C,0x24,0x40};
            const uint8_t zoom[]={0x51,0x56,0x8B,0xF1,0x8B,0x8E,0x90,0x03,0,0,0x8A,0x41};
            if(!entity_profile(false) || !ped_pool_profile() || !reference_profile() ||
               !profile(0x45AB10,spawn) || !profile(0x50C7C0,camera) || !profile(0x50C1B0,zoom) || !profile(0x569660,ground))return;
            const auto& input=call->inputs;
            auto position=source_vector(input.data()+1);
            const auto heading=entity_float(input[4]);const auto model=input[5];
            if(!position || !std::isfinite(heading) || model<0 || model>=20000)return;
            const auto info=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xA9B0C8)+model*4);
            // Require the actual streamed model before the constructor can
            // enter. The native RC factory does not request missing resources.
            if(!info || !*reinterpret_cast<const uintptr_t*>(info+0x1C))return;
            const auto focus=*reinterpret_cast<const uint8_t*>(native_entity_address(0xB7CD74));if(focus>1)return;
            const auto player=native_entity_address(0xB7CD98)+focus*0x190;
            const auto ped=player_ped_at(player);if(!ped)return;
            const auto ped_key=remote_ped_key(ped);if(ped_key<0)return;
            auto* slot=reinterpret_cast<uintptr_t*>(player+0xB0);
            auto& owned=created_remote_cars[focus];
            const auto old=*slot;
            // Permit the original repeated-create path only for a source-owned
            // live RC. An unrelated mod's remote controller stays untouched.
            if(owned.vehicle && (remote_vehicle_key(owned.vehicle)!=owned.handle ||
               !remote_reference_live(owned.vehicle,&owned.vehicle)))return;
            if(old && (old!=owned.vehicle || !remote_reference_live(old,slot)))return;
            const auto camera_state=native_entity_address(0xB6F028);
            const auto zoom_ped=*reinterpret_cast<const uint8_t*>(camera_state+0x38);
            const auto zoom_car=*reinterpret_cast<const uint8_t*>(camera_state+0x39);
            const auto zoom_car_value=*reinterpret_cast<const uint32_t*>(camera_state+0xC4);
            const auto zoom_ped_value=*reinterpret_cast<const uint32_t*>(camera_state+0xD8);
            if((*position)[2]<=-100.f) {
                receipt->entered=true;
                (*position)[2]=native_collective_ground((*position)[0],(*position)[1]);
                if(!std::isfinite((*position)[2]))return;
            }
            if(*reinterpret_cast<const uint8_t*>(native_entity_address(0xB7CD74))!=focus ||
               player_ped_at(player)!=ped || remote_ped_key(ped)!=ped_key ||
               *slot!=old)return;
            receipt->entered=true;
            if(owned.vehicle) {
                const auto previous=owned.vehicle;
                native_cleanup_reference(previous,&owned.vehicle);
                if(owned.vehicle!=previous || remote_reference_live(previous,&owned.vehicle))return;
                owned.vehicle=0;owned.handle=-1;
            }
            if(old) {
                native_cleanup_reference(old,slot);
                if(*slot!=old || remote_reference_live(old,slot))return;
                *slot=0;
            }
            // 45AB10 owns construction, ground/base adjustment, road joining,
            // remote status, locking, engine, world insertion, physical-car
            // disable and the focused player's registered entity reference.
            native_create_remote_car(*position,heading*0.017453292519943295f,int16_t(model));
            // The ground-car source branch leaves both script zoom policies
            // untouched. Undo SA's unconditional SetZoomValueCamStringScript(1).
            *reinterpret_cast<uint8_t*>(camera_state+0x38)=zoom_ped;
            *reinterpret_cast<uint8_t*>(camera_state+0x39)=zoom_car;
            *reinterpret_cast<uint32_t*>(camera_state+0xC4)=zoom_car_value;
            *reinterpret_cast<uint32_t*>(camera_state+0xD8)=zoom_ped_value;
            const auto car=*reinterpret_cast<const uintptr_t*>(player+0xB0);
            const auto handle=car?remote_vehicle_key(car):-1;
            if(handle<0 || *reinterpret_cast<const int16_t*>(car+0x22)!=model ||
               !remote_reference_live(car,reinterpret_cast<const uintptr_t*>(player+0xB0)) ||
               (*reinterpret_cast<const uint8_t*>(car+0x36)>>3)!=8 ||
               *reinterpret_cast<const uint8_t*>(native_entity_address(0xB7CD74))!=focus ||
               player_ped_at(player)!=ped || remote_ped_key(ped)!=ped_key)return;
            // The original fixed RCBANDIT takes MODE_BEHINDCAR (2253D0).
            // SA's factory instead selects CAM_ON_A_STRING and zoom1.
            native_remote_camera(car,3,1);
            if(remote_vehicle_key(car)!=handle || *reinterpret_cast<const uintptr_t*>(player+0xB0)!=car ||
               !remote_reference_live(car,reinterpret_cast<const uintptr_t*>(player+0xB0)) ||
               *reinterpret_cast<const uint8_t*>(native_entity_address(0xB7CD74))!=focus ||
               player_ped_at(player)!=ped || remote_ped_key(ped)!=ped_key)return;
            owned.vehicle=car;owned.handle=handle;
            native_register_reference(car,&owned.vehicle);
            if(owned.vehicle!=car || !remote_reference_live(car,&owned.vehicle) || remote_vehicle_key(car)!=handle ||
               *slot!=car || !remote_reference_live(car,slot))return;
            receipt->values[0]=handle;receipt->ok=true;return;
        }
        if(action==EntityCommand::override_remote_car) {
            const uint8_t camera[]={0x56,0x8B,0xF1,0x8B,0x4C,0x24,0x14,0x83,0xF9,0x02,0xB0,0x01};
            const uint8_t zoom[]={0x51,0x56,0x8B,0xF1,0x8B,0x8E,0x90,0x03,0,0,0x8A,0x41};
            const uint8_t cleanup[]={0xE9,0x5B,0x93,0x10,0x01};
            if(!remote_cancel_profile() || !appearance_profile() || !profile(0x50C7C0,camera) ||
               !profile(0x50C1B0,zoom) || !profile(0x4654B0,cleanup))return;
            const auto handle=call->inputs[0];
            const auto car=entity_at(*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74494)),handle,0xA18,2);
            if(!car || !*reinterpret_cast<const uintptr_t*>(car+0x384))return;
            const auto appearance=native_remote_appearance(car);
            // Ground cars use the per-vehicle door policy once its producer
            // callsites are healthy; the aerial appearances retain their path.
            if(appearance!=1 && appearance!=3 && appearance!=5)return;
            const auto focus=*reinterpret_cast<const uint8_t*>(native_entity_address(0xB7CD74));if(focus>1)return;
            const auto player=native_entity_address(0xB7CD98)+focus*0x190;
            const auto ped=player_ped_at(player);if(!ped)return;
            const auto ped_key=remote_ped_key(ped);if(ped_key<0)return;
            auto* slot=reinterpret_cast<uintptr_t*>(player+0xB0);
            if(*slot && (*slot!=car || !remote_reference_live(car,slot)))return;
            const auto in_vehicle=(*reinterpret_cast<const uint8_t*>(ped+0x46D)&1)!=0;
            const auto physical=in_vehicle?*reinterpret_cast<const uintptr_t*>(ped+0x58C):uintptr_t(0);
            const auto physical_key=physical?remote_vehicle_key(physical):-1;
            if(in_vehicle && physical_key<0)return;
            const auto same=[&]() {
                return *reinterpret_cast<const uint8_t*>(native_entity_address(0xB7CD74))==focus &&
                    player_ped_at(player)==ped && remote_ped_key(ped)==ped_key && remote_vehicle_key(car)==handle &&
                    bool(*reinterpret_cast<const uint8_t*>(ped+0x46D)&1)==in_vehicle &&
                    (!physical || (*reinterpret_cast<const uintptr_t*>(ped+0x58C)==physical && remote_vehicle_key(physical)==physical_key));
            };
            const bool ground_door_policy=appearance==1;
            if(!same() || (ground_door_policy && !sa_remote_door_policy_calls_healthy()))return;
            receipt->entered=true;
            if(ground_door_policy && !sa_remote_door_policy_track_vehicle(car,handle))return;
            if(!same())return;
            *reinterpret_cast<uint8_t*>(car+0x36)=uint8_t((*reinterpret_cast<uint8_t*>(car+0x36)&7)|(8<<3));
            // plugin-sdk CVehicle.cpp: this is the actual native control-input
            // consumer's disable flag, not the adjacent contact/mouse flags.
            *reinterpret_cast<uint8_t*>(native_entity_address(0xC1CC00))=1;
            if(!*slot){*slot=car;native_register_reference(car,slot);}
            if(!same() || *slot!=car || !remote_reference_live(car,slot))return;
            native_remote_camera(car,ground_door_policy?0x10:0x12,call->inputs[1]?1:2);
            if(!same() || *slot!=car)return;
            if(!ground_door_policy) {
                native_remote_zoom(0);
                if(!same() || *slot!=car)return;
            }
            if(call->mission_context) {
                native_remote_cleanup(handle);
                if(!same() || *slot!=car)return;
            }
            if(physical)*reinterpret_cast<uint8_t*>(physical+0x42A)&=uint8_t(~0x20u);
            receipt->ok=true;return;
        }
        if(action==EntityCommand::cancel_remote_mode) {
            if(!remote_cancel_profile())return;
            const auto focus=*reinterpret_cast<const uint8_t*>(native_entity_address(0xB7CD74));
            if(focus>1)return;
            const auto player=native_entity_address(0xB7CD98)+focus*0x190;
            const auto ped=player_ped_at(player);if(!ped)return;
            const auto ped_key=remote_ped_key(ped);if(ped_key<0)return;
            const auto remote=*reinterpret_cast<const uintptr_t*>(player+0xB0);
            const auto remote_key=remote?remote_vehicle_key(remote):-1;
            if(remote && (remote_key<0 || (*reinterpret_cast<const uint8_t*>(remote+0x1D)&8) ||
                !remote_reference_live(remote,reinterpret_cast<const uintptr_t*>(player+0xB0))))return;
            const auto in_vehicle=(*reinterpret_cast<const uint8_t*>(ped+0x46D)&1)!=0;
            const auto physical=in_vehicle?*reinterpret_cast<const uintptr_t*>(ped+0x58C):uintptr_t(0);
            const auto physical_key=physical?remote_vehicle_key(physical):-1;
            if(in_vehicle && physical_key<0)return;
            // Capture identity, focus and pool generations ahead of any native call.
            // No call to the unsafe empty SA 0409 or the exploding 56EA30.
            const auto same=[&](bool require_reference=true) -> bool {
                return *reinterpret_cast<const uint8_t*>(native_entity_address(0xB7CD74))==focus &&
                    player_ped_at(player)==ped &&
                    remote_ped_key(ped)==ped_key &&
                    ((*reinterpret_cast<const uint8_t*>(ped+0x46D)&1)!=0)==in_vehicle &&
                    (!in_vehicle || *reinterpret_cast<const uintptr_t*>(ped+0x58C)==physical) &&
                    (!physical || remote_vehicle_key(physical)==physical_key) &&
                    *reinterpret_cast<const uintptr_t*>(player+0xB0)==remote &&
                    (!remote || (remote_vehicle_key(remote)==remote_key &&
                    (!require_reference || remote_reference_live(remote,reinterpret_cast<const uintptr_t*>(player+0xB0)))));
            };
            if(!same())return;
            // Physical occupied car, not the RC car, regains damage permission.
            if(physical) {
                receipt->entered=true;
                *reinterpret_cast<uint8_t*>(physical+0x42A)|=0x20;
                if(!same())return;
            }
            receipt->entered=true;
            if(remote) {
                // Actual cdecl CRemote service: false never triggers explosion.
                native_cancel_remote_release(false);
            } else {
                // The source invokes its state transition even with no remote
                // vehicle. This native helper would dereference null. Reproduce
                // ONLY its verified final stores without changing ownership.
                *reinterpret_cast<uint32_t*>(player+0xE0)=
                    *reinterpret_cast<const uint32_t*>(native_entity_address(0xB7CB84));
                *reinterpret_cast<uint8_t*>(player+0xDD)=1;
                *reinterpret_cast<uint8_t*>(player+0xDE)=0;
                *reinterpret_cast<uint8_t*>(player+0xDF)=1;
            }
            if(!same())return;
            if(*reinterpret_cast<const uint8_t*>(player+0xDD)!=1 ||
               *reinterpret_cast<const uint8_t*>(player+0xDE)!=0 ||
               *reinterpret_cast<const uint8_t*>(player+0xDF)!=1)return;
            // Unregister the old entity reference before clearing the pointer;
            // a later deletion must not be able to null a newly installed RC.
            if(remote) {
                native_cleanup_reference(remote,reinterpret_cast<uintptr_t*>(player+0xB0));
                // A successful CleanUpOldReference deliberately removes
                // the registration, so do not require it after this call.
                if(!same(false) || remote_reference_live(remote,
                    reinterpret_cast<const uintptr_t*>(player+0xB0)))return;
            }
            *reinterpret_cast<uint8_t*>(player+0xDF)=0; // source field_D6=false
            *reinterpret_cast<uintptr_t*>(player+0xB0)=0;
            if(*reinterpret_cast<const uint8_t*>(native_entity_address(0xB7CD74))!=focus ||
               player_ped_at(player)!=ped || remote_ped_key(ped)!=ped_key)return;
            native_cancel_camera_restore(); // native CCamera::Restore thiscall
            receipt->ok=true;return;
        }
        if(action==EntityCommand::blow_up_rc_buggy) {
            const uint8_t helper[]={0x56,0x8B,0xF1,0x8B,0x86,0xB0,0,0,0,0x85,0xC0,0x74,0x3F,0x8B,0x48,0x1C,0xF6,0xC5,0x08,0x75,0x37};
            const uint8_t transition[]={0xE9,0xDB,0x72,0x10,0x01};
            const uint8_t remote[]={0x0F,0xB6,0x05,0x74,0xCD,0xB7,0,0x69,0xC0,0x90,0x01,0,0,0x8B,0x88,0x48,0xCE,0xB7,0};
            const uint8_t argument[]={0x53,0x8B,0x5C,0x24,0x0C,0x53,0xE8,0x30,0xC4,0xEE,0xFF,0x83,0xC4,0x04,0x84,0xDB,0x5B,0x74,0x24};
            const uint8_t creator[]={0x0F,0xB6,0x05,0x74,0xCD,0xB7,0,0x8B,0x8E,0xB0,0,0,0,0x69,0xC0,0x90,0x01,0,0,0x8B,0x80,0x98,0xCD,0xB7,0};
            const uint8_t state[]={0x8A,0x4C,0x24,0x04,0x89,0x90,0x78,0xCE,0xB7,0,0xC6,0x80,0x75,0xCE,0xB7,0,0x01,0x88,0x88,0x76,0xCE,0xB7,0,0xC6,0x80,0x77,0xCE,0xB7,0,0x01,0xC3};
            const uint8_t explode[]={0x6A,0,0x50,0xFF,0x92,0xA4,0,0,0};
            if(!profile(0x56EA30,helper) || !profile(0x45AE80,transition) ||
               !profile(0x1562160,remote) || !profile(0x56EA73,explode) || !profile(0x56EA45,argument) ||
               !profile(0x56EA58,creator) || !profile(0x15621F7,state) || !entity_profile(false))return;
            const auto focus=*reinterpret_cast<const uint8_t*>(native_entity_address(0xB7CD74));if(focus>1)return;
            const auto player=native_entity_address(0xB7CD98)+focus*0x190;
            const auto car=*reinterpret_cast<const uintptr_t*>(player+0xB0);
            // Original legitimately does nothing when no RC vehicle exists.
            if(!car){receipt->ok=true;return;}
            if(physical_pointer(car)!=car || (*reinterpret_cast<const uint8_t*>(car+0x36)&7)!=2)return;
            if(*reinterpret_cast<const uint8_t*>(car+0x1D)&8){receipt->ok=true;return;}
            if(!player_ped_at(player))return;
            receipt->entered=true;native_blow_up_rc(player);receipt->ok=true;return;
        }
        if(action==EntityCommand::char_leaving_vehicle_to_die) {
            if(!ped_pool_profile())return;
            const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74490));
            const auto ped=entity_at(pool,call->inputs[0],0x7C4,3);if(!ped)return;
            const auto state=leaving_vehicle_to_die(ped);if(state<0)return;
            receipt->condition=state!=0;receipt->ok=true;return;
        }
        const bool object=action==EntityCommand::object_turn_speed || action==EntityCommand::object_render_damaged ||
            action==EntityCommand::get_object_orientation || action==EntityCommand::set_object_orientation;
        const bool random=action==EntityCommand::random_car_unchecked || action==EntityCommand::random_car_no_save;
        const bool read_orientation=action==EntityCommand::get_car_orientation || action==EntityCommand::get_object_orientation;
        const bool set_orientation=action==EntityCommand::set_car_orientation || action==EntityCommand::set_object_orientation;
        const auto input=call->inputs.data();
        if(action==EntityCommand::char_wander_path) {
            const uint8_t los[]={0x83,0xEC,0x5C,0x66,0x81,0x3D,0x78,0xCD,0xB7,0,0xFF,0xFF,0x73};
            const uint8_t vertical[]={0x83,0xEC,0x2C,0x90,0x90,0x90,0x90,0xE9,0x4B,0x07,0xEA,0xFF};
            const uint8_t vertical_trampoline[]={0x66,0x83,0x3D,0x78,0xCD,0xB7,0,0xFF,0xE9,0xA8,0xF8,0x15,0};
            const uint8_t water[]={0x8B,0x54,0x24,0x0C,0x83,0xEC,0x08,0x53,0x8B,0x5C,0x24,0x10,0x56,0x8B,0x74,0x24,0x20};
            if(!ped_pool_profile() || !profile(0x56A490,los) || !profile(0x5674E0,vertical) ||
               !profile(0x407C37,vertical_trampoline) || !profile(0x6EB690,water))return;
            const auto ped=entity_at(*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74490)),input[0],0x7C4,3);
            if(!ped)return;
            const auto matrix=*reinterpret_cast<const uintptr_t*>(ped+0x14);
            WanderPoint from{};std::memcpy(from.data(),reinterpret_cast<const void*>(matrix?matrix+0x30:ped+4),12);
            const auto to=source_vector(input+1);if(!to || !std::isfinite(entity_float(input[4])))return;
            WanderQueries queries{&receipt->entered};
            const auto clear=source_wander_path(from,*to,entity_float(input[4]),queries);
            if(!clear)return;
            receipt->condition=*clear;receipt->ok=true;return;
        }
        if(action==EntityCommand::car_collision) {
            const uint8_t damage_target[]={0x8D,0x86,0x0C,0x05,0,0,0x50,0x8B,0xCF,0xC6,0x86,0x08,0x05,0,0,0x31,0x89,0x38,0xE8,0x7F,0xA0,0xEC,0xFF};
            if(!entity_profile(false) || !reference_profile() || !profile(0x6A7ADA,damage_target))return;
            const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74494));
            const auto car=entity_at(pool,input[0],0xA18,2);if(!car)return;
            auto* slot=reinterpret_cast<uintptr_t*>(car+0x50C);
            receipt->values[0]=-1;
            if(!*slot){receipt->ok=true;return;}
            // Never pass a non-vehicle or dangling pointer to GetVehicleRef.
            // Retail LCS did so without a type check, producing unsafe handles.
            const auto target=physical_pointer(*slot);if(!target)return;
            if((*reinterpret_cast<const uint8_t*>(target+0x36)&7)==2) {
                const auto first=*reinterpret_cast<const uintptr_t*>(pool),flags=*reinterpret_cast<const uintptr_t*>(pool+4);
                const auto index=(target-first)/0xA18;
                receipt->values[0]=int32_t((uint32_t(index)<<8)|*reinterpret_cast<const uint8_t*>(flags+index));
            }
            receipt->entered=true;native_cleanup_reference(target,slot);*slot=0;
            receipt->ok=true;return;
        }
        if(action==EntityCommand::car_wreck) {
            const uint8_t damage_layout[]={0xBA,0x04,0x04,0x04,0x04,0x89,0x56,0x09,0x66,0x89,0x56,0x0D};
            const uint8_t damage_finish[]={0x89,0x46,0x10,0x89,0x46,0x14,0xC6,0x46,0x04,0xFA};
            const uint8_t wreck_status[]={0x80,0xE1,0x07,0x80,0xC9,0x28,0x88,0x4E,0x36,0x81,0x4E,0x40,0,0,0,0x20};
            const uint8_t bombs[]={0x88,0x8E,0xA8,0x04,0,0};
            const uint8_t bumper[]={0x53,0x55,0x56,0x57,0x8B,0x7C,0x24,0x14,0x8B,0xF1,0x57,0x90,0xE9,0xBC,0x87,0xD5,0xFF};
            const uint8_t trampoline[]={0x8D,0x8E,0xA0,0x05,0,0,0xE9,0x39,0x78,0x2A,0};
            const uint8_t doors[]={0x6A,0xFF,0x68,0x78,0x81,0x84,0,0x64,0xA1,0,0,0,0,0x50,0x64,0x89,0x25,0,0,0,0,0x83,0xEC,0x58,0x53,0x55,0x56,0x57,0x8B,0x7C,0x24,0x78,0x8B,0xF1,0x8D,0x8E,0xA0,0x05,0,0};
            const uint8_t panel_nodes[]={0x8B,0x4C,0x24,0x04,0x83,0xC8,0xFF,0x83,0xF9,0x06,0x77,0x24,0xFF,0x24,0x8D,0xD4,0x26,0x6C,0};
            const uint8_t panel_lookup[]={0xC5,0x26,0x6C,0,0xCB,0x26,0x6C,0};
            const uint8_t panel_result[]={0xB8,0x0C,0,0,0,0xC3,0xB8,0x0D,0,0,0,0xC3};
            const uint8_t door_nodes[]={0x8B,0x4C,0x24,0x04,0x83,0xC8,0xFF,0x83,0xF9,0x05,0x77,0x2A,0xFF,0x24,0x8D,0x28,0x27,0x6C,0};
            const uint8_t door_lookup[]={0x03,0x27,0x6C,0,0x09,0x27,0x6C,0,0x0F,0x27,0x6C,0,0x15,0x27,0x6C,0,0x1B,0x27,0x6C,0,0x21,0x27,0x6C,0};
            const uint8_t door_result[]={0xB8,0x10,0,0,0,0xC3,0xB8,0x11,0,0,0,0xC3,0xB8,0x0A,0,0,0,0xC3,0xB8,0x08,0,0,0,0xC3,0xB8,0x0B,0,0,0,0xC3,0xB8,0x09,0,0,0,0xC3};
            if(input[1]<0 || input[1]>=20000 || !entity_profile(false) || !profile(0x6C2608,damage_layout) ||
               !profile(0x6C268A,damage_finish) || !profile(0x6B3C22,wreck_status) || !profile(0x6B3D27,bombs) ||
               !profile(0x6B1350,bumper) || !profile(0x409B1D,trampoline) || !profile(0x6B1600,doors) ||
               !profile(0x6C26A0,panel_nodes) || !profile(0x6C26E8,panel_lookup) || !profile(0x6C26C5,panel_result) ||
               !profile(0x6C26F0,door_nodes) || !profile(0x6C2728,door_lookup) || !profile(0x6C2703,door_result))return;
            const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74494));
            const auto car=entity_at(pool,input[0],0xA18,2);if(!car)return;
            // Source asserts IsCar. Boats/bikes/planes have different storage.
            if(*reinterpret_cast<const int32_t*>(car+0x590)!=0 || *reinterpret_cast<const int32_t*>(car+0x594)!=0)return;
            const auto model=*reinterpret_cast<const int16_t*>(car+0x22);if(model<0 || model>=20000)return;
            const bool visual=model!=input[1];
            if(visual) {
                bool nodes=false;
                for(size_t n:{size_t(8),size_t(9),size_t(10),size_t(11),size_t(12),size_t(13),size_t(16),size_t(17)})
                    if(*reinterpret_cast<const uintptr_t*>(car+0x648+n*4))nodes=true;
                if(nodes) {
                    const auto info=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xA9B0C8)+model*4);
                    if(!info || !*reinterpret_cast<const uintptr_t*>(info+0x5C) || !*reinterpret_cast<const uintptr_t*>(car+0x384) ||
                       !*reinterpret_cast<const uintptr_t*>(car+0x18))return;
                }
            }
            receipt->entered=true;
            *reinterpret_cast<float*>(car+0x4C0)=0;
            *reinterpret_cast<uint8_t*>(car+0x36)=uint8_t((*reinterpret_cast<const uint8_t*>(car+0x36)&7)|0x28);
            *reinterpret_cast<uint8_t*>(car+0x43)|=0x20;
            // Retail source 32FF88: fixed wheel zero, all six doors missing,
            // lights/panels reset and engine 250. Native FuckCarCompletely
            // selects a random wheel, so calling it would alter this contract.
            *reinterpret_cast<uint8_t*>(car+0x5A5)=2;
            std::memset(reinterpret_cast<void*>(car+0x5A9),4,6); // native missing-door enum
            *reinterpret_cast<uint32_t*>(car+0x5B0)=0;
            *reinterpret_cast<uint32_t*>(car+0x5B4)=0;
            *reinterpret_cast<uint8_t*>(car+0x5A4)=250;
            if(visual) {
                native_wreck_bumper(car,5);native_wreck_bumper(car,6);
                for(int32_t n=0;n<6;++n)native_wreck_door(car,n);
            }
            *reinterpret_cast<uint8_t*>(car+0x4A8)&=0xF8;
            *reinterpret_cast<uint8_t*>(car+0x428)&=0xAF;
            *reinterpret_cast<float*>(car+0x4C0)=0;
            *reinterpret_cast<uint16_t*>(car+0x4DE)=0;
            *reinterpret_cast<uint8_t*>(car+0x42D)&=0x7F;
            receipt->ok=true;return;
        }
        if(action==EntityCommand::char_attach_object) {
            const uint8_t attach[]={0x55,0x8B,0x6C,0x24,0x08,0x85,0xED,0x56,0x8B,0xF1,0x74,0x0B,0x90,0xE9,0xD5,0x99,0xE1,0xFF,0xF6,0xC4,0x01};
            const uint8_t trampoline[]={0x8B,0x86,0x6C,0x04,0x00,0x00,0xE9,0x20,0x66,0x1E,0x00};
            const uint8_t fields[]={0x8D,0x86,0xFC,0x00,0x00,0x00,0x50,0x8B,0xCD,0x89,0x28,0xE8,0x92,0x9E,0xF8,0xFF};
            if(!ped_pool_profile() || !entity_profile(true) || !reference_profile() || !profile(0x5E7CB0,attach) ||
               !profile(0x401697,trampoline) || !profile(0x5E7CCE,fields))return;
            const auto offset=source_vector(input+2);const auto degrees=entity_float(input[6]);
            if(!offset || !std::isfinite(degrees) || input[7]<0 || input[7]>46)return;
            const auto ped_pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74490));
            const auto object_pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB7449C));
            const auto ped=entity_at(ped_pool,input[0],0x7C4,3),object=entity_at(object_pool,input[1],0x19C,4);
            if(!ped || !object)return;
            const auto type=*reinterpret_cast<const uint32_t*>(ped+0x598);
            if(type>31 || (type<=1 && !*reinterpret_cast<const uintptr_t*>(ped+0x480)))return;
            if(*reinterpret_cast<const int32_t*>(ped+0x70C)==55 && *reinterpret_cast<const uint8_t*>(ped+0x718)>=13)return;
            auto* slot=reinterpret_cast<uintptr_t*>(ped+0xFC);const auto old=*slot;
            if(old && !physical_pointer(old))return;
            auto ancestor=object;size_t depth=0;
            while(ancestor) {
                if(ancestor==ped || depth++>=128 || !physical_pointer(ancestor))return;
                ancestor=*reinterpret_cast<const uintptr_t*>(ancestor+0xFC);
            }
            const float radians=entity_float(0x3C8EFA36); // actual PS2 multiplier at 186FC8
            receipt->entered=true;
            const auto target=native_attach_character(ped,object,*offset,uint16_t(input[5]),degrees*radians,input[7]);
            if(target && (target!=object || *slot!=object))return;
            // A non-controllable ped is ignored by the real original/native
            // helper. Only release an old parent when a new attach succeeded.
            if(target && old && old!=object)native_cleanup_reference(old,slot);
            receipt->condition=target!=0;receipt->ok=true;return;
        }
        if(action==EntityCommand::attach_object_car || action==EntityCommand::detach_object_car) {
            const uint8_t attach[]={0x55,0x8B,0x6C,0x24,0x08,0x85,0xED,0x56,0x8B,0xF1,0x75,0x07,0x5E,0x33,0xC0,0x5D,0xC2,0x1C,0x00};
            const uint8_t attachment_slot[]={0x8D,0xBE,0xFC,0x00,0x00,0x00,0x57,0x8B,0xCD,0x89,0x2F,0xE8,0xDB,0x45,0x02,0x00};
            const uint8_t ignored_slot[]={0x89,0x96,0x28,0x01,0x00,0x00};
            if(!entity_profile(true) || !reference_profile() || !profile(0x54D570,attach) ||
               !profile(0x54D585,attachment_slot) || !profile(0x54D61F,ignored_slot))return;
            const auto object_pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB7449C));
            const auto object=entity_at(object_pool,input[0],0x19C,4);
            // Retail DETACH explicitly permits an absent object.
            if(!object){if(action==EntityCommand::detach_object_car)receipt->ok=true;return;}
            auto* attached=reinterpret_cast<uintptr_t*>(object+0xFC);
            auto* ignored=reinterpret_cast<uintptr_t*>(object+0x128);
            if(!*attached && action==EntityCommand::detach_object_car){receipt->ok=true;return;}
            if((*attached && !physical_pointer(*attached)) || (*ignored && !physical_pointer(*ignored)))return;
            if(action==EntityCommand::detach_object_car) {
                // LCS 221B30 clears only these pointers. SA 0682 also changes
                // velocity/physics, even for zero operands, so it is not used.
                receipt->entered=true;
                native_cleanup_reference(*attached,attached);
                if(*ignored)native_cleanup_reference(*ignored,ignored);
                *ignored=0;*attached=0;receipt->ok=true;return;
            }
            const auto offset=source_vector(input+2);if(!offset || !entity_profile(false))return;
            const auto car_pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74494));
            const auto car=entity_at(car_pool,input[1],0xA18,2);if(!car)return;
            // Reject a cycle or corrupted ancestry before the engine recursively
            // positions attachments. This does not change source progression.
            auto ancestor=car;size_t depth=0;
            while(ancestor) {
                if(ancestor==object || depth++>=128 || !physical_pointer(ancestor))return;
                ancestor=*reinterpret_cast<const uintptr_t*>(ancestor+0xFC);
            }
            receipt->entered=true;
            if(*attached)native_cleanup_reference(*attached,attached);
            if(*ignored)native_cleanup_reference(*ignored,ignored);
            native_attach_object(object,car,*offset);
            if(*attached!=car || *ignored!=car)return;
            receipt->ok=true;return;
        }
        if(action==EntityCommand::heli_goto) {
            const uint8_t heli[]={0x8B,0x44,0x24,0x04,0x8B,0x54,0x24,0x0C,0x56,0x8B,0xF1,0xD9,0x86,0xA8,0x08,0x00,0x00};
            const uint8_t flight[]={0xC6,0x86,0xBA,0x03,0x00,0x00,0x14,0xC6,0x86,0xD0,0x03,0x00,0x00,0x64,0x89,0x8E,0xA8,0x09,0x00,0x00};
            if(!entity_profile(false) || !profile(0x6A2390,heli) || !profile(0x6A23D9,flight))return;
            const auto position=source_vector(input+1);if(!position)return;
            const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74494));
            const auto car=entity_at(pool,input[0],0xA18,2);if(!car)return;
            if(*reinterpret_cast<const int32_t*>(car+0x590)!=3 || *reinterpret_cast<const int32_t*>(car+0x594)!=3)return;
            const auto low=*reinterpret_cast<const float*>(car+0x9B0),high=*reinterpret_cast<const float*>(car+0x9A8);
            const auto heading=*reinterpret_cast<const float*>(car+0x8A8);
            if(!std::isfinite(low) || !std::isfinite(high) || !std::isfinite(heading))return;
            if(heading==0) {
                const auto matrix=*reinterpret_cast<const uintptr_t*>(car+0x14);if(!matrix)return;
                if(!source_vector(reinterpret_cast<const int32_t*>(matrix+0x10)))return;
            }
            receipt->entered=true;
            // Native 04A2 takes two altitude floats, not the source speed byte.
            // Keep this helicopter's live limits and use its real controller.
            native_heli_goto(car,*position,low,high);
            *reinterpret_cast<uint8_t*>(car+0x3D0)=uint8_t(input[4]);
            receipt->ok=true;return;
        }
        if(action==EntityCommand::car_ram_car) {
            const uint8_t mission[]={0x0F,0xBE,0x86,0xBA,0x03,0x00,0x00,0x83,0xF8,0x44,0x0F,0x87,0x9B,0x00,0x00,0x00,0x0F,0xB6,0x80,0x7C,0x2D,0x43,0x00,0xFF,0x24,0x85,0x68,0x2D,0x43,0x00};
            const uint8_t target_field[]={0x8B,0x86,0x1C,0x04,0x00,0x00};
            const uint8_t target_case[]={3},target_jump[]={0x31,0x2D,0x43,0x00};
            if(!entity_profile(false) || !reference_profile() || !profile(0x432CB8,mission) ||
               !profile(0x432D31,target_field) || !profile(0x432D8B,target_case) || !profile(0x432D74,target_jump))return;
            const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74494));
            const auto car=entity_at(pool,input[0],0xA18,2),target=entity_at(pool,input[1],0xA18,2);
            if(!car || !target)return;
            auto* slot=reinterpret_cast<uintptr_t*>(car+0x41C);if(*slot && !physical_pointer(*slot))return;
            receipt->entered=true;
            if(*slot)native_cleanup_reference(*slot,slot);
            *slot=target;native_register_reference(target,slot);
            // Retail 29C558 keeps existing speed unless signed byte < 6.
            auto* speed=reinterpret_cast<int8_t*>(car+0x3D0);if(*speed<6)*speed=6;
            *reinterpret_cast<uint8_t*>(car+0x428)|=16;
            *reinterpret_cast<uint8_t*>(car+0x3BA)=15;
            receipt->ok=true;return;
        }
        if(action==EntityCommand::reset_player_havoc || action==EntityCommand::get_player_havoc) {
            if(input[0]<0 || input[0]>1)return;
            const uint8_t player_lookup[]={0x8B,0x44,0x24,0x04,0x85,0xC0,0x7D,0x07,0x0F,0xB6,0x05,0x74,0xCD,0xB7,0,0x69,0xC0,0x90,0x01,0,0,0x8B,0x80,0x98,0xCD,0xB7,0,0xC3};
            const uint8_t clear_havoc[]={0x89,0x9E,0x40,0x01,0,0,0x0F,0xB6,0x0D,0x74,0xCD,0xB7,0};
            if(!profile(0x56E210,player_lookup) || !profile(0x56F46C,clear_havoc))return;
            const auto player=native_entity_address(0xB7CD98)+uintptr_t(input[0])*0x190;
            if(!player_ped_at(player))return;
            auto* havoc=reinterpret_cast<uint32_t*>(player+0x140);
            if(action==EntityCommand::get_player_havoc)std::memcpy(receipt->values.data(),havoc,4);
            else {receipt->entered=true;*havoc=0;}
            receipt->ok=true;return;
        }
        if(action==EntityCommand::char_locomotion_group) {
            const auto name=original_locomotion_group(input[1]);if(!name)return;
            // The actual retail SA 0245 collects a handle and a 16-character
            // name, then writes the matching association group. ScriptDebug's
            // numeric LCS group is a different ABI, not a native group index.
            const uint8_t group_name[]={0x8B,0x44,0x24,0x04,0x8D,0x04,0x40,0xC1,0xE0,0x04,0x05,0xA8,0xA5,0x8A,0,0xC3};
            const uint8_t group_write[]={0x0F,0xB7,0xCE,0x89,0x8F,0xD4,0x04,0,0};
            const uint8_t group_read[]={0x83,0xF9,0x04,0x57,0x8B,0xBE,0xD4,0x04,0,0,0x89,0x8E,0x38,0x05,0,0};
            if(!ped_pool_profile() || !profile(0x4D3A20,group_name) ||
               !profile(0x47EF71,group_write) || !profile(0x5E4ACA,group_read))return;
            const auto count=*reinterpret_cast<const int32_t*>(native_entity_address(0xB4EA28));
            // The fixed retail definition storage ends before the adjacent
            // animation descriptor data. Reject an expanded/relocated profile.
            if(count<=0 || count>145)return;
            const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74490));
            const auto ped=entity_at(pool,input[0],0x7C4,3);if(!ped)return;
            int32_t native_group=-1;
            for(int32_t i=0;i<count;++i) {
                const auto row=reinterpret_cast<const char*>(native_entity_address(0x8AA5A8)+uintptr_t(i)*0x30);
                if(!std::memchr(row,0,16))return;
                if(std::strcmp(row,name)==0){native_group=i;break;}
            }
            // SA's 0245 writes count when the name is missing, which is out of
            // range. Refuse before writing, instead of reproducing that crash.
            if(native_group<0)return;
            receipt->entered=true;
            *reinterpret_cast<int32_t*>(ped+0x4D4)=native_group;
            receipt->ok=true;return;
        }
        if(action==EntityCommand::photograph_taken) {
            const uint8_t taken[]={0xC6,0x05,0xC0,0xA7,0xC8,0,0x01,0xC6,0x05,0xC1,0xA7,0xC8,0,0x01};
            const uint8_t reset[]={0xA2,0xC0,0xA7,0xC8,0,0xA2,0xC1,0xA7,0xC8,0};
            if(!profile(0x73C252,taken) || !profile(0x73A31B,reset))return;
            auto* flag=reinterpret_cast<uint8_t*>(native_entity_address(0xC8A7C0));
            receipt->condition=*flag!=0;
            // Source consumes this latch even when false. ms_bTakePhoto in
            // the adjacent byte belongs to the camera pipeline, not this query.
            receipt->entered=true;*flag=0;receipt->ok=true;return;
        }
        if(action==EntityCommand::activate_save_menu) {
            // Complete retail SA 03D8 body (0x48542C..0x485485), including its
            // calls to CPhysical::ResetMoveSpeed (0x441130) / ResetTurnSpeed
            // (0x45AFB0), which zero m_vecMoveSpeed (+0x44) / m_vecTurnSpeed (+0x50).
            const uint8_t handler[]={0x83,0xCB,0xFF,0x53,0xE8,0xDB,0x8D,0x0E,0x00,0x83,0xC4,0x04,0x85,0xC0,0x74,0x47,
                0x53,0xE8,0xCE,0x8D,0x0E,0x00,0x8B,0x88,0x6C,0x04,0x00,0x00,0x83,0xC4,0x04,0xF6,0xC5,0x01,0x75,0x33,
                0x6A,0x00,0x6A,0x00,0x6A,0x00,0x53,0xC6,0x05,0xA7,0x67,0xBA,0x00,0x01,
                0xE8,0xAD,0x8D,0x0E,0x00,0x83,0xC4,0x04,0x8B,0xC8,0xE8,0xC3,0xBC,0xFB,0xFF,
                0x6A,0x00,0x6A,0x00,0x6A,0x00,0x53,0xE8,0x97,0x8D,0x0E,0x00,0x83,0xC4,0x04,0x8B,0xC8,0xE8,0x2D,0x5B,0xFD,0xFF,
                0x32,0xC0};
            const uint8_t query[]={0xA0,0xA4,0x67,0xBA,0x00,0x84,0xC0,0x75,0x0E,
                0xA0,0xA7,0x67,0xBA,0x00,0x84,0xC0,0xC6,0x44,0x24,0x10,
                0x01,0x74,0x05,0xC6,0x44,0x24,0x10,0x00};
            const uint8_t player_lookup[]={0x8B,0x44,0x24,0x04,0x85,0xC0,0x7D,0x07,0x0F,0xB6,0x05,0x74,0xCD,0xB7,0,0x69,0xC0,0x90,0x01,0,0,0x8B,0x80,0x98,0xCD,0xB7,0,0xC3};
            const uint8_t stat_table[]={0x66,0x8B,0x44,0x24,0x04,0x66,0x3D,0x52,0,0x73,0x0B,0x0F,0xB7,0xC0,0xD9,0x04,0x85,0x80,0x93,0xB7,0,0xC3,0x0F,0xB7,0xC8,0xDB,0x04,0x8D,0x20,0x8E,0xB7,0,0xC3};
            if(!entity_profile(false) || !ped_pool_profile() || !profile(0x48542C,handler) ||
               !profile(0x48548A,query) || !profile(0x56E210,player_lookup) || !profile(0x558E40,stat_table))return;
            const auto focus=*reinterpret_cast<const uint8_t*>(native_entity_address(0xB7CD74));
            if(focus>1)return;
            const auto player=native_entity_address(0xB7CD98)+uintptr_t(focus)*0x190;
            const auto raw_ped=*reinterpret_cast<const uintptr_t*>(player);
            // Retail PS2 22A820 and SA 48542C both return without mutating
            // SafeHouseVisits, m_bActivateSaveMenu or speeds when FindPlayerPed() is null.
            if(!raw_ped){receipt->ok=true;return;}
            const auto ped=player_ped_at(player);if(!ped)return;
            const auto ped_key=remote_ped_key(ped);if(ped_key<0)return;
            const bool in_vehicle=(*reinterpret_cast<const uint8_t*>(ped+0x46D)&1u)!=0;
            // Retail PS2 22A820 and SA 48542C both return without mutating
            // when the focused player is in a vehicle. They test only the
            // occupancy flag, not the retained last-vehicle reference.
            if(in_vehicle){receipt->ok=true;return;}
            auto* visits=reinterpret_cast<int32_t*>(native_entity_address(0xB79040));
            auto* save_menu=reinterpret_cast<uint8_t*>(native_entity_address(0xBA67A7));
            const auto next_visits=int32_t(uint32_t(*visits)+1u);
            receipt->entered=true;
            // PS2 22A858 stores the menu byte before the stat store in the
            // 22A860 call delay slot. Preserve that partial-effect boundary.
            *save_menu=1u;
            *visits=next_visits;
            std::memset(reinterpret_cast<void*>(ped+0x44),0,12);
            std::memset(reinterpret_cast<void*>(ped+0x50),0,12);
            if(*reinterpret_cast<const uint8_t*>(native_entity_address(0xB7CD74))!=focus ||
               player_ped_at(player)!=ped || remote_ped_key(ped)!=ped_key ||
               (*reinterpret_cast<const uint8_t*>(ped+0x46D)&1u)!=0 ||
               *visits!=next_visits || *save_menu!=1u)return;
            receipt->ok=true;return;
        }
        if(action==EntityCommand::save_game_finished) {
            // Retail SA 03D9 reads the real front-end menu-active and
            // save-menu-active flags. Same two-state predicate as LCS 0990;
            // never substitute an unconditional completion or saved-game bit.
            const uint8_t query[]={0xA0,0xA4,0x67,0xBA,0x00,0x84,0xC0,0x75,0x0E,
                0xA0,0xA7,0x67,0xBA,0x00,0x84,0xC0,0xC6,0x44,0x24,0x10,
                0x01,0x74,0x05,0xC6,0x44,0x24,0x10,0x00};
            if(!profile(0x48548A,query))return;
            const auto menu_active=*reinterpret_cast<const uint8_t*>(native_entity_address(0xBA67A4));
            const auto save_active=*reinterpret_cast<const uint8_t*>(native_entity_address(0xBA67A7));
            // Native renderer/owner owns these bytes. Pure observational
            // query: no mutation, no synthetic state or invented receipt.
            receipt->condition=!menu_active && !save_active;
            receipt->ok=true;return;
        }
        if(action==EntityCommand::credits_finished) {
            const uint8_t finished[]={0x8A,0x0D,0x7C,0xE9,0xC6,0,0x33,0xC0,0x84,0xC9,0x0F,0x94,0xC0,0xC3};
            if(!profile(0x489410,finished))return;
            receipt->condition=*reinterpret_cast<const uint8_t*>(native_entity_address(0xC6E97C))==0;
            receipt->ok=true;return;
        }
        if(action==EntityCommand::enable_control_camera) {
            const uint8_t pad[]={0x8B,0x44,0x24,0x04,0x69,0xC0,0x34,0x01,0,0,0x05,0x58,0x34,0xB7,0,0xC3};
            const uint8_t control[]={0x66,0x83,0xB9,0x0E,0x01,0,0,0,0x75,0x18};
            if(!profile(0x53FB70,pad) || !profile(0x53FD30,control))return;
            auto* flags=reinterpret_cast<uint16_t*>(native_entity_address(0xB73458)+0x10E);
            const auto current=*flags;
            // Only release PLAYERCONTROL_CAMERA (bit 0) on pad zero. Preserve
            // phone, garage, cutscene and native mod control ownership bits.
            receipt->entered=true;*flags=uint16_t(current&~1u);receipt->ok=true;return;
        }
        if(action==EntityCommand::first_person_weapon_camera) {
            const uint8_t camera[]={0x66,0x8B,0x81,0x30,0x08,0,0};
            if(!profile(0x50BFF0,camera))return;
            const auto mode=*reinterpret_cast<const uint16_t*>(native_entity_address(0xB6F028)+0x830);
            // The source consumes but does not use its player argument. SA's
            // native helper additionally accepts HS rockets and attached aim;
            // those two modes are absent from this source predicate.
            receipt->condition=mode==7 || mode==34 || mode==8 || mode==45 || mode==46;
            receipt->ok=true;return;
        }
        if(action==EntityCommand::char_drowning) {
            const uint8_t flags[]={0xF6,0x86,0x72,0x04,0,0,0x01};
            if(!ped_pool_profile() || !profile(0x5E873A,flags))return;
            const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74490));
            if(!pool)return;
            const auto count=*reinterpret_cast<const int32_t*>(pool+8);
            if(count<0 || count>0x7FFFFF || !*reinterpret_cast<const uintptr_t*>(pool) || !*reinterpret_cast<const uintptr_t*>(pool+4))return;
            const auto tags=*reinterpret_cast<const uintptr_t*>(pool+4);
            if(!valid_entity_slot(input[0],count,reinterpret_cast<const uint8_t*>(tags))){receipt->ok=true;return;}
            const auto ped=entity_at(pool,input[0],0x7C4,3);if(!ped)return;
            // Original predicate explicitly permits an absent pedestrian.
            receipt->condition=ped && (*reinterpret_cast<const uint8_t*>(ped+0x472)&8);
            receipt->ok=true;return;
        }
        if(action==EntityCommand::player_clip_ammo) {
            const uint8_t focused[]={0x8B,0x44,0x24,0x04,0x85,0xC0,0x7D,0x07,0x0F,0xB6,0x05,0x74,0xCD,0xB7,0,0x69,0xC0,0x90,0x01,0,0,0x8B,0x80,0x98,0xCD,0xB7,0,0xC3};
            const uint8_t active[]={0x0F,0xBE,0x81,0x18,0x07,0,0,0x6B,0xC0,0x1C,0x8D,0x84,0x08,0xA0,0x05,0,0,0xC3};
            if(!profile(0x56E210,focused) || !profile(0x43CE90,active))return;
            const auto focus=*reinterpret_cast<const uint8_t*>(native_entity_address(0xB7CD74));if(focus>1)return;
            const auto ped=player_ped_at(native_entity_address(0xB7CD98)+focus*0x190);if(!ped)return;
            const auto slot=*reinterpret_cast<const uint8_t*>(ped+0x718);if(slot>=13)return;
            // Zero leaves the current clip untouched in the source. Preserve
            // all other 32-bit values and the remaining weapon fields.
            if(input[0]){receipt->entered=true;*reinterpret_cast<uint32_t*>(ped+0x5A8+slot*0x1C)=uint32_t(input[0]);}
            receipt->ok=true;return;
        }
        if(action==EntityCommand::get_pickup_value || action==EntityCommand::set_pickup_value) {
            const uint8_t jump[]={0xE9,0x0B,0xF9,0x10,0x01};
            const uint8_t pickup[]={0x8B,0x4C,0x24,0x04,0x83,0xF9,0xFF,0x74,0x18,0x8B,0xC1,0x25,0xFF,0xFF,0,0,0x8B,0xD0,0xC1,0xE2,0x05,0xC1,0xE9,0x10,0x66,0x3B,0x8A,0xDA,0x88,0x97,0,0x74,0x03,0x83,0xC8,0xFF,0xC3};
            if(std::memcmp(reinterpret_cast<const void*>(native_entity_address(0x4552A0)),jump,sizeof(jump)) ||
               std::memcmp(reinterpret_cast<const void*>(native_entity_address(0x1564BB0)),pickup,sizeof(pickup)))return;
            auto* pickups=reinterpret_cast<uint8_t*>(native_entity_address(0x9788C0));
            const auto slot=source_pickup_slot(input[0],pickups,620*0x20);
            // Original invalid GET is zero and invalid SET leaves storage
            // unchanged. Type NONE does not invalidate a current generation.
            if(!slot){receipt->ok=true;return;}
            auto* revenue=reinterpret_cast<float*>(pickups+*slot*0x20);
            if(action==EntityCommand::get_pickup_value) {
                const auto value=source_pickup_integer(*revenue);if(!value)return;
                receipt->values[0]=*value;
            } else {receipt->entered=true;*revenue=float(input[1]);}
            receipt->ok=true;return;
        }
        if(action==EntityCommand::radar_brightness) {
            const uint8_t bright[]={0x8B,0x54,0x24,0x04,0x83,0xFA,0xFF,0x74,0x43,0x8B,0xCA,0x81,0xE1,0xFF,0xFF,0,0,0x8D,0x04,0x89,0xC1,0xE0,0x03};
            if(std::memcmp(reinterpret_cast<const void*>(native_entity_address(0x583C70)),bright,sizeof(bright)))return;
            auto* traces=reinterpret_cast<uint8_t*>(native_entity_address(0xBA86F0));
            const auto slot=source_radar_slot(input[0],traces,175*0x28);
            // Original brightness changes ignore removed or absent blips.
            if(!slot){receipt->ok=true;return;}
            auto* flags=traces+*slot*0x28+0x25;const auto value=source_radar_brightness(*flags,input[1]);
            receipt->entered=true;*flags=value;receipt->ok=true;return;
        }
        if(action==EntityCommand::get_out_of_jail_free || action==EntityCommand::player_fireproof ||
           action==EntityCommand::player_max_health || action==EntityCommand::player_max_armour) {
            if(call->inputs.size()!=2 || input[0]<0 || input[0]>1)return;
            auto* player=reinterpret_cast<uint8_t*>(native_entity_address(0xB7CD98)+input[0]*0x190);
            const auto ped=player_ped_at(reinterpret_cast<uintptr_t>(player));if(!ped)return;
            const auto mutation=entity_field_mutation(action,input,call->inputs.size(),player,0x190);
            if(!mutation)return;
            const bool health=action==EntityCommand::player_max_health,armour=action==EntityCommand::player_max_armour;
            if(health || armour) {
                const uint8_t flash[]={0x6A,0x01,0x8B,0xCE,0xE8,0xF9,0xE9,0xFD,0xFF,0x66,0x8B,0x0D,0x78,0x3C,0xA4,0,0x66,0x89,0x0D,0xDC,0xB1,0xBA,0};
                if(std::memcmp(reinterpret_cast<const void*>(native_entity_address(0x48567E)),flash,sizeof(flash)))return;
            }
            receipt->entered=true;
            if(!apply_entity_field_mutation(player,0x190,*mutation))return;
            if(health || armour) {
                *reinterpret_cast<float*>(ped+(health?0x540:0x548))=float(mutation->bytes[0]);
                *reinterpret_cast<int16_t*>(native_entity_address(0xBAB1DC))=health?4:3;
            }
            receipt->ok=true;return;
        }
        if(!entity_profile(object))return;
        const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(object?0xB7449C:0xB74494));if(!pool)return;
        if(random) {
            if(call->inputs.size()!=(action==EntityCommand::random_car_no_save?5:4))return;
            std::array<float,4> bounds{};for(size_t n=0;n<4;++n){bounds[n]=entity_float(input[n]);if(!std::isfinite(bounds[n]))return;}
            const auto count=*reinterpret_cast<const int32_t*>(pool+8);
            const auto flags=*reinterpret_cast<const uintptr_t*>(pool+4);if(count<=0 || count>0x7FFFFF || !flags)return;
            if(action==EntityCommand::random_car_no_save && !appearance_profile())return;
            receipt->values[0]=-1;
            for(int32_t slot=count-1;slot>=0;--slot) {
                const auto generation=*reinterpret_cast<const uint8_t*>(flags+slot);if(generation&0x80)continue;
                const auto handle=int32_t((uint32_t(slot)<<8)|generation);
                const auto vehicle=entity_at(pool,handle,0xA18,2);if(!vehicle)continue;
                const auto matrix=*reinterpret_cast<const uintptr_t*>(vehicle+0x14);
                const auto position=matrix?matrix+0x30:vehicle+4;
                const auto x=*reinterpret_cast<const float*>(position),y=*reinterpret_cast<const float*>(position+4);
                if(!source_car_in_area(x,y,bounds))continue;
                if(action==EntityCommand::random_car_no_save) {
                    const auto health=*reinterpret_cast<const float*>(vehicle+0x4C0);
                    if(!std::isfinite(health) || health==0 || *reinterpret_cast<const uint8_t*>(vehicle+0x4A4)!=1 ||
                       (input[4]>=0 && *reinterpret_cast<const uint16_t*>(vehicle+0x22)!=input[4]) ||
                       !*reinterpret_cast<const uintptr_t*>(vehicle+0x384))continue;
                    const auto appearance=reinterpret_cast<int32_t(__thiscall*)(uintptr_t)>(native_entity_address(0x6D1080))(vehicle);
                    if(appearance!=1 && appearance!=2)continue;
                }
                receipt->values[0]=handle;break;
            }
            receipt->ok=true;return;
        }
        const auto entity=entity_at(pool,input[0],object?0x19C:0xA18,object?4:2);if(!entity)return;
        if(action==EntityCommand::remove_car_boot) {
            const auto kind=*reinterpret_cast<const int32_t*>(entity+0x590),subclass=*reinterpret_cast<const int32_t*>(entity+0x594);
            if(kind<0 || kind>2 || subclass!=kind)return;
            const uint8_t damage[]={0x8B,0x44,0x24,0x04,0x85,0xC0,0x7C,0x0D,0x83,0xF8,0x06,0x7D,0x08,0x8A,0x54,0x24,0x08,0x88,0x54,0x08,0x09,0xC2,0x08,0};
            const uint8_t visual[]={0x6A,0xFF,0x68,0x78,0x81,0x84,0,0x64,0xA1,0,0,0,0,0x50,0x64,0x89,0x25,0,0,0,0,0x83,0xEC,0x58,0x53,0x55,0x56,0x57,0x8B,0x7C,0x24,0x78,0x8B,0xF1,0x8D,0x8E,0xA0,0x05,0,0};
            const uint8_t without_effect[]={0x80,0xFB,0x04,0x75,0x4A,0x8A,0x44,0x24,0x7C,0x84,0xC0,0x75,0x2F};
            const uint8_t boot_node[]={0xB8,0x11,0,0,0,0xC3};
            if(!profile(0x6C21C0,damage) || !profile(0x6B1600,visual) || !profile(0x6B17E5,without_effect) ||
               !profile(0x6C2709,boot_node) || *reinterpret_cast<const uintptr_t*>(native_entity_address(0x6C272C))!=0x6C2709)return;
            auto* door=reinterpret_cast<uint8_t*>(entity+0x5AA);if(*door>4)return;
            // The original updates damage even on cars without a boot node.
            // When a node exists the engine routine owns visual removal.
            if(*reinterpret_cast<const uintptr_t*>(entity+0x68C)) {
                const auto model=*reinterpret_cast<const int16_t*>(entity+0x22);if(model<0 || model>=20000)return;
                const auto info=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xA9B0C8)+model*4);
                const auto handling=*reinterpret_cast<const uintptr_t*>(entity+0x384);
                if(!info || !handling || !*reinterpret_cast<const uintptr_t*>(info+0x5C))return;
            }
            receipt->entered=true;*door=4;native_remove_boot_visual(entity);receipt->ok=true;return;
        }
        if(action==EntityCommand::boat_in_water || action==EntityCommand::heli_rotor_full_speed) {
            const auto type=*reinterpret_cast<const int32_t*>(entity+0x590);if(type<0 || type>11)return;
            if(action==EntityCommand::boat_in_water) {
                // Source asserts a boat; refuse another class before accessing
                // its subclass fields, rather than reporting invented false.
                if(type!=5)return;
                receipt->condition=bool(*reinterpret_cast<const uint8_t*>(entity+0x5AC)&1);
            } else {
                const uint8_t rotor[]={0x8B,0x88,0x94,0x05,0,0,0x83,0xF9,0x03,0x75,0x11,0xC7,0x80,0x4C,0x08,0,0,0xAE,0x47,0x61,0x3E};
                if(std::memcmp(reinterpret_cast<const void*>(native_entity_address(0x4740F6)),rotor,sizeof(rotor)))return;
                if(type==3) {
                    if(*reinterpret_cast<const int32_t*>(entity+0x594)!=3)return;
                    receipt->entered=true;*reinterpret_cast<float*>(entity+0x84C)=.22f;
                }
                // Original source leaves non-helicopter vehicles unchanged.
            }
            receipt->ok=true;return;
        }
        if(action==EntityCommand::car_is_boat || action==EntityCommand::car_is_bike || action==EntityCommand::car_being_jacked) {
            const auto type=*reinterpret_cast<const int32_t*>(entity+0x590);
            if(type<0 || type>11)return;
            receipt->condition=action==EntityCommand::car_is_boat?type==5:
                action==EntityCommand::car_is_bike?(type==9 || type==10):
                bool(*reinterpret_cast<const uint8_t*>(entity+0x42A)&8);
            receipt->ok=true;return;
        }
        if(read_orientation) {
            const auto matrix=*reinterpret_cast<const uintptr_t*>(entity+0x14);
            std::array<float,3> right{},forward{},up{};
            if(matrix) {
                std::memcpy(right.data(),reinterpret_cast<const void*>(matrix),12);
                std::memcpy(forward.data(),reinterpret_cast<const void*>(matrix+0x10),12);
                std::memcpy(up.data(),reinterpret_cast<const void*>(matrix+0x20),12);
            } else {
                const auto heading=*reinterpret_cast<const float*>(entity+0x10);if(!std::isfinite(heading))return;
                right={{std::cos(heading),std::sin(heading),0}};forward={{-std::sin(heading),std::cos(heading),0}};up={{0,0,1}};
            }
            const auto angles=source_orientation(right,forward,up);if(!angles)return;
            for(size_t n=0;n<3;++n)receipt->values[n]=entity_bits((*angles)[n]);receipt->ok=true;return;
        }
        if(set_orientation) {
            if(!orientation_profile())return;const auto angles=source_vector(input+1);if(!angles)return;
            receipt->entered=true;
            // GetMatrix allocates/initializes through the native matrix pool.
            // Without it SetOrientation discards pitch/roll for simple entities.
            if(!reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t)>(native_entity_address(0x411990))(entity))return;
            constexpr float radians=3.14159265358979323846f/180.f;
            reinterpret_cast<void(__thiscall*)(uintptr_t,float,float,float)>(native_entity_address(0x439A80))(entity,
                (*angles)[1]*radians,(*angles)[0]*radians,(*angles)[2]*radians);
            receipt->ok=true;return;
        }
        const auto mutation=entity_field_mutation(action,input,call->inputs.size(),reinterpret_cast<const uint8_t*>(entity),object?0x19C:0xA18);
        if(!mutation)return;receipt->entered=true;
        receipt->ok=apply_entity_field_mutation(reinterpret_cast<uint8_t*>(entity),object?0x19C:0xA18,*mutation);
    } __except(EXCEPTION_EXECUTE_HANDLER){receipt->exception=GetExceptionCode();}
}
}
std::optional<ScriptTaskSnapshot> sa_script_task_snapshot() {
    if(!sa_executor_profile_matches())return {};
    __try {
        const uint8_t lookup[]={0x8B,0x54,0x24,0x08,0x56,0x8B,0x74,0x24,0x08,0x83,0xC8,0xFF,0xB9,0xE8,0xB1,0xC0,0};
        const uint8_t bounds[]={0x83,0xC1,0x14,0x81,0xF9,0xE8,0xBB,0xC0,0};
        if(!profile(0x608710,lookup) || !profile(0x60872F,bounds))return {};
        ScriptTaskSnapshot snapshot{};
        std::memcpy(snapshot.data(),reinterpret_cast<const void*>(native_entity_address(0xC0B1E8)),sizeof(snapshot));
        return snapshot;
    } __except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
std::optional<int32_t> sa_pickup_model_in_area(const std::array<float,4>& sphere) {
    if(!sa_executor_profile_matches() || !std::all_of(sphere.begin(),sphere.end(),
            [](float v){return std::isfinite(v);}) || sphere[3]<0)return {};
    // These bytes are the existing reviewed SA pickup handle reader:
    // slot index * 0x20, 620 physical slots, and generation validation.
    constexpr uint8_t pickup[]={0x8B,0x4C,0x24,0x04,0x83,0xF9,0xFF,0x74,0x18,0x8B,0xC1,0x25,0xFF,0xFF,0,0,0x8B,0xD0,0xC1,0xE2,0x05,0xC1,0xE9,0x10,0x66,0x3B,0x8A,0xDA,0x88,0x97,0,0x74,0x03,0x83,0xC8,0xFF,0xC3};
    constexpr uint8_t jump[]={0xE9,0x0B,0xF9,0x10,0x01};
    __try {
        if(!profile(0x4552A0,jump) || !profile(0x1564BB0,pickup) || !entity_profile(true))return {};
        const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB7449C));
        if(!pool)return {};
        const auto objects=*reinterpret_cast<const uintptr_t*>(pool);
        const auto tags=*reinterpret_cast<const uintptr_t*>(pool+4);
        const auto count=*reinterpret_cast<const int32_t*>(pool+8);
        if(!objects || !tags || count<0 || count>0x7FFFFF)return {};
        const auto* pickups=reinterpret_cast<const uint8_t*>(native_entity_address(0x9788C0));
        int32_t last=-1;
        for(size_t i=0;i<620;++i) {
            const auto* entry=pickups+i*0x20;
            if(entry[0x1C]==0)continue; // SA PICKUP_NONE
            const auto object=*reinterpret_cast<const uintptr_t*>(entry+4);
            if(!object)continue;
            if(object<objects || (object-objects)%0x19C)return {};
            const auto index=(object-objects)/0x19C;
            if(index>=size_t(count) || (reinterpret_cast<const uint8_t*>(tags)[index]&0x80))return {};
            if((*reinterpret_cast<const uint8_t*>(object+0x36)&7)!=4)return {};
            const auto* coords=reinterpret_cast<const int16_t*>(entry+0x10);
            // Retail ELF 250110..250160 uses sub.s, mul.s, two add.s,
            // sqrt.s and a strict c.olt.s. Double squared-radius comparison
            // changes the result at representable rounded sphere boundaries.
            const float x=float(coords[0])/8.0f-sphere[0];
            const float y=float(coords[1])/8.0f-sphere[1];
            const float z=float(coords[2])/8.0f-sphere[2];
            const float xx=x*x,yy=y*y,zz=z*z;
            const float xy=xx+yy;
            if(std::sqrt(xy+zz)<sphere[3])last=*reinterpret_cast<const int16_t*>(object+0x22);
        }
        return last;
    } __except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
std::optional<ObjectiveActor> sa_live_objective_actor(int32_t handle) {
    if(!sa_executor_profile_matches())return {};
    __try {
        if(!ped_pool_profile())return {};
        const auto ped=entity_at(*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74490)),handle,0x7C4,3);
        if(!ped)return {};
        return ObjectiveActor{handle,ped};
    } __except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
std::optional<uintptr_t> sa_garage_ped_identity(int32_t handle) {
    if(!sa_executor_profile_matches())return {};
    __try {
        if(!ped_pool_profile())return {};
        const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74490));
        if(!pool)return {};
        const auto data=*reinterpret_cast<const uintptr_t*>(pool),flags=*reinterpret_cast<const uintptr_t*>(pool+4);
        const auto count=*reinterpret_cast<const int32_t*>(pool+8);
        if(!data || !flags || count<=0 || count>0x7FFFFF)return {};
        if(handle<0 || (uint32_t(handle)>>8)>=uint32_t(count))return uintptr_t(0);
        const auto tag=*reinterpret_cast<const uint8_t*>(flags+(uint32_t(handle)>>8));
        if((tag&0x80) || tag!=uint8_t(handle))return uintptr_t(0);
        const auto ped=entity_at(pool,handle,0x7C4,3);
        return ped?std::optional<uintptr_t>(ped):std::nullopt;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return {};}
}
bool sa_actor_reference_live(const ObjectiveActor& actor,const uintptr_t* cell) {
    __try {
        if(!cell || *cell!=actor.native_ped || !reference_profile())return false;
        const auto live=sa_live_objective_actor(actor.handle);
        return live && *live==actor && remote_reference_live(actor.native_ped,cell);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool sa_register_actor_reference(const ObjectiveActor& actor,uintptr_t* cell) {
    __try {
        if(!cell || *cell || !reference_profile())return false;
        const auto live=sa_live_objective_actor(actor.handle);
        if(!live || !(*live==actor))return false;
        *cell=actor.native_ped;
        native_register_reference(actor.native_ped,cell);
        // RegisterReference returns void and may exhaust its free list.
        // Only the actual registration, not the unchanged pointer, is authority.
        return sa_actor_reference_live(actor,cell);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool sa_release_actor_reference(uintptr_t* cell) {
    __try {
        if(!cell)return false;
        if(!*cell)return true; // native ResolveReferences already retired it
        if(!reference_profile())return false;
        const auto entity=physical_pointer(*cell);
        if(!entity)return false; // keep storage alive when cleanup is uncertain
        native_cleanup_reference(entity,cell);
        if(*cell!=entity || remote_reference_live(entity,cell))return false;
        *cell=0;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
std::optional<RouteActorSnapshot> sa_route_actor_snapshot(const ObjectiveActor& actor) {
    if(!sa_executor_profile_matches())return {};
    __try {
        const uint8_t control[]={0x90,0xE9,0x6A,0xD9,0xE1,0xFF,0xF6,0xC4,0x06,0x75,0x18,0x8B,0x81,0x30,0x05,0,0,0x83,0xF8,0x36,0x74,0x0D,0x83,0xF8,0x37,0x74,0x08,0x83,0xF8,0x3F,0x74,0x03,0xB0,0x01,0xC3,0x32,0xC0,0xC3};
        const uint8_t trampoline[]={0x8B,0x81,0x6C,0x04,0,0,0xE9,0x8B,0x26,0x1E,0};
        if(!ped_pool_profile() || !profile(0x5E3960,control) || !profile(0x4012D0,trampoline))return {};
        const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74490));
        const auto ped=entity_at(pool,actor.handle,0x7C4,3);
        if(!ped || ped!=actor.native_ped)return {};
        const auto matrix=*reinterpret_cast<const uintptr_t*>(ped+0x14);
        RouteActorSnapshot result;result.actor=actor;
        if(matrix && matrix>UINTPTR_MAX-0x3C)return {};
        std::memcpy(result.position.data(),reinterpret_cast<const void*>(matrix?matrix+0x30:ped+4),12);
        for(float value:result.position)if(!std::isfinite(value))return {};
        const auto health=*reinterpret_cast<const float*>(ped+0x540);
        if(!std::isfinite(health))return {};
        const auto state=*reinterpret_cast<const int32_t*>(ped+0x530);
        const auto flags=*reinterpret_cast<const uint8_t*>(ped+0x46D);
        // Exact reviewed native IsPedInControl predicate, plus source health gate.
        // InVehicle is deliberately separate: native control does not reject it.
        result.controllable=health>0 && !(flags&6) && state!=54 && state!=55 && state!=63;
        result.on_foot=!(flags&1);
        result.source_foot_controllable=result.controllable && state>=0 && state<39;
        result.source_seek_allowed=state!=11;
        result.source_driving=state==50;
        if(entity_at(pool,actor.handle,0x7C4,3)!=ped)return {};
        return result;
    } __except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
namespace {
bool route_vehicle_bus_profile() {
    // Retail CVehicle construction maps handling bIsBus to bit 1 of byte
    // +0x429, then reads that same bit. Unknown builds keep this fact absent.
    const uint8_t bus[]={0xF6,0x80,0xCC,0,0,0,0x02,0x74,0x07,0x80,0x8E,0x29,0x04,0,0,0x02,
        0xF6,0x80,0xCC,0,0,0,0x04,0x74,0x06,0x08,0x8E,0x29,0x04,0,0,0xF6,0x86,0x29,0x04,0,0,0x02};
    return profile(0x6B0C6E,bus);
}
bool route_ped_surface_profile() {
    // CPed::ProcessEntityCollision writes +0x568 = the accepted vehicle/object
    // contact and registers it as a CEntity weak reference. +0x584 is the
    // separate general contact pointer.
    const uint8_t surface[]={0x84,0xC0,0xDD,0xD8,0x0F,0x85,0xC7,0,0,0,0x8A,0x43,0x36,0x24,0x07,
        0x3C,0x02,0x74,0x08,0x3C,0x04,0x0F,0x85,0xB6,0,0,0,0x8D,0xBE,0x68,0x05,0,0,0x57,
        0x8B,0xCB,0x89,0x1F,0xE8,0xE4,0xF0,0xF8,0xFF};
    return profile(0x5E2A61,surface);
}
bool route_ped_bus_jacked_profile() {
    // Retail CPed code at 694444 sets bit 26 of its packed flag dword at
    // +0x46C; the same routine tests it at 694471 before taking its branch.
    // This is CPed::bBusJacked (+0x46F bit 2), not CVehicle car-jacking state.
    const uint8_t bus_jacked_write[]={0x81,0x8F,0x6C,0x04,0,0,0,0,0,0x04};
    const uint8_t bus_jacked_test[]={0xF7,0x87,0x6C,0x04,0,0,0,0,0,0x04};
    return profile(0x694444,bus_jacked_write) && profile(0x694471,bus_jacked_test);
}
bool route_pad_profile() {
    const uint8_t get_pad[]={0x8B,0x44,0x24,0x04,0x69,0xC0,0x34,0x01,0,0,0x05,0x58,0x34,0xB7,0,0xC3};
    return profile(0x53FB70,get_pad);
}
std::optional<bool> route_entity_on_screen(uintptr_t entity,uint16_t model) {
    // CEntity::GetIsOnScreen is a read-only bounding-sphere query. Its exact
    // entry bytes were read from the supplied retail PE; unknown builds leave
    // this fact unavailable instead of treating profile loss as off-screen.
    const uint8_t visible[]={0x83,0xEC,0x10,0x56,0x8B,0xF1,0x0F,0xBF,0x46,0x22,0x8B,0x0C,0x85,0xC8,0xB0,0xA9,0x00};
    if(!entity || model>=20000 || !profile(0x534540,visible))return {};
    const auto info=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xA9B0C8)+uintptr_t(model)*4);
    if(!info)return {};
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
    return native_route_effect_is_on_screen(entity);
#else
    return reinterpret_cast<bool(__thiscall*)(uintptr_t)>(native_entity_address(0x534540))(entity);
#endif
}
}
std::optional<bool> sa_route_player_controls_disabled() {
    if(!sa_executor_profile_matches())return {};
    __try {
        if(!route_pad_profile())return {};
        const auto focus=*reinterpret_cast<const uint8_t*>(native_entity_address(0xB7CD74));
        if(focus>=2)return {};
        const auto expected_pad=native_entity_address(0xB73458+uintptr_t(focus)*0x134);
        const auto pad=native_route_effect_get_pad(focus);
        if(!pad || pad!=expected_pad || pad>UINTPTR_MAX-0x110)return {};
        const auto disabled=*reinterpret_cast<const uint16_t*>(pad+0x10E);
        if(*reinterpret_cast<const uint8_t*>(native_entity_address(0xB7CD74))!=focus ||
           native_route_effect_get_pad(focus)!=pad ||
           *reinterpret_cast<const uint16_t*>(pad+0x10E)!=disabled)return {};
        return disabled!=0;
    } __except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
std::optional<RouteVehicleSnapshot> sa_route_vehicle_snapshot(int32_t handle) {
    if(!sa_executor_profile_matches())return {};
    __try {
        if(!entity_profile(false) || !ped_pool_profile())return {};
        const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74494));
        const auto vehicle=entity_at(pool,handle,0xA18,2);
        if(!vehicle || remote_vehicle_key(vehicle)!=handle)return {};

        const auto type=*reinterpret_cast<const int32_t*>(vehicle+0x590);
        if(type<0 || type>11)return {};
        const auto matrix=*reinterpret_cast<const uintptr_t*>(vehicle+0x14);
        if(matrix && matrix>UINTPTR_MAX-0x30)return {};
        const auto* position=reinterpret_cast<const void*>(matrix?matrix+0x30:vehicle+4);
        RouteVehicleSnapshot result;
        result.handle=handle;result.entity=vehicle;result.generation=uint8_t(handle);
        result.native_class=type;
        std::memcpy(result.position.data(),position,sizeof(result.position));
        for(float coordinate:result.position)if(!std::isfinite(coordinate))return {};
        result.health=*reinterpret_cast<const float*>(vehicle+0x4C0);
        if(!std::isfinite(result.health))return {};
        result.alive=result.health>0.0f;
        const auto* native_speed=reinterpret_cast<const float*>(vehicle+0x44);
        const float speed_x=native_speed[0],speed_y=native_speed[1],speed_z=native_speed[2];
        std::optional<float> speed_magnitude_sq_3d;
        if(std::isfinite(speed_x) && std::isfinite(speed_y)) {
            const float speed_sq=speed_x*speed_x+speed_y*speed_y;
            if(std::isfinite(speed_sq))result.speed_magnitude_sq_2d=speed_sq;
        }
        if(std::isfinite(speed_x) && std::isfinite(speed_y) && std::isfinite(speed_z))
            result.move_speed=std::array<float,3>{speed_x,speed_y,speed_z};
        if(std::isfinite(speed_x) && std::isfinite(speed_y) && std::isfinite(speed_z)) {
            const float speed_sq=speed_x*speed_x+speed_y*speed_y+speed_z*speed_z;
            if(std::isfinite(speed_sq)) {
                result.speed_magnitude_sq_3d=speed_sq;
                speed_magnitude_sq_3d=speed_sq;
            }
        }
        const auto native_status=int32_t(*reinterpret_cast<const uint8_t*>(vehicle+0x36)>>3);
        const auto disabled_native_status=source_vehicle_status(13); // source STATUS_PLAYER_DISABLED
        if(disabled_native_status)result.player_disabled=native_status==*disabled_native_status;
        const auto native_door_lock=*reinterpret_cast<const int32_t*>(vehicle+0x4F8);
        // Source Vehicle.cpp rejects lock ordinals 2, 4, 5 and 9 and does not
        // inspect its CPed argument. LCS Script3.cpp's LOCK_CAR_DOORS forwards
        // the source enum integer directly to native command 0x020A; the raw
        // 32-bit field is CVehicle::m_eDoorLock at SDK offset +0x4F8. Preserve
        // source values 0..9 instead of substituting SA CanPedOpenLocks rules.
        if(native_door_lock>=0 && native_door_lock<=9)
            result.source_can_ped_open_locks=native_door_lock!=2 && native_door_lock!=4 &&
                native_door_lock!=5 && native_door_lock!=9;
        result.is_boat=type==5; // Retail native CVehicle::m_nVehicleClass enum.
        result.is_bike=type==9 || type==10; // Same tested model class as car_is_bike.

        if(route_vehicle_bus_profile())
            result.is_bus=((*reinterpret_cast<const uint8_t*>(vehicle+0x429)&0x02u)!=0);
        const auto passengers=*reinterpret_cast<const uint8_t*>(vehicle+0x484);
        const auto getting_in_flags=*reinterpret_cast<const uint8_t*>(vehicle+0x486);
        const auto capacity=*reinterpret_cast<const uint8_t*>(vehicle+0x488);
        if(passengers>8 || capacity>8 || passengers>capacity)return {};
        result.num_passengers=passengers;result.max_passengers=capacity;
        result.getting_in_flags=getting_in_flags;
        const auto driver=*reinterpret_cast<const uintptr_t*>(vehicle+0x460);
        if(driver) {
            const auto key=remote_ped_key(driver);if(key<0)return {};
            result.driver_ped=driver;result.driver_handle=key;
        }
        for(size_t i=0;i<result.passenger_peds.size();++i) {
            const auto ped=*reinterpret_cast<const uintptr_t*>(vehicle+0x464+i*4);
            if(!ped)continue;
            const auto key=remote_ped_key(ped);if(key<0)return {};
            result.passenger_peds[i]=ped;result.passenger_handles[i]=key;
        }
        // CVehicle::IsUpsideDown in the supplied SDK is exactly matrix up.z
        // <= -0.9. That native method dereferences m_matrix, so a missing
        // matrix makes the fact unknown rather than inventing a fallback.
        float matrix_up_z=0.0f;
        std::array<float,3> turn_speed_snapshot{};
        bool turn_speed_read=false;
        if(matrix) {
            const auto up_z=*reinterpret_cast<const float*>(matrix+0x28);
            if(!std::isfinite(up_z))return {};
            matrix_up_z=up_z;
            result.upside_down=up_z<=-0.9f;

            // Literal source CVehicle::CanPedExitCar(false): only matrix up.z
            // selects the tilted path. A tilted boat exits immediately; other
            // vehicles then use distinct strict/inclusive speed and turn gates.
            const bool tilted=up_z>0.1f || up_z< -0.1f;
            if(tilted && result.is_boat) {
                result.source_can_ped_exit_car=true;
            } else if(speed_magnitude_sq_3d) {
                const float speed_sq=*speed_magnitude_sq_3d;
                const bool speed_allows_turn_check=tilted ? speed_sq<=0.005f : speed_sq<0.005f;
                if(!speed_allows_turn_check) {
                    result.source_can_ped_exit_car=false;
                } else {
                    const auto* native_turn=reinterpret_cast<const float*>(vehicle+0x50);
                    std::memcpy(turn_speed_snapshot.data(),native_turn,sizeof(turn_speed_snapshot));
                    turn_speed_read=true;
                    if(std::isfinite(turn_speed_snapshot[0]) && std::isfinite(turn_speed_snapshot[1]) &&
                       std::isfinite(turn_speed_snapshot[2])) {
                        const auto within_turn_limit=[&](float value) {
                            const float magnitude=std::fabs(value);
                            return tilted ? magnitude<=0.01f : magnitude<0.01f;
                        };
                        result.source_can_ped_exit_car=within_turn_limit(turn_speed_snapshot[0]) &&
                            within_turn_limit(turn_speed_snapshot[1]) && within_turn_limit(turn_speed_snapshot[2]);
                    }
                }
            }
        }
        const auto model=*reinterpret_cast<const uint16_t*>(vehicle+0x22);
        result.on_screen=route_entity_on_screen(vehicle,model);

        const auto* position_after=reinterpret_cast<const float*>(matrix?matrix+0x30:vehicle+4);
        const float health_after=*reinterpret_cast<const float*>(vehicle+0x4C0);
        if(remote_vehicle_key(vehicle)!=handle ||
           *reinterpret_cast<const uintptr_t*>(vehicle+0x14)!=matrix ||
           *reinterpret_cast<const int32_t*>(vehicle+0x590)!=type ||
           *reinterpret_cast<const uint16_t*>(vehicle+0x22)!=model ||
           !std::isfinite(health_after) || health_after!=result.health ||
           *reinterpret_cast<const uint8_t*>(vehicle+0x484)!=passengers ||
           *reinterpret_cast<const uint8_t*>(vehicle+0x486)!=getting_in_flags ||
           *reinterpret_cast<const uint8_t*>(vehicle+0x488)!=capacity ||
           *reinterpret_cast<const uintptr_t*>(vehicle+0x460)!=driver ||
           *reinterpret_cast<const int32_t*>(vehicle+0x4F8)!=native_door_lock)return {};
        const auto* speed_after=reinterpret_cast<const float*>(vehicle+0x44);
        if(std::isfinite(speed_x) && std::isfinite(speed_y) &&
           (!std::isfinite(speed_after[0]) || !std::isfinite(speed_after[1]) ||
            speed_after[0]!=speed_x || speed_after[1]!=speed_y))return {};
        if(std::isfinite(speed_z) && (!std::isfinite(speed_after[2]) || speed_after[2]!=speed_z))return {};
        if(turn_speed_read && std::memcmp(reinterpret_cast<const void*>(vehicle+0x50),
                turn_speed_snapshot.data(),sizeof(turn_speed_snapshot))!=0)return {};
        if((*reinterpret_cast<const uint8_t*>(vehicle+0x36)>>3)!=native_status)return {};
        for(size_t axis=0;axis<3;++axis)
            if(!std::isfinite(position_after[axis]) || position_after[axis]!=result.position[axis])return {};
        if(result.is_bus &&
           ((*reinterpret_cast<const uint8_t*>(vehicle+0x429)&0x02u)!=0)!=*result.is_bus)return {};
        if(matrix) {
            const auto up_z=*reinterpret_cast<const float*>(matrix+0x28);
            if(!std::isfinite(up_z) || (up_z<=-0.9f)!=result.upside_down.value_or(false) ||
               (result.source_can_ped_exit_car && up_z!=matrix_up_z))return {};
        }
        for(size_t i=0;i<result.passenger_peds.size();++i)
            if(*reinterpret_cast<const uintptr_t*>(vehicle+0x464+i*4)!=result.passenger_peds[i] ||
               (result.passenger_peds[i] && remote_ped_key(result.passenger_peds[i])!=result.passenger_handles[i]))return {};
        if(driver && remote_ped_key(driver)!=result.driver_handle)return {};
        if(remote_vehicle_key(vehicle)!=handle)return {};
        return result;
    } __except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
namespace {
constexpr float route_kill_exit_speed_sq_limit=0.02f*0.02f;
// sdk-CVehicle.h places CAutoPilot at +0x390. sdk-CAutoPilot.h places the
// one-byte mission and cruise-speed fields at +0x2A and +0x40 respectively.
constexpr uintptr_t route_autopilot_mission_offset=0x390+0x2A;
constexpr uintptr_t route_autopilot_cruise_speed_offset=0x390+0x40;
// Both the source AutoPilot enum and Plugin-SDK's native enum start at NONE=0.
// Native declaration: https://raw.githubusercontent.com/DK22Pac/plugin-sdk/master/plugin_sa/game_sa/enums/eCarMission.h
constexpr uint8_t route_native_mission_none=0;

struct RouteKillExitStopReceipt {bool ok=false,entered=false;DWORD exception=0;};

bool route_kill_exit_identity(int32_t actor_handle,uintptr_t actor_ped,
        int32_t vehicle_handle,uintptr_t vehicle) {
    if(!sa_executor_profile_matches() || !entity_profile(false) || !ped_pool_profile() ||
       actor_handle<0 || !actor_ped || vehicle_handle<0 || !vehicle ||
       remote_vehicle_key(vehicle)!=vehicle_handle || remote_ped_key(actor_ped)!=actor_handle)return false;
    const auto ped_pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74490));
    const auto live_ped=entity_at(ped_pool,actor_handle,0x7C4,3);
    if(live_ped!=actor_ped)return false;
    const auto flags=*reinterpret_cast<const uint8_t*>(actor_ped+0x46D);
    return (flags&1u)!=0 && *reinterpret_cast<const uintptr_t*>(actor_ped+0x58C)==vehicle &&
        remote_vehicle_key(vehicle)==vehicle_handle;
}
bool route_kill_entry_identity(int32_t actor_handle,uintptr_t actor_ped,
        int32_t vehicle_handle,uintptr_t vehicle) {
    if(!sa_executor_profile_matches() || !entity_profile(false) || !ped_pool_profile() ||
       actor_handle<0 || !actor_ped || vehicle_handle<0 || !vehicle ||
       remote_vehicle_key(vehicle)!=vehicle_handle || remote_ped_key(actor_ped)!=actor_handle)return false;
    const auto ped_pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74490));
    const auto live_ped=entity_at(ped_pool,actor_handle,0x7C4,3);
    if(live_ped!=actor_ped)return false;
    // This branch follows the actor after its native exit. Do not use CPed's
    // old vehicle pointer (+0x58C) as provenance for the source-owned car.
    const auto health=*reinterpret_cast<const float*>(actor_ped+0x540);
    const auto state=*reinterpret_cast<const int32_t*>(actor_ped+0x530);
    return (*reinterpret_cast<const uint8_t*>(actor_ped+0x46D)&1u)==0 &&
        std::isfinite(health) && health>0.0f && state!=54 && state!=55 && state!=63 &&
        remote_vehicle_key(vehicle)==vehicle_handle;
}
bool route_kill_exit_authorized(bool(*authorized)(void*),void* context) {
    if(!authorized)return false;
    __try {return authorized(context);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

// This leaf contains only scalar locals so its SEH boundary can safely contain
// a stale native pointer at the two direct field writes.
void invoke_route_kill_exit_stop(int32_t actor_handle,uintptr_t actor_ped,
        int32_t vehicle_handle,uintptr_t vehicle,bool(*authorized)(void*),void* context,
        RouteKillExitStopReceipt* receipt) {
    __try {
        if(!receipt || !route_kill_exit_identity(actor_handle,actor_ped,vehicle_handle,vehicle) ||
           !route_kill_exit_authorized(authorized,context) ||
           !route_kill_exit_identity(actor_handle,actor_ped,vehicle_handle,vehicle))return;
        receipt->entered=true;
        *reinterpret_cast<uint8_t*>(vehicle+route_autopilot_cruise_speed_offset)=0;
        if(!route_kill_exit_identity(actor_handle,actor_ped,vehicle_handle,vehicle) ||
           !route_kill_exit_authorized(authorized,context) ||
           !route_kill_exit_identity(actor_handle,actor_ped,vehicle_handle,vehicle))return;
        *reinterpret_cast<uint8_t*>(vehicle+route_autopilot_mission_offset)=route_native_mission_none;
        if(!route_kill_exit_identity(actor_handle,actor_ped,vehicle_handle,vehicle) ||
           !route_kill_exit_authorized(authorized,context) ||
           !route_kill_exit_identity(actor_handle,actor_ped,vehicle_handle,vehicle) ||
           *reinterpret_cast<const uint8_t*>(vehicle+route_autopilot_cruise_speed_offset)!=0 ||
           *reinterpret_cast<const uint8_t*>(vehicle+route_autopilot_mission_offset)!=route_native_mission_none)return;
        receipt->ok=true;
    } __except(EXCEPTION_EXECUTE_HANDLER){receipt->exception=GetExceptionCode();}
}
// This leaf deliberately performs one byte write only. The source task may
// then issue ENTER_DRIVER against the remembered source vehicle.
void invoke_route_kill_entry_stop(int32_t actor_handle,uintptr_t actor_ped,
        int32_t vehicle_handle,uintptr_t vehicle,bool(*authorized)(void*),void* context,
        RouteKillExitStopReceipt* receipt) {
    __try {
        if(!receipt || !route_kill_entry_identity(actor_handle,actor_ped,vehicle_handle,vehicle) ||
           !route_kill_exit_authorized(authorized,context) ||
           !route_kill_entry_identity(actor_handle,actor_ped,vehicle_handle,vehicle))return;
        receipt->entered=true;
        *reinterpret_cast<uint8_t*>(vehicle+route_autopilot_cruise_speed_offset)=0;
        if(!route_kill_entry_identity(actor_handle,actor_ped,vehicle_handle,vehicle) ||
           !route_kill_exit_authorized(authorized,context) ||
           !route_kill_entry_identity(actor_handle,actor_ped,vehicle_handle,vehicle) ||
           *reinterpret_cast<const uint8_t*>(vehicle+route_autopilot_cruise_speed_offset)!=0)return;
        receipt->ok=true;
    } __except(EXCEPTION_EXECUTE_HANDLER){receipt->exception=GetExceptionCode();}
}
}
NativeResult sa_route_stop_vehicle_for_kill_exit(const RouteActorVehicleSnapshot& expected_actor,
        const RouteVehicleSnapshot& expected_vehicle,bool(*authorized)(void*),void* authorization_context) {
    NativeResult result;
    if(!authorized || !sa_executor_profile_matches() || !expected_actor.alive || expected_actor.attached ||
       !expected_actor.in_vehicle || expected_actor.actor.handle<0 || !expected_actor.actor.native_ped ||
       expected_actor.current_vehicle_handle!=expected_vehicle.handle ||
       expected_actor.current_vehicle_entity!=expected_vehicle.entity ||
       expected_actor.current_vehicle_generation!=expected_vehicle.generation ||
       expected_vehicle.handle<0 || !expected_vehicle.entity || !expected_vehicle.alive ||
       !expected_vehicle.speed_magnitude_sq_3d ||
       !(*expected_vehicle.speed_magnitude_sq_3d<route_kill_exit_speed_sq_limit)) {
        result.diagnostic="ANY_MEANS vehicle stop lacks a live in-vehicle actor, matching vehicle identity, source speed, or authorization";
        return result;
    }
    const auto actor_before=sa_route_actor_vehicle_snapshot(expected_actor.actor);
    const auto vehicle_before=sa_route_vehicle_snapshot(expected_vehicle.handle);
    const auto actor_matches=[&](const RouteActorVehicleSnapshot& live) {
        return live.actor==expected_actor.actor && live.alive && !live.attached && live.in_vehicle &&
            live.native_state==expected_actor.native_state && live.native_move_state==expected_actor.native_move_state &&
            live.native_type==expected_actor.native_type && live.created_by==expected_actor.created_by &&
            live.health==expected_actor.health && live.current_vehicle_handle==expected_vehicle.handle &&
            live.current_vehicle_entity==expected_vehicle.entity &&
            live.current_vehicle_generation==expected_vehicle.generation;
    };
    const auto vehicle_matches=[&](const RouteVehicleSnapshot& live) {
        return live.handle==expected_vehicle.handle && live.entity==expected_vehicle.entity &&
            live.generation==expected_vehicle.generation && live.alive && live.speed_magnitude_sq_3d &&
            *live.speed_magnitude_sq_3d<route_kill_exit_speed_sq_limit;
    };
    if(!actor_before || !vehicle_before || !actor_matches(*actor_before) || !vehicle_matches(*vehicle_before) ||
       !route_kill_exit_authorized(authorized,authorization_context)) {
        result.diagnostic="ANY_MEANS vehicle stop preflight refused a stale actor, occupancy, vehicle, speed, or source authorization";
        return result;
    }

    RouteKillExitStopReceipt receipt;
    invoke_route_kill_exit_stop(expected_actor.actor.handle,expected_actor.actor.native_ped,
        expected_vehicle.handle,expected_vehicle.entity,authorized,authorization_context,&receipt);
    result.native_entered=receipt.entered;
    if(!receipt.ok) {
        result.diagnostic=receipt.entered?
            "ANY_MEANS vehicle stop partially entered; do not replay before source objective repair":
            "ANY_MEANS vehicle stop refused before its first autopilot write";
        return result;
    }

    const auto actor_after=sa_route_actor_vehicle_snapshot(expected_actor.actor);
    const auto vehicle_after=sa_route_vehicle_snapshot(expected_vehicle.handle);
    if(!actor_after || !vehicle_after || !actor_matches(*actor_after) || !vehicle_matches(*vehicle_after) ||
       !route_kill_exit_authorized(authorized,authorization_context)) {
        result.diagnostic="ANY_MEANS vehicle stop wrote autopilot fields, but actor, occupancy, vehicle, speed, or source authorization changed before receipt";
        return result;
    }
    result.executed=true;
    return result;
}
NativeResult sa_route_stop_vehicle_for_kill_entry(const RouteActorVehicleSnapshot& expected_actor,
        const RouteVehicleSnapshot& expected_car,bool(*authorized)(void*),void* authorization_context) {
    NativeResult result;
    if(!authorized || !sa_executor_profile_matches() || !expected_actor.alive ||
       expected_actor.in_vehicle || expected_actor.actor.handle<0 || !expected_actor.actor.native_ped ||
       expected_car.handle<0 || !expected_car.entity) {
        result.diagnostic="ANY_MEANS vehicle entry lacks a live on-foot actor, source-owned car identity, or authorization";
        return result;
    }
    const auto actor_before=sa_route_actor_vehicle_snapshot(expected_actor.actor);
    const auto car_before=sa_route_vehicle_snapshot(expected_car.handle);
    const auto actor_matches=[&](const RouteActorVehicleSnapshot& live) {
        return live.actor==expected_actor.actor && live.alive && !live.in_vehicle;
    };
    const auto car_matches=[&](const RouteVehicleSnapshot& live) {
        return live.handle==expected_car.handle && live.entity==expected_car.entity &&
            live.generation==expected_car.generation;
    };
    if(!actor_before || !car_before || !actor_matches(*actor_before) || !car_matches(*car_before) ||
       !route_kill_exit_authorized(authorized,authorization_context)) {
        result.diagnostic="ANY_MEANS vehicle entry preflight refused a stale on-foot actor, source-owned car, or authorization";
        return result;
    }

    RouteKillExitStopReceipt receipt;
    invoke_route_kill_entry_stop(expected_actor.actor.handle,expected_actor.actor.native_ped,
        expected_car.handle,expected_car.entity,authorized,authorization_context,&receipt);
    result.native_entered=receipt.entered;
    if(!receipt.ok) {
        result.diagnostic=receipt.entered?
            "ANY_MEANS vehicle entry partially stopped cruise speed; do not replay before source objective repair":
            "ANY_MEANS vehicle entry refused before its cruise-speed write";
        return result;
    }

    const auto actor_after=sa_route_actor_vehicle_snapshot(expected_actor.actor);
    const auto car_after=sa_route_vehicle_snapshot(expected_car.handle);
    if(!actor_after || !car_after || !actor_matches(*actor_after) || !car_matches(*car_after) ||
       !route_kill_exit_authorized(authorized,authorization_context)) {
        result.diagnostic="ANY_MEANS vehicle entry wrote cruise speed, but actor, car identity, or source authorization changed before receipt";
        return result;
    }
    result.executed=true;
    return result;
}
bool sa_route_clear_car_jacked(const RouteVehicleSnapshot& expected,
        bool(*authorized)(void*),void* authorization_context) {
    if(!authorized || !sa_executor_profile_matches())return false;
    const auto live=sa_route_vehicle_snapshot(expected.handle);
    if(!live || live->entity!=expected.entity || live->generation!=expected.generation)return false;
    __try {
        if(!authorized(authorization_context) || !entity_profile(false) ||
           remote_vehicle_key(expected.entity)!=expected.handle)return false;
        // Same approved CVehicle bit as EntityCommand::car_being_jacked:
        // SDK vehicle flags start at 0x428, third byte bit3.
        *reinterpret_cast<uint8_t*>(expected.entity+0x42A)&=uint8_t(~0x08u);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
namespace {
// POD result so the SEH region owns no C++ object with unwinding.
int vehicle_slot_state(int32_t handle) {
    // 0 unavailable, 1 absent, 2 slot occupied by this exact generation
    __try {
        if(!entity_profile(false) || !ped_pool_profile())return 0;
        const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74494));
        if(!pool)return 0;
        const auto data=*reinterpret_cast<const uintptr_t*>(pool),flags=*reinterpret_cast<const uintptr_t*>(pool+4);
        const auto count=*reinterpret_cast<const int32_t*>(pool+8);
        if(!data || !flags || count<=0 || count>0x7FFFFF)return 0;
        if(handle<0 || (uint32_t(handle)>>8)>=uint32_t(count))return 1;
        const auto tag=*reinterpret_cast<const uint8_t*>(flags+(uint32_t(handle)>>8));
        if((tag&0x80) || tag!=uint8_t(handle))return 1;
        return 2;
    } __except(EXCEPTION_EXECUTE_HANDLER){return 0;}
}
}
RouteVehicleLookup sa_route_vehicle_lookup(int32_t handle) {
    RouteVehicleLookup result;
    if(!sa_executor_profile_matches())return result;
    const int state=vehicle_slot_state(handle);
    if(state==1){result.status=VehicleLookupStatus::absent;return result;}
    if(state!=2)return result;
    result.snapshot=sa_route_vehicle_snapshot(handle);
    if(result.snapshot)result.status=VehicleLookupStatus::present;
    return result;
}
std::optional<RouteActorVehicleSnapshot> sa_route_actor_vehicle_snapshot(const ObjectiveActor& actor) {
    if(!sa_executor_profile_matches())return {};
    const auto pose=sa_route_actor_snapshot(actor);
    if(!pose)return {};
    __try {
        if(!route_ped_surface_profile() || !route_ped_bus_jacked_profile())return {};
        const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74490));
        const auto ped=entity_at(pool,actor.handle,0x7C4,3);
        if(!ped || ped!=actor.native_ped)return {};
        const auto health=*reinterpret_cast<const float*>(ped+0x540);
        const auto state=*reinterpret_cast<const int32_t*>(ped+0x530);
        const auto move_state=*reinterpret_cast<const int32_t*>(ped+0x534);
        const auto native_type=*reinterpret_cast<const int32_t*>(ped+0x598);
        const auto flags=*reinterpret_cast<const uint8_t*>(ped+0x46D);
        // CPed's packed SDK flags begin at +0x46C. bBusJacked is the third
        // bit in the fourth byte (+0x46F, mask 0x04); this is ped state, not
        // CVehicle::bIsBeingCarJacked.
        const auto bus_jacked_flags=*reinterpret_cast<const uint8_t*>(ped+0x46F);
        const auto stationary_flags=*reinterpret_cast<const uint8_t*>(ped+0x46E);
        if(!std::isfinite(health))return {};
        RouteActorVehicleSnapshot result;
        result.actor=actor;result.position=pose->position;result.native_state=state;
        result.native_move_state=move_state;result.native_type=native_type;
        if(const auto source_state=original_cop_state(state))
            result.source_entering_car=*source_state==56 || *source_state==58;
        result.health=health;result.created_by=*reinterpret_cast<const uint8_t*>(ped+0x484);
        result.attached=*reinterpret_cast<const uintptr_t*>(ped+0xFC)!=0;
        result.standing_on_entity=*reinterpret_cast<const uintptr_t*>(ped+0x568);
        result.controllable=pose->controllable;
        result.alive=health>0.0f && state!=54 && state!=55 && state!=63;
        result.in_vehicle=(flags&1u)!=0;
        result.bus_jacked=(bus_jacked_flags&0x04u)!=0;
        result.kinda_stay_in_same_place=(stationary_flags&0x40u)!=0;
        if(result.in_vehicle) {
            const auto vehicle=*reinterpret_cast<const uintptr_t*>(ped+0x58C);
            const auto handle=remote_vehicle_key(vehicle);
            if(!vehicle || handle<0)return {};
            result.current_vehicle_handle=handle;
            result.current_vehicle_entity=vehicle;
            result.current_vehicle_generation=uint8_t(handle);
        }
        for(int32_t player_index=0;player_index<2;++player_index) {
            const auto player=native_entity_address(0xB7CD98+uintptr_t(player_index)*0x190);
            if(player_ped_at(player)==ped)result.is_player=true;
        }
        const auto model=*reinterpret_cast<const uint16_t*>(ped+0x22);
        result.on_screen=route_entity_on_screen(ped,model);
        // SA exposes the raw CPed::m_nCreatedBy byte (+0x484), but it is not
        // semantically identical to LCS's mission-character flag.
        result.mission_character.reset();
        const auto matrix=*reinterpret_cast<const uintptr_t*>(ped+0x14);
        const auto* position_after=reinterpret_cast<const float*>(matrix?matrix+0x30:ped+4);
        for(size_t axis=0;axis<3;++axis)
            if(!std::isfinite(position_after[axis]) || position_after[axis]!=result.position[axis])return {};
        bool is_player_after=false;
        for(int32_t player_index=0;player_index<2;++player_index) {
            const auto player=native_entity_address(0xB7CD98+uintptr_t(player_index)*0x190);
            if(player_ped_at(player)==ped)is_player_after=true;
        }
        if(is_player_after!=result.is_player || entity_at(pool,actor.handle,0x7C4,3)!=ped ||
           *reinterpret_cast<const float*>(ped+0x540)!=health ||
           *reinterpret_cast<const int32_t*>(ped+0x530)!=state ||
           *reinterpret_cast<const int32_t*>(ped+0x534)!=move_state ||
           *reinterpret_cast<const int32_t*>(ped+0x598)!=native_type ||
           *reinterpret_cast<const uint8_t*>(ped+0x46D)!=flags ||
           *reinterpret_cast<const uint8_t*>(ped+0x46F)!=bus_jacked_flags ||
           *reinterpret_cast<const uint8_t*>(ped+0x46E)!=stationary_flags ||
           *reinterpret_cast<const uint8_t*>(ped+0x484)!=result.created_by ||
           (*reinterpret_cast<const uintptr_t*>(ped+0xFC)!=0)!=result.attached ||
           *reinterpret_cast<const uintptr_t*>(ped+0x568)!=result.standing_on_entity)return {};
        if(result.in_vehicle && *reinterpret_cast<const uintptr_t*>(ped+0x58C)!=result.current_vehicle_entity)return {};
        return result;
    } __except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
bool sa_register_vehicle_reference(int32_t handle,uintptr_t* stable_cell) {
    __try {
        if(!stable_cell || !entity_profile(false) || !reference_profile())return false;
        const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74494));
        const auto vehicle=entity_at(pool,handle,0xA18,2);
        if(!vehicle || remote_vehicle_key(vehicle)!=handle)return false;
        if(*stable_cell)return *stable_cell==vehicle && remote_reference_live(vehicle,stable_cell) && remote_vehicle_key(vehicle)==handle;
        *stable_cell=vehicle;
        native_register_reference(vehicle,stable_cell);
        if(*stable_cell==vehicle && remote_vehicle_key(vehicle)==handle && remote_reference_live(vehicle,stable_cell))return true;
        // Only a cell proved absent from the object's reference list can be
        // reset here. An uncertain weak cell remains quarantined by its owner.
        if(*stable_cell==vehicle && !remote_reference_live(vehicle,stable_cell))*stable_cell=0;
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool sa_vehicle_reference_live(const RouteVehicleSnapshot& expected,const uintptr_t* stable_cell) {
    __try {
        return stable_cell && expected.handle>=0 && expected.entity &&
            expected.generation==uint8_t(expected.handle) && *stable_cell==expected.entity &&
            entity_profile(false) && reference_profile() && remote_vehicle_key(expected.entity)==expected.handle &&
            remote_reference_live(expected.entity,stable_cell);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool sa_release_vehicle_reference(const RouteVehicleSnapshot& expected,uintptr_t* stable_cell) {
    __try {
        if(!stable_cell)return false;
        if(!*stable_cell)return true;
        if(expected.handle<0 || !expected.entity || expected.generation!=uint8_t(expected.handle) ||
           *stable_cell!=expected.entity || !entity_profile(false) || !reference_profile() ||
           remote_vehicle_key(expected.entity)!=expected.handle)return false;
        if(remote_reference_live(expected.entity,stable_cell))native_cleanup_reference(expected.entity,stable_cell);
        else *stable_cell=0;
        return !*stable_cell && !remote_reference_live(expected.entity,stable_cell);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
std::optional<bool> sa_active_walking_task(const ObjectiveActor& actor,const CapturedScriptTask& owned) {
    if(!sa_executor_profile_matches())return {};
    const auto live=sa_live_objective_actor(actor.handle);
    if(!live || !(*live==actor) || owned.slot>=128 || owned.record.event || !owned.record.task ||
       owned.record.ped!=actor.native_ped || (owned.record.opcode!=0x05D3 && owned.record.opcode!=0x05F5 && owned.record.opcode!=0x0A2E && owned.record.opcode!=0x05D9 && owned.record.opcode!=0x05E2))return {};
    const auto bank=sa_script_task_snapshot();
    if(!bank)return {};
    const auto& record=(*bank)[owned.slot];
    if(record.ped!=owned.record.ped || record.opcode!=owned.record.opcode || record.task!=owned.record.task || record.event ||
       (record.status!=1 && record.status!=2 && record.status!=4 && record.status!=5 && record.status!=6))return {};
    __try {
        const uint8_t active[]={0x33,0xC0,0x83,0x3C,0x81,0x00,0x75,0x09,0x40,0x83,0xF8,0x05};
        const uint8_t allocator[]={0x8B,0x0D,0xA8,0x44,0xB7,0,0xE9,0x55,0xFF,0xFF,0xFF};
        const uint8_t stride[]={0xC1,0xE0,0x07};
        if(!profile(0x681720,active) || !sa_task_allocator_profile_matches() || !profile(0x61A55A,stride))return {};
        if(!sa_task_slot_live(uintptr_t(record.task)))return {};
        const auto intelligence=*reinterpret_cast<const uintptr_t*>(actor.native_ped+0x47C);
        if(!intelligence)return {};
        uintptr_t task=0;
        // Match GetActiveTask: only the first non-null primary root. Finding
        // the accepted job in a suspended lower-priority root is insufficient.
        for(size_t slot=0;slot<5;++slot) {
            task=*reinterpret_cast<const uintptr_t*>(intelligence+4+slot*4);
            if(task)break;
        }
        uintptr_t previous=0;
        for(size_t depth=0;task && depth<32;++depth) {
            if(!sa_task_slot_live(task))return {};
            if(*reinterpret_cast<const uintptr_t*>(task+4)!=previous)return {};
            if(task==record.task)return true;
            const auto table=*reinterpret_cast<const uintptr_t*>(task);
            if(!table)return {};
            const auto getter=*reinterpret_cast<const uintptr_t*>(table+8);
            const uint8_t complex[]={0x8B,0x41,0x08,0xC3};
            const uint8_t simple[]={0x33,0xC0,0xC3};
            // Read reviewed getter semantics without calling a virtual method
            // from an arbitrary or foreign task. Modified getters defer.
            if(profile(getter,simple))return false;
            if(!profile(getter,complex))return {};
            previous=task;task=*reinterpret_cast<const uintptr_t*>(task+8);
        }
        return task?std::optional<bool>{}:std::optional<bool>{false};
    } __except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
// Original 804: pPed->m_leader==pLeader && !pPed->bWaitForLeaderToComeCloser.
// SA adaptation: the LCS-only wait flag is not rebuilt; the current native
// group membership decides. Both handles must be live peds (the original
// asserts on a missing one) and the group read must succeed, so an unavailable
// read is never reported as "not in group". Read-only: no group/task changes.
static NativeResult char_in_chars_group(const NativeCall& call) {
    NativeResult result;
    const auto ped=sa_live_objective_actor(call.inputs[0]);
    const auto leader=sa_live_objective_actor(call.inputs[1]);
    if(!ped || !leader){result.diagnostic="Group query character identity unavailable";return result;}
    const auto group=sa_ped_group_snapshot(*ped);
    if(!group){result.diagnostic="Native group membership unavailable";return result;}
    const auto ped_after=sa_live_objective_actor(call.inputs[0]),leader_after=sa_live_objective_actor(call.inputs[1]);
    if(!ped_after || !leader_after || !(*ped_after==*ped) || !(*leader_after==*leader)) {
        result.diagnostic="Group query identity changed during read";return result;
    }
    result.executed=true;
    result.condition=group->member && !group->leader && group->leader_ped && group->leader_ped==leader->native_ped;
    return result;
}
NativeResult sa_execute_entity_command(const NativeCall& call) {
    NativeResult result;
    if(!call.source_entity_command || call.source_statistic || call.source_frenzy_headshots || call.source_environment || call.source_garage || call.source_traffic || call.opcode || call.text || call.text_position || call.replacement_text || call.mission_flag_reference || call.suppress_previous_brief ||
       call.source_wait_in_car || call.source_wander_direction || !sa_executor_profile_matches()) {
        result.diagnostic="Source entity operation mode/profile mismatch";return result;
    }
    const auto action=*call.source_entity_command;
    const bool predicate=action==EntityCommand::car_is_boat || action==EntityCommand::car_is_bike || action==EntityCommand::car_being_jacked || action==EntityCommand::boat_in_water ||
        action==EntityCommand::char_drowning || action==EntityCommand::first_person_weapon_camera ||
        action==EntityCommand::photograph_taken || action==EntityCommand::credits_finished || action==EntityCommand::save_game_finished || action==EntityCommand::char_wander_path ||
        action==EntityCommand::char_leaving_vehicle_to_die || action==EntityCommand::char_in_chars_group;
    if(call.predicate!=predicate){result.diagnostic="Source entity predicate layout mismatch";return result;}
    if(action==EntityCommand::char_in_chars_group) {
        if(call.inputs.size()!=2 || call.outputs!=0){result.diagnostic="Source entity operand layout mismatch";return result;}
        return char_in_chars_group(call);
    }
    size_t inputs=1,outputs=0;
    switch(action) {
    case EntityCommand::restart_critical_mission:inputs=4;break;
    case EntityCommand::photograph_taken:case EntityCommand::credits_finished:case EntityCommand::activate_save_menu:
    case EntityCommand::save_game_finished:case EntityCommand::enable_control_camera:case EntityCommand::blow_up_rc_buggy:
    case EntityCommand::cancel_remote_mode:inputs=0;break;
    case EntityCommand::create_remote_car:inputs=6;outputs=1;break;
    case EntityCommand::override_remote_car:inputs=2;break;
    case EntityCommand::clear_area_objects:inputs=6;break;
    case EntityCommand::car_move_speed:case EntityCommand::car_turn_speed:case EntityCommand::object_turn_speed:
    case EntityCommand::set_car_orientation:case EntityCommand::set_object_orientation:inputs=4;break;
    case EntityCommand::get_car_orientation:case EntityCommand::get_object_orientation:outputs=3;break;
    case EntityCommand::get_out_of_jail_free:case EntityCommand::player_fireproof:
    case EntityCommand::player_max_health:case EntityCommand::player_max_armour:inputs=2;break;
    case EntityCommand::radar_brightness:inputs=2;break;
    case EntityCommand::char_locomotion_group:inputs=2;break;
    case EntityCommand::set_pickup_value:inputs=2;break;
    case EntityCommand::get_pickup_value:outputs=1;break;
    case EntityCommand::get_player_havoc:outputs=1;break;
    case EntityCommand::reset_player_havoc:break;
    case EntityCommand::attach_object_car:case EntityCommand::heli_goto:inputs=5;break;
    case EntityCommand::char_wander_path:inputs=5;break;
    case EntityCommand::detach_object_car:break;
    case EntityCommand::car_ram_car:inputs=2;break;
    case EntityCommand::char_attach_object:inputs=8;break;
    case EntityCommand::car_collision:outputs=1;break;
    case EntityCommand::car_wreck:inputs=2;break; // source car + translated RC identity
    case EntityCommand::boat_in_water:case EntityCommand::heli_rotor_full_speed:break;
    case EntityCommand::car_is_boat:case EntityCommand::car_is_bike:case EntityCommand::car_being_jacked:break;
    case EntityCommand::random_car_unchecked:inputs=4;outputs=1;break;
    case EntityCommand::random_car_no_save:inputs=5;outputs=1;break;
    case EntityCommand::car_alarm:case EntityCommand::object_render_damaged:break;
    case EntityCommand::char_drowning:case EntityCommand::first_person_weapon_camera:
    case EntityCommand::char_leaving_vehicle_to_die:
    case EntityCommand::player_clip_ammo:case EntityCommand::remove_car_boot:break;
    default:result.diagnostic="Unknown source entity operation";return result;
    }
    if(call.inputs.size()!=inputs || call.outputs!=outputs){result.diagnostic="Source entity operand layout mismatch";return result;}
    EntityReceipt receipt;invoke_entity(&call,&receipt);
    result.executed=receipt.ok;result.native_entered=receipt.entered;result.condition=receipt.condition;result.values=receipt.values;
    if(!result.executed){std::ostringstream detail;detail<<"Source entity operation "<<int(action)<<" rejected; exception=0x"<<std::hex<<receipt.exception;result.diagnostic=detail.str();}
    return result;
}
bool sa_cop_creation_profile_matches() {
    if(!sa_executor_profile_matches())return false;
    __try {
        const uint8_t creation[]={0x56,0x8b,0xf1,0x8b,0x86,0x98,0x05,0x00,0x00,0x85,0xc0,0x8a,0x4c,0x24,0x08,0x88,0x8e,0x84,0x04,0x00,0x00,0x74,0x05,0x83,0xf8,0x01,0x75,0x04,0x6a,0xfe,0xeb,0x14,0x80,0xf9,0x02,0x74,0x0d,0x8b,0x86,0x9c,0x05,0x00,0x00,0x0f,0xbe,0x48,0x32,0x51,0xeb,0x02,0x6a,0xff,0x8b,0x8e,0x7c,0x04,0x00,0x00,0xe8,0x31,0xc3,0x01,0x00,0x80,0xbe,0x84,0x04,0x00,0x00,0x02,0x75,0x4c,0x8b,0x15,0x2c,0xc9,0x86,0x00,0x8b,0x8e,0x7c,0x04,0x00,0x00,0x52,0xe8,0xb6,0xc3,0x01,0x00,0xa1,0x2c,0xc9,0x86,0x00,0x8b,0x8e,0x7c,0x04,0x00,0x00,0x50,0xe8,0x95,0xc3,0x01,0x00,0x8b,0x86,0x98,0x05,0x00,0x00,0x85,0xc0,0x74,0x1f,0x83,0xf8,0x01,0x74,0x1a,0x8b,0xb6,0x7c,0x04,0x00,0x00,0xc7,0x86,0xc8,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xc7,0x86,0xc4,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x5e,0xc2,0x04,0x00};
        const uint8_t decision[]={0x8b,0x81,0xb4,0x00,0x00,0x00,0x85,0xc0,0x75,0x0c,0x8b,0x44,0x24,0x04,0x89,0x81,0xb8,0x00,0x00,0x00,0xeb,0x1c,0x8b,0x54,0x24,0x04,0x85,0xd2,0x75,0x0e,0x89,0x81,0xb8,0x00,0x00,0x00,0x89,0x91,0xb4,0x00,0x00,0x00,0xeb,0x06,0x89,0x91,0xb4,0x00,0x00,0x00,0x83,0xb9,0xb4,0x00,0x00,0x00,0x07,0x75,0x14,0xc7,0x81,0xc8,0x00,0x00,0x00,0x00,0x00,0xa0,0x40,0xc7,0x81,0xc4,0x00,0x00,0x00,0x0f,0x00,0x00,0x00,0xc2,0x04,0x00};
        const uint8_t seeing[]={0x8b,0x44,0x24,0x04,0x89,0x81,0xbc,0x00,0x00,0x00,0xc2,0x04,0x00};
        const uint8_t hearing[]={0x8b,0x44,0x24,0x04,0x89,0x81,0xc0,0x00,0x00,0x00,0xc2,0x04,0x00};
        return ped_pool_profile() && profile(0x5E47E0,creation) && profile(0x600B50,decision) &&
            profile(0x600BE0,seeing) && profile(0x600BF0,hearing);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
namespace {
struct CopCreationReceipt {bool entered=false,ok=false;uint32_t exception=0;};
// The reviewed SetCharCreatedBy(2) effect depends on native ped type: player
// types 0/1 receive decision maker -2 and keep +C4/+C8; every other type
// receives -1 and clears both. Random cops additionally require RANDOM_CHAR.
void invoke_char_creation(const NativeThreatSnapshot* before,int32_t type,bool require_random,CopCreationReceipt* receipt) {
    __try {
        if(!sa_cop_creation_profile_matches())return;
        const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74490));
        const auto ped=entity_at(pool,before->actor.handle,0x7C4,3);
        if(!ped || ped!=before->actor.native_ped || !before->intelligence ||
            *reinterpret_cast<const uintptr_t*>(ped+0x47C)!=before->intelligence ||
            *reinterpret_cast<const int32_t*>(ped+0x598)!=type ||
            (require_random && *reinterpret_cast<const uint8_t*>(ped+0x484)!=1))return;
        const auto brain=before->intelligence;
        const auto old_decision=*reinterpret_cast<const int32_t*>(brain+0xB4);
        const auto old_c4=*reinterpret_cast<const uint32_t*>(brain+0xC4),old_c8=*reinterpret_cast<const uint32_t*>(brain+0xC8);
        const auto sense=*reinterpret_cast<const uint32_t*>(native_entity_address(0x86C92C));
        const bool player_type=type==0 || type==1;
        // Mark entry before invoking the real setter: a fault after writing the
        // creation byte must not be retried or treated as a preflight refusal.
        receipt->entered=true;native_cop_mission_creation(ped);
        if(entity_at(pool,before->actor.handle,0x7C4,3)!=ped ||
            *reinterpret_cast<const uintptr_t*>(ped+0x47C)!=brain ||
            *reinterpret_cast<const int32_t*>(ped+0x598)!=type ||
            *reinterpret_cast<const uint8_t*>(ped+0x484)!=2)return;
        if(*reinterpret_cast<const int32_t*>(brain+(old_decision?0xB4:0xB8))!=(player_type?-2:-1) ||
            *reinterpret_cast<const uint32_t*>(brain+0xBC)!=sense ||
            *reinterpret_cast<const uint32_t*>(brain+0xC0)!=sense ||
            *reinterpret_cast<const uint32_t*>(brain+0xC4)!=(player_type?old_c4:0) ||
            *reinterpret_cast<const uint32_t*>(brain+0xC8)!=(player_type?old_c8:0))return;
        receipt->ok=true;
    } __except(EXCEPTION_EXECUTE_HANDLER){receipt->exception=GetExceptionCode();}
}
}
NativeResult sa_cop_set_mission_creation(const NativeThreatSnapshot& before) {
    CopCreationReceipt receipt;invoke_char_creation(&before,6,true,&receipt);
    NativeResult result;result.executed=receipt.ok;result.native_entered=receipt.entered;
    if(!receipt.ok)result.diagnostic=receipt.entered?"Cop creation entered without complete verified effect; do not replay":"Cop creation preflight refused";
    return result;
}
namespace {
void invoke_char_threat_suppression(const NativeThreatSnapshot* before,int32_t type,CopCreationReceipt* receipt) {
    __try {
        if(!sa_executor_profile_matches() || !ped_pool_profile())return;
        const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74490));
        const auto ped=entity_at(pool,before->actor.handle,0x7C4,3);
        if(!ped || ped!=before->actor.native_ped || !before->intelligence ||
            *reinterpret_cast<const uintptr_t*>(ped+0x47C)!=before->intelligence ||
            *reinterpret_cast<const int32_t*>(ped+0x598)!=type ||
            *reinterpret_cast<const uint8_t*>(ped+0x484)!=2)return;
        const auto brain=before->intelligence;
        for(size_t i=0;i<3;++i)if(*reinterpret_cast<const uint8_t*>(brain+0x244+i)!=before->responses[i])return;
        receipt->entered=true; // synchronous side effects; no new task/objective
        for(size_t i=0;i<3;++i)*reinterpret_cast<uint8_t*>(brain+0x244+i)=0;
        if(entity_at(pool,before->actor.handle,0x7C4,3)!=ped ||
            *reinterpret_cast<const uintptr_t*>(ped+0x47C)!=brain ||
            *reinterpret_cast<const uint8_t*>(ped+0x484)!=2)return;
        for(size_t i=0;i<3;++i)if(*reinterpret_cast<const uint8_t*>(brain+0x244+i))return;
        receipt->ok=true;
    } __except(EXCEPTION_EXECUTE_HANDLER){receipt->exception=GetExceptionCode();}
}
}
NativeResult sa_cop_suppress_threats(const NativeThreatSnapshot& before) {
    NativeResult result;
    const auto fresh=sa_char_threat_snapshot(before.actor);
    if(!fresh || fresh->intelligence!=before.intelligence || fresh->responses!=before.responses) {
        result.diagnostic="Cop threat policy changed before suppression";return result;
    }
    CopCreationReceipt receipt;invoke_char_threat_suppression(&before,6,&receipt);
    result.executed=receipt.ok;result.native_entered=receipt.entered;
    if(!receipt.ok)result.diagnostic=receipt.entered?"Cop threat suppression entered without complete receipt; do not replay":"Cop threat suppression preflight refused";
    return result;
}
std::optional<NativeThreatSnapshot> sa_char_threat_snapshot(const ObjectiveActor& actor) {
    if(!sa_executor_profile_matches())return {};
    __try {
        const uint8_t writer[]={0x8a,0x51,0x0a,0x8b,0x44,0x24,0x04,0x8b,0x80,0x7c,0x04,0x00,0x00,0x53,0x8a,0x59,0x09,0x8a,0x49,0x08,0x05,0x38,0x02,0x00,0x00,0x88,0x58,0x0d,0x88,0x48,0x0c,0x88,0x50,0x0e};
        if(!ped_pool_profile() || !profile(0x6337A0,writer))return {};
        const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74490));
        const auto ped=entity_at(pool,actor.handle,0x7C4,3);
        if(!ped || ped!=actor.native_ped)return {};
        const auto intelligence=*reinterpret_cast<const uintptr_t*>(ped+0x47C);
        if(!intelligence)return {};
        NativeThreatSnapshot result;result.actor=actor;result.intelligence=intelligence;
        for(size_t i=0;i<3;++i) {
            result.responses[i]=*reinterpret_cast<const uint8_t*>(intelligence+0x244+i);
            if(result.responses[i]>1)return {}; // native bool representation
        }
        if(entity_at(pool,actor.handle,0x7C4,3)!=ped ||
                *reinterpret_cast<const uintptr_t*>(ped+0x47C)!=intelligence)return {};
        return result;
    } __except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
std::optional<uint32_t> sa_mission_ped_count() {
    if(!sa_executor_profile_matches())return {};
    __try {
        // Genuine CREATE_CHAR load/increment/store establishes counter ABI;
        // MaxNumberOfPedsInUse at 8D2538 is a different population variable.
        const uint8_t accounting[]={0xa1,0x24,0xec,0xc0,0x00,0x8b,0x0d,0x90,0x44,0xb7,0x00,0x83,0xc4,0x04,0x40,0x57,0xa3,0x24,0xec,0xc0,0x00};
        if(!profile(0x46772B,accounting))return {};
        const auto counter=reinterpret_cast<const volatile uint32_t*>(native_entity_address(0xC0EC24));
        const auto before=*counter,after=*counter;
        if(before!=after)return {};
        return before; // zero and UINT_MAX are observations, not invented defaults
    } __except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
namespace {
void invoke_char_count(const NativeThreatSnapshot* before,int32_t type,uint32_t expected,CopCreationReceipt* receipt) {
    __try {
        const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74490));
        const auto ped=entity_at(pool,before->actor.handle,0x7C4,3);
        if(!ped || ped!=before->actor.native_ped || !before->intelligence ||
            *reinterpret_cast<const uintptr_t*>(ped+0x47C)!=before->intelligence ||
            *reinterpret_cast<const int32_t*>(ped+0x598)!=type ||
            *reinterpret_cast<const uint8_t*>(ped+0x484)!=2 || expected==UINT32_MAX)return;
        for(size_t i=0;i<3;++i)if(*reinterpret_cast<const uint8_t*>(before->intelligence+0x244+i))return;
        auto* counter=reinterpret_cast<volatile uint32_t*>(native_entity_address(0xC0EC24));
        if(*counter!=expected)return;
        receipt->entered=true;*counter=expected+1;
        if(*counter!=expected+1 || entity_at(pool,before->actor.handle,0x7C4,3)!=ped ||
            *reinterpret_cast<const uintptr_t*>(ped+0x47C)!=before->intelligence ||
            *reinterpret_cast<const uint8_t*>(ped+0x484)!=2)return;
        receipt->ok=true;
    } __except(EXCEPTION_EXECUTE_HANDLER){receipt->exception=GetExceptionCode();}
}
}
NativeResult sa_cop_increment_mission_count(const NativeThreatSnapshot& before,uint32_t expected) {
    NativeResult result;
    const auto count=sa_mission_ped_count();const auto actor=sa_char_threat_snapshot(before.actor);
    if(!count || *count!=expected || expected==UINT32_MAX || !actor ||
            actor->intelligence!=before.intelligence || actor->responses!=std::array<uint8_t,3>{}) {
        result.diagnostic="Cop mission accounting preflight refused";return result;
    }
    CopCreationReceipt receipt;invoke_char_count(&before,6,expected,&receipt);
    result.executed=receipt.ok;result.native_entered=receipt.entered;
    if(!receipt.ok)result.diagnostic=receipt.entered?"Cop accounting entered without complete receipt; do not replay":"Cop accounting boundary changed";
    return result;
}
std::optional<MissionCleanupSnapshot> sa_mission_cleanup_snapshot() {
    if(!sa_executor_profile_matches())return {};
    __try {
        // This executable has an on-disk trampoline. Both entry and target are
        // reviewed; accepting any JMP would trust unrelated mod code blindly.
        const uint8_t entry[]={0xe9,0x5b,0x25,0x10,0x01};
        const uint8_t writer[]={0x33,0xd2,0x8b,0xc1,0x80,0x38,0x00,0x74,0x0d,0x83,0xc0,0x08,0x42,0x66,0x83,0xfa,0x4b,0x72,0xf1,0xc2,0x08,0x00,0x8b,0x54,0x24,0x04,0x89,0x50,0x04,0x8a,0x54,0x24,0x08,0x88,0x10,0xfe,0x81,0x58,0x02,0x00,0x00,0xc2,0x08,0x00};
        if(!profile(0x4637E0,entry) || !profile(0x1565D40,writer))return {};
        MissionCleanupSnapshot result,second;
        const auto table=reinterpret_cast<const void*>(native_entity_address(0xA90850));
        std::memcpy(result.bytes.data(),table,result.bytes.size());
        std::memcpy(second.bytes.data(),table,second.bytes.size());
        if(result.bytes!=second.bytes)return {};
        size_t occupied=0;
        for(size_t i=0;i<MissionCleanupSnapshot::slots;++i)if(result.type(i))++occupied;
        if(occupied!=result.bytes[MissionCleanupSnapshot::count_offset])return {};
        return result; // a full, consistent table is valid; reservation refuses it
    } __except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
namespace {
struct CopCleanupReceipt {CopCreationReceipt effect;MissionCleanupSnapshot after;};
void invoke_char_cleanup(const NativeThreatSnapshot* actor,int32_t type,const MissionCleanupSnapshot* before,
        uint32_t expected_count,CopCleanupReceipt* receipt) {
    __try {
        const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74490));
        const auto ped=entity_at(pool,actor->actor.handle,0x7C4,3);
        if(!ped || ped!=actor->actor.native_ped || !actor->intelligence ||
            *reinterpret_cast<const uintptr_t*>(ped+0x47C)!=actor->intelligence ||
            *reinterpret_cast<const int32_t*>(ped+0x598)!=type ||
            *reinterpret_cast<const uint8_t*>(ped+0x484)!=2 ||
            *reinterpret_cast<const uint32_t*>(native_entity_address(0xC0EC24))!=expected_count)return;
        for(size_t i=0;i<3;++i)if(*reinterpret_cast<const uint8_t*>(actor->intelligence+0x244+i))return;
        const auto table=reinterpret_cast<const void*>(native_entity_address(0xA90850));
        if(std::memcmp(table,before->bytes.data(),before->bytes.size()))return;
        receipt->effect.entered=true;native_cop_cleanup(actor->actor.handle);
        std::memcpy(receipt->after.bytes.data(),table,receipt->after.bytes.size());
        if(entity_at(pool,actor->actor.handle,0x7C4,3)!=ped ||
            *reinterpret_cast<const uintptr_t*>(ped+0x47C)!=actor->intelligence ||
            *reinterpret_cast<const uint8_t*>(ped+0x484)!=2 ||
            *reinterpret_cast<const uint32_t*>(native_entity_address(0xC0EC24))!=expected_count)return;
        receipt->effect.ok=true;
    } __except(EXCEPTION_EXECUTE_HANDLER){receipt->effect.exception=GetExceptionCode();}
}
}
NativeResult sa_cop_register_cleanup(const NativeThreatSnapshot& actor,const MissionCleanupSnapshot& before,uint32_t count) {
    NativeResult result;
    const auto reservation=reserve_char_cleanup(before,actor.actor.handle);
    const auto fresh=sa_mission_cleanup_snapshot();const auto population=sa_mission_ped_count();
    const auto threats=sa_char_threat_snapshot(actor.actor);
    if(!reservation || !fresh || fresh->bytes!=before.bytes || !population || *population!=count ||
            !threats || threats->intelligence!=actor.intelligence || threats->responses!=std::array<uint8_t,3>{}) {
        result.diagnostic="Cop cleanup preflight refused";return result;
    }
    CopCleanupReceipt receipt;invoke_char_cleanup(&actor,6,&before,count,&receipt);
    result.native_entered=receipt.effect.entered;
    result.executed=receipt.effect.ok && char_cleanup_inserted(before,receipt.after,*reservation);
    if(!result.executed)result.diagnostic=result.native_entered?"Cop cleanup entered without exact insertion receipt; do not replay":"Cop cleanup boundary changed";
    return result;
}
NativeResult sa_accept_random_cop(const NativeCopSnapshot& selected,bool mission,const std::function<bool()>& commit) {
    NativeResult result;
    const auto fresh=sa_cop_snapshot(selected.actor);
    const auto threats=sa_char_threat_snapshot(selected.actor);
    const auto count=sa_mission_ped_count();
    const auto cleanup=mission?sa_mission_cleanup_snapshot():std::optional<MissionCleanupSnapshot>{};
    if(!commit || !fresh || !threats || !count || *count==UINT32_MAX || !sa_cop_creation_profile_matches() ||
        fresh->native_model!=selected.native_model || fresh->native_type!=6 || fresh->created_by!=1 ||
        fresh->native_state!=selected.native_state || fresh->position!=selected.position ||
        fresh->controllable!=selected.controllable || fresh->removed || fresh->fading || fresh->group.member ||
        (mission && (!cleanup || !reserve_char_cleanup(*cleanup,selected.actor.handle)))) {
        result.diagnostic="Random cop acceptance preflight refused before selection/promotion";return result;
    }
    // Source stores LastRandomPedId before creation/threat/count/cleanup. The
    // caller must preserve this order even if a later effect fails partially.
    try {if(!commit()){result.diagnostic="Random cop selection commit refused";return result;}}
    catch(...){result.native_entered=true;result.diagnostic="Random cop selection commit failed; do not replay";return result;}
    result.native_entered=true;
    auto effect=sa_cop_set_mission_creation(*threats);
    if(!effect.executed){effect.native_entered=true;return effect;}
    const auto promoted=sa_char_threat_snapshot(selected.actor);
    if(!promoted || promoted->intelligence!=threats->intelligence){result.diagnostic="Cop intelligence changed after promotion";return result;}
    effect=sa_cop_suppress_threats(*promoted);
    if(!effect.executed){effect.native_entered=true;return effect;}
    effect=sa_cop_increment_mission_count(*promoted,*count);
    if(!effect.executed){effect.native_entered=true;return effect;}
    if(mission) {
        effect=sa_cop_register_cleanup(*promoted,*cleanup,*count+1);
        if(!effect.executed){effect.native_entered=true;return effect;}
    }
    result.executed=true;result.values[0]=selected.actor.handle;
    return result;
}
namespace {
// POD-only SEH copy of the facts retail 1494 reads, plus the native type and
// creation owner that the promotion receipts must verify. No eligibility here.
bool copy_unchecked_char(const ObjectiveActor* actor,NativeUncheckedCharSnapshot* out) {
    __try {
        if(!sa_executor_profile_matches() || !ped_pool_profile())return false;
        const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74490));
        const auto ped=entity_at(pool,actor->handle,0x7C4,3);
        if(!ped || ped!=actor->native_ped)return false;
        const auto matrix=*reinterpret_cast<const uintptr_t*>(ped+0x14);
        if(matrix && matrix>UINTPTR_MAX-0x3C)return false;
        std::memcpy(out->position.data(),reinterpret_cast<const void*>(matrix?matrix+0x30:ped+4),12);
        out->actor=*actor;
        out->native_type=*reinterpret_cast<const int32_t*>(ped+0x598);
        out->created_by=*reinterpret_cast<const uint8_t*>(ped+0x484);
        out->removed=(*reinterpret_cast<const uint8_t*>(ped+0x1D)&8)!=0;
        out->fading=(*reinterpret_cast<const uint8_t*>(ped+0x470)&8)!=0;
        out->native_state=*reinterpret_cast<const int32_t*>(ped+0x530);
        out->has_vehicle=*reinterpret_cast<const uintptr_t*>(ped+0x58C)!=0 ||
            (*reinterpret_cast<const uint8_t*>(ped+0x46D)&1)!=0;
        return entity_at(pool,actor->handle,0x7C4,3)==ped;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
std::optional<NativeUncheckedCharSnapshot> sa_unchecked_char_snapshot(const ObjectiveActor& actor) {
    NativeUncheckedCharSnapshot result;
    if(!copy_unchecked_char(&actor,&result))return {};
    return result;
}
namespace {
// POD-only SEH read of SA's own stuck evidence for one live ped (CPedIntelligence at ped+0x47C):
// m_AnotherStaticCounter (+0x274) and the global limit the native go-to tasks compare it with.
bool copy_char_stuck(const ObjectiveActor* actor,int32_t* counter,int32_t* limit) {
    __try {
        if(!sa_executor_profile_matches() || !ped_pool_profile())return false;
        const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74490));
        const auto ped=entity_at(pool,actor->handle,0x7C4,3);
        if(!ped || ped!=actor->native_ped)return false;
        const auto intelligence=*reinterpret_cast<const uintptr_t*>(ped+0x47C);
        if(!intelligence || *reinterpret_cast<const uintptr_t*>(intelligence)!=ped)return false;
        *counter=*reinterpret_cast<const int32_t*>(intelligence+0x274);
        *limit=*reinterpret_cast<const int32_t*>(native_entity_address(0x86C938));
        return entity_at(pool,actor->handle,0x7C4,3)==ped && *reinterpret_cast<const uintptr_t*>(ped+0x47C)==intelligence;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
// 1398 IS_CHAR_STUCK. LCS: WAITSTATE_STUCK after 50 frames in control, walking, colliding and not moving.
// SA's equivalent evidence is CPedIntelligence::ProcessStaticCounter (0x605650): while a walk/run/sprint
// go-to task is active and the ped is blocked by an entity, m_AnotherStaticCounter counts frames spent
// within 0.25 m of the blocked position, and SA's own go-to/follow tasks treat counter > the global at
// 0x86C938 (30 in retail data) as "stuck". The predicate reports exactly that native condition; the LCS
// 50-frame/5-second wait state is not rebuilt. nullopt = the native state could not be read (never FALSE).
std::optional<bool> sa_char_stuck(const ObjectiveActor& actor) {
    static const uint8_t producer[]={0x53,0x55,0x56,0x8B,0xF1,0x57,0x8D,0x4E,0x04,0xE8,0x72,0xC3,0x07,0x00,0x33,0xED,0x3B,0xC5,0x0F,0x84,0x5C,0x01,0x00,0x00};
    static const uint8_t consumer[]={0x8B,0x88,0x74,0x02,0x00,0x00,0x3B,0x0D,0x38,0xC9,0x86,0x00};
    __try {
        if(!profile(0x605650,producer) || !profile(0x657650,consumer))return {};
    } __except(EXCEPTION_EXECUTE_HANDLER){return {};}
    int32_t counter=0,limit=0;
    if(!copy_char_stuck(&actor,&counter,&limit) || limit<=0 || limit>10000 || counter<0)return {};
    return counter>limit;
}
NativeResult sa_accept_unchecked_char(const NativeUncheckedCharSnapshot& selected,bool mission,const std::function<bool()>& commit) {
    NativeResult result;
    const auto fresh=sa_unchecked_char_snapshot(selected.actor);
    const auto threats=sa_char_threat_snapshot(selected.actor);
    const auto count=sa_mission_ped_count();
    const auto cleanup=mission?sa_mission_cleanup_snapshot():std::optional<MissionCleanupSnapshot>{};
    const auto expected_cleanup=cleanup?expected_source_char_cleanup(*cleanup,selected.actor.handle):std::optional<MissionCleanupSnapshot>{};
    // No type/creation/control/group gate: retail 1494 promotes civilians,
    // cops, gang members, already-mission peds and the player alike.
    if(!commit || !fresh || !threats || !count || *count==UINT32_MAX || !sa_cop_creation_profile_matches() ||
        fresh->native_type!=selected.native_type || fresh->position!=selected.position ||
        fresh->removed || fresh->fading || (mission && !expected_cleanup)) {
        result.diagnostic="Unchecked character acceptance preflight refused before selection/promotion";return result;
    }
    const auto type=selected.native_type;
    // 2503F8 stores LastRandomPedId before CharCreatedBy (250400), the threat
    // flag, the population counter (250420) and cleanup (250434).
    try {if(!commit()){result.diagnostic="Unchecked character selection commit refused";return result;}}
    catch(...){result.native_entered=true;result.diagnostic="Unchecked character selection commit failed; do not replay";return result;}
    result.native_entered=true;
    CopCreationReceipt creation;invoke_char_creation(&*threats,type,false,&creation);
    if(!creation.ok){result.diagnostic="Unchecked character creation entered without complete verified effect; do not replay";return result;}
    const auto promoted=sa_char_threat_snapshot(selected.actor);
    if(!promoted || promoted->intelligence!=threats->intelligence){result.diagnostic="Character intelligence changed after promotion";return result;}
    CopCreationReceipt suppression;invoke_char_threat_suppression(&*promoted,type,&suppression);
    if(!suppression.ok){result.diagnostic="Unchecked character threat suppression incomplete; do not replay";return result;}
    const auto quiet=sa_char_threat_snapshot(selected.actor);
    if(!quiet || quiet->intelligence!=promoted->intelligence || quiet->responses!=std::array<uint8_t,3>{}) {
        result.diagnostic="Character threat state changed before accounting";return result;
    }
    CopCreationReceipt accounting;invoke_char_count(&*quiet,type,*count,&accounting);
    if(!accounting.ok){result.diagnostic="Unchecked character accounting incomplete; do not replay";return result;}
    if(mission) {
        const auto current=sa_mission_cleanup_snapshot();
        if(!current || current->bytes!=cleanup->bytes){result.diagnostic="Mission cleanup changed during promotion; do not replay";return result;}
        CopCleanupReceipt registered;invoke_char_cleanup(&*quiet,type,&*cleanup,*count+1,&registered);
        // A full source list is a genuine silent no-insertion, not a failure.
        if(!registered.effect.ok || registered.after.bytes!=expected_cleanup->bytes) {
            result.diagnostic="Unchecked character cleanup without exact source receipt; do not replay";return result;
        }
    }
    result.executed=true;result.values[0]=selected.actor.handle;
    return result;
}
namespace {
bool copy_peds_descending(std::vector<ObjectiveActor>* result) {
    __try {
        if(!sa_executor_profile_matches() || !ped_pool_profile())return false;
        const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74490));
        if(!pool)return false;
        const auto data=*reinterpret_cast<const uintptr_t*>(pool),flags=*reinterpret_cast<const uintptr_t*>(pool+4);
        const auto count=*reinterpret_cast<const int32_t*>(pool+8);
        if(count<0 || count>0x7FFFFF || (count && (!data || !flags)))return false;
        for(int32_t i=count-1;i>=0;--i) {
            const auto tag=*reinterpret_cast<const uint8_t*>(flags+i);
            if(tag&0x80)continue;
            const int32_t handle=(i<<8)|tag;
            const auto ped=entity_at(pool,handle,0x7C4,3);
            if(!ped)return false;
            result->push_back({handle,ped});
        }
        if(*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74490))!=pool ||
            *reinterpret_cast<const uintptr_t*>(pool)!=data || *reinterpret_cast<const uintptr_t*>(pool+4)!=flags ||
            *reinterpret_cast<const int32_t*>(pool+8)!=count)return false;
        for(const auto& actor:*result)if(entity_at(pool,actor.handle,0x7C4,3)!=actor.native_ped)return false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
namespace {
struct PhoneRandomReceipt {bool entered=false,ok=false;uint32_t value=0;};
void invoke_phone_random(PhoneRandomReceipt* receipt) {
    __try {
        const uint8_t random[]={0xe8,0x1a,0x60,0x00,0x00,0x8b,0x48,0x14,0x69,0xc9,0xfd,0x43,0x03,0x00,0x81,0xc1,0xc3,0x9e,0x26,0x00,0x89,0x48,0x14,0x8b,0xc1,0xc1,0xe8,0x10,0x25,0xff,0x7f,0x00,0x00,0xc3};
        if(!sa_phone_random_ready() || !profile(0x821B1E,random))return;
        receipt->entered=true;receipt->value=native_phone_random_sample();
        receipt->ok=receipt->value<=0x7FFF;
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}
}
bool sa_phone_random_ready() {
    if(!sa_executor_profile_matches())return false;
    const DWORD saved_error=GetLastError();bool ready=false;
    __try {
        const uint8_t helper[]={0x53,0x56,0xff,0x15,0x94,0x80,0x85,0x00,0xff,0x35,0x10,0x31,0x8e,0x00,0x8b,0xd8,0xff,0x15,0xe0,0x81,0x85,0x00,0x8b,0xf0,0x85,0xf6,0x75,0x49,0x68,0x88,0x00,0x00,0x00,0x6a,0x01,0xe8,0xb1,0xc8,0xff,0xff,0x8b,0xf0,0x85,0xf6,0x59,0x59,0x74,0x2d,0x56,0xff,0x35,0x10,0x31,0x8e,0x00,0xff,0x15,0xe4,0x81,0x85,0x00,0x85,0xc0,0x74,0x1c,0xc7,0x46,0x54,0x10,0x35,0x8e,0x00,0xc7,0x46,0x14,0x01,0x00,0x00,0x00,0xff,0x15,0xe8,0x81,0x85,0x00,0x83,0x4e,0x04,0xff,0x89,0x06,0xeb,0x08,0x6a,0x10,0xe8,0x30,0xc9,0xff,0xff,0x59,0x53,0xff,0x15,0x9c,0x80,0x85,0x00,0x8b,0xc6,0x5e,0x5b,0xc3};
        const uint8_t random[]={0xe8,0x1a,0x60,0x00,0x00,0x8b,0x48,0x14,0x69,0xc9,0xfd,0x43,0x03,0x00,0x81,0xc1,0xc3,0x9e,0x26,0x00,0x89,0x48,0x14,0x8b,0xc1,0xc1,0xe8,0x10,0x25,0xff,0x7f,0x00,0x00,0xc3};
        if(profile(0x827B3D,helper) && profile(0x821B1E,random)) {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
            ready=native_phone_tls_ready();
#else
            const auto kernel=GetModuleHandleA("KERNEL32.dll");
            const bool imports=kernel &&
                *reinterpret_cast<const uintptr_t*>(0x858094)==reinterpret_cast<uintptr_t>(GetProcAddress(kernel,"GetLastError")) &&
                *reinterpret_cast<const uintptr_t*>(0x85809C)==reinterpret_cast<uintptr_t>(GetProcAddress(kernel,"SetLastError")) &&
                *reinterpret_cast<const uintptr_t*>(0x8581E0)==reinterpret_cast<uintptr_t>(GetProcAddress(kernel,"TlsGetValue"));
            const DWORD index=*reinterpret_cast<const volatile DWORD*>(0x8E3110);
            if(imports && index!=TLS_OUT_OF_INDEXES) {
                const auto data=reinterpret_cast<uintptr_t>(TlsGetValue(index));
                if(data && !(data&3) && *reinterpret_cast<const DWORD*>(data)==GetCurrentThreadId()) {
                    const volatile auto seed=*reinterpret_cast<const uint32_t*>(data+0x14);
                    ready=*reinterpret_cast<const volatile DWORD*>(0x8E3110)==index &&
                        reinterpret_cast<uintptr_t>(TlsGetValue(index))==data;
                    (void)seed;
                }
            }
#endif
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){ready=false;}
    // TLS lookup changes last-error; do not disturb the game or another mod.
    SetLastError(saved_error);return ready;
}
NativeResult sa_phone_random_sample() {
    PhoneRandomReceipt receipt;invoke_phone_random(&receipt);
    NativeResult result;result.executed=receipt.ok;result.native_entered=receipt.entered;
    if(receipt.ok)result.values[0]=int32_t(receipt.value);
    else result.diagnostic=receipt.entered?"Native phone RNG entered without valid sample; do not replay":"Native phone RNG ABI unavailable";
    return result;
}
bool sa_phone_render_profile_matches() {
    if(!sa_executor_profile_matches())return false;
    __try {
        const uint8_t copy[]={0x8b,0x11,0x8b,0x44,0x24,0x04,0x89,0x10,0x8b,0x51,0x04,0x89,0x50,0x04,0x8b,0x51,0x08,0x89,0x50,0x08,0x8b,0x51,0x10,0x89,0x50,0x10,0x8b,0x51,0x14,0x89,0x50,0x14,0x8b,0x51,0x18,0x89,0x50,0x18,0x8b,0x51,0x20,0x89,0x50,0x20,0x8b,0x51,0x24,0x89,0x50,0x24,0x8b,0x51,0x28,0x89,0x50,0x28,0x8b,0x51,0x30,0x89,0x50,0x30,0x8b,0x51,0x34,0x89,0x50,0x34,0x8b,0x49,0x38,0x50,0x89,0x48,0x38,0xe8,0xe0,0x6a,0x25,0x00,0x59,0xc2,0x04,0x00};
        const uint8_t matrix_update[]={0x8b,0x44,0x24,0x04,0x81,0x60,0x0c,0xfc,0xff,0xfd,0xff,0xc3};
        const uint8_t entity_frame[]={0x8b,0x41,0x18,0x85,0xc0,0x74,0x0a,0x8b,0x40,0x04,0x50,0xe8,0x00,0xde,0x2b,0x00,0x59,0xc3};
        const uint8_t frame_update[]={0x8b,0x44,0x24,0x04,0x33,0xc9,0x8b,0x90,0xa0,0x00,0x00,0x00,0x8a,0x4a,0x03,0xf6,0xc1,0x03,0x75,0x53,0x56,0x8b,0x35,0x24,0x7b,0xc9,0x00,0x8b,0xb6,0xbc,0x00,0x00,0x00,0x89,0x72,0x08,0x8b,0x15,0x24,0x7b,0xc9,0x00,0x8b,0xb0,0xa0,0x00,0x00,0x00,0x81,0xc2,0xbc,0x00,0x00,0x00,0x89,0x56,0x0c,0x8b,0x35,0x24,0x7b,0xc9,0x00,0x8b,0x90,0xa0,0x00,0x00,0x00,0x8b,0xb6,0xbc,0x00,0x00,0x00,0x83,0xc2,0x08,0x89,0x56,0x04,0x8b,0x90,0xa0,0x00,0x00,0x00,0x8b,0x35,0x24,0x7b,0xc9,0x00,0x83,0xc2,0x08,0x89,0x96,0xbc,0x00,0x00,0x00,0x5e,0x8b,0x90,0xa0,0x00,0x00,0x00,0x80,0xc9,0x03,0x88,0x4a,0x03,0x8a,0x48,0x03,0x80,0xc9,0x0c,0x88,0x48,0x03,0xc3};
        return profile(0x59AD70,copy) && profile(0x7F18A0,matrix_update) &&
            profile(0x532B00,entity_frame) && profile(0x7F0910,frame_update);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
std::optional<NativePhoneBooth> sa_phone_booth_snapshot(int32_t model,const std::array<float,3>& wanted) {
    if(!sa_executor_profile_matches() || model<0 || model>65535)return {};
    for(float coordinate:wanted)if(!std::isfinite(coordinate))return {};
    __try {
        const uint8_t entry[]={0x8b,0x0d,0x98,0x44,0xb7,0x00,0xe9,0x05,0xff,0xff,0xff};
        const uint8_t stride[]={0x8b,0x41,0x0c,0x8b,0x11,0x6b,0xc0,0x38,0x5b,0x5f,0x03,0xc2,0x5e,0xc3};
        if(!profile(0x404090,entry) || !profile(0x403FF4,stride))return {};
        const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74498));if(!pool)return {};
        const auto data=*reinterpret_cast<const uintptr_t*>(pool),flags=*reinterpret_cast<const uintptr_t*>(pool+4);
        const auto count=*reinterpret_cast<const int32_t*>(pool+8);
        if(count<0 || count>0x7FFFFF || (count && (!data || !flags)))return {};
        NativePhoneBooth result;
        for(int32_t i=count-1;i>=0;--i) {
            const auto tag=*reinterpret_cast<const uint8_t*>(flags+i);if(tag&0x80)continue;
            const auto handle=(i<<8)|tag;const auto building=entity_at(pool,handle,0x38,1);
            if(!building)return {};
            if(*reinterpret_cast<const uint16_t*>(building+0x22)!=model)continue;
            const auto matrix=*reinterpret_cast<const uintptr_t*>(building+0x14);
            std::array<float,3> position;
            std::memcpy(position.data(),reinterpret_cast<const void*>(matrix?matrix+0x30:building+4),12);
            for(float coordinate:position)if(!std::isfinite(coordinate))return {};
            if(position!=wanted)continue;
            if(result.entity)return {}; // do not guess between duplicate placements
            result.handle=handle;result.entity=building;result.matrix=matrix;result.model=uint16_t(model);result.position=position;
            result.rw_object=*reinterpret_cast<const uintptr_t*>(building+0x18);
            result.seed=*reinterpret_cast<const uint16_t*>(building+0x20);
            if(matrix) {
                result.up_z=*reinterpret_cast<const float*>(matrix+0x28);
                if(!std::isfinite(*result.up_z))return {};
            }
        }
        if(*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74498))!=pool ||
            *reinterpret_cast<const uintptr_t*>(pool)!=data || *reinterpret_cast<const uintptr_t*>(pool+4)!=flags ||
            *reinterpret_cast<const int32_t*>(pool+8)!=count)return {};
        if(result.entity && (entity_at(pool,result.handle,0x38,1)!=result.entity ||
            *reinterpret_cast<const uintptr_t*>(result.entity+0x14)!=result.matrix ||
            *reinterpret_cast<const uintptr_t*>(result.entity+0x18)!=result.rw_object ||
            *reinterpret_cast<const uint16_t*>(result.entity+0x22)!=result.model))return {};
        return result;
    } __except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
std::optional<NativePhoneRenderSnapshot> sa_phone_render_snapshot(const NativePhoneBooth& booth) {
    const auto fresh=sa_phone_booth_snapshot(booth.model,booth.position);
    if(!fresh || !fresh->entity || fresh->handle!=booth.handle || fresh->entity!=booth.entity ||
        fresh->matrix!=booth.matrix || fresh->rw_object!=booth.rw_object || !fresh->matrix || !fresh->rw_object ||
        !sa_phone_render_profile_matches())return {};
    __try {
        const auto rw=booth.rw_object;
        if(*reinterpret_cast<const uint8_t*>(rw)!=1)return {}; // reviewed static building atomic
        const auto frame=*reinterpret_cast<const uintptr_t*>(rw+4);if(!frame || (frame&3))return {};
        if(*reinterpret_cast<const uint8_t*>(frame)!=0)return {}; // rwFRAME
        const auto root=*reinterpret_cast<const uintptr_t*>(frame+0xA0);
        if(!root || (root&3) || *reinterpret_cast<const uint8_t*>(root)!=0 ||
            *reinterpret_cast<const uintptr_t*>(root+0xA0)!=root)return {};
        const auto engine=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xC97B24));if(!engine)return {};
        const auto anchor=engine+0xBC;
        const auto next=*reinterpret_cast<const uintptr_t*>(anchor),prev=*reinterpret_cast<const uintptr_t*>(anchor+4);
        if(!next || !prev || *reinterpret_cast<const uintptr_t*>(next+4)!=anchor ||
            *reinterpret_cast<const uintptr_t*>(prev)!=anchor)return {};
        if(*reinterpret_cast<const uint8_t*>(root+3)&3) {
            const auto link=root+8;
            const auto after=*reinterpret_cast<const uintptr_t*>(link),before=*reinterpret_cast<const uintptr_t*>(link+4);
            if(!after || !before || *reinterpret_cast<const uintptr_t*>(after+4)!=link ||
                *reinterpret_cast<const uintptr_t*>(before)!=link)return {};
        }
        for(size_t offset:{size_t(0),size_t(0x10),size_t(0x20),size_t(0x30)})for(size_t component=0;component<3;++component)
            if(!std::isfinite(*reinterpret_cast<const float*>(booth.matrix+offset+component*4)))return {};
        if(*reinterpret_cast<const uintptr_t*>(rw+4)!=frame ||
            *reinterpret_cast<const uintptr_t*>(frame+0xA0)!=root ||
            *reinterpret_cast<const uintptr_t*>(native_entity_address(0xC97B24))!=engine)return {};
        return NativePhoneRenderSnapshot{*fresh,frame,root,engine};
    } __except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
namespace {
bool phone_render_identity(const NativePhoneRenderSnapshot* before) {
    const auto& b=before->booth;
    const auto anchor=before->engine+0xBC;
    const auto next=*reinterpret_cast<const uintptr_t*>(anchor),prev=*reinterpret_cast<const uintptr_t*>(anchor+4);
    if(!next || !prev || *reinterpret_cast<const uintptr_t*>(next+4)!=anchor || *reinterpret_cast<const uintptr_t*>(prev)!=anchor)return false;
    if(*reinterpret_cast<const uint8_t*>(before->root+3)&3) {
        const auto link=before->root+8;
        const auto after=*reinterpret_cast<const uintptr_t*>(link),prior=*reinterpret_cast<const uintptr_t*>(link+4);
        if(!after || !prior || *reinterpret_cast<const uintptr_t*>(after+4)!=link || *reinterpret_cast<const uintptr_t*>(prior)!=link)return false;
    }
    const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74498));
    return entity_at(pool,b.handle,0x38,1)==b.entity &&
        *reinterpret_cast<const uint8_t*>(b.rw_object)==1 && *reinterpret_cast<const uint8_t*>(before->frame)==0 &&
        *reinterpret_cast<const uint8_t*>(before->root)==0 && *reinterpret_cast<const uintptr_t*>(before->root+0xA0)==before->root &&
        !std::memcmp(reinterpret_cast<const void*>(b.matrix+0x30),b.position.data(),12) &&
        *reinterpret_cast<const uint16_t*>(b.entity+0x20)==b.seed &&
        *reinterpret_cast<const uint16_t*>(b.entity+0x22)==b.model &&
        *reinterpret_cast<const uintptr_t*>(b.entity+0x14)==b.matrix &&
        *reinterpret_cast<const uintptr_t*>(b.entity+0x18)==b.rw_object &&
        *reinterpret_cast<const uintptr_t*>(b.rw_object+4)==before->frame &&
        *reinterpret_cast<const uintptr_t*>(before->frame+0xA0)==before->root &&
        *reinterpret_cast<const uintptr_t*>(native_entity_address(0xC97B24))==before->engine;
}
void invoke_phone_visual(const NativePhoneRenderSnapshot* before,float up_z,CopCreationReceipt* receipt,bool copy_only=false) {
    __try {
        if(!sa_phone_render_profile_matches() || !phone_render_identity(before) ||
            *reinterpret_cast<const float*>(before->booth.matrix+0x28)!=*before->booth.up_z)return;
        receipt->entered=true;
        if(!copy_only)*reinterpret_cast<float*>(before->booth.matrix+0x28)=up_z;
        if(!phone_render_identity(before) || !sa_phone_render_profile_matches())return;
        native_phone_copy_matrix(before->booth.matrix,before->frame+0x10);
        if(!phone_render_identity(before) || !sa_phone_render_profile_matches())return;
        for(size_t offset:{size_t(0),size_t(0x10),size_t(0x20),size_t(0x30)})
            if(std::memcmp(reinterpret_cast<const void*>(before->booth.matrix+offset),
                reinterpret_cast<const void*>(before->frame+0x10+offset),12))return;
        if(*reinterpret_cast<const float*>(before->booth.matrix+0x28)!=up_z)return;
        native_phone_update_frame(before->booth.entity);
        if(!phone_render_identity(before) ||
            (*reinterpret_cast<const uint8_t*>(before->root+3)&3)!=3 ||
            (*reinterpret_cast<const uint8_t*>(before->frame+3)&12)!=12)return;
        receipt->ok=true;
    } __except(EXCEPTION_EXECUTE_HANDLER){receipt->exception=GetExceptionCode();}
}
void invoke_phone_up_z(const NativePhoneRenderSnapshot* before,float up_z,CopCreationReceipt* receipt) {
    __try {
        if(!sa_phone_render_profile_matches() || !phone_render_identity(before) ||
            *reinterpret_cast<const float*>(before->booth.matrix+0x28)!=*before->booth.up_z)return;
        receipt->entered=true;*reinterpret_cast<float*>(before->booth.matrix+0x28)=up_z;
        receipt->ok=phone_render_identity(before) && *reinterpret_cast<const float*>(before->booth.matrix+0x28)==up_z;
    } __except(EXCEPTION_EXECUTE_HANDLER){receipt->exception=GetExceptionCode();}
}
}
NativeResult sa_phone_write_up_z(const NativePhoneRenderSnapshot& before,float up_z) {
    NativeResult result;const auto fresh=sa_phone_render_snapshot(before.booth);
    if(!std::isfinite(up_z) || !before.booth.up_z || !fresh || fresh->frame!=before.frame ||
        fresh->root!=before.root || fresh->engine!=before.engine || fresh->booth.up_z!=before.booth.up_z) {
        result.diagnostic="Phone CPU visual preflight refused";return result;
    }
    CopCreationReceipt receipt;invoke_phone_up_z(&before,up_z,&receipt);
    result.executed=receipt.ok;result.native_entered=receipt.entered;
    if(!receipt.ok)result.diagnostic="Phone CPU visual effect has no complete receipt; do not replay if entered";
    return result;
}
NativeResult sa_phone_refresh_visual(const NativePhoneRenderSnapshot& before) {
    NativeResult result;const auto fresh=sa_phone_render_snapshot(before.booth);
    if(!before.booth.up_z || !fresh || fresh->frame!=before.frame || fresh->root!=before.root ||
        fresh->engine!=before.engine || fresh->booth.up_z!=before.booth.up_z) {
        result.diagnostic="Phone RW refresh preflight refused";return result;
    }
    CopCreationReceipt receipt;invoke_phone_visual(&before,*before.booth.up_z,&receipt,true);
    result.executed=receipt.ok;result.native_entered=receipt.entered;
    if(!receipt.ok)result.diagnostic="Phone RW refresh has no complete receipt; do not replay if entered";
    return result;
}
NativeResult sa_phone_apply_visual(const NativePhoneRenderSnapshot& before,float up_z) {
    NativeResult result;
    const auto fresh=sa_phone_render_snapshot(before.booth);
    if(!std::isfinite(up_z) || !before.booth.up_z || !fresh || fresh->frame!=before.frame ||
        fresh->root!=before.root || fresh->engine!=before.engine || fresh->booth.up_z!=before.booth.up_z) {
        result.diagnostic="Phone visual preflight refused";return result;
    }
    CopCreationReceipt receipt;invoke_phone_visual(&before,up_z,&receipt);
    result.executed=receipt.ok;result.native_entered=receipt.entered;
    if(!receipt.ok)result.diagnostic=receipt.entered?"Phone visual effect entered without complete receipt; do not replay":"Phone visual boundary changed";
    return result;
}
std::optional<std::array<float,3>> sa_camera_placeable_position() {
    if(!sa_executor_profile_matches())return {};
    __try {
        const uint8_t position_branch[]={0xa1,0x3c,0xf0,0xb6,0x00,0x83,0xec,0x14,0x85,0xc0,0x74,0x05,0x83,0xc0,0x30,0xeb,0x05,0xb8,0x2c,0xf0,0xb6,0x00,0x8b,0x08,0x8b,0x50,0x04,0x8b,0x40,0x08};
        if(!profile(0x4090A0,position_branch))return {};
        const auto slot=reinterpret_cast<const uintptr_t*>(native_entity_address(0xB6F03C));
        const auto matrix=*slot;
        const auto position=matrix?matrix+0x30:native_entity_address(0xB6F02C);
        if(matrix && position<matrix)return {};
        std::array<float,3> result;
        std::memcpy(result.data(),reinterpret_cast<const void*>(position),12);
        for(float value:result)if(!std::isfinite(value))return {};
        if(*slot!=matrix)return {};
        return result;
    } __except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
std::optional<SpatialListener> sa_phone_camera_listener() {
    const auto position=sa_camera_placeable_position();
    if(!position)return {};
    __try {
        const auto slot=reinterpret_cast<const volatile uintptr_t*>(native_entity_address(0xB6F03C));
        const auto matrix=*slot;
        // A position-only fallback has no reviewed orientation. Refuse it.
        if(!matrix || matrix+0x30<matrix)return {};
        SpatialListener result;
        std::memcpy(result.right.data(),reinterpret_cast<const void*>(matrix),12);
        std::memcpy(result.position.data(),reinterpret_cast<const void*>(matrix+0x30),12);
        float length=0;
        for(size_t i=0;i<3;++i) {
            if(!std::isfinite(result.right[i]) || !std::isfinite(result.position[i]))return {};
            length+=result.right[i]*result.right[i];
        }
        if(length<0.5f || length>1.5f || *slot!=matrix || result.position!=*position)return {};
        std::array<float,3> right_again,position_again;
        std::memcpy(right_again.data(),reinterpret_cast<const void*>(matrix),12);
        std::memcpy(position_again.data(),reinterpret_cast<const void*>(matrix+0x30),12);
        if(*slot!=matrix || right_again!=result.right || position_again!=result.position)return {};
        return result;
    } __except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
std::optional<bool> sa_phone_slow_motion() {
    if(!sa_executor_profile_matches())return {};
    __try {
        const uint8_t predicate[]={0xd9,0x05,0x64,0xcb,0xb7,0x00,0xd8,0x1d,0x24,0x86,0x85,0x00,
            0xdf,0xe0,0xf6,0xc4,0x05,0x7a,0x03,0xb0,0x01,0xc3,0x32,0xc0,0xc3};
        if(!profile(0x561AD0,predicate) ||
            *reinterpret_cast<const uint32_t*>(native_entity_address(0x858624))!=0x3F800000u)return {};
        const auto slot=reinterpret_cast<const volatile float*>(native_entity_address(0xB7CB64));
        const float scale=*slot;
        if(!std::isfinite(scale) || scale<0 || *slot!=scale)return {};
        return scale<1.f;
    } __except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
std::optional<NativeFrameClock> sa_frame_clock() {
    if(!sa_executor_profile_matches())return {};
    __try {
        const uint8_t history[]={0xa1,0x84,0xcb,0xb7,0x00,0xa3,0x6c,0xcb,0xb7,0x00,0xa3,0x70,0xcb,0xb7,0x00,0xa3,0x74,0xcb,0xb7,0x00,0xa3,0x78,0xcb,0xb7,0x00};
        const uint8_t counter[]={0xa1,0x4c,0xcb,0xb7,0x00,0x83,0xc4,0x04,0x40,0xa3,0x4c,0xcb,0xb7,0x00};
        if(!profile(0x561AA0,history) || !profile(0x561C5D,counter))return {};
        NativeFrameClock result;
        result.frame=*reinterpret_cast<const volatile uint32_t*>(native_entity_address(0xB7CB4C));
        result.now=*reinterpret_cast<const volatile uint32_t*>(native_entity_address(0xB7CB84));
        result.previous=*reinterpret_cast<const volatile uint32_t*>(native_entity_address(0xB7CB78));
        if(*reinterpret_cast<const volatile uint32_t*>(native_entity_address(0xB7CB4C))!=result.frame ||
           *reinterpret_cast<const volatile uint32_t*>(native_entity_address(0xB7CB84))!=result.now ||
           *reinterpret_cast<const volatile uint32_t*>(native_entity_address(0xB7CB78))!=result.previous)return {};
        return result;
    } __except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
std::optional<std::array<float,3>> sa_focused_player_position() {
    if(!sa_executor_profile_matches())return {};
    __try {
        const uint8_t coors[]={0x8b,0x44,0x24,0x08,0x33,0xd2,0x3b,0xc2,0x7d,0x07,0x0f,0xb6,0x05,0x74,0xcd,0xb7,0x00,0x69,0xc0,0x90,0x01,0x00,0x00,0x8b,0x80,0x98,0xcd,0xb7,0x00,0x3b,0xc2,0x75,0x0d,0x8b,0x44,0x24,0x04,0x89,0x10,0x89,0x50,0x04,0x89,0x50,0x08,0xc3,0x8b,0x88,0x6c,0x04,0x00,0x00,0xf6,0xc5,0x01,0x56,0x74,0x1b,0x8b,0x88,0x8c,0x05,0x00,0x00,0x3b,0xca,0x74,0x11,0x8b,0x41,0x14,0x3b,0xc2,0x74,0x05,0x8d,0x48,0x30,0xeb,0x14,0x83,0xc1,0x04,0xeb,0x0f,0x8b,0x48,0x14,0x3b,0xca,0x74,0x05,0x83,0xc1,0x30,0xeb,0x03,0x8d,0x48,0x04,0x8b,0x31,0x8b,0x44,0x24,0x08,0x8b,0xd0,0x89,0x32,0x8b,0x71,0x04,0x89,0x72,0x04,0x8b,0x49,0x08,0x89,0x4a,0x08,0x5e,0xc3};
        if(!profile(0x56E010,coors))return {};
        const auto focus=*reinterpret_cast<const uint8_t*>(native_entity_address(0xB7CD74));
        if(focus>1)return {};
        const auto player=native_entity_address(0xB7CD98+focus*0x190);
        const auto ped=player_ped_at(player);if(!ped)return {};
        const bool in_car=(*reinterpret_cast<const uint8_t*>(ped+0x46D)&1)!=0;
        const auto vehicle=*reinterpret_cast<const uintptr_t*>(ped+0x58C);
        uintptr_t entity=ped;
        if(in_car && vehicle) {
            if(physical_pointer(vehicle)!=vehicle || (*reinterpret_cast<const uint8_t*>(vehicle+0x36)&7)!=2)return {};
            entity=vehicle;
        }
        const auto matrix=*reinterpret_cast<const uintptr_t*>(entity+0x14);
        std::array<float,3> result;
        std::memcpy(result.data(),reinterpret_cast<const void*>(matrix?matrix+0x30:entity+4),12);
        for(float value:result)if(!std::isfinite(value))return {};
        if(*reinterpret_cast<const uint8_t*>(native_entity_address(0xB7CD74))!=focus || player_ped_at(player)!=ped ||
            *reinterpret_cast<const uintptr_t*>(entity+0x14)!=matrix ||
            ((*reinterpret_cast<const uint8_t*>(ped+0x46D)&1)!=0)!=in_car ||
            *reinterpret_cast<const uintptr_t*>(ped+0x58C)!=vehicle)return {};
        return result;
    } __except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
std::optional<std::vector<ObjectiveActor>> sa_peds_descending() {
    std::vector<ObjectiveActor> result;
    try {if(!copy_peds_descending(&result))return {};}
    catch(...){return {};}
    return result;
}
namespace {
// No C++ owning objects inside SEH. World query and occupant reads run on the
// script thread; only generational pool identities escape this guarded copy.
struct CollectiveCopy {CollectiveCandidate items[64*9];size_t count=0;};
bool copy_collective(float x,float y,float radius,CollectiveCopy* out) {
    __try {
        constexpr uint8_t query[]={0x83,0xEC,0x18,0x53,0x55,0x56,0x57,0x8B,0x7C,0x24,0x2C};
        constexpr uint8_t ground_entry[]={0x83,0xEC,0x38,0x8B,0x44,0x24,0x3C,0x8B,0x4C,0x24,0x40};
        if(!sa_executor_profile_matches() || !ped_pool_profile() || !entity_profile(false) || !profile(0x564A20,query) || !profile(0x569660,ground_entry))return false;
        const auto z=native_collective_ground(x,y);
        if(!std::isfinite(z))return false;
        std::array<float,3> center{{x,y,z}};uintptr_t entities[64]{};int16_t count=0;
        native_collective_query(center,radius,true,&count,64,entities,false,true,true,false,false);
        if(count<0 || count>64)return false;
        for(int i=0;i<count;++i){
            const auto entity=entities[i];if(!entity)return false;
            uintptr_t peds[9]{};size_t n=0;
            const auto ped_key=remote_ped_key(entity);
            if(ped_key>=0)peds[n++]=entity;
            else {
                const auto key=remote_vehicle_key(entity);if(key<0)return false;
                const auto passengers=*reinterpret_cast<const uint8_t*>(entity+0x484);
                if(passengers>8)return false;
                peds[n++]=*reinterpret_cast<const uintptr_t*>(entity+0x460);
                // Retail LCS loops NumPassengers, not all passenger capacity slots.
                for(size_t j=0;j<passengers;++j)peds[n++]=*reinterpret_cast<const uintptr_t*>(entity+0x464+j*4);
                if(remote_vehicle_key(entity)!=key)return false;
            }
            for(size_t j=0;j<n;++j){
                const auto ped=peds[j];if(!ped)continue;
                const auto key=remote_ped_key(ped);if(key<0)return false;
                const auto type=*reinterpret_cast<const int32_t*>(ped+0x598);
                const auto created=*reinterpret_cast<const uint8_t*>(ped+0x484);
                if(remote_ped_key(ped)!=key)return false;
                out->items[out->count++]={{key,ped},type,created};
            }
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
std::optional<std::vector<CollectiveCandidate>> sa_collective_candidates(float x,float y,float radius) {
    CollectiveCopy copy;
    if(!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(radius) || !copy_collective(x,y,radius,&copy))return {};
    return std::vector<CollectiveCandidate>(copy.items,copy.items+copy.count);
}
bool sa_collectives_available(){
    __try {
        if(!sa_executor_profile_matches() || !ped_pool_profile())return false;
        const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74490));
        if(!pool)return false;
        const auto count=*reinterpret_cast<const int32_t*>(pool+8);
        return *reinterpret_cast<const uintptr_t*>(pool) && *reinterpret_cast<const uintptr_t*>(pool+4) && count>=0 && count<=0x7FFFFF;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
NativeResult sa_random_cop_in_area(const std::array<float,4>& area,const std::array<int32_t,5>& kinds,
        const std::array<int32_t,12>& donors,int32_t excluded,bool mission,const std::function<bool(int32_t)>& commit) {
    NativeResult result;
    for(float coordinate:area)if(!std::isfinite(coordinate)) {
        result.diagnostic="Cop query area unavailable";return result;
    }
    const auto models=SourceCopModelMap::approved(donors);
    const auto player=sa_focused_player_position();const auto peds=sa_peds_descending();
    if(!models || !player || !peds || !commit) {
        result.diagnostic="Cop query source donors/player/pool/selection commit unavailable";return result;
    }
    for(const auto& actor:*peds) {
        if(actor.handle==excluded)continue;
        const auto native=sa_cop_snapshot(actor);
        if(!native){result.diagnostic="Cop query actor snapshot unavailable";return result;}
        if(native->native_type!=6 || native->created_by!=1 || native->removed || native->fading || native->group.member)continue;
        const auto source=source_cop_candidate(*native,*models);
        if(!source){result.diagnostic="Cop query source model/state identity unavailable";return result;}
        const auto eligible=original_cop_eligible(*source,area,kinds,(*player)[2],excluded);
        if(!eligible){result.diagnostic="Cop query eligibility unavailable";return result;}
        if(!*eligible)continue;
        const auto current_player=sa_focused_player_position();
        if(!current_player || *current_player!=*player){result.diagnostic="Cop query player moved before acceptance";return result;}
        return sa_accept_random_cop(*native,mission,[&]{return commit(actor.handle);});
    }
    result.executed=true;result.values[0]=-1; // original genuine no-match branch
    return result;
}
NativeResult sa_random_char_unchecked(const std::array<float,4>& area,bool mission,
        const std::function<std::optional<bool>(int32_t)>& waiting,const std::function<bool(int32_t)>& commit) {
    NativeResult result;
    for(float coordinate:area)if(!std::isfinite(coordinate)) {
        result.diagnostic="Unchecked character area unavailable";return result;
    }
    const auto player=sa_focused_player_position();const auto peds=sa_peds_descending();
    if(!player || !peds || !waiting || !commit) {
        result.diagnostic="Unchecked character player/pool/wait-state/selection commit unavailable";return result;
    }
    // Descending pool order including slot zero; the first eligible ped wins.
    for(const auto& actor:*peds) {
        const auto native=sa_unchecked_char_snapshot(actor);
        if(!native){result.diagnostic="Unchecked character actor snapshot unavailable";return result;}
        const auto wait=waiting(actor.handle);
        if(!wait){result.diagnostic="Unchecked character wait-state authority unavailable";return result;}
        const OriginalUncheckedCharCandidate candidate{actor.handle,native->position,native->removed,native->fading,*wait};
        const auto eligible=original_unchecked_char_eligible(candidate,area,(*player)[2]);
        if(!eligible){result.diagnostic="Unchecked character eligibility unavailable";return result;}
        if(!*eligible)continue;
        const auto current_player=sa_focused_player_position();
        if(!current_player || *current_player!=*player){result.diagnostic="Unchecked character player moved before acceptance";return result;}
        return sa_accept_unchecked_char(*native,mission,[&]{return commit(actor.handle);});
    }
    result.executed=true;result.values[0]=-1; // original sentinel when no ped matches
    return result;
}
namespace {
// POD-only SEH helpers for 1486: the native facts the original reads and the single
// turn-force entry. Positions follow CPhysical's matrix pointer (+0x14 -> +0x30) with
// the placeable fallback (+4), exactly like the other entity readers.
struct TiltFacts {uintptr_t vehicle=0;float mass=0;std::array<float,3> vehicle_position{},ped_position{};};
bool read_entity_position(uintptr_t entity,std::array<float,3>* result) {
    const auto matrix=*reinterpret_cast<const uintptr_t*>(entity+0x14);
    if(matrix && matrix>UINTPTR_MAX-0x3C)return false;
    std::memcpy(result->data(),reinterpret_cast<const void*>(matrix?matrix+0x30:entity+4),12);return true;
}
bool copy_tilt_facts(int32_t vehicle_handle,int32_t ped_handle,TiltFacts* out) {
    __try {
        const uint8_t turn_force[]={0x83,0xEC,0x18,0x56,0x8B,0xF1,0x8B,0x46,0x40,0xA8,0x10,0x0F,0x85,0xDD,0x00,0x00,0x00,0xA8,0x40,0xC7,0x44,0x24,0x04,0x00,0x00,0x00,0x00,0xC7,0x44,0x24,0x08,0x00,0x00,0x00,0x00,0xC7,0x44,0x24,0x0C,0x00,0x00,0x00,0x00,0x75,0x2C,0x8B,0x46,0x14};
        if(!sa_executor_profile_matches() || !ped_pool_profile() || !entity_profile(false) || !profile(0x542A50,turn_force))return false;
        const auto ped_pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74490));
        const auto vehicle_pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74494));
        const auto ped=entity_at(ped_pool,ped_handle,0x7C4,3),vehicle=entity_at(vehicle_pool,vehicle_handle,0xA18,2);
        if(!ped || !vehicle)return false;
        if(!read_entity_position(ped,&out->ped_position) || !read_entity_position(vehicle,&out->vehicle_position))return false;
        out->mass=*reinterpret_cast<const float*>(ped+0x8C);out->vehicle=vehicle;
        return entity_at(ped_pool,ped_handle,0x7C4,3)==ped && entity_at(vehicle_pool,vehicle_handle,0xA18,2)==vehicle;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool invoke_turn_force(const TiltFacts* facts,float force_z,bool* entered) {
    __try {
        *entered=true;
        native_apply_turn_force(facts->vehicle,0.0f,0.0f,force_z,
            facts->ped_position[0]-facts->vehicle_position[0],facts->ped_position[1]-facts->vehicle_position[1],0.0f);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
// 1486 SET_CAR_TILTED_BY_CHAR (Script8): ApplyTurnForce(0,0,rand(-0.8..-1.2)*ped.mass,
// ped.xy - vehicle.xy, 0). `random_factor` is that factor (negative, from the caller's
// random source). The native entry is SA's CPhysical::ApplyTurnForce, whose point is
// likewise a world-axis offset from the entity position. The sound is a separate consumer.
NativeResult sa_apply_car_tilt(int32_t vehicle_handle,int32_t ped_handle,float random_factor) {
    NativeResult result;
    if(!std::isfinite(random_factor) || random_factor>-0.79f || random_factor<-1.21f){result.diagnostic="Car tilt random factor is outside the original -0.8..-1.2 range";return result;}
    TiltFacts facts;
    if(!copy_tilt_facts(vehicle_handle,ped_handle,&facts)){result.diagnostic="Car tilt vehicle/ped identity or native turn-force profile unavailable";return result;}
    for(float value:{facts.mass,facts.vehicle_position[0],facts.vehicle_position[1],facts.ped_position[0],facts.ped_position[1]})
        if(!std::isfinite(value)){result.diagnostic="Car tilt native facts are not finite";return result;}
    const float force=random_factor*facts.mass;
    if(!std::isfinite(force)){result.diagnostic="Car tilt force is not finite";return result;}
    bool entered=false;
    const bool ok=invoke_turn_force(&facts,force,&entered);
    result.native_entered=entered;result.executed=ok;
    if(!ok)result.diagnostic="Native turn force faulted; do not replay";
    return result;
}
static bool roof_profile_matches() {
    const uint8_t roof_profile[]={0x83,0xEC,0x38,0x8B,0x44,0x24,0x3C,0x8B,0x4C,0x24,0x40,0x8B,0x54,0x24,0x44,0x6A,0x00,0x6A,0x00,0x6A,0x01,0x6A,0x00,0x6A,0x00,0x6A,0x00,0x6A,0x01,0x89,0x44,0x24,0x1C,0x8D,0x44,0x24,0x58,0x50,0x89,0x4C,0x24,0x24,0x8D,0x4C,0x24,0x2C,0x51,0x89,0x54,0x24,0x2C,0x8D,0x54,0x24,0x24,0x68,0x00,0x00,0x7A,0x44,0x52,0xE8};
    __try{return profile(0x569750,roof_profile);}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// 738 GET_RANDOM_CHAR_IN_ZONE (retail Script3 / SA 02DD handler 0x4802D0 for the SA-side
// filters). Candidates are scanned in descending pool order and slot zero is never
// examined (the original loop is `while(--i)`). Eligibility: RANDOM_CHAR (native 1),
// not removed/fading, ped type allowed by the three kind flags, inside the loaded LCS
// zone box (inclusive XYZ), player Z +/-5, in control (native query supplied by the
// caller), not a member of a native group (SA GetGroup(): covers LCS leader/follower),
// and no roof above the candidate (SA calls FindRoofZFor3DCoord at the ped; LCS used the
// player's position). The shared LastRandomPedId exclusion is `excluded`. Promotion
// reuses the approved 1494 sequence (commit LastRandomPedId, mission creation, threat
// suppression, mission-ped count, optional mission cleanup). Absence is the genuine -1.
NativeResult sa_random_char_in_zone(const std::array<float,6>& zone,const std::array<int32_t,3>& kinds,int32_t excluded,bool mission,
        const std::function<std::optional<bool>(int32_t)>& in_control,const std::function<bool(int32_t)>& commit) {
    NativeResult result;
    for(float coordinate:zone)if(!std::isfinite(coordinate)){result.diagnostic="Zone character query bounds are unavailable";return result;}
    const auto player=sa_focused_player_position();const auto peds=sa_peds_descending();
    const bool roof_ready=roof_profile_matches();
    if(!player || !peds || !in_control || !commit || !roof_ready) {
        result.diagnostic="Zone character player/pool/control/roof/selection commit unavailable";return result;
    }
    for(const auto& actor:*peds) {
        if((uint32_t(actor.handle)>>8)==0 || actor.handle==excluded)continue;
        const auto native=sa_unchecked_char_snapshot(actor);
        if(!native){result.diagnostic="Zone character actor snapshot unavailable";return result;}
        if(native->created_by!=1 || native->removed || native->fading || native->has_vehicle ||
           native->native_state==54 || native->native_state==55 || !original_zone_char_kind_allowed(native->native_type,kinds))continue;
        const auto& at=native->position;
        bool inside=true;
        for(size_t axis=0;axis<3;++axis)if(!std::isfinite(at[axis]) || at[axis]<zone[axis] || at[axis]>zone[axis+3])inside=false;
        if(!inside || at[2]<(*player)[2]-5.0f || at[2]>(*player)[2]+5.0f)continue;
        const auto control=in_control(actor.handle);
        if(!control){result.diagnostic="Zone character control state unavailable";return result;}
        if(!*control)continue;
        const auto group=sa_ped_group_snapshot(actor);
        if(!group){result.diagnostic="Zone character group state unavailable";return result;}
        if(group->member)continue;
        if(native_roof_found(at[0],at[1],at[2]))continue;
        const auto current_player=sa_focused_player_position();
        if(!current_player || *current_player!=*player){result.diagnostic="Zone character player moved before acceptance";return result;}
        return sa_accept_unchecked_char(*native,mission,[&]{return commit(actor.handle);});
    }
    result.executed=true;result.values[0]=-1; // genuine "nobody found" sentinel
    return result;
}
std::optional<NativePedGroupSnapshot> sa_ped_group_snapshot(const ObjectiveActor& actor) {
    if(!sa_executor_profile_matches())return {};
    __try {
        const uint8_t leader_profile[]={0x8b,0x54,0x24,0x4,0x33,0xc9,0xb8,0x48,0x99,0xc0,0x0,0x53,0x8d,0x64,0x24,0x0,0x8a,0x99,0xe0,0x98,0xc0,0x0,0x84,0xdb,0x74,0x8,0x85,0xd2,0x74,0x4,0x39,0x10,0x74,0x11,0x5,0xd4,0x2,0x0,0x0,0x41,0x3d,0xe8,0xaf,0xc0,0x0,0x7c,0xe1,0x32,0xc0,0x5b,0xc3,0xb0,0x1,0x5b,0xc3};
        const uint8_t group_profile[]={0x56,0x57,0x8b,0x7c,0x24,0xc,0x33,0xc0,0xe9,0xf4,0x99,0xe0,0xff,0x8d,0x49,0x0,0x8a,0x88,0xe0,0x98,0xc0,0x0,0x84,0xc9,0x74,0x1b,0x85,0xff,0x74,0x17,0x33,0xc9,0x8d,0x56,0x4,0x39,0x3a,0x74,0x22,0x41,0x83,0xc2,0x4,0x83,0xf9,0x7,0x7c,0xf3,0x39,0x7e,0x20,0x74,0x14,0x81,0xc6,0xd4,0x2,0x0,0x0,0x40,0x81,0xfe,0xc8,0xaf,0xc0,0x0,0x7c,0xcc,0x5f,0x5e,0x33,0xc0,0xc3,0x69,0xc0,0xd4,0x2,0x0,0x0,0x5f,0x5e,0x5,0x20,0x99,0xc0,0x0,0xc3};
        const uint8_t group_trampoline[]={0xbe,0x28,0x99,0xc0,0x0,0xe9,0x2,0x66,0x1f,0x0};
        const uint8_t membership_leader[]={0x8b,0x41,0x20,0xc3};
        // Read the existing generation; issuing a new reference would invalidate
        // the group's current owning script handle.
        const uint8_t group_reference_profile[]={0x8b,0x4c,0x24,0x04,0x33,0xc0,0x66,0x8b,0x04,0x4d,0xd0,0x98,0xc0,0x00,0x66,0x3d,0xfe,0xff,0x73,0x12,0x40,0x66,0x89,0x04,0x4d,0xd0,0x98,0xc0,0x00,0x0f,0xb7,0xc0,0xc1,0xe0,0x10,0x0b,0xc1,0xc3};
        if(!ped_pool_profile() || !profile(0x5F7E40,leader_profile) || !profile(0x5F7E80,group_profile) ||
           !profile(0x401881,group_trampoline) || !profile(0x5F69A0,membership_leader) ||
           !profile(0x483933,group_reference_profile))return {};
        const auto ped=entity_at(*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74490)),actor.handle,0x7C4,3);
        if(!ped || ped!=actor.native_ped)return {};
        NativePedGroupSnapshot result;
        for(size_t group=0;group<8;++group) {
            if(!*reinterpret_cast<const uint8_t*>(native_entity_address(0xC098E0+group))){result.free_group=true;continue;}
            const auto base=native_entity_address(0xC09920+group*0x2D4);
            bool member=false;
            for(size_t slot=0;slot<8;++slot)if(*reinterpret_cast<const uintptr_t*>(base+0xC+slot*4)==ped)member=true;
            if(!member)continue;
            if(result.member)return {}; // contradictory duplicate memberships
            const auto generation=*reinterpret_cast<const uint16_t*>(native_entity_address(0xC098D0+group*2));
            result.member=true;result.group=static_cast<int32_t>((uint32_t(generation)<<16)|uint32_t(group));
            result.leader_ped=*reinterpret_cast<const uintptr_t*>(base+0x28);
            result.leader=result.leader_ped==ped;
            // A stale leader cannot be silently read as an absent reference.
            if(result.leader_ped && (physical_pointer(result.leader_ped)!=result.leader_ped ||
               (*reinterpret_cast<const uint8_t*>(result.leader_ped+0x36)&7)!=3))return {};
        }
        const auto live=entity_at(*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74490)),actor.handle,0x7C4,3);
        if(live!=ped)return {};
        return result;
    } __except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
std::optional<TaskSequenceState> sa_task_sequence_state() {
    if(!sa_executor_profile_matches())return {};
    __try {
        // Reviewed 0615 OPEN (allocate flag, active global), 0616 CLOSE and the
        // handle producer (generation table + issued flag) in the user's executable.
        const uint8_t allocate[]={0xC6,0x87,0x98,0x78,0xC1,0x00,0x01};
        const uint8_t activate[]={0x89,0x3D,0x98,0x2E,0x8D,0x00};
        const uint8_t close[]={0xC6,0x80,0x98,0x78,0xC1,0x00,0x00,0xC7,0x05,0x98,0x2E,0x8D,0x00,0xFF,0xFF,0xFF,0xFF};
        const uint8_t handle[]={0x8B,0x4C,0x24,0x04,0x33,0xC0,0x66,0x8B,0x04,0x8D,0x6A,0x3F,0xA4,0x00,0x66,0x3D,0xFE,0xFF,0x73,0x0B,0x40,0x66,0x89,0x04,0x8D,0x6A,0x3F,0xA4,0x00,0xEB,0x0A,0x66,0xC7,0x04,0x8D,0x6A,0x3F,0xA4,0x00,0x01,0x00,0x0F,0xB7,0x04,0x8D,0x6A,0x3F,0xA4,0x00,0xC1,0xE0,0x10,0xC6,0x04,0x8D,0x68,0x3F,0xA4,0x00,0x01,0x0B,0xC1,0xC3};
        // 632E00 allocator: slot is free only when open flag (C17898) is 0 and the first
        // task pointer (record C17900 + 0x40*slot) is 0. 061B: refcount (+3C) != 0 defers
        // the release by setting the pending flag (+38) instead of freeing the tasks.
        const uint8_t allocator[]={0xC1,0xE2,0x06,0x81,0xC2,0x00,0x79,0xC1,0x00,0x90,0x8A,0x99,0x98,0x78,0xC1,0x00,0x84,0xDB,0x75,0x05,0x83,0x3A,0x00,0x74,0x0B,0x41,0x83,0xC2,0x40,0x3B,0xCE,0x7C};
        const uint8_t clear[]={0x8B,0xCF,0xC1,0xE1,0x06,0x81,0xC1,0xF0,0x78,0xC1,0x00,0x39,0x59,0x3C,0x88,0x9F,0x98,0x78,0xC1,0x00,0x75,0x0A,0x88,0x59,0x38,0xE8,0x81,0x11,0x1A,0x00,0xEB,0x04,0xC6,0x41,0x38,0x01};
        if(!profile(0x4918A5,allocate) || !profile(0x4918B4,activate) || !profile(0x491933,close) || !profile(0x48382C,handle) ||
           !profile(0x632E26,allocator) || !profile(0x491A71,clear))return {};
        TaskSequenceState state;
        state.active=*reinterpret_cast<const int32_t*>(native_entity_address(0x8D2E98));
        if(state.active<-1 || state.active>=64)return {};
        for(size_t slot=0;slot<64;++slot) {
            state.open[slot]=*reinterpret_cast<const uint8_t*>(native_entity_address(0xC17898+slot));
            state.first_task[slot]=*reinterpret_cast<const uint32_t*>(native_entity_address(0xC17900+slot*0x40));
            state.pending[slot]=*reinterpret_cast<const uint8_t*>(native_entity_address(0xC17928+slot*0x40));
            state.references[slot]=*reinterpret_cast<const uint32_t*>(native_entity_address(0xC1792C+slot*0x40));
            state.issued[slot]=*reinterpret_cast<const uint8_t*>(native_entity_address(0xA43F68+slot*4));
            state.generation[slot]=*reinterpret_cast<const uint16_t*>(native_entity_address(0xA43F6A+slot*4));
            if(state.open[slot]>1 || state.pending[slot]>1 || state.issued[slot]>1)return {};
        }
        if(state.active>=0 && !state.open[size_t(state.active)])return {}; // contradictory construction context
        return state;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return {};}
}
bool sa_abort_unissued_task_sequence(size_t slot,uint16_t previous_generation,uint8_t previous_issued) {
    // Used only for the first free non-mission slot observed immediately before
    // our OPEN. Before a new handle exists, CLOSE/CLEAR cannot address that slot.
    // An empty record has no heap task to destroy; restore only its construction
    // flag and our active global, preserving all handle metadata and other slots.
    if(slot>=32 || previous_issued>1)return false;
    const auto state=sa_task_sequence_state();
    if(!state || !state->open[slot] || (state->active!=-1 && state->active!=int32_t(slot)) ||
       state->generation[slot]!=previous_generation || state->issued[slot]!=previous_issued ||
       state->pending[slot] || state->references[slot])return false;
    __try {
        const auto record=native_entity_address(0xC178F0+slot*0x40);
        for(size_t task=0;task<8;++task)if(*reinterpret_cast<const uint32_t*>(record+0x10+task*4))return false;
        *reinterpret_cast<uint8_t*>(native_entity_address(0xC17898+slot))=0;
        if(state->active==int32_t(slot))*reinterpret_cast<int32_t*>(native_entity_address(0x8D2E98))=-1;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
std::optional<NativeCopSnapshot> sa_cop_snapshot(const ObjectiveActor& actor) {
    const auto pose=sa_route_actor_snapshot(actor);if(!pose)return {};
    const auto group=sa_ped_group_snapshot(actor);if(!group)return {};
    __try {
        const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74490));
        const auto ped=entity_at(pool,actor.handle,0x7C4,3);
        if(!ped || ped!=actor.native_ped)return {};
        NativeCopSnapshot result;
        result.actor=actor;result.position=pose->position;result.controllable=pose->controllable;result.group=*group;
        result.native_model=*reinterpret_cast<const uint16_t*>(ped+0x22);
        result.native_type=*reinterpret_cast<const int32_t*>(ped+0x598);
        result.native_state=*reinterpret_cast<const int32_t*>(ped+0x530);
        result.created_by=*reinterpret_cast<const uint8_t*>(ped+0x484);
        result.removed=(*reinterpret_cast<const uint8_t*>(ped+0x1D)&8)!=0;
        result.fading=(*reinterpret_cast<const uint8_t*>(ped+0x470)&8)!=0;
        if(entity_at(pool,actor.handle,0x7C4,3)!=ped)return {};
        return result;
    } __except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
NativeResult sa_create_route_motion(const RouteMotionRequest& request) {
    NativeResult result;
    if(!sa_executor_profile_matches() || request.actor.handle<0 || !request.actor.native_ped ||
       (request.move_state!=4 && request.move_state!=6 && request.move_state!=7)) {
        result.diagnostic="Route motion actor/movement/profile unavailable";return result;
    }
    for(float value:request.target)if(!std::isfinite(value)) {
        result.diagnostic="Non-finite original route target";return result;
    }
    const auto before=sa_script_task_snapshot();
    EntityReceipt receipt;invoke_route_motion(&request,&receipt);
    result.executed=receipt.ok;result.native_entered=receipt.entered;
    if(!result.executed){result.diagnostic="Native route task construction/assignment refused or faulted";return result;}
    if(!request.track_owned_lifetime && !before){result.task_capture=NativeResult::TaskCapture::unavailable_before;return result;}
    const auto current=sa_live_objective_actor(request.actor.handle);
    if(!current || !(*current==request.actor)){result.task_capture=NativeResult::TaskCapture::actor_changed;return result;}
    const auto after=sa_script_task_snapshot();
    if(!after){result.task_capture=NativeResult::TaskCapture::unavailable_after;return result;}
    if(request.track_owned_lifetime) {
        // Fresh private construction supplies the epoch BEFORE publication.
        // Byte-identical bank/pool reuse is not confused with an older task.
        bool ambiguous=false;
        for(size_t slot=0;slot<after->size();++slot) {
            const CapturedScriptTask candidate{slot,(*after)[slot]};
            if(sa_owned_route_binding_epoch(candidate,request.actor)!=receipt.route_epoch)continue;
            if(result.accepted_script_task){ambiguous=true;break;}
            result.accepted_script_task=candidate;
        }
        if(ambiguous)result.accepted_script_task.reset();
    } else {
        if(!before){result.task_capture=NativeResult::TaskCapture::unavailable_before;return result;}
        result.accepted_script_task=capture_accepted_script_task(*before,*after,uint32_t(request.actor.native_ped),0x05D3);
    }
    result.task_capture=result.accepted_script_task?NativeResult::TaskCapture::captured:NativeResult::TaskCapture::no_unique_record;
    return result;
}
}
