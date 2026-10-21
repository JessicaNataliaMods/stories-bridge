#include <windows.h>
#include "sa_gameplay_skins.hpp"
#include "sa_world_preview.hpp"
#include "lcs/sa_executor.hpp"
#include <fstream>
#include <string>
#include <stdexcept>
#include <cstdint>
namespace lcs {
namespace {
uint32_t skin_key(const std::string& name) {
    uint32_t value=0xffffffff;
    for(unsigned char c:name) {
        if(c>='a' && c<='z')c-=32;
        value^=c;
        for(int i=0;i<8;++i)value=(value>>1)^((value&1)?0xedb88320u:0u);
    }
    return value;
}
bool correct_model(int32_t id,uint32_t key) {
    if(sa_model_class(id)!=7)return false;
    __try {
        const auto info=*reinterpret_cast<const uintptr_t*>(0xa9b0c8+4*id);
        return info && *reinterpret_cast<const uint32_t*>(info+4)==key;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool service_skin_pool(const bool* models) {
    __try {
#ifdef LCS_GAMEPLAY_SKIN_POOL_FIXTURE
        const auto pool=LCS_GAMEPLAY_SKIN_POOL_FIXTURE;
#else
        const auto pool=*reinterpret_cast<const uintptr_t*>(0xb74490);
#endif
        if(!pool)return false;
        const auto flags=*reinterpret_cast<const uintptr_t*>(pool+4);
        const auto count=*reinterpret_cast<const int32_t*>(pool+8);
        if(!flags || count<0 || count>100000)return false;
        for(int32_t i=0;i<count;++i) {
            const auto tag=*reinterpret_cast<const uint8_t*>(flags+i);
            if(tag&0x80)continue;
#ifdef LCS_GAMEPLAY_SKIN_PED_RESOLVER
            const auto ped=LCS_GAMEPLAY_SKIN_PED_RESOLVER((i<<8)|tag);
#else
            const auto ped=reinterpret_cast<uintptr_t(__cdecl*)(int32_t)>(0x54ff90)((i<<8)|tag);
#endif
            if(!ped)continue;
            const auto model=*reinterpret_cast<const uint16_t*>(ped+0x22);
            if(model>=300 || !models[model])continue;
            const auto clump=*reinterpret_cast<const uintptr_t*>(ped+0x18);
            if(clump && !bind_sa_skin_double_sided(clump))return false;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
SaGameplaySkins::SaGameplaySkins(const std::filesystem::path& world) {
    std::ifstream file(world/L"peds"/L"index.tsv");
    int id;std::string name,asset;
    if(!file)throw std::runtime_error("Gameplay skin index missing");
    while(file>>id>>name>>asset) {
        if(id<=0 || id>=290 || models_[id] || name.empty() || asset.rfind("lcs_",0)!=0 || !correct_model(id,skin_key(name)))
            throw std::runtime_error("Gameplay skin native slot/name mismatch");
        models_[id]=true;++count_;
    }
    if(!file.eof() || !count_)throw std::runtime_error("Gameplay skin index is invalid/empty");
}
bool SaGameplaySkins::service() {
    // Resolve every live pool tag each frame. Never retain actor/clump/atomic
    // addresses across deletion, streaming, respawn or a clothes/model change.
    return service_skin_pool(models_.data());
}
}
