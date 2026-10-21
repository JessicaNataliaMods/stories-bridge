#include "sa_reward_vehicle.hpp"
#include "lcs/route_vehicle_native.hpp"
#include "lcs/sa_executor.hpp"
#include <algorithm>
#include <cstring>
#include <iterator>
#include <limits>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace lcs {
namespace {
constexpr uintptr_t kVehiclePoolGlobal=0xB74494;
constexpr size_t kVehicleStride=0xA18;
constexpr size_t kVehicleCreatedByOffset=0x4A4;
constexpr size_t kVehicleModelOffset=0x22;
constexpr int32_t kMissionVehicle=2;
constexpr size_t kPatchSiteCount=7;

constexpr uintptr_t kRewardBranch=0x44999C;
constexpr uintptr_t kRewardBranchContinue=0x4499A2;
constexpr uintptr_t kRewardBranchSkip=0x449A03;
constexpr uintptr_t kNativeStoreCar=0x449760;
constexpr uintptr_t kNativeRestoreCar=0x447E40;
constexpr uintptr_t kRewardDeleteCall=0x449A01;
constexpr uintptr_t kRewardDeleteContinue=0x449A07;

RewardVehicleRuntime runtime;
bool callback_fault=false;
uintptr_t reward_branch_continue=kRewardBranchContinue;
uintptr_t reward_branch_skip=kRewardBranchSkip;
uintptr_t reward_native_store_car=kNativeStoreCar;
uintptr_t reward_native_restore_car=kNativeRestoreCar;
uintptr_t reward_delete_continue=kRewardDeleteContinue;

struct PatchSite {
    uintptr_t address=0;
    uint8_t length=0;
    RewardPatchBytes original{};
    RewardPatchBytes replacement{};
    bool owned=false;
};
std::array<PatchSite,kPatchSiteCount> patch_sites{};
bool patch_specs_ready=false;

#if LCS_REWARD_VEHICLE_NATIVE_BACKEND
bool resolve_live_identity(uintptr_t entity,RewardVehicleIdentity* result);
bool all_patches_owned();
extern "C" void reward_branch_hook();
extern "C" void reward_store_call_hook();
extern "C" void reward_restore_call_hook();
extern "C" void reward_delete_call_hook();
#endif

#if LCS_REWARD_VEHICLE_NATIVE_BACKEND
bool same_native_identity(const RouteVehicleSnapshot& snapshot,const RewardVehicleIdentity& identity) {
    return snapshot.handle==identity.handle && snapshot.entity==identity.entity &&
        snapshot.generation==identity.generation;
}

bool native_reference_live(const RewardVehicleIdentity& identity,const uintptr_t* cell) {
    if(!cell || !*cell)return false;
    const auto snapshot=sa_route_vehicle_snapshot(identity.handle);
    return snapshot && same_native_identity(*snapshot,identity) &&
        sa_vehicle_reference_live(*snapshot,cell);
}

bool native_reference_release(const RewardVehicleIdentity& identity,uintptr_t* cell) {
    if(!cell)return false;
    if(!*cell)return true;
    const auto snapshot=sa_route_vehicle_snapshot(identity.handle);
    if(!snapshot || !same_native_identity(*snapshot,identity))return false;
    return sa_release_vehicle_reference(*snapshot,cell);
}
#endif

bool read_marker_record(uintptr_t record,uint16_t* model) {
#if LCS_REWARD_VEHICLE_NATIVE_BACKEND
    if(!record || record>UINTPTR_MAX-reward_stored_car_model_offset-1)return false;
    __try {
        const auto marker=*reinterpret_cast<const uint8_t*>(record+reward_stored_car_marker_offset);
        const auto native_model=*reinterpret_cast<const uint16_t*>(record+reward_stored_car_model_offset);
        if(marker!=reward_stored_car_marker || native_model==0)return false;
        if(model)*model=native_model;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
#else
    (void)record;(void)model;
    return false;
#endif
}

bool clear_associated_record(const RewardStoredRecord& expected) {
#if LCS_REWARD_VEHICLE_NATIVE_BACKEND
    if(!expected.address || expected.address>UINTPTR_MAX-reward_stored_car_model_offset-1)return false;
    __try {
        auto* record=reinterpret_cast<uint8_t*>(expected.address);
        const auto marker=record[reward_stored_car_marker_offset];
        const auto model=*reinterpret_cast<const uint16_t*>(expected.address+reward_stored_car_model_offset);
        if(marker==0)return true;
        return clear_reward_stored_car_marker(record,expected.model) && model==expected.model;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
#else
    (void)expected;
    return false;
#endif
}

bool mark_associated_record(uintptr_t record,uintptr_t vehicle) {
#if LCS_REWARD_VEHICLE_NATIVE_BACKEND
    RewardVehicleIdentity identity{};
    if(!resolve_live_identity(vehicle,&identity) || !runtime.confirmed(identity,native_reference_live))return true;
    __try {
        if(vehicle>UINTPTR_MAX-kVehicleModelOffset-1 ||
           record>UINTPTR_MAX-reward_stored_car_model_offset-1)return false;
        const auto model=*reinterpret_cast<const uint16_t*>(vehicle+kVehicleModelOffset);
        auto* slot=reinterpret_cast<uint8_t*>(record);
        const auto action=mark_reward_stored_car(slot,model,true);
        if(action!=RewardStoredRecordAction::marked && action!=RewardStoredRecordAction::already_marked)return false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
#else
    (void)record;(void)vehicle;
    return false;
#endif
}

bool remove_owned_mission_cleanup_before_delete(uintptr_t vehicle) {
#if LCS_REWARD_VEHICLE_NATIVE_BACKEND
    RewardVehicleIdentity identity{};
    if(!resolve_live_identity(vehicle,&identity))return true;
    __try {
        const auto created_by=*reinterpret_cast<const uint8_t*>(vehicle+kVehicleCreatedByOffset);
        if(!RewardVehicleRuntime::should_remove_mission_cleanup(created_by,
            runtime.confirmed(identity,native_reference_live)))return true;
        // Same thiscall and exact-handle cleanup used by the checked native
        // remote-release path. The profile is pinned before any patch installs.
        reinterpret_cast<void(__thiscall*)(uintptr_t,int32_t,int32_t)>(0x4654B0)(0xA90850,identity.handle,1);
        if(!runtime.retire(identity,native_reference_release))callback_fault=true;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        callback_fault=true;
        return false;
    }
#else
    (void)vehicle;
    return false;
#endif
}

bool reward_restore_after_native(uintptr_t record,uintptr_t vehicle,uint32_t captured_model) {
#if LCS_REWARD_VEHICLE_NATIVE_BACKEND
    if(!captured_model || !vehicle)return true;
    uint16_t current_model=0;
    if(!read_marker_record(record,&current_model) || current_model!=captured_model) {
        callback_fault=true;
        return false;
    }
    RewardVehicleIdentity identity{};
    if(!resolve_live_identity(vehicle,&identity)) {
        callback_fault=true;
        return false;
    }
    __try {
        if(vehicle>UINTPTR_MAX-kVehicleModelOffset-1 ||
           *reinterpret_cast<const uint16_t*>(vehicle+kVehicleModelOffset)!=captured_model) {
            callback_fault=true;
            return false;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        callback_fault=true;
        return false;
    }
    auto* cell=runtime.prepare(identity);
    if(!cell) {callback_fault=true;return false;}
    bool registered=*cell ? native_reference_live(identity,cell) :
        sa_register_vehicle_reference(identity.handle,cell);
    if(!registered || !native_reference_live(identity,cell) || !runtime.publish(identity,cell)) {
        runtime.retire(identity,native_reference_release);
        callback_fault=true;
        return false;
    }
    // Both native callers consume the slot after successful restoration. Do
    // not retain its address: a later car of the same model may reuse it.
    if(!clear_associated_record({record,static_cast<uint16_t>(captured_model)})) {
        callback_fault=true;
        return false;
    }
    return true;
#else
    (void)record;(void)vehicle;(void)captured_model;
    return false;
#endif
}

uint32_t capture_reward_restore_record(uintptr_t record) {
#if LCS_REWARD_VEHICLE_NATIVE_BACKEND
    uint16_t model=0;
    return read_marker_record(record,&model)?model:0;
#else
    (void)record;
    return 0;
#endif
}

bool make_relative_branch(uintptr_t site,uintptr_t destination,uint8_t opcode,
    RewardPatchBytes* output,size_t length) {
    if(!output || length<5)return false;
    const int64_t delta=static_cast<int64_t>(destination)-static_cast<int64_t>(site+5);
    if(delta<std::numeric_limits<int32_t>::min() || delta>std::numeric_limits<int32_t>::max())return false;
    output->fill(0x90);
    (*output)[0]=opcode;
    const int32_t relative=static_cast<int32_t>(delta);
    std::memcpy(output->data()+1,&relative,sizeof(relative));
    return true;
}

void set_call_original(RewardPatchBytes* bytes,uint8_t b1,uint8_t b2,uint8_t b3,uint8_t b4) {
    bytes->fill(0);
    (*bytes)[0]=0xE8;(*bytes)[1]=b1;(*bytes)[2]=b2;(*bytes)[3]=b3;(*bytes)[4]=b4;
}

bool initialize_patch_specs() {
    if(patch_specs_ready)return true;
    auto& branch=patch_sites[0];
    branch.address=kRewardBranch;branch.length=6;
    branch.original={{0x74,0x65,0x8B,0x44,0x24,0x24}};

    const uintptr_t store_sites[]={0x4499C8,0x449B18,0x44A4BB};
    const uint8_t store_rel[][4]={{0x93,0xFD,0xFF,0xFF},{0x43,0xFC,0xFF,0xFF},{0xA0,0xF2,0xFF,0xFF}};
    for(size_t i=0;i<3;++i) {
        auto& patch=patch_sites[1+i];patch.address=store_sites[i];patch.length=5;
        set_call_original(&patch.original,store_rel[i][0],store_rel[i][1],store_rel[i][2],store_rel[i][3]);
    }
    const uintptr_t restore_sites[]={0x15614AB,0x156D8EA};
    const uint8_t restore_rel[][4]={{0x90,0x69,0xEE,0xFE},{0x51,0xA5,0xED,0xFE}};
    for(size_t i=0;i<2;++i) {
        auto& patch=patch_sites[4+i];patch.address=restore_sites[i];patch.length=5;
        set_call_original(&patch.original,restore_rel[i][0],restore_rel[i][1],restore_rel[i][2],restore_rel[i][3]);
    }
    auto& deletion=patch_sites[6];
    deletion.address=kRewardDeleteCall;deletion.length=6;
    deletion.original={{0xFF,0x12,0x8B,0x44,0x24,0x18}};
#if LCS_REWARD_VEHICLE_NATIVE_BACKEND
    if(!make_relative_branch(branch.address,reinterpret_cast<uintptr_t>(&reward_branch_hook),0xE9,
        &branch.replacement,branch.length))return false;
    for(size_t i=1;i<=3;++i)
        if(!make_relative_branch(patch_sites[i].address,reinterpret_cast<uintptr_t>(&reward_store_call_hook),
            0xE8,&patch_sites[i].replacement,patch_sites[i].length))return false;
    for(size_t i=4;i<6;++i)
        if(!make_relative_branch(patch_sites[i].address,reinterpret_cast<uintptr_t>(&reward_restore_call_hook),
            0xE8,&patch_sites[i].replacement,patch_sites[i].length))return false;
    if(!make_relative_branch(deletion.address,reinterpret_cast<uintptr_t>(&reward_delete_call_hook),
        0xE9,&deletion.replacement,deletion.length))return false;
#endif
    patch_specs_ready=true;
    return true;
}

bool memory_matches(uintptr_t address,const uint8_t* bytes,size_t length) {
#if LCS_REWARD_VEHICLE_NATIVE_BACKEND
    if(!address || !bytes || !length)return false;
    __try {return std::memcmp(reinterpret_cast<const void*>(address),bytes,length)==0;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
#else
    (void)address;(void)bytes;(void)length;
    return false;
#endif
}

bool reward_storage_profile_matches() {
#if LCS_REWARD_VEHICLE_NATIVE_BACKEND
    if(!sa_executor_profile_matches())return false;
    constexpr uint8_t store_clears_word[]={0x66,0xC7,0x47,0x10,0x00,0x00};
    constexpr uint8_t store_native_flag[]={0x66,0xC7,0x47,0x10,0x01,0x00};
    constexpr uint8_t restore_flag_01[]={0xF6,0x47,0x10,0x01};
    constexpr uint8_t restore_flag_02[]={0xF6,0x47,0x10,0x02};
    constexpr uint8_t restore_flag_04[]={0xF6,0x47,0x10,0x04};
    constexpr uint8_t restore_flag_08[]={0xF6,0x47,0x10,0x08};
    constexpr uint8_t restore_flag_10[]={0xF6,0x47,0x10,0x10};
    constexpr uint8_t restore_flag_20[]={0xF6,0x47,0x10,0x20};
    constexpr uint8_t restore_flag_40[]={0xF6,0x47,0x10,0x40};
    constexpr uint8_t restore_flag_sign[]={0x8A,0x4F,0x10};
    constexpr uint8_t save_loop[]={0xBB,0xD8,0xAB,0x96,0x00,0x8B,0xF3,0xBF,0x14,0x00,0x00,0x00,0x6A,0x40,0x56};
    constexpr uint8_t load_loop[]={0xBB,0xD8,0xAB,0x96,0x00,0x8B,0xF3,0xBF,0x14,0x00,0x00,0x00,0x6A,0x40,0x56};
    constexpr uint8_t save_helper[]={0xA0,0xFC,0x6E,0xC1,0x00,0x84,0xC0,0x74,0x03,0x32,0xC0,0xC3};
    constexpr uint8_t load_helper[]={0xA0,0xFC,0x6E,0xC1,0x00,0x84,0xC0,0x74,0x03,0x32,0xC0,0xC3};
    constexpr uint8_t rep_movsd[]={0xF3,0xA5};
    constexpr uint8_t rep_movsb[]={0xF3,0xA4};
    constexpr uint8_t cleanup_jump[]={0xE9,0x5B,0x93,0x10,0x01};
    constexpr uint8_t cleanup_body[]={0x51,0x55,0x56,0x8B,0xE9,0x57,0x8D,0x7D,0x04,
        0xC7,0x44,0x24,0x0C,0x4B,0x00,0x00,0x00};
    return memory_matches(0x449832,store_clears_word,sizeof(store_clears_word)) &&
        memory_matches(0x449841,store_native_flag,sizeof(store_native_flag)) &&
        memory_matches(0x4481EF,restore_flag_01,sizeof(restore_flag_01)) &&
        memory_matches(0x4481FC,restore_flag_02,sizeof(restore_flag_02)) &&
        memory_matches(0x44820A,restore_flag_04,sizeof(restore_flag_04)) &&
        memory_matches(0x448217,restore_flag_08,sizeof(restore_flag_08)) &&
        memory_matches(0x448224,restore_flag_10,sizeof(restore_flag_10)) &&
        memory_matches(0x448231,restore_flag_20,sizeof(restore_flag_20)) &&
        memory_matches(0x448245,restore_flag_40,sizeof(restore_flag_40)) &&
        memory_matches(0x448255,restore_flag_sign,sizeof(restore_flag_sign)) &&
        memory_matches(0x5D31EC,save_loop,sizeof(save_loop)) &&
        memory_matches(0x5D32FC,load_loop,sizeof(load_loop)) &&
        memory_matches(0x5D1270,save_helper,sizeof(save_helper)) &&
        memory_matches(0x5D1300,load_helper,sizeof(load_helper)) &&
        memory_matches(0x5D12DD,rep_movsd,sizeof(rep_movsd)) &&
        memory_matches(0x5D12E4,rep_movsb,sizeof(rep_movsb)) &&
        memory_matches(0x5D136A,rep_movsd,sizeof(rep_movsd)) &&
        memory_matches(0x5D1371,rep_movsb,sizeof(rep_movsb)) &&
        memory_matches(0x4654B0,cleanup_jump,sizeof(cleanup_jump)) &&
        memory_matches(0x156E810,cleanup_body,sizeof(cleanup_body));
#else
    return false;
#endif
}

bool apply_patch(PatchSite& site,bool install) {
#if LCS_REWARD_VEHICLE_NATIVE_BACKEND
    if((install && !reward_storage_profile_matches()) || !site.address ||
       site.length<5 || site.length>site.original.size())return false;
    __try {
        RewardPatchBytes current{};
        std::memcpy(current.data(),reinterpret_cast<const void*>(site.address),site.length);
        const auto action=plan_reward_patch(current,site.original,site.replacement,site.length,site.owned,install);
        if(action==RewardPatchAction::already_installed) {site.owned=true;return true;}
        if(action==RewardPatchAction::already_restored) {site.owned=false;return true;}
        if(action==RewardPatchAction::foreign_or_unowned)return false;
        const auto& desired=install?site.replacement:site.original;
        DWORD previous=0;
        auto* code=reinterpret_cast<uint8_t*>(site.address);
        if(!VirtualProtect(code,site.length,PAGE_EXECUTE_READWRITE,&previous))return false;
        std::memcpy(code,desired.data(),site.length);
        FlushInstructionCache(GetCurrentProcess(),code,site.length);
        DWORD ignored=0;
        if(!VirtualProtect(code,site.length,previous,&ignored)) {
            const auto& rollback=install?site.original:site.replacement;
            std::memcpy(code,rollback.data(),site.length);
            FlushInstructionCache(GetCurrentProcess(),code,site.length);
            VirtualProtect(code,site.length,previous,&ignored);
            return false;
        }
        site.owned=install;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
#else
    (void)site;(void)install;
    return false;
#endif
}

bool all_patches_owned() {
    if(!initialize_patch_specs() || !reward_storage_profile_matches())return false;
    for(auto& site:patch_sites) {
        if(!site.owned || !memory_matches(site.address,site.replacement.data(),site.length))return false;
    }
    return true;
}

bool install_patches() {
    if(!initialize_patch_specs() || !reward_storage_profile_matches())return false;
    if(all_patches_owned())return true;
    constexpr size_t order[]={1,2,3,4,5,6,0};
    size_t installed=0;
    for(;installed<std::size(order);++installed) {
        if(!apply_patch(patch_sites[order[installed]],true))break;
    }
    if(installed==std::size(order))return all_patches_owned();
    while(installed>0) {
        --installed;
        apply_patch(patch_sites[order[installed]],false);
    }
    return false;
}

bool uninstall_patches() {
    if(!initialize_patch_specs())return false;
    constexpr size_t order[]={0,6,5,4,3,2,1};
    bool ok=true;
    for(const auto index:order)if(!apply_patch(patch_sites[index],false))ok=false;
    return ok;
}

bool patches_owned_without_fault() {return all_patches_owned() && !callback_fault;}

#if LCS_REWARD_VEHICLE_NATIVE_BACKEND
bool resolve_live_identity(uintptr_t entity,RewardVehicleIdentity* result) {
    if(!entity || !result)return false;
    __try {
        const auto pool=*reinterpret_cast<const uintptr_t*>(kVehiclePoolGlobal);
        if(!pool || pool>UINTPTR_MAX-12)return false;
        const auto objects=*reinterpret_cast<const uintptr_t*>(pool);
        const auto flags=*reinterpret_cast<const uintptr_t*>(pool+4);
        const auto count=*reinterpret_cast<const int32_t*>(pool+8);
        if(!objects || !flags || count<=0 || count>4096 || entity<objects)return false;
        const auto distance=entity-objects;
        if(distance%kVehicleStride)return false;
        const auto slot=distance/kVehicleStride;
        if(slot>=static_cast<uint32_t>(count) || slot>UINTPTR_MAX/kVehicleStride)return false;
        const auto offset=slot*kVehicleStride;
        if(objects>UINTPTR_MAX-offset || objects+offset!=entity)return false;
        const auto generation=reinterpret_cast<const uint8_t*>(flags)[slot];
        if(generation&0x80u)return false;
        const auto handle_bits=(uint32_t(slot)<<8)|generation;
        if(handle_bits>uint32_t(INT32_MAX))return false;
        *result=RewardVehicleIdentity{static_cast<int32_t>(handle_bits),entity,generation};
        return RewardVehicleRuntime::valid(*result);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool __cdecl reward_branch_lookup(uintptr_t entity) noexcept {
    if(callback_fault || !all_patches_owned())return false;
    RewardVehicleIdentity identity{};
    if(!resolve_live_identity(entity,&identity))return false;
    __try {
        const auto created_by=*reinterpret_cast<const uint8_t*>(entity+kVehicleCreatedByOffset);
        const bool reward=created_by==kMissionVehicle && runtime.confirmed(identity,native_reference_live);
        return !RewardVehicleRuntime::should_skip_mission_vehicle(created_by,reward);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

uint32_t __cdecl reward_restore_capture(uintptr_t record) {
    if(!all_patches_owned())return 0;
    return capture_reward_restore_record(record);
}

bool __cdecl reward_store_after_native(uintptr_t record,uintptr_t vehicle) {
    if(!all_patches_owned())return false;
    const bool ok=mark_associated_record(record,vehicle);
    if(!ok)callback_fault=true;
    return ok;
}

bool __cdecl reward_restore_after(uintptr_t record,uintptr_t vehicle,uint32_t captured_model) {
    if(!all_patches_owned())return false;
    return reward_restore_after_native(record,vehicle,captured_model);
}

void __cdecl reward_before_native_delete(uintptr_t vehicle) {
    if(!all_patches_owned())return;
    if(!remove_owned_mission_cleanup_before_delete(vehicle))callback_fault=true;
}

extern "C" __declspec(naked) void reward_branch_hook() {
    __asm {
        pushfd
        pushad
        push esi
        call reward_branch_lookup
        add esp, 4
        test eax, eax
        jz native_decision
        popad
        popfd
        mov eax, dword ptr [esp + 24h]
        jmp dword ptr [reward_branch_continue]
    native_decision:
        popad
        popfd
        je skip_mission_vehicle
        mov eax, dword ptr [esp + 24h]
        jmp dword ptr [reward_branch_continue]
    skip_mission_vehicle:
        jmp dword ptr [reward_branch_skip]
    }
}

extern "C" __declspec(naked) void reward_store_call_hook() {
    __asm {
        push ecx
        push dword ptr [esp + 8]
        call dword ptr [reward_native_store_car]
        pushfd
        pushad
        mov eax, dword ptr [esp + 2Ch]
        mov edx, dword ptr [esp + 24h]
        push eax
        push edx
        call reward_store_after_native
        add esp, 8
        popad
        popfd
        lea esp, [esp + 4]
        ret 4
    }
}

extern "C" __declspec(naked) void reward_restore_call_hook() {
    __asm {
        push ecx
        push ecx
        call reward_restore_capture
        add esp, 4
        push eax
        mov ecx, dword ptr [esp + 4]
        call dword ptr [reward_native_restore_car]
        pushfd
        pushad
        mov eax, dword ptr [esp + 1Ch]
        test eax, eax
        jz no_reward_restore
        mov ecx, dword ptr [esp + 28h]
        mov edx, dword ptr [esp + 24h]
        push edx
        push eax
        push ecx
        call reward_restore_after
        add esp, 0Ch
    no_reward_restore:
        popad
        popfd
        lea esp, [esp + 8]
        ret
    }
}

// Replaces `call dword ptr [edx]; mov eax,[esp+18h]` at the owned native
// deletion point. It removes the mission cleanup entry only for a confirmed
// reward receipt, then executes the unchanged virtual deleting destructor.
extern "C" __declspec(naked) void reward_delete_call_hook() {
    __asm {
        pushfd
        pushad
        push esi
        call reward_before_native_delete
        add esp, 4
        popad
        popfd
        call dword ptr [edx]
        mov eax, dword ptr [esp + 18h]
        jmp dword ptr [reward_delete_continue]
    }
}
#endif
}

RewardVehicleRuntime::~RewardVehicleRuntime() {
    for(auto& item:active_entries_)if(item.second && item.second->reference_cell)item.second.release();
    for(auto& entry:retired_entries_)if(entry && entry->reference_cell)entry.release();
}

RewardVehicleRuntime::Entry* RewardVehicleRuntime::find(RewardVehicleIdentity identity) {
    const auto it=active_entries_.find(identity.handle);
    return it!=active_entries_.end() && it->second && it->second->identity==identity ? it->second.get():nullptr;
}

const RewardVehicleRuntime::Entry* RewardVehicleRuntime::find(RewardVehicleIdentity identity) const {
    const auto it=active_entries_.find(identity.handle);
    return it!=active_entries_.end() && it->second && it->second->identity==identity ? it->second.get():nullptr;
}

void RewardVehicleRuntime::quarantine(std::unique_ptr<Entry>& entry) {
    if(!entry)return;
    entry->active=false;
    if(entry->reference_cell) {
        try {retired_entries_.push_back(std::move(entry));}
        catch(...) {
            // Keep the native RegisterReference cell alive even when the
            // bookkeeping vector cannot grow; release_all must then fail closed.
            entry.release();
            if(unrecoverable_cell_count_!=std::numeric_limits<size_t>::max())++unrecoverable_cell_count_;
        }
    } else entry.reset();
}

bool RewardVehicleRuntime::valid(RewardVehicleIdentity identity) {
    return identity.handle>=0 && identity.entity && !(identity.generation&0x80u) &&
        identity.generation==uint8_t(identity.handle);
}

uintptr_t* RewardVehicleRuntime::prepare(RewardVehicleIdentity identity) {
    if(!valid(identity))return nullptr;
    auto it=active_entries_.find(identity.handle);
    if(it!=active_entries_.end()) {
        auto& entry=it->second;
        if(entry && entry->identity==identity) {
            if(entry->reference_cell)return entry->active?&entry->reference_cell:nullptr;
            active_entries_.erase(it);
        } else {
            quarantine(entry);
            active_entries_.erase(it);
        }
    }
    try {
        auto entry=std::make_unique<Entry>();
        entry->identity=identity;
        const auto inserted=active_entries_.emplace(identity.handle,std::move(entry));
        return inserted.second?&inserted.first->second->reference_cell:nullptr;
    } catch(...) {return nullptr;}
}

bool RewardVehicleRuntime::publish(RewardVehicleIdentity identity,uintptr_t* stable_cell) {
    auto* entry=find(identity);
    if(!entry || !stable_cell || stable_cell!=&entry->reference_cell ||
       entry->reference_cell!=identity.entity)return false;
    entry->active=true;
    return true;
}

bool RewardVehicleRuntime::confirmed(RewardVehicleIdentity identity,RewardReferenceLive reference_live) const {
    const auto* entry=find(identity);
    return valid(identity) && entry && entry->active && entry->reference_cell==identity.entity &&
        reference_live && reference_live(identity,&entry->reference_cell);
}

bool RewardVehicleRuntime::has_active(RewardVehicleIdentity identity) const {
    const auto* entry=find(identity);
    return valid(identity) && entry && entry->active && entry->reference_cell==identity.entity;
}

std::optional<RewardStoredRecord> RewardVehicleRuntime::stored_record(RewardVehicleIdentity identity) const {
    const auto* entry=find(identity);
    if(!entry || !entry->active || !entry->stored_record || !entry->stored_model)return {};
    return RewardStoredRecord{entry->stored_record,entry->stored_model};
}

bool RewardVehicleRuntime::bind_stored_record(RewardVehicleIdentity identity,uintptr_t record,uint16_t model) {
    auto* entry=find(identity);
    if(!entry || !entry->active || !record || !model || entry->reference_cell!=identity.entity)return false;
    entry->stored_record=record;entry->stored_model=model;
    return true;
}

bool RewardVehicleRuntime::retire(RewardVehicleIdentity identity,RewardReferenceRelease release_reference) {
    auto it=active_entries_.find(identity.handle);
    if(it==active_entries_.end() || !it->second || !(it->second->identity==identity))return true;
    auto& entry=it->second;
    entry->active=false;
    if(!entry->reference_cell) {active_entries_.erase(it);return true;}
    if(release_reference && release_reference(identity,&entry->reference_cell) && !entry->reference_cell) {
        active_entries_.erase(it);
        return true;
    }
    quarantine(entry);
    active_entries_.erase(it);
    return false;
}

bool RewardVehicleRuntime::retire_handle(int32_t handle,const RewardVehicleIdentity* current_identity,
    RewardReferenceRelease release_reference) {
    bool cleared=true;
    auto it=active_entries_.find(handle);
    if(it!=active_entries_.end() && it->second) {
        auto& entry=it->second;
        entry->active=false;
        const bool same=current_identity && entry->identity==*current_identity;
        if(!entry->reference_cell || (same && release_reference &&
           release_reference(entry->identity,&entry->reference_cell) && !entry->reference_cell)) {
            active_entries_.erase(it);
        } else {
            quarantine(entry);active_entries_.erase(it);cleared=false;
        }
    }
    for(auto it_retired=retired_entries_.begin();it_retired!=retired_entries_.end();) {
        auto& entry=*it_retired;
        if(!entry || entry->identity.handle!=handle) {++it_retired;continue;}
        const bool same=current_identity && entry->identity==*current_identity;
        if(!entry->reference_cell || (same && release_reference &&
           release_reference(entry->identity,&entry->reference_cell) && !entry->reference_cell)) {
            it_retired=retired_entries_.erase(it_retired);
        } else {cleared=false;++it_retired;}
    }
    return cleared;
}

bool RewardVehicleRuntime::release_all(RewardReferenceRelease release_reference) {
    bool cleared=unrecoverable_cell_count_==0;
    for(auto it=active_entries_.begin();it!=active_entries_.end();) {
        auto& entry=it->second;
        entry->active=false;
        if(!entry->reference_cell || (release_reference &&
           release_reference(entry->identity,&entry->reference_cell) && !entry->reference_cell)) {
            it=active_entries_.erase(it);
        } else {
            quarantine(entry);it=active_entries_.erase(it);cleared=false;
        }
    }
    for(auto it=retired_entries_.begin();it!=retired_entries_.end();) {
        auto& entry=*it;
        if(!entry || !entry->reference_cell || (release_reference &&
           release_reference(entry->identity,&entry->reference_cell) && !entry->reference_cell)) {
            it=retired_entries_.erase(it);
        } else {cleared=false;++it;}
    }
    return cleared;
}

size_t RewardVehicleRuntime::active_count() const {return active_entries_.size();}
size_t RewardVehicleRuntime::retained_cell_count() const {
    if(unrecoverable_cell_count_>std::numeric_limits<size_t>::max()-retired_entries_.size())
        return std::numeric_limits<size_t>::max();
    return retired_entries_.size()+unrecoverable_cell_count_;
}

bool RewardVehicleRuntime::should_skip_mission_vehicle(uint8_t created_by,bool reward_enabled) {
    return created_by==kMissionVehicle && !reward_enabled;
}

bool RewardVehicleRuntime::should_remove_mission_cleanup(uint8_t created_by,bool reward_enabled) {
    return created_by==kMissionVehicle && reward_enabled;
}

RewardStoredRecordAction mark_reward_stored_car(uint8_t* record,uint16_t live_vehicle_model,bool reward_is_live) {
    if(!record || !live_vehicle_model)return RewardStoredRecordAction::rejected;
    uint16_t stored_model=0;
    std::memcpy(&stored_model,record+reward_stored_car_model_offset,sizeof(stored_model));
    if(stored_model==0 || stored_model!=live_vehicle_model)return RewardStoredRecordAction::rejected;
    if(!reward_is_live)return RewardStoredRecordAction::ignored;
    const auto marker=record[reward_stored_car_marker_offset];
    if(marker==reward_stored_car_marker)return RewardStoredRecordAction::already_marked;
    if(marker!=0)return RewardStoredRecordAction::rejected;
    record[reward_stored_car_marker_offset]=reward_stored_car_marker;
    return RewardStoredRecordAction::marked;
}

bool reward_stored_car_has_marker(const uint8_t* record,uint16_t* model) {
    if(!record || record[reward_stored_car_marker_offset]!=reward_stored_car_marker)return false;
    uint16_t stored_model=0;
    std::memcpy(&stored_model,record+reward_stored_car_model_offset,sizeof(stored_model));
    if(!stored_model)return false;
    if(model)*model=stored_model;
    return true;
}

bool clear_reward_stored_car_marker(uint8_t* record,uint16_t expected_model) {
    if(!record || !expected_model)return false;
    uint16_t model=0;
    std::memcpy(&model,record+reward_stored_car_model_offset,sizeof(model));
    const auto marker=record[reward_stored_car_marker_offset];
    if(marker==0)return true;
    if(marker!=reward_stored_car_marker || model!=expected_model)return false;
    record[reward_stored_car_marker_offset]=0;
    return true;
}

RewardPatchAction plan_reward_patch(const RewardPatchBytes& current,const RewardPatchBytes& original,
    const RewardPatchBytes& replacement,size_t length,bool owned,bool want_installed) {
    if(length==0 || length>current.size())return RewardPatchAction::foreign_or_unowned;
    const bool is_original=std::equal(current.begin(),current.begin()+length,original.begin());
    const bool is_replacement=std::equal(current.begin(),current.begin()+length,replacement.begin());
    if(want_installed) {
        if(is_original)return RewardPatchAction::install;
        if(owned && is_replacement)return RewardPatchAction::already_installed;
        return RewardPatchAction::foreign_or_unowned;
    }
    if(is_original)return RewardPatchAction::already_restored;
    if(owned && is_replacement)return RewardPatchAction::restore;
    return RewardPatchAction::foreign_or_unowned;
}

size_t sa_reward_vehicle_patch_sites(RewardPatchSiteInfo* output,size_t capacity) {
    if(!initialize_patch_specs())return 0;
    if(output)for(size_t i=0;i<std::min(capacity,kPatchSiteCount);++i)
        output[i]=RewardPatchSiteInfo{static_cast<uint32_t>(patch_sites[i].address),patch_sites[i].length,patch_sites[i].original};
    return kPatchSiteCount;
}

bool sa_install_car_reward_hook() {
#if LCS_REWARD_VEHICLE_NATIVE_BACKEND
    return install_patches() && patches_owned_without_fault();
#else
    return false;
#endif
}
bool sa_car_reward_hook_ready() {
#if LCS_REWARD_VEHICLE_NATIVE_BACKEND
    return patches_owned_without_fault();
#else
    return false;
#endif
}

NativeResult sa_set_car_reward(int32_t car,bool enabled) {
    NativeResult result;
#if LCS_REWARD_VEHICLE_NATIVE_BACKEND
    if(!all_patches_owned() || (enabled && callback_fault)) {
        result.diagnostic="Reward vehicle hooks are unavailable, unowned, or faulted";
        return result;
    }
    const auto before=sa_route_vehicle_snapshot(car);
    if(!before) {
        result.diagnostic="Reward metadata requires a live SA vehicle handle and pool generation";
        return result;
    }
    const RewardVehicleIdentity identity{before->handle,before->entity,before->generation};
    if(!RewardVehicleRuntime::valid(identity)) {
        result.diagnostic="Reward metadata identity does not match the live vehicle pool generation";
        return result;
    }
    if(!enabled) {
        if(runtime.has_active(identity))result.native_entered=true;
        const bool retired=runtime.retire(identity,native_reference_release);
        if(!retired)callback_fault=true;
        if(!retired) {
            result.diagnostic="Reward was disabled in memory, but its weak cell could not be safely released";
            return result;
        }
        result.executed=true;
        return result;
    }

    auto* cell=runtime.prepare(identity);
    if(!cell) {
        result.diagnostic="An earlier vehicle reference cell is still quarantined for this identity";
        return result;
    }
    const bool was_registered=*cell!=0;
    if(!was_registered)result.native_entered=true;
    const bool registered=was_registered ? native_reference_live(identity,cell) :
        sa_register_vehicle_reference(car,cell);
    const auto after=sa_route_vehicle_snapshot(car);
    if(!registered || !after || !same_native_identity(*after,identity) ||
       !native_reference_live(identity,cell) || !runtime.publish(identity,cell)) {
        if(!runtime.retire(identity,native_reference_release))callback_fault=true;
        result.diagnostic="Reward weak reference could not be registered and revalidated for the live vehicle";
        return result;
    }
    result.executed=true;
    return result;
#else
    (void)car;(void)enabled;
    result.diagnostic="The reward-vehicle backend test build has no native adapter";
    return result;
#endif
}

void sa_retire_car_reward(int32_t car) {
#if LCS_REWARD_VEHICLE_NATIVE_BACKEND
    const auto snapshot=sa_route_vehicle_snapshot(car);
    if(snapshot) {
        const RewardVehicleIdentity identity{snapshot->handle,snapshot->entity,snapshot->generation};
        runtime.retire_handle(car,&identity,native_reference_release);
    } else runtime.retire_handle(car,nullptr,native_reference_release);
#else
    (void)car;
#endif
}

bool sa_release_car_rewards() {
#if LCS_REWARD_VEHICLE_NATIVE_BACKEND
    const bool patches_released=uninstall_patches();
    const bool references_released=runtime.release_all(native_reference_release);
    const bool released=patches_released && references_released && runtime.active_count()==0 &&
        runtime.retained_cell_count()==0;
    if(released)callback_fault=false;
    return released;
#else
    return false;
#endif
}

}
