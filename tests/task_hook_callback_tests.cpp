#include <windows.h>
#include "lcs/task_hook_callbacks.hpp"
#include "lcs/task_observer.hpp"
#include <iostream>
#include <stdexcept>
using namespace lcs;
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static int native_calls=0,observed_site=-1,ends=0,fault=0;
static uint32_t observed_self=0,observed_argument=0,got_self=0,got_ped=0,got_value=0;
static int32_t got_opcode=0;
static bool returned=false,native_fault=false;
static ScriptTaskSnapshot integration_bank{};
struct IntegrationReader:TaskObserverReader {
    bool healthy()const override{return task_hook_callbacks_observing();}
    bool record(size_t slot,ScriptTaskRecord& out)override {if(slot>=128)return false;out=integration_bank[slot];return true;}
    bool event_task(uint32_t event,uint32_t& out)override {if(event!=0xAB00)return false;out=0xBC00;return true;}
};
static void __fastcall native_event_set(uint32_t self,void*,uint32_t ped,int32_t opcode,uint32_t value) {
    ++native_calls;integration_bank[(self-0xC0B1E8)/0x14]={opcode,value,0,ped,0};
}
static void __fastcall native_associate(uint32_t self,void*,uint32_t task) {
    ++native_calls;auto& entry=integration_bank[(self-0xC0B1E8)/0x14];entry.event=0;entry.task=task;entry.status=1;
}
struct Sink:TaskHookSink {
    uint64_t begin(size_t site,uint32_t self,uint32_t argument)override {
        observed_site=int(site);observed_self=self;observed_argument=argument;
        if(fault==1)RaiseException(0xE0123456,0,0,nullptr);
        return site+1;
    }
    void end(uint64_t,bool success)override {++ends;returned=success;if(fault==2)RaiseException(0xE0123456,0,0,nullptr);}
    void inline_flush(uint32_t self)override {
        observed_site=9;observed_self=self;
        __asm {pxor xmm0,xmm0}
        if(fault==3)RaiseException(0xE0123456,0,0,nullptr);
    }
};
static uint32_t __fastcall ctor(uint32_t self,void*){++native_calls;got_self=self;if(native_fault)RaiseException(0xE0765432,0,0,nullptr);return 0xDEADBEEF;}
static void __fastcall clear(uint32_t self,void*){++native_calls;got_self=self;}
static void __fastcall set(uint32_t self,void*,uint32_t ped,int32_t opcode,uint32_t value){++native_calls;got_self=self;got_ped=ped;got_opcode=opcode;got_value=value;}
static void __fastcall associate(uint32_t self,void*,uint32_t value){++native_calls;got_self=self;got_value=value;}
static void __cdecl release(uint32_t self){++native_calls;got_self=self;}
__declspec(naked) static void continuation(){__asm {ret}}
static uint32_t inline_callback=0,captured_eax=0,captured_esi=0,captured_edi=0,captured_ebx=0,captured_flags=0;
__declspec(align(16)) static uint8_t captured_xmm[16];
static float captured_x87=0;
static void inline_call() {
    __asm {
        push ebx
        push esi
        push edi
        mov ebx,0ABCDEF12h
        mov esi,040123Ch
        mov edi,099887766h
        mov eax,011223344h
        pcmpeqd xmm0,xmm0
        fld1
        stc
        call dword ptr [inline_callback]
        mov captured_eax,eax
        mov captured_esi,esi
        mov captured_edi,edi
        mov captured_ebx,ebx
        pushfd
        pop eax
        mov captured_flags,eax
        movdqu captured_xmm,xmm0
        fstp captured_x87
        pop edi
        pop esi
        pop ebx
    }
}
static bool catches_native_fault(uint32_t callback) {
    __try {reinterpret_cast<uint32_t(__thiscall*)(uint32_t)>(callback)(0x2222);return false;}
    __except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode()==0xE0765432;}
}
int main(){try{
    Sink sink;const auto callbacks=task_hook_callback_addresses();
    std::array<uint32_t,11> targets{{uint32_t(ctor),uint32_t(clear),uint32_t(set),uint32_t(set),uint32_t(set),uint32_t(set),uint32_t(associate),uint32_t(associate),uint32_t(release),uint32_t(continuation),uint32_t(clear)}};
    check(configure_task_hook_callbacks(targets,&sink),"All native continuations configured under test exclusion");
    auto invalid=targets;invalid[10]=0;check(!configure_task_hook_callbacks(invalid,&sink),"Partial continuation set refuses configuration without touching active targets");
    check(reinterpret_cast<uint32_t(__thiscall*)(uint32_t)>(callbacks[0])(0x1234)==0xDEADBEEF && got_self==0x1234 && observed_site==0 && returned,"Constructor preserves ECX identity, EAX return and begin/end order");
    for(size_t i:{size_t(1),size_t(10)}) {
        reinterpret_cast<void(__thiscall*)(uint32_t)>(callbacks[i])(0x3456);
        check(got_self==0x3456 && observed_site==int(i) && returned,"Void thiscall Flush/destructor forwards identity and return receipt");
    }
    for(size_t i=2;i<=5;++i) {
        reinterpret_cast<void(__thiscall*)(uint32_t,uint32_t,int32_t,uint32_t)>(callbacks[i])(0x1234,0x5678,-123,0xABCDEF);
        check(got_self==0x1234 && got_ped==0x5678 && got_opcode==-123 && got_value==0xABCDEF && observed_site==int(i) && observed_argument==0xABCDEF,"Three stack arguments forwarded unchanged and cleaned by callee");
    }
    for(size_t i=6;i<=7;++i) {
        reinterpret_cast<void(__thiscall*)(uint32_t,uint32_t)>(callbacks[i])(0x1234,0xCAFEBABE);
        check(got_self==0x1234 && got_value==0xCAFEBABE && observed_argument==0xCAFEBABE && observed_site==int(i),"Event/task association forwards exact native argument");
    }
    reinterpret_cast<void(__cdecl*)(uint32_t)>(callbacks[8])(0xFEED);
    check(got_self==0xFEED && observed_site==8,"Task release keeps cdecl stack argument/cleanup");
    inline_callback=callbacks[9];inline_call();
    check(observed_site==9 && observed_self==0x401230 && captured_eax==0x11223344 && captured_esi==0x40123C && captured_edi==0x99887766 && captured_ebx==0xABCDEF12 && (captured_flags&1),"Inline notification uses ESI-12 and preserves all checked general registers/flags");
    for(auto byte:captured_xmm)check(byte==0xFF,"Inline callback restores clobbered SSE state");
    check(captured_x87==1.f,"Inline callback restores x87 stack");
    native_fault=true;check(catches_native_fault(callbacks[0]) && !returned,"Native exception propagates unchanged and reports abnormal return without fake success");native_fault=false;
    for(int mode:{1,2}) {
        fault=mode;configure_task_hook_callbacks(targets,&sink);const auto before=native_calls;
        check(reinterpret_cast<uint32_t(__thiscall*)(uint32_t)>(callbacks[0])(0x1234)==0xDEADBEEF && native_calls==before+1,"Observer failure cannot prevent/replay native routine or change its return");
        fault=0;const auto finished=ends;reinterpret_cast<uint32_t(__thiscall*)(uint32_t)>(callbacks[0])(0x1234);check(ends==finished && !task_hook_callbacks_observing(),"Faulted observer remains disabled; controller health gate refuses all ownership until explicit reconfiguration");
    }
    fault=3;configure_task_hook_callbacks(targets,&sink);inline_call();check(captured_x87==1.f && (captured_flags&1),"Inline observer fault contained while original register state continues");fault=0;
    configure_task_hook_callbacks(targets,nullptr);check(reinterpret_cast<uint32_t(__thiscall*)(uint32_t)>(callbacks[0])(0x1234)==0xDEADBEEF,"Empty observer still delegates native behavior");
    IntegrationReader integration_reader;ScriptTaskObserver lifecycle(integration_reader);
    auto integration_targets=targets;integration_targets[2]=uint32_t(native_event_set);integration_targets[7]=uint32_t(native_associate);
    configure_task_hook_callbacks(integration_targets,&lifecycle);
    const auto before=integration_bank;const auto bank_address=0xC0B1E8+127*0x14;
    reinterpret_cast<void(__thiscall*)(uint32_t,uint32_t,int32_t,uint32_t)>(callbacks[2])(bank_address,0xDC00,0x05D3,0xAB00);
    auto captured=capture_accepted_script_task(before,integration_bank,0xDC00,0x05D3);
    check(bool(captured),"Native Set wrapper produces exact before/after executor capture");
    auto receipt=lifecycle.accept(*captured);check(receipt && lifecycle.refresh(*receipt),"ABI callback -> native write -> verified capture -> observer epoch linked");
    reinterpret_cast<void(__thiscall*)(uint32_t,uint32_t)>(callbacks[7])(bank_address,0xBC00);
    auto associated=lifecycle.refresh(*receipt);check(associated && associated->binding.record.task==0xBC00,"Actual association callback updates canonical binding while preserving accepted receipt");
    reinterpret_cast<void(__cdecl*)(uint32_t)>(callbacks[8])(0xBC00);
    check(!lifecycle.refresh(*associated),"Actual release callback retires associated task with stale record bytes still present");
    const auto next_before=integration_bank;
    reinterpret_cast<void(__thiscall*)(uint32_t,uint32_t,int32_t,uint32_t)>(callbacks[2])(bank_address,0xDC00,0x05D3,0xAB00);
    captured=capture_accepted_script_task(next_before,integration_bank,0xDC00,0x05D3);check(bool(captured),"Fresh accepted event before suspension");
    auto before_suspend=lifecycle.accept(*captured);check(bool(before_suspend),"Live accepted receipt before observer suspension");
    TaskHookCallbackPreparation preparation(&lifecycle);preparation.suspended();
    check(!task_hook_callbacks_observing() && !lifecycle.refresh(*before_suspend) &&
        reinterpret_cast<uint32_t(__thiscall*)(uint32_t)>(callbacks[0])(0x1234)==0xDEADBEEF,"Partial-install suspension invalidates observation but native continuation targets remain callable");
    check(preparation.attach(integration_targets) && !lifecycle.refresh(*before_suspend) && !lifecycle.accept(*captured),"Reattachment cannot resurrect pre-suspension receipts from unchanged bank bytes");
    configure_task_hook_callbacks(targets,nullptr);
    std::cout<<"PASS: eleven Win32 callback ABIs, native returns/faults, stack operands, inline ESI/flags/x87/SSE and observer fault isolation; synthetic continuations only\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
