#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
namespace lcs {
// Reviewed AddEntityToList layout. Snapshot copying/profile checks and actor
// lifetime checks belong to the native caller; this policy never reads memory.
struct MissionCleanupSnapshot {
    static constexpr size_t slots=75, stride=8, count_offset=slots*stride;
    std::array<uint8_t,count_offset+1> bytes{};
    uint8_t type(size_t slot)const {return bytes[slot*stride];}
    int32_t handle(size_t slot)const {
        const size_t p=slot*stride+4;
        const uint32_t word=uint32_t(bytes[p]) | uint32_t(bytes[p+1])<<8 |
            uint32_t(bytes[p+2])<<16 | uint32_t(bytes[p+3])<<24;
        return static_cast<int32_t>(word);
    }
};
struct MissionCleanupReservation {size_t slot;uint8_t count;int32_t handle;};
inline std::optional<MissionCleanupReservation> reserve_char_cleanup(
        const MissionCleanupSnapshot& before,int32_t handle) {
    if(handle<0)return {};
    size_t occupied=0,free=MissionCleanupSnapshot::slots;
    for(size_t i=0;i<MissionCleanupSnapshot::slots;++i) {
        if(before.type(i)==0) {if(free==MissionCleanupSnapshot::slots)free=i;}
        else {
            ++occupied;
            if(before.type(i)==2 && before.handle(i)==handle)return {};
        }
    }
    if(occupied!=before.bytes[MissionCleanupSnapshot::count_offset] ||
            free==MissionCleanupSnapshot::slots)return {};
    return MissionCleanupReservation{free,uint8_t(occupied),handle};
}
inline bool char_cleanup_inserted(const MissionCleanupSnapshot& before,
        const MissionCleanupSnapshot& after,const MissionCleanupReservation& reservation) {
    const auto reviewed=reserve_char_cleanup(before,reservation.handle);
    if(!reviewed || reviewed->slot!=reservation.slot || reviewed->count!=reservation.count)return false;
    auto expected=before;
    const size_t p=reservation.slot*MissionCleanupSnapshot::stride;
    expected.bytes[p]=2;
    const uint32_t handle=uint32_t(reservation.handle);
    for(size_t j=0;j<4;++j)expected.bytes[p+4+j]=uint8_t(handle>>(j*8));
    expected.bytes[MissionCleanupSnapshot::count_offset]=uint8_t(reservation.count+1);
    // A return alone proves nothing: full native lists silently refuse. Any
    // callback-time unrelated table mutation also refuses this receipt.
    return expected.bytes==after.bytes;
}
// Unreserved source AddEntityToList(handle,CLEANUP_CHAR), as called by 1494:
// no duplicate check, first free slot, and a full list silently refuses.
// Returns the exact table the native writer must leave behind.
inline std::optional<MissionCleanupSnapshot> expected_source_char_cleanup(
        const MissionCleanupSnapshot& before,int32_t handle) {
    if(handle<0)return {};
    size_t occupied=0,free=MissionCleanupSnapshot::slots;
    for(size_t i=0;i<MissionCleanupSnapshot::slots;++i) {
        if(before.type(i)==0) {if(free==MissionCleanupSnapshot::slots)free=i;}
        else ++occupied;
    }
    if(occupied!=before.bytes[MissionCleanupSnapshot::count_offset])return {};
    auto expected=before;
    if(free==MissionCleanupSnapshot::slots)return expected;
    const size_t p=free*MissionCleanupSnapshot::stride;
    expected.bytes[p]=2;
    const uint32_t word=uint32_t(handle);
    for(size_t j=0;j<4;++j)expected.bytes[p+4+j]=uint8_t(word>>(j*8));
    expected.bytes[MissionCleanupSnapshot::count_offset]=uint8_t(occupied+1);
    return expected;
}
}
