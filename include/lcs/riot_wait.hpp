#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace lcs {
// reLCS Ped.cpp:PlayRandomAnimationsFromAnimBlock and AnimManager.cpp:aRiot*.
// RIOT is a persistent random partial-animation state, not a timed stand task.
struct RiotWaitCycle {
    static constexpr std::array<std::string_view,7> names{
        "riot_angry","riot_angry_b","riot_chant","riot_punches",
        "riot_shout","riot_challenge","riot_fuku"};
    int previous=-1;
    uint32_t deadline=0;
    int choose(uint32_t now,uint32_t choice,uint32_t duration) {
        const auto next=previous<0 ? int(choice%7) : int(choice%6);
        previous=previous>=0 && next>=previous ? next+1 : next;
        deadline=now+(looping() ? 3000+duration%5000 : 8000);
        return previous;
    }
    bool looping() const {return previous>=0 && previous<5;}
    bool expired(uint32_t now) const {return int32_t(now-deadline)>0;}
    uint16_t flags() const {return uint16_t(1|8|16|(looping()?2:0));}
};
}
