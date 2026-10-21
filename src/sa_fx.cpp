#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "lcs/native_memory.hpp"
#include "lcs/sa_fx.hpp"

namespace lcs {

#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
uintptr_t native_fx_create(const char* name,const float* position);
void native_fx_play(uintptr_t system);
void native_fx_kill(uintptr_t system);
void native_fx_add_particle(uintptr_t system,const float* position,const float* velocity,float time_since,
    const float* multiplier,float rotation,float brightness,float size,bool local);
#endif

namespace {
constexpr uintptr_t fx_manager=0xA9AE80;       // FxManager_c g_fxMan
constexpr uintptr_t manager_list_head=0x0C;    // CList: head, tail, count
constexpr uintptr_t manager_list_count=0x14;
constexpr uintptr_t system_next=4,system_blueprint=8,system_status=0x50,system_kill_flag=0x51;
constexpr uint8_t status_playing=0,killed_flag=2;
constexpr size_t max_list_walk=8192;

// Retail 1.0 US entry profiles (first sixteen bytes) of the entry points used below.
constexpr uint8_t create_profile[]={0x8B,0x44,0x24,0x10,0x8B,0x54,0x24,0x08,0x56,0x50,0x8B,0x44,0x24,0x0C,0x8B,0xF1};
constexpr uint8_t play_profile[]={0x51,0x56,0x8B,0xF1,0x80,0x7E,0x50,0x02,0x57,0x74,0x2C,0x8B,0x46,0x08,0xC7,0x46};
constexpr uint8_t kill_profile[]={0x56,0x8B,0xF1,0xE8,0x98,0xFF,0xFF,0xFF,0xC6,0x46,0x51,0x02,0x5E,0xC3,0x90,0x90};
constexpr uint8_t add_particle_profile[]={0x83,0xEC,0x08,0x56,0x57,0x8B,0xF9,0x89,0x7C,0x24,0x0C,0xE8,0xCE,0x76,0x37,0x00};

// system_nonce is the per-Play random offset Play (0x4AA2F0) stores at +0x64 and prims is the
// primitive array pointer at +0x78. With the blueprint they fingerprint one SA allocation; this
// narrows address reuse but is NOT a proof against it (see AUDITORIA, ABA section).
constexpr uintptr_t system_nonce=0x64,system_prims=0x78;
struct Owned {uintptr_t system=0,blueprint=0;uint32_t nonce=0;uintptr_t prims=0;};
struct FireSlot {Owned owned;uint16_t generation=1;bool used=false;bool dying=false;};
std::array<Owned,3> pool;          // blood, smoke_light, smoke_dark
std::vector<FireSlot> fires;
// Systems created by this backend whose Kill is not yet confirmed (e.g. Play failed and Kill
// failed too). The reference is retained until the stop is confirmed or SA itself removed it.
std::vector<Owned> pending;

bool profile(uintptr_t address,const uint8_t* bytes,size_t length) {
    __try {
        return std::memcmp(reinterpret_cast<const void*>(native_entity_address(address)),bytes,length)==0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool in_manager_list(uintptr_t system) {
    __try {
        const auto base=native_entity_address(fx_manager);
        const auto count=*reinterpret_cast<const uint32_t*>(base+manager_list_count);
        auto node=*reinterpret_cast<const uintptr_t*>(base+manager_list_head);
        for(size_t step=0;node && step<max_list_walk && step<=count;++step) {
            if(node==system)return true;
            node=*reinterpret_cast<const uintptr_t*>(node+system_next);
        }
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool read_system(uintptr_t system,uintptr_t* blueprint,uint8_t* status,uint8_t* kill) {
    __try {
        *blueprint=*reinterpret_cast<const uintptr_t*>(system+system_blueprint);
        *status=*reinterpret_cast<const uint8_t*>(system+system_status);
        *kill=*reinterpret_cast<const uint8_t*>(system+system_kill_flag);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool read_fingerprint(uintptr_t system,uint32_t* nonce,uintptr_t* prims) {
    __try {
        *nonce=*reinterpret_cast<const uint32_t*>(system+system_nonce);
        *prims=*reinterpret_cast<const uintptr_t*>(system+system_prims);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

// Records the identity of a system right after it was created/played.
bool identify(Owned& owned) {
    uint8_t status=0,kill=0;
    return read_system(owned.system,&owned.blueprint,&status,&kill) && owned.blueprint &&
        read_fingerprint(owned.system,&owned.nonce,&owned.prims);
}

// Still the live SA system created by this backend: in FxManager's list, same blueprint,
// same Play nonce and primitive array, and without SA's kill flag.
bool alive(const Owned& owned) {
    if(!owned.system || !owned.blueprint || !in_manager_list(owned.system))return false;
    uintptr_t blueprint=0,prims=0;uint8_t status=0,kill=0;uint32_t nonce=0;
    return read_system(owned.system,&blueprint,&status,&kill) && blueprint==owned.blueprint &&
        kill!=killed_flag && read_fingerprint(owned.system,&nonce,&prims) &&
        nonce==owned.nonce && prims==owned.prims;
}

bool call_create(const char* name,const float* position,uintptr_t* system) {
    __try {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        *system=native_fx_create(name,position);
#else
        *system=reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t,const char*,const float*,void*,bool)>(
            native_entity_address(0x4A9BE0))(native_entity_address(fx_manager),name,position,nullptr,true);
#endif
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool call_play(uintptr_t system) {
    __try {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        native_fx_play(system);
#else
        reinterpret_cast<void(__thiscall*)(uintptr_t)>(native_entity_address(0x4AA2F0))(system);
#endif
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool call_kill(uintptr_t system) {
    __try {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        native_fx_kill(system);
#else
        reinterpret_cast<void(__thiscall*)(uintptr_t)>(native_entity_address(0x4AA3F0))(system);
#endif
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool call_add_particle(uintptr_t system,const float* position,const float* velocity,const float* multiplier) {
    __try {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        native_fx_add_particle(system,position,velocity,0.0f,multiplier,-1.0f,1.0f,1.0f,false);
#else
        reinterpret_cast<void(__thiscall*)(uintptr_t,const float*,const float*,float,const float*,float,float,float,bool)>(
            native_entity_address(0x4AA440))(system,position,velocity,0.0f,multiplier,-1.0f,1.0f,1.0f,false);
#endif
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

// True once the system is confirmed stopped: SA already removed it, carries SA's kill flag, or
// our Kill call just completed. False keeps ownership with the caller (Kill refused/faulted).
bool kill_confirmed(const Owned& owned) {
    if(!alive(owned))return true;
    if(!call_kill(owned.system))return false;
    return !alive(owned);
}

// A system this backend created and could not stop stays referenced until a Kill is confirmed.
void retain_unconfirmed(const Owned& owned) {pending.push_back(owned);}

// Creates and starts a system. Anything created is killed again when it cannot be started; if
// that Kill is also refused the system is retained (never dropped) until a later Kill is confirmed.
bool create_owned(const char* name,const float* position,Owned& out,FxStatus& status) {
    if(!sa_fx_profiles_match()){status.diagnostic="SA FxManager entry points do not match retail 1.0 US";return false;}
    uintptr_t system=0;
    if(!call_create(name,position,&system)){status.native_entered=true;status.diagnostic="FxManager CreateFxSystem faulted";return false;}
    status.native_entered=true;
    if(!system){status.diagnostic=std::string("SA has no particle blueprint named ")+name;return false;}
    Owned created;created.system=system;
    if(!in_manager_list(system) || !identify(created) || !alive(created)) {
        status.diagnostic="CreateFxSystem returned a system that is not in FxManager's list";return false;
    }
    const auto abandon=[&](const char* why){
        Owned current=created;identify(current); // Play may already have changed the nonce
        if(!kill_confirmed(current))retain_unconfirmed(current);
        status.diagnostic=why;return false;
    };
    if(!call_play(system))return abandon("FxSystem Play faulted");
    Owned played;played.system=system;uint8_t state=0,kill=0;
    if(!identify(played) || !read_system(system,&played.blueprint,&state,&kill) || state!=status_playing)
        return abandon("FxSystem did not enter the playing state");
    out=played;return true;
}

// Retries every unconfirmed stop. Independent of any campaign: it is also driven after abort/halt.
void drain_unconfirmed() {
    for(auto it=pending.begin();it!=pending.end();)
        it=kill_confirmed(*it)?pending.erase(it):it+1;
    for(auto& slot:fires) {
        if(!slot.used || !slot.dying || !kill_confirmed(slot.owned))continue;
        slot.used=false;slot.dying=false;slot.owned=Owned{};
        if(++slot.generation==0)slot.generation=1;
    }
}

const char* kind_name(FxSystemKind kind) {
    switch(kind) {
        case FxSystemKind::blood: return "prt_blood";
        case FxSystemKind::smoke_light: return "prt_collisionsmoke";
        default: return "prt_smokeII_3_expand";
    }
}
// Blueprint SIZEX at its reference key (effects.fxp): prt_blood 0.1 constant; prt_smokeII
// 0.5->2.0->10.0 (mid key 2.0), prt_collisionsmoke uses the same reference.
float kind_reference_size(FxSystemKind kind) {return kind==FxSystemKind::blood?0.1f:2.0f;}

uint32_t make_token(size_t index,uint16_t generation) {return (uint32_t(generation)<<16)|uint32_t(index+1);}

FireSlot* slot_for(uint32_t token) {
    const size_t index=size_t(token&0xFFFF);
    if(!index || index>fires.size())return nullptr;
    auto& slot=fires[index-1];
    return slot.used && slot.generation==uint16_t(token>>16)?&slot:nullptr;
}
}

bool sa_fx_profiles_match() {
    return profile(0x4A9BE0,create_profile,sizeof(create_profile)) &&
        profile(0x4AA2F0,play_profile,sizeof(play_profile)) &&
        profile(0x4AA3F0,kill_profile,sizeof(kill_profile)) &&
        profile(0x4AA440,add_particle_profile,sizeof(add_particle_profile));
}

FxStatus sa_fx_emit(const FxParticleRequest& request) {
    drain_unconfirmed();
    FxStatus status;
    for(size_t axis=0;axis<3;++axis)
        if(!std::isfinite(request.position[axis]) || !std::isfinite(request.velocity[axis])){
            status.diagnostic="Particle vector is not finite";return status;
        }
    if(!std::isfinite(request.size) || request.size<0){status.diagnostic="Particle size is invalid";return status;}
    auto& owned=pool[size_t(request.kind)];
    if(!alive(owned)) {
        owned=Owned{};
        const float origin[3]={0,0,0}; // SA's static particle systems are created at the origin
        if(!create_owned(kind_name(request.kind),origin,owned,status))return status;
    } else if(!sa_fx_profiles_match()) {
        status.diagnostic="SA FxSystem AddParticle profile changed";return status;
    }
    // FxPrtMult_c: r,g,b,a,size,unused,life. LCS velocity is per 30 Hz frame; SA takes m/s.
    const float scale=request.size>0?std::min(25.0f,std::max(0.05f,request.size/kind_reference_size(request.kind))):1.0f;
    const float multiplier[7]={
        request.has_color?request.red/255.0f:1.0f,request.has_color?request.green/255.0f:1.0f,
        request.has_color?request.blue/255.0f:1.0f,request.has_color?request.alpha/255.0f:1.0f,scale,0.0f,1.0f};
    const float velocity[3]={request.velocity[0]*30.0f,request.velocity[1]*30.0f,request.velocity[2]*30.0f};
    const float position[3]={request.position[0],request.position[1],request.position[2]};
    status.native_entered=true;
    if(!call_add_particle(owned.system,position,velocity,multiplier)){
        status.diagnostic="FxSystem AddParticle faulted";return status;
    }
    status.ok=true;return status;
}

uint32_t sa_fx_fire_start(const FxVec3& position,float strength,FxStatus& status) {
    drain_unconfirmed();
    for(float value:position)if(!std::isfinite(value)){status.diagnostic="Fire position is not finite";return 0;}
    if(!std::isfinite(strength) || strength<0){status.diagnostic="Fire size is invalid";return 0;}
    const char* name=strength<=1.0f?"fire":strength<=2.0f?"fire_med":"fire_large";
    Owned owned;
    const float place[3]={position[0],position[1],position[2]};
    if(!create_owned(name,place,owned,status))return 0;
    size_t index=0;
    for(;index<fires.size();++index)if(!fires[index].used)break;
    if(index==fires.size()) {
        if(fires.size()>=0xFFFF){
            if(!kill_confirmed(owned))retain_unconfirmed(owned);
            status.diagnostic="Fire table is full";return 0;
        }
        fires.emplace_back();
    }
    auto& slot=fires[index];
    slot.owned=owned;slot.used=true;slot.dying=false;
    return make_token(index,slot.generation);
}

bool sa_fx_fire_stop(uint32_t token) {
    auto* slot=slot_for(token);
    if(!slot)return true;
    if(!kill_confirmed(slot->owned)) {
        slot->dying=true; // stays referenced; sa_fx_service_pending() keeps retrying without any campaign
        return false;
    }
    slot->used=false;slot->dying=false;slot->owned=Owned{};
    if(++slot->generation==0)slot->generation=1;
    return true;
}

bool sa_fx_release_all() {
    for(auto& owned:pool) {
        if(!kill_confirmed(owned))retain_unconfirmed(owned);
        owned=Owned{};
    }
    // A fire whose Kill is refused keeps its slot, generation and reference.
    for(auto& slot:fires)
        if(slot.used && !kill_confirmed(slot.owned))slot.dying=true;
        else if(slot.used){
            slot.used=false;slot.dying=false;slot.owned=Owned{};
            if(++slot.generation==0)slot.generation=1;
        }
    return sa_fx_pending()==0;
}

void sa_fx_service_pending() {drain_unconfirmed();}

size_t sa_fx_pending() {
    size_t count=pending.size();
    for(const auto& slot:fires)if(slot.used && slot.dying)++count;
    return count;
}

}
