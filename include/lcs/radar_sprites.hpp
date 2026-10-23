#pragma once
#include <cstdint>
#include <array>

namespace lcs {
struct RadarSpriteBlip {
    std::array<float,3> position{};
    int32_t sprite=0;
    bool contact=false,short_range=false;
};
// Script sprite identities and SA's built-in waypoint must share the same
// allocation in the command adapter and the texture consumer. In particular,
// native slot 41 is used directly by the pause-map waypoint, outside the VM.
inline constexpr int32_t source_radar_sprite(int32_t original,bool unique) {
    if(original<0 || original>62)return -1;
    if(unique) {
        if(original==0)return 0;
        if(original==40)return 42; // Vincenzo: a native mission-icon slot.
        if(original==41)return 63; // Move the displaced bomb, not the waypoint.
        if(original==62)return 41; // SA's native waypoint slot stays its own.
        return original+1;
    }
    switch(original) {
    case 0:return 0;case 1:return 2;case 2:return 3;case 3:return 4;
    case 12:return 9;case 16:return 6;case 19:return 35;
    case 25:return 31;case 27:return 63;case 28:case 41:return 45;
    case 42:return 32;case 62:return 41;
    default:return 37;
    }
}
}
