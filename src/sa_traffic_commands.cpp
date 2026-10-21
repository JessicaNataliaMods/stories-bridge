#include <windows.h>
#include "lcs/sa_executor.hpp"
#include "lcs/native_memory.hpp"
#include <cstring>
#include <sstream>

namespace lcs {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
void native_remove_vehicle_cleanup(int32_t handle);
#else
static void native_remove_vehicle_cleanup(int32_t handle) {
    reinterpret_cast<void(__thiscall*)(uintptr_t,int32_t,uint8_t)>(0x4654B0)(0xA90850,handle,1);
}
#endif
namespace {
template<size_t N> bool profile(uintptr_t address,const uint8_t (&bytes)[N]) {
    return !std::memcmp(reinterpret_cast<const void*>(native_entity_address(address)),bytes,N);
}
uintptr_t vehicle(int32_t handle) {
    const uint8_t pool_layout[]={0x8B,0x4C,0x24,0x04,0x8B,0x15,0x94,0x44,0xB7,0,0x53,0x56,0x8B,0x72,0x04,0x8B,0xC1,0xC1,0xF8,0x08,0x8A,0x1C,0x30,0x5E,0x3A,0xD9,0x5B,0x75,0x0B,0x8B,0x0A,0x69,0xC0,0x18,0x0A,0,0};
    if(!profile(0x54FFF0,pool_layout))return 0;
    const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74494));if(!pool)return 0;
    const auto data=*reinterpret_cast<const uintptr_t*>(pool),tags=*reinterpret_cast<const uintptr_t*>(pool+4);
    const auto count=*reinterpret_cast<const int32_t*>(pool+8);
    if(!data || !tags || count>0x7FFFFF || !valid_entity_slot(handle,count,reinterpret_cast<const uint8_t*>(tags)))return 0;
    const auto offset=uintptr_t(uint32_t(handle)>>8)*0xA18;if(data+offset<data)return 0;
    const auto found=data+offset;return (*reinterpret_cast<const uint8_t*>(found+0x36)&7)==2?found:0;
}
struct Receipt {bool ok=false,entered=false;DWORD exception=0;uintptr_t fault=0;};
int capture_fault(EXCEPTION_POINTERS* exception,Receipt* receipt) {
    receipt->exception=exception->ExceptionRecord->ExceptionCode;
    if(receipt->exception==EXCEPTION_ACCESS_VIOLATION && exception->ExceptionRecord->NumberParameters>=2)
        receipt->fault=exception->ExceptionRecord->ExceptionInformation[1];
    return EXCEPTION_EXECUTE_HANDLER;
}
void invoke(const NativeCall* call,Receipt* receipt) {
    __try {
        const auto action=*call->source_traffic;const auto* input=call->inputs.data();
        if(action==TrafficCommand::emergency_vehicles || action==TrafficCommand::cars_around_camera) {
            const uint8_t emergency[]={0xC6,0x05,0x28,0x5B,0x8A,0,0x01};
            const uint8_t camera[]={0x88,0x1D,0xC1,0x90,0x96,0};
            if(!profile(0x421339,emergency) || !profile(0x421346,camera))return;
            const auto address=native_entity_address(action==TrafficCommand::emergency_vehicles?0x8A5B28:0x9690C1);
            receipt->entered=true;*reinterpret_cast<uint8_t*>(address)=input[0]!=0;receipt->ok=true;return;
        }
        if(action==TrafficCommand::drunk_input_delay) {
            const uint8_t pad[]={0x8B,0x44,0x24,0x04,0x69,0xC0,0x34,0x01,0,0,0x05,0x58,0x34,0xB7,0,0xC3};
            const uint8_t delay[]={0x8B,0x44,0x24,0x04,0x89,0x41,0x74,0xC2,0x04,0};
            if(!profile(0x53FB70,pad) || !profile(0x53F910,delay))return;
            receipt->entered=true;*reinterpret_cast<int32_t*>(native_entity_address(0xB73458)+input[0]*0x134+0x74)=input[1];
            receipt->ok=true;return;
        }
        const auto car=vehicle(input[0]);if(!car)return;
        if(action==TrafficCommand::release_vehicle_cleanup) {
            const uint8_t transfer[]={0xE9,0x5B,0x93,0x10,0x01};
            const uint8_t cleanup[]={0x51,0x55,0x56,0x8B,0xE9,0x57,0x8D,0x7D,0x04,0xC7,0x44,0x24,0x0C,0x4B,0,0,0,0x8A,0x47,0xFC,0x3A,0x44,0x24,0x18,0x0F,0x85,0xAB,0,0,0,0x8B,0x0F,0x3B,0x4C,0x24,0x14,0x0F,0x85,0x9F,0,0};
            if(!profile(0x4654B0,transfer) || !profile(0x156E810,cleanup))return;
            const auto list=native_entity_address(0xA90850);
            const auto count=*reinterpret_cast<const uint8_t*>(list+0x258);if(count>75)return;
            size_t occupied=0;for(size_t i=0;i<75;++i)if(*reinterpret_cast<const uint8_t*>(list+i*8))++occupied;
            if(occupied!=count)return;
            // Native removal also releases static-waiting-for-collision and
            // joins the moving list. Clearing only the record would omit that.
            receipt->entered=true;native_remove_vehicle_cleanup(input[0]);receipt->ok=true;return;
        }
        const auto kind=*reinterpret_cast<const int32_t*>(car+0x590),subclass=*reinterpret_cast<const int32_t*>(car+0x594);
        if(kind<0 || kind>2 || subclass!=kind)return;
        if(action==TrafficCommand::tank_detonates_cars) {
            const uint8_t flag[]={0xF6,0x87,0x68,0x08,0,0,0x20};
            if(!profile(0x6AF145,flag))return;
            auto* flags=reinterpret_cast<uint8_t*>(car+0x868);const auto current=*flags;
            receipt->entered=true;*flags=uint8_t((current&~0x20u)|(input[1]?0x20:0));receipt->ok=true;return;
        }
        const uint8_t getter[]={0x0F,0xB6,0x44,0x01,0x09,0xE9,0xD8,0x09,0x2C,0};
        const uint8_t setter[]={0x8B,0x44,0x24,0x04,0x85,0xC0,0x7C,0x0D,0x83,0xF8,0x06,0x7D,0x08,0x8A,0x54,0x24,0x08,0x88,0x54,0x08,0x09,0xC2,0x08,0};
        const uint8_t angular[]={0xC7,0x87,0xE4,0x05,0,0,0,0,0,0xC0};
        if(!profile(0x401860,getter) || !profile(0x6C21C0,setter) || !profile(0x6A4510,angular))return;
        auto* damage=reinterpret_cast<uint8_t*>(car+0x5AA);const auto state=*damage;if(state>4)return;
        // LCS has four door states; SA distinguishes damaged open/closed.
        // Source OK/SMASHED become SWINGING. Already open/missing states
        // are retained. Its angular-speed assignment is unconditional.
        receipt->entered=true;if(state==0 || state==2)*damage=1;
        *reinterpret_cast<float*>(car+0x5E4)=-2.f;receipt->ok=true;
    } __except(capture_fault(GetExceptionInformation(),receipt)){}
}
}
NativeResult sa_execute_traffic_command(const NativeCall& call) {
    NativeResult result;
    if(!call.source_traffic || call.source_entity_command || call.source_statistic || call.source_frenzy_headshots ||
       call.source_environment || call.source_garage || call.opcode || call.outputs || call.predicate || call.text || call.text_position ||
       call.replacement_text || call.mission_flag_reference || call.suppress_previous_brief ||
       call.source_wait_in_car || call.source_wander_direction || !sa_executor_profile_matches()) {
        result.diagnostic="Original traffic/control mode/profile mismatch";return result;
    }
    const auto action=*call.source_traffic;
    const auto inputs=action==TrafficCommand::tank_detonates_cars || action==TrafficCommand::drunk_input_delay?2u:1u;
    if(unsigned(action)>unsigned(TrafficCommand::drunk_input_delay) || call.inputs.size()!=inputs ||
       (action==TrafficCommand::drunk_input_delay && (call.inputs[0]<0 || call.inputs[0]>1 || call.inputs[1]<0 || call.inputs[1]>=10))) {
        result.diagnostic="Original traffic/control operand shape/value mismatch";return result;
    }
    Receipt receipt;invoke(&call,&receipt);result.executed=receipt.ok;result.native_entered=receipt.entered;
    if(!result.executed){std::ostringstream error;error<<"Original traffic/control service "<<unsigned(action)<<" refused; exception=0x"<<std::hex<<receipt.exception<<" access=0x"<<receipt.fault;result.diagnostic=error.str();}
    return result;
}
}
