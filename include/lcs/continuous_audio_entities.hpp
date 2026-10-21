#pragma once
#include "continuous_sound_adapter.hpp"
#include <vector>

namespace lcs {
// Retail238160/238238: audio identities are first-free slots in a separate
// 250-entry table; service order is insertion order, not script-pool order.
// This ledger owns source identities only. A renderer must release its actual
// channels before invoking release; this class never claims driver cleanup.
class ContinuousAudioEntities {
    struct Entry {bool used=false;ContinuousSoundObjects::Snapshot source{};};
    std::array<Entry,250> entries_{};
    std::vector<int32_t> order_;
    static bool same(ContinuousSoundObjects::Token a,ContinuousSoundObjects::Token b){
        return a.reference==b.reference && a.epoch==b.epoch;
    }
public:
    ContinuousAudioEntities(){order_.reserve(entries_.size());}
    ContinuousEntityReceipt create(const ContinuousSoundObjects::Snapshot& source){
        // Duplicate dispatch is refused, rather than allocating a second
        // source entity for the same internal owner after an uncertain call.
        for(auto index:order_)if(same(entries_[size_t(index)].source.token,source.token))return {};
        for(size_t index=0;index<entries_.size();++index){
            auto& entry=entries_[index];if(entry.used)continue;
            entry.source=source;entry.source.object.audio_entity=int32_t(index);
            entry.used=true;order_.push_back(int32_t(index));
            return {true,true,int32_t(index)};
        }
        // Original CreateEntity returns-3 for a full entity table;402 still
        // retains its separate script object and publishes its pool handle.
        return {true,true,-3};
    }
    ContinuousEntityReceipt release(const ContinuousSoundObjects::Snapshot& source){
        const auto identity=source.object.audio_entity;
        if(identity==-3)return {true,true,identity}; // Proven full-table result has no entity.
        if(identity<0 || size_t(identity)>=entries_.size())return {};
        auto& entry=entries_[size_t(identity)];
        if(!entry.used || !same(entry.source.token,source.token))return {};
        for(auto it=order_.begin();it!=order_.end();++it)if(*it==identity){
            order_.erase(it);entry.used=false;return {true,true,identity};
        }
        return {};
    }
    std::vector<ContinuousSoundObjects::Snapshot> service_order()const{
        std::vector<ContinuousSoundObjects::Snapshot> result;result.reserve(order_.size());
        for(auto index:order_)result.push_back(entries_[size_t(index)].source);
        return result;
    }
};
}
