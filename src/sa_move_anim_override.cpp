#include "sa_move_anim_override.hpp"
#include "lcs/sa_executor.hpp"
#include <cstring>
#include <limits>
#if LCS_MOVE_ANIM_OVERRIDE_NATIVE_BACKEND
#include <windows.h>
#endif
namespace lcs {
void MoveAnimOverrideRuntime::quarantine(std::unique_ptr<Entry>& entry) {
    if(!entry)return;
    entry->active=false;
    if(!entry->cell) {entry.reset();return;}
    try {retired_.push_back(std::move(entry));}
    catch(...) {entry.release();lost_cell_=true;}
}
MoveAnimOverrideRuntime::~MoveAnimOverrideRuntime() {
    for(auto& item:entries_)if(item.second && item.second->cell)item.second.release();
    for(auto& item:retired_)if(item && item->cell)item.release();
}
bool MoveAnimOverrideRuntime::valid(ObjectiveActor actor) {
    return actor.handle>=0 && !(uint32_t(actor.handle)&0x80u) && actor.native_ped;
}
uintptr_t* MoveAnimOverrideRuntime::prepare(ObjectiveActor actor) {
    if(!valid(actor) || lost_cell_)return nullptr;
    for(const auto& old:retired_)if(old && old->cell && old->actor.handle==actor.handle)return nullptr;
    auto found=entries_.find(actor.handle);
    if(found!=entries_.end()) {
        auto& entry=found->second;
        if(entry && entry->actor==actor && entry->cell)return &entry->cell;
        quarantine(entry);entries_.erase(found);
        // A replaced identity with a still registered cell cannot be reused.
        for(const auto& old:retired_)if(old && old->cell && old->actor.handle==actor.handle)return nullptr;
    }
    try {
        auto entry=std::make_unique<Entry>();entry->actor=actor;
        auto added=entries_.emplace(actor.handle,std::move(entry));
        return added.second?&added.first->second->cell:nullptr;
    } catch(...) {return nullptr;}
}
bool MoveAnimOverrideRuntime::publish(ObjectiveActor actor,uintptr_t* cell) {
    auto found=entries_.find(actor.handle);
    if(found==entries_.end() || !found->second)return false;
    auto& entry=*found->second;
    if(!(entry.actor==actor) || cell!=&entry.cell || *cell!=actor.native_ped)return false;
    entry.active=true;return true;
}
bool MoveAnimOverrideRuntime::confirmed(ObjectiveActor actor,MoveAnimReferenceLive live) const {
    const auto found=entries_.find(actor.handle);
    if(found==entries_.end() || !found->second || !live)return false;
    const auto& entry=*found->second;
    return entry.active && entry.actor==actor && entry.cell==actor.native_ped && live(actor,&entry.cell);
}
bool MoveAnimOverrideRuntime::confirmed_ped(uintptr_t ped,MoveAnimReferenceLive live) const {
    for(const auto& pair:entries_)if(pair.second && pair.second->actor.native_ped==ped && confirmed(pair.second->actor,live))return true;
    return false;
}
bool MoveAnimOverrideRuntime::retire_handle(int32_t handle,MoveAnimReferenceRelease release) {
    bool ok=true;
    auto found=entries_.find(handle);
    if(found!=entries_.end()) {
        auto& entry=found->second;
        if(entry) {
            entry->active=false;
            if(entry->cell && (!release || !release(&entry->cell) || entry->cell)) {quarantine(entry);ok=false;}
        }
        entries_.erase(found);
    }
    for(auto it=retired_.begin();it!=retired_.end();) {
        auto& entry=*it;
        if(!entry || !entry->cell) {it=retired_.erase(it);continue;}
        if(entry->actor.handle!=handle) {++it;continue;}
        if(release && release(&entry->cell) && !entry->cell)it=retired_.erase(it);
        else {ok=false;++it;}
    }
    return ok && !lost_cell_;
}
bool MoveAnimOverrideRuntime::release_all(MoveAnimReferenceRelease release) {
    bool ok=!lost_cell_;
    for(auto& pair:entries_) {
        auto& entry=pair.second;
        if(!entry)continue;
        entry->active=false;
        if(entry->cell && (!release || !release(&entry->cell) || entry->cell)) {quarantine(entry);ok=false;}
    }
    entries_.clear();
    for(auto it=retired_.begin();it!=retired_.end();) {
        auto& entry=*it;
        if(!entry || !entry->cell || (release && release(&entry->cell) && !entry->cell))it=retired_.erase(it);
        else {ok=false;++it;}
    }
    return ok && retired_.empty();
}
size_t MoveAnimOverrideRuntime::retained_count() const {return retired_.size()+(lost_cell_?1:0);}
MoveAnimPatchAction plan_move_anim_patch(const MoveAnimPatchBytes& current,const MoveAnimPatchBytes& replacement,bool owned,bool install) {
    if(install) {
        if(current==move_anim_original_bytes)return MoveAnimPatchAction::install;
        if(owned && current==replacement)return MoveAnimPatchAction::already_installed;
    } else {
        if(current==move_anim_original_bytes)return MoveAnimPatchAction::already_restored;
        if(owned && current==replacement)return MoveAnimPatchAction::restore;
    }
    return MoveAnimPatchAction::refuse;
}
namespace {
MoveAnimOverrideRuntime runtime;
#if LCS_MOVE_ANIM_OVERRIDE_NATIVE_BACKEND
bool patch_owned=false;
MoveAnimPatchBytes replacement{};
uintptr_t native_continue=move_anim_native_continue;
uintptr_t own_group_continue=move_anim_own_group_continue;
uintptr_t native_group_query=move_anim_native_group_query;
extern "C" void move_anim_run_hook();
bool bytes_match(uintptr_t address,const uint8_t* expected,size_t size) {
    __try {return std::memcmp(reinterpret_cast<const void*>(address),expected,size)==0;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool consumer_profile() {
    constexpr uint8_t entry[]={0x56,0x8B,0xF1,0x8B,0x86,0x30,0x05,0x00,0x00};
    constexpr uint8_t own_group[]={0x8B,0xBE,0xD4,0x04,0x00,0x00};
    constexpr uint8_t own_run[]={0x68,0x00,0x00,0x80,0x3F,0x6A,0x02,0x57,0x8B,0x4E,0x18,0x51};
    constexpr uint8_t native_resume[]={0x83,0xC4,0x04,0x84,0xC0,0x74,0x3E};
    constexpr uint8_t leader_group[]={0x6A,0x36};
    return sa_executor_profile_matches() && bytes_match(0x5E4A00,entry,sizeof(entry)) &&
        bytes_match(0x5E4ACE,own_group,sizeof(own_group)) &&
        bytes_match(0x5E4C04,own_run,sizeof(own_run)) && bytes_match(0x5E4BBF,native_resume,sizeof(native_resume)) &&
        bytes_match(0x5E4BFF,leader_group,sizeof(leader_group));
}
bool owned_now() {
    return patch_owned && consumer_profile() && bytes_match(move_anim_patch_site,replacement.data(),replacement.size());
}
bool change_patch(bool install) {
    if(install && !consumer_profile())return false;
    const int64_t delta=int64_t(reinterpret_cast<uintptr_t>(&move_anim_run_hook))-int64_t(move_anim_patch_site+5);
    if(delta<INT32_MIN || delta>INT32_MAX)return false;
    replacement.fill(0x90);replacement[0]=0xE9;
    const auto relative=static_cast<int32_t>(delta);std::memcpy(replacement.data()+1,&relative,4);
    __try {
        MoveAnimPatchBytes current{};
        std::memcpy(current.data(),reinterpret_cast<const void*>(move_anim_patch_site),current.size());
        const auto action=plan_move_anim_patch(current,replacement,patch_owned,install);
        if(action==MoveAnimPatchAction::already_installed)return true;
        if(action==MoveAnimPatchAction::already_restored) {patch_owned=false;return true;}
        if(action==MoveAnimPatchAction::refuse)return false;
        auto* code=reinterpret_cast<void*>(move_anim_patch_site);
        DWORD previous=0;
        if(!VirtualProtect(code,6,PAGE_EXECUTE_READWRITE,&previous))return false;
        const auto& target=install?replacement:move_anim_original_bytes;
        std::memcpy(code,target.data(),6);FlushInstructionCache(GetCurrentProcess(),code,6);
        DWORD ignored=0;
        if(!VirtualProtect(code,6,previous,&ignored)) {
            std::memcpy(code,current.data(),6);FlushInstructionCache(GetCurrentProcess(),code,6);
            VirtualProtect(code,6,previous,&ignored);return false;
        }
        patch_owned=install;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool __cdecl move_anim_owned_lookup(uintptr_t ped) noexcept {
    if(!owned_now())return false;
    __try {
        if(!ped || ped>UINTPTR_MAX-0x59B || !MoveAnimOverrideRuntime::applies_to_native_type(
            *reinterpret_cast<const uint32_t*>(ped+0x598)))return false;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    return runtime.confirmed_ped(ped,sa_actor_reference_live);
}
extern "C" __declspec(naked) void move_anim_run_hook() {
    __asm {
        pushfd
        pushad
        push esi
        call move_anim_owned_lookup
        add esp, 4
        test al, al
        jz original_run
        popad
        popfd
        jmp dword ptr [own_group_continue]
    original_run:
        popad
        popfd
        push esi
        call dword ptr [native_group_query]
        jmp dword ptr [native_continue]
    }
}
#endif
}
bool sa_move_anim_override_hook_ready() {
#if LCS_MOVE_ANIM_OVERRIDE_NATIVE_BACKEND
    return owned_now();
#else
    return false;
#endif
}
NativeResult sa_override_char_move_anim(int32_t handle) {
    NativeResult result;
#if LCS_MOVE_ANIM_OVERRIDE_NATIVE_BACKEND
    const auto before=sa_live_objective_actor(handle);
    if(!before || !MoveAnimOverrideRuntime::valid(*before)) {result.diagnostic="Move override requires a live SA actor";return result;}
    if(!change_patch(true) || !owned_now()) {result.diagnostic="Move override consumer profile is foreign or unavailable";return result;}
    auto* cell=runtime.prepare(*before);
    if(!cell) {result.diagnostic="Move override weak cell is quarantined or unavailable";return result;}
    // From this point even failed registration may leave a native reference.
    // Publishing also changes a consumer; never replay a failure after it.
    result.native_entered=true;
    const bool registered=*cell?sa_actor_reference_live(*before,cell):sa_register_actor_reference(*before,cell);
    const auto after=sa_live_objective_actor(handle);
    if(!registered || !after || !(*after==*before) || !owned_now() ||
       !sa_actor_reference_live(*before,cell) || !runtime.publish(*before,cell)) {
        runtime.retire_handle(handle,sa_release_actor_reference);
        result.diagnostic="Move override actor/reference changed during registration";return result;
    }
    const auto final_actor=sa_live_objective_actor(handle);
    if(!final_actor || !(*final_actor==*before) || !runtime.confirmed(*before,sa_actor_reference_live)) {
        runtime.retire_handle(handle,sa_release_actor_reference);
        result.diagnostic="Move override actor changed after publish";return result;
    }
    result.executed=true;
#else
    (void)handle;result.diagnostic="Move override backend has no native adapter";
#endif
    return result;
}
NativeResult sa_clear_char_move_anim_override(int32_t handle) {
    NativeResult result;
#if LCS_MOVE_ANIM_OVERRIDE_NATIVE_BACKEND
    // Clear is valid after the actor disappeared: weak cells still have to be
    // retired, and their stable addresses must survive uncertain release.
    result.native_entered=true;
    result.executed=runtime.retire_handle(handle,sa_release_actor_reference);
    if(!result.executed)result.diagnostic="Move override cleared but native weak cell remains quarantined";
#else
    (void)handle;result.diagnostic="Move override backend has no native adapter";
#endif
    return result;
}
bool sa_release_move_anim_overrides() {
#if LCS_MOVE_ANIM_OVERRIDE_NATIVE_BACKEND
    const bool unpatched=change_patch(false);
    const bool released=runtime.release_all(sa_release_actor_reference);
    return unpatched && released;
#else
    return false;
#endif
}
}
