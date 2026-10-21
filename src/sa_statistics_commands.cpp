#include <windows.h>
#include "lcs/sa_executor.hpp"
#include "lcs/native_memory.hpp"
#include <cstring>
#include <sstream>
namespace lcs {
namespace {
struct StatReceipt {bool ok=false,entered=false;DWORD exception=0;};
void invoke_stat(const NativeCall* call,uint16_t slot,StatReceipt* receipt) {
    __try {
        // Retail native SetStatValue passes through a float and sign-extends
        // a 16-bit conversion. That loses original integer taxi totals. Match
        // CStats' source update on the actual saved 32-bit SA counter instead.
        const uint8_t get[]={0x66,0x8B,0x44,0x24,0x04,0x66,0x3D,0x52,0,0x73,0x0B,0x0F,0xB7,0xC0,0xD9,0x04,0x85,0x80,0x93,0xB7,0,0xC3,0x0F,0xB7,0xC8,0xDB,0x04,0x8D,0x20,0x8E,0xB7,0,0xC3};
        if(std::memcmp(reinterpret_cast<const void*>(native_entity_address(0x558E40)),get,sizeof(get)))return;
        auto* counter=reinterpret_cast<int32_t*>(native_entity_address(0xB79000)+slot*4);
        const auto value=source_stat_update(*call->source_statistic,*counter,call->inputs[0]);
        receipt->entered=true;*counter=value;receipt->ok=true;
    }__except(EXCEPTION_EXECUTE_HANDLER){receipt->exception=GetExceptionCode();}
}
}
NativeResult sa_execute_statistic_command(const NativeCall& call) {
    NativeResult result;
    if(!call.source_statistic || call.source_entity_command || call.source_frenzy_headshots || call.source_environment || call.source_garage || call.source_traffic || call.opcode || call.inputs.size()!=1 || call.outputs ||
       call.text || call.replacement_text || call.mission_flag_reference || call.predicate || call.source_wait_in_car ||
       call.source_wander_direction || !sa_executor_profile_matches()) {
        result.diagnostic="Original statistic layout/profile mismatch";return result;
    }
    const auto slot=source_stat_slot(*call.source_statistic);
    if(!slot){result.diagnostic="Original statistic has no matching native counter";return result;}
    StatReceipt receipt;invoke_stat(&call,*slot,&receipt);result.executed=receipt.ok;result.native_entered=receipt.entered;
    if(!result.executed){std::ostringstream error;error<<"Original native statistic rejected; exception=0x"<<std::hex<<receipt.exception;result.diagnostic=error.str();}
    return result;
}
}
