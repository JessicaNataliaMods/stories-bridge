#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
namespace lcs {
struct ThreatRangeSnapshot {
    int32_t handle=0;
    uintptr_t ped=0,intelligence=0;
    std::array<float,2> ranges{};
};
// Scale the native scanner's real hearing/seeing radii. Retain each actor's
// baseline; repeated frames never multiply already scaled values. A changed
// pool tag/pointer is a new actor, and an external range edit becomes its new
// baseline. Writes commit individually so a retry cannot double a partial pass.
class ThreatRangePolicy {
    struct Entry {uintptr_t ped,intelligence;std::array<float,2> base,applied;};
    std::unordered_map<int32_t,Entry> entries_;
    uint16_t multiplier_=1;
public:
    static std::optional<uint16_t> original_multiplier(float value) {
        if(!std::isfinite(value) || value>65535.f)return {};
        return value<1.f?uint16_t(1):uint16_t(value);
    }
    void set(uint16_t multiplier){multiplier_=multiplier;}
    bool active() const{return multiplier_!=1 || !entries_.empty();}
    bool tick(const std::vector<ThreatRangeSnapshot>& snapshots,
              const std::function<bool(const ThreatRangeSnapshot&,const std::array<float,2>&)>& write) {
        std::unordered_set<int32_t> live;bool ok=true;
        for(const auto& snapshot:snapshots) {
            if(!live.insert(snapshot.handle).second || !snapshot.ped || !snapshot.intelligence)return false;
            auto old=entries_.find(snapshot.handle);
            auto base=snapshot.ranges;
            if(old!=entries_.end() && old->second.ped==snapshot.ped && old->second.intelligence==snapshot.intelligence &&
               old->second.applied==snapshot.ranges)base=old->second.base;
            auto wanted=base;bool valid=true;
            for(size_t i=0;i<2;++i){wanted[i]*=multiplier_;if(!std::isfinite(base[i]) || base[i]<0 || !std::isfinite(wanted[i]))valid=false;}
            if(!valid){ok=false;continue;}
            if(wanted!=snapshot.ranges && !write(snapshot,wanted)){ok=false;continue;}
            if(multiplier_==1)entries_.erase(snapshot.handle);
            else entries_[snapshot.handle]={snapshot.ped,snapshot.intelligence,base,wanted};
        }
        for(auto it=entries_.begin();it!=entries_.end();)if(!live.count(it->first))it=entries_.erase(it);else ++it;
        return ok;
    }
};
}
