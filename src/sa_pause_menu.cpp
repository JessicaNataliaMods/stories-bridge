#include <windows.h>
#include "lcs/pause_menu.hpp"
#include "lcs/sa_executor.hpp"
#include <cstring>
namespace lcs {
namespace {
PauseMenuGate gate;
bool installed=false;
uint8_t branch[5]{};
// The verified executable ends CMenuManager::Process with a tail JMP to
// CheckForMenuClosing. ECX contains this; the saved ESI has already been popped.
void __fastcall closing_guard(void* menu,void*) {
    bool delegate=true;
    __try {
        if(reinterpret_cast<uintptr_t>(menu)==0xBA6748) {
            const auto data=static_cast<const uint8_t*>(menu);
            delegate=gate.delegate(data[0x5C]!=0,data[0x32]!=0,data[0x33]!=0,data[0x5F]!=0);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {delegate=true;}
    if(delegate)reinterpret_cast<void(__thiscall*)(void*)>(0x576B70)(menu);
}
bool replace(const uint8_t* expected,const uint8_t* replacement) {
    auto* address=reinterpret_cast<uint8_t*>(native_pause_tail);
    __try {
        if(std::memcmp(address,expected,5)!=0)return false;
        DWORD previous=0;if(!VirtualProtect(address,5,PAGE_EXECUTE_READWRITE,&previous))return false;
        std::memcpy(address,replacement,5);
        FlushInstructionCache(GetCurrentProcess(),address,5);
        DWORD ignored=0;
        if(VirtualProtect(address,5,previous,&ignored))return true;
        std::memcpy(address,expected,5);FlushInstructionCache(GetCurrentProcess(),address,5);
        VirtualProtect(address,5,previous,&ignored);return false;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
}
bool sa_disable_pause_menu(bool disabled) {
    if(!sa_executor_profile_matches())return false;
    if(!disabled) {gate.disabled=false;return true;}
    if(!installed) {
        __try {
            if(std::memcmp(reinterpret_cast<const void*>(native_pause_tail-3),native_pause_tail_bytes.data(),8)!=0)return false;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
        branch[0]=0xE9;
        const auto delta=uint32_t(reinterpret_cast<uintptr_t>(&closing_guard)-(native_pause_tail+5));
        std::memcpy(branch+1,&delta,4);
        if(!replace(native_pause_tail_bytes.data()+3,branch))return false;
        installed=true;
    }else {
        __try {if(std::memcmp(reinterpret_cast<const void*>(native_pause_tail),branch,5)!=0)return false;}
        __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    gate.disabled=true;return true;
}
bool sa_release_pause_menu() {
    gate.disabled=false;
    if(!installed)return true;
    if(!replace(branch,native_pause_tail_bytes.data()+3))return false;
    installed=false;return true;
}
}
