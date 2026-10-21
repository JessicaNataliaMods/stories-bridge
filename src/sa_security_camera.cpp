#include <windows.h>

#include "sa_security_camera.hpp"
#include "lcs/native_memory.hpp"
#include "lcs/sa_executor.hpp"
#include <cstring>

namespace lcs {
namespace {
constexpr uintptr_t post_effects_cctv_switch = 0x7011B0;
constexpr uintptr_t post_effects_cctv_flag = 0xC402C5;

struct CctvOwnership {
    bool active = false;
    bool original = false;
    bool applied = true;
};
CctvOwnership ownership;

#ifdef LCS_ISOLATED_SECURITY_CAMERA_BACKEND_TEST
SecurityCameraTestCallbacks callbacks{};

bool profile_matches() {
    return callbacks.profile_matches && callbacks.profile_matches();
}
bool read_cctv(bool* enabled) {
    return callbacks.read_cctv && callbacks.read_cctv(enabled);
}
bool switch_cctv(bool enabled) {
    return callbacks.switch_cctv && callbacks.switch_cctv(enabled);
}
#else
bool profile_matches() {
    if(!sa_executor_profile_matches())return false;
    __try {
        const uint8_t setter[]={0x8A,0x44,0x24,0x04,0xA2,0xC5,0x02,0xC4,0x00,0xC3};
        const uint8_t renderer[]={0x83,0xEC,0x08,0xA1,0x24,0x7B,0xC9,0x00};
        const uint8_t gate[]={0xA0,0xC5,0x02,0xC4,0x00,0x84,0xC0,0x74,0x05,0xE8,0x8E,0xDC,0xFF,0xFF};
        return std::memcmp(reinterpret_cast<const void*>(native_entity_address(post_effects_cctv_switch)),setter,sizeof(setter))==0 &&
            std::memcmp(reinterpret_cast<const void*>(native_entity_address(0x702F40)),renderer,sizeof(renderer))==0 &&
            std::memcmp(reinterpret_cast<const void*>(native_entity_address(0x7052A4)),gate,sizeof(gate))==0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool read_cctv(bool* enabled) {
    __try {
        *enabled = *reinterpret_cast<const uint8_t*>(native_entity_address(post_effects_cctv_flag))!=0;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool switch_cctv(bool enabled) {
    __try {
        // CPostEffects::ScriptCCTVSwitch(bool), x86 cdecl ABI, retail 1.0 US.
        reinterpret_cast<void(__cdecl*)(bool)>(native_entity_address(post_effects_cctv_switch))(enabled);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
#endif

NativeResult failure(const char* message, bool entered = false) {
    NativeResult result;
    result.diagnostic = message;
    result.native_entered = entered;
    return result;
}
NativeResult success(bool entered = false) {
    NativeResult result;
    result.executed = true;
    result.native_entered = entered;
    return result;
}

bool restore_owned_cctv(bool* native_entered = nullptr) {
    if (native_entered) *native_entered = false;
    if (!ownership.active) return true;
    if (!profile_matches()) return false;

    bool current = false;
    if (!read_cctv(&current)) return false;
    if (current != ownership.applied) {
        // The engine or another mod changed the flag after our write. Retire
        // ownership without undoing that later state.
        ownership = {};
        return true;
    }

    const bool restored = switch_cctv(ownership.original);
    if (native_entered) *native_entered = true;
    if (!restored) return false;
    if (!read_cctv(&current)) return false;
    if (current != ownership.original) {
        // A newer writer won after our call; do not keep retrying against it.
        ownership = {};
        return true;
    }
    ownership = {};
    return true;
}
}

NativeResult sa_security_camera_set(bool enabled) {
    if (!profile_matches()) return failure("San Andreas security-camera post-effect profile is unavailable");

    bool current = false;
    if (!read_cctv(&current)) return failure("Could not read the native CCTV effect state");

    if (enabled) {
        if (ownership.active) {
            if (current == ownership.applied) return success();
        } else if (current) {
            // Another native owner already enabled CCTV. Do not claim its
            // state; the matching false command still follows source semantics.
            return success();
        } else {
            ownership.active = true;
            ownership.original = current;
            ownership.applied = true;
        }

        if (!switch_cctv(true))
            return failure("Native CCTV switch faulted; ownership retained for safe cleanup", true);

        bool after = false;
        if (!read_cctv(&after) || !after)
            return failure("Native CCTV switch did not produce the requested state", true);
        return success(true);
    }

    if (ownership.active) {
        bool entered = false;
        if (!restore_owned_cctv(&entered))
            return failure("Could not safely restore the owned CCTV effect", entered);
        return success(entered);
    }

    // A false command must clear the native script-visible effect even when
    // this module did not acquire it; the retail command writes the global flag.
    if (!current) return success();
    if (!switch_cctv(false)) return failure("Native CCTV disable faulted", true);
    bool after = true;
    if (!read_cctv(&after) || after)
        return failure("Native CCTV disable did not produce the requested state", true);
    return success(true);
}

bool sa_security_camera_cleanup() {
    return restore_owned_cctv();
}

#ifdef LCS_ISOLATED_SECURITY_CAMERA_BACKEND_TEST
void sa_security_camera_test_bind(const SecurityCameraTestCallbacks& value) {
    callbacks = value;
    ownership = {};
}
void sa_security_camera_test_reset() {
    ownership = {};
    callbacks = {};
}
#endif

}
