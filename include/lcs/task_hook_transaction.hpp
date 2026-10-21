#pragma once
#include "task_hook_plan.hpp"

namespace lcs {
// The host must exclude execution/concurrent code writers during install and
// removal. This interface does not make a five-byte x86 patch atomic.
struct TaskHookMemory {
    enum class Write {unchanged,written,uncertain};
    virtual ~TaskHookMemory()=default;
    virtual bool read(uint32_t address,uint8_t* bytes,size_t length)=0;
    virtual uint32_t allocate(size_t length)=0;
    // Publish executable trampolines and flush their instruction cache before
    // redirecting any native entry. Failure must leave native entries alone.
    virtual bool publish(uint32_t address,const uint8_t* bytes,size_t length)=0;
    // Compare the complete expected prefix under host exclusion, write desired,
    // restore protection and flush the cache. Any possible partial write/cache
    // or protection failure MUST return uncertain, never unchanged.
    virtual Write exchange(uint32_t address,const uint8_t* expected,const uint8_t* desired,size_t length)=0;
    virtual void release(uint32_t address)=0;
};
struct TaskHookPreparation {
    virtual ~TaskHookPreparation()=default;
    // Configure EVERY native continuation before the first entry can redirect,
    // including a failed install that leaves a resident partial detour.
    virtual bool attach(const std::array<uint32_t,11>& trampolines)noexcept=0;
    virtual void detached()noexcept=0;
    virtual void suspended()noexcept=0; // retain native targets, disable partial observation
};
class TaskHookTransaction {
public:
    enum class State {empty,installed,resident_failure};
private:
    static constexpr size_t count=task_hook_sites.size(),stride=32;
    std::array<TaskHookPlan,count> plans_{};
    // Reviewed sites; only the task-release entry may be replaced by the host (see install).
    std::array<TaskHookSite,count> sites_=task_hook_sites;
    uint32_t allocation_=0;
    State state_=State::empty;
    TaskHookPreparation* preparation_=nullptr;
    static bool equal(TaskHookMemory& memory,uint32_t address,const uint8_t* bytes,size_t length) {
        std::array<uint8_t,16> actual{};
        return length<=actual.size() && memory.read(address,actual.data(),length) && !std::memcmp(actual.data(),bytes,length);
    }
    bool rollback(TaskHookMemory& memory,size_t attempted,bool uncertain=false) {
        // Even restored bytes cannot prove cache/protection success after an
        // uncertain write. Keep reachable storage resident in that case.
        bool clean=!uncertain;
        while(attempted) {
            const auto index=--attempted;const auto& site=sites_[index];const auto& plan=plans_[index];
            if(equal(memory,site.address,site.prefix.data(),site.length))continue;
            // Do not erase an unknown/partial patch or another mod's detour.
            if(!equal(memory,site.address,plan.entry.data(),site.length) ||
               memory.exchange(site.address,plan.entry.data(),site.prefix.data(),site.length)!=TaskHookMemory::Write::written ||
               !equal(memory,site.address,site.prefix.data(),site.length))clean=false;
        }
        if(!clean){state_=State::resident_failure;if(preparation_)preparation_->suspended();return false;}
        if(preparation_)preparation_->detached();preparation_=nullptr;
        memory.release(allocation_);allocation_=0;state_=State::empty;return true;
    }
public:
    TaskHookTransaction()=default;
    TaskHookTransaction(const TaskHookTransaction&)=delete;
    TaskHookTransaction& operator=(const TaskHookTransaction&)=delete;
    TaskHookTransaction(TaskHookTransaction&&)=delete;
    TaskHookTransaction& operator=(TaskHookTransaction&&)=delete;
    State state()const{return state_;}
    bool owns_entries(TaskHookMemory& memory)const {
        if(state_!=State::installed)return false;
        for(size_t i=0;i<count;++i)if(!equal(memory,sites_[i].address,plans_[i].entry.data(),sites_[i].length))return false;
        return true;
    }
    // Recognize our own redirect without accepting arbitrary detours. The
    // replaced bytes must be the reviewed pre-image and its executable
    // continuation must still be exactly the one this transaction published.
    bool owns_original_prefix(TaskHookMemory& memory,uint32_t address,const uint8_t* expected,size_t length)const {
        if(state_!=State::installed || !expected || !length || length>64)return false;
        for(size_t i=0;i<count;++i) {
            const auto& site=sites_[i];const auto& plan=plans_[i];
            if(site.address!=address)continue;
            const auto replaced=length<site.length?length:site.length;
            if(std::memcmp(expected,site.prefix.data(),replaced) ||
               !equal(memory,address,plan.entry.data(),site.length) ||
               !equal(memory,allocation_+uint32_t(i*stride),plan.trampoline.data(),plan.trampoline_size))return false;
            if(length<=site.length)return true;
            std::array<uint8_t,64> tail{};
            const auto remaining=length-site.length;
            return uint64_t(address)+length<=uint64_t(UINT32_MAX)+1 &&
                memory.read(address+site.length,tail.data(),remaining) &&
                !std::memcmp(tail.data(),expected+site.length,remaining);
        }
        return false;
    }
    // The callback module must remain loaded while this owns resident pages,
    // including resident_failure. No destructor frees possibly reachable code.
    uint32_t trampoline(size_t index)const {
        return allocation_ && index<count?allocation_+uint32_t(index*stride):0;
    }
    // `release_site` replaces ONLY the task-release entry (index 8, a cdecl one-argument function whose first six bytes
    // are straight-line instructions) when the host proved that another reviewed module owns the live free path, e.g.
    // the Open Limit Adjuster forwarding 0x61A5B0 to its own release. All other entries stay the retail sites.
    bool install(TaskHookMemory& memory,const std::array<uint32_t,count>& callbacks,TaskHookPreparation& preparation,
            const std::optional<TaskHookSite>& release_site=std::nullopt) {
        if(state_!=State::empty || allocation_)return false;
        sites_=task_hook_sites;
        if(release_site) {
            if(release_site->length!=6 || !release_site->address)return false;
            sites_[8]=*release_site;
        }
        for(size_t i=0;i<count;++i)if(!callbacks[i] ||
            !equal(memory,sites_[i].address,sites_[i].prefix.data(),sites_[i].length))return false;
        allocation_=memory.allocate(count*stride);
        if(!allocation_)return false;
        std::array<uint8_t,count*stride> code{};code.fill(0xCC);
        bool valid=uint64_t(allocation_)+code.size()<=uint64_t(UINT32_MAX)+1;
        for(size_t i=0;valid && i<count;++i) {
            // A callback/trampoline must not overlap ANY of the entry regions,
            // or ANY other trampoline in the allocation, not only its own.
            if(uint64_t(callbacks[i])>=allocation_ && uint64_t(callbacks[i])<uint64_t(allocation_)+code.size())valid=false;
            for(const auto& site:sites_)if(
                (callbacks[i]>=site.address && callbacks[i]<site.address+site.length) ||
                (uint64_t(allocation_)<uint64_t(site.address)+site.length && uint64_t(allocation_)+code.size()>site.address))valid=false;
            if(!valid)break;
            const auto& site=sites_[i];auto plan=plan_task_hook(site,callbacks[i],trampoline(i),site.prefix.data(),site.length);
            if(!plan){valid=false;break;}
            plans_[i]=*plan;std::memcpy(code.data()+i*stride,plan->trampoline.data(),plan->trampoline_size);
        }
        if(!valid || !memory.publish(allocation_,code.data(),code.size())) {
            memory.release(allocation_);allocation_=0;return false;
        }
        std::array<uint32_t,count> continuations{};
        for(size_t i=0;i<count;++i)continuations[i]=trampoline(i);
        if(!preparation.attach(continuations)) {
            preparation.detached();memory.release(allocation_);allocation_=0;return false;
        }
        preparation_=&preparation;
        // Recheck every entry after allocation/publication, before the first
        // mutation. Refuse already hooked functions without chaining blindly.
        for(const auto& site:sites_)if(!equal(memory,site.address,site.prefix.data(),site.length)) {
            preparation_->detached();preparation_=nullptr;
            memory.release(allocation_);allocation_=0;return false;
        }
        for(size_t i=0;i<count;++i) {
            const auto& site=sites_[i];const auto& plan=plans_[i];
            const auto written=memory.exchange(site.address,site.prefix.data(),plan.entry.data(),site.length);
            if(written!=TaskHookMemory::Write::written || !equal(memory,site.address,plan.entry.data(),site.length)){
                rollback(memory,i+1,written==TaskHookMemory::Write::uncertain);return false;
            }
        }
        state_=State::installed;return true;
    }
    bool remove(TaskHookMemory& memory) {
        if(state_!=State::installed)return false;
        // Full preflight means a foreign hook refuses removal before touching
        // any other entry. Keep code resident if it might still be reached.
        for(size_t i=0;i<count;++i)if(!equal(memory,sites_[i].address,plans_[i].entry.data(),sites_[i].length)) {
            state_=State::resident_failure;if(preparation_)preparation_->suspended();return false;
        }
        return rollback(memory,count);
    }
};
}
