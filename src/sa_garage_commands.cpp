#include <windows.h>
#include "lcs/sa_executor.hpp"
#include "lcs/native_memory.hpp"
#include <cstring>
#include <sstream>

namespace lcs {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
bool native_garage_contains(uintptr_t garage,uintptr_t entity,float radius);
#else
static bool native_garage_contains(uintptr_t garage,uintptr_t entity,float radius) {
    return reinterpret_cast<bool(__thiscall*)(uintptr_t,uintptr_t,float)>(0x448BE0)(garage,entity,radius);
}
#endif
namespace {
template<size_t N> bool profile(uintptr_t address,const uint8_t (&bytes)[N]) {
    return std::memcmp(reinterpret_cast<const void*>(native_entity_address(address)),bytes,N)==0;
}
bool pool_profile(bool peds) {
    const uint8_t vehicle[]={0x8B,0x4C,0x24,0x04,0x8B,0x15,0x94,0x44,0xB7,0,0x53,0x56,0x8B,0x72,0x04,0x8B,0xC1,0xC1,0xF8,0x08,0x8A,0x1C,0x30,0x5E,0x3A,0xD9,0x5B,0x75,0x0B,0x8B,0x0A,0x69,0xC0,0x18,0x0A,0,0};
    const uint8_t ped[]={0x8B,0x4C,0x24,0x04,0x8B,0x15,0x90,0x44,0xB7,0,0x53,0x56,0x8B,0x72,0x04,0x8B,0xC1,0xC1,0xF8,0x08,0x8A,0x1C,0x30,0x5E,0x3A,0xD9,0x5B,0x75,0x0B,0x8B,0x0A,0x69,0xC0,0xC4,0x07,0,0};
    return peds?profile(0x54FF90,ped):profile(0x54FFF0,vehicle);
}
uintptr_t entity(int32_t handle,bool peds) {
    if(!pool_profile(peds))return 0;
    const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(peds?0xB74490:0xB74494));if(!pool)return 0;
    const auto data=*reinterpret_cast<const uintptr_t*>(pool),tags=*reinterpret_cast<const uintptr_t*>(pool+4);
    const auto count=*reinterpret_cast<const int32_t*>(pool+8);
    if(!data || !tags || count>0x7FFFFF || !valid_entity_slot(handle,count,reinterpret_cast<const uint8_t*>(tags)))return 0;
    const auto offset=uintptr_t(uint32_t(handle)>>8)*(peds?0x7C4:0xA18);if(data+offset<data)return 0;
    const auto found=data+offset;
    return (*reinterpret_cast<const uint8_t*>(found+0x36)&7)==(peds?3:2)?found:0;
}
uintptr_t player() {
    const auto focus=*reinterpret_cast<const uint8_t*>(native_entity_address(0xB7CD74));if(focus>1)return 0;
    const auto ped=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB7CD98)+focus*0x190);
    const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74490));if(!ped || !pool)return 0;
    const auto data=*reinterpret_cast<const uintptr_t*>(pool),tags=*reinterpret_cast<const uintptr_t*>(pool+4);
    const auto count=*reinterpret_cast<const int32_t*>(pool+8);if(!data || !tags || count<=0 || count>0x7FFFFF || ped<data)return 0;
    const auto delta=ped-data,slot=delta/0x7C4;if(delta%0x7C4 || slot>=uint32_t(count))return 0;
    const auto generation=*reinterpret_cast<const uint8_t*>(tags+slot);
    return entity(int32_t((uint32_t(slot)<<8)|generation),true)==ped && *reinterpret_cast<const uintptr_t*>(ped+0x480)?ped:0;
}
uintptr_t garage(const char* label) {
    const auto count=*reinterpret_cast<const int32_t*>(native_entity_address(0x96C024));if(count<0 || count>50)return 0;
    uintptr_t found=0;
    for(int32_t i=0;i<count;++i) {
        const auto address=native_entity_address(0x96C048+i*0xD8);
        if(std::memcmp(reinterpret_cast<const void*>(address+0x44),label,8)!=0)continue;
        // Duplicate names make an external native binding ambiguous.
        if(found || !*reinterpret_cast<const uint8_t*>(address+0x4C))return 0;
        found=address;
    }
    return found;
}
struct Receipt {bool ok=false,entered=false,condition=false;DWORD exception=0;uintptr_t fault=0;};
int capture_fault(EXCEPTION_POINTERS* exception,Receipt* result) {
    result->exception=exception->ExceptionRecord->ExceptionCode;
    if(result->exception==EXCEPTION_ACCESS_VIOLATION && exception->ExceptionRecord->NumberParameters>=2)
        result->fault=exception->ExceptionRecord->ExceptionInformation[1];
    return EXCEPTION_EXECUTE_HANDLER;
}
void invoke(const NativeCall* call,const char* label,Receipt* result) {
    __try {
        const auto action=*call->source_garage;
        if(action==GarageCommand::free_resprays) {
            // Retail 0335 stores (parameter != 0) in RespraysAreFree. Keep
            // NoResprays and the neighbouring bombshop policy independent.
            const uint8_t free_resprays[]={0x39,0x3D,0x78,0x3C,0xA4,0,0x0F,0x95,0xC0,0xA2,0x09,0xC0,0x96,0};
            if(!profile(0x481811,free_resprays))return;
            result->entered=true;
            *reinterpret_cast<int8_t*>(native_entity_address(0x96C009))=call->inputs[0]!=0;
            result->ok=true;return;
        }
        if(action==GarageCommand::free_bombs) {
            const uint8_t free_bombs[]={0xA0,0x0A,0xC0,0x96,0,0x84,0xC0,0x75,0x52};
            if(!profile(0x44B49D,free_bombs))return;
            result->entered=true;*reinterpret_cast<int8_t*>(native_entity_address(0x96C00A))=call->inputs[0]!=0;
            result->ok=true;return;
        }
        if(action==GarageCommand::car_delivered || action==GarageCommand::respray_happened || action==GarageCommand::contains_car) {
            const uint8_t delivered[]={0x0F,0xBF,0x44,0x24,0x04,0x69,0xC0,0xD8,0,0,0,0x8A,0x90,0x95,0xC0,0x96,0,0x33,0xC9,0x80,0xFA,0x05,0x0F,0x94,0xC1,0x8A,0xC1,0xC3};
            const uint8_t respray[]={0x0F,0xBF,0x54,0x24,0x04,0x69,0xD2,0xD8,0,0,0,0x8A,0x8A,0x96,0xC0,0x96,0,0x8D,0x92,0x96,0xC0,0x96,0,0x8A,0xC1,0xC0,0xE8,0x02,0x80,0xE1,0xFB,0x24,0x01,0x88,0x0A,0xC3};
            const uint8_t contains[]={0x83,0xEC,0x10,0x53,0x55,0x8B,0x6C,0x24,0x1C,0x8B,0xD1,0x8B,0x4D,0x14,0x85,0xC9,0x56,0x57,0x89,0x54,0x24,0x10,0x8D,0x41,0x30,0x75,0x03,0x8D,0x45,0x04};
            if(!profile(0x447C90,delivered) || !profile(0x447E10,respray))return;
            const auto registered=garage(label);if(!registered)return;
            if(action==GarageCommand::car_delivered) {
                const auto state=*reinterpret_cast<const uint8_t*>(registered+0x4D);if(state>5)return;
                result->condition=state==5;
            } else if(action==GarageCommand::respray_happened) {
                auto* flags=reinterpret_cast<uint8_t*>(registered+0x4E);const auto current=*flags;
                result->condition=(current&4)!=0;result->entered=true;*flags=uint8_t(current&~4u);
            } else {
                if(!profile(0x448BE0,contains))return;
                const auto car=entity(call->inputs[0],false);if(!car)return;
                // The native geometry routine dereferences the collision model.
                // Guard missing native model data before entering it.
                const auto id=*reinterpret_cast<const uint16_t*>(car+0x22);if(id>=20000)return;
                const auto model=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xA9B0C8)+id*4);
                if(!model || !*reinterpret_cast<const uintptr_t*>(model+0x14))return;
                const auto* geometry=reinterpret_cast<const float*>(registered);
                for(size_t i=0;i<14;++i)if(!std::isfinite(geometry[i]))return;
                result->entered=true;result->condition=native_garage_contains(registered,car,0.f);
            }
            result->ok=true;return;
        }
        const uint8_t bomb_field[]={0x56,0x8B,0xF1,0x8A,0x86,0xA8,0x04,0,0,0x8A,0xC8,0x80,0xE1,0x07,0x80,0xF9,0x01};
        const uint8_t bomb_owner[]={0x8B,0x8E,0xCC,0x04,0,0,0x85,0xC9,0x8D,0x86,0xE0,0x04,0,0,0x66,0xC7,0x86,0xDE,0x04,0,0,0xE8,0x03,0x89,0x08};
        if(!profile(0x6D24F0,bomb_field) || !profile(0x6D2593,bomb_owner))return;
        const auto car=entity(call->inputs[0],false);if(!car)return;
        const auto kind=*reinterpret_cast<const int32_t*>(car+0x590);
        if(!((kind>=0 && kind<=2) || kind==9 || kind==10))return;
        auto* flags=reinterpret_cast<uint8_t*>(car+0x4A8);auto* owner=reinterpret_cast<uintptr_t*>(car+0x4CC);
        const auto current=*flags;
        if(action==GarageCommand::car_bomb_type){result->condition=(current&7)==call->inputs[1];result->ok=true;return;}
        if(action==GarageCommand::disarm_car_bomb) {
            // Original disarm does not alter an existing fuse, damage owner,
            // lights or winch. An already empty bomb leaves its owner untouched.
            if(current&7){result->entered=true;*flags=uint8_t(current&~7u);*owner=0;}
        } else {
            const auto installer=player();if(!installer)return;
            result->entered=true;*flags=uint8_t((current&~7u)|call->inputs[1]);*owner=installer;
        }
        result->ok=true;
    } __except(capture_fault(GetExceptionInformation(),result)){}
}
}
bool sa_garage_vehicle_slice(uint32_t slice,uint32_t slices,std::vector<int32_t>& handles) {
    handles.clear();
    if(!slices || slices>128 || slice>=slices || !sa_executor_profile_matches())return false;
    __try {
        if(!pool_profile(false))return false;
        const auto pool=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74494));
        if(!pool)return false;
        const auto objects=*reinterpret_cast<const uintptr_t*>(pool);
        const auto flags=*reinterpret_cast<const uintptr_t*>(pool+4);
        const auto count=*reinterpret_cast<const int32_t*>(pool+8);
        if(!objects || !flags || count<0 || count>100000)return false;
        const auto begin=uint32_t(uint64_t(count)*slice/slices),end=uint32_t(uint64_t(count)*(slice+1)/slices);
        for(auto slot=begin;slot<end;++slot) {
            const auto tag=*reinterpret_cast<const uint8_t*>(flags+slot);
            if(tag&0x80)continue;
            const auto handle=int32_t((slot<<8)|tag);
            if(!entity(handle,false)) {handles.clear();return false;}
            handles.push_back(handle);
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {handles.clear();return false;}
}
NativeResult sa_execute_garage_command(const NativeCall& call) {
    NativeResult result;
    if(!call.source_garage || call.source_entity_command || call.source_statistic || call.source_frenzy_headshots || call.source_environment || call.source_traffic ||
       call.opcode || call.outputs || call.replacement_text || call.mission_flag_reference || call.suppress_previous_brief ||
       call.source_wait_in_car || call.source_wander_direction || !sa_executor_profile_matches()) {
        result.diagnostic="Original garage service mode/profile mismatch";return result;
    }
    const auto action=*call.source_garage;
    const bool named=action==GarageCommand::car_delivered || action==GarageCommand::respray_happened || action==GarageCommand::contains_car;
    const bool predicate=named || action==GarageCommand::car_bomb_type;
    const size_t inputs=action==GarageCommand::car_delivered || action==GarageCommand::respray_happened?0:
        action==GarageCommand::arm_car_bomb || action==GarageCommand::car_bomb_type?2:1;
    if(unsigned(action)>unsigned(GarageCommand::free_resprays) || call.predicate!=predicate || call.inputs.size()!=inputs ||
       named!=bool(call.text) || (call.text && (call.text->empty() || call.text->size()>8 || call.text->find('\0')!=std::string::npos)) ||
       (call.text && call.text_position!=0) || ((action==GarageCommand::arm_car_bomb || action==GarageCommand::car_bomb_type) &&
       (call.inputs[1]<0 || call.inputs[1]>5))) {
        result.diagnostic="Original garage operand shape/value mismatch";return result;
    }
    char label[8]{};if(call.text)std::memcpy(label,call.text->data(),call.text->size());
    Receipt receipt;invoke(&call,label,&receipt);
    result.executed=receipt.ok;result.native_entered=receipt.entered;result.condition=receipt.condition;
    if(!result.executed){std::ostringstream message;message<<"Original garage service "<<unsigned(action)<<" refused; exception=0x"<<std::hex<<receipt.exception<<" access=0x"<<receipt.fault;result.diagnostic=message.str();}
    return result;
}
}
