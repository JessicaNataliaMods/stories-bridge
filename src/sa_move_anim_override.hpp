#pragma once
#include "lcs/native.hpp"
#include "lcs/objective_receipts.hpp"
#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

#if defined(LCS_MOVE_ANIM_OVERRIDE_BACKEND_TEST)
#define LCS_MOVE_ANIM_OVERRIDE_NATIVE_BACKEND 0
#elif defined(_WIN32) && defined(_MSC_VER) && defined(_M_IX86)
#define LCS_MOVE_ANIM_OVERRIDE_NATIVE_BACKEND 1
#else
#define LCS_MOVE_ANIM_OVERRIDE_NATIVE_BACKEND 0
#endif

namespace lcs {
using MoveAnimReferenceLive=bool(*)(const ObjectiveActor&,const uintptr_t*);
using MoveAnimReferenceRelease=bool(*)(uintptr_t*);
class MoveAnimOverrideRuntime {
    struct Entry {ObjectiveActor actor; uintptr_t cell=0; bool active=false;};
    std::unordered_map<int32_t,std::unique_ptr<Entry>> entries_;
    std::vector<std::unique_ptr<Entry>> retired_;
    bool lost_cell_=false;
    void quarantine(std::unique_ptr<Entry>&);
public:
    ~MoveAnimOverrideRuntime();
    static bool valid(ObjectiveActor);
    // Original PlayerPed::SetMoveAnim is empty. Preserve SA player movement
    // even when SA reaches the CPed base updater through a different path.
    static bool applies_to_native_type(uint32_t type) {return type>1;}
    uintptr_t* prepare(ObjectiveActor);
    bool publish(ObjectiveActor,uintptr_t*);
    bool confirmed(ObjectiveActor,MoveAnimReferenceLive) const;
    bool confirmed_ped(uintptr_t,MoveAnimReferenceLive) const;
    bool retire_handle(int32_t,MoveAnimReferenceRelease);
    bool release_all(MoveAnimReferenceRelease);
    size_t retained_count() const;
};
using MoveAnimPatchBytes=std::array<uint8_t,6>;
inline constexpr uintptr_t move_anim_patch_site=0x5E4BB9;
inline constexpr uintptr_t move_anim_native_continue=0x5E4BBF;
inline constexpr uintptr_t move_anim_own_group_continue=0x5E4C04;
inline constexpr uintptr_t move_anim_native_group_query=0x5F7F10;
inline constexpr MoveAnimPatchBytes move_anim_original_bytes{{0x56,0xE8,0x51,0x33,0x01,0x00}};
enum class MoveAnimPatchAction {install,already_installed,restore,already_restored,refuse};
MoveAnimPatchAction plan_move_anim_patch(const MoveAnimPatchBytes&,const MoveAnimPatchBytes&,bool owned,bool install);
// The lookup balances PUSHFD/PUSHAD plus one cdecl argument. Both exits restore
// all registers and flags; the native exit alone replays PUSH ESI/CALL, leaving
// its 4-byte argument for the original ADD ESP,4 at 5E4BBF.
struct MoveAnimHookStackContract {
    static constexpr unsigned lookup_saved_bytes=36;
    static constexpr unsigned lookup_argument_bytes=4;
    static constexpr unsigned native_pending_argument_bytes=4;
    static constexpr unsigned own_group_pending_argument_bytes=0;
};
NativeResult sa_override_char_move_anim(int32_t);
NativeResult sa_clear_char_move_anim_override(int32_t);
bool sa_move_anim_override_hook_ready();
bool sa_release_move_anim_overrides();
}
