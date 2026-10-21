#include <windows.h>

#include "lcs/sniper_query.hpp"
#include "lcs/native_memory.hpp"
#include "lcs/sa_executor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace lcs {
namespace {

constexpr uintptr_t update_call = 0x0053C05C;
constexpr uintptr_t segment_call = 0x007367E3;
constexpr std::array<uintptr_t,2> call_sites{{update_call,segment_call}};
constexpr uintptr_t sniper_type_compare=0x0073677C;
constexpr uintptr_t sniper_type_skip=0x0073679F;

struct CallBinding { uintptr_t previous_target=0; };
std::array<CallBinding,call_sites.size()> bindings{};

bool frame_started=false;
bool segment_seen=false;
bool segment_valid=false;
std::array<float,3> segment_start{};
std::array<float,3> segment_end{};

constexpr uint8_t update_prefix[]={0x83,0xC4,0x04};
constexpr uint8_t update_suffix[]={0xA0,0x51,0xF8,0xB5,0x00,0x84,0xC0,0x75,0x05};
constexpr uint8_t segment_prefix[]={
    0x68,0xAC,0x88,0xC8,0x00,0x68,0xA0,0x88,0xC8,0x00,0x89,0x0D,0xAC,0x88,0xC8,0x00,
    0x89,0x15,0xB0,0x88,0xC8,0x00,0xA3,0xB4,0x88,0xC8,0x00
};
constexpr uint8_t segment_suffix[]={
    0x83,0xC4,0x08,0x8B,0x4C,0x24,0x14,0x8B,0x54,0x24,0x18,0x8B,0x44,0x24,0x1C,
    0x89,0x0B,0x89,0x53,0x04,0x89,0x43,0x08
};
constexpr uint8_t sniper_type_signature[]={0x83,0x7F,0xFC,0x22};
constexpr uint8_t sniper_type_skip_signature[]={0x75,0x4A};

using NativeWeaponUpdate = void(__cdecl*)();
using NativeLineTest = void(__cdecl*)(const float*,const float*);

size_t site_index(uintptr_t address) noexcept {
    for(size_t i=0;i<call_sites.size();++i)if(call_sites[i]==address)return i;
    return call_sites.size();
}

bool site_context_matches() noexcept {
    __try {
        const auto* update_before=reinterpret_cast<const uint8_t*>(native_entity_address(update_call-sizeof(update_prefix)));
        const auto* update_after=reinterpret_cast<const uint8_t*>(native_entity_address(update_call+5));
        const auto* segment_before=reinterpret_cast<const uint8_t*>(native_entity_address(segment_call-sizeof(segment_prefix)));
        const auto* segment_after=reinterpret_cast<const uint8_t*>(native_entity_address(segment_call+5));
        const auto* type_compare=reinterpret_cast<const uint8_t*>(native_entity_address(sniper_type_compare));
        const auto* type_skip=reinterpret_cast<const uint8_t*>(native_entity_address(sniper_type_skip));
        return std::memcmp(update_before,update_prefix,sizeof(update_prefix))==0 &&
            std::memcmp(update_after,update_suffix,sizeof(update_suffix))==0 &&
            std::memcmp(segment_before,segment_prefix,sizeof(segment_prefix))==0 &&
            std::memcmp(segment_after,segment_suffix,sizeof(segment_suffix))==0 &&
            std::memcmp(type_compare,sniper_type_signature,sizeof(sniper_type_signature))==0 &&
            std::memcmp(type_skip,sniper_type_skip_signature,sizeof(sniper_type_skip_signature))==0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool guarded_segment_copy(const float* start,const float* end,float* output) noexcept {
    __try {
        if(!start || !end)return false;
        for(size_t axis=0;axis<3;++axis) {
            output[axis]=start[axis];
            output[axis+3]=end[axis];
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

void __cdecl weapon_update_bridge() {
    sa_sniper_query_begin_frame();
    const auto previous=reinterpret_cast<NativeWeaponUpdate>(bindings[0].previous_target);
    if(previous)previous();
}

void __cdecl segment_capture_bridge(const float* start,const float* end) {
    // The game writes the per-frame segment to C888A0/C888AC, pushes those
    // addresses, then calls the line helper. Copy before that helper runs.
    sa_sniper_query_capture_segment(start,end);
    const auto previous=reinterpret_cast<NativeLineTest>(bindings[1].previous_target);
    if(previous)previous(start,end);
}

// Retail's original PS2 handler calls the scalar TestForSniperBullet body,
// not FIX_BUGS' CCollision::TestLineBox implementation. Keep its historical
// clipping arithmetic, including its asymmetric early-outs and Y-axis path.
bool retail_scalar_intersection(const std::array<float,3>& start,
                                const std::array<float,3>& end,
                                const std::array<float,6>& bounds) noexcept {
    float min_p=0.0f;
    float max_p=1.0f;

    const float min_x=std::min(start[0],end[0]);
    const float max_x=std::max(start[0],end[0]);
    if(min_x<bounds[3] || max_x>bounds[0]) {
        if(min_x<bounds[0])min_p=std::min(min_p,(bounds[0]-min_x)/(max_x-min_x));
        if(max_x>bounds[3])max_p=std::max(max_p,(max_x-bounds[3])/(max_x-min_x));
    } else return false;

    const float min_y=std::min(start[1],end[1]);
    const float max_y=std::max(start[1],end[1]);
    if(min_y<bounds[4] || max_y>bounds[1]) {
        if(min_y<bounds[1])min_p=std::min(min_p,(bounds[1]-min_y)/(max_y-min_y));
        if(max_y>bounds[4])max_p=std::max(max_p,(max_y-bounds[4])/(max_y-min_y));
    }

    const float min_z=std::min(start[2],end[2]);
    const float max_z=std::max(start[2],end[2]);
    if(min_z<bounds[5] || max_z>bounds[2]) {
        if(min_z<bounds[2])min_p=std::min(min_p,(bounds[2]-min_z)/(max_z-min_z));
        if(max_z>bounds[5])max_p=std::max(max_p,(max_z-bounds[5])/(max_z-min_z));
    } else return false;

    return min_p<=max_p;
}

} // namespace

const uintptr_t* sa_sniper_query_call_sites(size_t* count) noexcept {
    if(count)*count=call_sites.size();
    return call_sites.data();
}

uintptr_t sa_sniper_query_callback_for_site(uintptr_t address) noexcept {
    switch(site_index(address)) {
    case 0:return reinterpret_cast<uintptr_t>(&weapon_update_bridge);
    case 1:return reinterpret_cast<uintptr_t>(&segment_capture_bridge);
    default:return 0;
    }
}

bool sa_sniper_query_bind_callsite(uintptr_t address,uintptr_t previous_target) noexcept {
    const auto index=site_index(address);
    const auto callback=sa_sniper_query_callback_for_site(address);
    if(index==call_sites.size() || !previous_target || previous_target==callback || !site_context_matches())return false;
    auto& binding=bindings[index];
    if(binding.previous_target && binding.previous_target!=previous_target)return false;
    binding.previous_target=previous_target;
    return true;
}

bool sa_sniper_query_profile_matches() noexcept {
    return sa_executor_profile_matches() && site_context_matches();
}

bool sa_sniper_query_calls_healthy() noexcept {
    if(!sa_sniper_query_profile_matches())return false;
    __try {
        for(size_t i=0;i<call_sites.size();++i) {
            const auto address=call_sites[i];
            if(!bindings[i].previous_target)return false;
            const auto target_callback=sa_sniper_query_callback_for_site(address);
            const auto* code=reinterpret_cast<const uint8_t*>(native_entity_address(address));
            if(!code || code[0]!=0xE8)return false;
            int32_t relative=0;
            std::memcpy(&relative,code+1,sizeof(relative));
            const auto target=uintptr_t(int64_t(address+5)+relative);
            if(target!=target_callback)return false;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

void sa_sniper_query_begin_frame() noexcept {
    frame_started=true;
    segment_seen=false;
    segment_valid=false;
}

void sa_sniper_query_capture_segment(const float* start,const float* end) noexcept {
    if(!frame_started)return;
    segment_seen=true;
    segment_valid=false;
    float copy[6]{};
    if(!guarded_segment_copy(start,end,copy))return;
    for(size_t axis=0;axis<3;++axis)
        if(!std::isfinite(copy[axis]) || !std::isfinite(copy[axis+3]))return;
    std::copy_n(copy,3,segment_start.begin());
    std::copy_n(copy+3,3,segment_end.begin());
    segment_valid=true;
}

std::optional<bool> sa_sniper_bullet_in_area(const std::array<float,6>& bounds) noexcept {
    if(!sa_sniper_query_calls_healthy())return {};
    if(!frame_started)return {};
    for(size_t axis=0;axis<3;++axis) {
        if(!std::isfinite(bounds[axis]) || !std::isfinite(bounds[axis+3]) || bounds[axis]>bounds[axis+3])return {};
    }
    if(!segment_seen)return false;
    if(!segment_valid)return {};
    return retail_scalar_intersection(segment_start,segment_end,bounds);
}

} // namespace lcs
