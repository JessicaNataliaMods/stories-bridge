#include <windows.h>
#include "lcs/task_hook_callbacks.hpp"
namespace lcs {namespace {
uint32_t targets[11]{};
TaskHookSink* observer=nullptr;
void suspend()noexcept {
    auto* old=observer;observer=nullptr;
    __try {if(old)old->observation_lost();}
    __except(EXCEPTION_EXECUTE_HANDLER){}
}
uint64_t begin(size_t site,uint32_t self,uint32_t argument) {
    __try {return observer?observer->begin(site,self,argument):0;}
    __except(EXCEPTION_EXECUTE_HANDLER){suspend();return 0;}
}
void end(uint64_t token,bool returned) {
    __try {if(observer && token)observer->end(token,returned);}
    __except(EXCEPTION_EXECUTE_HANDLER){suspend();}
}
void __cdecl flush(uint32_t record) {
    __try {if(observer)observer->inline_flush(record);}
    __except(EXCEPTION_EXECUTE_HANDLER){suspend();}
}
uint32_t __fastcall constructor(uint32_t self,void*) {
    const auto token=begin(0,self,0);uint32_t result=0;
    __try {result=reinterpret_cast<uint32_t(__thiscall*)(uint32_t)>(targets[0])(self);}
    __finally {end(token,!AbnormalTermination());}
    return result;
}
void __fastcall clear(uint32_t self,void*) {
    const auto token=begin(1,self,0);
    __try {reinterpret_cast<void(__thiscall*)(uint32_t)>(targets[1])(self);}
    __finally {end(token,!AbnormalTermination());}
}
template<size_t Site> void __fastcall set(uint32_t self,void*,uint32_t ped,int32_t opcode,uint32_t value) {
    const auto token=begin(Site,self,value);
    __try {reinterpret_cast<void(__thiscall*)(uint32_t,uint32_t,int32_t,uint32_t)>(targets[Site])(self,ped,opcode,value);}
    __finally {end(token,!AbnormalTermination());}
}
template<size_t Site> void __fastcall associate(uint32_t self,void*,uint32_t value) {
    const auto token=begin(Site,self,value);
    __try {reinterpret_cast<void(__thiscall*)(uint32_t,uint32_t)>(targets[Site])(self,value);}
    __finally {end(token,!AbnormalTermination());}
}
void __cdecl release_task(uint32_t task) {
    const auto token=begin(8,task,0);
    __try {reinterpret_cast<void(__cdecl*)(uint32_t)>(targets[8])(task);}
    __finally {end(token,!AbnormalTermination());}
}
void __fastcall destroy_event(uint32_t self,void*) {
    const auto token=begin(10,self,0);
    __try {reinterpret_cast<void(__thiscall*)(uint32_t)>(targets[10])(self);}
    __finally {end(token,!AbnormalTermination());}
}
// Inline Process has no function ABI. Preserve general/flags and x87/MMX/SSE,
// notify using ESI-12, then execute the original copied stores exactly once.
__declspec(naked) void process_flush() {
    __asm {
        pushfd
        pushad
        sub esp,544
        lea eax,[esp+15]
        and eax,0FFFFFFF0h
        mov [esp+528],eax
        fxsave [eax]
        mov eax,[esp+548]
        sub eax,12
        push eax
        call flush
        add esp,4
        mov eax,[esp+528]
        fxrstor [eax]
        add esp,544
        popad
        popfd
        jmp dword ptr [targets+36]
    }
}
}
bool configure_task_hook_callbacks(const std::array<uint32_t,11>& trampolines,TaskHookSink* sink) {
    static_assert(sizeof(void*)==4,"Reviewed native callbacks require Win32");
    for(auto target:trampolines)if(!target)return false;
    suspend();
    for(size_t i=0;i<11;++i)targets[i]=trampolines[i];observer=sink;return true;
}
std::array<uint32_t,11> task_hook_callback_addresses() {
    return {{uint32_t(constructor),uint32_t(clear),uint32_t(set<2>),uint32_t(set<3>),uint32_t(set<4>),uint32_t(set<5>),
        uint32_t(associate<6>),uint32_t(associate<7>),uint32_t(release_task),uint32_t(process_flush),uint32_t(destroy_event)}};
}
bool task_hook_callbacks_observing(){return observer!=nullptr;}
bool task_hook_callbacks_attached_to(const TaskHookSink* sink){return observer==sink;}
void suspend_task_hook_observation()noexcept{suspend();}
}
