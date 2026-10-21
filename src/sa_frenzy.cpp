#include <windows.h>
#include "lcs/sa_executor.hpp"
#include "lcs/native_memory.hpp"
#include <cstring>
#include <sstream>

namespace lcs {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
// Tests replace only these engine-call boundaries. The production preflight,
// argument layout and receipts run unchanged on owned non-executable pages.
const char* native_frenzy_text(const char* key);
void native_frenzy_start(int32_t,int32_t,uint16_t,int32_t,const char*,int32_t,int32_t,int32_t,bool,bool);
#else
static const char* native_frenzy_text(const char* key) {
    return reinterpret_cast<const char*(__thiscall*)(void*,const char*)>(0x6A0050)(reinterpret_cast<void*>(0xC1B340),key);
}
static void native_frenzy_start(int32_t weapon,int32_t time,uint16_t kills,int32_t model,const char* text,int32_t model2,int32_t model3,int32_t model4,bool standard,bool headshots) {
    reinterpret_cast<void(__cdecl*)(int32_t,int32_t,uint16_t,int32_t,const char*,int32_t,int32_t,int32_t,bool,bool)>(0x43D3B0)(
        weapon,time,kills,model,text,model2,model3,model4,standard,headshots);
}
#endif
namespace {
struct FrenzyReceipt {bool ok=false,entered=false;DWORD exception=0;};
bool profile() {
    const uint8_t start[]={0x56,0x57,0x6A,0,0xE8,0xA7,0x41,0,0,0x8B,0x7C,0x24,0x10,0x83,0xC4,0x04,0x83,0xFF,0x34,0xBE,0x1C,0,0,0,0x74,0x02,0x8B,0xF7,0xE8,0xBF,0x3F,0,0,0x84,0xC0,0x74,0x15,0x6A,0,0x66,0xC7,0x05,0x04,0xA7,0x96,0,0x04,0,0xE8,0xCB,0x54,0,0,0x83,0xC4,0x04,0xEB,0x09,0x66,0xC7,0x05,0x04,0xA7,0x96,0,0x01,0};
    const uint8_t flags[]={0x8A,0x44,0x24,0x2C,0x8A,0x4C,0x24,0x30,0x8B,0x15,0x84,0xCB,0xB7,0,0xA2,0x4A,0x9A,0x96,0,0x88,0x0D,0x49,0x9A,0x96,0};
    const uint8_t status[]={0x66,0xA1,0x04,0xA7,0x96,0,0xC3};
    const uint8_t text[]={0x83,0xEC,0x20,0x56,0x57,0x8B,0x7C,0x24,0x2C,0x8A,0x07,0x3C,0x20,0x8B,0xF1,0xE9,0x53,0x3A,0xD6,0xFF};
    const uint8_t ped[]={0x8B,0x4C,0x24,0x04,0x8B,0x15,0x90,0x44,0xB7,0,0x53,0x56,0x8B,0x72,0x04,0x8B,0xC1,0xC1,0xF8,0x08,0x8A,0x1C,0x30,0x5E,0x3A,0xD9,0x5B,0x75,0x0B,0x8B,0x0A,0x69,0xC0,0xC4,0x07,0,0};
    return !std::memcmp(reinterpret_cast<const void*>(native_entity_address(0x43D3B0)),start,sizeof(start)) &&
        !std::memcmp(reinterpret_cast<const void*>(native_entity_address(0x43D465)),flags,sizeof(flags)) &&
        !std::memcmp(reinterpret_cast<const void*>(native_entity_address(0x43D1E0)),status,sizeof(status)) &&
        !std::memcmp(reinterpret_cast<const void*>(native_entity_address(0x6A0050)),text,sizeof(text)) &&
        !std::memcmp(reinterpret_cast<const void*>(native_entity_address(0x54FF90)),ped,sizeof(ped));
}
bool player_ready() {
    if(*reinterpret_cast<const uint8_t*>(native_entity_address(0xB7CD74))!=0 ||
       *reinterpret_cast<const uintptr_t*>(native_entity_address(0xB7CD98)+0x190))return false;
    const auto player=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB7CD98));
    const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74490));if(!player || !pool)return false;
    const auto first=*reinterpret_cast<const uintptr_t*>(pool),tags=*reinterpret_cast<const uintptr_t*>(pool+4);
    const auto count=*reinterpret_cast<const int32_t*>(pool+8);
    if(!first || !tags || count<=0 || count>0x7FFFFF || player<first)return false;
    const auto delta=player-first,slot=delta/0x7C4;
    return delta%0x7C4==0 && slot<uint32_t(count) &&
        !(*reinterpret_cast<const uint8_t*>(tags+slot)&0x80) &&
        (*reinterpret_cast<const uint8_t*>(player+0x36)&7)==3 &&
        *reinterpret_cast<const uintptr_t*>(player+0x480);
}
void invoke(const NativeCall* call,FrenzyReceipt* receipt) {
    __try {
        if(!profile() || !player_ready())return;
        const auto status=*reinterpret_cast<const uint16_t*>(native_entity_address(0x96A704));
        // Preserve any already-running native rampage, including one from a mod
        // or two-player SA. Neither its counters nor its inventory are replaced.
        if(status==1 || status>3)return;
        const auto text=native_frenzy_text(call->text->c_str());if(!text)return;
        const auto& n=call->inputs;
        receipt->entered=true;
        native_frenzy_start(n[0],n[1],uint16_t(n[2]),n[3],text,n[4],n[5],n[6],n[7]!=0,*call->source_frenzy_headshots);
        if(*reinterpret_cast<const uint16_t*>(native_entity_address(0x96A704))!=1 ||
           *reinterpret_cast<const uint8_t*>(native_entity_address(0x969A49))!=uint8_t(*call->source_frenzy_headshots) ||
           *reinterpret_cast<const uint8_t*>(native_entity_address(0x969A4A))!=uint8_t(n[7]!=0))return;
        constexpr uintptr_t fields[]={0x96A700,0x96A6E8,0x96A6EC,0x96A6FC,0x96A6F8,0x96A6F4,0x96A6F0};
        for(size_t i=0;i<7;++i)if(*reinterpret_cast<const int32_t*>(native_entity_address(fields[i]))!=(i==2?int32_t(uint16_t(n[2])):n[i]))return;
        receipt->ok=true;
    } __except(EXCEPTION_EXECUTE_HANDLER){receipt->exception=GetExceptionCode();}
}
}
NativeResult sa_execute_frenzy_command(const NativeCall& call) {
    NativeResult result;
    if(!call.source_frenzy_headshots || call.opcode || call.inputs.size()!=8 || call.outputs || call.predicate ||
       !call.text || call.text->size()>8 || call.text->find('\0')!=std::string::npos || call.text_position || call.replacement_text ||
       call.source_entity_command || call.source_statistic || call.source_environment || call.source_garage || call.source_traffic || call.source_wait_in_car || call.source_wander_direction ||
       call.mission_flag_reference || call.suppress_previous_brief || !sa_executor_profile_matches()) {
        result.diagnostic="Original rampage operand mode/profile mismatch";return result;
    }
    const auto weapon=call.inputs[0];
    if(weapon<0 || weapon>57 || weapon==47 || weapon==48) {
        result.diagnostic="Rampage weapon selector is outside verified native types";return result;
    }
    for(size_t i=3;i<7;++i)if(call.inputs[i]>=20000) {
        result.diagnostic="Rampage target model exceeds native binding range";return result;
    }
    FrenzyReceipt receipt;invoke(&call,&receipt);result.executed=receipt.ok;result.native_entered=receipt.entered;
    if(!result.executed){std::ostringstream detail;detail<<"Original rampage rejected; exception=0x"<<std::hex<<receipt.exception;result.diagnostic=detail.str();}
    return result;
}
}
