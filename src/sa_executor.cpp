#include <windows.h>
#include "lcs/sa_executor.hpp"
#include "lcs/sa_script.hpp"
#include "lcs/road_area.hpp"
#include "lcs/ped_flags.hpp"
#include "lcs/crouch_duration.hpp"
#include "lcs/native_player_exports.hpp"
#include <cstddef>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace lcs {
namespace {
constexpr uintptr_t process_one=0x469EB0;
// This instruction sequence was read from the user's executable. It is not
// borrowed from LCS or inferred from a version name. Other profiles need validation.
constexpr uint8_t signature[]={0x66,0xFF,0x05,0xF4,0x47,0xA4,0x00,0x8B,0x51,0x14,0x33,0xC0,0x66,0x8B,0x02,0x83};
struct NativeFault {DWORD code=0;uintptr_t instruction=0,address=0;ULONG_PTR access=0;};
int capture_fault(EXCEPTION_POINTERS* pointers,NativeFault* fault) {
    const auto& exception=*pointers->ExceptionRecord;
    fault->code=exception.ExceptionCode;fault->instruction=reinterpret_cast<uintptr_t>(exception.ExceptionAddress);
    if(exception.ExceptionCode==EXCEPTION_ACCESS_VIOLATION && exception.NumberParameters>=2) {
        fault->access=exception.ExceptionInformation[0];fault->address=exception.ExceptionInformation[1];
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
bool invoke_protected(SaScript* script,NativeFault* fault,bool suppress_brief) {
    __try {
        if(suppress_brief) {
            // CTheScripts::bAddNextMessageToPreviousBriefs. The native help
            // handler consumes this flag; restore the caller's policy even
            // if a plugin throws while processing the original text packet.
            auto* policy=reinterpret_cast<bool*>(0xA4448B);
            const bool previous=*policy;
            __try {
                *policy=false;
                reinterpret_cast<void(__thiscall*)(SaScript*)>(process_one)(script);
            } __finally {*policy=previous;}
        } else reinterpret_cast<void(__thiscall*)(SaScript*)>(process_one)(script);
        return true;
    } __except(capture_fault(GetExceptionInformation(),fault)) { return false; }
}
bool source_task_profile() {
    constexpr uint8_t wander[]={0x6A,0x01,0x6A,0x08,0x6A,0x00,0xE8,0x4C,0x62,0xF7,0xFF,0x83,0xC4,0x08,0x50,0x6A,0x04,0x8B,0xCF,0xE8,0xAF,0xD5,0xFF,0xFF};
    constexpr uint8_t assign[]={0x64,0xA1,0x00,0x00,0x00,0x00,0x6A,0xFF,0x68,0x18,0x7D,0x83,0x00};
    constexpr uint8_t drive[]={0x6A,0xFF,0x68,0x23,0x18,0x84,0x00};
    __try {
        return !std::memcmp(reinterpret_cast<const void*>(0x490F29),wander,sizeof(wander)) &&
               !std::memcmp(reinterpret_cast<const void*>(0x465C20),assign,sizeof(assign)) &&
               !std::memcmp(reinterpret_cast<const void*>(0x63C340),drive,sizeof(drive));
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool invoke_source_task(SaScript* script,int32_t handle,int32_t direction,bool wander,bool* entered,NativeFault* fault) {
    __try {
        const auto ped=reinterpret_cast<uintptr_t(__cdecl*)(int32_t)>(0x54FF90)(handle);if(!ped)return false;
        const auto intelligence=*reinterpret_cast<const uintptr_t*>(ped+0x47C);if(!intelligence)return false;
        // bInVehicle is bit 8 (the first bit of the next byte).
        const auto vehicle=!wander && (*reinterpret_cast<const uint8_t*>(ped+0x46D)&1)?
            *reinterpret_cast<const uintptr_t*>(ped+0x58C):uintptr_t(0);
        if(!wander && !vehicle)return false;
        const auto task=reinterpret_cast<uintptr_t(__cdecl*)(uint32_t)>(0x61A5A0)(wander?0x38:0x60);if(!task)return false;
        *entered=true;
        if(wander) {
            if(direction<0 || direction>7)direction=reinterpret_cast<int32_t(__cdecl*)(int32_t,int32_t)>(0x407180)(0,7);
            reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t,int32_t,uint8_t,bool)>(0x48E4F0)(task,4,uint8_t(direction),true);
            // Source ClearAll replaces prior objectives and reactions. The SA
            // clear uses its normal task/event lifecycle rather than deleting
            // another task tree behind its back.
            reinterpret_cast<void(__thiscall*)(uintptr_t,bool,bool)>(0x601420)(intelligence,true,true);
        } else reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t,uintptr_t,uintptr_t,bool)>(0x63C340)(task,vehicle,0,false);
        reinterpret_cast<void(__thiscall*)(SaScript*,int32_t,uintptr_t,int32_t)>(0x465C20)(script,handle,task,wander?0x05DE:0x05D0);
        return true;
    } __except(capture_fault(GetExceptionInformation(),fault)){return false;}
}
}
bool sa_executor_profile_matches() {
    if(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))!=0x400000) return false;
    MEMORY_BASIC_INFORMATION info{};
    if(!VirtualQuery(reinterpret_cast<const void*>(process_one),&info,sizeof(info)) ||
       info.State!=MEM_COMMIT || (info.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
    return std::memcmp(reinterpret_cast<const void*>(process_one),signature,sizeof(signature))==0;
}
std::optional<int32_t> sa_sync_native_player_exports() {
    if(!sa_executor_profile_matches())return {};
    constexpr uint8_t ref_signature[]={0x8B,0x4C,0x24,0x04,0x56,0x8B,0x35,0x90,0x44,0xB7,0x00};
    __try {
        auto* space=reinterpret_cast<uint8_t*>(0xA49960);
        if(!owns_native_player_exports(space,627) ||
           std::memcmp(reinterpret_cast<const void*>(0x54FF60),ref_signature,sizeof(ref_signature)))return {};
        const auto ped=*reinterpret_cast<const uintptr_t*>(0xB7CD98);
        const auto pool=*reinterpret_cast<const uintptr_t*>(0xB74490);if(!pool || !ped)return {};
        const auto objects=*reinterpret_cast<const uintptr_t*>(pool);
        const auto size=*reinterpret_cast<const int32_t*>(pool+8);
        // The verified pool stores CPlayerPed-sized slots (0x7C4). Bound the
        // pointer before GetPedRef indexes its generation byte array.
        if(!objects || size<=0 || ped<objects || (ped-objects)%0x7C4 ||
           (ped-objects)/0x7C4>=uintptr_t(size))return {};
        if(!ped || (*reinterpret_cast<const uint8_t*>(ped+0x36)&7)!=3 ||
           !*reinterpret_cast<const uintptr_t*>(ped+0x480))return {};
        const auto handle=reinterpret_cast<int32_t(__cdecl*)(uintptr_t)>(0x54FF60)(ped);
        if(handle<0 || reinterpret_cast<uintptr_t(__cdecl*)(int32_t)>(0x54FF90)(handle)!=ped)return {};
        if(!publish_native_player_exports(space,627,handle))return {};
        return handle;
    } __except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
std::optional<SpatialListener> sa_audio_listener() {
    if(!sa_executor_profile_matches())return {};
    __try {
        // Plugin-SDK CCamera::m_mCameraMatrix at 0x974, native matrix right
        // at zero and position at 0x30. Read the full camera orientation,
        // including roll; do not infer pan from a horizontal heading.
        constexpr uintptr_t matrix=0xB6F028+0x974;
        SpatialListener listener;
        std::memcpy(listener.right.data(),reinterpret_cast<const void*>(matrix),12);
        std::memcpy(listener.position.data(),reinterpret_cast<const void*>(matrix+0x30),12);
        float length=0;
        for(size_t i=0;i<3;++i){if(!std::isfinite(listener.right[i]) || !std::isfinite(listener.position[i]))return {};length+=listener.right[i]*listener.right[i];}
        if(length<0.5f || length>1.5f)return {};
        return listener;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return {};}
}
std::optional<int32_t> sa_native_weapon_slot(int32_t weapon) {
    if(!sa_executor_profile_matches() || weapon<0 || weapon>=47)return {};
    constexpr uint8_t signature[]={0x8B,0x44,0x24,0x04,0x0F,0xBF,0xC0,0x6B,0xC0,0x70,0x05,0xB8,0xAA,0xC8,0x00};
    __try {
        // Exact STD CWeaponInfo branch: index*0x70 + 0xC8AAB8. Read the
        // active data's slot instead of hardcoding stock weapon buckets.
        if(std::memcmp(reinterpret_cast<const void*>(0x743C85),signature,sizeof(signature)))return {};
        const auto slot=*reinterpret_cast<const int32_t*>(0xC8AAB8+uintptr_t(weapon)*0x70+0x14);
        return slot>=0 && slot<13?std::optional<int32_t>(slot):std::nullopt;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return {};}
}
std::optional<bool> sa_can_see_dead_char(int32_t actor,int32_t type) {
    if(!sa_executor_profile_matches() || type<0 || type>32)return {};
    constexpr uint8_t scanner_signature[]={0x8B,0x98,0x7C,0x04,0x00,0x00,0x81,0xC3,0x30,0x01,0x00,0x00};
    constexpr uint8_t los_signature[]={0x8B,0x4C,0x24,0x08,0x8B,0x41,0x14,0x83,0xEC,0x0C};
    __try {
        if(std::memcmp(reinterpret_cast<const void*>(0x48ABF5),scanner_signature,sizeof(scanner_signature)) ||
           std::memcmp(reinterpret_cast<const void*>(0x5F1C40),los_signature,sizeof(los_signature)))return {};
        const auto ped=reinterpret_cast<uintptr_t(__cdecl*)(int32_t)>(0x54FF90)(actor);if(!ped)return {};
        const auto intelligence=*reinterpret_cast<const uintptr_t*>(ped+0x47C);if(!intelligence)return {};
        const auto nearby=reinterpret_cast<const uintptr_t*>(intelligence+0x130);
        // Original SA 0480 ignores its collected type operand. Retain its
        // actual scanner/LOS queries but restore LCS's type and health filters.
        for(size_t i=0;i<16;++i){
            const auto target=nearby[i];if(!target || *reinterpret_cast<const int32_t*>(target+0x598)!=type)continue;
            const auto health=*reinterpret_cast<const float*>(target+0x540);
            if(!std::isfinite(health) || health>0)continue;
            if(reinterpret_cast<bool(__cdecl*)(uintptr_t,uintptr_t,bool)>(0x5F1C40)(ped,target,true))return true;
        }
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return {};}
}
bool sa_set_char_crouch(int32_t actor,bool crouch,int32_t duration) {
    const auto length=native_crouch_duration(duration);
    if(!sa_executor_profile_matches() || (crouch && !length))return false;
    constexpr uint8_t duck_signature[]={0x56,0x8D,0x71,0x04,0x6A,0x01,0x8B,0xCE};
    constexpr uint8_t control_signature[]={0x8A,0x41,0x18,0x84,0xC0,0x75,0x29,0x8A,0x41,0x19};
    __try {
        if(crouch && (std::memcmp(reinterpret_cast<const void*>(0x6010A0),duck_signature,sizeof(duck_signature)) ||
           std::memcmp(reinterpret_cast<const void*>(0x6924F0),control_signature,sizeof(control_signature))))return false;
        const auto ped=reinterpret_cast<uintptr_t(__cdecl*)(int32_t)>(0x54FF90)(actor);if(!ped)return false;
        const auto intelligence=*reinterpret_cast<const uintptr_t*>(ped+0x47C);if(!intelligence)return false;
        auto& flags=*reinterpret_cast<uint8_t*>(ped+0x46F); // CPed::bCrouchWhenShooting
        if(crouch)flags|=2;else flags&=uint8_t(~2u);
        // A native secondary task preserves the actor's movement/combat objective.
        if(crouch){
            reinterpret_cast<void(__thiscall*)(uintptr_t,uint16_t)>(0x601230)(intelligence,*length);
            const auto task=reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t,bool)>(0x6010A0)(intelligence,true);
            // TASK_CONTROLLED needs a controlling task every few frames.
            // Original SCM crouch is an independent secondary posture. Use
            // standalone weapon crouch for finite durations, script control
            // for -1 so it lasts until the original clear command.
            if(!task || !reinterpret_cast<bool(__thiscall*)(uintptr_t,uint8_t)>(0x6924F0)(task,duration==-1?4:1))return false;
        }
        else reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x601390)(intelligence);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool sa_set_char_can_fight(int32_t actor,bool enabled) {
    if(!sa_executor_profile_matches())return false;
    __try {
        const auto ped=reinterpret_cast<uintptr_t(__cdecl*)(int32_t)>(0x54FF90)(actor);if(!ped)return false;
        // SDK CPed flags start at 0x46C; bDontFight is bit 56 (0x473 bit 0).
        auto& flags=*reinterpret_cast<uint8_t*>(ped+0x473);
        if(enabled)flags&=uint8_t(~1u);else flags|=1;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool sa_set_stationary_shooter(int32_t actor,bool enabled) {
    if(!sa_executor_profile_matches())return false;
    __try {
        const auto ped=reinterpret_cast<uintptr_t(__cdecl*)(int32_t)>(0x54FF90)(actor);if(!ped)return false;
        stationary_shooter_flags(*reinterpret_cast<uint8_t*>(ped+0x46E),enabled);
        // SA uses task state instead of LCS m_nLastPedState. Its current ped
        // state must not be cleared: that would corrupt a native active task.
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool sa_set_char_heed_threats(int32_t actor,bool enabled) {
    if(!sa_executor_profile_matches())return false;
    constexpr uint8_t signature[]={0x8A,0x51,0x0A,0x8B,0x44,0x24,0x04,0x8B,0x80,0x7C,0x04,0x00,0x00,0x53,
        0x8A,0x59,0x09,0x8A,0x49,0x08,0x05,0x38,0x02,0x00,0x00,0x88,0x58,0x0D,0x88,0x48,0x0C,0x88,0x50,0x0E};
    __try {
        if(std::memcmp(reinterpret_cast<const void*>(0x6337A0),signature,sizeof(signature)))return false;
        const auto ped=reinterpret_cast<uintptr_t(__cdecl*)(int32_t)>(0x54FF90)(actor);if(!ped)return false;
        const auto intelligence=*reinterpret_cast<const uintptr_t*>(ped+0x47C);if(!intelligence)return false;
        // Exact synchronous effect of native 0688's ProcessPed, read from the
        // user's executable: on foot, in a vehicle and during scripted tasks.
        // Do not construct/assign a new objective or cancel boarding/combat.
        for(size_t i=0;i<3;++i)*reinterpret_cast<bool*>(intelligence+0x244+i)=enabled;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool sa_set_char_crouch_when_threatened(int32_t actor) {
    if(!sa_executor_profile_matches())return false;
    __try {
        const auto ped=reinterpret_cast<uintptr_t(__cdecl*)(int32_t)>(0x54FF90)(actor);if(!ped)return false;
        // Original Script7 consumes the boolean but always sets this flag.
        // SDK CPed bit 62 is the corresponding native bCrouchWhenScared.
        *reinterpret_cast<uint8_t*>(ped+0x473)|=0x40;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool sa_player_is_restarting(bool& restarting) {
    restarting=false;
    if(!sa_executor_profile_matches())return false;
    __try {
        // CWorld::Players / PlayerInFocus; Plugin-SDK CPlayerInfo has stride
        // 0x190 and the one-byte ePlayerState at offset 0xDC. States 1/2 are
        // HAS_DIED/HAS_BEEN_ARRESTED, exactly the original LCS recovery trigger.
        const auto focus=*reinterpret_cast<const uint8_t*>(0xB7CD74);
        if(focus>=2)return false;
        const auto state=*reinterpret_cast<const uint8_t*>(0xB7CD98+focus*0x190+0xDC);
        if(state>4)return false;
        restarting=state==1 || state==2;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool sa_set_char_personality(int32_t actor,const std::string& name) {
    if(!sa_executor_profile_matches() || name.empty() || name.size()>=24)return false;
    __try {
        const auto ped=reinterpret_cast<uintptr_t(__cdecl*)(int32_t)>(0x54FF90)(actor);if(!ped)return false;
        const auto records=*reinterpret_cast<const uintptr_t*>(0xC0BBEC);if(!records)return false;
        const char* expected=name.c_str();
        // The original COWARD/SENSIBLE_GUY profiles have identical behaviour
        // values; native SA's primary 43-record enum contains SENSIBLE_GUY.
        if(name=="STAT_COWARD")expected="STAT_SENSIBLE_GUY";
        for(int32_t i=0;i<43;++i) {
            const auto record=records+uintptr_t(i)*0x34;
            if(std::strncmp(reinterpret_cast<const char*>(record+4),expected,24)!=0)continue;
            // Same pointer assignment as verified CPed::SetPedStats. Native
            // records retain native ownership; no global stats are overwritten.
            *reinterpret_cast<uintptr_t*>(ped+0x59C)=record;return true;
        }
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
std::optional<float> sa_frame_time_step() {
    if(!sa_executor_profile_matches())return {};
    __try {
        const auto value=*reinterpret_cast<const float*>(0xB7CB5C); // CTimer::ms_fTimeStep
        if(!std::isfinite(value) || value<0)return {};
        return value;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return {};}
}
NativeResult execute_sa(const NativeCall& call) {
    NativeResult result;
    if(!sa_executor_profile_matches()) { result.diagnostic="Unsupported or modified native script dispatcher"; return result; }
    if(call.source_frenzy_headshots)return sa_execute_frenzy_command(call);
    if(call.source_environment)return sa_execute_environment_command(call);
    if(call.source_garage)return sa_execute_garage_command(call);
    if(call.source_traffic)return sa_execute_traffic_command(call);
    if(call.source_statistic)return sa_execute_statistic_command(call);
    if(call.source_entity_command)return sa_execute_entity_command(call);
    if(call.source_wander_direction || call.source_wait_in_car) {
        if(call.inputs.size()!=1 || call.outputs || call.predicate || call.text ||
           (call.source_wander_direction && (call.source_wait_in_car || call.opcode!=0x05DE)) ||
           (call.source_wait_in_car && call.opcode!=0x05D0) || !source_task_profile()) {
            result.diagnostic="Original pedestrian task layout/profile rejected before entry";return result;
        }
        if(call.source_wait_in_car) {
            const auto in_car=execute_sa({0x00DF,{call.inputs[0]},{},0,0,true});
            if(!in_car.executed){return in_car;}
            if(!in_car.condition)return execute_sa({0x05BA,{call.inputs[0],-1},{},0,0,false,false,call.mission_context});
        }
        uint8_t empty_packet[2]{};SaScript script(empty_packet,call.mission_context);NativeFault fault;
        result.executed=invoke_source_task(&script,call.inputs[0],call.source_wander_direction.value_or(0),bool(call.source_wander_direction),&result.native_entered,&fault);
        if(!result.executed) {
            std::ostringstream detail;detail<<"Original pedestrian task opcode=0x"<<std::hex<<call.opcode<<" exception=0x"<<fault.code
                <<" instruction=0x"<<fault.instruction<<" actor="<<std::dec<<call.inputs[0];result.diagnostic=detail.str();
        }
        return result;
    }
    auto bytes=call.encode(); const auto expected=bytes.size();
    // Guard bytes for plugins that inspect the next instruction after execution.
    bytes.push_back(0);bytes.push_back(0);
    SaScript script(bytes.data(),call.mission_context);
    // Init is reconstructed field by field: the user's Hoodlum executable has
    // a trampoline at CRunningScript::Init. We do not execute that unverified code.
    NativeFault fault;
    result.native_entered=true;
    if(!invoke_protected(&script,&fault,call.suppress_previous_brief)) {
        std::ostringstream detail;
        detail<<"Native opcode=0x"<<std::hex<<call.opcode<<" exception=0x"<<fault.code
              <<" instruction=0x"<<fault.instruction<<" access="<<std::dec<<fault.access
              <<" address=0x"<<std::hex<<fault.address<<" inputs=";
        for(const auto input:call.inputs)detail<<"0x"<<uint32_t(input)<<",";
        result.diagnostic=detail.str();return result;
    }
    if(script.ip!=bytes.data()+expected) { result.diagnostic="SA consumed a different parameter layout"; return result; }
    result.executed=true;result.condition=script.condition;
    for(size_t i=0;i<call.outputs;++i) result.values[i]=script.locals[i];
    return result;
}
std::optional<int32_t> sa_saved_player_weapon(int32_t player) {
    if(!sa_executor_profile_matches() || player<0 || player>=2)return {};
    __try {
        const auto ped=*reinterpret_cast<const uintptr_t*>(0xB7CD98+player*0x190);
        if(!ped)return {};
        return *reinterpret_cast<const int32_t*>(ped+0x70C);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return {};}
}
std::optional<int32_t> sa_collectables(bool total,std::optional<int32_t> setting) {
    if(!sa_executor_profile_matches())return {};
    __try {
        const auto focus=*reinterpret_cast<const uint8_t*>(0xB7CD74);
        if(focus>=2)return {};
        // Verified SDK CPlayerInfo: 0x190 stride, count 0xC0, total 0xC4.
        auto& value=*reinterpret_cast<int32_t*>(0xB7CD98+focus*0x190+(total?0xC4:0xC0));
        if(setting)value=*setting;
        return value;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return {};}
}
std::optional<int32_t> sa_camera_ped_zoom(std::optional<int32_t> setting) {
    if(!sa_executor_profile_matches())return {};
    __try {
        constexpr uintptr_t camera=0xB6F028;
        const auto active=*reinterpret_cast<const uint8_t*>(camera+0x59);
        if(active>=3)return {};
        const auto mode=*reinterpret_cast<const uint16_t*>(camera+0x174+active*0x238+0xC);
        if(mode!=4)return -1; // original follow-ped-only zoom semantics
        auto& zoom=*reinterpret_cast<uint32_t*>(camera+0xC8);
        if(setting && *setting>=0 && *setting<=2)zoom=uint32_t(*setting);
        return zoom<=2 ? std::optional<int32_t>(int32_t(zoom)):std::nullopt;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return {};}
}
bool sa_freeze_ped_zoom(bool freeze) {
    if(!sa_executor_profile_matches())return false;
    __try {
        constexpr uintptr_t camera=0xB6F028;
        if(freeze) {
            // Pin the actual current zoom through SA's own script zoom facility.
            *reinterpret_cast<float*>(camera+0xD8)=*reinterpret_cast<const float*>(camera+0xD0);
        }
        *reinterpret_cast<bool*>(camera+0x38)=freeze;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
std::optional<uint8_t> sa_model_class(int32_t model) {
    if(!sa_executor_profile_matches() || model<0 || model>=20000)return {};
    __try {
        const auto info=*reinterpret_cast<const uintptr_t*>(0xA9B0C8+model*4);
        if(!info)return {};
        const auto vtable=*reinterpret_cast<const uintptr_t*>(info);
        if(!vtable)return {};
        // CBaseModelInfo's fifth virtual method is GetModelType (primary
        // class declaration). Do not infer ped/vehicle class from the ID range.
        const auto function=*reinterpret_cast<const uintptr_t*>(vtable+4*4);
        MEMORY_BASIC_INFORMATION memory{};
        if(!VirtualQuery(reinterpret_cast<const void*>(function),&memory,sizeof(memory)) ||
           memory.State!=MEM_COMMIT || !(memory.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)) ||
           (memory.Protect&PAGE_GUARD))return {};
        return reinterpret_cast<uint8_t(__thiscall*)(uintptr_t)>(function)(info);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return {};}
}
bool sa_switch_angled_roads(const std::array<float,7>& region,bool cars,bool off) {
    if(!sa_executor_profile_matches())return false;
    __try {
        // Plugin-SDK offsets (CPathFind, CPathNode), with the same native
        // neighbor propagation as SA's SwitchRoadsOffInAreaForOneRegion.
        const auto paths=*reinterpret_cast<const uintptr_t*>(0x40CA27);
        if(!paths)return false;
        const auto switch_node=reinterpret_cast<void(__thiscall*)(uintptr_t,uintptr_t,uintptr_t*,uintptr_t*,bool,bool)>(0x452160);
        for(uint32_t area=0;area<64;++area) {
            const auto nodes=*reinterpret_cast<const uintptr_t*>(paths+0x804+area*4);
            const auto count=*reinterpret_cast<const uint32_t*>(paths+0xFA4+area*4);
            const auto vehicles=*reinterpret_cast<const uint32_t*>(paths+0x10C4+area*4);
            if(count>65535 || vehicles>count)return false;
            if(!nodes)continue;
            for(uint32_t i=cars?0:vehicles;i<(cars?vehicles:count);++i) {
                const auto node=nodes+i*0x1C;
                const auto pos=reinterpret_cast<const int16_t*>(node+8);
                const auto flags=*reinterpret_cast<const uint8_t*>(node+0x18);
                const auto behaviour=(*reinterpret_cast<const uint8_t*>(node+0x1A))>>4;
                if(behaviour==1 || behaviour==2 || bool(flags&0x20)==off ||
                   !angled_road_contains(region,pos[0]/8.0f,pos[1]/8.0f,pos[2]/8.0f))continue;
                uintptr_t next1=0,next2=0;
                switch_node(paths,node,&next1,&next2,off,false);
                uint32_t steps=0;
                while(next1){if(++steps>65535)return false;switch_node(paths,next1,&next1,nullptr,off,false);}
                while(next2){if(++steps>65535)return false;switch_node(paths,next2,&next2,nullptr,off,false);}
            }
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
namespace {
struct RoadSwitchRecord {float box[6];uint8_t off,cars,pad[2];};
static_assert(sizeof(RoadSwitchRecord)==0x1C);
constexpr uintptr_t road_switch_count=0x96F050+0x3568,road_switch_records=0x96F050+0x356C;
int read_road_switches(RoadSwitchRecord* out) {
    __try {
        const auto count=*reinterpret_cast<const int32_t*>(road_switch_count);
        if(count<0 || count>64)return -1;
        std::memcpy(out,reinterpret_cast<const void*>(road_switch_records),sizeof(RoadSwitchRecord)*size_t(count));
        return count;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return -1;}
}
bool native_switch_roads(const float* b,bool cars,bool off) {
    __try {
        reinterpret_cast<void(__thiscall*)(uintptr_t,float,float,float,float,float,float,bool,bool,bool)>(0x452C80)(
            0x96F050,b[0],b[1],b[2],b[3],b[4],b[5],off,cars,false);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool append_road_switch(const RoadSwitchRecord& record) {
    __try {
        auto& count=*reinterpret_cast<int32_t*>(road_switch_count);
        if(count<0 || count>=64)return false;
        std::memcpy(reinterpret_cast<void*>(road_switch_records+sizeof(RoadSwitchRecord)*size_t(count)),&record,sizeof(record));
        ++count;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
}
std::optional<std::vector<SaRoadSwitch>> sa_road_switches() {
    if(!sa_executor_profile_matches())return {};
    RoadSwitchRecord records[64];const auto count=read_road_switches(records);
    if(count<0)return {};
    std::vector<SaRoadSwitch> result;
    for(int i=0;i<count;++i){SaRoadSwitch entry{{},records[i].off!=0,records[i].cars!=0};std::copy(records[i].box,records[i].box+6,entry.box.begin());result.push_back(entry);}
    return result;
}
bool sa_switch_roads(const std::array<float,6>& box,bool cars,bool off,bool restore_contained) {
    // SwitchRoadsOffInArea (0x452C80) removes every stored area inside the new
    // box before storing it (0x452D18), whatever its type, so a pedestrian
    // switch would drop a car switch inside it (and vice versa) and the node
    // state it describes would be lost at SAVE/LOAD. Node flags of the two
    // types are independent; re-store the other type's removed areas.
    if(!sa_executor_profile_matches())return false;
    RoadSwitchRecord before[64],after[64];
    const auto count=read_road_switches(before);
    if(count<0 || !native_switch_roads(box.data(),cars,off))return false;
    const auto present=read_road_switches(after);
    if(present<0)return false;
    for(int i=0;i<count;++i) {
        const bool same_type=(before[i].cars!=0)==cars;
        if(same_type && (!restore_contained || std::equal(box.begin(),box.end(),before[i].box)))continue;
        // Compare the stored fields only; SA leaves the two padding bytes unset.
        const bool kept=std::any_of(after,after+present,[&](const RoadSwitchRecord& r){
            return std::memcmp(r.box,before[i].box,sizeof(r.box))==0 && r.off==before[i].off && r.cars==before[i].cars;});
        if(!kept)append_road_switch(before[i]);
    }
    return true;
}
std::optional<bool> sa_char_in_control(int32_t handle) {
    if(!sa_executor_profile_matches())return {};
    __try {
        const auto ped=reinterpret_cast<uintptr_t(__cdecl*)(int32_t)>(0x54FF90)(handle);
        if(!ped)return {};
        return reinterpret_cast<bool(__thiscall*)(uintptr_t)>(0x5E3960)(ped);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return {};}
}
std::optional<int32_t> sa_scripted_pad_state(int32_t selector) {
    if(!sa_executor_profile_matches() || selector<0 || selector>44)return {};
    __try {
        const auto pad=reinterpret_cast<uintptr_t(__cdecl*)(int32_t)>(0x53FB70)(0);if(!pad)return {};
        // Script8.cpp uses action queries, not the numbered raw button list.
        // NewState/OldState are consecutive SDK CControllerState (0x30 bytes).
        const auto current=reinterpret_cast<const int16_t*>(pad);
        const auto previous=reinterpret_cast<const int16_t*>(pad+0x30);
        if(selector==0 || selector==23 || selector==42 || selector==43 || selector==44) {
            const auto button=selector==0?19:selector==23?13:selector==43?15:16;
            return int32_t(current[button]!=0 && previous[button]==0);
        }
        if(selector>=38 && selector<=41) {
            constexpr uintptr_t analogue[]={0x5409B0,0x5409E0,0x540950,0x540980};
            constexpr int buttons[]={10,11,8,9};const auto i=selector-38;
            return int32_t(reinterpret_cast<bool(__thiscall*)(uintptr_t)>(analogue[i])(pad) ||
                (current[buttons[i]]!=0 && previous[buttons[i]]==0));
        }
        if(selector==20 || selector==21 || (selector>=34 && selector<=37)) {
            const auto focus=*reinterpret_cast<const uint8_t*>(0xB7CD74);if(focus>=2)return {};
            const auto ped=*reinterpret_cast<const uintptr_t*>(0xB7CD98+focus*0x190);if(!ped)return {};
            if(selector==20)return reinterpret_cast<int32_t(__thiscall*)(uintptr_t,uintptr_t)>(0x540180)(pad,ped);
            if(selector==21)return int32_t(reinterpret_cast<bool(__thiscall*)(uintptr_t,uintptr_t)>(0x540250)(pad,ped));
            constexpr uintptr_t axes[]={0x541040,0x5410C0,0x540BD0,0x540CC0};
            return int32_t(reinterpret_cast<int16_t(__thiscall*)(uintptr_t,uintptr_t)>(axes[selector-34])(pad,ped));
        }
        struct Action {uintptr_t address;bool boolean;};
        constexpr Action actions[]={
            {0,false},{0x540080,false},{0x53FB80,false},{0x53FBD0,false},{0x53FC50,false},{0x53FC10,false},
            {0x53FC90,false},{0x53FD30,false},{0x53FDD0,true},{0x53FE10,true},{0x53FE70,true},{0x53FEC0,true},
            {0x53FEE0,true},{0x53FF30,true},{0x53FF90,false},{0x53FFE0,false},{0x540040,false},{0x540080,false},
            {0x5400D0,true},{0x540120,true},{0,false},{0,false},{0x5403F0,false},{0,false},
            {0x540610,true},{0x540640,true},{0x540670,true},{0x5406B0,true},{0x540770,true},{0x5407A0,true},
            {0x540850,true},{0x540AE0,true},{0x540B30,true},{0x540B80,true}
        };
        const auto action=actions[selector];if(!action.address)return {};
        return action.boolean ? int32_t(reinterpret_cast<bool(__thiscall*)(uintptr_t)>(action.address)(pad)) :
            int32_t(reinterpret_cast<int16_t(__thiscall*)(uintptr_t)>(action.address)(pad));
    } __except(EXCEPTION_EXECUTE_HANDLER) {return {};}
}
std::optional<bool> sa_player_made_safe() {
    if(!sa_executor_profile_matches())return {};
    __try {
        const auto pad=reinterpret_cast<uintptr_t(__cdecl*)(int32_t)>(0x53FB70)(0);
        if(!pad)return {};
        return bool(*reinterpret_cast<const uint16_t*>(pad+0x10E)&0x20);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return {};}
}
std::optional<int32_t> sa_random_char_in_area(const std::array<float,4>& area,const std::array<int32_t,3>& kinds,int32_t excluded,bool mission) {
    if(!sa_executor_profile_matches())return {};
    __try {
        // Read native pool tags and resolve handles through CPools::GetPed;
        // no assumption about CPed/CCopPed slot stride (or a limit adjuster).
        const auto pool=*reinterpret_cast<const uintptr_t*>(0xB74490);
        if(!pool)return {};
        const auto flags=*reinterpret_cast<const uintptr_t*>(pool+4);
        const auto count=*reinterpret_cast<const int32_t*>(pool+8);
        if(!flags || count<0 || count>100000)return {};
        const auto focus=*reinterpret_cast<const uint8_t*>(0xB7CD74);if(focus>=2)return {};
        const auto player=*reinterpret_cast<const uintptr_t*>(0xB7CD98+focus*0x190);if(!player)return {};
        const auto matrix=*reinterpret_cast<const uintptr_t*>(player+0x14);
        const auto player_z=*reinterpret_cast<const float*>(matrix?matrix+0x38:player+0xC);
        for(int32_t i=count-1;i>0;--i) {
            const auto tag=*reinterpret_cast<const uint8_t*>(flags+i);if(tag&0x80)continue;
            const auto handle=(i<<8)|tag;if(handle==excluded)continue;
            const auto ped=reinterpret_cast<uintptr_t(__cdecl*)(int32_t)>(0x54FF90)(handle);
            if(!ped || *reinterpret_cast<const uint8_t*>(ped+0x484)!=1 ||
               (*reinterpret_cast<const uint8_t*>(ped+0x1D)&8) || (*reinterpret_cast<const uint8_t*>(ped+0x470)&8) ||
               !reinterpret_cast<bool(__thiscall*)(uintptr_t)>(0x5E3960)(ped))continue;
            const auto type=*reinterpret_cast<const int32_t*>(ped+0x598);
            if(!((kinds[0] && (type==4 || type==5)) || (kinds[1] && type>=7 && type<=15) || (kinds[2] && (type==20 || type==22))))continue;
            const auto transform=*reinterpret_cast<const uintptr_t*>(ped+0x14);
            const auto pos=reinterpret_cast<const float*>(transform?transform+0x30:ped+4);
            if(pos[0]<area[0] || pos[0]>area[2] || pos[1]<area[1] || pos[1]>area[3] || pos[2]<player_z-5 || pos[2]>player_z+5)continue;
            if(mission && *reinterpret_cast<const uint8_t*>(0xA90850+75*8)>=75)return {};
            reinterpret_cast<void(__thiscall*)(uintptr_t,uint8_t)>(0x5E47E0)(ped,2);
            if(mission)reinterpret_cast<void(__thiscall*)(uintptr_t,int32_t,int32_t)>(0x4637E0)(0xA90850,handle,2);
            return handle;
        }
        return -1;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return {};}
}
}
