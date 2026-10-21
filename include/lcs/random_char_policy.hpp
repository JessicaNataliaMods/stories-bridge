#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>
namespace lcs {
// Retail geometry/flags for1494 (SLUS_214.23 handler2502B0).
// Approved SA adaptation: waiting means a script-owned wait, not a replica
// of the LCS ambient AI's internal m_nWaitState. Native SA AI is preserved.
// Only these facts gate selection:
// live pool slot (index 0 included), bRemoveFromWorld (+48 bit0), bFadeOut
// (+198 bit23), m_nWaitState==WAITSTATE_FALSE (+360), IsWithinArea and an
// inclusive player Z +/-5.0f window. No ped type, creation, LastRandomPedId,
// player, control or group filter exists in the original.
struct OriginalUncheckedCharCandidate {
    int32_t handle=-1;
    std::array<float,3> position{};
    bool removed=false,fading=false,waiting=false;
};
inline std::optional<bool> original_unchecked_char_eligible(const OriginalUncheckedCharCandidate& c,
        std::array<float,4> area,float player_z) {
    if(!std::isfinite(player_z) || c.handle<0)return {};
    for(float x:area)if(!std::isfinite(x))return {};
    for(float x:c.position)if(!std::isfinite(x))return {};
    // IsWithinArea normalizes X and Y independently; endpoints are inclusive.
    if(area[0]>area[2])std::swap(area[0],area[2]);
    if(area[1]>area[3])std::swap(area[1],area[3]);
    if(c.removed || c.fading || c.waiting)return false;
    if(c.position[0]<area[0] || c.position[0]>area[2] || c.position[1]<area[1] || c.position[1]>area[3])return false;
    // 250398..2503C4: c.ole.s (pz-5)<=z and z<=(pz+5), single precision.
    constexpr float tolerance=5.0f;
    const float low=player_z-tolerance,high=player_z+tolerance;
    return low<=c.position[2] && c.position[2]<=high;
}
// 738 / GET_RANDOM_CHAR_IN_ZONE: the three SCM kind flags select native ped types exactly
// as the approved GET_RANDOM_CHAR_IN_AREA SA port: civilians 4/5, gang members 7..15,
// criminals/prostitutes 20 and 22.
inline bool original_zone_char_kind_allowed(int32_t native_type,const std::array<int32_t,3>& kinds) {
    return (kinds[0] && (native_type==4 || native_type==5)) || (kinds[1] && native_type>=7 && native_type<=15) ||
        (kinds[2] && (native_type==20 || native_type==22));
}
}
