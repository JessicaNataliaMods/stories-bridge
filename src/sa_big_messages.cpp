#include <windows.h>
#include "lcs/sa_executor.hpp"
#include "lcs/native_memory.hpp"
#include <cstring>
namespace lcs {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
const char* native_big_message_text(const char* key);
void native_big_message_display(const char*,uint32_t,uint16_t,const std::array<int32_t,6>&);
#else
static const char* native_big_message_text(const char* key) {
    return reinterpret_cast<const char*(__thiscall*)(uintptr_t,const char*)>(0x6A0050)(0xC1B340,key);
}
static void native_big_message_display(const char* text,uint32_t duration,uint16_t style,const std::array<int32_t,6>& n) {
    // Verified native 036D cleans nine stack arguments (0x24 bytes).
    reinterpret_cast<void(__cdecl*)(const char*,uint32_t,int32_t,int32_t,int32_t,int32_t,int32_t,int32_t,int32_t)>(0x69E5F0)
        (text,duration,style,n[0],n[1],n[2],n[3],n[4],n[5]);
}
#endif
namespace {
struct BigReceipt {bool ok=false,entered=false;DWORD exception=0;};
void invoke_big_message(const BigMissionMessage* r,BigReceipt* receipt) {
    __try {
        const uint8_t lookup[]={0x83,0xEC,0x20,0x56,0x57,0x8B,0x7C,0x24,0x2C,0x8A,0x07,0x3C,0x20,0x8B,0xF1,0xE9,0x53,0x3A,0xD6,0xFF};
        const uint8_t display[]={0x8B,0x4C,0x24,0x14,0x8B,0x54,0x24,0x10,0x81,0xEC,0x20,0x03,0,0};
        const uint8_t call_abi[]={0x48,0x50,0x51,0x57,0xE8,0xDD,0xB2,0x21,0,0x83,0xC4,0x24};
        if(std::memcmp(reinterpret_cast<const void*>(native_entity_address(0x6A0050)),lookup,sizeof(lookup)) ||
           std::memcmp(reinterpret_cast<const void*>(native_entity_address(0x69E5F0)),display,sizeof(display)) ||
           std::memcmp(reinterpret_cast<const void*>(native_entity_address(0x48330A)),call_abi,sizeof(call_abi)))return;
        const auto text=native_big_message_text(r->key.c_str());if(!text || !*text)return;
        // Text lookup is preflight. Once the display helper enters it may
        // mutate a message/style slot even if a later font/helper call faults.
        receipt->entered=true;
        native_big_message_display(text,r->duration,r->style,r->numbers);
        receipt->ok=true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {receipt->exception=GetExceptionCode();}
}
}
NativeResult sa_display_big_message(const BigMissionMessage& request) {
    NativeResult result;
    if(!sa_executor_profile_matches() || request.key.empty() || request.key.size()>8 ||
       request.key.find('\0')!=std::string::npos || request.style>=7) {
        result.diagnostic="Invalid/unverified original big-message key or native style";return result;
    }
    BigReceipt receipt;invoke_big_message(&request,&receipt);
    result.executed=receipt.ok;result.native_entered=receipt.entered;
    if(!receipt.ok)result.diagnostic=receipt.exception?"Native numbered big-message boundary fault":"Numbered big-message ABI or mounted text unavailable";
    return result;
}
}
