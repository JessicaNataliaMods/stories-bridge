#include "sa_remote_door_policy.hpp"
#include "lcs/native_memory.hpp"
#include <windows.h>

#include <array>
#include <cstring>
#include <utility>

namespace lcs {
namespace {

constexpr std::array<SaRemoteDoorPolicyCallSite,53> call_sites{{
    {0x0045BDC9,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x0045BDF9,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x0045BE29,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x0045BE59,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x0045BE89,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x0045BEB9,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x00477B46,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x0049B568,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x0049B585,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006A26E2,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006A2852,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006A2891,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006A3482,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006A348D,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006A3498,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006A34A3,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006A35EF,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006A4500,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006A450B,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006A6CA3,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006A9E98,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006A9FA3,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006AA089,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006AA27D,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006ADF55,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006B0E68,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006B0E73,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006B0E7E,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006B0E89,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006B167F,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006B16DD,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006C424F,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006C8F5A,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006C8FA7,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006C900B,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006C905C,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006CABD8,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006CABE3,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006CABEE,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006CABF9,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006CE2C7,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006CE2D2,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006CE2DD,SaRemoteDoorPolicyCallKind::set_door_status},
    {0x006CE2E8,SaRemoteDoorPolicyCallKind::set_door_status},

    {0x0063E270,SaRemoteDoorPolicyCallKind::component_set_door_status},
    {0x0063F1D8,SaRemoteDoorPolicyCallKind::component_set_door_status},
    {0x0063F4A2,SaRemoteDoorPolicyCallKind::component_set_door_status},
    {0x00641E23,SaRemoteDoorPolicyCallKind::component_set_door_status},
    {0x00648222,SaRemoteDoorPolicyCallKind::component_set_door_status},
    {0x00648F31,SaRemoteDoorPolicyCallKind::component_set_door_status},
    {0x0064B25B,SaRemoteDoorPolicyCallKind::component_set_door_status},
    {0x0064DFD1,SaRemoteDoorPolicyCallKind::component_set_door_status},

    {0x006C253C,SaRemoteDoorPolicyCallKind::progress_door_damage},
}};

struct CallBinding { uintptr_t previous_target=0; };
std::array<CallBinding,call_sites.size()> bindings{};

using NativeSetDoorStatus = void(__thiscall*)(uintptr_t,int32_t,uint32_t);
using NativeProgressDoorDamage = bool(__thiscall*)(uintptr_t,int32_t,uintptr_t);

size_t site_index(uintptr_t address) noexcept {
    for(size_t i=0;i<call_sites.size();++i)if(call_sites[i].address==address)return i;
    return call_sites.size();
}

template<size_t Index>
__declspec(noinline) void __fastcall set_door_status_bridge(
    uintptr_t manager,uintptr_t,int32_t door,uint32_t status) {
    const auto native=reinterpret_cast<NativeSetDoorStatus>(bindings[Index].previous_target);
    if(!native)return;
    auto forwarded=status;
    if(sa_remote_door_policy_calls_healthy() && door>=1 && door<=5 && status==2 &&
       manager>=0x5A0 && sa_remote_door_policy_vehicle_live(manager-0x5A0))
        forwarded=1; // SA DAMSTATE_DAMAGED -> source SWINGING / SA OPENED.
    native(manager,door,forwarded);
}

template<size_t Index>
__declspec(noinline) void __fastcall component_set_door_status_bridge(
    uintptr_t manager,uintptr_t,int32_t component,uint32_t status) {
    const auto native=reinterpret_cast<NativeSetDoorStatus>(bindings[Index].previous_target);
    if(!native)return;
    auto forwarded=status;
    const bool source_door_node=component>=8 && component<=11;
    if(sa_remote_door_policy_calls_healthy() && source_door_node && status==2 &&
       manager>=0x5A0 && sa_remote_door_policy_vehicle_live(manager-0x5A0))
        forwarded=1; // The 8..11 nodes map only to SA doors 2..5.
    native(manager,component,forwarded);
}

template<size_t Index>
__declspec(noinline) bool __fastcall progress_door_damage_bridge(
    uintptr_t manager,uintptr_t,int32_t door,uintptr_t vehicle) {
    const auto native=reinterpret_cast<NativeProgressDoorDamage>(bindings[Index].previous_target);
    if(!native)return false;
    if(door<1 || door>5 || !vehicle || vehicle>UINTPTR_MAX-0x5A0 || manager!=vehicle+0x5A0 ||
       !sa_remote_door_policy_calls_healthy() || !sa_remote_door_policy_vehicle_live(vehicle))
        return native(manager,door,vehicle);

    const auto status_cell=reinterpret_cast<uint8_t*>(manager+door+9);
    const uint8_t old_status=*status_cell;
    const bool native_result=native(manager,door,vehicle);

    // The source increments the door status deterministically. SA's RNG gates
    // some damaged states, so apply the source transition only after the
    // chained native method returns normally; its door-angle/RNG side effects
    // remain intact and the caller sees the source result before SetDoorDamage.
    uint8_t source_status=0;
    bool source_result=false;
    switch(old_status) {
    case 0:case 2:source_status=1;source_result=true;break;
    case 1:case 3:source_status=4;source_result=true;break;
    case 4:return false;
    default:return native_result;
    }
    if(sa_remote_door_policy_calls_healthy() && sa_remote_door_policy_vehicle_live(vehicle) &&
       manager==vehicle+0x5A0) {
        *status_cell=source_status;
        return source_result;
    }
    return native_result;
}

template<size_t Index>
uintptr_t callback_address_for_site() noexcept {
    if constexpr(call_sites[Index].kind==SaRemoteDoorPolicyCallKind::set_door_status)
        return reinterpret_cast<uintptr_t>(&set_door_status_bridge<Index>);
    else if constexpr(call_sites[Index].kind==SaRemoteDoorPolicyCallKind::component_set_door_status)
        return reinterpret_cast<uintptr_t>(&component_set_door_status_bridge<Index>);
    else
        return reinterpret_cast<uintptr_t>(&progress_door_damage_bridge<Index>);
}

template<size_t... Indices>
std::array<uintptr_t,sizeof...(Indices)> make_callback_addresses(std::index_sequence<Indices...>) noexcept {
    return {{callback_address_for_site<Indices>()...}};
}

const auto callback_addresses=make_callback_addresses(std::make_index_sequence<call_sites.size()>{});

} // namespace

const SaRemoteDoorPolicyCallSite* sa_remote_door_policy_call_sites(size_t* count) noexcept {
    if(count)*count=call_sites.size();
    return call_sites.data();
}

uintptr_t sa_remote_door_policy_callback_for_site(uintptr_t address) noexcept {
    const auto index=site_index(address);
    return index==call_sites.size()?0:callback_addresses[index];
}

bool sa_remote_door_policy_bind_callsite(uintptr_t address,uintptr_t previous_target) noexcept {
    const auto index=site_index(address);
    if(index==call_sites.size() || !previous_target || previous_target==callback_addresses[index])return false;
    auto& binding=bindings[index];
    if(binding.previous_target && binding.previous_target!=previous_target)return false;
    binding.previous_target=previous_target;
    return true;
}

bool sa_remote_door_policy_calls_healthy() noexcept {
    __try {
        for(size_t i=0;i<call_sites.size();++i) {
            const auto& call=call_sites[i];
            if(!bindings[i].previous_target)return false;
            const auto address=native_entity_address(call.address);
            if(!address)return false;
            const auto* code=reinterpret_cast<const uint8_t*>(address);
            if(*code!=0xE8)return false;
            int32_t relative=0;
            std::memcpy(&relative,code+1,sizeof(relative));
            const auto target=uintptr_t(int64_t(call.address+5)+relative);
            if(target!=callback_addresses[i])return false;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

} // namespace lcs
