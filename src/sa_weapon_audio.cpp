#include "sa_weapon_audio.hpp"
#include <windows.h>
#include <cmath>
#include <cstring>
namespace lcs {
bool sa_weapon_audio_profile_matches() {
    constexpr uint8_t fire_prologue[]={0x8B,0x44,0x24,0x08,0x85,0xC0,0x56,0x8B,0xF1,0x0F,0x84,0xB0,0x02,0x00,0x00,0x8B};
    constexpr uint8_t reload_prologue[]={0x51,0x53,0x55,0x56,0x57,0x8B,0x7C,0x24,0x1C,0x85,0xFF,0x8B,0xD9,0xC7,0x44,0x24};
    __try {
        if(std::memcmp(reinterpret_cast<const void*>(SaWeaponAudioProfile::fire),fire_prologue,sizeof(fire_prologue)) ||
           std::memcmp(reinterpret_cast<const void*>(SaWeaponAudioProfile::reload),reload_prologue,sizeof(reload_prologue)))return false;
        const auto targets=[](uintptr_t site,uintptr_t target){
            const auto* code=reinterpret_cast<const uint8_t*>(site);
            int32_t relative=0;std::memcpy(&relative,code+1,4);
            return code[0]==0xE8 && uintptr_t(int64_t(site+5)+relative)==target;
        };
        for(const auto site:SaWeaponAudioProfile::fire_calls)if(!targets(site,SaWeaponAudioProfile::fire))return false;
        for(const auto site:SaWeaponAudioProfile::reload_calls)if(!targets(site,SaWeaponAudioProfile::reload))return false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
std::optional<std::array<float,3>> sa_physical_position(const void* entity) {
    if(!entity)return std::nullopt;
    __try {
        const auto base=reinterpret_cast<uintptr_t>(entity);
        const auto matrix=*reinterpret_cast<const uintptr_t*>(base+0x14);
        std::array<float,3> position{};
        std::memcpy(position.data(),reinterpret_cast<const void*>(matrix?matrix+0x30:base+4),12);
        for(const auto value:position)if(!std::isfinite(value))return std::nullopt;
        return position;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return std::nullopt;}
}
}
