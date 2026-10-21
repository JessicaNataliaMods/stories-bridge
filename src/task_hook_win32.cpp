#include <windows.h>
#include "lcs/task_hook_win32.hpp"

namespace lcs {
namespace {
bool region(uint32_t address,size_t length,bool executable) {
    if(!address || !length || uint64_t(address)+length>uint64_t(UINT32_MAX)+1)return false;
    MEMORY_BASIC_INFORMATION info{};
    if(!VirtualQuery(reinterpret_cast<const void*>(address),&info,sizeof(info)) || info.State!=MEM_COMMIT ||
       (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
    const auto start=reinterpret_cast<uintptr_t>(info.BaseAddress);
    if(address<start || uint64_t(address)+length>uint64_t(start)+info.RegionSize)return false;
    const auto access=info.Protect&0xFFu;
    return !executable || access==PAGE_EXECUTE || access==PAGE_EXECUTE_READ ||
        access==PAGE_EXECUTE_READWRITE || access==PAGE_EXECUTE_WRITECOPY;
}
}
bool TaskHookWin32Memory::read(uint32_t address,uint8_t* bytes,size_t length) {
    if(!bytes || !region(address,length,false))return false;
    __try {std::memcpy(bytes,reinterpret_cast<const void*>(address),length);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
uint32_t TaskHookWin32Memory::allocate(size_t length) {
    if(!quiescent_ || !quiescent_() || allocation_ || !length || length>4096)return 0;
    auto* memory=VirtualAlloc(nullptr,length,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    if(!memory)return 0;
    const auto address=reinterpret_cast<uintptr_t>(memory);
    if(address>UINT32_MAX || uint64_t(address)+length>uint64_t(UINT32_MAX)+1){VirtualFree(memory,0,MEM_RELEASE);return 0;}
    allocation_=uint32_t(address);allocation_size_=length;return allocation_;
}
bool TaskHookWin32Memory::publish(uint32_t address,const uint8_t* bytes,size_t length) {
    if(!quiescent_ || !quiescent_() || !allocation_ || address!=allocation_ ||
       length!=allocation_size_ || !bytes || !region(address,length,false))return false;
    DWORD previous=0;
    __try {
        std::memcpy(reinterpret_cast<void*>(address),bytes,length);
        if(!VirtualProtect(reinterpret_cast<void*>(address),length,PAGE_EXECUTE_READ,&previous))return false;
        return FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<const void*>(address),length)!=0;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
TaskHookMemory::Write TaskHookWin32Memory::exchange(uint32_t address,const uint8_t* expected,const uint8_t* desired,size_t length) {
    if(!quiescent_ || !quiescent_() || !expected || !desired || !length || length>11 || !region(address,length,true))return Write::unchanged;
    DWORD previous=0;bool writable=false,attempted=false;
    __try {
        if(std::memcmp(reinterpret_cast<const void*>(address),expected,length))return Write::unchanged;
        if(!VirtualProtect(reinterpret_cast<void*>(address),length,PAGE_EXECUTE_READWRITE,&previous))return Write::unchanged;
        writable=true;
        // Recheck after the protection change; host exclusion remains required.
        if(std::memcmp(reinterpret_cast<const void*>(address),expected,length)) {
            DWORD ignored=0;
            return VirtualProtect(reinterpret_cast<void*>(address),length,previous,&ignored)?Write::unchanged:Write::uncertain;
        }
        attempted=true;std::memcpy(reinterpret_cast<void*>(address),desired,length);
        const bool flushed=FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<const void*>(address),length)!=0;
        DWORD ignored=0;const bool restored=VirtualProtect(reinterpret_cast<void*>(address),length,previous,&ignored)!=0;
        return flushed && restored?Write::written:Write::uncertain;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        DWORD ignored=0;
        const bool restored=!writable || VirtualProtect(reinterpret_cast<void*>(address),length,previous,&ignored)!=0;
        return attempted || !restored?Write::uncertain:Write::unchanged;
    }
}
void TaskHookWin32Memory::release(uint32_t address) {
    if(!quiescent_ || !quiescent_() || !allocation_ || allocation_!=address)return;
    if(VirtualFree(reinterpret_cast<void*>(address),0,MEM_RELEASE)){allocation_=0;allocation_size_=0;}
}
}
