#pragma once
#include "script_task_epochs.hpp"
#include "task_hook_callbacks.hpp"
#include <functional>
namespace lcs {
// What the observer saw when SA released the task of an owned receipt: the pool delete notification arrives BEFORE the
// task is destroyed, so a probe supplied by the objective owner can read the live game state at exactly that instant.
// `verdict` is the owner's own judgement at the release moment (not a cached sample); it never says why SA released it.
struct ScriptTaskRelease {
    static constexpr int not_arrived=0,arrived_early=1,arrived=2;   // early: at the target but the source pacing timer is pending
    EpochScriptTask token,initial;       // the receipt as last associated and as first issued (the event)
    bool probed=false;
    int verdict=not_arrived;
};
using ScriptTaskReleaseProbe=std::function<std::optional<int>(const EpochScriptTask&)>;
struct TaskObserverReader {
    virtual ~TaskObserverReader()=default;
    virtual bool healthy()const=0;
    virtual bool record(size_t slot,ScriptTaskRecord& out)=0;
    // Must validate the live script-event vtable before copying its owned task.
    virtual bool event_task(uint32_t event,uint32_t& out)=0;
};
class ScriptTaskObserver final:public TaskHookSink {
    struct Owned {EpochScriptTask initial,current;};
    struct Frame {
        uint64_t token=0;size_t site=0,slot=128;uint32_t argument=0;
        std::optional<ScriptTaskEpochs::Mutation> mutation;
        std::optional<EpochScriptTask> owner;
        ScriptTaskRecord before;
    };
    TaskObserverReader& reader_;
    ScriptTaskEpochs epochs_;
    std::array<std::optional<Owned>,128> owned_{};
    std::array<std::optional<ScriptTaskRelease>,128> releases_{};
    ScriptTaskReleaseProbe probe_;
    std::array<Frame,128> frames_{};
    uint64_t serial_=1;
    bool enabled_=true;
    static std::optional<size_t> index(uint32_t address) {
        if(address<0xC0B1E8 || address>=0xC0BBE8 || (address-0xC0B1E8)%0x14)return {};
        return (address-0xC0B1E8)/0x14;
    }
    bool busy(size_t slot)const {for(const auto& f:frames_)if(f.token && f.slot==slot)return true;return false;}
    void disable(){enabled_=false;epochs_.reset();owned_={};releases_={};frames_={};}
    void invalidate(size_t slot){owned_[slot].reset();epochs_.inline_flush(slot);}
    // The task being deleted is the associated task of an owned receipt whose bank record is still that exact binding:
    // record the owner's verdict at this moment. Anything else (event not associated yet, replaced slot, foreign task)
    // leaves no release evidence and the receipt simply retires.
    void releasing(uint32_t task) {
        if(!task)return;
        for(size_t slot=0;slot<owned_.size();++slot) {
            if(!owned_[slot])continue;
            const auto& current=owned_[slot]->current;
            if(current.binding.record.status==0 || current.binding.record.task!=task)continue;
            ScriptTaskRecord actual;
            if(!reader_.record(slot,actual) || !epochs_.still_owned(current,actual))continue;
            ScriptTaskRelease release;release.token=current;release.initial=owned_[slot]->initial;
            if(probe_) {
                try {
                    if(const auto verdict=probe_(current)){release.probed=true;release.verdict=*verdict;}
                } catch(...) {}
            }
            releases_[slot]=release;
        }
    }
    bool health()const{return enabled_ && reader_.healthy();}
public:
    explicit ScriptTaskObserver(TaskObserverReader& reader):reader_(reader){}
    bool observing()const{return enabled_;}
    // Supplied by the owner of the receipts (the objective service) before any native entry can reach a hook.
    void set_release_probe(ScriptTaskReleaseProbe probe){probe_=std::move(probe);}
    // Release evidence for exactly this receipt (same slot, epoch and associated task). The release stays valid across the
    // flush that normally follows it (the epoch binds it to the retired receipt); a new owner of the slot clears it.
    std::optional<ScriptTaskRelease> released(const EpochScriptTask& token)const {
        const auto slot=token.binding.slot;
        if(!health() || slot>=128 || !releases_[slot])return {};
        const auto& stored=releases_[slot]->token;
        // The holder may still carry the receipt as issued (event) or as last refreshed (associated task), like refresh().
        if(stored.epoch!=token.epoch ||
           (!(stored.binding.record==token.binding.record) && !(releases_[slot]->initial.binding.record==token.binding.record)))return {};
        return releases_[slot];
    }
    // Acceptance is supplied by the executor's single native-call capture.
    // Mutations/polls alone never create an app-owned job.
    std::optional<EpochScriptTask> accept(const CapturedScriptTask& captured) {
        if(!health() || captured.slot>=128 || busy(captured.slot))return {};
        releases_[captured.slot].reset();
        ScriptTaskRecord actual;if(!reader_.record(captured.slot,actual))return {};
        const auto token=epochs_.capture(captured,actual);if(!token)return {};
        if(actual.event) {
            uint32_t task=0;
            if(!reader_.event_task(actual.event,task) || !epochs_.observe_event_task(*token,actual,actual.event,task)) {
                invalidate(captured.slot);return {};
            }
        }
        owned_[captured.slot]=Owned{*token,*token};return token;
    }
    std::optional<EpochScriptTask> refresh(const EpochScriptTask& token) {
        const auto slot=token.binding.slot;
        if(!health() || slot>=128 || busy(slot) || !owned_[slot])return {};
        const auto& owned=*owned_[slot];
        if(token.epoch!=owned.current.epoch ||
           (!(token.binding.record==owned.initial.binding.record) && !(token.binding.record==owned.current.binding.record)))return {};
        ScriptTaskRecord actual;
        if(!reader_.record(slot,actual) || !epochs_.still_owned(owned.current,actual))return {};
        return owned.current;
    }
    uint64_t begin(size_t site,uint32_t self,uint32_t argument)override {
        if(!health())return 0;
        if(site==8){releasing(self);epochs_.retiring_task(self);return 0;}
        if(site==10){epochs_.retiring_event(self);return 0;}
        if(site>7){disable();return 0;}
        const auto slot=index(self);if(!slot)return 0; // other copies are not native bank records
        Frame* frame=nullptr;for(auto& f:frames_)if(!f.token){frame=&f;break;}
        if(!frame || !serial_){disable();return 0;}
        *frame=Frame{};frame->site=site;frame->slot=*slot;frame->argument=argument;
        if(site==7 && owned_[*slot] && reader_.record(*slot,frame->before) &&
           epochs_.still_owned(owned_[*slot]->current,frame->before))frame->owner=owned_[*slot]->current;
        else {owned_[*slot].reset();frame->mutation=epochs_.replacing(*slot);}
        if(!frame->owner && !frame->mutation){disable();return 0;}
        frame->token=serial_;serial_=serial_==UINT64_MAX?0:serial_+1;return frame->token;
    }
    void end(uint64_t token,bool returned)override {
        if(!health())return;
        Frame* pending=nullptr;for(auto& f:frames_)if(f.token==token && token){pending=&f;break;}
        if(!pending){disable();return;}
        const auto frame=*pending;*pending=Frame{};
        ScriptTaskRecord actual;
        const bool valid=returned && reader_.record(frame.slot,actual);
        if(frame.mutation) {
            epochs_.replaced(*frame.mutation,valid?actual:ScriptTaskRecord{});
            if(!valid)invalidate(frame.slot);
        } else {
            const auto associated=valid?epochs_.associated(*frame.owner,frame.slot,frame.before,actual,frame.argument):std::nullopt;
            if(!associated || !owned_[frame.slot] || owned_[frame.slot]->current.epoch!=frame.owner->epoch)invalidate(frame.slot);
            else owned_[frame.slot]->current=*associated;
        }
    }
    void inline_flush(uint32_t address)override {
        if(!health())return;const auto slot=index(address);if(slot)invalidate(*slot);
    }
    void reset(){epochs_.reset();owned_={};releases_={};frames_={};} // never reset token serial, never reenable a failed observer
    void observation_lost()noexcept override{reset();}
};
}
