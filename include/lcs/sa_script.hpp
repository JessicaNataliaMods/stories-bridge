#pragma once
#include <cstddef>
#include <cstdint>

namespace lcs {
// Native command packets have private output locals. Mission cleanup (C6)
// registers their entities; mission ownership (DC) instead redirects ALL locals
// to SA's shared A48960 buffer. The LCS VM owns the mission, so DC stays false.
struct SaScript {
    SaScript *next=nullptr,*previous=nullptr;
    char name[8]={'l','c','s','s','a',0,0,0};
    uint8_t *base_ip=nullptr,*ip=nullptr,*stack[8]{};
    uint16_t stack_size=0,pad3a=0;
    int32_t locals[32]{},timers[2]{};
    bool active=true,condition=false,cleanup=false,external=false,text_override=false;
    uint8_t external_type=0xff,padca[2]{};
    uint32_t wake_time=0;
    uint16_t logical=0;
    bool inverted=false,death_check=true,death_executed=false;
    uint8_t padd5[3]{};
    uint8_t* scene_skip=nullptr;
    bool mission=false;
    uint8_t paddd[3]{};

    SaScript(uint8_t* packet,bool mission_cleanup):base_ip(packet),ip(packet),cleanup(mission_cleanup) {}
};
static_assert(sizeof(SaScript)==0xE0 && offsetof(SaScript,ip)==0x14 &&
              offsetof(SaScript,locals)==0x3C && offsetof(SaScript,condition)==0xC5 &&
              offsetof(SaScript,cleanup)==0xC6 && offsetof(SaScript,external_type)==0xC9 &&
              offsetof(SaScript,scene_skip)==0xD8 && offsetof(SaScript,mission)==0xDC,
              "SA 1.0 US CRunningScript ABI; compile for 32 bits");
}
