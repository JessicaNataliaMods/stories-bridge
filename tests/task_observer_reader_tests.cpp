#include "lcs/task_observer_reader.hpp"
#include <map>
#include <vector>
#include <iostream>
#include <stdexcept>
using namespace lcs;
static bool abi=true,hooks=true;
static bool profile(){return abi;}static bool observed(){return hooks;}
static uint32_t live_task=0;static bool task_live(uint32_t task){return task && task==live_task;}
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct Memory:TaskHookMemory {
    std::map<uint32_t,std::vector<uint8_t>> blocks;
    template<class T> void put(uint32_t address,const T& value){blocks[address]=std::vector<uint8_t>(reinterpret_cast<const uint8_t*>(&value),reinterpret_cast<const uint8_t*>(&value)+sizeof(value));}
    void bytes(uint32_t address,std::initializer_list<uint8_t> value){blocks[address]=value;}
    bool read(uint32_t address,uint8_t* out,size_t size)override {
        auto it=blocks.upper_bound(address);if(it==blocks.begin())return false;--it;
        const auto offset=uint64_t(address)-it->first;
        if(offset+size>it->second.size())return false;
        std::memcpy(out,it->second.data()+size_t(offset),size);return true;
    }
    uint32_t allocate(size_t)override{throw std::runtime_error("Reader must never allocate executable code");}
    bool publish(uint32_t,const uint8_t*,size_t)override{throw std::runtime_error("Reader must never publish code");}
    Write exchange(uint32_t,const uint8_t*,const uint8_t*,size_t)override{throw std::runtime_error("Reader must never mutate native bytes");}
    void release(uint32_t)override{throw std::runtime_error("Reader must never release native storage");}
};
int main(){try{
    Memory m;
    m.bytes(0x608710,{0x8B,0x54,0x24,0x08,0x56,0x8B,0x74,0x24,0x08,0x83,0xC8,0xFF,0xB9,0xE8,0xB1,0xC0,0});
    m.bytes(0x60872F,{0x83,0xC1,0x14,0x81,0xF9,0xE8,0xBB,0xC0,0});
    m.bytes(0x4B0A30,{0xB8,0x20,0,0,0,0xC3});
    m.bytes(0x61A5A0,{0x8B,0x0D,0xA8,0x44,0xB7,0,0xE9,0x55,0xFF,0xFF,0xFF});
    m.bytes(0x61A55A,{0xC1,0xE0,0x07});
    m.put(0xB744A8,uint32_t(0x10000));m.put(0x10000,uint32_t(0x20000));m.put(0x10004,uint32_t(0x30000));m.put(0x10008,uint32_t(4));m.bytes(0x30000,{0,0,0,0});
    m.put(0x40000,uint32_t(0x85B378));m.put(0x40010,uint32_t(0x20180));
    const ScriptTaskRecord event{0x05D3,0x40000,0,0xABCD,0};m.put(0xC0B1E8+127*0x14,event);
    const auto original=m.blocks;VerifiedTaskObserverReader reader(m,profile,observed);
    ScriptTaskRecord result;uint32_t task=0;
    check(reader.record(127,result) && result==event && !reader.record(128,result),"Exact last native record and bounded bank");
    check(reader.event_task(0x40000,task) && task==0x20180 && m.blocks==original,"Exact script-event vtable and live aligned task pool slot read without mutation");
    for(const auto address:{0x608710u,0x60872Fu,0x4B0A30u,0x61A5A0u,0x61A55Au})for(size_t byte=0;byte<original.at(address).size();++byte) {
        m.blocks=original;m.blocks[address][byte]^=1;
        check(address<0x610000 && address!=0x4B0A30? !reader.record(127,result): !reader.event_task(0x40000,task),"Every reviewed native layout/helper prefix byte is required");
    }
    m.blocks=original;hooks=false;check(!reader.record(127,result) && !reader.event_task(0x40000,task),"Observer failure disables all native receipt reads");hooks=true;abi=false;check(!reader.healthy(),"ABI failure refuses all observation");abi=true;
    for(const auto value:{uint32_t(0),uint32_t(0x20001),uint32_t(0x20200),uint32_t(0x1FFFF)}) {
        m.blocks=original;m.put(0x40010,value);task=0xDEAD;check(!reader.event_task(0x40000,task) && task==0xDEAD,"Null/misaligned/outside/below-pool tasks refused atomically");
    }
    m.blocks=original;m.blocks[0x30000][3]=0x80;check(!reader.event_task(0x40000,task),"Freed task slot cannot become queued objective dependency");
    for(uint32_t count:{uint32_t(0),uint32_t(0xFFFFFFFF)}) {m.blocks=original;m.put(0x10008,count);check(!reader.event_task(0x40000,task),"Zero/negative native pool capacity cannot validate a task");}
    m.blocks=original;m.put(0x40000,uint32_t(0x85AB38));check(!reader.event_task(0x40000,task),"Base/foreign event vtable is not CEventScriptCommand");
    m.blocks=original;m.blocks.erase(0x40010);check(!reader.event_task(0x40000,task),"Unreadable queued task is unavailable");
    check(!reader.event_task(UINT32_MAX-3,task),"Event field range cannot wrap");
    m.blocks=original;ScriptTaskObserver sink(reader);auto token=sink.begin(2,0xC0B1E8+127*0x14,0x40000);sink.end(token,true);
    auto receipt=sink.accept({127,event});check(receipt && sink.refresh(*receipt),"Verified native reader connects callback mutation to accepted capture");sink.begin(8,0x20180,0);check(!sink.refresh(*receipt),"Real reader dependency retires through observed task release");
    {   // Host liveness proof (the adjuster replaces the retail pool and its allocator prologue): the retail pool bytes are
        // not consulted, the event vtable/type still are.
        m.blocks=original;m.blocks[0x61A5A0][0]^=1;m.blocks.erase(0x61A55A);m.put(0xB744A8,uint32_t(0));
        VerifiedTaskObserverReader hosted(m,profile,observed,task_live);
        live_task=0x20180;task=0;check(hosted.event_task(0x40000,task) && task==0x20180,"A host-proven live task is accepted without the retail pool layout");
        live_task=0x20188;task=0;check(!hosted.event_task(0x40000,task) && task==0,"A task the host does not prove live is refused atomically");
        live_task=0x20180;m.put(0x40000,uint32_t(0x85AB38));check(!hosted.event_task(0x40000,task),"The event class is still verified");
    }
    std::cout<<"PASS: native record/event byte reader, exact profiles, live task allocation, atomic refusal and observer sink connection; original executable not executed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
