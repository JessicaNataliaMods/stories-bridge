#pragma once
#include "route_consumer.hpp"
#include "route_intent.hpp"
#include "owned_route_task.hpp"
#include "sa_executor.hpp"
namespace lcs {
struct RouteNativeFunctions {
    decltype(&sa_route_actor_snapshot) pose=sa_route_actor_snapshot;
    decltype(&sa_active_walking_task) active=sa_active_walking_task;
    decltype(&sa_create_route_motion) motion=sa_create_route_motion;
    decltype(&sa_bind_owned_route_control) bind_control=sa_bind_owned_route_control;
    decltype(&sa_unbind_owned_route_control) unbind_control=sa_unbind_owned_route_control;
    decltype(&sa_retarget_owned_route_task) retarget=sa_retarget_owned_route_task;
    bool track_owned_lifetime=false;
};
// Host must implement genuine observer refresh/accept. No default ownership,
// intent or thread/exclusion proof is supplied by this adapter.
template<class Host> class NativeRouteConsumerBackend final:public RouteConsumerBackend {
    Host& host_;RouteIntentReader& intent_;uint64_t source_revision_;RouteNativeFunctions native_;
    struct Observation {RouteObservation state;uint64_t revision=0;};
    std::optional<Observation> read(ObjectiveActor actor,const EpochScriptTask& expected) {
        if(!native_.pose || !native_.active || !native_.motion)return {};
        const auto intent=intent_.route_intent(actor);
        if(!intent || !(intent->actor==actor) || !source_revision_ || intent->revision!=source_revision_)return {};
        const auto binding=host_.refresh(expected);
        if(!binding || binding->epoch!=expected.epoch || binding->binding.slot!=expected.binding.slot)return {};
        const auto& record=binding->binding.record;
        const bool event=record.status==0 && record.event && !record.task;
        const bool task=(record.status==1 || record.status==2 || record.status==4 || record.status==5 || record.status==6) && record.task && !record.event;
        if(record.opcode!=0x05D3 || record.ped!=actor.native_ped || (!event && !task))return {};
        const auto pose=native_.pose(actor);if(!pose || !(pose->actor==actor))return {};
        bool active=false;
        if(!binding->binding.record.event) {
            const auto found=native_.active(actor,binding->binding);if(!found)return {};
            active=*found;
        }
        // Reads may encounter a callback/source mutation. Do not mix an old
        // source objective with a freshly restored or replaced native receipt.
        const auto current=intent_.route_intent(actor);const auto live=host_.refresh(*binding);
        if(!current || !live || !(current->actor==actor) || current->revision!=intent->revision ||
           current->current_route!=intent->current_route || current->temporary!=intent->temporary || current->running!=intent->running ||
           live->epoch!=binding->epoch || live->binding.slot!=binding->binding.slot ||
           !(live->binding.record==binding->binding.record))return {};
        return Observation{{actor,*binding,pose->position,intent->current_route,intent->temporary,
            pose->source_foot_controllable && pose->source_seek_allowed,pose->on_foot,active,intent->running},intent->revision};
    }
    struct Authorization {
        NativeRouteConsumerBackend* backend;ObjectiveActor actor;EpochScriptTask expected;
        uint64_t revision;bool running;
    };
    static bool authorized(void* context) {
        auto& a=*static_cast<Authorization*>(context);
        const auto value=a.backend->read(a.actor,a.expected);
        return value && value->revision==a.revision && value->state.current_route && !value->state.temporary &&
            value->state.controllable && value->state.on_foot && value->state.motion_active &&
            !value->state.binding.binding.record.event && value->state.running==a.running;
    }
public:
    NativeRouteConsumerBackend(Host& host,RouteIntentReader& intent,uint64_t accepted_source_revision,RouteNativeFunctions native={}):host_(host),intent_(intent),source_revision_(accepted_source_revision),native_(native){}
    std::optional<RouteObservation> observe(ObjectiveActor actor,const EpochScriptTask& expected)override {
        const auto value=read(actor,expected);return value?std::optional<RouteObservation>(value->state):std::nullopt;
    }
    RouteReplacement replace(ObjectiveActor actor,const EpochScriptTask& expected,const std::array<float,3>& point,bool running,bool cleanup)override {
        const auto before=read(actor,expected);if(!before)return {};
        Authorization authorization{this,actor,expected,before->revision,running};
        if(!authorized(&authorization))return {};
        RouteMotionRequest request{actor,point,running?6:4,cleanup,authorized,&authorization};
        request.track_owned_lifetime=native_.track_owned_lifetime;
        const auto result=native_.motion(request);
        RouteReplacement replacement{result.executed,result.native_entered,{}};
        // A successful engine effect is never replayed because capture failed.
        // An executor's advisory receipt is accepted ONLY through live epochs.
        if(result.executed && result.native_entered && result.accepted_script_task)
            replacement.accepted=host_.accept(*result.accepted_script_task);
        return replacement;
    }
    OwnedRouteRetargetResult retarget(ObjectiveActor actor,const EpochScriptTask& expected,const std::array<float,3>& point,bool running,bool cleanup)override {
        (void)cleanup; // Same owned task retains its existing native mission context.
        if(!native_.retarget)return {};
        const auto before=read(actor,expected);if(!before)return {};
        Authorization authorization{this,actor,expected,before->revision,running};
        if(!authorized(&authorization) || before->state.binding.binding.record.event ||
           !before->state.binding.binding.record.task)return {};
        return native_.retarget(before->state.binding.binding.record.task,actor,
            before->state.binding.epoch,point,authorized,&authorization);
    }
};
}
