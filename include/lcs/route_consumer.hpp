#pragma once
#include "route_table.hpp"
#include "objective_receipts.hpp"
#include "script_task_epochs.hpp"
#include "owned_route_task.hpp"
namespace lcs {
struct RouteObservation {
    ObjectiveActor actor;
    EpochScriptTask binding;
    std::array<float,3> position{};
    bool current_route=false,temporary=false,controllable=false,on_foot=false,motion_active=false,running=false;
};
struct RouteReplacement {
    bool executed=false,entered=false;
    std::optional<EpochScriptTask> accepted;
};
struct RouteConsumerBackend {
    virtual ~RouteConsumerBackend()=default;
    // Must resolve the current generation and refresh through the observer.
    virtual std::optional<RouteObservation> observe(ObjectiveActor,const EpochScriptTask&)=0;
    // Must revalidate expected ownership at the native write boundary, execute
    // exactly once, and obtain a NEW accepted receipt. Never replay entered faults.
    virtual RouteReplacement replace(ObjectiveActor,const EpochScriptTask&,const std::array<float,3>&,bool running,bool cleanup)=0;
    // Retarget the already-owned GoToPointAndStandStill while its control
    // callback is running. The accepted task epoch remains the same.
    virtual OwnedRouteRetargetResult retarget(ObjectiveActor,const EpochScriptTask&,const std::array<float,3>&,bool running,bool cleanup) {
        (void)running;(void)cleanup;return {};
    }
};
class OriginalRouteConsumer {
public:
    enum class Result {waiting,suspended,advanced,sequence_ended,refused,unavailable};
private:
    OriginalRouteCursor cursor_;
    ObjectiveActor actor_;
    EpochScriptTask binding_;
    bool cleanup_=false,monitoring_=true,task_control_=false;
    static bool receipt(ObjectiveActor actor,const EpochScriptTask& token) {
        const auto& record=token.binding.record;
        return actor.handle>=0 && !(uint32_t(actor.handle)&0x80u) && actor.native_ped && token.epoch && token.binding.slot<128 &&
            record.ped==actor.native_ped && record.opcode==0x05D3 &&
            ((record.status==0 && record.event && !record.task) ||
             ((record.status==1 || record.status==2 || record.status==4 || record.status==5 || record.status==6) && record.task && !record.event));
    }
    OriginalRouteConsumer(OriginalRouteCursor cursor,ObjectiveActor actor,EpochScriptTask binding,bool cleanup,bool task_control):cursor_(cursor),actor_(actor),binding_(binding),cleanup_(cleanup),task_control_(task_control){}
public:
    // Called only after the reviewed original setter and real native assignment
    // have been accepted. This object does not implement/reset the SCM latch.
    static std::optional<OriginalRouteConsumer> accepted(OriginalRouteCursor cursor,ObjectiveActor actor,EpochScriptTask binding,bool cleanup,bool task_control=false) {
        if(!receipt(actor,binding))return {};
        return OriginalRouteConsumer(cursor,actor,binding,cleanup,task_control);
    }
    const OriginalRouteCursor& cursor()const{return cursor_;}
    const EpochScriptTask& binding()const{return binding_;}
    bool monitoring()const{return monitoring_;}
    Result control(const OriginalRouteTable& table,const RouteObservation& observed,RouteConsumerBackend& backend) {
        if(!monitoring_ || !task_control_)return Result::unavailable;
        if(!(observed.actor==actor_)){monitoring_=false;return Result::unavailable;}
        // Source callbacks, not native task names, establish route intent.
        // Restoration is rebound only after its own accepted assignment.
        if(observed.temporary)return Result::suspended;
        if(!observed.current_route || observed.binding.epoch!=binding_.epoch ||
           observed.binding.binding.slot!=binding_.binding.slot || !receipt(actor_,observed.binding) ||
           observed.binding.binding.record.event) {
            monitoring_=false;return Result::unavailable;
        }
        binding_=observed.binding;
        if(!observed.controllable || !observed.on_foot || !observed.motion_active)return Result::suspended;
        auto candidate=cursor_;
        const auto arrived=candidate.arrive(table,observed.position);
        if(arrived==OriginalRouteCursor::Result::waiting)return Result::waiting;
        if(arrived==OriginalRouteCursor::Result::unavailable){monitoring_=false;return Result::unavailable;}
        if(arrived==OriginalRouteCursor::Result::ended) {
            // STOP_WHEN_DONE: retail selects point -1 and seeks gaRoutes[-1] (the tail of an unrelated .bss
            // object: no authored position). SA outcome: the owned GoToPointAndStandStill already targets the
            // final point, so the ped holds there. FOLLOW_ROUTE stays the source objective: nothing here sets
            // the script latch or restores a previous objective, and the retail out-of-bounds seek is not
            // reproduced.
            cursor_=candidate;monitoring_=false;return Result::sequence_ended;
        }
        const auto effect=backend.retarget(actor_,binding_,candidate.target(),observed.running,cleanup_);
        if(!effect.executed) {
            if(effect.entered){monitoring_=false;return Result::unavailable;}
            return Result::refused;
        }
        cursor_=candidate;return Result::advanced;
    }
    Result tick(const OriginalRouteTable& table,RouteConsumerBackend& backend) {
        if(!monitoring_)return Result::unavailable;
        const auto observed=backend.observe(actor_,binding_);
        if(!observed || !(observed->actor==actor_)){monitoring_=false;return Result::unavailable;}
        // A temporary source objective suspends route intent. Resumption needs
        // the actual restoration's accepted assignment, not automatic takeover.
        if(observed->temporary)return Result::suspended;
        if(!observed->current_route || observed->binding.epoch!=binding_.epoch ||
           observed->binding.binding.slot!=binding_.binding.slot || !receipt(actor_,observed->binding)) {
            monitoring_=false;return Result::unavailable;
        }
        binding_=observed->binding; // exact observed event->task transition
        if(!observed->controllable || !observed->on_foot || !observed->motion_active || binding_.binding.record.event)return Result::suspended;
        // Owned production tasks process arrival inside ControlSubTask before
        // their GoToPoint child can finish. Existing observer-only backends
        // retain their polled replace behavior for compatibility.
        if(task_control_)return Result::waiting;
        auto candidate=cursor_;
        const auto arrived=candidate.arrive(table,observed->position);
        if(arrived==OriginalRouteCursor::Result::waiting)return Result::waiting;
        if(arrived==OriginalRouteCursor::Result::unavailable){monitoring_=false;return Result::unavailable;}
        if(arrived==OriginalRouteCursor::Result::ended) {
            cursor_=candidate;monitoring_=false;return Result::sequence_ended; // same STOP_WHEN_DONE outcome as control()
        }
        const auto replacement=backend.replace(actor_,binding_,candidate.target(),observed->running,cleanup_);
        if(!replacement.executed) {
            if(replacement.entered){monitoring_=false;return Result::unavailable;}
            return Result::refused;
        }
        if(!replacement.entered){monitoring_=false;return Result::unavailable;}
        cursor_=candidate;
        if(!replacement.accepted || !receipt(actor_,*replacement.accepted) ||
           (replacement.accepted->epoch==binding_.epoch && replacement.accepted->binding.slot==binding_.binding.slot)) {
            monitoring_=false;return Result::unavailable;
        }
        binding_=*replacement.accepted;
        return Result::advanced;
    }
};
}
