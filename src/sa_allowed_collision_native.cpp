#include "sa_allowed_collision.hpp"
#include "lcs/sa_executor.hpp"
#include <cstring>
#include <limits>
#if defined(_WIN32) && defined(_MSC_VER) && defined(_M_IX86)
#include <windows.h>
#endif
namespace lcs {
namespace {
AllowedCollisionRuntime runtime;
#if defined(_WIN32) && defined(_MSC_VER) && defined(_M_IX86)
constexpr uintptr_t patch_site=0x41093F;
constexpr uint8_t original[6]={0x8B,0x15,0x94,0x44,0xB7,0x00};
uint8_t replacement[6]{};
bool owned=false,faulted=false;
uintptr_t native_continue=0x410945;
extern "C" void allowed_collision_load_hook();
bool bytes_match(uintptr_t address,const uint8_t* expected,size_t size) {
    __try {return std::memcmp(reinterpret_cast<const void*>(address),expected,size)==0;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool profile() {
    // The native player and foreign second-position requests run before this
    // site; mission cleanup and the normal request/remove loop run after it.
    constexpr uint8_t first[]={0x50,0xE8,0xCE,0x20,0x14,0x00}; // ForAllMatching5529F0
    constexpr uint8_t second[]={0x68,0x80,0x55,0x96,0x00,0xE8,0xA4,0xFB,0xFF,0xFF};
    constexpr uint8_t next[]={0x55,0x56,0x57,0x8B,0x3D,0x90,0x44,0xB7,0x00};
    constexpr uint8_t mark[]={0xA1,0x54,0x55,0x96,0x00,0x85,0xC0,0x8B,0x4C,0x24,0x08};
    constexpr uint8_t tree[]={0x53,0x8B,0x5C,0x24,0x08,0x55,0x8B,0x6C,0x24,0x10,0x56,0x57};
    return sa_executor_profile_matches() && bytes_match(0x4103D0,mark,sizeof(mark)) &&
        bytes_match(0x5529F0,tree,sizeof(tree)) && bytes_match(0x41091C,first,sizeof(first)) &&
        bytes_match(0x410932,second,sizeof(second)) && bytes_match(0x410945,next,sizeof(next));
}
bool owned_now() {return owned && profile() && bytes_match(patch_site,replacement,sizeof(replacement));}
bool pin_module() {
    // A later mod may chain this callback and own the final patch bytes.
    // Keep code/data alive even when ownership prevents unpatching on teardown.
    HMODULE module=nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&allowed_collision_load_hook),&module)!=FALSE;
}
bool patch(bool install) {
    if(install && !profile())return false;
    const int64_t delta=int64_t(reinterpret_cast<uintptr_t>(&allowed_collision_load_hook))-int64_t(patch_site+5);
    if(delta<INT32_MIN || delta>INT32_MAX)return false;
    replacement[0]=0xE9;const auto relative=static_cast<int32_t>(delta);
    std::memcpy(replacement+1,&relative,4);replacement[5]=0x90;
    __try {
        auto* code=reinterpret_cast<void*>(patch_site);
        if(install && owned_now())return true;
        if(bytes_match(patch_site,original,sizeof(original))) {
            if(!install) {owned=false;return true;}
        } else if(!owned || !bytes_match(patch_site,replacement,sizeof(replacement)))return false;
        DWORD old=0;
        if(!VirtualProtect(code,6,PAGE_EXECUTE_READWRITE,&old))return false;
        uint8_t previous[6];std::memcpy(previous,code,6);
        std::memcpy(code,install?replacement:original,6);FlushInstructionCache(GetCurrentProcess(),code,6);
        DWORD ignored=0;
        if(!VirtualProtect(code,6,old,&ignored)) {
            std::memcpy(code,previous,6);FlushInstructionCache(GetCurrentProcess(),code,6);
            VirtualProtect(code,6,old,&ignored);return false;
        }
        owned=install;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool mark_extra_position(const float* position) {
    // Match the native call at41091C exactly, using its normal-world callback.
    // Do not overwrite SA's separate ms_bCollisionNeeded request or remove COLs.
    __try {
        const auto tree=*reinterpret_cast<const uintptr_t*>(0x96555C);
        if(!tree)return false;
        auto* area=reinterpret_cast<int32_t*>(0x965554);const auto previous=*area;
        __try {
            *area=0;
            using Mark=void(__thiscall*)(uintptr_t,const float*,uintptr_t);
            reinterpret_cast<Mark>(0x5529F0)(tree,position,0x4103D0);
        } __finally {*area=previous;}
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
void __cdecl allowed_collision_before_load() noexcept {
    if(faulted || !owned_now())return;
    const auto position=runtime.position();
    if(position && !mark_extra_position(position->data())) {
        // A native fault cannot be replayed by the SCM or leak a selected
        // island into a later campaign. The next setter also refuses.
        runtime.clear();faulted=true;
    }
}
extern "C" __declspec(naked) void allowed_collision_load_hook() {
    __asm {
        pushfd
        pushad
        call allowed_collision_before_load
        popad
        popfd
        mov edx, dword ptr ds:[0xB74494]
        jmp dword ptr [native_continue]
    }
}
#endif
}
NativeResult sa_set_allowed_collision(int32_t first,int32_t second) {
    NativeResult result;
    AllowedCollisionRuntime proposed=runtime;
    if(!proposed.set(first,second)) {result.diagnostic="Invalid selected original collision level";return result;}
#if defined(_WIN32) && defined(_MSC_VER) && defined(_M_IX86)
    if(faulted || !pin_module() || !patch(true) || !owned_now()) {
        result.diagnostic="Native collision residency consumer is foreign or unavailable";return result;
    }
    runtime=proposed;
    result.executed=true;
#else
    result.diagnostic="Native collision residency requires the Win32 SA backend";
#endif
    return result;
}
bool sa_allowed_collision_healthy() {
#if defined(_WIN32) && defined(_MSC_VER) && defined(_M_IX86)
    return !faulted && (!runtime.position() || owned_now());
#else
    return !runtime.position();
#endif
}
bool sa_release_allowed_collision() {
    runtime.clear();
#if defined(_WIN32) && defined(_MSC_VER) && defined(_M_IX86)
    // Clear the policy even if a foreign hook prevents restoring our bytes.
    // Any chained callback then has no additional island to request.
    const bool restored=patch(false);
    if(restored)faulted=false;
    return restored;
#else
    return true;
#endif
}
}
