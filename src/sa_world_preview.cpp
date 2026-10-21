#include <windows.h>
#include "sa_world_preview.hpp"
#include "sa_map_culling.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <sstream>
#include <utility>

namespace lcs {
SaWorldPreview::SaWorldPreview(const std::filesystem::path& directory) {
    std::ifstream base_ranges(directory/L"render-ranges.tsv"),zones(directory/L"island-zones.tsv");
    if(!base_ranges || !zones)throw std::runtime_error("Missing map render-range/island policy");
    std::stringstream ranges;ranges<<base_ranges.rdbuf();
    std::ifstream campaign_ranges(directory/L"campaign-render-ranges.tsv");
    if(campaign_ranges)ranges<<'\n'<<campaign_ranges.rdbuf();
    int id,lod,related,physical;float minimum;unsigned levels;size_t count=0;
    while(ranges>>id>>lod>>minimum>>related>>levels>>physical) {
        if(id<10000 || id>=20000 || rules_[id].owned || (lod!=0 && lod!=1) ||
           !std::isfinite(minimum) || minimum<0 || minimum>10000 || levels==0 || levels>31 ||
           physical<0 || physical>1 || (physical && lod) ||
           (related!=-1 && (related<10000 || related>=20000)))throw std::runtime_error("Invalid map render rule");
        rules_[id]={true,lod!=0,minimum,related,levels,physical!=0};++count;
    }
    if(!ranges.eof() || count==0)throw std::runtime_error("Invalid map render-range table");
    IslandZone zone{};
    while(zones>>zone.level>>zone.bounds[0]>>zone.bounds[1]>>zone.bounds[2]>>zone.bounds[3]>>zone.bounds[4]>>zone.bounds[5]) {
        if(zone.level<0 || zone.level>4 || zones_.size()>=1024)throw std::runtime_error("Invalid original island zone");
        for(int i=0;i<3;++i)if(!std::isfinite(zone.bounds[i]) || !std::isfinite(zone.bounds[i+3]) || zone.bounds[i]>zone.bounds[i+3])throw std::runtime_error("Invalid island bounds");
        zones_.push_back(zone);
    }
    if(!zones.eof() || zones_.empty())throw std::runtime_error("Invalid original island-zone table");
}
namespace {
using AtomicRender=void*(__cdecl*)(void*);
using StateFunction=int(__cdecl*)(int,void*);
// Keep only callback functions, never streamed atomic/entity pointers. Shared
// callback slots remain valid after new games and atomic address reuse.
std::array<AtomicRender,64> previous_render{};
std::uint64_t no_cull_draws=0;
bool culling_failed=false;
bool render_state_functions(StateFunction& get,StateFunction& set) {
    __try {
#ifdef LCS_MAP_CULLING_FIXTURE
        const auto engine=LCS_MAP_CULLING_FIXTURE;
#else
        const auto engine=*reinterpret_cast<const uintptr_t*>(0xC97B24);
#endif
        if(!engine)return false;
        // Plugin-SDK RwGlobals: dOpenDevice at 0x10; its state Set/Get at
        // 0x10/0x14. Use the current device callbacks, including other mods.
        set=*reinterpret_cast<const StateFunction*>(engine+0x20);
        get=*reinterpret_cast<const StateFunction*>(engine+0x24);
        return get && set;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
template<size_t Slot>void* __cdecl render_double_sided(void* atomic) {
    const auto draw=previous_render[Slot];
    if(!draw)return atomic;
    StateFunction get=nullptr,set=nullptr;
    if(!render_state_functions(get,set)){culling_failed=true;return draw(atomic);}
    return draw_map_double_sided(
        [get](std::uint32_t& value){const bool ok=get(20,&value)!=0;if(!ok)culling_failed=true;return ok;},
        [set](std::uint32_t value){const bool ok=set(20,reinterpret_cast<void*>(uintptr_t(value)))!=0;if(!ok)culling_failed=true;return ok;},
        [draw,atomic](){++no_cull_draws;return draw(atomic);});
}
template<size_t... Slot>constexpr auto render_wrappers(std::index_sequence<Slot...>) {
    return std::array<AtomicRender,sizeof...(Slot)>{{&render_double_sided<Slot>...}};
}
constexpr auto wrappers=render_wrappers(std::make_index_sequence<64>{});
bool executable_callback(AtomicRender callback) {
    MEMORY_BASIC_INFORMATION info{};
    return callback && VirtualQuery(reinterpret_cast<void*>(callback),&info,sizeof(info)) &&
           info.State==MEM_COMMIT && !(info.Protect&PAGE_GUARD) &&
           (info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));
}
bool bind_atomic_culling(uintptr_t object) {
    // Native map IDEs create RpAtomics, not character/vehicle RpClumps.
    // RwObject.type=1, RpAtomic::renderCallBack at 0x48 (RW 3.6 Win32).
    if(!object || *reinterpret_cast<const unsigned char*>(object)!=1)return true;
    auto& callback=*reinterpret_cast<AtomicRender*>(object+0x48);
    for(auto wrapper:wrappers)if(callback==wrapper)return true;
    if(!executable_callback(callback))return false;
    for(size_t i=0;i<previous_render.size();++i)if(previous_render[i]==callback || !previous_render[i]) {
        previous_render[i]=callback;callback=wrappers[i];return true;
    }
    return false;
}
struct SkinDrawBinding {bool valid=true;unsigned atomics=0;};
void* __cdecl bind_skin_atomic(void* atomic,void* data) {
    auto& result=*static_cast<SkinDrawBinding*>(data);
    if(!atomic || ++result.atomics>64 || *static_cast<const unsigned char*>(atomic)!=1 ||
       !bind_atomic_culling(reinterpret_cast<uintptr_t>(atomic))) {
        result.valid=false;return nullptr;
    }
    return atomic;
}
bool object_pool_capacity(size_t* capacity) {
    __try {
        const auto pool=*reinterpret_cast<const uintptr_t*>(0xB7449C);
        if(!pool){*capacity=0;return true;}
        const auto entries=*reinterpret_cast<const uintptr_t*>(pool);
        const auto flags=*reinterpret_cast<const uintptr_t*>(pool+4);
        const auto count=*reinterpret_cast<const int*>(pool+8);
        if(!entries || !flags || count<0 || count>200000)return false;
        *capacity=size_t(count);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
struct PromotedDummyLink {uintptr_t address;};
bool copy_promoted_dummy_links(PromotedDummyLink* output,size_t capacity,size_t* used) {
    __try {
        *used=0;
        const auto pool=*reinterpret_cast<const uintptr_t*>(0xB7449C);
        if(!pool)return true;
        const auto entries=*reinterpret_cast<const uintptr_t*>(pool);
        const auto flags=*reinterpret_cast<const uintptr_t*>(pool+4);
        const auto count=*reinterpret_cast<const int*>(pool+8);
        if(!entries || !flags || count<0 || size_t(count)>capacity || count>200000)return false;
        for(int i=0;i<count;++i) {
            if(*reinterpret_cast<const unsigned char*>(flags+i)&128)continue;
            const auto object=entries+0x19C*size_t(i);
            if((*reinterpret_cast<const unsigned char*>(object+0x36)&7)!=4)continue;
            const auto dummy=*reinterpret_cast<const uintptr_t*>(object+0x170);
            if(dummy)output[(*used)++]={dummy};
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool promoted_dummy_links(std::vector<PromotedDummyLink>& output) {
    size_t capacity=0;
    if(!object_pool_capacity(&capacity))return false;
    output.resize(capacity);
    size_t used=0;
    if(!copy_promoted_dummy_links(output.data(),output.size(),&used))return false;
    output.resize(used);
    std::sort(output.begin(),output.end(),[](const auto& left,const auto& right){return left.address<right.address;});
    return true;
}
bool promoted_dummy_matches(const std::vector<PromotedDummyLink>& links,uintptr_t entity) {
    const auto owner=std::lower_bound(links.begin(),links.end(),entity,
        [](const PromotedDummyLink& link,uintptr_t address){return link.address<address;});
    return owner!=links.end() && owner->address==entity;
}
// Plugin-SDK CEntity: 0x38-byte buildings/dummies, flags at 0x1C,
// model index at 0x22, CPlaceable matrix at 0x14. Read pools every frame;
// never keep an entity pointer across a new game or a streaming operation.
bool apply_pool(uintptr_t address,const WorldRenderRule* rules,const float* camera,int level,float multiplier,
                const std::vector<PromotedDummyLink>& promoted_dummies) {
    __try {
        const auto pool=*reinterpret_cast<const uintptr_t*>(address);if(!pool)return true;
        const auto items=*reinterpret_cast<const uintptr_t*>(pool);
        const auto flags=*reinterpret_cast<const uintptr_t*>(pool+4);
        const auto size=*reinterpret_cast<const int*>(pool+8);
        if(!items || !flags || size<0 || size>200000)return false;
        for(int i=0;i<size;++i) {
            if(*reinterpret_cast<const unsigned char*>(flags+i)&128)continue;
            const auto entity=items+0x38*i;
            const auto id=*reinterpret_cast<const unsigned short*>(entity+0x22);
            if(id>=20000 || !rules[id].owned)continue;
            // Disable backface culling only for this converted map. Preserve
            // other entity flags and every stock vehicle/ped/mod pipeline.
            auto& bits=*reinterpret_cast<unsigned*>(entity+0x1C);bits&=~(1u<<27);
            if(!bind_atomic_culling(*reinterpret_cast<const uintptr_t*>(entity+0x18)))return false;
            const auto& rule=rules[id];
            if(rule.physical && !rule.lod) {
                // SA puts drawDistance*LODmultipler > 300 in LOD sectors,
                // disabling collision. A collision bit alone cannot fix it:
                // move physical source entities into normal world sectors.
                const bool was_big=(bits&(1u<<8))!=0;
                const bool is_dummy_pool=address==0xB744A0;
                const bool promoted=is_dummy_pool && promoted_dummy_matches(promoted_dummies,entity);
                preview_apply_physical_membership(bits,rule,was_big,is_dummy_pool,promoted,
                    [entity](){reinterpret_cast<void(__thiscall*)(void*)>(0x534AE0)(reinterpret_cast<void*>(entity));},
                    [entity](){reinterpret_cast<void(__thiscall*)(void*)>(0x533020)(reinterpret_cast<void*>(entity));});
            }
            if(!rule.lod && rule.levels==31)continue;
            const auto matrix=*reinterpret_cast<const uintptr_t*>(entity+0x14);
            const auto position=reinterpret_cast<const float*>(matrix?matrix+0x30:entity+4);
            float squared=0;for(int j=0;j<3;++j){float d=camera[j]-position[j];squared+=d*d;}
            bool loaded=false;
            if(rule.related>=0) {
                const auto info=*reinterpret_cast<const uintptr_t*>(0xA9B0C8+4*rule.related);
                loaded=info && *reinterpret_cast<const uintptr_t*>(info+0x1C) && *reinterpret_cast<const unsigned char*>(info+0xC)==255;
            }
            if(preview_lod_visible(rule,level,std::sqrt(squared),multiplier,loaded))bits|=128;else bits&=~128u;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool camera_position(float* output,float* multiplier) {
    __try {
        // Plugin-SDK CCamera::m_vecGameCamPos (0x908) / LODDistMultiplier (0xF0).
        for(int j=0;j<3;++j)output[j]=*reinterpret_cast<const float*>(0xB6F028+0x908+4*j);
        *multiplier=*reinterpret_cast<const float*>(0xB6F028+0xF0);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
bool bind_sa_world_overlay_culling(std::uintptr_t entity) {
    __try {
        if(!entity || (*reinterpret_cast<const unsigned char*>(entity+0x36)&7)!=4 ||
           *reinterpret_cast<const unsigned short*>(entity+0x22)<10000 ||
           *reinterpret_cast<const unsigned short*>(entity+0x22)>=20000)return false;
        *reinterpret_cast<unsigned*>(entity+0x1C)&=~(1u<<27);
        return bind_atomic_culling(*reinterpret_cast<const uintptr_t*>(entity+0x18));
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool bind_sa_skin_double_sided(std::uintptr_t clump) {
    __try {
        if(!clump || *reinterpret_cast<const unsigned char*>(clump)!=2)return false;
        SkinDrawBinding binding{};
        using Visit=void*(__cdecl*)(void*,void*(__cdecl*)(void*,void*),void*);
#ifdef LCS_CLUMP_ATOMIC_ITERATOR
        const Visit visit=LCS_CLUMP_ATOMIC_ITERATOR;
#else
        // Plugin-SDK RenderWare.cpp, SA 1.0 US RpClumpForAllAtomics.
        const auto visit=reinterpret_cast<Visit>(0x749b70);
#endif
        return visit(reinterpret_cast<void*>(clump),bind_skin_atomic,&binding)==reinterpret_cast<void*>(clump) &&
               binding.valid && binding.atomics>0 && !culling_failed;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool SaWorldPreview::service() {
    float camera[3],multiplier;
    if(!camera_position(camera,&multiplier))return false;
    for(float v:camera)if(!std::isfinite(v))return false;
    if(!std::isfinite(multiplier) || multiplier<=0 || multiplier>20)return false;
    const int level=preview_level(zones_,camera);
    std::vector<PromotedDummyLink> promoted_dummies;
    if(!promoted_dummy_links(promoted_dummies))return false;
    return apply_pool(0xB74498,rules_.data(),camera,level,multiplier,promoted_dummies) &&
           apply_pool(0xB744A0,rules_.data(),camera,level,multiplier,promoted_dummies);
}
std::uint64_t SaWorldPreview::double_sided_draws() const {return no_cull_draws;}
bool SaWorldPreview::culling_healthy() const {return !culling_failed;}
}
