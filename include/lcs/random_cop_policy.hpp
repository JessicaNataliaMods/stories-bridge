#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>
namespace lcs {
struct OriginalCopCandidate {
    int32_t handle=-1,original_model=-1,ped_type=-1,created_by=-1,original_state=-1;
    std::array<float,3> position{};
    bool controllable=false,removed=false,fading=false,leader=false,has_leader=false;
};
// Requires an authoritative original model mapping and original state, not an
// arbitrary native police-looking skin or guessed native movement state.
inline bool original_cop_category(int32_t model,const std::array<int32_t,5>& enabled) {
    if(model>=1 && model<=4)return enabled[size_t(model-1)]!=0;
    return model>=97 && model<=104 && enabled[4]!=0;
}
inline std::optional<bool> original_cop_eligible(const OriginalCopCandidate& c,std::array<float,4> area,
        const std::array<int32_t,5>& kinds,float player_z,int32_t excluded) {
    if(!std::isfinite(player_z))return {};
    for(float x:area)if(!std::isfinite(x))return {};
    for(float x:c.position)if(!std::isfinite(x))return {};
    if(area[0]>area[2])std::swap(area[0],area[2]);
    if(area[1]>area[3])std::swap(area[1],area[3]);
    if(c.handle<0 || c.original_model<0 || c.original_state<0)return {};
    return c.handle!=excluded && c.ped_type==6 && original_cop_category(c.original_model,kinds) && c.created_by==1 &&
        (c.controllable || c.original_state==50 || c.original_state==39) && !c.removed && !c.fading && !c.leader && !c.has_leader &&
        c.position[0]>=area[0] && c.position[0]<=area[2] && c.position[1]>=area[1] && c.position[1]<=area[3] &&
        c.position[2]>=player_z-10.f && c.position[2]<=player_z+10.f;
}
}
