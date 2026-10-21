#pragma once
#include "task_observer_reader.hpp"
#include <atomic>
namespace lcs {
// Owns the complete observation pipeline. Engine-thread and exclusion proofs
// are supplied by the actual runtime boundary, never hardcoded true here.
// The host/backend/module must remain resident while native entries can reach
// a trampoline, including partial failure. Destruction does not free code.
class TaskObserverHost final:public TaskHookSink {
    TaskHookMemory& memory_;
    bool (*profile_)();bool (*game_thread_)();bool (*excluded_)();
    TaskHookTransaction transaction_;
    bool health_lost_=false;
    std::atomic<bool> foreign_entry_{false};
    class Reader final:public TaskObserverReader {
        TaskObserverHost& host_;VerifiedTaskObserverReader native_;
    public:
        Reader(TaskObserverHost& host,TaskHookMemory& memory,bool (*profile)(),bool (*task_live)(uint32_t)):host_(host),native_(memory,profile,task_hook_callbacks_observing,task_live){}
        bool healthy()const override{return host_.active() && native_.healthy();}
        bool record(size_t slot,ScriptTaskRecord& out)override{return healthy() && native_.record(slot,out);}
        bool event_task(uint32_t event,uint32_t& out)override{return healthy() && native_.event_task(event,out);}
    } reader_;
    ScriptTaskObserver observer_;
    TaskHookCallbackPreparation preparation_;
    bool callback_thread()noexcept {
        try {if(game_thread_ && game_thread_())return true;}catch(...){}
        foreign_entry_.store(true,std::memory_order_release);return false;
    }
public:
    TaskObserverHost(TaskHookMemory& memory,bool (*profile)(),bool (*game_thread)(),bool (*excluded)(),bool (*task_live)(uint32_t)=nullptr):
        memory_(memory),profile_(profile),game_thread_(game_thread),excluded_(excluded),reader_(*this,memory,profile,task_live),observer_(reader_),preparation_(this){}
    TaskObserverHost(const TaskObserverHost&)=delete;
    TaskObserverHost& operator=(const TaskObserverHost&)=delete;
    ~TaskObserverHost()override{preparation_.suspended();} // never detach/free from a destructor without exclusion
    // Filter at the callback sink BEFORE ScriptTaskObserver touches even its
    // health/container fields. A foreign callback only sets an atomic loss flag;
    // the engine thread performs actual receipt retirement on its next read.
    uint64_t begin(size_t site,uint32_t self,uint32_t argument)override {
        return callback_thread()?observer_.begin(site,self,argument):0;
    }
    void end(uint64_t token,bool returned)override {if(callback_thread())observer_.end(token,returned);}
    void inline_flush(uint32_t record)override {if(callback_thread())observer_.inline_flush(record);}
    void observation_lost()noexcept override {
        if(callback_thread()){health_lost_=true;observer_.observation_lost();}
    }
    bool active() {
        // Never modify the observer from a foreign thread. Engine-thread
        // observation loss is latched: restoring byte-identical hooks cannot
        // resurrect receipts after missed mutation callbacks.
        if(!game_thread_ || !game_thread_())return false;
        if(transaction_.state()!=TaskHookTransaction::State::installed)return false;
        const bool valid=!foreign_entry_.load(std::memory_order_acquire) && profile_ && profile_() && observer_.observing() &&
            task_hook_callbacks_attached_to(this) && transaction_.owns_entries(memory_);
        if(!valid && !health_lost_){health_lost_=true;observer_.observation_lost();}
        return valid && !health_lost_;
    }
    bool install(const std::optional<TaskHookSite>& release_site=std::nullopt) {
        if(!profile_ || !game_thread_ || !excluded_ || !profile_() || !game_thread_() || !excluded_())return false;
        if(!transaction_.install(memory_,task_hook_callback_addresses(),preparation_,release_site))return false;
        foreign_entry_.store(false,std::memory_order_release);health_lost_=false;return true;
    }
    bool remove() {
        if(!game_thread_ || !excluded_ || !game_thread_() || !excluded_())return false;
        return transaction_.remove(memory_);
    }
    TaskHookTransaction::State state()const{return transaction_.state();}
    bool owns_original_prefix(uint32_t address,const uint8_t* expected,size_t length)const {
        return game_thread_ && game_thread_() && transaction_.owns_original_prefix(memory_,address,expected,length);
    }
    std::optional<EpochScriptTask> accept(const CapturedScriptTask& captured){return active()?observer_.accept(captured):std::nullopt;}
    std::optional<EpochScriptTask> refresh(const EpochScriptTask& token){return active()?observer_.refresh(token):std::nullopt;}
    std::optional<ScriptTaskRelease> released(const EpochScriptTask& token){return active()?observer_.released(token):std::nullopt;}
    // Owner of the receipts (the objective service) installs this on the game thread; it runs inside the native
    // task-release notification and must only read plain data.
    void set_release_probe(ScriptTaskReleaseProbe probe){if(game_thread_ && game_thread_())observer_.set_release_probe(std::move(probe));}
    void reset(){if(game_thread_ && game_thread_())observer_.reset();}
};
}
