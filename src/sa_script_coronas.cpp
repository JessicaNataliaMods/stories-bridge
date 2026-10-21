#include <windows.h>
#include "sa_script_coronas.hpp"
#include "lcs/sa_executor.hpp"
#include <cstring>
#include <map>
#include <cmath>
namespace lcs {
namespace {
struct Owner {ScriptCorona source;bool accepted=false,quarantined=false;};
std::map<uint32_t,Owner> owned;
struct CoronaVector {float x,y,z;};
#ifdef LCS_ISOLATED_CORONA_TEST
uintptr_t corona_register_entry=0,corona_lookup_entry=0,corona_pool=0;
const void* mock_corona_ground_entry=nullptr;
float __cdecl mock_corona_ground(float,float);
void __cdecl mock_register_corona(uint32_t,void*,uint8_t,uint8_t,uint8_t,uint8_t,
    const CoronaVector&,float,float,uint8_t,uint8_t,bool,bool,int,float,bool,float,uint8_t,float,bool,bool);
#else
constexpr uintptr_t corona_register_entry=0x6FC180,corona_lookup_entry=0x6FC580,corona_pool=0xC3E058;
#endif
bool profile(){
    constexpr unsigned char entry[]={0x83,0xEC,0x1C,0x55,0x56,0x8B,0x74,0x24,0x2C,0x85,0xF6,0x57};
    constexpr unsigned char lookup[]={0x0F,0xB6,0x54,0x24,0x28,0x8B,0x04,0x95,0x00,0xE0,0xC3,0x00};
    __try{return !std::memcmp(reinterpret_cast<void*>(corona_register_entry),entry,sizeof(entry)) &&
        !std::memcmp(reinterpret_cast<void*>(corona_lookup_entry),lookup,sizeof(lookup));}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool invoke(uint32_t id,const ScriptCorona* source,const ScriptCorona* previous,bool* entered){
    __try {
        // Primary SDK: CCoronas array64, CRegisteredCorona stride3C, ID+C.
        // Refuse an ID already used by another mod; never clear its entry.
        for(size_t i=0;i<64;++i){
            const auto entry=corona_pool+i*0x3C;
            if(*reinterpret_cast<const uint32_t*>(entry+0xC)!=id)continue;
            if(!previous || *reinterpret_cast<const uint32_t*>(entry+0x38) ||
               std::memcmp(reinterpret_cast<const void*>(entry),previous->position.data(),12) ||
               *reinterpret_cast<const float*>(entry+0x14)!=-previous->size ||
               *reinterpret_cast<const uint8_t*>(entry+0x2C)!=previous->r ||
               *reinterpret_cast<const uint8_t*>(entry+0x2D)!=previous->g ||
               *reinterpret_cast<const uint8_t*>(entry+0x2E)!=previous->b)return false;
        }
        const CoronaVector position{source->position[0],source->position[1],source->position[2]};
        // SA DRAW_CORONA proves the native defaults: far450, reflection1,
        // no obstacles, near1.5 and fadeSpeed15. Original script emitter negates size.
        using Register=void(__cdecl*)(uint32_t,void*,uint8_t,uint8_t,uint8_t,uint8_t,
            const CoronaVector&,float,float,uint8_t,uint8_t,bool,bool,int,float,bool,float,uint8_t,float,bool,bool);
#ifdef LCS_ISOLATED_CORONA_TEST
        const Register function=mock_register_corona;
#else
        const Register function=reinterpret_cast<Register>(0x6FC580);
#endif
        *entered=true;
        function(id,nullptr,source->r,source->g,source->b,255,
            position,-source->size,450.f,source->type,source->flare,true,false,0,0.f,false,1.5f,0,15.f,false,false);
        // Culling or a full native render pool is an actual engine decision,
        // not evidence that an emitter rendered. Its persistent source object remains.
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
bool sa_script_coronas_available(){return sa_executor_profile_matches() && profile();}
std::optional<float> sa_script_corona_ground(float x,float y){
    if(!sa_script_coronas_available() || !std::isfinite(x) || !std::isfinite(y))return {};
    __try {
        constexpr uint8_t entry[]={0x83,0xEC,0x38,0x8B,0x44,0x24,0x3C,0x8B,0x4C,0x24,0x40};
#ifdef LCS_ISOLATED_CORONA_TEST
        if(!mock_corona_ground_entry || std::memcmp(mock_corona_ground_entry,entry,sizeof(entry)))return {};
        const auto height=mock_corona_ground(x,y);
#else
        if(std::memcmp(reinterpret_cast<void*>(0x569660),entry,sizeof(entry)))return {};
        const auto height=reinterpret_cast<float(__cdecl*)(float,float)>(0x569660)(x,y);
#endif
        if(!std::isfinite(height))return {};return height;
    } __except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
bool sa_register_script_corona(uintptr_t identity,const ScriptCorona& source){
    if(!identity || identity>UINT32_MAX || source.type>8 || source.flare>2 || !sa_script_coronas_available())return false;
    const auto id=uint32_t(identity);
    auto& owner=owned[id]; // allocate before native entry, never after an effect
    if(owner.quarantined)return false;
    bool entered=false;
    if(!invoke(id,&source,owner.accepted?&owner.source:nullptr,&entered)){
        if(entered)owner.quarantined=true; // never replay a partially entered renderer
        return false;
    }
    owner.source=source;owner.accepted=true;return true;
}
}
