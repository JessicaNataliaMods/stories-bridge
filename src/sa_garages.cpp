#include <windows.h>
#include "lcs/sa_executor.hpp"
#include "lcs/hideout_capacity.hpp"
#include <cmath>
#include <cstring>
#include <cstdio>

namespace lcs {
namespace {
struct CapacityHook {uintptr_t address;uint8_t original[5];uint8_t replacement[5]{};bool installed=false;};
CapacityHook entry_hook{0x44BDA7,{0xE8,0x44,0xD8,0xFF,0xFF}};
CapacityHook storage_hook{0x44BE44,{0xE8,0xB7,0xDA,0xFF,0xFF}};
std::array<int,50> capacities=[](){std::array<int,50> a{};a.fill(-1);return a;}();
int capacity_for(uintptr_t garage) {
    if(garage<0x96C048 || (garage-0x96C048)%0xD8)return -1;
    const auto index=(garage-0x96C048)/0xD8;return index<50?capacities[index]:-1;
}
int32_t __fastcall entrance_count(uintptr_t garage,uintptr_t,uintptr_t ignore) {
    const auto actual=reinterpret_cast<int32_t(__thiscall*)(uintptr_t,uintptr_t)>(0x4495F0)(garage,ignore);
    return hideout_entry_count(actual,capacity_for(garage));
}
void __fastcall store_hideout(uintptr_t garage,uintptr_t,uintptr_t stored,int32_t slots) {
    reinterpret_cast<void(__thiscall*)(uintptr_t,uintptr_t,int32_t)>(0x449900)(garage,stored,hideout_storage_slots(slots,capacity_for(garage)));
}
bool patch_capacity(CapacityHook& hook,bool install,uintptr_t callback=0) {
    __try {
        auto* code=reinterpret_cast<uint8_t*>(hook.address);
        if(hook.installed==install)return !install || std::memcmp(code,hook.replacement,5)==0;
        if(std::memcmp(code,install?hook.original:hook.replacement,5)!=0)return false;
        if(install) {hook.replacement[0]=0xE8;const auto delta=uint32_t(callback-(hook.address+5));std::memcpy(hook.replacement+1,&delta,4);}
        DWORD protection=0;if(!VirtualProtect(code,5,PAGE_EXECUTE_READWRITE,&protection))return false;
        std::memcpy(code,install?hook.replacement:hook.original,5);
        FlushInstructionCache(GetCurrentProcess(),code,5);
        DWORD ignored=0;
        if(!VirtualProtect(code,5,protection,&ignored)) {
            std::memcpy(code,install?hook.original:hook.replacement,5);FlushInstructionCache(GetCurrentProcess(),code,5);
            VirtualProtect(code,5,protection,&ignored);return false;
        }
        hook.installed=install;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool set_garage_flag(const char* name,uint8_t flag) {
    __try {
        const auto count=*reinterpret_cast<const int32_t*>(0x96C024);
        if(count<0 || count>50)return false;
        for(int32_t i=0;i<count;++i) {
            const auto garage=reinterpret_cast<uint8_t*>(0x96C048+i*0xD8);
            if(garage[0x4C] && std::memcmp(garage+0x44,name,8)==0) {
                // Native m_bDoorOpensUp (bit 3) selects BuildRotatedDoorMatrix.
                // Native door progress is normalized, so do not apply LCS's
                // old half-height formula to SA's garage geometry.
                garage[0x4E]|=flag;return true;
            }
        }
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
// SA 1.0 US CGarages::AddOne, verified against the local reverse and GRGE loader.
// Use SA's own initialization and door discovery, never construct CGarage bytes.
bool register_native(const float* bounds,uint8_t type,const char* name,char* actual) {
    __try {
        const auto count=*reinterpret_cast<const int32_t*>(0x96C024);
        if(count<0 || count>50)return false;
        for(int32_t i=0;i<count;++i) {
            const auto garage=reinterpret_cast<const uint8_t*>(0x96C048+i*0xD8);
            // The first-mission bootstrap already declared the first original
            // garage. Reuse it only if its identity, position and policy match.
            if(type==19 && std::memcmp(garage+0x44,"LCSVIC1",8)==0 && garage[0x4C]==19) {
                constexpr float expected[]={1160.7f,-267.5f,16.25f,1156.0f,-267.5f,1160.7f,-273.7f,19.25f};
                bool same=true;
                for(size_t n=0;n<8;++n)same=same && std::fabs(bounds[n]-expected[n])<0.001f;
                for(size_t n=0;n<3;++n)same=same && std::fabs(reinterpret_cast<const float*>(garage)[n]-bounds[n])<0.001f;
                if(same){std::memcpy(actual,"LCSVIC1",8);return true;}
            }
            if(std::memcmp(garage+0x44,name,8)==0)return false;
        }
        if(count>=50)return false;
        using AddOne=void(__cdecl*)(float,float,float,float,float,float,float,float,uint8_t,uint32_t,char*,uint32_t);
        reinterpret_cast<AddOne>(0x4471E0)(bounds[0],bounds[1],bounds[2],bounds[3],bounds[4],bounds[5],bounds[6],bounds[7],type,0,const_cast<char*>(name),0);
        if(*reinterpret_cast<const int32_t*>(0x96C024)!=count+1)return false;
        const auto garage=reinterpret_cast<uint8_t*>(0x96C048+count*0xD8);
        if(std::memcmp(garage+0x44,name,8)!=0 || garage[0x4C]!=type)return false;
        // Runtime AddOne does not initialize door progress/state/timer.
        // Use SA's own per-garage initializer, including its flag policy,
        // rather than duplicating only part of its switch table.
        reinterpret_cast<void(__thiscall*)(uint8_t*)>(0x447600)(garage);
        std::memcpy(actual,name,8);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
}
std::optional<std::string> sa_create_garage(const std::array<float,8>& bounds,int32_t type,int32_t original_id) {
    if(!sa_executor_profile_matches() || type<0 || type>45 || original_id<0 || original_id>=50)return {};
    for(const auto value:bounds)if(!std::isfinite(value))return {};
    char name[9]{},actual[9]{};
    std::snprintf(name,sizeof(name),"LCSG%02d",original_id);
    if(!register_native(bounds.data(),uint8_t(type),name,actual))return {};
    return std::string(actual);
}
bool sa_rotate_garage_door(const std::string& name) {
    if(!sa_executor_profile_matches() || name.empty() || name.size()>8 || name.find('\0')!=std::string::npos)return false;
    char label[9]{};std::memcpy(label,name.data(),name.size());return set_garage_flag(label,0x08);
}
bool sa_leave_garage_camera(const std::string& name) {
    if(!sa_executor_profile_matches() || name.empty() || name.size()>8 || name.find('\0')!=std::string::npos)return false;
    char label[9]{};std::memcpy(label,name.data(),name.size());return set_garage_flag(label,0x20);
}
bool sa_set_garage_capacity(const std::string& name,uint8_t capacity) {
    if(!sa_executor_profile_matches() || name.empty() || name.size()>8 || capacity>4)return false;
    char label[9]{};std::memcpy(label,name.data(),name.size());int index=-1;
    __try {
        const auto count=*reinterpret_cast<const int32_t*>(0x96C024);if(count<0 || count>50)return false;
        for(int i=0;i<count;++i)if(std::memcmp(reinterpret_cast<const void*>(0x96C048+i*0xD8+0x44),label,8)==0){index=i;break;}
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    if(index<0)return false;
    if(!patch_capacity(entry_hook,true,reinterpret_cast<uintptr_t>(&entrance_count)))return false;
    if(!patch_capacity(storage_hook,true,reinterpret_cast<uintptr_t>(&store_hideout))){patch_capacity(entry_hook,false);return false;}
    capacities[index]=capacity;return true;
}
bool sa_release_garage_capacity() {
    capacities.fill(-1);
    const auto a=patch_capacity(entry_hook,false),b=patch_capacity(storage_hook,false);return a && b;
}
bool sa_clear_garage_message() {
    if(!sa_executor_profile_matches())return false;
    __try {*reinterpret_cast<uint32_t*>(0x96C01C)=*reinterpret_cast<const uint32_t*>(0x96C020);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
}
