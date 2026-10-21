#include "sa_jack_protection.hpp"
#include "lcs/sa_executor.hpp"
#include <cstring>
#include <limits>
#if LCS_JACK_PROTECTION_NATIVE_BACKEND
#include <windows.h>
#endif

namespace lcs {
void JackProtectionRuntime::quarantine(std::unique_ptr<Entry>& entry) {
    if(!entry)return;
    entry->active=false;
    if(!entry->cell) {entry.reset();return;}
    try {retired_.push_back(std::move(entry));}
    catch(...) {entry.release();lost_cell_=true;}
}

JackProtectionRuntime::~JackProtectionRuntime() {
    // A failed native release leaves SA holding this cell's address. Keep its
    // allocation alive; the pinned native module also keeps the runtime alive.
    for(auto& item:entries_)if(item.second && item.second->cell)item.second.release();
    for(auto& item:retired_)if(item && item->cell)item.release();
}

bool JackProtectionRuntime::valid(ObjectiveActor actor) {
    return actor.handle>=0 && !(uint32_t(actor.handle)&0x80u) && actor.native_ped;
}

uintptr_t* JackProtectionRuntime::prepare(ObjectiveActor actor) {
    if(!valid(actor) || lost_cell_)return nullptr;
    for(const auto& old:retired_)if(old && old->cell && old->actor.handle==actor.handle)return nullptr;
    auto found=entries_.find(actor.handle);
    if(found!=entries_.end()) {
        auto& entry=found->second;
        if(entry && entry->actor==actor && entry->cell)return &entry->cell;
        quarantine(entry);entries_.erase(found);
        // Do not reuse a handle while SA may still retain its previous cell.
        for(const auto& old:retired_)if(old && old->cell && old->actor.handle==actor.handle)return nullptr;
    }
    try {
        auto entry=std::make_unique<Entry>();entry->actor=actor;
        auto added=entries_.emplace(actor.handle,std::move(entry));
        return added.second?&added.first->second->cell:nullptr;
    } catch(...) {return nullptr;}
}

bool JackProtectionRuntime::publish(ObjectiveActor actor,uintptr_t* cell) {
    const auto found=entries_.find(actor.handle);
    if(found==entries_.end() || !found->second)return false;
    auto& entry=*found->second;
    if(!(entry.actor==actor) || cell!=&entry.cell || *cell!=actor.native_ped)return false;
    entry.active=true;return true;
}

bool JackProtectionRuntime::confirmed(ObjectiveActor actor,JackProtectionReferenceLive live) const {
    const auto found=entries_.find(actor.handle);
    if(found==entries_.end() || !found->second || !live)return false;
    const auto& entry=*found->second;
    return entry.active && entry.actor==actor && entry.cell==actor.native_ped && live(actor,&entry.cell);
}

bool JackProtectionRuntime::confirmed_ped(uintptr_t ped,JackProtectionReferenceLive live) const {
    for(const auto& pair:entries_) {
        if(pair.second && pair.second->actor.native_ped==ped && confirmed(pair.second->actor,live))return true;
    }
    return false;
}

bool JackProtectionRuntime::retire_actor(ObjectiveActor actor,JackProtectionReferenceRelease release) {
    bool ok=true;
    const auto found=entries_.find(actor.handle);
    if(found!=entries_.end() && found->second && found->second->actor==actor) {
        auto& entry=found->second;
        entry->active=false;
        if(entry->cell && (!release || !release(&entry->cell) || entry->cell)) {
            quarantine(entry);ok=false;
        }
        entries_.erase(found);
    }
    for(auto it=retired_.begin();it!=retired_.end();) {
        auto& entry=*it;
        if(!entry || !entry->cell) {it=retired_.erase(it);continue;}
        if(!(entry->actor==actor)) {++it;continue;}
        if(release && release(&entry->cell) && !entry->cell)it=retired_.erase(it);
        else {ok=false;++it;}
    }
    return ok && !lost_cell_;
}

bool JackProtectionRuntime::release_all(JackProtectionReferenceRelease release) {
    bool ok=!lost_cell_;
    // Disable every lookup before invoking SA's reference cleanup. Cleanup can
    // run engine code, so no other actor may remain protected during it.
    for(auto& pair:entries_)if(pair.second)pair.second->active=false;
    for(auto& pair:entries_) {
        auto& entry=pair.second;
        if(!entry)continue;
        if(entry->cell && (!release || !release(&entry->cell) || entry->cell)) {
            quarantine(entry);ok=false;
        }
    }
    entries_.clear();
    for(auto it=retired_.begin();it!=retired_.end();) {
        auto& entry=*it;
        if(!entry || !entry->cell || (release && release(&entry->cell) && !entry->cell))it=retired_.erase(it);
        else {ok=false;++it;}
    }
    return ok && retired_.empty();
}

size_t JackProtectionRuntime::retained_count() const {return retired_.size()+(lost_cell_?1:0);}

JackProtectionPatchAction plan_jack_protection_patch(const JackProtectionPatchBytes& current,
    const JackProtectionPatchBytes& replacement,bool owned,bool install) {
    if(install) {
        if(current==jack_protection_original_bytes)return JackProtectionPatchAction::install;
        if(owned && current==replacement)return JackProtectionPatchAction::already_installed;
    } else {
        if(current==jack_protection_original_bytes)return JackProtectionPatchAction::already_restored;
        if(owned && current==replacement)return JackProtectionPatchAction::restore;
    }
    return JackProtectionPatchAction::refuse;
}

namespace {
JackProtectionRuntime runtime;
#if LCS_JACK_PROTECTION_NATIVE_BACKEND
bool patch_owned=false;
bool module_pinned=false;
JackProtectionPatchBytes replacement{};
uintptr_t protected_skip=jack_protection_native_protected_skip;
uintptr_t native_test_continue=jack_protection_native_test_continue;
extern "C" void jack_protection_run_hook();

bool bytes_match(uintptr_t address,const uint8_t* expected,size_t size) {
    __try {return std::memcmp(reinterpret_cast<const void*>(address),expected,size)==0;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool consumer_profile() {
    return sa_executor_profile_matches() &&
        bytes_match(jack_protection_native_test_continue,jack_protection_native_jne.data(),jack_protection_native_jne.size()) &&
        bytes_match(jack_protection_native_test_continue+2,jack_protection_native_normal_path_bytes.data(),
            jack_protection_native_normal_path_bytes.size());
}

bool owned_now() {
    return patch_owned && consumer_profile() &&
        bytes_match(jack_protection_patch_site,replacement.data(),replacement.size());
}

bool pin_module() {
    if(module_pinned)return true;
    HMODULE module=nullptr;
    const DWORD flags=GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN;
    if(!GetModuleHandleExW(flags,reinterpret_cast<LPCWSTR>(&jack_protection_run_hook),&module))return false;
    module_pinned=true;return true;
}

bool change_patch(bool install) {
    if(install && !consumer_profile())return false;
    const int64_t delta=int64_t(reinterpret_cast<uintptr_t>(&jack_protection_run_hook))-
        int64_t(jack_protection_patch_site+5);
    if(delta<INT32_MIN || delta>INT32_MAX)return false;
    replacement.fill(0x90);replacement[0]=0xE9;
    const auto relative=static_cast<int32_t>(delta);std::memcpy(replacement.data()+1,&relative,4);
    if(install && !pin_module())return false;
    __try {
        JackProtectionPatchBytes current{};
        std::memcpy(current.data(),reinterpret_cast<const void*>(jack_protection_patch_site),current.size());
        const auto action=plan_jack_protection_patch(current,replacement,patch_owned,install);
        if(action==JackProtectionPatchAction::already_installed)return true;
        if(action==JackProtectionPatchAction::already_restored) {patch_owned=false;return true;}
        if(action==JackProtectionPatchAction::refuse)return false;
        auto* code=reinterpret_cast<void*>(jack_protection_patch_site);
        DWORD previous=0;
        if(!VirtualProtect(code,10,PAGE_EXECUTE_READWRITE,&previous))return false;
        const auto& target=install?replacement:jack_protection_original_bytes;
        std::memcpy(code,target.data(),target.size());FlushInstructionCache(GetCurrentProcess(),code,target.size());
        DWORD ignored=0;
        if(!VirtualProtect(code,10,previous,&ignored)) {
            std::memcpy(code,current.data(),current.size());FlushInstructionCache(GetCurrentProcess(),code,current.size());
            VirtualProtect(code,10,previous,&ignored);return false;
        }
        patch_owned=install;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool __cdecl jack_protection_owned_lookup(uintptr_t ped) noexcept {
    if(!owned_now())return false;
    return runtime.confirmed_ped(ped,sa_actor_reference_live);
}

extern "C" __declspec(naked) void jack_protection_run_hook() {
    __asm {
        pushfd
        pushad
        push esi
        call jack_protection_owned_lookup
        add esp, 4
        test al, al
        jz native_test
        popad
        popfd
        jmp dword ptr [protected_skip]
    native_test:
        popad
        popfd
        test dword ptr [esi+470h], 800000h
        jmp dword ptr [native_test_continue]
    }
}
#endif
}

bool sa_jack_protection_hook_ready() {
#if LCS_JACK_PROTECTION_NATIVE_BACKEND
    return owned_now();
#else
    return false;
#endif
}

NativeResult sa_erase_char_jack_protection(const ObjectiveActor& actor) {
    NativeResult result;
#if LCS_JACK_PROTECTION_NATIVE_BACKEND
    if(!JackProtectionRuntime::valid(actor)) {
        result.diagnostic="Jack protection clear requires a valid SA actor identity";return result;
    }
    result.native_entered=true;
    result.executed=runtime.retire_actor(actor,sa_release_actor_reference);
    if(!result.executed)result.diagnostic="Jack protection cleared locally but its native weak cell remains quarantined";
#else
    (void)actor;result.diagnostic="Jack protection backend has no native adapter";
#endif
    return result;
}

NativeResult sa_set_char_jack_protection(int32_t handle,bool value) {
    NativeResult result;
#if LCS_JACK_PROTECTION_NATIVE_BACKEND
    const auto before=sa_live_objective_actor(handle);
    if(!before || !JackProtectionRuntime::valid(*before)) {
        result.diagnostic="Jack protection requires a live SA actor";return result;
    }
    if(!value)return sa_erase_char_jack_protection(*before);
    if(!change_patch(true) || !owned_now()) {
        result.diagnostic="Jack protection consumer profile is foreign or unavailable";return result;
    }
    auto* cell=runtime.prepare(*before);
    if(!cell) {result.diagnostic="Jack protection weak cell is quarantined or unavailable";return result;}
    if(*cell && runtime.confirmed(*before,sa_actor_reference_live)) {
        result.executed=true;return result;
    }
    // From here a failed native registration may still leave the stable cell
    // linked into SA's resolver; the caller must not replay the source command.
    result.native_entered=true;
    const bool registered=*cell?sa_actor_reference_live(*before,cell):sa_register_actor_reference(*before,cell);
    const auto after=sa_live_objective_actor(handle);
    if(!registered || !after || !(*after==*before) || !owned_now() ||
       !sa_actor_reference_live(*before,cell) || !runtime.publish(*before,cell)) {
        runtime.retire_actor(*before,sa_release_actor_reference);
        result.diagnostic="Jack protection actor/reference changed during registration";return result;
    }
    const auto final_actor=sa_live_objective_actor(handle);
    if(!final_actor || !(*final_actor==*before) || !runtime.confirmed(*before,sa_actor_reference_live)) {
        runtime.retire_actor(*before,sa_release_actor_reference);
        result.diagnostic="Jack protection actor changed after publish";return result;
    }
    result.executed=true;
#else
    (void)handle;(void)value;result.diagnostic="Jack protection backend has no native adapter";
#endif
    return result;
}

bool sa_release_jack_protections() {
#if LCS_JACK_PROTECTION_NATIVE_BACKEND
    // Disable all lookups first. Keep the owned hook and runtime resident while
    // any native weak cell still needs a later release retry.
    const bool released=runtime.release_all(sa_release_actor_reference);
    if(!released)return false;
    return change_patch(false);
#else
    return false;
#endif
}
}
