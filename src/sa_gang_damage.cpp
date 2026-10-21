#include <windows.h>
#include "lcs/sa_executor.hpp"
#include "lcs/gang_damage.hpp"
#include <array>
#include <cstring>

namespace lcs {
namespace {
constexpr uintptr_t affects_ped=0x4B35A0;
constexpr std::array<uint8_t,6> original{{0x83,0xEC,0x0C,0x56,0x8B,0xF1}};
GangDamagePolicy policy;
uintptr_t trampoline=0;
std::array<uint8_t,6> installed{};
uintptr_t resolve_actor(int32_t handle) {
    __try {return reinterpret_cast<uintptr_t(__cdecl*)(int32_t)>(0x54FF90)(handle);}
    __except(EXCEPTION_EXECUTE_HANDLER){return 0;}
}
bool read_damage(uintptr_t event,uintptr_t& source,int32_t& type,bool& weapon_attack) {
    __try {
        source=*reinterpret_cast<const uintptr_t*>(event+0x14);if(!source)return false;
        // Use the actual source entity, without unwrapping a vehicle driver.
        if((*reinterpret_cast<const uint8_t*>(source+0x36)&7)!=3)return false;
        type=*reinterpret_cast<const int32_t*>(source+0x598);
        const auto weapon=*reinterpret_cast<const int32_t*>(event+0x1C);
        if(weapon<0 || weapon>=47)return false;
        const auto fire=*reinterpret_cast<const int32_t*>(0xC8AAB8+uintptr_t(weapon)*0x70);
        weapon_attack=fire==0 || fire==1;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool __fastcall filter_damage(uintptr_t event,void*,uintptr_t victim) {
    uintptr_t source=0;int32_t type=0;bool weapon_attack=false;
    if(policy.size() && read_damage(event,source,type,weapon_attack) && policy.denies(victim,type,weapon_attack,resolve_actor))return false;
    return reinterpret_cast<bool(__thiscall*)(uintptr_t,uintptr_t)>(trampoline)(event,victim);
}
bool replace_code(const std::array<uint8_t,6>& expected,const std::array<uint8_t,6>& replacement) {
    DWORD protection=0;
    __try {
        if(std::memcmp(reinterpret_cast<const void*>(affects_ped),expected.data(),expected.size()) ||
           !VirtualProtect(reinterpret_cast<void*>(affects_ped),replacement.size(),PAGE_EXECUTE_READWRITE,&protection))return false;
        std::memcpy(reinterpret_cast<void*>(affects_ped),replacement.data(),replacement.size());
        DWORD ignored=0;VirtualProtect(reinterpret_cast<void*>(affects_ped),replacement.size(),protection,&ignored);
        FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<const void*>(affects_ped),replacement.size());return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool install_filter() {
    if(trampoline)return !std::memcmp(reinterpret_cast<const void*>(affects_ped),installed.data(),installed.size());
    constexpr uint8_t tail[]={0x8A,0x46,0x42,0x84,0xC0,0x74,0x10};
    constexpr uint8_t weapon_table[]={0x8B,0x44,0x24,0x04,0x0F,0xBF,0xC0,0x6B,0xC0,0x70,0x05,0xB8,0xAA,0xC8,0x00};
    if(std::memcmp(reinterpret_cast<const void*>(affects_ped),original.data(),original.size()) ||
       std::memcmp(reinterpret_cast<const void*>(affects_ped+6),tail,sizeof(tail)) ||
       std::memcmp(reinterpret_cast<const void*>(0x743C85),weapon_table,sizeof(weapon_table)))return false;
    auto* memory=static_cast<uint8_t*>(VirtualAlloc(nullptr,11,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));if(!memory)return false;
    std::memcpy(memory,original.data(),original.size());memory[6]=0xE9;
    auto displacement=int32_t(affects_ped+6-(reinterpret_cast<uintptr_t>(memory)+11));std::memcpy(memory+7,&displacement,4);
    DWORD previous=0;if(!VirtualProtect(memory,11,PAGE_EXECUTE_READ,&previous)){VirtualFree(memory,0,MEM_RELEASE);return false;}
    FlushInstructionCache(GetCurrentProcess(),memory,11);
    installed={{0xE9,0,0,0,0,0x90}};
    displacement=int32_t(reinterpret_cast<uintptr_t>(filter_damage)-(affects_ped+5));std::memcpy(installed.data()+1,&displacement,4);
    trampoline=reinterpret_cast<uintptr_t>(memory);
    if(!replace_code(original,installed)){trampoline=0;VirtualFree(memory,0,MEM_RELEASE);return false;}
    return true;
}
}
bool sa_set_gang_damage(int32_t actor,int32_t gang,bool enabled) {
    if(!sa_executor_profile_matches() || !GangDamagePolicy::valid_gang(gang))return false;
    const auto ped=resolve_actor(actor);if(!ped || !install_filter())return false;
    return policy.set(actor,ped,gang,enabled);
}
bool sa_release_gang_damage() {
    policy.clear();if(!trampoline)return true;
    // A later mod may chain this trampoline. Never overwrite its patch or free
    // its original target; the now-empty policy delegates all events.
    if(!replace_code(installed,original))return false;
    VirtualFree(reinterpret_cast<void*>(trampoline),0,MEM_RELEASE);trampoline=0;return true;
}
}
