#pragma once

#include "lcs/native.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#if defined(LCS_REWARD_VEHICLE_BACKEND_TEST)
#define LCS_REWARD_VEHICLE_NATIVE_BACKEND 0
#elif defined(_WIN32) && defined(_MSC_VER) && defined(_M_IX86)
#define LCS_REWARD_VEHICLE_NATIVE_BACKEND 1
#else
#define LCS_REWARD_VEHICLE_NATIVE_BACKEND 0
#endif

namespace lcs {

struct RewardVehicleIdentity {
    int32_t handle=-1;
    uintptr_t entity=0;
    uint8_t generation=0;
    bool operator==(const RewardVehicleIdentity& other) const {
        return handle==other.handle && entity==other.entity && generation==other.generation;
    }
};

using RewardReferenceLive=bool(*)(const RewardVehicleIdentity&,const uintptr_t*);
using RewardReferenceRelease=bool(*)(const RewardVehicleIdentity&,uintptr_t*);

struct RewardStoredRecord {
    uintptr_t address=0;
    uint16_t model=0;
};

class RewardVehicleRuntime {
    struct Entry {
        RewardVehicleIdentity identity;
        uintptr_t reference_cell=0;
        uintptr_t stored_record=0;
        uint16_t stored_model=0;
        bool active=false;
    };
    std::unordered_map<int32_t,std::unique_ptr<Entry>> active_entries_;
    std::vector<std::unique_ptr<Entry>> retired_entries_;
    size_t unrecoverable_cell_count_=0;
    Entry* find(RewardVehicleIdentity identity);
    const Entry* find(RewardVehicleIdentity identity) const;
    void quarantine(std::unique_ptr<Entry>& entry);
public:
    ~RewardVehicleRuntime();
    static bool valid(RewardVehicleIdentity identity);
    // Returns a stable cell owned by a heap-stable receipt. A cell with an
    // uncertain nonzero value is quarantined and cannot be reused.
    uintptr_t* prepare(RewardVehicleIdentity identity);
    bool publish(RewardVehicleIdentity identity,uintptr_t* stable_cell);
    bool confirmed(RewardVehicleIdentity identity,RewardReferenceLive reference_live) const;
    bool has_active(RewardVehicleIdentity identity) const;
    std::optional<RewardStoredRecord> stored_record(RewardVehicleIdentity identity) const;
    bool bind_stored_record(RewardVehicleIdentity identity,uintptr_t record,uint16_t model);
    bool retire(RewardVehicleIdentity identity,RewardReferenceRelease release_reference);
    bool retire_handle(int32_t handle,const RewardVehicleIdentity* current_identity,
        RewardReferenceRelease release_reference);
    bool release_all(RewardReferenceRelease release_reference);
    size_t active_count() const;
    size_t retained_cell_count() const;

    // Mirrors the native JE after CMP [ESI+0x4A4],2. Only a reward-marked
    // mission vehicle bypasses the original skip branch.
    static bool should_skip_mission_vehicle(uint8_t created_by,bool reward_enabled);
    // The native garage reaches its owned deletion call for accepted mission
    // vehicles, including when the hideout has no free record. Remove only
    // that exact mission-cleanup handle before the native destructor.
    static bool should_remove_mission_cleanup(uint8_t created_by,bool reward_enabled);
};

inline constexpr size_t reward_stored_car_record_size=0x40;
inline constexpr size_t reward_stored_car_flags_offset=0x10;
inline constexpr size_t reward_stored_car_marker_offset=0x11;
inline constexpr size_t reward_stored_car_model_offset=0x12;
inline constexpr uint8_t reward_stored_car_marker=0xA7;

struct RewardStoreHookStackContract {
    static constexpr uint8_t vehicle_argument_after_pushad=0x2C;
    static constexpr uint8_t saved_original_this_after_pushad=0x24;
    static constexpr uint8_t restore_capture_model_after_pushad=0x24;
    static constexpr uint8_t restore_record_after_pushad=0x28;
    static constexpr uint8_t restore_vehicle_after_pushad=0x1C;
};

enum class RewardStoredRecordAction : uint8_t {ignored,marked,already_marked,rejected};
RewardStoredRecordAction mark_reward_stored_car(uint8_t* record,uint16_t live_vehicle_model,
    bool reward_is_live);
bool reward_stored_car_has_marker(const uint8_t* record,uint16_t* model=nullptr);
bool clear_reward_stored_car_marker(uint8_t* record,uint16_t expected_model);

enum class RewardPatchAction : uint8_t {
    install,already_installed,restore,already_restored,foreign_or_unowned
};
using RewardPatchBytes=std::array<uint8_t,6>;
struct RewardPatchSiteInfo {
    uint32_t address=0;
    uint8_t length=0;
    RewardPatchBytes original{};
};
RewardPatchAction plan_reward_patch(const RewardPatchBytes& current,
    const RewardPatchBytes& original,const RewardPatchBytes& replacement,
    size_t length,bool owned,bool want_installed);
size_t sa_reward_vehicle_patch_sites(RewardPatchSiteInfo* output,size_t capacity);

// Install at campaign initialization, before any garage restore is run. This
// owns only checked StoreCar/RestoreCar callsites and the native eligibility
// branch; SA continues to execute every original method body.
bool sa_install_car_reward_hook();
bool sa_car_reward_hook_ready();

// Opcode 1655 backend. False clears only the live runtime receipt; native
// restore callers consume the stored slot, which may immediately be reused.
NativeResult sa_set_car_reward(int32_t car,bool enabled);
// Call before native DELETE_CAR while the original full vehicle identity is live.
void sa_retire_car_reward(int32_t car);
// Release owned code patches and all native weak-reference cells; false means
// a foreign patch or a cell still requires this module's stable storage.
bool sa_release_car_rewards();

}
