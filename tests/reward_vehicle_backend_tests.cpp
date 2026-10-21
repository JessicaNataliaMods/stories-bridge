#include "../src/sa_reward_vehicle.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace {
using namespace lcs;

struct SaStoredCarLayoutFixture {
    uint8_t position[12];
    uint32_t handling_flags;
    uint8_t stored_flags;
    uint8_t reserved_marker_byte;
    uint16_t model_index;
    uint8_t payload[reward_stored_car_record_size-0x14];
};
static_assert(offsetof(SaStoredCarLayoutFixture,stored_flags)==0x10);
static_assert(offsetof(SaStoredCarLayoutFixture,reserved_marker_byte)==0x11);
static_assert(offsetof(SaStoredCarLayoutFixture,model_index)==reward_stored_car_model_offset);
static_assert(sizeof(SaStoredCarLayoutFixture)==reward_stored_car_record_size);
static_assert(RewardStoreHookStackContract::vehicle_argument_after_pushad==0x2C);
static_assert(RewardStoreHookStackContract::saved_original_this_after_pushad==0x24);
static_assert(RewardStoreHookStackContract::restore_capture_model_after_pushad==0x24);
static_assert(RewardStoreHookStackContract::restore_record_after_pushad==0x28);
static_assert(RewardStoreHookStackContract::restore_vehicle_after_pushad==0x1C);

struct FakeReference {
    RewardVehicleIdentity live_identity{};
    bool registered=false;
};
FakeReference fake_reference;

bool fake_reference_live(const RewardVehicleIdentity& identity,const uintptr_t* cell) {
    return cell && *cell==identity.entity && fake_reference.registered &&
        fake_reference.live_identity==identity;
}

bool fake_reference_release(const RewardVehicleIdentity& identity,uintptr_t* cell) {
    if(!cell)return false;
    if(!*cell)return true;
    if(!fake_reference.registered || !(fake_reference.live_identity==identity) || *cell!=identity.entity)
        return false;
    *cell=0;
    fake_reference.registered=false;
    return true;
}

bool fake_register(const RewardVehicleIdentity& identity,uintptr_t* cell) {
    if(!cell)return false;
    *cell=identity.entity;
    fake_reference.live_identity=identity;
    fake_reference.registered=true;
    return true;
}

} // namespace

int main() {
    using namespace lcs;
    int failures=0;
    const auto check=[&](bool condition,const char* message) {
        if(!condition){std::cerr<<"FAIL: "<<message<<'\n';++failures;}
    };

    const RewardVehicleIdentity mission_reward{0x00000107,0x10000000,0x07};
    const RewardVehicleIdentity reused_handle{0x00000107,0x10001000,0x07};
    const RewardVehicleIdentity ordinary_car{0x00000208,0x10002000,0x08};
    check(RewardVehicleRuntime::valid(mission_reward),"A live pool handle carries the expected lower-byte generation");
    check(!RewardVehicleRuntime::valid({0x00000187,0x10000000,0x87}),"Freed-pool generation tags are rejected");
    check(RewardVehicleRuntime::should_skip_mission_vehicle(2,false),"Unmarked MISSION_VEHICLE retains the native JE skip");
    check(!RewardVehicleRuntime::should_skip_mission_vehicle(2,true),"A confirmed reward MISSION_VEHICLE bypasses only the native skip branch");
    check(!RewardVehicleRuntime::should_skip_mission_vehicle(1,false),"Non-mission eligibility remains native");
    check(RewardVehicleRuntime::should_remove_mission_cleanup(2,true),
        "Owned reward mission vehicles remove the exact mission-cleanup entry before native deletion");
    check(!RewardVehicleRuntime::should_remove_mission_cleanup(2,false) &&
          !RewardVehicleRuntime::should_remove_mission_cleanup(1,true),
        "Cleanup removal is refused for unowned missions and every non-mission vehicle");

    RewardPatchSiteInfo sites[7]{};
    check(sa_reward_vehicle_patch_sites(sites,7)==7,"Production backend exposes seven pinned native patch sites");
    const uint32_t expected_addresses[]={0x44999C,0x4499C8,0x449B18,0x44A4BB,
        0x15614AB,0x156D8EA,0x449A01};
    const uint8_t expected_lengths[]={6,5,5,5,5,5,6};
    const uint8_t expected_bytes[][6]={
        {0x74,0x65,0x8B,0x44,0x24,0x24},
        {0xE8,0x93,0xFD,0xFF,0xFF,0x00},
        {0xE8,0x43,0xFC,0xFF,0xFF,0x00},
        {0xE8,0xA0,0xF2,0xFF,0xFF,0x00},
        {0xE8,0x90,0x69,0xEE,0xFE,0x00},
        {0xE8,0x51,0xA5,0xED,0xFE,0x00},
        {0xFF,0x12,0x8B,0x44,0x24,0x18}
    };
    for(size_t i=0;i<7;++i) {
        check(sites[i].address==expected_addresses[i],"Native patch site address matches the reviewed retail call path");
        check(sites[i].length==expected_lengths[i],"Native patch site covers whole instructions only");
        check(std::memcmp(sites[i].original.data(),expected_bytes[i],expected_lengths[i])==0,
            "Native patch site original bytes match the reviewed profile");
    }

    const RewardPatchBytes original{{0x74,0x65,0x8B,0x44,0x24,0x24}};
    const RewardPatchBytes replacement{{0xE9,0x11,0x22,0x33,0x44,0x90}};
    auto foreign=replacement;foreign[1]^=0x80;
    check(plan_reward_patch(original,original,replacement,6,false,true)==RewardPatchAction::install,
        "A retail-owned original branch can be claimed");
    check(plan_reward_patch(replacement,original,replacement,6,true,true)==RewardPatchAction::already_installed,
        "An identical patch is accepted only when this backend owns it");
    check(plan_reward_patch(replacement,original,replacement,6,false,true)==RewardPatchAction::foreign_or_unowned,
        "An identical foreign patch is not claimed");
    check(plan_reward_patch(foreign,original,replacement,6,true,false)==RewardPatchAction::foreign_or_unowned,
        "A foreign modification blocks teardown instead of being overwritten");
    check(plan_reward_patch(replacement,original,replacement,6,true,false)==RewardPatchAction::restore,
        "Owned bytes restore only to the exact retail instruction sequence");
    check(plan_reward_patch(original,original,replacement,6,false,false)==RewardPatchAction::already_restored,
        "Already-restored retail bytes are recognized");

    SaStoredCarLayoutFixture stored{};
    auto* record=reinterpret_cast<uint8_t*>(&stored);
    stored.position[8]=0xA5;
    stored.stored_flags=0x41;
    stored.model_index=0x1234;
    stored.payload[0]=0xC7;
    const auto flags_before=stored.stored_flags;
    check(mark_reward_stored_car(record,0x1234,true)==RewardStoredRecordAction::marked,
        "The native post-StoreCar callback tags only the verified reserved high byte");
    check(reward_stored_car_has_marker(record) && record[0x11]==0xA7,
        "Marker recognition uses the proved +0x11 byte and requires a nonzero native model");
    check(stored.stored_flags==flags_before && stored.model_index==0x1234 &&
          stored.position[8]==0xA5 && stored.payload[0]==0xC7,
        "Marker writes preserve native flags, model, position and mod payload");
    check(mark_reward_stored_car(record,0x9999,true)==RewardStoredRecordAction::rejected,
        "A reused record with a mismatched model cannot inherit the reward marker");
    check(clear_reward_stored_car_marker(record,0x9999)==false,
        "A mismatched model cannot clear a reward marker belonging to another stored car");
    check(clear_reward_stored_car_marker(record,0x1234) && !reward_stored_car_has_marker(record),
        "Disabling reward clears only the marker byte when the stored model still matches");
    record[0x11]=0x42;
    check(mark_reward_stored_car(record,0x1234,true)==RewardStoredRecordAction::rejected &&
          record[0x11]==0x42,
        "An unknown byte owner prevents marker overwrite");

    fake_reference={};
    RewardVehicleRuntime runtime;
    auto* stable_cell=runtime.prepare(mission_reward);
    check(stable_cell!=nullptr,"Reward receipt allocates a stable weak-reference cell");
    check(fake_register(mission_reward,stable_cell) && runtime.publish(mission_reward,stable_cell),
        "Receipt publishes only after the native weak reference contains the exact entity");
    check(runtime.confirmed(mission_reward,fake_reference_live) && runtime.has_active(mission_reward),
        "Production reward ownership requires the same handle, entity and pool generation");
    check(runtime.bind_stored_record(mission_reward,0x20000000,0x1234),
        "Stored record association is published only for an active exact identity");
    const auto saved_record=runtime.stored_record(mission_reward);
    check(saved_record && saved_record->address==0x20000000 && saved_record->model==0x1234,
        "False setter can locate its owned stored marker by the receipt identity");

    auto* recycled_cell=runtime.prepare(reused_handle);
    check(recycled_cell && recycled_cell!=stable_cell && runtime.retained_cell_count()==1,
        "A reused handle quarantines the old nonzero weak cell instead of reusing its storage");
    check(*stable_cell==mission_reward.entity && !runtime.has_active(mission_reward),
        "The recycled entity cannot inherit the prior reward receipt");
    check(fake_register(reused_handle,recycled_cell) && runtime.publish(reused_handle,recycled_cell) &&
          runtime.confirmed(reused_handle,fake_reference_live),
        "A new generation becomes owned only after its own weak reference is registered");
    check(runtime.retire(reused_handle,fake_reference_release) && runtime.active_count()==0,
        "DELETE_CAR retirement releases the matching reference and receipt");
    check(!runtime.release_all(fake_reference_release) && runtime.retained_cell_count()==1,
        "Release keeps unresolved old-generation cells alive for a safe retry");
    *stable_cell=0; // Native RegisterReference normally nulls this during entity destruction.
    check(runtime.release_all(fake_reference_release) && runtime.retained_cell_count()==0,
        "Cleanup succeeds only after every retained native weak cell is confirmed null");

    if(failures)return 1;
    std::cout<<"PASS: production SA eligibility, mission cleanup ownership, patch ownership, +0x11 persistence marker, stack contracts, and weak-cell lifecycle\n";
    return 0;
}
