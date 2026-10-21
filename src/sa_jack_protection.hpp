#pragma once
#include "lcs/native.hpp"
#include "lcs/objective_receipts.hpp"
#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

#if defined(LCS_JACK_PROTECTION_BACKEND_TEST)
#define LCS_JACK_PROTECTION_NATIVE_BACKEND 0
#elif defined(_WIN32) && defined(_MSC_VER) && defined(_M_IX86)
#define LCS_JACK_PROTECTION_NATIVE_BACKEND 1
#else
#define LCS_JACK_PROTECTION_NATIVE_BACKEND 0
#endif

namespace lcs {
using JackProtectionReferenceLive=bool(*)(const ObjectiveActor&,const uintptr_t*);
using JackProtectionReferenceRelease=bool(*)(uintptr_t*);

class JackProtectionRuntime {
    struct Entry {ObjectiveActor actor;uintptr_t cell=0;bool active=false;};
    std::unordered_map<int32_t,std::unique_ptr<Entry>> entries_;
    std::vector<std::unique_ptr<Entry>> retired_;
    bool lost_cell_=false;
    void quarantine(std::unique_ptr<Entry>&);
public:
    ~JackProtectionRuntime();
    static bool valid(ObjectiveActor);
    uintptr_t* prepare(ObjectiveActor);
    bool publish(ObjectiveActor,uintptr_t*);
    bool confirmed(ObjectiveActor,JackProtectionReferenceLive) const;
    bool confirmed_ped(uintptr_t,JackProtectionReferenceLive) const;
    // Disable exactly this actor generation. A reused script handle must not
    // clear another actor's protection or release its weak reference.
    bool retire_actor(ObjectiveActor,JackProtectionReferenceRelease);
    bool release_all(JackProtectionReferenceRelease);
    size_t retained_count() const;
};

using JackProtectionPatchBytes=std::array<uint8_t,10>;
inline constexpr uintptr_t jack_protection_patch_site=0x64F57F;
inline constexpr uintptr_t jack_protection_native_test_continue=0x64F589;
inline constexpr uintptr_t jack_protection_native_protected_skip=0x64F5D3;
inline constexpr uintptr_t jack_protection_following_call=0x64F58D;
inline constexpr uintptr_t jack_protection_following_call_target=0x64F110;
inline constexpr uint32_t jack_protection_native_stay_mask=0x00800000;
inline constexpr JackProtectionPatchBytes jack_protection_original_bytes{{
    0xF7,0x86,0x70,0x04,0x00,0x00,0x00,0x00,0x80,0x00
}};
inline constexpr std::array<uint8_t,2> jack_protection_native_jne{{0x75,0x48}};
inline constexpr std::array<uint8_t,7> jack_protection_native_normal_path_bytes{{
    0x56,0x57,0xE8,0x7E,0xFB,0xFF,0xFF
}};
inline constexpr bool jack_protection_native_stay_flag(uint32_t flags) {
    return (flags&jack_protection_native_stay_mask)!=0;
}

enum class JackProtectionPatchAction {install,already_installed,restore,already_restored,refuse};
JackProtectionPatchAction plan_jack_protection_patch(const JackProtectionPatchBytes& current,
    const JackProtectionPatchBytes& replacement,bool owned,bool install);

// The lookup balances PUSHFD/PUSHAD plus one cdecl argument on both paths.
// The native path replays TEST and rejoins at the original JNE; the owned
// path branches to the native protected-ped skip with all flags/registers saved.
struct JackProtectionHookStackContract {
    static constexpr unsigned lookup_saved_bytes=36;
    static constexpr unsigned lookup_argument_bytes=4;
};

NativeResult sa_set_char_jack_protection(int32_t handle,bool value);
NativeResult sa_erase_char_jack_protection(const ObjectiveActor& actor);
bool sa_jack_protection_hook_ready();
bool sa_release_jack_protections();
}
