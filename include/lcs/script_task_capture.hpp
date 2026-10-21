#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace lcs {
// Exact reviewed Win32 record layout. These are snapshots, never borrowed
// pointers to native records, and none of their fields proves completion.
struct ScriptTaskRecord {
    int32_t opcode=-1;
    uint32_t event=0,task=0,ped=0;
    int32_t status=3;
    bool operator==(const ScriptTaskRecord& other)const {
        return opcode==other.opcode && event==other.event && task==other.task && ped==other.ped && status==other.status;
    }
};
static_assert(sizeof(ScriptTaskRecord)==0x14);
using ScriptTaskSnapshot=std::array<ScriptTaskRecord,128>;
struct CapturedScriptTask {
    size_t slot=0;
    ScriptTaskRecord record;
};
// Capture around ONE successful native command, on the game thread. A
// pre-existing task/status or a target match by itself is not ownership.
// Ambiguous/malformed capture stays unavailable; it does not select slot zero.
inline std::optional<CapturedScriptTask> capture_accepted_script_task(
        const ScriptTaskSnapshot& before,const ScriptTaskSnapshot& after,
        uint32_t live_ped,int32_t native_opcode) {
    if(!live_ped || native_opcode<0)return {};
    std::optional<CapturedScriptTask> found;
    for(size_t slot=0;slot<after.size();++slot) {
        const auto& record=after[slot];const auto& old=before[slot];
        if(record.ped!=live_ped || record.opcode!=native_opcode || record==old)continue;
        if(record.event==old.event && record.task==old.task && record.ped==old.ped && record.opcode==old.opcode)
            continue; // Only status changed in an earlier job.
        if(old.event && old.ped==record.ped && old.opcode==record.opcode && !record.event && record.task)
            return {}; // Earlier event associated with a task: no new ownership proof.
        const bool event=record.status==0 && record.event && !record.task;
        const bool task=(record.status==1 || record.status==2 || (record.status>=4 && record.status<=6)) && record.task && !record.event;
        if(!event && !task)return {};
        if(found)return {};
        found=CapturedScriptTask{slot,record};
    }
    return found;
}
// Use only inside an observed native AssociateWithTask invocation. A pair of
// later snapshots is not this evidence: the record could have been reused.
inline std::optional<CapturedScriptTask> associate_captured_event(
        const CapturedScriptTask& owned,size_t observed_slot,
        const ScriptTaskRecord& before,const ScriptTaskRecord& after,uint32_t task_argument) {
    if(owned.slot>=128 || owned.slot!=observed_slot || !(owned.record==before) ||
       before.status!=0 || !before.event || before.task || !before.ped || !task_argument)return {};
    if(after.opcode!=before.opcode || after.ped!=before.ped || after.event ||
       after.task!=task_argument || after.status!=1)return {};
    return CapturedScriptTask{owned.slot,after};
}
// A native Flush invalidates ownership; it never completes an objective.
// Status may have moved between TASK/PRIMARY/SECONDARY without replacing its
// binding, so compare binding identity rather than requiring identical status.
inline bool flushes_captured_binding(const CapturedScriptTask& owned,size_t observed_slot,
        const ScriptTaskRecord& before,const ScriptTaskRecord& after) {
    if(owned.slot>=128 || observed_slot!=owned.slot || owned.record.opcode!=before.opcode ||
       owned.record.ped!=before.ped || owned.record.event!=before.event || owned.record.task!=before.task)
        return false;
    return after.opcode==-1 && !after.ped && !after.event && !after.task && after.status==3;
}
}
