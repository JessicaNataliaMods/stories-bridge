#include <windows.h>

#include "lcs/sa_executor.hpp"
#include "sa_no_objective.hpp"

#ifndef LCS_NO_OBJECTIVE_BACKEND_TEST
#include "lcs/native_memory.hpp"
#endif

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace lcs {
#ifdef LCS_NO_OBJECTIVE_BACKEND_TEST
uintptr_t native_entity_address(uintptr_t retail);
#endif
namespace {

constexpr int32_t record_event_associated=0;
constexpr int32_t record_task_associated=1;
constexpr int32_t record_primary=2;
constexpr int32_t record_flushed=3;
constexpr int32_t record_group=4;
constexpr int32_t record_brain=5;
constexpr int32_t record_secondary=6;

template<size_t N>
bool profile(uintptr_t address,const uint8_t (&bytes)[N]) {
    return std::memcmp(reinterpret_cast<const void*>(native_entity_address(address)),bytes,N)==0;
}

// CPedIntelligence begins with m_pPed followed by CTaskManager at +4. The
// manager's primary array has five pointer slots; index 3 is PRIMARY and
// index 4 is DEFAULT. Check the retail intelligence constructor and the
// existing GetActiveTask loop before reading those slots; no task method is
// called.
bool read_primary_script_task(const ObjectiveActor& actor,uintptr_t& task) {
    task=0;
    if(!actor.native_ped || actor.native_ped>UINTPTR_MAX-0x47C)return false;
    __try {
        const uint8_t intelligence_ctor[]={
            0x6A,0xFF,0x68,0x76,0xEA,0x83,0x00,0x64,0xA1,0,0,0,0,0x50,0x64,
            0x89,0x25,0,0,0,0,0x51,0x53,0x55,0x8B,0x6C,0x24,0x1C,0x56,0x8B,
            0xF1,0x57,0x55,0x8D,0x4E,0x04,0x89,0x74,0x24,0x14,0x89,0x2E,0xE8,
            0x31,0xA5,0x07,0x00
        };
        const uint8_t active_primary[]={0x33,0xC0,0x83,0x3C,0x81,0x00,0x75,0x09,0x40,0x83,0xF8,0x05};
        if(!profile(0x607140,intelligence_ctor) || !profile(0x681720,active_primary))return false;

        const auto intelligence=*reinterpret_cast<const uintptr_t*>(actor.native_ped+0x47C);
        if(!intelligence || intelligence>UINTPTR_MAX-0x14 ||
           *reinterpret_cast<const uintptr_t*>(intelligence)!=actor.native_ped)return false;

        // Only the script-primary slot is a script objective here. The
        // default slot and the other native response slots remain SA behavior.
        task=*reinterpret_cast<const uintptr_t*>(intelligence+4+3*sizeof(uint32_t));
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        task=0;
        return false;
    }
}

bool valid_script_task_record(const ScriptTaskRecord& record) {
    if(record.status==record_flushed)
        return record.opcode==-1 && !record.event && !record.task && !record.ped;
    if(record.opcode<0 || !record.ped)return false;
    if(record.status==record_event_associated)
        return record.event && !record.task;
    if(record.status==record_task_associated || record.status==record_primary ||
       record.status==record_group || record.status==record_brain || record.status==record_secondary)
        return !record.event && record.task;
    return false;
}

std::optional<bool> no_script_record_for_ped(const ScriptTaskSnapshot& records,uint32_t ped) {
    if(!ped)return {};
    bool found=false;
    for(const auto& record:records) {
        // CPedScriptedTaskRecord::Process can observe a cleared weak ped
        // reference immediately before Flush clears the rest of that slot.
        // Such an orphan belongs to another/dead identity and says nothing
        // about the live target ped.
        if(record.ped!=ped)continue;
        if(!valid_script_task_record(record))return {};
        found=true;
    }
    return !found;
}

bool same_ped_record_bindings(const ScriptTaskSnapshot& before,const ScriptTaskSnapshot& after,uint32_t ped) {
    for(size_t slot=0;slot<before.size();++slot) {
        const auto& a=before[slot];
        const auto& b=after[slot];
        if((a.ped==ped || b.ped==ped) &&
           (a.opcode!=b.opcode || a.event!=b.event || a.task!=b.task || a.ped!=b.ped))return false;
    }
    return true;
}

} // namespace

std::optional<bool> sa_char_has_no_objective(int32_t ped_handle) {
    if(!sa_executor_profile_matches())return {};
    const auto actor=sa_live_objective_actor(ped_handle);
    if(!actor || actor->handle!=ped_handle || !actor->native_ped)return {};

    const auto records_before=sa_script_task_snapshot();
    if(!records_before)return {};
    const auto no_record_before=no_script_record_for_ped(*records_before,uint32_t(actor->native_ped));
    if(!no_record_before)return {};

    uintptr_t primary_before=0;
    if(!read_primary_script_task(*actor,primary_before))return {};

    const auto records_after=sa_script_task_snapshot();
    if(!records_after)return {};
    uintptr_t primary_after=0;
    if(!read_primary_script_task(*actor,primary_after) || primary_before!=primary_after ||
       !same_ped_record_bindings(*records_before,*records_after,uint32_t(actor->native_ped)))return {};

    const auto actor_after=sa_live_objective_actor(ped_handle);
    if(!actor_after || !(*actor_after==*actor))return {};
    if(!sa_executor_profile_matches())return {};

    const auto no_record_after=no_script_record_for_ped(*records_after,uint32_t(actor->native_ped));
    if(!no_record_after)return {};
    // A task in slot 3 or any active scripted-task record (including event,
    // secondary, group and brain records) means an objective is still present.
    // Slot 4/default remains the native ambient/default task, not a script goal.
    return *no_record_before && *no_record_after && primary_before==0;
}

} // namespace lcs
