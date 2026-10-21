#pragma once
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>

namespace lcs {
// Preflight for source 1655. This is not an SA vehicle flag writer. The native
// backend must prove a live generation and a genuine reward-flag consumer before
// registering an opcode; the receipt cannot be used to fake such a write.
struct RewardVehicleIdentity {
    int32_t handle=-1;
    uintptr_t native_vehicle=0;
    bool operator==(const RewardVehicleIdentity& other) const {
        return handle==other.handle && native_vehicle==other.native_vehicle;
    }
};
struct RewardVehicleAttempt {
    RewardVehicleIdentity identity;
    uint64_t sequence=0;
    bool enabled=false;
};
class RewardVehicleReceipts {
    struct Entry {RewardVehicleIdentity identity;uint64_t sequence;bool enabled;};
    std::unordered_map<int32_t,Entry> entries_;
    std::unordered_map<int32_t,RewardVehicleAttempt> pending_;
    uint64_t next_sequence_=1;
    static bool valid(RewardVehicleIdentity id) {
        return id.handle>=0 && !(uint32_t(id.handle)&0x80u) && id.native_vehicle;
    }
public:
    // Beginning an attempt never acknowledges a native write.
    std::optional<RewardVehicleAttempt> begin(RewardVehicleIdentity id,bool enabled) {
        if(!valid(id) || !next_sequence_)return {};
        const auto number=next_sequence_;
        next_sequence_=number==std::numeric_limits<uint64_t>::max()?0:number+1;
        pending_.insert_or_assign(id.handle,RewardVehicleAttempt{id,number,enabled});
        return RewardVehicleAttempt{id,number,enabled};
    }
    // A native backend must report acceptance AND re-read the *same* live pool
    // generation. A recycled handle/pointer cannot inherit the previous flag.
    bool confirm(const RewardVehicleAttempt& attempt,RewardVehicleIdentity observed,bool native_accepted) {
        const auto pending=pending_.find(attempt.identity.handle);
        if(pending==pending_.end() || pending->second.sequence!=attempt.sequence || !(pending->second.identity==attempt.identity))return false;
        pending_.erase(pending); // single-use even on native refusal/mismatched identity
        if(!native_accepted || !valid(attempt.identity) || !valid(observed) ||
           !(attempt.identity==observed) || !attempt.sequence)return false;
        const auto found=entries_.find(observed.handle);
        if(found!=entries_.end() && found->second.sequence>=attempt.sequence)return false;
        entries_.insert_or_assign(observed.handle,Entry{observed,attempt.sequence,attempt.enabled});
        for(auto it=entries_.begin();it!=entries_.end();) {
            if(it->first!=observed.handle && it->second.identity.native_vehicle==observed.native_vehicle)it=entries_.erase(it);
            else ++it;
        }
        return true;
    }
    std::optional<bool> confirmed(RewardVehicleIdentity observed) const {
        if(!valid(observed))return {};
        const auto found=entries_.find(observed.handle);
        if(found==entries_.end() || !(found->second.identity==observed))return {};
        return found->second.enabled;
    }
    void retired(RewardVehicleIdentity observed) {
        const auto pending=pending_.find(observed.handle);
        if(pending!=pending_.end() && pending->second.identity==observed)pending_.erase(pending);
        const auto found=entries_.find(observed.handle);
        if(found!=entries_.end() && found->second.identity==observed)entries_.erase(found);
    }
    void clear(){entries_.clear();pending_.clear();}
};
}
