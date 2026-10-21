#include <windows.h>
#include "sa_crane.hpp"
#include "sa_lcs_bridge.hpp"
#include "lcs/sa_task_memory.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace lcs {
namespace {
// CPlaceable::SetHeading (this in ecx): matrix present -> CMatrix::SetRotateZOnly, else placement heading (+0x10).
constexpr unsigned char set_heading_profile[]={0x8B,0xC1,0x8B,0x48,0x14,0x85,0xC9,0x74,0x05,0xE9,0x52,0xCF,0x15,0x00,
    0x8B,0x4C,0x24,0x04,0x89,0x48,0x10,0xC2,0x04,0x00};
constexpr unsigned char rotate_z_profile[]={0xD9,0x44,0x24,0x04,0x33,0xC0,0xD9,0xFF,0x89,0x41,0x08,0x89,0x41,0x18,
    0x89,0x41,0x20,0x89,0x41,0x24,0xC7,0x41,0x28,0x00,0x00,0x80,0x3F};
// CEntity::SetIsStatic(false), SA 1.0 US. This clears only bIsStatic in
// entity flags (+0x1C); it is the native wake transition used after restoring
// a carried vehicle's collision and gravity.
constexpr unsigned char set_is_static_profile[]={0x0F,0xB6,0x44,0x24,0x04,0x8B,0x51,0x1C,0xC1,0xE0,0x02,0x33,
    0xC2,0x83,0xE0,0x04,0x33,0xD0,0x89,0x51,0x1C,0xC2,0x04,0x00};
constexpr float xy_tolerance=1.0f,z_tolerance=0.5f;

uint16_t crane_model=0;
std::array<float,3> crane_at{};
bool configured=false,rotated=false;
float rotated_to=0.0f;

struct Held {
    RouteVehicleSnapshot identity;
    uintptr_t cell=0;
    uint8_t collision=0,gravity=0;
    bool active=false,flags_saved=false,wrote=false,restored=false;
} held;

#ifdef LCS_CRANE_TEST_BACKEND
uintptr_t test_pool_root=0,test_model_info=0;
#endif
uintptr_t pool_root() {
#ifdef LCS_CRANE_TEST_BACKEND
    return test_pool_root;
#else
    return 0xB74498;
#endif
}
bool close(float value,float expected,float tolerance){return std::isfinite(value) && std::fabs(value-expected)<=tolerance;}
bool profile() {
    if(!sa_lcs_bridge_profile_matches())return false;
#ifdef LCS_CRANE_TEST_BACKEND
    return true;
#else
    __try {
        return !std::memcmp(reinterpret_cast<void*>(0x43E0C0),set_heading_profile,sizeof(set_heading_profile)) &&
               !std::memcmp(reinterpret_cast<void*>(0x59B020),rotate_z_profile,sizeof(rotate_z_profile)) &&
               !std::memcmp(reinterpret_cast<void*>(0x403E20),set_is_static_profile,sizeof(set_is_static_profile));
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
#endif
}
SaCraneResolve scan(uintptr_t root,uint16_t model,const std::array<float,3>& at,uintptr_t* out) {
    __try {
        const auto pool=*reinterpret_cast<const uintptr_t*>(root);
        if(!pool)return SaCraneResolve::streamed_out;
        const auto data=*reinterpret_cast<const uintptr_t*>(pool);
        const auto flags=*reinterpret_cast<const uintptr_t*>(pool+4);
        const auto count=*reinterpret_cast<const int32_t*>(pool+8);
        if(!data || !flags || count<0 || count>200000)return SaCraneResolve::unavailable;
        uintptr_t found=0;
        for(int32_t slot=0;slot<count;++slot) {
            if(*reinterpret_cast<const uint8_t*>(flags+slot)&0x80)continue;
            const uintptr_t entity=data+uintptr_t(slot)*0x38;
            if(*reinterpret_cast<const uint16_t*>(entity+0x22)!=model)continue;
            if((*reinterpret_cast<const uint8_t*>(entity+0x36)&7)!=1)return SaCraneResolve::unavailable;
            const auto matrix=*reinterpret_cast<const uintptr_t*>(entity+0x14);
            const auto position=matrix?matrix+0x30:entity+4;
            if(!close(*reinterpret_cast<const float*>(position),at[0],xy_tolerance) ||
               !close(*reinterpret_cast<const float*>(position+4),at[1],xy_tolerance) ||
               !close(*reinterpret_cast<const float*>(position+8),at[2],z_tolerance))return SaCraneResolve::unavailable;
            if(found)return SaCraneResolve::unavailable;
            found=entity;
        }
        *out=found;
        return found?SaCraneResolve::ready:SaCraneResolve::streamed_out;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return SaCraneResolve::unavailable;}
}
// SEH leaf, no C++ unwind object inside. `entered` is set before the first mutation.
bool turn(uintptr_t entity,float angle,bool* entered) {
    __try {
        *entered=true;
#ifdef LCS_CRANE_TEST_BACKEND
        const auto matrix=*reinterpret_cast<const uintptr_t*>(entity+0x14);
        if(matrix) {
            *reinterpret_cast<float*>(matrix)=std::cos(angle);*reinterpret_cast<float*>(matrix+4)=std::sin(angle);
            *reinterpret_cast<float*>(matrix+0x10)=-std::sin(angle);*reinterpret_cast<float*>(matrix+0x14)=std::cos(angle);
        } else *reinterpret_cast<float*>(entity+0x10)=angle;
        return true;
#else
        reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x534AE0)(entity);
        reinterpret_cast<void(__thiscall*)(uintptr_t,float)>(0x43E0C0)(entity,angle);
        reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x446F90)(entity);
        reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x532B00)(entity);
        reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x533020)(entity);
        const auto matrix=*reinterpret_cast<const uintptr_t*>(entity+0x14);
        if(matrix)return close(*reinterpret_cast<const float*>(matrix),std::cos(angle),0.0005f) &&
                         close(*reinterpret_cast<const float*>(matrix+4),std::sin(angle),0.0005f);
        return close(*reinterpret_cast<const float*>(entity+0x10),angle,0.0005f);
#endif
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
// Retail FindDoorHeightForMI reads the same box: model info +0x14 -> collision model, max.z at +0x14.
bool read_top(uintptr_t vehicle,float* top) {
    __try {
        const auto model=*reinterpret_cast<const uint16_t*>(vehicle+0x22);
#ifdef LCS_CRANE_TEST_BACKEND
        const auto info=test_model_info;
        (void)model;
#else
        if(model>=20000)return false;
        const auto info=*reinterpret_cast<const uintptr_t*>(0xA9B0C8+uintptr_t(model)*4);
#endif
        if(!info)return false;
        const auto col=*reinterpret_cast<const uintptr_t*>(info+0x14);
        if(!col)return false;
        const float max_z=*reinterpret_cast<const float*>(col+0x14);
        if(!std::isfinite(max_z) || max_z<0.0f || max_z>20.0f)return false;
        *top=max_z;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool read_forward(uintptr_t vehicle,float* angle) {
    __try {
        const auto matrix=*reinterpret_cast<const uintptr_t*>(vehicle+0x14);
        if(!matrix)return false;
        const float x=*reinterpret_cast<const float*>(matrix+0x10),y=*reinterpret_cast<const float*>(matrix+0x14);
        if(!std::isfinite(x) || !std::isfinite(y) || (x==0.0f && y==0.0f))return false;
        float a=std::atan2(y,x);if(a<0.0f)a+=6.28318530718f;
        *angle=a;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool hold_fields(bool restore,bool* entered) {
    __try {
        if(!held.cell || !sa_vehicle_reference_live(held.identity,&held.cell))return false;
        auto* collision=reinterpret_cast<uint8_t*>(held.cell+0x1C);
        auto* physics=reinterpret_cast<uint8_t*>(held.cell+0x40);
        if(!held.flags_saved){held.collision=*collision&1u;held.gravity=*physics&2u;held.flags_saved=true;}
        *entered=true;
        held.wrote=true;
        *collision=uint8_t((*collision&~1u)|(restore?held.collision:0));
        *physics=uint8_t((*physics&~2u)|(restore?held.gravity:0));
        if(!restore)for(int i=0;i<6;++i)*reinterpret_cast<float*>(held.cell+0x44+i*4)=0.0f; // move and turn speed
#ifdef LCS_CRANE_TEST_BACKEND
        if(restore)*collision=uint8_t(*collision&~4u); // CEntity::SetIsStatic(false), fixture behavior.
#else
        if(restore)reinterpret_cast<void(__thiscall*)(uintptr_t,bool)>(0x403E20)(held.cell,false);
#endif
        const bool confirmed=(*collision&1u)==(restore?held.collision:0) && (!restore || (*collision&4u)==0) &&
            (*physics&2u)==(restore?held.gravity:0) &&
            sa_vehicle_reference_live(held.identity,&held.cell);
        held.restored=restore && confirmed;
        return confirmed;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool write_pose(uintptr_t vehicle,const std::array<float,3>& position,float angle,const std::array<float,3>& up) {
    __try {
        const auto matrix=*reinterpret_cast<const uintptr_t*>(vehicle+0x14);
        if(!matrix)return false;
        // Retail: Right = Forward x Up, Forward = Up x Right (Forward starts as the heading vector).
        const float f[3]={std::cos(angle),std::sin(angle),0.0f};
        float right[3]={f[1]*up[2]-f[2]*up[1],f[2]*up[0]-f[0]*up[2],f[0]*up[1]-f[1]*up[0]};
        float length=std::sqrt(right[0]*right[0]+right[1]*right[1]+right[2]*right[2]);
        if(!(length>0.0f))return false;
        for(float& v:right)v/=length;
        float forward[3]={up[1]*right[2]-up[2]*right[1],up[2]*right[0]-up[0]*right[2],up[0]*right[1]-up[1]*right[0]};
        length=std::sqrt(forward[0]*forward[0]+forward[1]*forward[1]+forward[2]*forward[2]);
        if(!(length>0.0f))return false;
        for(float& v:forward)v/=length;
#ifndef LCS_CRANE_TEST_BACKEND
        // The SA sector and collision lists are keyed by the entity's current
        // transform. Remove before changing its basis, then let the existing
        // bridge-pinned SetPosn/UpdateRW/UpdateRwFrame/Add sequence publish it.
        if(!sa_world_entity_remove(vehicle))return false;
#endif
        for(int i=0;i<3;++i) {
            *reinterpret_cast<float*>(matrix+i*4)=right[i];
            *reinterpret_cast<float*>(matrix+0x10+i*4)=forward[i];
            *reinterpret_cast<float*>(matrix+0x20+i*4)=up[i];
#ifdef LCS_CRANE_TEST_BACKEND
            *reinterpret_cast<float*>(matrix+0x30+i*4)=position[i];
#endif
        }
#ifndef LCS_CRANE_TEST_BACKEND
        reinterpret_cast<void(__thiscall*)(uintptr_t,float,float,float)>(0x420B80)(vehicle,position[0],position[1],position[2]);
        reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x446F90)(vehicle);
        reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x532B00)(vehicle);
        if(!sa_world_entity_add(vehicle))return false;
#endif
        for(int i=0;i<6;++i)*reinterpret_cast<float*>(vehicle+0x44+i*4)=0.0f;
        const auto updated_matrix=*reinterpret_cast<const uintptr_t*>(vehicle+0x14);
        if(!updated_matrix)return false;
        for(int i=0;i<3;++i) {
            if(!close(*reinterpret_cast<const float*>(updated_matrix+i*4),right[i],0.001f) ||
               !close(*reinterpret_cast<const float*>(updated_matrix+0x10+i*4),forward[i],0.001f) ||
               !close(*reinterpret_cast<const float*>(updated_matrix+0x20+i*4),up[i],0.001f))return false;
        }
        const auto updated_position=updated_matrix+0x30;
        for(int i=0;i<3;++i)
            if(!close(*reinterpret_cast<const float*>(updated_position+i*4),position[i],0.001f))return false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool same_identity(const RouteVehicleSnapshot& a,const RouteVehicleSnapshot& b){return a.handle==b.handle && a.entity==b.entity && a.generation==b.generation;}
std::optional<RouteVehicleSnapshot> live(const RouteVehicleSnapshot& expected) {
    const auto found=sa_route_vehicle_lookup(expected.handle);
    if(found.status!=VehicleLookupStatus::present || !found.snapshot || !same_identity(*found.snapshot,expected))return {};
    return found.snapshot;
}

// Read the exact focused player's active enter-car target without calling a
// virtual method through a foreign task vtable. This mirrors the verified SA
// task pool/primary-tree layout used by the route and owned-entry consumers.
constexpr uintptr_t player_focus_address=0xB7CD74,player_info_address=0xB7CD98;
constexpr uintptr_t ped_pool_address=0xB74490,vehicle_pool_address=0xB74494;
constexpr size_t ped_stride=0x7C4,vehicle_stride=0xA18;
constexpr int32_t enter_car_passenger_type=700,enter_car_driver_type=701;

struct EntryTargetRead {int32_t handle=-1;uintptr_t entity=0;bool known=false;};

bool entry_target_profiles_match() {
    constexpr unsigned char ped_getter[]={0x8B,0x4C,0x24,0x04,0x8B,0x15,0x90,0x44,0xB7,0x00,0x53,0x56,0x8B,
        0x72,0x04,0x8B,0xC1,0xC1,0xF8,0x08,0x8A,0x1C,0x30,0x5E,0x3A,0xD9,0x5B,0x75,0x0B,0x8B,0x0A,
        0x69,0xC0,0xC4,0x07,0x00,0x00};
    constexpr unsigned char task_primary_count[]={0x33,0xC0,0x83,0x3C,0x81,0x00,0x75,0x09,0x40,0x83,0xF8,0x05};
    __try {
        return !std::memcmp(reinterpret_cast<const void*>(0x54FF90),ped_getter,sizeof(ped_getter)) &&
            !std::memcmp(reinterpret_cast<const void*>(0x681720),task_primary_count,sizeof(task_primary_count)) &&
            sa_task_allocator_profile_matches();
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool pool_entry(uintptr_t root,size_t stride,uint8_t type,uintptr_t entity,int32_t* handle) {
    __try {
        const auto pool=*reinterpret_cast<const uintptr_t*>(root);
        if(!pool)return false;
        const auto first=*reinterpret_cast<const uintptr_t*>(pool),tags=*reinterpret_cast<const uintptr_t*>(pool+4);
        const auto count=*reinterpret_cast<const int32_t*>(pool+8);
        if(!first || !tags || count<=0 || count>0x7FFFFF || entity<first)return false;
        const auto delta=entity-first,slot=delta/stride;
        if(delta%stride || slot>=uint32_t(count) || (*reinterpret_cast<const uint8_t*>(tags+slot)&0x80u))return false;
        const auto generation=*reinterpret_cast<const uint8_t*>(tags+slot);
        if((*reinterpret_cast<const uint8_t*>(entity+0x36)&7u)!=type)return false;
        *handle=int32_t((uint32_t(slot)<<8)|generation);
        return first+slot*stride==entity;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool live_task(uintptr_t task) {return sa_task_slot_live(task);}

// 1 = known getter shape/value, 0 = unknown or foreign task ABI.
int read_task_type(uintptr_t task,uint32_t* type) {
    __try {
        const auto table=*reinterpret_cast<const uintptr_t*>(task);
        if(!table)return 0;
        const auto getter=*reinterpret_cast<const uintptr_t*>(table+16);
        if(getter<0x400000 || getter>=0x800000)return 0;
        const auto* code=reinterpret_cast<const uint8_t*>(getter);
        if(code[0]!=0xB8 || code[5]!=0xC3)return 0;
        std::memcpy(type,code+1,sizeof(*type));return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return 0;}
}

// 1 = complex subtask, 2 = simple task, 0 = unknown ABI.
int read_task_child(uintptr_t task,uintptr_t* child) {
    __try {
        const auto table=*reinterpret_cast<const uintptr_t*>(task);
        if(!table)return 0;
        const auto getter=*reinterpret_cast<const uintptr_t*>(table+8);
        if(getter<0x400000 || getter>=0x800000)return 0;
        const auto* code=reinterpret_cast<const uint8_t*>(getter);
        if(code[0]==0x8B && code[1]==0x41 && code[2]==0x08 && code[3]==0xC3) {
            *child=*reinterpret_cast<const uintptr_t*>(task+8);return 1;
        }
        if(code[0]==0x33 && code[1]==0xC0 && code[2]==0xC3) {*child=0;return 2;}
        return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return 0;}
}

EntryTargetRead read_player_entry_target() {
    EntryTargetRead result;
    if(!entry_target_profiles_match())return result;
    __try {
        const auto focus=*reinterpret_cast<const uint8_t*>(player_focus_address);
        if(focus>1)return result;
        const auto player=player_info_address+uintptr_t(focus)*0x190;
        const auto ped=*reinterpret_cast<const uintptr_t*>(player);
        int32_t ped_handle=-1;
        if(!ped || !pool_entry(ped_pool_address,ped_stride,3,ped,&ped_handle))return result;
        const auto intelligence=*reinterpret_cast<const uintptr_t*>(ped+0x47C);
        if(!intelligence)return result;
        uintptr_t entry_task=0,entry_vehicle=0;
        for(size_t primary=0;primary<5;++primary) {
            uintptr_t task=*reinterpret_cast<const uintptr_t*>(intelligence+4+primary*4),parent=0;
            for(size_t depth=0;task && depth<32;++depth) {
                if(!live_task(task) || *reinterpret_cast<const uintptr_t*>(task+4)!=parent)return result;
                uint32_t type=0;
                if(!read_task_type(task,&type))return result;
                if(type==enter_car_passenger_type || type==enter_car_driver_type) {
                    const auto vehicle=*reinterpret_cast<const uintptr_t*>(task+0x0C);
                    int32_t handle=-1;
                    if(!vehicle || !pool_entry(vehicle_pool_address,vehicle_stride,2,vehicle,&handle))return result;
                    if(entry_task && (entry_task!=task || entry_vehicle!=vehicle))return result;
                    entry_task=task;entry_vehicle=vehicle;
                }
                uintptr_t child=0;
                const auto child_kind=read_task_child(task,&child);
                if(!child_kind)return result;
                if(child_kind==2)task=0;
                else {parent=task;task=child;}
                if(depth==31 && task)return result;
            }
        }
        if(*reinterpret_cast<const uint8_t*>(player_focus_address)!=focus ||
           *reinterpret_cast<const uintptr_t*>(player)!=ped ||
           !pool_entry(ped_pool_address,ped_stride,3,ped,&ped_handle))return result;
        result.known=true;
        if(entry_task) {
            int32_t handle=-1;
            if(!live_task(entry_task) ||
               *reinterpret_cast<const uintptr_t*>(entry_task+0x0C)!=entry_vehicle ||
               !pool_entry(vehicle_pool_address,vehicle_stride,2,entry_vehicle,&handle))return EntryTargetRead{};
            result.handle=handle;result.entity=entry_vehicle;
        }
        return result;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return EntryTargetRead{};}
}
}

#ifdef LCS_CRANE_TEST_BACKEND
void sa_crane_test_pool_root(uintptr_t address){test_pool_root=address;}
void sa_crane_test_model_info(uintptr_t address){test_model_info=address;}
#endif

bool sa_crane_configure(uint16_t model,const std::array<float,3>& placement) {
    if(!model || model>=20000)return false;
    for(const float value:placement)if(!std::isfinite(value))return false;
    if(rotated && (crane_model!=model || crane_at!=placement))return false;
    crane_model=model;crane_at=placement;configured=true;return true;
}

SaCraneResolve sa_crane_resolve(uintptr_t root,uint16_t model,const std::array<float,3>& placement,uintptr_t& entity) {
    entity=0;
    if(!root || !model || model>=20000)return SaCraneResolve::unavailable;
    for(const float value:placement)if(!std::isfinite(value))return SaCraneResolve::unavailable;
    uintptr_t found=0;
    const auto state=scan(root,model,placement,&found);
    if(state==SaCraneResolve::ready)entity=found;
    return state;
}

std::optional<bool> sa_crane_ready(size_t index) {
    if(!configured || index!=0 || !profile())return {};
    uintptr_t entity=0;
    const auto state=sa_crane_resolve(pool_root(),crane_model,crane_at,entity);
    if(state==SaCraneResolve::unavailable)return {};
    return state==SaCraneResolve::ready;
}

std::optional<RouteVehicleSnapshot> sa_crane_player_entry_target() {
    const auto read=read_player_entry_target();
    if(!read.known || read.handle<0 || !read.entity)return {};
    const auto found=sa_route_vehicle_lookup(read.handle);
    if(found.status!=VehicleLookupStatus::present || !found.snapshot || found.snapshot->entity!=read.entity ||
       found.snapshot->generation!=uint8_t(read.handle))return {};
    return found.snapshot;
}

NativeResult sa_crane_rotate(size_t index,float angle) {
    NativeResult result;
    if(!configured || index!=0 || !std::isfinite(angle) || angle<0.0f || angle>6.2832f) {
        result.diagnostic="Crane angle or binding unavailable";return result;
    }
    if(!profile()){result.diagnostic="Crane native consumer profile unavailable";return result;}
    uintptr_t entity=0;
    const auto found=sa_crane_resolve(pool_root(),crane_model,crane_at,entity);
    if(found!=SaCraneResolve::ready) {
        result.diagnostic=found==SaCraneResolve::streamed_out?"Crane building is streamed out":"Crane building ownership mismatch";return result;
    }
    rotated=true;
    result.executed=turn(entity,angle,&result.native_entered);
    if(result.executed)rotated_to=angle;
    else result.diagnostic="Crane native rotation failed; replay forbidden after entry";
    return result;
}

std::optional<float> sa_crane_vehicle_top(const RouteVehicleSnapshot& expected) {
    if(!live(expected))return {};
    float top=0;
    if(!read_top(expected.entity,&top))return {};
    return live(expected)?std::optional<float>(top):std::nullopt;
}

std::optional<float> sa_crane_vehicle_forward_angle(const RouteVehicleSnapshot& expected) {
    if(!live(expected))return {};
    float angle=0;
    if(!read_forward(expected.entity,&angle))return {};
    return live(expected)?std::optional<float>(angle):std::nullopt;
}

NativeResult sa_crane_hold(const RouteVehicleSnapshot& expected) {
    NativeResult result;
    if(!profile() || expected.handle<0 || !expected.entity){result.diagnostic="Crane vehicle profile/identity unavailable";return result;}
    if(!live(expected)){result.diagnostic="Crane target changed before the physical hold";return result;}
    if(held.active && !same_identity(held.identity,expected)){result.diagnostic="Previous crane target still owns a native weak cell";return result;}
    if(!held.active) {
#ifndef LCS_CRANE_TEST_BACKEND
        HMODULE pinned=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&sa_crane_hold),&pinned)){result.diagnostic="Crane weak-cell owner could not be pinned";return result;}
#endif
        held={};held.identity=expected;held.active=true;
        result.native_entered=true;
        if(!sa_register_vehicle_reference(expected.handle,&held.cell)){
            result.diagnostic="Crane vehicle reference registration uncertain; retained for cleanup";return result;
        }
    }
    result.executed=hold_fields(false,&result.native_entered);
    if(!result.executed)result.diagnostic="Crane physical hold failed; weak cell retained for cleanup";
    return result;
}

NativeResult sa_crane_carry(const RouteVehicleSnapshot& expected,const std::array<float,3>& position,float angle,
                            const std::array<float,3>& up) {
    NativeResult result;
    if(!profile()){result.diagnostic="Crane movement/wake profile unavailable";return result;}
    for(const float v:position)if(!std::isfinite(v)){result.diagnostic="Crane carry position is not finite";return result;}
    for(const float v:up)if(!std::isfinite(v)){result.diagnostic="Crane carry orientation is not finite";return result;}
    if(!std::isfinite(angle) || !held.active || !same_identity(held.identity,expected) || !live(expected) ||
       !sa_vehicle_reference_live(held.identity,&held.cell)) {
        result.diagnostic="Crane carry target is not the held vehicle";return result;
    }
    result.native_entered=true;
    result.executed=write_pose(expected.entity,position,angle,up);
    if(!result.executed)result.diagnostic="Crane carry pose write failed";
    return result;
}

bool sa_crane_release() {
    if(!held.active)return true;
    if(held.cell && held.wrote && !held.restored) {
        if(!profile())return false;
        bool entered=false;
        if(!hold_fields(true,&entered))return false;
    }
    if(!sa_release_vehicle_reference(held.identity,&held.cell))return false;
    held={};return true;
}

bool sa_crane_cleanup() {
    if(!sa_crane_release())return false;
    if(!rotated)return true;
    if(!profile())return false;
    uintptr_t entity=0;
    const auto found=sa_crane_resolve(pool_root(),crane_model,crane_at,entity);
    if(found==SaCraneResolve::streamed_out){rotated=false;return true;}
    if(found!=SaCraneResolve::ready)return false;
    bool entered=false;
    if(!turn(entity,0.0f,&entered))return false;        // approved IPL pose: identity orientation
    rotated=false;return true;
}
}
