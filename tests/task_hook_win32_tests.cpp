#include <windows.h>
#include "lcs/task_hook_win32.hpp"
#include <iostream>
#include <stdexcept>
using namespace lcs;
static bool excluded=false;
static bool gate(){return excluded;}
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct Page {
    uint8_t* memory=static_cast<uint8_t*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    Page(){check(memory!=nullptr,"Allocate private test page");}
    ~Page(){VirtualFree(memory,0,MEM_RELEASE);}
    uint32_t address()const{return uint32_t(reinterpret_cast<uintptr_t>(memory));}
    void protect(DWORD access){DWORD old=0;check(VirtualProtect(memory,4096,access,&old)!=0,"Protect private test page");}
    DWORD access()const{MEMORY_BASIC_INFORMATION info{};check(VirtualQuery(memory,&info,sizeof(info))!=0,"Query private test page");return info.Protect;}
};
// Translate only the reviewed native entry addresses to private fixture pages.
// Generated detour bytes are inspected, never executed; no game mapping exists.
struct PrivateEntries:TaskHookMemory,TaskHookPreparation {
    Page page;
    TaskHookWin32Memory backend{gate};
    uint32_t bank=0;
    bool prepared=false;
    bool attach(const std::array<uint32_t,11>& targets)noexcept override {
        for(size_t i=0;i<targets.size();++i)if(targets[i]!=bank+i*32)return false;
        prepared=true;return true;
    }
    void detached()noexcept override{prepared=false;}
    void suspended()noexcept override{}
    PrivateEntries(){for(size_t i=0;i<task_hook_sites.size();++i)std::memcpy(page.memory+i*64,task_hook_sites[i].prefix.data(),task_hook_sites[i].length);page.protect(PAGE_EXECUTE_READ);}
    uint32_t translate(uint32_t address)const {
        for(size_t i=0;i<task_hook_sites.size();++i)if(address==task_hook_sites[i].address)return page.address()+uint32_t(i*64);
        return 0;
    }
    bool read(uint32_t address,uint8_t* bytes,size_t length)override{return backend.read(translate(address),bytes,length);}
    uint32_t allocate(size_t length)override{bank=backend.allocate(length);return bank;}
    bool publish(uint32_t address,const uint8_t* bytes,size_t length)override{return backend.publish(address,bytes,length);}
    Write exchange(uint32_t address,const uint8_t* expected,const uint8_t* desired,size_t length)override{check(prepared,"Callback continuations ready before real Win32 entry exchange");return backend.exchange(translate(address),expected,desired,length);}
    void release(uint32_t address)override{backend.release(address);}
};
int main(){try{
    static_assert(sizeof(void*)==4,"Actual hook backend only supports the reviewed Win32 ABI");
    TaskHookWin32Memory memory(gate),ungated(nullptr);Page entry;
    const std::array<uint8_t,7> original{{0x53,0x56,0x8B,0xF1,0x8B,0x4E,0x0C}};
    std::array<uint8_t,7> desired{{0xE9,1,2,3,4,0x90,0x90}},snapshot{};
    std::memcpy(entry.memory,original.data(),original.size());
    check(!memory.allocate(32) && !ungated.allocate(32),"No implicit permission to mutate code outside host exclusion");
    excluded=true;
    check(!memory.allocate(0) && !memory.allocate(4097),"Bounded native trampoline allocation");
    const auto bank=memory.allocate(32);check(bank && !memory.allocate(32),"Single allocation owner, no concurrent bank replacement");
    std::array<uint8_t,32> trampoline{};trampoline.fill(0xCC);
    check(!memory.publish(bank+1,trampoline.data(),32) && !memory.publish(bank,trampoline.data(),31),"Only exact owned bank can be published");
    check(memory.publish(bank,trampoline.data(),32),"Publish private trampoline page as executable with actual Win32 cache flush");
    MEMORY_BASIC_INFORMATION info{};check(VirtualQuery(reinterpret_cast<const void*>(bank),&info,sizeof(info)) && info.Protect==PAGE_EXECUTE_READ,"Published code is RX, not left writable");
    check(memory.read(entry.address(),snapshot.data(),snapshot.size()) && snapshot==original,"Private memory snapshots preserve bytes");
    check(memory.exchange(entry.address(),original.data(),desired.data(),7)==TaskHookMemory::Write::unchanged,"Ordinary data pages cannot be patched as native executable entries");
    entry.protect(PAGE_EXECUTE_READ);
    excluded=false;
    check(memory.exchange(entry.address(),original.data(),desired.data(),7)==TaskHookMemory::Write::unchanged,"Withdrawal of host exclusion refuses write");
    memory.release(bank);check(VirtualQuery(reinterpret_cast<const void*>(bank),&info,sizeof(info)) && info.State==MEM_COMMIT,"No page release outside host exclusion");
    excluded=true;
    auto wrong=original;wrong[3]^=1;
    check(memory.exchange(entry.address(),wrong.data(),desired.data(),7)==TaskHookMemory::Write::unchanged &&
        entry.access()==PAGE_EXECUTE_READ && !std::memcmp(entry.memory,original.data(),7),"Foreign prefix is untouched, including its page protection");
    check(memory.exchange(entry.address(),original.data(),desired.data(),7)==TaskHookMemory::Write::written &&
        entry.access()==PAGE_EXECUTE_READ && !std::memcmp(entry.memory,desired.data(),7),"Whole private executable prefix written, cache flushed and protection restored");
    check(memory.exchange(entry.address(),desired.data(),original.data(),7)==TaskHookMemory::Write::written &&
        entry.access()==PAGE_EXECUTE_READ && !std::memcmp(entry.memory,original.data(),7),"Real Win32 restoration returns original bytes and protection");
    check(memory.exchange(entry.address(),original.data(),reinterpret_cast<const uint8_t*>(1),7)==TaskHookMemory::Write::uncertain &&
        entry.access()==PAGE_EXECUTE_READ && !std::memcmp(entry.memory,original.data(),7),"Real access violation after changing protection is contained, restored and reported uncertain rather than safe to free");
    check(memory.exchange(entry.address(),reinterpret_cast<const uint8_t*>(1),desired.data(),7)==TaskHookMemory::Write::unchanged &&
        entry.access()==PAGE_EXECUTE_READ,"Fault before protection/write leaves executable entry untouched");
    entry.protect(PAGE_NOACCESS);
    check(!memory.read(entry.address(),snapshot.data(),7) &&
        memory.exchange(entry.address(),original.data(),desired.data(),7)==TaskHookMemory::Write::unchanged,"Unreadable executable profile refuses read/write without propagating an access violation");
    check(!memory.read(UINT32_MAX-2,snapshot.data(),7) && !memory.read(0,snapshot.data(),7),"Null and wrapped native ranges refused");
    entry.protect(PAGE_READWRITE|PAGE_GUARD);
    check(!memory.read(entry.address(),snapshot.data(),7) && (entry.access()&PAGE_GUARD),"Inspection does not consume another owner's guard page");
    memory.release(bank+1);check(VirtualQuery(reinterpret_cast<const void*>(bank),&info,sizeof(info)) && info.State==MEM_COMMIT,"Cannot release a foreign or interior pointer");
    memory.release(bank);check(VirtualQuery(reinterpret_cast<const void*>(bank),&info,sizeof(info)) && info.State==MEM_FREE,"Release only explicitly detached owned bank");
    const auto second=memory.allocate(16);check(second,"New bank possible after explicit successful release");memory.release(second);
    {
        PrivateEntries fixture;TaskHookTransaction transaction;
        std::array<uint32_t,task_hook_sites.size()> callbacks{};
        for(size_t i=0;i<callbacks.size();++i)callbacks[i]=0x12340000+uint32_t(i*64);
        check(transaction.install(fixture,callbacks,fixture),"Full eleven-entry transaction works through real Win32 memory operations on isolated fixture pages");
        for(size_t i=0;i<callbacks.size();++i)check(fixture.page.memory[i*64]==0xE9,"Every private entry redirected only after trampoline bank publication");
        check(fixture.page.access()==PAGE_EXECUTE_READ && transaction.remove(fixture),"Full real backend removal preserves RX protection");
        for(size_t i=0;i<callbacks.size();++i)check(!std::memcmp(fixture.page.memory+i*64,task_hook_sites[i].prefix.data(),task_hook_sites[i].length),"All eleven native prefixes restored on private page");
        check(VirtualQuery(reinterpret_cast<const void*>(fixture.bank),&info,sizeof(info)) && info.State==MEM_FREE,"Detached full trampoline bank freed only after successful restoration");
    }
    std::cout<<"PASS: real Win32 allocation/protection/cache operations on private pages only, explicit exclusion gate, exact prefixes and inaccessible ranges; game not executed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
