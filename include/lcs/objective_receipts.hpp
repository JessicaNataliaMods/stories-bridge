#pragma once
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>

namespace lcs {
// This is bookkeeping, not a native CPed field or a task-status translator.
// The caller must resolve a live ped before accepting a command or querying.
struct ObjectiveActor {
    int32_t handle=-1;
    uintptr_t native_ped=0;
    bool operator==(const ObjectiveActor& other)const {
        return handle==other.handle && native_ped==other.native_ped;
    }
};
struct ObjectiveReceipt {
    ObjectiveActor actor;
    uint64_t revision=0;
    uint16_t original_opcode=0;
};
class ObjectiveReceipts {
    struct Entry {
        ObjectiveReceipt receipt;
        bool completed=false;
        bool monitoring=true;
    };
    std::unordered_map<int32_t,Entry> entries_;
    uint64_t next_revision_=1;
    static bool valid(ObjectiveActor actor) {
        return actor.handle>=0 && !(uint32_t(actor.handle)&0x80u) && actor.native_ped;
    }
    Entry* match(const ObjectiveReceipt& receipt) {
        const auto found=entries_.find(receipt.actor.handle);
        if(found==entries_.end())return nullptr;
        auto& entry=found->second;
        return entry.receipt.actor==receipt.actor && entry.receipt.revision==receipt.revision &&
            entry.receipt.original_opcode==receipt.original_opcode?&entry:nullptr;
    }
public:
    // Call AFTER the real objective command was accepted, never on dispatch,
    // a pre-entry refusal, or an entered fault. resets_latch must come from
    // the reviewed original handler, not a generic task naming convention.
    std::optional<ObjectiveReceipt> accepted(ObjectiveActor actor,uint16_t opcode,bool resets_latch) {
        if(!valid(actor) || !opcode || !next_revision_)return {};
        const auto found=entries_.find(actor.handle);
        const bool known=found!=entries_.end() && found->second.receipt.actor==actor;
        // An unreviewed/non-reset command cannot initialize an unknown latch.
        if(!resets_latch && !known)return {};
        const bool completed=!resets_latch && found->second.completed;
        ObjectiveReceipt receipt{actor,next_revision_,opcode};
        entries_.insert_or_assign(actor.handle,Entry{receipt,completed,true});
        // A newly resolved live generation at the same pool address proves
        // earlier generations there are retired, even before their cleanup
        // callback arrives. Never let their queued result complete anything.
        for(auto it=entries_.begin();it!=entries_.end();) {
            if(it->first!=actor.handle && it->second.receipt.actor.native_ped==actor.native_ped)
                it=entries_.erase(it);
            else ++it;
        }
        next_revision_=next_revision_==std::numeric_limits<uint64_t>::max()?0:next_revision_+1;
        return receipt;
    }
    // The caller needs a reviewed original completion condition observed in
    // its real consumer. A missing/FLUSHED SA task is never such evidence.
    bool observed_completion(const ObjectiveReceipt& receipt) {
        const auto entry=match(receipt);
        if(!entry || !entry->monitoring)return false;
        entry->completed=true;return true;
    }
    // Stop callbacks without resetting the original non-consuming latch.
    bool stop_monitoring(const ObjectiveReceipt& receipt) {
        const auto entry=match(receipt);
        if(!entry)return false;
        entry->monitoring=false;return true;
    }
    std::optional<bool> latch(ObjectiveActor actor)const {
        const auto found=entries_.find(actor.handle);
        if(!valid(actor) || found==entries_.end() || !(found->second.receipt.actor==actor))return {};
        return found->second.completed;
    }
    void retired(ObjectiveActor actor) {
        const auto found=entries_.find(actor.handle);
        if(found!=entries_.end() && found->second.receipt.actor==actor)entries_.erase(found);
    }
    // Keep the revision sequence: queued old callbacks cannot match a receipt
    // issued after a world/mission reset in the same ledger instance.
    void clear(){entries_.clear();}
};
}
