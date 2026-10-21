#define LCS_ISOLATED_ENTITY_BACKEND_TEST
#include "../src/sa_no_objective.hpp"
#include "lcs/native_memory.hpp"
#include "lcs/sa_executor.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <stdexcept>

namespace {
using namespace lcs;

static_assert(sizeof(uintptr_t)==sizeof(uint32_t),"The SA backend fixture uses the retail 32-bit pointer layout");
constexpr uintptr_t image_base=0x400000;
constexpr size_t image_size=0xB00000;
constexpr int32_t target_handle=0x101;
alignas(16) std::array<uint8_t,image_size> image{};
ObjectiveActor actor{};
ScriptTaskSnapshot records{};
bool profile_available=true,records_available=true,actor_available=true;
bool change_actor_on_revalidation=false;
size_t live_actor_reads=0;

uintptr_t cell(uintptr_t retail) {
    if(retail<image_base || retail-image_base>=image.size())return 0;
    return reinterpret_cast<uintptr_t>(image.data())+(retail-image_base);
}

void check(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}

template<size_t N>
void bytes(uintptr_t retail,const uint8_t (&value)[N]) {
    const auto address=cell(retail);
    if(!address)throw std::out_of_range("fixture address outside native image");
    std::memcpy(reinterpret_cast<void*>(address),value,N);
}

void reset() {
    image.fill(0);
    records=ScriptTaskSnapshot{};
    profile_available=true;
    records_available=true;
    actor_available=true;
    change_actor_on_revalidation=false;
    live_actor_reads=0;

    const uint8_t intelligence_ctor[]={
        0x6A,0xFF,0x68,0x76,0xEA,0x83,0x00,0x64,0xA1,0,0,0,0,0x50,0x64,
        0x89,0x25,0,0,0,0,0x51,0x53,0x55,0x8B,0x6C,0x24,0x1C,0x56,0x8B,
        0xF1,0x57,0x55,0x8D,0x4E,0x04,0x89,0x74,0x24,0x14,0x89,0x2E,0xE8,
        0x31,0xA5,0x07,0x00
    };
    const uint8_t active_primary[]={0x33,0xC0,0x83,0x3C,0x81,0x00,0x75,0x09,0x40,0x83,0xF8,0x05};
    bytes(0x607140,intelligence_ctor);
    bytes(0x681720,active_primary);

    actor={target_handle,cell(0xCE5000)};
    const auto intelligence=cell(0xCE6000);
    *reinterpret_cast<uintptr_t*>(actor.native_ped+0x47C)=intelligence;
    *reinterpret_cast<uintptr_t*>(intelligence)=actor.native_ped;
}

void add_event_record() {
    records[7]={0x05D3,uint32_t(cell(0xCE7000)),0,uint32_t(actor.native_ped),0};
}

void add_group_record() {
    records[11]={0x05D3,0,uint32_t(cell(0xCE7100)),uint32_t(actor.native_ped),4};
}

void test_ambient_default_is_not_a_script_objective() {
    reset();
    const auto intelligence=*reinterpret_cast<const uintptr_t*>(actor.native_ped+0x47C);
    *reinterpret_cast<uintptr_t*>(intelligence+4+4*sizeof(uint32_t))=cell(0xCE7100); // DEFAULT slot 4
    const auto result=sa_char_has_no_objective(target_handle);
    check(result && *result,"An empty script record table with only the SA default task is no script objective");
}

void test_primary_script_slot_is_an_objective() {
    reset();
    const auto intelligence=*reinterpret_cast<const uintptr_t*>(actor.native_ped+0x47C);
    *reinterpret_cast<uintptr_t*>(intelligence+4+3*sizeof(uint32_t))=cell(0xCE7100); // PRIMARY slot 3
    const auto result=sa_char_has_no_objective(target_handle);
    check(result && !*result,"A live SA primary script task prevents no-objective");
}

void test_pending_event_and_group_task_are_objectives() {
    reset();
    add_event_record();
    auto result=sa_char_has_no_objective(target_handle);
    check(result && !*result,"A pending script event record prevents no-objective");

    reset();
    add_group_record();
    result=sa_char_has_no_objective(target_handle);
    check(result && !*result,"A script task associated through the group remains an objective");
}

void test_suspended_script_task_still_counts() {
    reset();
    records[17]={0x05D3,0,uint32_t(cell(0xCE7100)),uint32_t(actor.native_ped),6};
    const auto result=sa_char_has_no_objective(target_handle);
    check(result && !*result,"A script task tracked in a secondary/suspended state is not reported absent");
}

void test_orphaned_other_ped_record_does_not_block_query() {
    reset();
    records[23]={0x05D3,uint32_t(cell(0xCE7200)),0,0,0};
    const auto result=sa_char_has_no_objective(target_handle);
    check(result && *result,"An orphaned record whose weak ped reference was cleared does not block another live ped query");
}

void test_invalid_record_or_profile_is_unavailable() {
    reset();
    records[4]={0x05D3,0,0,uint32_t(actor.native_ped),7};
    check(!sa_char_has_no_objective(target_handle),"An inconsistent scripted-task record remains unavailable");

    reset();
    profile_available=false;
    check(!sa_char_has_no_objective(target_handle),"Unsupported executable profile remains unavailable");

    reset();
    *reinterpret_cast<uint8_t*>(cell(0x681720))^=1;
    check(!sa_char_has_no_objective(target_handle),"A changed task-manager layout pin remains unavailable");
}

void test_identity_is_revalidated() {
    reset();
    change_actor_on_revalidation=true;
    check(!sa_char_has_no_objective(target_handle),"A ped generation change during the read remains unavailable");

    reset();
    actor_available=false;
    check(!sa_char_has_no_objective(target_handle),"A missing ped handle remains unavailable");
}

} // namespace

namespace lcs {

uintptr_t native_entity_address(uintptr_t retail) {return cell(retail);}
bool sa_executor_profile_matches() {return profile_available;}
std::optional<ObjectiveActor> sa_live_objective_actor(int32_t handle) {
    ++live_actor_reads;
    if(!actor_available || handle!=target_handle)return {};
    auto current=actor;
    if(change_actor_on_revalidation && live_actor_reads>1)current.native_ped+=0x7C4;
    return current;
}
std::optional<ScriptTaskSnapshot> sa_script_task_snapshot() {
    if(!records_available)return {};
    return records;
}

} // namespace lcs

int main() {
    try {
        test_ambient_default_is_not_a_script_objective();
        test_primary_script_slot_is_an_objective();
        test_pending_event_and_group_task_are_objectives();
        test_suspended_script_task_still_counts();
        test_orphaned_other_ped_record_does_not_block_query();
        test_invalid_record_or_profile_is_unavailable();
        test_identity_is_revalidated();
        std::cout<<"no-objective backend checks passed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
