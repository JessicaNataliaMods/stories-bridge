#pragma once
#include <array>
#include <cmath>
#include <algorithm>
namespace lcs {
// Original LCS SwitchRoadsInAngledArea: p1->p2 and its left perpendicular.
// A bounding box would incorrectly disable nodes outside the rotated rectangle.
inline bool angled_road_contains(const std::array<float,7>& a,float x,float y,float z) noexcept {
    const float dx=a[3]-a[0],dy=a[4]-a[1];
    const float length=std::sqrt(dx*dx+dy*dy);
    if(!(length>0) || !(a[6]>=0) || z<std::min(a[2],a[5]) || z>std::max(a[2],a[5]))return false;
    const float px=x-a[0],py=y-a[1];
    const float along=(px*dx+py*dy)/length,side=(-px*dy+py*dx)/length;
    return along>=0 && along<=length && side>=0 && side<=a[6];
}
}
