#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace lcs {
// The standard SA SCM globals are byte offsets, not local-variable indices.
// These exports refer to the real CWorld::Players[0] pool entry; the LCS VM has
// its own globals and must never be copied into this native storage.
inline bool owns_native_player_exports(const uint8_t* space,size_t length) {
    constexpr uint8_t name[]={0xA4,0x03,0x09,'L','C','S','S','A','B','T',0};
    return space && length>=616+sizeof(name) && !std::memcmp(space+616,name,sizeof(name));
}
inline bool publish_native_player_exports(uint8_t* space,size_t length,int32_t actor) {
    if(actor<0 || !owns_native_player_exports(space,length))return false;
    const int32_t player=0;
    std::memcpy(space+8,&player,sizeof(player));
    std::memcpy(space+12,&actor,sizeof(actor));
    return true;
}
}
