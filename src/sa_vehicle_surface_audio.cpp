#include "sa_vehicle_surface_audio.hpp"

#include "lcs/continuous_sound.hpp"
#include "lcs/sa_executor.hpp"
#include <windows.h>
#include <cmath>
#include <cstring>
#include <algorithm>
namespace lcs {
namespace {
struct Call {uintptr_t site,target;unsigned family;bool helper;};
constexpr Call calls[]={
 {0x501B2A,0x4F8B00,0,false},{0x501B79,0x4F8B00,0,false},
 {0x501F9B,0x4F8B00,0,false},{0x50215C,0x4F8B00,0,false},
 {0x501B32,0x4F8F10,1,false},{0x501FB6,0x4F8F10,1,false},
 {0x50205B,0x4F8F10,1,false},{0x502189,0x4F8F10,1,false},
 {0x502170,0x4F8DF0,2,false},
 {0x4F8D35,0x4F84D0,0,true},{0x4F929A,0x4F8360,1,true},
 {0x4F8EF7,0x4F87D0,2,true}
};
using Process=void(__thiscall*)(void*,void*);
using Play=void(__thiscall*)(void*,int32_t,float,float);
std::function<SaVehicleSurfaceAudio*()> provider;
thread_local bool owned[3]{};
bool installed=false;
template<unsigned Family> void __fastcall process(void* self,void*,void* params) {
    const bool previous=owned[Family];owned[Family]=false;
    try {if(provider)if(auto* audio=provider())owned[Family]=audio->request(Family,params);}catch(...){}
    constexpr uintptr_t targets[]={0x4F8B00,0x4F8F10,0x4F8DF0};
    reinterpret_cast<Process>(targets[Family])(self,params);
    owned[Family]=previous;
}
template<unsigned Family> void __fastcall play(void* self,void*,int32_t sample,float speed,float volume) {
    constexpr uintptr_t targets[]={0x4F84D0,0x4F8360,0x4F87D0};
    reinterpret_cast<Play>(targets[Family])(self,sample,speed,owned[Family]?-1000.0f:volume);
}
bool targets(uintptr_t site,uintptr_t target) {
    __try {const auto* p=reinterpret_cast<const uint8_t*>(site);int32_t rel=0;
        std::memcpy(&rel,p+1,4);return *p==0xE8 && uintptr_t(int64_t(site+5)+rel)==target;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool patch(uintptr_t site,uintptr_t expected,uintptr_t replacement) {
    if(!targets(site,expected))return false;
    DWORD old=0;auto* p=reinterpret_cast<uint8_t*>(site);
    if(!VirtualProtect(p,5,PAGE_EXECUTE_READWRITE,&old))return false;
    const int32_t rel=int32_t(int64_t(replacement)-int64_t(site+5));
    std::memcpy(p+1,&rel,4);FlushInstructionCache(GetCurrentProcess(),p,5);
    DWORD ignored=0;
    if(VirtualProtect(p,5,old,&ignored))return true;
    const int32_t original=int32_t(int64_t(expected)-int64_t(site+5));
    std::memcpy(p+1,&original,4);FlushInstructionCache(GetCurrentProcess(),p,5);
    VirtualProtect(p,5,old,&ignored);return false;
}
struct Read {
    uintptr_t vehicle=0;int model=0,type=0;uint8_t surface=0,ground=0,previous_ground=0,gear=0;
    char drive='4';float velocity=0,max_velocity=0,max_reverse=0,wet=0,gas=0,gas_audio=0,turn=0;
    bool engine=false;std::array<float,3> position{};
    std::array<uint32_t,4> wheels{};std::array<float,4> contact{};
};
std::optional<VehicleRoadSurface> read_surface(uint8_t id) {
    if(id>=179)return {};
    uint32_t flags=0;
    __try {flags=(*reinterpret_cast<const uint32_t*>(0xB79538+size_t(id)*12+0x98)>>10)&0x1FFu;}
    __except(EXCEPTION_EXECUTE_HANDLER){return {};}
    using S=VehicleRoadSurface;
    switch(flags) {
    case 1:return S::concrete;case 2:return S::grass;case 4:return S::sand;
    case 8:return S::gravel;case 16:return S::wood;case 32:return S::water;
    case 64:return S::metal;case 128:return S::hedge;case 256:return S::concrete;
    default:return {};
    }
}
bool read(void* params,Read& out) {
    if(!params)return false;
    __try {
        const auto p=reinterpret_cast<uintptr_t>(params);
        out.vehicle=*reinterpret_cast<const uintptr_t*>(p+0x10);
        out.type=*reinterpret_cast<const int*>(p+4);
        const auto transmission=*reinterpret_cast<const uintptr_t*>(p+0x14);
        if(!out.vehicle || !transmission || (out.type!=0 && out.type!=9 && out.type!=10))return false;
        const auto v=out.vehicle;
        out.model=*reinterpret_cast<const uint16_t*>(v+0x22);
        const auto matrix=*reinterpret_cast<const uintptr_t*>(v+0x14);
        std::memcpy(out.position.data(),reinterpret_cast<const void*>(matrix?matrix+0x30:v+4),12);
        out.surface=*reinterpret_cast<const uint8_t*>(v+0xBA);
        out.velocity=*reinterpret_cast<const float*>(p+0x1C);
        out.max_velocity=*reinterpret_cast<const float*>(transmission+0x58);
        out.max_reverse=std::fabs(*reinterpret_cast<const float*>(transmission+0x60));
        out.drive=*reinterpret_cast<const char*>(transmission+0x48);
        out.wet=*reinterpret_cast<const float*>(0xC81308);
        out.gas=*reinterpret_cast<const float*>(v+0x49C);
        out.gear=*reinterpret_cast<const uint8_t*>(v+0x4B8);
        out.engine=(*reinterpret_cast<const uint32_t*>(v+0x428)&0x10)!=0;
        out.turn=*reinterpret_cast<const float*>(v+0x50);
        const bool bike=out.type!=0;
        out.ground=*reinterpret_cast<const uint8_t*>(v+(bike?0x804:0x960));
        out.previous_ground=*reinterpret_cast<const uint8_t*>(v+(bike?0x806:0x962));
        out.gas_audio=*reinterpret_cast<const float*>(v+(bike?0x808:0x964));
        std::memcpy(out.wheels.data(),reinterpret_cast<const void*>(v+(bike?0x80C:0x968)),(bike?2:4)*4);
        std::memcpy(out.contact.data(),reinterpret_cast<const void*>(v+(bike?0x730:0x7F4)),4*4);
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
    return true;
}
}
SaVehicleSurfaceAudio::SaVehicleSurfaceAudio(const AudioArchive& archive,std::shared_ptr<SaAudioMixer> mixer,ModelResolver resolver)
 :archive_(archive),mixer_(std::move(mixer)),resolver_(std::move(resolver)) {
    worker_=std::make_unique<AudioWorker>(nullptr,std::chrono::milliseconds(1000));
    for(unsigned i=0;i<assets_.size();++i)worker_->post([this,i]{try{
        const auto id=vehicle_surface_sample(VehicleSurfaceSample(i));
        const auto record=archive_.record(id);auto pcm=archive_.sample(id);
        const auto span=continuous_pcm_loop_span(record.size,record.loop_start,record.loop_end);
        if(!span || pcm.channels!=1 || pcm.pcm.size()!=record.size || !pcm.frequency)return;
        Asset value{std::make_shared<const AudioSample>(std::move(pcm)),span->begin,span->end};
        std::lock_guard<std::mutex> lock(mutex_);assets_[i]=std::move(value);
    }catch(...){}});
}
SaVehicleSurfaceAudio::~SaVehicleSurfaceAudio(){shutdown();}
bool SaVehicleSurfaceAudio::publish(uint64_t id,const VehicleSurfaceLoop& spec) {
    Asset asset;{std::lock_guard<std::mutex> lock(mutex_);asset=assets_[size_t(spec.sample)];}
    if(!asset.pcm || !mixer_->publish_loop(id,asset.pcm,asset.begin,asset.end,spec.frequency,spec.volume,spec.pan)){release(id);return false;}
    touched_[id]=GetTickCount();return true;
}
void SaVehicleSurfaceAudio::release(uint64_t id){mixer_->release_loop(id);touched_.erase(id);}
bool SaVehicleSurfaceAudio::request(unsigned family,void* params) {
    Read v;if(!mixer_ || !mixer_->available() || !read(params,v) || !resolver_)return false;
    const auto original=resolver_(v.model);if(!original)return false;
    const auto surface=read_surface(v.surface);if(!surface)return false;
    const auto listener=sa_audio_listener();if(!listener)return false;
    float squared=0,right=0;
    for(unsigned i=0;i<3;++i){const float d=v.position[i]-listener->position[i];squared+=d*d;right+=d*listener->right[i];}
    const float distance=std::sqrt(squared);
    const auto key=[&](unsigned kind){return 0x4000000000000000ull|(uint64_t(v.vehicle)<<2)|kind;};
    if(family==0) {
        Asset road;{std::lock_guard<std::mutex> lock(mutex_);road=assets_[0];}
        if(!road.pcm)return false;
        const auto specs=vehicle_road_noise_sounds({distance,right,v.velocity,v.max_velocity,v.wet,road.pcm->frequency,v.ground,*surface});
        if(!specs){release(key(0));release(key(1));return false;}
        if(specs->rolling && !publish(key(0),*specs->rolling)){release(key(1));return false;}
        if(!specs->rolling)release(key(0));
        if(specs->wet && !publish(key(1),*specs->wet)){release(key(0));return false;}
        if(!specs->wet)release(key(1));return true;
    }
    if(family==1) {
        VehicleSkidInputs in;in.distance=distance;in.camera_x=right;in.velocity=v.velocity;
        in.max_velocity=v.max_velocity;in.turn_speed_z=v.turn;in.gas_audio=v.gas_audio;
        in.wheels_on_ground=v.ground;in.surface=*surface;in.bike=v.type!=0;in.wheel_count=in.bike?2:4;
        for(unsigned i=0;i<in.wheel_count;++i) {
            if(v.wheels[i]>3)return false;
            in.wheels[i]={VehicleWheelState(v.wheels[i]),in.bike?std::max(v.contact[i*2],v.contact[i*2+1]):v.contact[i],in.bike?i==1:(v.drive=='4'||(v.drive=='F'?(i==0||i==2):(i==1||i==3)))};
        }
        const auto spec=vehicle_skid_sound(in);
        if(!spec){release(key(2));return false;}return publish(key(2),*spec);
    }
    if(family==2 && vehicle_reverse_eligible(*original)) {
        bool drive_ground=false;
        for(unsigned i=0;i<4;++i)if(v.contact[i]>0 && (v.drive=='4'||(v.drive=='F'?(i==0||i==2):(i==1||i==3))))drive_ground=true;
        const auto spec=vehicle_reverse_sound({distance,right,v.velocity,v.max_reverse,v.gas_audio,v.engine,v.gas<0,v.gear==0,drive_ground,v.previous_ground!=0});
        if(!spec){release(key(3));return false;}return publish(key(3),*spec);
    }
    return false;
}
void SaVehicleSurfaceAudio::service(uint32_t now,bool paused) {
    for(auto it=touched_.begin();it!=touched_.end();) {
        if(paused || uint32_t(now-it->second)>400){mixer_->release_loop(it->first);it=touched_.erase(it);}else ++it;
    }
}
void SaVehicleSurfaceAudio::shutdown(){worker_.reset();if(mixer_)for(const auto& item:touched_)mixer_->release_loop(item.first);touched_.clear();}
bool sa_vehicle_surface_audio_install(std::function<SaVehicleSurfaceAudio*()> value) {
    if(installed || !value || !sa_executor_profile_matches())return false;
    for(const auto& call:calls)if(!targets(call.site,call.target))return false;
    const uintptr_t processors[]={reinterpret_cast<uintptr_t>(&process<0>),reinterpret_cast<uintptr_t>(&process<1>),reinterpret_cast<uintptr_t>(&process<2>)};
    const uintptr_t helpers[]={reinterpret_cast<uintptr_t>(&play<0>),reinterpret_cast<uintptr_t>(&play<1>),reinterpret_cast<uintptr_t>(&play<2>)};
    size_t count=0;
    for(const auto& call:calls){const auto hook=(call.helper?helpers:processors)[call.family];
        if(!patch(call.site,call.target,hook)) {
            while(count){const auto& undo=calls[--count];patch(undo.site,(undo.helper?helpers:processors)[undo.family],undo.target);}return false;
        }++count;
    }
    provider=std::move(value);installed=true;return true;
}
}
