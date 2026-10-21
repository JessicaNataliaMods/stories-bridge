#pragma once
#include "random_cop_policy.hpp"
#include "sa_executor.hpp"
namespace lcs {
class SourceCopModelMap {
    std::array<uint16_t,12> native_{};
    explicit SourceCopModelMap(std::array<uint16_t,12> ids):native_(ids){}
public:
    // Caller supplies the approved canonical bindings, not appearance guesses.
    static std::optional<SourceCopModelMap> approved(const std::array<int32_t,12>& ids) {
        std::array<uint16_t,12> result{};
        for(size_t i=0;i<ids.size();++i) {
            if(ids[i]<0 || ids[i]>65535)return {};
            for(size_t j=0;j<i;++j)if(ids[i]==ids[j])return {};
            result[i]=uint16_t(ids[i]);
        }
        return SourceCopModelMap(result);
    }
    std::optional<int32_t> original(uint16_t native)const {
        for(size_t i=0;i<native_.size();++i)if(native_[i]==native)return i<4?int32_t(i+1):int32_t(97+i-4);
        return {};
    }
};
inline std::optional<int32_t> original_cop_state(int32_t native) {
    // Primary SA/reLCS enums share 0..55. SA inserted DIE_BY_STEALTH at 56;
    // subsequent states shift by one. No source state is invented for 56.
    if(native>=0 && native<=55)return native;
    if(native>=57 && native<=64)return native-1;
    return {};
}
inline std::optional<OriginalCopCandidate> source_cop_candidate(const NativeCopSnapshot& n,const SourceCopModelMap& models) {
    const auto model=models.original(n.native_model),state=original_cop_state(n.native_state);
    if(!model || !state || n.actor.handle<0 || !n.actor.native_ped)return {};
    if(n.group.member && !n.group.leader_ped)return {}; // no automatic takeover of leaderless native groups
    OriginalCopCandidate result;
    result.handle=n.actor.handle;result.original_model=*model;result.original_state=*state;
    result.ped_type=n.native_type;result.created_by=n.created_by;result.position=n.position;
    // SA IsPedInControl admits more states than retail LCS. The source requires
    // state <39, positive health and neither airborne nor landing. Native pose
    // already verifies the latter gates, but the state restriction is essential.
    result.controllable=n.controllable && *state<39;
    result.removed=n.removed;result.fading=n.fading;
    result.leader=n.group.leader;result.has_leader=n.group.member && n.group.leader_ped && !n.group.leader;
    return result;
}
}
