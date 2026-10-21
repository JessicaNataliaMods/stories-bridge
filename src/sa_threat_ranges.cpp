#include <windows.h>
#include "lcs/sa_executor.hpp"
#include "lcs/threat_ranges.hpp"
#include <cstring>
namespace lcs {
namespace {
ThreatRangePolicy policy;
bool read_pool(uintptr_t& pool,uintptr_t& flags,int32_t& count) {
    __try {
        pool=*reinterpret_cast<const uintptr_t*>(0xB74490);if(!pool)return false;
        flags=*reinterpret_cast<const uintptr_t*>(pool+4);count=*reinterpret_cast<const int32_t*>(pool+8);
        return flags && count>=0 && count<=100000;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool read_actor(uintptr_t flags,int32_t index,ThreatRangeSnapshot& snapshot,bool& unused) {
    __try {
        const auto tag=*reinterpret_cast<const uint8_t*>(flags+index);unused=bool(tag&0x80);if(unused)return true;
        snapshot.handle=(index<<8)|tag;
        snapshot.ped=reinterpret_cast<uintptr_t(__cdecl*)(int32_t)>(0x54FF90)(snapshot.handle);if(!snapshot.ped)return false;
        snapshot.intelligence=*reinterpret_cast<const uintptr_t*>(snapshot.ped+0x47C);if(!snapshot.intelligence)return false;
        std::memcpy(snapshot.ranges.data(),reinterpret_cast<const void*>(snapshot.intelligence+0xBC),8);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool write_actor(const ThreatRangeSnapshot& snapshot,const std::array<float,2>& ranges) {
    __try {
        const auto ped=reinterpret_cast<uintptr_t(__cdecl*)(int32_t)>(0x54FF90)(snapshot.handle);
        if(ped!=snapshot.ped || *reinterpret_cast<const uintptr_t*>(ped+0x47C)!=snapshot.intelligence ||
           std::memcmp(reinterpret_cast<const void*>(snapshot.intelligence+0xBC),snapshot.ranges.data(),8))return false;
        std::memcpy(reinterpret_cast<void*>(snapshot.intelligence+0xBC),ranges.data(),8);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool profile() {
    if(!sa_executor_profile_matches())return false;
    constexpr uint8_t hearing[]={0x8B,0x44,0x24,0x04,0x89,0x81,0xBC,0x00,0x00,0x00};
    constexpr uint8_t seeing[]={0x8B,0x44,0x24,0x04,0x89,0x81,0xC0,0x00,0x00,0x00};
    constexpr uint8_t scanner[]={0x8B,0x8D,0x7C,0x04,0x00,0x00,0xD9,0x81,0xBC,0x00,0x00,0x00};
    __try {return !std::memcmp(reinterpret_cast<const void*>(0x600BE0),hearing,sizeof(hearing)) &&
        !std::memcmp(reinterpret_cast<const void*>(0x600BF0),seeing,sizeof(seeing)) &&
        !std::memcmp(reinterpret_cast<const void*>(0x5FFA61),scanner,sizeof(scanner));}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
bool sa_tick_threat_ranges() {
    if(!policy.active())return true;
    if(!profile())return false;
    uintptr_t pool=0,flags=0;int32_t count=0;if(!read_pool(pool,flags,count))return false;
    std::vector<ThreatRangeSnapshot> actors;actors.reserve(size_t(count));
    for(int32_t i=0;i<count;++i){ThreatRangeSnapshot actor;bool unused=false;if(!read_actor(flags,i,actor,unused))return false;if(!unused)actors.push_back(actor);}
    return policy.tick(actors,write_actor);
}
bool sa_set_threat_range_multiplier(uint16_t multiplier) {
    if(!multiplier || !profile())return false;
    policy.set(multiplier);return sa_tick_threat_ranges();
}
bool sa_release_threat_ranges(){policy.set(1);return sa_tick_threat_ranges();}
}
