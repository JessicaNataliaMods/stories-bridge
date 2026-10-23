#include <windows.h>
#include "sa_gameplay.hpp"
#include "lcs/sa_executor.hpp"
#include <fstream>
#include <sstream>
#include <cstring>
#include <algorithm>
#include <map>
#include <cstddef>
#include <cmath>
#include <utility>
#include <cstdio>
namespace lcs {
namespace {
struct NativeAnimBlockLayout {
    uint8_t name[16];
    uint8_t loaded;
    uint8_t pad11;
    uint16_t references;
    int32_t start_animation;
    int32_t animation_count;
    uint32_t animation_style;
};
static_assert(offsetof(NativeAnimBlockLayout,start_animation)==0x14);
static_assert(offsetof(NativeAnimBlockLayout,animation_count)==0x18);
static_assert(offsetof(NativeAnimBlockLayout,animation_style)==0x1c);
struct NativeBlockProbe {
    bool readable=false;
    bool exists=false;
    bool resident=false;
    uintptr_t block=0;
    int32_t index=-1;
    int32_t range_start=-1;
    int32_t range_count=0;
};
struct AnimationReceipt {
    std::string path;
    int animations=0;
    uintptr_t block=0;
    int32_t index=-1;
    int32_t range_start=-1;
    int32_t range_count=0;
};
std::string last_animation_load_diagnostic;
enum BackendLoadFailure {backend_ok,backend_profile,backend_capacity,backend_ownership,backend_stream_open,backend_stream_read,backend_identity,backend_exception};
void set_load_diagnostic(const char* alias,const char* reason,const NativeBlockProbe* probe) {
    last_animation_load_diagnostic=std::string("animation ")+(alias?alias:"<null>")+": "+reason;
    if(probe && probe->readable)last_animation_load_diagnostic+=std::string("; existing=")+(probe->exists?"yes":"no")+
        ", loaded="+(probe->resident?"yes":"no")+", count="+std::to_string(probe->range_count)+
        ", range="+std::to_string(probe->range_start)+"+"+std::to_string(probe->range_count);
}
std::map<std::string,AnimationReceipt>& animation_receipts() {
    // CAnimManager retains registrations across Campaign instances and native
    // LOAD. This process-lifetime receipt is the only authority to adopt one.
    static std::map<std::string,AnimationReceipt> receipts;
    return receipts;
}
bool acquire_ref(int32_t index);
void release_ref(int32_t index);
bool inspect_native(const char* alias,NativeBlockProbe* out) {
    if(!out)return false;
    *out={};
#ifdef LCS_GAMEPLAY_ANIMATION_PROBE
    return LCS_GAMEPLAY_ANIMATION_PROBE(alias,&out->readable,&out->exists,&out->resident,
        &out->block,&out->index,&out->range_start,&out->range_count);
#else
    __try {
        const auto block=reinterpret_cast<uintptr_t(__cdecl*)(const char*)>(0x4D3940)(alias);
        out->readable=true;
        if(!block)return true;
        const auto index=reinterpret_cast<int32_t(__cdecl*)(const char*)>(0x4D3990)(alias);
        const auto* native=reinterpret_cast<const NativeAnimBlockLayout*>(block);
        const auto resident=native->loaded;
        const auto count=native->animation_count;
        const auto start=native->start_animation;
        if(index<0 || index>=180 || resident>1 || count<0 || count>2500 || start<0 || start>2500 || start+count>2500)return false;
        out->exists=true;out->resident=resident!=0;out->block=block;out->index=index;
        out->range_start=start;out->range_count=count;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
#endif
}
bool same_receipt(const AnimationReceipt& receipt,const char* path,int animations,const NativeBlockProbe& probe) {
    return receipt.path==path && receipt.animations==animations && probe.exists &&
        receipt.block==probe.block && receipt.index==probe.index &&
        receipt.range_start==probe.range_start && receipt.range_count==probe.range_count &&
        probe.range_count==animations;
}
uintptr_t load_native_backend(const char* path,const char* alias,int animations,int32_t* index,bool registered,int32_t* failure) {
    if(failure)*failure=backend_ok;
#ifdef LCS_GAMEPLAY_ANIMATION_LOAD
    const auto result=LCS_GAMEPLAY_ANIMATION_LOAD(path,alias,animations,index,registered);
    if(!result && failure)*failure=backend_stream_read;return result;
#else
    uintptr_t stream=0;
    __try {
        const auto count=*reinterpret_cast<const int32_t*>(0xB4EA30),total=*reinterpret_cast<const int32_t*>(0xB4EA2C);
        if(count<0 || count>180 || total<0 || animations<=0){if(failure)*failure=backend_profile;return 0;}
        // Aliases belong exclusively to this package. Never replace a block
        // supplied by another plugin or load a block twice into the registry.
        const auto existing=reinterpret_cast<uintptr_t(__cdecl*)(const char*)>(0x4D3940)(alias);
        if(existing && (!registered || *reinterpret_cast<const uint8_t*>(existing+0x10))){if(failure)*failure=backend_ownership;return 0;}
        if(!existing && count>=180){if(failure)*failure=backend_capacity;return 0;}
        // A previously owned unloaded block keeps its hierarchy range. New
        // blocks must fit before LoadAnimFile can write the fixed SA stores.
        if((!existing || !*reinterpret_cast<const int32_t*>(existing+0x18)) && total+animations>2500){if(failure)*failure=backend_capacity;return 0;}
        stream=reinterpret_cast<uintptr_t(__cdecl*)(int,int,const char*)>(0x7ECEF0)(2,1,path);
        if(!stream){if(failure)*failure=backend_stream_open;return 0;}
        const char uncompressed[1][32]={};
        reinterpret_cast<void(__cdecl*)(uintptr_t,bool,const void*)>(0x4D47F0)(stream,true,uncompressed);
        const auto closed=reinterpret_cast<int(__cdecl*)(uintptr_t,void*)>(0x7ECE20)(stream,nullptr);stream=0;
        if(!closed){if(failure)*failure=backend_stream_read;return 0;}
        const auto block=reinterpret_cast<uintptr_t(__cdecl*)(const char*)>(0x4D3940)(alias);
        *index=reinterpret_cast<int32_t(__cdecl*)(const char*)>(0x4D3990)(alias);
        if(!block || *index<0 || *index>=180 || !*reinterpret_cast<const uint8_t*>(block+0x10)){if(failure)*failure=backend_identity;return 0;}
        reinterpret_cast<void(__cdecl*)(int32_t)>(0x4D3FB0)(*index);
        return block;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if(stream)reinterpret_cast<int(__cdecl*)(uintptr_t,void*)>(0x7ECE20)(stream,nullptr);
        if(failure)*failure=backend_exception;
        return 0;
    }
#endif
}
uintptr_t load_native(const char* path,const char* alias,int animations,int32_t* index) {
    if(!path || !alias || !index || animations<=0){set_load_diagnostic(alias,"invalid load arguments",nullptr);return 0;}
    auto& receipts=animation_receipts();
    const auto known=receipts.find(alias);
    NativeBlockProbe before;
    if(!inspect_native(alias,&before) || !before.readable){set_load_diagnostic(alias,"native registry read failed",nullptr);return 0;}
    bool owned=false;
    if(before.exists) {
        if(known==receipts.end()){set_load_diagnostic(alias,"ownership refused for existing foreign registration",&before);return 0;}
        if(!same_receipt(known->second,path,animations,before)){set_load_diagnostic(alias,"ownership receipt identity/range mismatch",&before);return 0;}
        owned=true;
        if(before.resident) {
            if(!acquire_ref(before.index)){set_load_diagnostic(alias,"resident range reference acquire failed",&before);return 0;}
            last_animation_load_diagnostic.clear();
            *index=before.index;return before.block;
        }
    }
    int32_t loaded_index=-1;
    int32_t failure=backend_ok;
    const auto loaded=load_native_backend(path,alias,animations,&loaded_index,owned,&failure);
    if(!loaded) {
        const char* reason="native load failed";
        switch(failure) {
            case backend_profile:reason="native profile/capacity read failed";break;
            case backend_capacity:reason="animation registration/range capacity exceeded";break;
            case backend_ownership:reason="native loader refused unowned registration";break;
            case backend_stream_open:reason="animation stream open failed";break;
            case backend_stream_read:reason="animation stream read/decode failed";break;
            case backend_identity:reason="native registration identity invalid after load";break;
            case backend_exception:reason="native memory read/write fault";break;
            default:break;
        }
        set_load_diagnostic(alias,reason,&before);return 0;
    }
    NativeBlockProbe after;
    if(!inspect_native(alias,&after) || !after.readable || !after.exists || !after.resident || after.block!=loaded ||
       after.index!=loaded_index || after.range_count!=animations || after.range_start<0) {
        if(loaded_index>=0)release_ref(loaded_index);
        set_load_diagnostic(alias,"post-load registration/count/range validation failed",&after);return 0;
    }
    if(owned && (after.block!=before.block || after.index!=before.index ||
       after.range_start!=before.range_start || after.range_count!=before.range_count)) {
        release_ref(loaded_index);set_load_diagnostic(alias,"reload changed owned block/index/range",&after);return 0;
    }
    if(!owned)receipts.insert_or_assign(alias,AnimationReceipt{path,animations,after.block,after.index,after.range_start,after.range_count});
    last_animation_load_diagnostic.clear();*index=after.index;return after.block;
}
std::vector<std::string> gameplay_trace;
// Sunken-ped watchdog. A ped standing idle whose centre is far below the surface under it (the engine keeps
// centre = ground + ~1.0) was spawned inside the floor beyond what SA collision recovers. Logged; lifted once.
struct PedSample {int32_t handle;uint16_t model;uint32_t state;float x,y,z;};
int sample_ped(int32_t i,PedSample* out) {
    __try {
        const auto pool=*reinterpret_cast<const uintptr_t*>(0xB74490);if(!pool)return -1;
        const auto objects=*reinterpret_cast<const uintptr_t*>(pool);const auto flags=*reinterpret_cast<const uintptr_t*>(pool+4);
        const int32_t size=*reinterpret_cast<const int32_t*>(pool+8);if(!objects || !flags || size<=0 || size>1000 || i>=size)return -1;
        const auto flag=*reinterpret_cast<const uint8_t*>(flags+i);if(flag&0x80)return 0;
        const auto ped=objects+uintptr_t(i)*0x7C4;
        if(ped==*reinterpret_cast<const uintptr_t*>(0xB7CD98))return 0; // never touch the player
        const auto matrix=*reinterpret_cast<const uintptr_t*>(ped+0x14);if(!matrix)return 0;
        out->handle=int32_t((i<<8)|(flag&0x7F));out->model=*reinterpret_cast<const uint16_t*>(ped+0x22);out->state=*reinterpret_cast<const uint32_t*>(ped+0x530);
        out->x=*reinterpret_cast<const float*>(matrix+0x30);out->y=*reinterpret_cast<const float*>(matrix+0x34);out->z=*reinterpret_cast<const float*>(matrix+0x38);
        return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return -1;}
}
int32_t float_word(float v){int32_t r;std::memcpy(&r,&v,4);return r;}
float word_float(int32_t v){float r;std::memcpy(&r,&v,4);return r;}
struct SunkTrack {float x=0,y=0,z=0;int stable=0;bool handled=false;uint32_t seen=0;};
std::map<int32_t,SunkTrack> sunk_tracks;
uint32_t sunk_serial=0;int sunk_logged=0;
void sunk_ped_watchdog() {
    ++sunk_serial;PedSample ped;
    for(int32_t i=0;i<1000;++i) {
        const int got=sample_ped(i,&ped);if(got<0)break;if(!got)continue;
        auto& track=sunk_tracks[ped.handle];track.seen=sunk_serial;
        const bool still=std::fabs(ped.x-track.x)<0.1f && std::fabs(ped.y-track.y)<0.1f && std::fabs(ped.z-track.z)<0.05f;
        track.x=ped.x;track.y=ped.y;track.z=ped.z;
        if(ped.state!=1 || !still){track.stable=0;continue;}
        if(track.handled || ++track.stable<3)continue;
        // Surface at most 0.6 m above the centre: a healthy ped has it ~1.0 m below.
        NativeCall query;query.opcode=0x02CE;query.inputs={float_word(ped.x),float_word(ped.y),float_word(ped.z+0.6f)};query.outputs=1;
        const auto ground=execute_sa(query);if(!ground.executed)continue;
        const float surface=word_float(ground.values[0]);
        if(!std::isfinite(surface) || surface<=-99.0f || surface<=ped.z-0.55f || surface>ped.z+0.6f)continue;
        const float expected=surface+1.0f;
        std::string action="logged only";
        if(expected-ped.z<=1.5f) {
            NativeCall move;move.opcode=0x00A1;move.inputs={ped.handle,float_word(ped.x),float_word(ped.y),float_word(surface)};
            action=execute_sa(move).executed ? "lifted to the surface" : "lift failed";
        }
        track.handled=true;
        if(sunk_logged++<200 && gameplay_trace.size()<400) {
            char text[320];snprintf(text,sizeof text,"Sunken ped: handle=%d model=%u pos=(%.2f,%.2f,%.2f) surface=%.2f below_expected=%.2f action=%s",
                ped.handle,unsigned(ped.model),ped.x,ped.y,ped.z,surface,expected-ped.z,action.c_str());
            gameplay_trace.push_back(text);
        }
    }
    for(auto it=sunk_tracks.begin();it!=sunk_tracks.end();)it=it->second.seen!=sunk_serial?sunk_tracks.erase(it):std::next(it);
}
bool fade_owned(int32_t handle,uintptr_t hierarchy) {
#ifdef LCS_GAMEPLAY_ANIMATION_FADE
    return LCS_GAMEPLAY_ANIMATION_FADE(handle,hierarchy);
#else
    __try {
        const auto ped=reinterpret_cast<uintptr_t(__cdecl*)(int32_t)>(0x54FF90)(handle);
        if(!ped)return true; // entity already deleted: its associations died with it
        const auto clump=*reinterpret_cast<const uintptr_t*>(ped+0x18);if(!clump)return false;
        const auto offset=*reinterpret_cast<const uint32_t*>(0xB5F878);if(offset>4096)return false;
        const auto data=*reinterpret_cast<const uintptr_t*>(clump+offset);if(!data)return false;
        auto link=*reinterpret_cast<const uintptr_t*>(data);unsigned visited=0;
        while(link) {
            if(++visited>1024)return false;
            const auto association=link-4;
            if(*reinterpret_cast<const uintptr_t*>(association+0x14)==hierarchy) {
                *reinterpret_cast<float*>(association+0x1C)=-8.0f;
                *reinterpret_cast<uint16_t*>(association+0x2E)|=8;
            }
            link=*reinterpret_cast<const uintptr_t*>(link);
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
#endif
}
uintptr_t play_native(int32_t handle,uintptr_t block,const char* animation,uint16_t flags,bool blend=false) {
#ifdef LCS_GAMEPLAY_ANIMATION_PLAY
    return LCS_GAMEPLAY_ANIMATION_PLAY(handle,block,animation,flags,blend);
#else
    __try {
        const auto ped=reinterpret_cast<uintptr_t(__cdecl*)(int32_t)>(0x54FF90)(handle);if(!ped)return 0;
        const auto clump=*reinterpret_cast<const uintptr_t*>(ped+0x18);if(!clump)return 0;
        const auto hierarchy=reinterpret_cast<uintptr_t(__cdecl*)(const char*,uintptr_t)>(0x4D42F0)(animation,block);if(!hierarchy)return 0;
        const auto association=blend ? reinterpret_cast<uintptr_t(__cdecl*)(uintptr_t,uintptr_t,int,float)>(0x4D4410)(clump,hierarchy,flags,3.0f) :
            reinterpret_cast<uintptr_t(__cdecl*)(uintptr_t,uintptr_t,int)>(0x4D4330)(clump,hierarchy,flags);
        if(!association)return 0;
        *reinterpret_cast<float*>(association+0x24)=1.0f;return hierarchy;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return 0;}
#endif
}
enum class RiotStatus {error,removed,interrupted,paused,ready,playing};
RiotStatus riot_status(int32_t handle,uintptr_t hierarchy,bool stop_loop) {
#ifdef LCS_GAMEPLAY_RIOT_STATUS
    return static_cast<RiotStatus>(LCS_GAMEPLAY_RIOT_STATUS(handle,hierarchy,stop_loop));
#else
    __try {
        const auto ped=reinterpret_cast<uintptr_t(__cdecl*)(int32_t)>(0x54FF90)(handle);
        if(!ped)return RiotStatus::removed;
        // SDK CPed::m_ePedState at 0x530; ePedState from gta-reversed/Plugin-SDK.
        // https://github.com/gta-reversed/gta-reversed/blob/master/source/game_sa/Enums/ePedState.h
        const auto state=*reinterpret_cast<const int32_t*>(ped+0x530);
        if(state==9 || state==16 || state==17)return RiotStatus::interrupted;
        if(!reinterpret_cast<bool(__thiscall*)(uintptr_t)>(0x5E3960)(ped))return RiotStatus::paused;
        if(!hierarchy)return RiotStatus::ready;
        const auto clump=*reinterpret_cast<const uintptr_t*>(ped+0x18);if(!clump)return RiotStatus::error;
        const auto offset=*reinterpret_cast<const uint32_t*>(0xB5F878);if(offset>4096)return RiotStatus::error;
        const auto data=*reinterpret_cast<const uintptr_t*>(clump+offset);if(!data)return RiotStatus::error;
        auto link=*reinterpret_cast<const uintptr_t*>(data);unsigned visited=0;
        while(link) {
            if(++visited>1024)return RiotStatus::error;
            const auto association=link-4;
            if(*reinterpret_cast<const uintptr_t*>(association+0x14)==hierarchy) {
                if(stop_loop)*reinterpret_cast<uint16_t*>(association+0x2E)&=~uint16_t(2);
                return *reinterpret_cast<const float*>(association+0x1C)<0 ? RiotStatus::ready : RiotStatus::playing;
            }
            link=*reinterpret_cast<const uintptr_t*>(link);
        }
        return RiotStatus::ready;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return RiotStatus::error;}
#endif
}
void release_ref(int32_t index) {
#ifdef LCS_GAMEPLAY_ANIMATION_RELEASE
    LCS_GAMEPLAY_ANIMATION_RELEASE(index);
#else
    __try {reinterpret_cast<void(__cdecl*)(int32_t)>(0x4D3FF0)(index);}
    __except(EXCEPTION_EXECUTE_HANDLER) {}
#endif
}
std::optional<bool> resident_block(uintptr_t block) {
#ifdef LCS_GAMEPLAY_ANIMATION_RESIDENT
    return LCS_GAMEPLAY_ANIMATION_RESIDENT(block);
#else
    __try {return bool(*reinterpret_cast<const uint8_t*>(block+0x10));}
    __except(EXCEPTION_EXECUTE_HANDLER) {return {};}
#endif
}
bool acquire_ref(int32_t index) {
#ifdef LCS_GAMEPLAY_ANIMATION_ACQUIRE
    return LCS_GAMEPLAY_ANIMATION_ACQUIRE(index);
#else
    __try {reinterpret_cast<void(__cdecl*)(int32_t)>(0x4D3FB0)(index);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
#endif
}
}
std::string gameplay_animation_load_diagnostic() {return last_animation_load_diagnostic;}
NativeGameplayAnimations::NativeGameplayAnimations(const std::filesystem::path& directory) {
    const bool bundled=std::filesystem::exists(directory/"LG00.ifp.bin");
    const auto suffix=bundled?".ifp.bin":".ifp";
    std::set<std::string> logical_blocks;
    std::ifstream input(directory/"index.tsv");std::string line;
    if(!input)throw std::runtime_error("Original mission gesture index missing");
    while(std::getline(input,line)) {
        std::istringstream row(line);int state=0,flags=0;Gesture gesture;
        if(!(row>>state>>gesture.block>>gesture.animation>>flags) || state<1 || state>255 || gesture.block.size()!=4 || gesture.block.substr(0,2)!="LG" ||
           !std::all_of(gesture.block.begin()+2,gesture.block.end(),[](char c){return c>='0' && c<='9';}) || gesture.animation.empty() || gesture.animation.size()>23 || (flags&~31))
            throw std::runtime_error("Invalid original mission gesture row");
        if(bundled) {
            if(!(row>>gesture.logical_block) || gesture.block!="LG00" || gesture.logical_block.size()!=4 || gesture.logical_block.substr(0,2)!="LG" ||
               !std::all_of(gesture.logical_block.begin()+2,gesture.logical_block.end(),[](char c){return c>='0' && c<='9';}))throw std::runtime_error("Invalid original logical gesture group");
        }else gesture.logical_block=gesture.block;
        std::string extra;if(row>>extra)throw std::runtime_error("Trailing mission gesture fields");
        logical_blocks.insert(gesture.logical_block);
        gesture.flags=uint16_t(flags);if(!gestures_.emplace(state,gesture).second)throw std::runtime_error("Duplicate original mission gesture state");
        auto block=blocks_.find(gesture.block);
        if(block==blocks_.end()) {
            const auto path=directory/(gesture.block+suffix);auto archive=AnimationArchive::load_anpk(path);
            if(archive.name!=gesture.block)throw std::runtime_error("Mission gesture block name differs");
            block=blocks_.emplace(gesture.block,Block{path,std::move(archive)}).first;
        }
        if(std::none_of(block->second.archive.animations.begin(),block->second.archive.animations.end(),[&](const Animation& a){return a.name==gesture.animation;}))throw std::runtime_error("Declared original gesture absent from its block");
    }
    if(gestures_.size()!=71 || logical_blocks.size()!=25 || blocks_.size()!=(bundled?1u:25u))throw std::runtime_error("Incomplete original mission gesture package");
    std::ifstream named_index(directory/"named"/"index.tsv");
    if(!named_index)throw std::runtime_error("Original named animation index missing");
    while(std::getline(named_index,line)) {
        std::istringstream row(line);std::string name,alias,extra;
        if(!(row>>name>>alias) || row>>extra || name.empty() || name.size()>8 || alias.size()!=4 || alias.substr(0,2)!="LN" ||
           !std::all_of(alias.begin()+2,alias.end(),[](char c){return c>='0' && c<='9';}) || !named_blocks_.emplace(name,alias).second)
            throw std::runtime_error("Invalid original named animation index");
        auto path=directory/"named"/(alias+suffix);auto archive=AnimationArchive::load_anpk(path);
        if(archive.name!=alias || !blocks_.emplace(alias,Block{path,std::move(archive)}).second)throw std::runtime_error("Conflicting named animation block");
    }
    const auto riot=named_blocks_.find("riot");
    if(riot==named_blocks_.end())throw std::runtime_error("Original RIOT block missing");
    const auto& animations=blocks_.at(riot->second).archive.animations;
    for(size_t i=0;i<RiotWaitCycle::names.size();++i) {
        const auto name=RiotWaitCycle::names[i];
        const auto found=std::find_if(animations.begin(),animations.end(),[&](const Animation& a){return _stricmp(a.name.c_str(),std::string(name).c_str())==0;});
        if(found==animations.end())throw std::runtime_error("Original RIOT animation absent: "+std::string(name));
        riot_names_[i]=found->name; // Preserve actual original archive spelling.
    }
}
std::vector<std::string> take_gameplay_animation_trace(){return std::exchange(gameplay_trace,{});}
NativeGameplayAnimations::~NativeGameplayAnimations() {
    if(!sa_executor_profile_matches())return;
    for(const auto& pose:poses_)fade_owned(pose.first,pose.second.hierarchy);
    for(const auto& pose:riots_)if(pose.second.hierarchy)fade_owned(pose.first,pose.second.hierarchy);
    for(const auto& block:blocks_)if(block.second.reference_held)release_ref(block.second.index);
}
bool NativeGameplayAnimations::request(int32_t state,bool load) {
    if(!sa_executor_profile_matches()){set_load_diagnostic("<gesture>","unsupported executable profile",nullptr);return false;}
    const auto gesture=gestures_.find(state);if(gesture==gestures_.end()){set_load_diagnostic("<gesture>","unknown gesture state",nullptr);return false;}
    if(!load){requested_.erase(gesture->second.logical_block);return true;}
    auto& block=blocks_.at(gesture->second.block);
    if(block.native) {
        const auto resident=resident_block(block.native);if(!resident){set_load_diagnostic(gesture->second.block.c_str(),"native residency query failed",nullptr);return false;}
        if(!*resident){block.native=0;block.reference_held=false;}
    }
    if(!block.native) {
        block.native=load_native(block.path.string().c_str(),gesture->second.block.c_str(),int(block.archive.animations.size()),&block.index);
        block.reference_held=bool(block.native);
    } else if(!block.reference_held) {
        if(!acquire_ref(block.index)){set_load_diagnostic(gesture->second.block.c_str(),"resident range reference acquire failed",nullptr);return false;}block.reference_held=true;
    }
    if(!block.native)return false;
    block.registered=true;requested_.insert(gesture->second.logical_block);last_animation_load_diagnostic.clear();return true;
}
std::optional<bool> NativeGameplayAnimations::named(const std::string& name,uint16_t action) {
    if(!sa_executor_profile_matches() || action>2)return {};
    const auto found=named_blocks_.find(name);if(found==named_blocks_.end())return {};
    auto& block=blocks_.at(found->second);
    if(action==0) {
        if(block.native) {
            const auto resident=resident_block(block.native);if(!resident)return {};
            if(!*resident){block.native=0;block.reference_held=false;}
        }
        if(!block.native){block.native=load_native(block.path.string().c_str(),found->second.c_str(),int(block.archive.animations.size()),&block.index);block.reference_held=bool(block.native);}
        else if(!block.reference_held){if(!acquire_ref(block.index))return {};block.reference_held=true;}
        if(!block.native)return {};
        block.registered=true;return true;
    }
    if(!block.native)return action==2;
    // Private files are loaded through CAnimManager, not CStreaming. Query
    // the actual native block instead of an unrelated streaming load flag.
    const auto resident=resident_block(block.native);if(!resident)return {};
    if(action==1)return *resident;
    if(name=="riot") {
        while(!riots_.empty())if(!clear(riots_.begin()->first))return {};
    }
    // This namespace is owned by this package and separate from gesture poses.
    if(block.reference_held){release_ref(block.index);block.reference_held=false;}
    const auto removed=execute_sa({0x04EF,{},found->second});
    if(!removed.executed)return {};
    // REMOVE_ANIMATION permits delayed eviction while associations still
    // reference a block. Retain its registry identity and reacquire on a later
    // request; never register a duplicate or release our reference twice.
    return true;
}
std::optional<bool> NativeGameplayAnimations::waiting(int32_t actor) const {
    const auto riot=riots_.find(actor);
    const auto pose=poses_.find(actor);
    if(riot==riots_.end() && pose==poses_.end())return false;
    if(!sa_executor_profile_matches())return {};
    const auto hierarchy=riot!=riots_.end()?riot->second.hierarchy:pose->second.hierarchy;
    const auto status=riot_status(actor,hierarchy,false);
    if(status==RiotStatus::error)return {};
    if(status==RiotStatus::removed || status==RiotStatus::interrupted)return false;
    // RIOT remains a script wait between its owned animation cycles. A plain
    // pose stops blocking once SA has removed/faded its association.
    return riot!=riots_.end() || status==RiotStatus::playing || status==RiotStatus::paused;
}
bool NativeGameplayAnimations::clear(int32_t actor) {
    const auto riot=riots_.find(actor);
    if(riot!=riots_.end()) {
        if(!sa_executor_profile_matches() || (riot->second.hierarchy && !fade_owned(actor,riot->second.hierarchy)))return false;
        riots_.erase(riot);
    }
    const auto pose=poses_.find(actor);if(pose==poses_.end())return true;
    if(!sa_executor_profile_matches() || !fade_owned(actor,pose->second.hierarchy))return false;
    poses_.erase(pose);return true;
}
bool NativeGameplayAnimations::play(int32_t actor,int32_t state,int32_t duration,bool repeat) {
    if(!sa_executor_profile_matches())return false;
    if(state!=0 && riots_.count(actor))return true; // LCS permits only explicit clear to replace RIOT.
    if(state==29) {
        if(!sa_executor_profile_matches())return false;
        const auto named_riot=named("riot",1);
        if(!named_riot || !*named_riot)return false; // Original REQUEST/HAS_ANIMATION_LOADED must complete.
        const auto control=sa_char_in_control(actor);if(!control)return false;if(!*control)return true;
        if(!clear(actor))return false;
        riots_.emplace(actor,RiotPose{}); // Animation is selected by Wait(), on the next actor tick.
        return true;
    }
    const auto gesture=gestures_.find(state);
    if(!sa_executor_profile_matches() || gesture==gestures_.end())return false;
    // Standard PED animations are resident in LCS without a LOAD_NON_STANDARD
    // command. A nonstandard request makes every gesture in that block usable.
    if(state<36 && !request(state,true))return false;
    if(!requested_.count(gesture->second.logical_block))return false;
    const auto control=sa_char_in_control(actor);if(!control)return false;if(!*control)return true;
    if(!clear(actor))return false;
    const uint16_t play_flags=uint16_t(gesture->second.flags | (repeat?2:0));
    const auto hierarchy=play_native(actor,blocks_.at(gesture->second.block).native,gesture->second.animation.c_str(),play_flags);
    if(!hierarchy)return false;
    // Only standard wait states use the script timeout. Nonstandard mission
    // gestures keep the original association's fade/freeze/loop lifetime.
    poses_[actor]={hierarchy,state<36 && duration>=0,now_+uint32_t(std::max(duration,0)),state,now_+1000};
    return true;
}
bool NativeGameplayAnimations::tick(uint32_t now) {
    now_=now;
    if(int32_t(now-next_sunk_check_)>=0){next_sunk_check_=now+1000;sunk_ped_watchdog();}
    for(auto riot=riots_.begin();riot!=riots_.end();) {
        const auto actor=riot->first;auto& pose=riot->second;
        const auto status=riot_status(actor,pose.hierarchy,pose.cycle.expired(now));
        if(status==RiotStatus::error)return false;
        if(status==RiotStatus::removed){riot=riots_.erase(riot);continue;}
        if(status==RiotStatus::interrupted){++riot;if(!clear(actor))return false;continue;}
        if(status==RiotStatus::ready) {
            const auto selected=pose.cycle.choose(now,random_(),random_());
            const auto& block=blocks_.at(named_blocks_.at("riot"));
            if(!block.native)return false;
            pose.hierarchy=play_native(actor,block.native,riot_names_[selected].c_str(),pose.cycle.flags(),true);
            if(!pose.hierarchy)return false;
        }
        ++riot;
    }
    for(auto pose=poses_.begin();pose!=poses_.end();) {
        if(!pose->second.timed || int32_t(now-pose->second.deadline)<0){++pose;continue;}
        const auto actor=pose->first;++pose;if(!clear(actor))return false;
    }
    return true;
}
}
