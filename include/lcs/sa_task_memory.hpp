#pragma once
#include <windows.h>
#include "native_memory.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace lcs {
// Set by the process-resident owner after a successful observer installation.
// It must authenticate both the live redirect and the original continuation.
using TaskMemoryOriginalPrefix=bool(*)(uintptr_t,const uint8_t*,size_t);
inline TaskMemoryOriginalPrefix task_memory_original_prefix=nullptr;
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
uintptr_t native_task_limit_adjuster_module();
#endif
namespace task_memory_detail {
inline bool bytes(uintptr_t address,const uint8_t* expected,size_t size) {
    __try {return !std::memcmp(reinterpret_cast<const void*>(address),expected,size) ||
        (task_memory_original_prefix && task_memory_original_prefix(address,expected,size));}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline bool jump(uintptr_t entry,uintptr_t target) {
    __try {
        const auto code=reinterpret_cast<const uint8_t*>(native_entity_address(entry));
        uint32_t delta=0;std::memcpy(&delta,code+1,4);
        return code[0]==0xE9 && uint32_t(native_entity_address(entry)+5+delta)==uint32_t(target);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline bool singleton_profile(uintptr_t address,const uint8_t* expected,size_t size,
                              size_t operand,uintptr_t singleton) {
    __try {
        if(size>64 || operand>size || size-operand<4 || singleton>UINT32_MAX)return false;
        uint8_t adjusted[64]{};std::memcpy(adjusted,expected,size);
        const uint32_t cell=uint32_t(singleton);std::memcpy(adjusted+operand,&cell,4);
        return bytes(address,adjusted,size);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
// Installed OLA build, inspected as PE data. Do not authorize arbitrary JMPs
// or rewrite its hooks. All three entry points must belong to the same module
// and reference its exact CTask pool singleton, including under ASLR.
inline uintptr_t ola_module() {
    __try {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        const auto module=native_task_limit_adjuster_module();
#else
        const auto module=reinterpret_cast<uintptr_t>(GetModuleHandleW(L"III.VC.SA.LimitAdjuster.asi"));
#endif
        if(!module)return 0;
        const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<0x40 || dos->e_lfanew>0x1000)return 0;
        const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(module+dos->e_lfanew);
        if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 ||
           nt->FileHeader.TimeDateStamp!=0x6A37C762 || nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
           nt->OptionalHeader.SizeOfImage!=0x68000)return 0;
        constexpr uint8_t alloc[]={0x8B,0x0D,0,0,0,0,0xE9,0x95,0x39,0,0};
        constexpr uint8_t release[]={0x55,0x8B,0xEC,0xFF,0x75,0x08,0x8B,0x0D,0,0,0,0,0xE8,0x8F,0xFD,0xFF,0xFF,0x5D,0xC3};
        constexpr uint8_t contains[]={0x55,0x8B,0xEC,0x8B,0x55,0x08,0x53,0x56,0x57,0x85,0xD2,0x74,0x2A,0x8B,0x35,
            0,0,0,0,0x33,0xC9,0x8B,0x7E,0x14,0x85,0xFF,0x7E,0x1B,0x8B,0x46,0x18,0x90,0x8B,0x1C,0x88};
        const auto cell=module+0x5F84C;
        return jump(0x61A5A0,module+0x26F60) && jump(0x61A5B0,module+0x27060) &&
            jump(0x61A4B0,module+0x26EB0) &&
            singleton_profile(module+0x26F60,alloc,sizeof(alloc),2,cell) &&
            singleton_profile(module+0x27060,release,sizeof(release),8,cell) &&
            singleton_profile(module+0x26EB0,contains,sizeof(contains),15,cell)?module:0;
    } __except(EXCEPTION_EXECUTE_HANDLER){return 0;}
}
inline bool retail_allocator() {
    constexpr uint8_t code[]={0x8B,0x0D,0xA8,0x44,0xB7,0,0xE9,0x55,0xFF,0xFF,0xFF};
    constexpr uint8_t stride[]={0xC1,0xE0,0x07};
    return bytes(native_entity_address(0x61A5A0),code,sizeof(code)) &&
        bytes(native_entity_address(0x61A55A),stride,sizeof(stride));
}
}
// Live free path owned by the verified Open Limit Adjuster build (0x61A5B0 forwards to it); 0 when not installed.
inline uintptr_t sa_task_ola_release_entry() {
    const auto module=task_memory_detail::ola_module();
    return module?module+0x27060:0;
}
inline bool sa_task_allocator_profile_matches() {
    return task_memory_detail::retail_allocator() || task_memory_detail::ola_module()!=0;
}
inline bool sa_task_release_profile_matches() {
    constexpr uint8_t code[]={0x8B,0x0D,0xA8,0x44,0xB7,0};
    return task_memory_detail::bytes(native_entity_address(0x61A5B0),code,sizeof(code)) ||
        task_memory_detail::ola_module()!=0;
}
// Read-only CTask membership. OLA flags use the concatenated index of all
// blocks, not the index relative to the block containing this task.
inline bool sa_task_slot_live(uintptr_t task) {
    __try {
        if(!task)return false;
        const bool retail=task_memory_detail::retail_allocator();
        const auto ola=retail?0:task_memory_detail::ola_module();
        if(!retail && !ola)return false;
        const auto pool=*reinterpret_cast<const uintptr_t*>(ola?ola+0x5F84C:native_entity_address(0xB744A8));
        if(!pool)return false;
        const auto flags=*reinterpret_cast<const uintptr_t*>(pool+4);
        const auto count=*reinterpret_cast<const int32_t*>(pool+8);
        if(!flags || count<=0 || count>0x7FFFFF)return false;
        if(retail) {
            const auto first=*reinterpret_cast<const uintptr_t*>(pool);
            if(!first || task<first || (task-first)%128)return false;
            const auto slot=(task-first)/128;
            return slot<uint32_t(count) && slot<=UINTPTR_MAX-flags &&
                !(*reinterpret_cast<const uint8_t*>(flags+slot)&0x80u);
        }
        if(*reinterpret_cast<const uintptr_t*>(pool)!=0)return false;
        const auto blocks=*reinterpret_cast<const int32_t*>(pool+0x14);
        const auto begins=*reinterpret_cast<const uintptr_t*>(pool+0x18);
        const auto ends=*reinterpret_cast<const uintptr_t*>(pool+0x1C);
        if(blocks<=0 || blocks>4096 || !begins || !ends)return false;
        uint32_t total=0,selected=UINT32_MAX;
        for(int32_t i=0;i<blocks;++i) {
            const auto begin=*reinterpret_cast<const uintptr_t*>(begins+uintptr_t(i)*4);
            const auto end=*reinterpret_cast<const uintptr_t*>(ends+uintptr_t(i)*4);
            if(!begin || end<=begin || (end-begin)%128)return false;
            const auto slots=(end-begin)/128;
            if(slots>uint32_t(count)-total)return false;
            if(task>=begin && task<end) {
                if((task-begin)%128 || selected!=UINT32_MAX)return false;
                selected=total+uint32_t((task-begin)/128);
            }
            total+=uint32_t(slots);
        }
        return total==uint32_t(count) && selected<total && selected<=UINTPTR_MAX-flags &&
            !(*reinterpret_cast<const uint8_t*>(flags+selected)&0x80u);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
