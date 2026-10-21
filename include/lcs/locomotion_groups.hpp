#pragma once
#include <cstdint>
namespace lcs {
// Retail LCS walk-cycle IDs. Other source groups are weapons, gestures or
// cutscenes: do not substitute a walk cycle merely because an ID is in range.
inline const char* original_locomotion_group(int32_t group) {
    if(group==0)return "man";
    if(group==31)return "player";
    static constexpr const char* styles[]={"shuffle","oldman","gang1","gang2",
        "fatman","oldfatman","jogger","woman","shopping","busywoman",
        "sexywoman","fatwoman","oldwoman","jogwoman","panicchunky","skate"};
    return group>=38 && group<=53?styles[group-38]:nullptr;
}
}
