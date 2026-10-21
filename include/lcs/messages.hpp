#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace lcs {
struct BigMissionMessage {
    std::string key;
    uint32_t duration=0;
    uint16_t style=0; // original zero-based ushort, after script style minus one
    std::array<int32_t,6> numbers{{-1,-1,-1,-1,-1,-1}};
};
struct MissionMessage {
    std::string key;
    uint32_t duration=0;
    uint16_t flags=0;
    std::array<int32_t,6> numbers{{-1,-1,-1,-1,-1,-1}};
    bool soon=true;
    bool queued=false;
    bool previous_brief=true;
    bool subtitle=false;
};
// Native SA tMessage uses 32-bit GXT pointers, not LCS wchar pointers.
// Primary layout: vendor/sa-reference/Messages.h / CMessages::BriefMessages.
struct NativeMessage {
    uint32_t text=0;
    uint16_t flags=0,padding=0;
    uint32_t duration=0,created_at=0;
    std::array<int32_t,6> numbers{{-1,-1,-1,-1,-1,-1}};
    uint32_t replacement=0;
    uint8_t previous_brief=1;
    std::array<uint8_t,3> trailing{};
};
static_assert(sizeof(NativeMessage)==0x30 && offsetof(NativeMessage,numbers)==0x10 &&
              offsetof(NativeMessage,replacement)==0x28 && offsetof(NativeMessage,previous_brief)==0x2C);
inline size_t insert_mission_message(NativeMessage* queue,const NativeMessage& message,bool soon,bool queued=false) {
    // Original AddMessageWithNumber appends to the first unused slot and
    // discards the new message when full, preserving every pending objective.
    if(queued) {
        for(size_t i=0;i<8;++i)if(!queue[i].text){queue[i]=message;return i;}
        return 8;
    }
    // Original AddMessageSoon inserts immediately after the active message,
    // moving pending messages down and dropping only the last when full.
    const size_t slot=soon && queue[0].text?1:0;
    if(slot)for(size_t i=7;i>1;--i)queue[i]=queue[i-1];
    queue[slot]=message;
    return slot;
}
}
