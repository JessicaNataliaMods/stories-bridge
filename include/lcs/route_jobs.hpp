#pragma once
#include "route_native_backend.hpp"
#include <vector>

namespace lcs {
// Joins actual source transitions with actual observer acceptance. Assignment
// seeds and restored live cursors are deliberately separate. This controller
// grants no hook exclusion, actor initialization or restoration evidence.
template<class Host> class OriginalRouteJobs {
    struct Job {
        ObjectiveActor actor;
        uint64_t source_revision=0;
        OriginalRouteCursor cursor;
        bool cleanup=false,suspended=false,pending=true,quarantined=false,ended=false;
        std::optional<EpochScriptTask> old_assignment;
        std::optional<OriginalRouteConsumer> live;
    };
    Host& host_;OriginalRouteIntents& intents_;const OriginalRouteTable& table_;
    RouteNativeFunctions native_;
    std::unordered_map<int32_t,Job> jobs_;
    Job* find(ObjectiveActor actor) {
        const auto it=jobs_.find(actor.handle);
        return it!=jobs_.end() && it->second.actor==actor?&it->second:nullptr;
    }
    void unbind_control(Job& job) {
        if(native_.track_owned_lifetime && job.live && native_.unbind_control)
            native_.unbind_control(job.live->binding().binding,job.actor,job.live->binding().epoch,
                &route_control_trampoline,this);
    }
    static void route_control_trampoline(void* context,ObjectiveActor actor,uintptr_t task,uintptr_t ped) {
        if(!context)return;
        try {static_cast<OriginalRouteJobs*>(context)->control_task(actor,task,ped);}catch(...) {}
    }
    void control_task(ObjectiveActor actor,uintptr_t task,uintptr_t ped) {
        auto* job=find(actor);
        const auto source=intents_.route_intent(actor);
        if(!job || job->quarantined || job->ended || job->suspended || job->pending || !job->live ||
           ped!=actor.native_ped || !source || !(source->actor==actor) ||
           source->revision!=job->source_revision || !source->current_route || source->temporary)return;
        NativeRouteConsumerBackend<Host> backend(host_,intents_,job->source_revision,native_);
        const auto observed=backend.observe(actor,job->live->binding());
        if(!observed || observed->binding.binding.record.task!=task)return;
        const auto result=job->live->control(table_,*observed,backend);
        job=find(actor);if(!job)return;
        job->cursor=job->live->cursor();job->old_assignment=job->live->binding();
        if(result==Result::sequence_ended) {
            job->ended=true;unbind_control(*job);
        } else if(result==Result::unavailable) {
            job->quarantined=true;unbind_control(*job);
        }
    }
public:
    using Result=OriginalRouteConsumer::Result;
    OriginalRouteJobs(Host& host,OriginalRouteIntents& intents,const OriginalRouteTable& table,
            RouteNativeFunctions native={}):host_(host),intents_(intents),table_(table),native_(native) {}
    ~OriginalRouteJobs(){clear();}
    std::optional<EpochScriptTask> reusable_route_seek(const OriginalRouteIntents::Plan& plan) {
        if(!native_.track_owned_lifetime || !intents_.current(plan) ||
           plan.status()!=OriginalRouteIntents::Change::ready ||
           plan.reason()!=OriginalRouteIntents::Cause::route || !plan.proposed().cursor ||
           plan.previous().current!=OriginalRouteIntents::follow_route)return {};
        const auto actor=plan.proposed().actor;
        const auto job=find(actor);
        if(!job || !job->live || job->pending || job->suspended || job->quarantined || job->ended)return {};
        const auto binding=host_.refresh(job->live->binding());
        if(!binding || binding->epoch!=job->live->binding().epoch ||
           binding->binding.slot!=job->live->binding().binding.slot || binding->binding.record.event ||
           !binding->binding.record.task)return {};
        const auto target=sa_owned_route_target(binding->binding.record.task,actor,binding->epoch);
        const auto& next=plan.proposed().cursor->target();
        if(!target || (*target)[0]!=next[0] || (*target)[1]!=next[1] || !intents_.current(plan))return {};
        const auto confirmed=host_.refresh(*binding);
        if(!confirmed || confirmed->epoch!=binding->epoch || confirmed->binding.slot!=binding->binding.slot ||
           !(confirmed->binding.record==binding->binding.record))return {};
        return confirmed;
    }
    // The real source producer calls this AFTER its accepted source effect.
    // A ready plan is an immutable projection of the reviewed original setter.
    // entered failure instead calls retired(); it must never commit or replay.
    bool source_accepted(const OriginalRouteIntents::Plan& plan,bool cleanup,bool retain_seek=false) {
        if(!intents_.current(plan))return false;
        if(plan.status()==OriginalRouteIntents::Change::ignored)return true;
        if(plan.status()!=OriginalRouteIntents::Change::ready)return false;
        const auto retained=retain_seek?reusable_route_seek(plan):std::nullopt;
        if(retain_seek && !retained)return false;
        const auto actor=plan.proposed().actor;
        auto* job=find(actor);
        if(job && job->live){job->cursor=job->live->cursor();job->old_assignment=job->live->binding();}
        if(!intents_.accepted(plan))return false;
        const auto source=intents_.state(actor);if(!source)return false;
        if(plan.reason()==OriginalRouteIntents::Cause::route) {
            if(job && !retained)unbind_control(*job);
            if(!source->cursor)return false;
            Job next;next.actor=actor;next.source_revision=source->revision;
            next.cursor=*source->cursor;next.cleanup=cleanup;
            if(job)next.old_assignment=job->old_assignment;
            if(retained) {
                next.live=OriginalRouteConsumer::accepted(next.cursor,actor,*retained,cleanup,true);
                if(!next.live)return false;
                next.old_assignment=*retained;next.pending=false;
                // The existing native callback already dispatches by actor to
                // this controller. Preserve its actual accepted epoch/binding.
            }
            jobs_.insert_or_assign(actor.handle,std::move(next));return true;
        }
        if(!job)return true; // another source objective, never create a route job
        job->source_revision=source->revision;
        if(plan.reason()==OriginalRouteIntents::Cause::running) {
            // The source running flag affects the CURRENT seek, not only the
            // next route point. Request a new accepted motion at the SAME target.
            if(job->live && !job->ended){unbind_control(*job);job->pending=true;job->live.reset();}
            return true;
        }
        if(source->current==OriginalRouteIntents::follow_route) {
            if(plan.previous().current==OriginalRouteIntents::follow_route)return true;
            // Retain the advanced cursor, not source->cursor (assignment seed).
            unbind_control(*job);job->suspended=false;job->pending=!job->ended;job->live.reset();return true;
        }
        // Source ProcessObjective may restore FOLLOW_ROUTE after any objective
        // that stored it, including non-temporary objectives such as WAIT_IN_CAR.
        // Keep exactly that suspended route job while FOLLOW_ROUTE is the
        // actual previous objective; never infer restoration from task loss.
        if(source->previous==OriginalRouteIntents::follow_route) {
            unbind_control(*job);job->suspended=true;job->pending=false;job->live.reset();return true;
        }
        unbind_control(*job);
        jobs_.erase(actor.handle);return true;
    }
    bool needs_assignment(ObjectiveActor actor) {
        const auto job=find(actor);const auto source=intents_.route_intent(actor);
        return job && !job->quarantined && !job->ended && !job->suspended && job->pending && source &&
            source->revision==job->source_revision && source->current_route && !source->temporary;
    }
    std::optional<std::array<float,3>> target(ObjectiveActor actor) {
        const auto job=find(actor);return job && needs_assignment(actor)?std::optional<std::array<float,3>>(job->cursor.target()):std::nullopt;
    }
    // Native factory result is already executed, never dispatch it here again.
    // A restored/new goal requires a genuinely NEW observer epoch/slot, even
    // when an old task record still has identical bytes after interruption.
    bool motion_accepted(ObjectiveActor actor,uint64_t source_revision,const NativeResult& effect) {
        auto* job=find(actor);
        if(!job || !needs_assignment(actor) || source_revision!=job->source_revision)return false;
        if(!effect.executed || !effect.native_entered || !effect.accepted_script_task) {
            if(effect.native_entered || effect.executed)job->quarantined=true;
            return false;
        }
        const auto accepted=host_.accept(*effect.accepted_script_task);
        job=find(actor); // acceptance/profile hooks cannot authorize stale source state
        if(!job || !needs_assignment(actor) || source_revision!=job->source_revision)return false;
        if(!accepted || (job->old_assignment && accepted->epoch==job->old_assignment->epoch &&
                accepted->binding.slot==job->old_assignment->binding.slot)) {
            job->quarantined=true;return false;
        }
        const auto consumer=OriginalRouteConsumer::accepted(job->cursor,actor,*accepted,job->cleanup,native_.track_owned_lifetime);
        if(!consumer){job->quarantined=true;return false;}
        if(native_.track_owned_lifetime) {
            if(!native_.bind_control || !native_.bind_control(accepted->binding,actor,accepted->epoch,
                    &route_control_trampoline,this)) {
                job->quarantined=true;return false;
            }
        }
        job->old_assignment=*accepted;job->live=*consumer;job->pending=false;return true;
    }
    // Dispatch the pending first/restored assignment once. The native factory
    // captures acceptance and this controller binds ControlSubTask before the
    // game can process the task on a later frame.
    NativeResult assign_pending(ObjectiveActor actor) {
        NativeResult result;
        auto* job=find(actor);const auto intent=intents_.route_intent(actor);
        if(!job || !needs_assignment(actor) || !intent || !native_.motion) {
            result.diagnostic="No accepted route assignment is ready";return result;
        }
        const auto revision=job->source_revision;
        struct Gate {OriginalRouteJobs* self;ObjectiveActor actor;uint64_t revision;};
        Gate gate{this,actor,revision};
        const auto authorized=[](void* context)->bool {
            auto& value=*static_cast<Gate*>(context);
            const auto* current=value.self->find(value.actor);
            const auto intent=value.self->intents_.route_intent(value.actor);
            if(!current || current->source_revision!=value.revision || !value.self->needs_assignment(value.actor) ||
               !intent || intent->revision!=value.revision || !intent->current_route || intent->temporary ||
               !value.self->native_.pose)return false;
            const auto pose=value.self->native_.pose(value.actor);
            return pose && pose->actor==value.actor && pose->controllable && pose->on_foot &&
                pose->source_foot_controllable && pose->source_seek_allowed;
        };
        if(!authorized(&gate)) {result.diagnostic="Route actor/source assignment gate is unavailable";return result;}
        RouteMotionRequest request;request.actor=actor;request.target=job->cursor.target();
        request.move_state=intent->running?6:4;request.mission_cleanup=job->cleanup;
        request.authorized=authorized;request.authorization_context=&gate;
        request.track_owned_lifetime=native_.track_owned_lifetime;
        result=native_.motion(request);
        if(!motion_accepted(actor,revision,result) && result.diagnostic.empty())
            result.diagnostic="Route motion was not accepted into a new owned task epoch";
        return result;
    }
    Result tick(ObjectiveActor actor) {
        auto* job=find(actor);if(!job || job->quarantined)return Result::unavailable;
        if(job->suspended)return Result::suspended;
        if(job->ended)return Result::sequence_ended;
        if(job->pending) {
            const auto pose=native_.pose?native_.pose(actor):std::nullopt;
            if(!pose || !(pose->actor==actor) || (!pose->source_foot_controllable && !pose->source_driving))return Result::waiting;
            // ProcessObjective can advance copied route points while SetSeek
            // is blocked by FOLLOW_PATH or driving; it must not steal motion.
            auto next=job->cursor;
            const auto arrival=next.arrive(table_,pose->position);
            if(arrival==OriginalRouteCursor::Result::advanced) {job->cursor=next;return Result::advanced;}
            if(arrival==OriginalRouteCursor::Result::ended) {
                // STOP_WHEN_DONE reached while another native objective owns the ped's movement: no seek was ever
                // assigned, none is invented for the invalid retail point -1. Same outcome as an owned task's end.
                job->cursor=next;job->ended=true;return Result::sequence_ended;
            }
            if(arrival==OriginalRouteCursor::Result::unavailable) {
                job->quarantined=true;return Result::unavailable;
            }
            return Result::waiting;
        }
        if(!job->live)return Result::unavailable;
        NativeRouteConsumerBackend<Host> backend(host_,intents_,job->source_revision,native_);
        const auto result=job->live->tick(table_,backend);
        job->cursor=job->live->cursor();
        job->old_assignment=job->live->binding();
        if(result==Result::sequence_ended)job->ended=true;
        if(result==Result::unavailable)job->quarantined=true;
        return result;
    }
    void retired(ObjectiveActor actor) {
        if(auto* job=find(actor)){unbind_control(*job);jobs_.erase(actor.handle);}
        intents_.retired(actor);
    }
    void untracked_objective(int32_t handle) {
        const auto it=jobs_.find(handle);
        if(it!=jobs_.end()) {unbind_control(it->second);jobs_.erase(it);}
        intents_.untracked_objective(handle);
    }
    std::vector<ObjectiveActor> active_actors()const {
        std::vector<ObjectiveActor> result;result.reserve(jobs_.size());
        for(const auto& [handle,job]:jobs_){(void)handle;result.push_back(job.actor);}
        return result;
    }
    void clear(){for(auto& [handle,job]:jobs_){(void)handle;unbind_control(job);}jobs_.clear();intents_.clear();}
};
}
