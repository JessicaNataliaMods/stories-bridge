#pragma once
#include <array>
#include <cmath>
#include <optional>

namespace lcs {
using WanderPoint = std::array<float,3>;
struct WanderHeight {
    bool valid=false;
    std::optional<float> height;
};
// Original retail world query, not SA's empty legacy opcode. Query providers
// must consider buildings only for LOS/rays and use touchingWater=false.
// nullopt is a provider/input failure; false is an observed blocked path.
// SLUS_214.23 17BF14..17BF28 takes MIN(floor(length), maxSteps), contrary
// to the Max expression in the local reconstructed World.cpp.
template<class Queries>
std::optional<bool> source_wander_path(const WanderPoint& from,const WanderPoint& to,
        float height_tolerance,Queries& query) {
    for(const auto& point:{from,to})for(float v:point)if(!std::isfinite(v))return {};
    if(!std::isfinite(height_tolerance))return {};
    if(std::fabs(from[2]-to[2])>height_tolerance)return false;
    const auto visible=query.building_los(from,to);
    if(!visible)return {};
    if(!*visible)return false;
    WanderPoint direction{};
    for(size_t i=0;i<3;++i)direction[i]=to[i]-from[i];
    const auto square=(direction[0]*direction[0]+direction[1]*direction[1])+direction[2]*direction[2];
    if(!std::isfinite(square))return {};
    const auto length=std::sqrt(square);
    // Clamp before narrowing: no overflow/huge iteration count at large distances.
    const unsigned steps=length>=4.f?4u:unsigned(std::floor(length));
    if(!steps)return true;
    if(square>0.f)for(auto& v:direction)v*=1.f/length;
    else direction[0]=1.f;
    const auto position=[&](unsigned step) {
        WanderPoint point{};
        for(size_t i=0;i<3;++i)point[i]=from[i]+direction[i]*float(step);
        return point;
    };
    for(unsigned step=1;step<steps;++step) {
        auto point=position(step);const auto water=query.water_level(point);
        if(!water.valid)return {};
        if(!water.height)continue;
        if(!std::isfinite(*water.height))return {};
        point[2]=*water.height;
        const auto roof=query.building_vertical(point,std::fmax(from[2],to[2]));
        if(!roof.valid || (roof.height && !std::isfinite(*roof.height)))return {};
        if(!roof.height)return false;
    }
    const auto ground=query.building_vertical(from,from[2]-5.f);
    if(!ground.valid || (ground.height && !std::isfinite(*ground.height)))return {};
    if(!ground.height)return false;
    float next=*ground.height+.5f;
    for(unsigned step=1;step<steps;++step) {
        auto point=position(step);point[2]=next;
        const auto ground_step=query.building_vertical(point,next-2.f);
        if(!ground_step.valid || (ground_step.height && !std::isfinite(*ground_step.height)))return {};
        if(!ground_step.height || std::fabs(*ground_step.height-next)>1.f)return false;
        next=*ground_step.height+.5f;
    }
    return true;
}
}
