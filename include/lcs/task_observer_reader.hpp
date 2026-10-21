#pragma once
#include "task_observer.hpp"
#include "task_hook_transaction.hpp"
namespace lcs {
// Byte reader is shared with the contained Win32 backend. No native virtual
// getter is called, borrowed pointer retained, or record modified here.
class VerifiedTaskObserverReader final:public TaskObserverReader {
    TaskHookMemory& memory_;
    bool (*profile_)();bool (*hooks_)();
    // Optional host liveness proof for a task pointer (the Open Limit Adjuster replaces the retail pool). When absent the
    // retail 128-byte pool arithmetic below is used.
    bool (*task_live_)(uint32_t)=nullptr;
    bool word(uint32_t address,uint32_t& out) {return memory_.read(address,reinterpret_cast<uint8_t*>(&out),4);}
    template<size_t N> bool matches(uint32_t address,const uint8_t (&bytes)[N]) {
        std::array<uint8_t,N> actual{};return memory_.read(address,actual.data(),N) && !std::memcmp(actual.data(),bytes,N);
    }
public:
    VerifiedTaskObserverReader(TaskHookMemory& memory,bool (*profile)(),bool (*hooks)(),bool (*task_live)(uint32_t)=nullptr):memory_(memory),profile_(profile),hooks_(hooks),task_live_(task_live){}
    bool healthy()const override{return profile_ && hooks_ && profile_() && hooks_();}
    bool record(size_t slot,ScriptTaskRecord& out)override {
        if(!healthy() || slot>=128)return false;
        const uint8_t lookup[]={0x8B,0x54,0x24,0x08,0x56,0x8B,0x74,0x24,0x08,0x83,0xC8,0xFF,0xB9,0xE8,0xB1,0xC0,0};
        const uint8_t bounds[]={0x83,0xC1,0x14,0x81,0xF9,0xE8,0xBB,0xC0,0};
        ScriptTaskRecord candidate;
        if(!matches(0x608710,lookup) || !matches(0x60872F,bounds) ||
           !memory_.read(0xC0B1E8+uint32_t(slot*0x14),reinterpret_cast<uint8_t*>(&candidate),sizeof(candidate)))return false;
        out=candidate;return true;
    }
    bool event_task(uint32_t event,uint32_t& out)override {
        if(!healthy() || !event || event>UINT32_MAX-0x14)return false;
        const uint8_t event_type[]={0xB8,0x20,0,0,0,0xC3};
        uint32_t vtable=0,task=0;
        if(!matches(0x4B0A30,event_type) || !word(event,vtable) || vtable!=0x85B378 || !word(event+0x10,task) || !task)return false;
        if(task_live_)return task_live_(task) && ((out=task),true);
        const uint8_t allocator[]={0x8B,0x0D,0xA8,0x44,0xB7,0,0xE9,0x55,0xFF,0xFF,0xFF};
        const uint8_t stride[]={0xC1,0xE0,0x07};
        uint32_t pool=0,objects=0,flags=0,count=0;
        if(!matches(0x61A5A0,allocator) || !matches(0x61A55A,stride) ||
           !word(0xB744A8,pool) || !pool || pool>UINT32_MAX-12 || !word(pool,objects) ||
           !word(pool+4,flags) || !word(pool+8,count) || !objects || !flags || !count || count>INT32_MAX || task<objects)return false;
        const uint32_t delta=task-objects,index=delta/128;
        if(delta%128 || index>=count || uint64_t(flags)+index>UINT32_MAX)return false;
        uint8_t state=0;
        if(!memory_.read(flags+index,&state,1) || (state&0x80))return false;
        out=task;return true;
    }
};
}
