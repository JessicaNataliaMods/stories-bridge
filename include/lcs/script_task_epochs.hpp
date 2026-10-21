#pragma once
#include "script_task_capture.hpp"
#include <limits>

namespace lcs {
struct EpochScriptTask {CapturedScriptTask binding;uint64_t epoch=0;};
// Reuse protection for synchronous native mutation notifications. It is not a
// polling replacement for hooks: unknown/unobserved slots cannot issue tokens.
// No epoch or record status is a task-completion signal.
class ScriptTaskEpochs {
public:
    struct Mutation {size_t slot=128;uint64_t epoch=0;};
private:
    struct Slot {uint64_t epoch=1;std::optional<ScriptTaskRecord> record;uint32_t queued_task=0;std::array<uint64_t,32> pending{};size_t count=0;};
    std::array<Slot,128> slots_{};
    static void retire(Slot& entry) {
        entry.record.reset();entry.queued_task=0;
        if(!entry.epoch || entry.epoch==(std::numeric_limits<uint64_t>::max)())entry.epoch=0;
        else ++entry.epoch;
    }
    static bool valid_binding(const ScriptTaskRecord& record) {
        if(record.opcode<0 || !record.ped)return false;
        if(record.status==0)return record.event && !record.task;
        return (record.status==1 || record.status==2 || record.status==4 || record.status==5 || record.status==6) && !record.event && record.task;
    }
    static bool binding_equal(const ScriptTaskRecord& a,const ScriptTaskRecord& b) {
        return a.opcode==b.opcode && a.ped==b.ped && a.event==b.event && a.task==b.task;
    }
public:
    // Call at every real Set/Flush/constructor entry, INCLUDING inline Flush
    // and an overwrite whose final bytes equal its previous bytes (ABA).
    std::optional<Mutation> replacing(size_t slot) {
        if(slot>=slots_.size())return {};
        auto& entry=slots_[slot];entry.record.reset();entry.queued_task=0;
        if(!entry.epoch || entry.epoch==(std::numeric_limits<uint64_t>::max)() || entry.count==entry.pending.size())entry.epoch=0;
        else ++entry.epoch;
        if(!entry.epoch)return {};
        entry.pending[entry.count++]=entry.epoch;return Mutation{slot,entry.epoch};
    }
    bool replaced(Mutation mutation,const ScriptTaskRecord& actual) {
        if(mutation.slot>=slots_.size() || !mutation.epoch)return false;
        auto& entry=slots_[mutation.slot];if(!entry.epoch)return false;
        size_t index=0;while(index<entry.count && entry.pending[index]!=mutation.epoch)++index;
        if(index==entry.count)return false;
        for(size_t i=index+1;i<entry.count;++i)entry.pending[i-1]=entry.pending[i];
        --entry.count;
        // A nested writer may have completed in the same slot. The outer
        // routine's remaining writes are another replacement, even if equal.
        if(mutation.epoch!=entry.epoch) {
            if(entry.epoch==(std::numeric_limits<uint64_t>::max)()){entry.epoch=0;entry.record.reset();return false;}
            ++entry.epoch;
        }
        entry.record=actual;return true;
    }
    void reset() {for(size_t slot=0;slot<slots_.size();++slot){replacing(slot);slots_[slot].count=0;}}
    // The native observer must validate CEventScriptCommand's real vtable and
    // copy its +16 task while the event is alive. A guessed/polled pointer is
    // not a dependency. No engine pointer is dereferenced by this class.
    bool observe_event_task(const EpochScriptTask& owned,const ScriptTaskRecord& actual,
            uint32_t observed_event,uint32_t observed_task) {
        if(!still_owned(owned,actual) || actual.status!=0 || actual.task ||
           !observed_task || observed_event!=actual.event)return false;
        auto& entry=slots_[owned.binding.slot];
        if(entry.queued_task && entry.queued_task!=observed_task){retire(entry);return false;}
        entry.queued_task=observed_task;return true;
    }
    // Notifications occur BEFORE native deletion/reuse. Retiring only matching
    // dependencies leaves unrelated actors/tasks intact and never completes a
    // job. Unknown slots cannot be rediscovered through a later equal snapshot.
    void retiring_task(uint32_t task) {
        if(!task)return;
        for(auto& entry:slots_)if(entry.record &&
            (entry.record->task==task || entry.queued_task==task))retire(entry);
    }
    void retiring_event(uint32_t event) {
        if(!event)return;
        for(auto& entry:slots_)if(entry.record && entry.record->event==event)retire(entry);
    }
    void inline_flush(size_t slot) {if(slot<slots_.size())retire(slots_[slot]);}
    std::optional<EpochScriptTask> capture(const CapturedScriptTask& binding,const ScriptTaskRecord& actual) const {
        if(binding.slot>=slots_.size())return {};
        const auto& entry=slots_[binding.slot];
        if(!entry.epoch || entry.count || !entry.record || !valid_binding(actual) || !(binding.record==actual) || !(*entry.record==actual))return {};
        // Caller must additionally prove capture around its accepted command.
        return EpochScriptTask{binding,entry.epoch};
    }
    bool still_owned(const EpochScriptTask& owned,const ScriptTaskRecord& actual) const {
        if(owned.binding.slot>=slots_.size() || !owned.epoch)return false;
        const auto& entry=slots_[owned.binding.slot];
        return !entry.count && entry.epoch==owned.epoch && entry.record && valid_binding(actual) && binding_equal(*entry.record,actual) &&
            binding_equal(owned.binding.record,actual);
    }
    std::optional<EpochScriptTask> associated(const EpochScriptTask& owned,size_t slot,
            const ScriptTaskRecord& before,const ScriptTaskRecord& after,uint32_t task_argument) {
        if(!still_owned(owned,before))return {};
        const auto binding=associate_captured_event(owned.binding,slot,before,after,task_argument);
        if(!binding)return {};
        slots_[slot].record=after;slots_[slot].queued_task=0;return EpochScriptTask{*binding,owned.epoch};
    }
};
}
