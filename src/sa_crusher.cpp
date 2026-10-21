#include <windows.h>
#include "sa_crusher.hpp"
#include "sa_lcs_bridge.hpp"
#include "lcs/route_vehicle_native.hpp"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace lcs {
namespace {
uint16_t body_model=0,lid_model=0;
bool touched=false;
const char* unavailable_reason="";
struct HeldVehicle {
    RouteVehicleSnapshot identity;
    uintptr_t cell=0;
    uint8_t collision=0,gravity=0;
    bool active=false,flags_saved=false,wrote=false,restored=false;
} held;
constexpr float half_pi=1.57079632679f;
constexpr float body_at[3]={1139.728759765625f,60.38515090942383f,1.098055362701416f};
constexpr float lid_at[3]={1139.548583984375f,65.05116271972656f,4.590259552001953f};
constexpr unsigned char allocate_profile[]={0x56,0x8B,0xF1,0x8B,0x46,0x14,0x85,0xC0,0x75,0x61,0xA1,0x28,0x44,0xB7,0x00,0x3D};
constexpr unsigned char rotate_profile[]={0xD9,0x44,0x24,0x04,0x33,0xC0,0xD9,0xFF,0xC7,0x01,0x00,0x00,0x80,0x3F,0x89,0x41};
constexpr unsigned char contains_profile[]={0x83,0xEC,0x10,0x53,0x55,0x8B,0x6C,0x24,0x1C,0x8B,0xD1,0x8B,0x4D,0x14,0x85,0xC9};
constexpr unsigned char point_profile[]={0xD9,0x41,0x08,0x83,0xEC,0x08,0xD8,0x64,0x24,0x18,0xD8,0x5C,0x24,0x14,0xDF,0xE0};
// Retail CGarage::Update callsites for the verified 0x448EE0 touching and
// 0x448D30 entirely-outside consumers. These bytes pin the call targets as
// well as the native code path before the adapter invokes either method.
constexpr unsigned char touching_profile_a[]={0x50,0x8B,0xCE,0xE8,0x27,0xD6,0xFF,0xFF,0xE9,0x9C,0x0D,0x00,0x00};
constexpr unsigned char touching_profile_b[]={0x50,0x8B,0xCE,0xE8,0x96,0xC8,0xFF,0xFF,0x84,0xC0,0x0F,0x85,0x72,0x01,0x00,0x00};
constexpr unsigned char outside_profile[]={0x68,0x00,0x00,0x00,0x40,0x50,0x8B,0xCE,0xE8,0x10,0xD4,0xFF,0xFF,0x84,0xC0,0x0F,0x84,0x9C,0x0E,0x00,0x00};
bool valid_models(uint16_t body,uint16_t lid) {return body && lid && body<20000 && lid<20000 && body!=lid;}
bool close(float x,float y) {return std::isfinite(x) && std::fabs(x-y)<=0.25f;}
bool profile() {
    if(!sa_lcs_bridge_profile_matches())return false; // Reuse approved world/RW consumer profiles.
#ifdef LCS_CRUSHER_TEST_BACKEND
    return true;
#else
    __try {
        return !std::memcmp(reinterpret_cast<void*>(0x54F560),allocate_profile,sizeof(allocate_profile)) &&
               !std::memcmp(reinterpret_cast<void*>(0x59AFA0),rotate_profile,sizeof(rotate_profile));
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
#endif
}
bool hold_fields(bool restore,bool* entered,float step=1.0f,bool locked=true) {
    __try {
        if(!held.cell || !sa_vehicle_reference_live(held.identity,&held.cell))return false;
        auto* collision=reinterpret_cast<uint8_t*>(held.cell+0x1C);
        auto* physics=reinterpret_cast<uint8_t*>(held.cell+0x40);
        if((restore || locked) && !held.flags_saved) {
            held.collision=*collision&1u;held.gravity=*physics&2u;held.flags_saved=true;
        }
        float speeds[3]{};
        const float damping=std::pow(0.8f,step);
        if(!restore)for(int i=0;i<3;++i) {
            const auto previous=*reinterpret_cast<const float*>(held.cell+0x44+i*4);
            if(!std::isfinite(previous))return false;
            speeds[i]=locked?0.0f:previous*damping;
        }
        *entered=true;
        if(restore || locked) {
            held.wrote=true;
            *collision=uint8_t((*collision&~1u)|(restore?held.collision:0));
            *physics=uint8_t((*physics&~2u)|(restore?held.gravity:0));
        }
        if(!restore)for(int i=0;i<3;++i)*reinterpret_cast<float*>(held.cell+0x44+i*4)=speeds[i];
        const bool confirmed=(!(restore || locked) || ((*collision&1u)==(restore?held.collision:0) &&
            (*physics&2u)==(restore?held.gravity:0))) && sa_vehicle_reference_live(held.identity,&held.cell);
        held.restored=restore && confirmed;
        return confirmed;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
// No C++ unwind object crosses this SEH boundary. Entry is reported before any
// allocation/world mutation; an uncertain native result must never be replayed.
bool apply(SaCrusherEntities entities,float angle,bool* entered) {
    __try {
        uintptr_t matrix=*reinterpret_cast<uintptr_t*>(entities.lid+0x14);
        *entered=true;
        if(!matrix) {
            reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x54F560)(entities.lid);
            matrix=*reinterpret_cast<uintptr_t*>(entities.lid+0x14);
        }
        if(!matrix)return false;
        // CGarage::UpdateCrusherAngle: SetRotateXOnly(TWOPI-door), UpdateRW,
        // UpdateRwFrame. The hinge position is fixed, as in the original;
        // no removal/reinsertion or change to another entity is necessary.
        reinterpret_cast<void(__thiscall*)(uintptr_t,float)>(0x59AFA0)(matrix,2.0f*3.14159265359f-angle);
        reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x446F90)(entities.lid);
        reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x532B00)(entities.lid);
        const float c=std::cos(-angle),s=std::sin(-angle);
        const float actual[]={*reinterpret_cast<float*>(matrix),*reinterpret_cast<float*>(matrix+0x14),
            *reinterpret_cast<float*>(matrix+0x18),*reinterpret_cast<float*>(matrix+0x24),*reinterpret_cast<float*>(matrix+0x28)};
        const float expected[]={1.0f,c,s,-s,c};
        for(int i=0;i<5;++i)if(!std::isfinite(actual[i]) || std::fabs(actual[i]-expected[i])>0.0001f)return false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
// Only the read-only geometry prefix of CGarage is used by448BE0/4487D0.
// This query does not register a garage, discover doors or initialize audio.
struct GarageGeometry {
    float position[3],direction_a[2],direction_b[2],top_z,width,height,left,right,front,back;
};
static_assert(sizeof(GarageGeometry)==0x38);
bool make_geometry(const std::array<float,8>& b,GarageGeometry& g) {
    for(const float value:b)if(!std::isfinite(value))return false;
    g.position[0]=b[0];g.position[1]=b[1];g.position[2]=b[2];g.top_z=b[7];
    g.width=std::hypot(b[3]-b[0],b[4]-b[1]);g.height=std::hypot(b[5]-b[0],b[6]-b[1]);
    if(!(g.width>0) || !(g.height>0) || !std::isfinite(g.width) || !std::isfinite(g.height) || g.top_z<g.position[2])return false;
    g.direction_a[0]=(b[3]-b[0])/g.width;g.direction_a[1]=(b[4]-b[1])/g.width;
    g.direction_b[0]=(b[5]-b[0])/g.height;g.direction_b[1]=(b[6]-b[1])/g.height;
    g.left=std::min({b[0],b[3],b[5],b[3]+b[5]-b[0]});g.right=std::max({b[0],b[3],b[5],b[3]+b[5]-b[0]});
    g.front=std::min({b[1],b[4],b[6],b[4]+b[6]-b[1]});g.back=std::max({b[1],b[4],b[6],b[4]+b[6]-b[1]});
    return true;
}
bool geometry_query_profile() {
    if(!sa_lcs_bridge_profile_matches())return false;
#ifdef LCS_CRUSHER_TEST_BACKEND
    return true;
#else
    __try {
        return !std::memcmp(reinterpret_cast<void*>(0x44B8B1),touching_profile_a,sizeof(touching_profile_a)) &&
               !std::memcmp(reinterpret_cast<void*>(0x44C642),touching_profile_b,sizeof(touching_profile_b)) &&
               !std::memcmp(reinterpret_cast<void*>(0x44B913),outside_profile,sizeof(outside_profile));
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
#endif
}
int contains(const std::array<float,8>& b,uintptr_t entity) {
    GarageGeometry g{};
    if(!make_geometry(b,g))return -1;
    __try {
        if(std::memcmp(reinterpret_cast<void*>(0x448BE0),contains_profile,sizeof(contains_profile)) ||
           std::memcmp(reinterpret_cast<void*>(0x4487D0),point_profile,sizeof(point_profile)))return -1;
        return reinterpret_cast<bool(__thiscall*)(const GarageGeometry*,uintptr_t,float)>(0x448BE0)(&g,entity,0.0f)?1:0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return -1;}
}
int touches(const std::array<float,8>& b,uintptr_t entity) {
    GarageGeometry g{};
    if(!make_geometry(b,g) || !geometry_query_profile())return -1;
    __try {
        return reinterpret_cast<bool(__thiscall*)(const GarageGeometry*,uintptr_t)>(0x448EE0)(&g,entity)?1:0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return -1;}
}
int entirely_outside(const std::array<float,8>& b,uintptr_t entity,float margin) {
    GarageGeometry g{};
    if(!std::isfinite(margin) || margin<0.0f || !make_geometry(b,g) || !geometry_query_profile())return -1;
    __try {
        return reinterpret_cast<bool(__thiscall*)(const GarageGeometry*,uintptr_t,float)>(0x448D30)(&g,entity,margin)?1:0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return -1;}
}
}

bool sa_crusher_configure(uint16_t body,uint16_t lid) {
    if(!valid_models(body,lid))return false;
    if(touched && (body_model!=body || lid_model!=lid))return false;
    body_model=body;lid_model=lid;return true;
}

const char* sa_crusher_unavailable_reason(){return unavailable_reason;}
std::optional<bool> sa_crusher_ready() {
    if(!valid_models(body_model,lid_model)){unavailable_reason="crushercrushB/crushertop model bindings not configured";return {};}
    if(!profile()){unavailable_reason="native bridge/matrix profile mismatch";return {};}
    SaCrusherEntities entities;
    const auto state=sa_crusher_resolve(0xB74498,body_model,lid_model,entities);
    if(state==SaCrusherResolve::unavailable)return {};
    return state==SaCrusherResolve::ready;
}

SaCrusherResolve sa_crusher_resolve(uintptr_t pool_root,uint16_t body,uint16_t lid,SaCrusherEntities& out) {
    out={};if(!pool_root || !valid_models(body,lid)){unavailable_reason="invalid pool/model request";return SaCrusherResolve::unavailable;}
    __try {
        const auto pool=*reinterpret_cast<const uintptr_t*>(pool_root);
        if(!pool)return SaCrusherResolve::streamed_out;
        const auto data=*reinterpret_cast<const uintptr_t*>(pool);
        const auto flags=*reinterpret_cast<const uintptr_t*>(pool+4);
        const auto count=*reinterpret_cast<const int32_t*>(pool+8);
        if(!data || !flags || count<0 || count>200000){unavailable_reason="building pool unreadable";return SaCrusherResolve::unavailable;}
        for(int32_t slot=0;slot<count;++slot) {
            if(*reinterpret_cast<const uint8_t*>(flags+slot)&0x80)continue;
            const uintptr_t entity=data+uintptr_t(slot)*0x38;
            const auto model=*reinterpret_cast<const uint16_t*>(entity+0x22);
            if(model!=body && model!=lid)continue;
            if((*reinterpret_cast<const uint8_t*>(entity+0x36)&7)!=1){unavailable_reason="crusher model entity is not a CBuilding";return SaCrusherResolve::unavailable;}
            const auto matrix=*reinterpret_cast<const uintptr_t*>(entity+0x14);
            const auto position=matrix?matrix+0x30:entity+4;
            const auto* at=model==body?body_at:lid_at;
            for(int i=0;i<3;++i)if(!close(*reinterpret_cast<const float*>(position+i*4),at[i])){
                unavailable_reason=model==body?"crushercrushB instance is not at its IPL placement":"crushertop instance is not at its IPL placement";return SaCrusherResolve::unavailable;}
            auto& target=model==body?out.body:out.lid;
            if(target){unavailable_reason=model==body?"duplicate crushercrushB building":"duplicate crushertop building";return SaCrusherResolve::unavailable;}
            target=entity;
        }
        return out.body && out.lid?SaCrusherResolve::ready:SaCrusherResolve::streamed_out;
    } __except(EXCEPTION_EXECUTE_HANDLER) {out={};unavailable_reason="building pool read faulted";return SaCrusherResolve::unavailable;}
}

NativeResult sa_crusher_door(int32_t garage,const std::array<float,8>& bounds,float angle) {
    NativeResult result;
    if(garage<0 || !std::isfinite(angle) || angle<0 || angle>half_pi || !valid_models(body_model,lid_model)) {
        result.diagnostic="Crusher angle/model binding unavailable";return result;
    }
    for(const auto value:bounds)if(!std::isfinite(value)) {result.diagnostic="Crusher bounds are not finite";return result;}
    // Restrict this physical consumer to the original Portland crusher bay.
    const float cx=(bounds[3]+bounds[5])*0.5f,cy=(bounds[4]+bounds[6])*0.5f;
    if(std::fabs(cx-body_at[0])>=20 || std::fabs(cy-body_at[1])>=20 || !profile()) {
        result.diagnostic="Crusher placement or native consumer profile unavailable";return result;
    }
    SaCrusherEntities entities;
    const auto found=sa_crusher_resolve(0xB74498,body_model,lid_model,entities);
    if(found!=SaCrusherResolve::ready) {
        result.diagnostic=found==SaCrusherResolve::streamed_out?"Crusher meshes are streamed out":"Crusher entity ownership mismatch";return result;
    }
    touched=true;
    result.executed=apply(entities,angle,&result.native_entered);
    if(!result.executed)result.diagnostic="Crusher native transform failed; replay forbidden after entry";
    return result;
}

std::optional<bool> sa_crusher_contains(const std::array<float,8>& bounds,const RouteVehicleSnapshot& expected) {
    if(!sa_lcs_bridge_profile_matches())return {};
    const auto before=sa_route_vehicle_lookup(expected.handle);
    if(!before.snapshot || before.snapshot->entity!=expected.entity || before.snapshot->generation!=expected.generation)return {};
    const auto answer=contains(bounds,expected.entity);
    const auto after=sa_route_vehicle_lookup(expected.handle);
    if(answer<0 || !after.snapshot || after.snapshot->entity!=expected.entity || after.snapshot->generation!=expected.generation)return {};
    return answer!=0;
}

std::optional<bool> sa_crusher_collector_vehicle_touches(const std::array<float,8>& bounds,const RouteVehicleSnapshot& expected) {
    if(!geometry_query_profile())return {};
    const auto before=sa_route_vehicle_lookup(expected.handle);
    if(!before.snapshot || before.snapshot->entity!=expected.entity || before.snapshot->generation!=expected.generation)return {};
    const auto answer=touches(bounds,expected.entity);
    const auto after=sa_route_vehicle_lookup(expected.handle);
    if(answer<0 || !after.snapshot || after.snapshot->entity!=expected.entity || after.snapshot->generation!=expected.generation)return {};
    return answer!=0;
}

std::optional<bool> sa_crusher_collector_player_outside(const std::array<float,8>& bounds,const ExclusiveGaragePlayer& player,float margin) {
    if(player.ped<0 || !player.ped_entity || !std::isfinite(margin) || margin<0.0f || !geometry_query_profile())return {};
    const auto before=sa_garage_ped_identity(player.ped);
    if(!before || *before!=player.ped_entity)return {};
    const auto answer=entirely_outside(bounds,player.ped_entity,margin);
    const auto after=sa_garage_ped_identity(player.ped);
    if(answer<0 || !after || *after!=player.ped_entity)return {};
    return answer!=0;
}

std::optional<bool> sa_crusher_collector_vehicle_deliverable(const RouteVehicleSnapshot& expected) {
    if(!sa_lcs_bridge_profile_matches() || expected.handle<0 || !expected.entity ||
       !std::isfinite(expected.health) || expected.native_class<0 || expected.native_class>11)return {};
    const auto before=sa_route_vehicle_lookup(expected.handle);
    if(!before.snapshot || before.snapshot->entity!=expected.entity || before.snapshot->generation!=expected.generation)return {};
    if(!expected.alive || expected.health<250.0f)return false;
    bool deliverable=false;
    __try {
        const auto native_status=int32_t(*reinterpret_cast<const uint8_t*>(expected.entity+0x36)>>3);
        // SA's entity status ordinals preserve source STATUS_WRECKED (5).
        deliverable=native_status!=5;
        if(deliverable && expected.native_class==0) {
            // CAutomobile: CDamageManager starts at +0x5A0; engine status is
            // +0x5A4; m_fireParticleCounter follows m_harvesterParticleCounter
            // at +0x981 in the verified 0x988-byte CAutomobile layout.
            const auto engine_status=*reinterpret_cast<const uint8_t*>(expected.entity+0x5A4);
            const auto fire_particles=*reinterpret_cast<const uint8_t*>(expected.entity+0x981);
            deliverable=engine_status<225 && fire_particles==0;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {return {};}
    const auto after=sa_route_vehicle_lookup(expected.handle);
    if(!after.snapshot || after.snapshot->entity!=expected.entity || after.snapshot->generation!=expected.generation)return {};
    return deliverable;
}

NativeResult sa_crusher_hold(const RouteVehicleSnapshot& expected,float step,bool locked) {
    NativeResult result;
    if(!profile() || expected.handle<0 || !expected.entity || !std::isfinite(step) || step<0 || step>10) {result.diagnostic="Crusher physics profile/identity unavailable";return result;}
    const auto current=sa_route_vehicle_lookup(expected.handle);
    if(!current.snapshot || current.snapshot->entity!=expected.entity || current.snapshot->generation!=expected.generation) {
        result.diagnostic="Crusher target changed before physics mutation";return result;
    }
    if(held.active && (held.identity.handle!=expected.handle || held.identity.entity!=expected.entity ||
                      held.identity.generation!=expected.generation)) {
        result.diagnostic="Previous crusher target still owns a native weak cell";return result;
    }
    if(!held.active) {
#ifndef LCS_CRUSHER_TEST_BACKEND
        HMODULE pinned=nullptr;
        if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&sa_crusher_hold),&pinned)) {
            result.diagnostic="Crusher weak-cell owner could not be pinned";return result;
        }
#endif
        held.identity=expected;held.active=true;
        result.native_entered=true;
        if(!sa_register_vehicle_reference(expected.handle,&held.cell)) {
            result.diagnostic="Crusher vehicle reference registration uncertain; retained for cleanup";return result;
        }
    }
    result.executed=hold_fields(false,&result.native_entered,step,locked);
    if(!result.executed)result.diagnostic="Crusher physics update failed; weak cell retained for cleanup";
    return result;
}

bool sa_crusher_release_vehicle() {
    if(!held.active)return true;
    if(held.cell && held.wrote && !held.restored) {
        if(!profile())return false;
        bool entered=false;
        if(!hold_fields(true,&entered))return false;
    }
    if(!sa_release_vehicle_reference(held.identity,&held.cell))return false;
    held={};return true;
}

bool sa_crusher_cleanup() {
    if(!sa_crusher_release_vehicle())return false;
    if(!touched)return true;
    if(!profile())return false;
    SaCrusherEntities entities;
    const auto found=sa_crusher_resolve(0xB74498,body_model,lid_model,entities);
    if(found==SaCrusherResolve::streamed_out) {touched=false;return true;}
    if(found!=SaCrusherResolve::ready)return false;
    bool entered=false;
    if(!apply(entities,0.0f,&entered))return false; // Restore original approved IPL pose.
    touched=false;return true;
}
}
