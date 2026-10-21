#pragma once
#include <cstdint>
#include <functional>
#include <unordered_map>

namespace lcs {
// The source CPed stores eight damage permissions. A ninth gang has no bit.
// Actor identity includes the pool generation; a recycled address is not the
// actor that received the script command.
class GangDamagePolicy {
    struct Entry {int32_t handle;uint8_t mask=255;};
    std::unordered_map<uintptr_t,Entry> actors_;
public:
    static bool valid_gang(int32_t gang){return uint8_t(gang)<32;}
    bool set(int32_t handle,uintptr_t ped,int32_t gang,bool enabled) {
        if(!ped || !valid_gang(gang))return false;
        for(auto i=actors_.begin();i!=actors_.end();) {
            if(i->second.handle==handle && i->first!=ped)i=actors_.erase(i);else ++i;
        }
        auto found=actors_.find(ped);
        if(found!=actors_.end() && found->second.handle!=handle)actors_.erase(found);
        auto& entry=actors_.try_emplace(ped,Entry{handle,255}).first->second;
        const auto bit=uint8_t(uint32_t(1)<<uint8_t(gang));
        entry.mask=enabled?uint8_t(entry.mask|bit):uint8_t(entry.mask&uint8_t(~bit));
        return true;
    }
    // Only the original melee/instant-hit weapon paths use these permissions.
    // Vehicle impacts, drowning, explosions and unrelated pedestrians retain
    // the native engine's damage rules.
    bool denies(uintptr_t victim,int32_t attacker_type,bool weapon_attack,
                const std::function<uintptr_t(int32_t)>& resolve) {
        const auto entry=actors_.find(victim);if(entry==actors_.end())return false;
        if(!resolve || resolve(entry->second.handle)!=victim){actors_.erase(entry);return false;}
        if(!weapon_attack || attacker_type<7 || attacker_type>15)return false;
        return !(entry->second.mask & (uint32_t(1)<<(attacker_type-7)));
    }
    void clear(){actors_.clear();}
    size_t size()const{return actors_.size();}
};
}
