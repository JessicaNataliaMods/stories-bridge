#pragma once

#include "lcs/cop_native_translation.hpp"
#include "lcs/entity_commands.hpp"
#include "lcs/owned_route_host.hpp"
#include "lcs/route_jobs.hpp"
#include "lcs/route_vehicle_goal.hpp"
#include "lcs/route_effects.hpp"
#include "lcs/sa_executor.hpp"
#include "lcs/task_observer.hpp"
#include "ScriptCommands.h"
#include "sa_owned_vehicle_entry.hpp"
#include <array>
#include <cmath>
#include <algorithm>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lcs {

// Connects accepted original objective setters to the source ledger and to
// the owned native route consumer. It is called only on the game thread.
class SaRouteService {
public:
    using ActorResolver=std::function<std::optional<ObjectiveActor>(int32_t)>;
    using SourceStateResolver=std::function<std::optional<int32_t>(ObjectiveActor)>;
    using ClearWalking=std::function<void(int32_t)>;
    using NativeExecutor=std::function<NativeResult(const NativeCall&)>;

private:
    struct VehicleReference {
        RouteVehicleSnapshot snapshot;
        uintptr_t cell=0;
    };
    struct PedReference { ObjectiveActor target;uintptr_t cell=0; };
    struct RememberedVehicle {
        ObjectiveActor actor;
        std::unique_ptr<VehicleReference> reference;
    };
    struct ExitReceipt {
        uint64_t revision=0;
        SourceEntityIdentity target;
    };
    struct EntryReceipt {
        uint64_t revision=0;
        SourceEntityIdentity target;
        ObjectiveActor actor;
        uint64_t epoch=0;
    };
    struct ObjectiveSpec {
        int32_t goal=-1;
        OriginalRouteIntents::Setter setter=OriginalRouteIntents::Setter::plain;
        enum class Target {none,ped,vehicle} target=Target::none;
        bool vehicle=false;
    };
    struct VehicleEffectGate {
        SaRouteService* self=nullptr;
        ObjectiveActor actor;
        uint64_t revision=0;
        SourceEntityIdentity target;
        const OriginalRouteIntents::Plan* restoration_plan=nullptr;
        uintptr_t* transient_target_cell=nullptr;
        int32_t source_state=-1;
    };

    // The SCM latch follows the last script objective, not every internal SA
    // task transition. Source intent revisions provide the owner; task receipts
    // and weak target references provide the live evidence used by each watch.
    enum class ObjectiveWatchKind {unknown,never,position,enter_car,destroy_car,kill_char,goto_char};
    struct ObjectiveWatch {
        ObjectiveActor actor;
        ObjectiveWatchKind kind=ObjectiveWatchKind::unknown;
        uint64_t source_revision=0;
        int32_t source_current=OriginalRouteIntents::none;
        SourceEntityIdentity target;
        ObjectiveActor target_actor;
        std::array<float,3> point{};
        std::optional<EpochScriptTask> walking_receipt; // observed receipt: event->task association is tracked by the hooks
        std::unique_ptr<PedReference> ped_reference;
        // Walking objectives (05D3/05D9). armed_ms is the game time of the latest (re)issue: it stands for the LCS
        // m_nPedStateTimer that SetSeek arms. `reissue` is the exact accepted native packet, replayed only to keep a
        // still-current source objective alive after SA released its task without the ped arriving.
        std::optional<NativeCall> reissue;
        uint32_t armed_ms=0,next_rearm_ms=0;
        int rearms=0;
        bool released_arrived=false,rearm_pending=false;
        bool completed=false,reset_pending=false;
    };
    // Registry read by the release probe from inside the native task-release notification. Plain data only: the probe
    // never walks the watch map and never calls into the engine.
    struct WalkProbe {
        bool used=false;
        ObjectiveWatchKind kind=ObjectiveWatchKind::unknown;
        ObjectiveActor actor,target;
        std::array<float,3> point{};
        uint64_t epoch=0;
        uint32_t armed_ms=0;
    };
    static constexpr uint32_t goto_char_timer_ms=600,rearm_delay_ms=1000;
    static constexpr int rearm_budget=8;

    OriginalRouteIntents& intents_;
    const OriginalRouteTable& table_;
    OwnedRouteTaskHost host_;
    OriginalRouteJobs<OwnedRouteTaskHost> jobs_;
    ActorResolver actor_;
    SourceStateResolver source_state_;
    ClearWalking clear_walking_;
    NativeExecutor vehicle_executor_;
    OriginalRouteVehicleGoals vehicle_goals_;
    std::unordered_map<int32_t,bool> cleanup_;
    std::unordered_map<int32_t,std::unique_ptr<VehicleReference>> vehicle_refs_;
    std::unordered_map<int32_t,std::unique_ptr<PedReference>> ped_refs_;
    // Original m_pMyVehicle, separate from m_carInObjective. Only actual
    // observed occupancy supplies it; no on-foot native pointer inference.
    std::unordered_map<int32_t,RememberedVehicle> remembered_vehicles_;
    std::unordered_map<int32_t,ExitReceipt> exits_;
    std::unordered_map<int32_t,EntryReceipt> entries_;
    std::unordered_map<int32_t,ObjectiveActor> vehicle_actors_;
    std::unordered_map<int32_t,ObjectiveWatch> objective_watches_;
    float enter_car_range_multiplier_=1.0f;
    // Production receipts for script tasks that are not owned lifetimes (walking objectives). accept() is called once, right
    // after the accepted native command; refresh() returns the observed current binding or nothing when ownership was lost.
    std::function<std::optional<EpochScriptTask>(const CapturedScriptTask&)> walking_accept_;
    std::function<std::optional<EpochScriptTask>(const EpochScriptTask&)> walking_refresh_;
    std::function<std::optional<ScriptTaskRelease>(const EpochScriptTask&)> walking_released_;
    NativeExecutor walking_reissue_;
    std::array<WalkProbe,128> walk_probes_{};
    bool servicing_=false;
    uint32_t service_now_=0;
    std::string last_diagnostic_;
    const char* objective_reason_="";

    // If native reference cleanup cannot prove unlinking, keep the cell alive
    // for the rest of the process. Freeing it would leave SA with a dangling
    // CEntity reference slot.
    static std::vector<std::unique_ptr<VehicleReference>>& quarantined_vehicle_refs() {
        static auto* refs=new std::vector<std::unique_ptr<VehicleReference>>;
        return *refs;
    }
    static void release_ped_reference(std::unique_ptr<PedReference>& ref) {
        if(!ref)return;
        if(sa_release_actor_reference(&ref->cell)) {ref.reset();return;}
        static auto* quarantine=new std::vector<std::unique_ptr<PedReference>>;
        quarantine->push_back(std::move(ref));
    }
    void forget_ped_reference(int32_t actor) {
        const auto found=ped_refs_.find(actor);if(found==ped_refs_.end())return;
        release_ped_reference(found->second);ped_refs_.erase(found);
    }

    static SourceEntityIdentity vehicle_identity(const RouteVehicleSnapshot& value) {
        return {value.handle,value.entity,value.generation};
    }
    static SourceEntityIdentity ped_identity(const ObjectiveActor& value) {
        return {value.handle,value.native_ped,uint8_t(value.handle)};
    }
    static bool is_dying(int32_t state) {return state==54 || state==55;}
    static bool is_source_control(int32_t state) {return state>=0 && state<39;}
    static NativeResult accepted_without_entry() {
        NativeResult result;result.executed=true;return result;
    }
    static std::optional<ObjectiveSpec> source_objective(uint16_t opcode) {
        using Setter=OriginalRouteIntents::Setter;
        using Target=ObjectiveSpec::Target;
        switch(opcode) {
        case COMMAND_SET_CHAR_OBJ_GOTO_COORD_ON_FOOT:return ObjectiveSpec{24,Setter::position,Target::none,false};
        case COMMAND_SET_CHAR_OBJ_RUN_TO_COORD:return ObjectiveSpec{25,Setter::position,Target::none,false};
        case COMMAND_SET_CHAR_OBJ_SPRINT_TO_COORD:return ObjectiveSpec{49,Setter::position,Target::none,false};
        case COMMAND_SET_CHAR_OBJ_WAIT_ON_FOOT:case COMMAND_CHAR_SET_IDLE:return ObjectiveSpec{1,Setter::plain,Target::none,false};
        case COMMAND_SET_CHAR_OBJ_WAIT_IN_CAR:return ObjectiveSpec{6,Setter::plain,Target::none,false};
        case COMMAND_SET_CHAR_OBJ_NO_OBJ:return ObjectiveSpec{0,Setter::plain,Target::none,false};
        case COMMAND_SET_CHAR_OBJ_FLEE_ON_FOOT_TILL_SAFE:return ObjectiveSpec{3,Setter::plain,Target::none,false};
        case COMMAND_SET_CHAR_OBJ_KILL_CHAR_ON_FOOT:case COMMAND_SET_CHAR_OBJ_KILL_PLAYER_ON_FOOT:
            return ObjectiveSpec{8,Setter::entity,Target::ped,false};
        case COMMAND_SET_CHAR_OBJ_KILL_CHAR_ANY_MEANS:case COMMAND_SET_CHAR_OBJ_KILL_PLAYER_ANY_MEANS:
            return ObjectiveSpec{9,Setter::entity,Target::ped,false};
        case COMMAND_SET_CHAR_OBJ_FLEE_CHAR_ON_FOOT_TILL_SAFE:
            return ObjectiveSpec{10,Setter::entity,Target::ped,false};
        case COMMAND_SET_CHAR_OBJ_FLEE_CHAR_ON_FOOT_ALWAYS:case COMMAND_SET_CHAR_OBJ_FLEE_PLAYER_ON_FOOT_ALWAYS:
            return ObjectiveSpec{11,Setter::entity,Target::ped,false};
        case COMMAND_SET_CHAR_OBJ_GOTO_CHAR_ON_FOOT:case COMMAND_SET_CHAR_OBJ_GOTO_PLAYER_ON_FOOT:
            return ObjectiveSpec{12,Setter::entity,Target::ped,false};
        case COMMAND_SET_CHAR_OBJ_WALK_TO_CHAR:return ObjectiveSpec{13,Setter::entity,Target::ped,false};
        case COMMAND_SET_CHAR_OBJ_AIM_GUN_AT_CHAR:return ObjectiveSpec{45,Setter::entity,Target::ped,false};
        case COMMAND_SET_CHAR_OBJ_DESTROY_CAR:return ObjectiveSpec{22,Setter::entity,Target::vehicle,false};
        case COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_DRIVER:return ObjectiveSpec{18,Setter::entity,Target::vehicle,true};
        case COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_PASSENGER:return ObjectiveSpec{17,Setter::entity,Target::vehicle,true};
        case COMMAND_SET_CHAR_OBJ_LEAVE_CAR:return ObjectiveSpec{16,Setter::entity,Target::vehicle,true};
        case COMMAND_SET_CHAR_OBJ_LEAVE_ANY_CAR:return ObjectiveSpec{16,Setter::plain,Target::vehicle,true};
        default:return {};
        }
    }
    std::optional<RouteActorVehicleSnapshot> source_actor_facts(ObjectiveActor actor)const {
        auto facts=sa_route_actor_vehicle_snapshot(actor);
        if(!facts)return {};
        const auto source=intents_.state(actor);
        if(source && source->source_created_by) {
            facts->source_created_by=source->source_created_by;
            facts->mission_character=*source->source_created_by==2;
        }
        return facts;
    }
    bool known_actor(int32_t handle,ObjectiveActor& actor,std::optional<int32_t>& state,
            std::optional<RouteActorVehicleSnapshot>& facts) {
        if(!actor_ || !source_state_ || handle<0)return false;
        const auto resolved=actor_(handle);if(!resolved || resolved->handle!=handle)return false;
        const auto translated=source_state_(*resolved);if(!translated)return false;
        const auto snapshot=source_actor_facts(*resolved);
        if(!snapshot || !(snapshot->actor==*resolved))return false;
        actor=*resolved;state=*translated;facts=*snapshot;return true;
    }
    std::optional<VehicleReference*> current_reference(ObjectiveActor actor,SourceEntityIdentity expected) {
        const auto found=vehicle_refs_.find(actor.handle);
        if(found==vehicle_refs_.end() || !found->second || !(vehicle_identity(found->second->snapshot)==expected))return {};
        auto* ref=found->second.get();
        if(ref->cell==expected.native_entity && sa_vehicle_reference_live(ref->snapshot,&ref->cell))return ref;
        if(ref->cell==0)return ref; // The registered source weak reference was actually cleared.
        return {};
    }
    bool release_reference(std::unique_ptr<VehicleReference>& ref) {
        if(!ref)return true;
        if(sa_release_vehicle_reference(ref->snapshot,&ref->cell))return true;
        quarantined_vehicle_refs().push_back(std::move(ref));
        return false;
    }
    void forget_vehicle_reference(int32_t handle) {
        auto it=vehicle_refs_.find(handle);if(it==vehicle_refs_.end())return;
        release_reference(it->second);vehicle_refs_.erase(it);
    }
    std::unique_ptr<VehicleReference> prepare_vehicle_reference(const RouteVehicleSnapshot& snapshot) {
        auto ref=std::make_unique<VehicleReference>();ref->snapshot=snapshot;
        if(!sa_register_vehicle_reference(snapshot.handle,&ref->cell)) {
            // A failed native registration may already retain the cell.
            release_reference(ref);return {};
        }
        return ref;
    }
    void forget_remembered_vehicle(int32_t handle) {
        const auto it=remembered_vehicles_.find(handle);if(it==remembered_vehicles_.end())return;
        release_reference(it->second.reference);remembered_vehicles_.erase(it);
    }
    bool observe_current_vehicle(ObjectiveActor actor,const RouteActorVehicleSnapshot& facts) {
        if(!facts.in_vehicle)return true;
        if(!(facts.actor==actor))return false;
        const auto car=sa_route_vehicle_snapshot(facts.current_vehicle_handle);
        if(!car || car->entity!=facts.current_vehicle_entity || car->generation!=facts.current_vehicle_generation) {
            forget_remembered_vehicle(actor.handle);return false;
        }
        const auto old=remembered_vehicles_.find(actor.handle);
        if(old!=remembered_vehicles_.end() && old->second.actor==actor && old->second.reference) {
            const auto& ref=*old->second.reference;
            if(vehicle_identity(ref.snapshot)==vehicle_identity(*car) && ref.cell==car->entity &&
               sa_vehicle_reference_live(*car,&ref.cell))return true;
        }
        auto ref=prepare_vehicle_reference(*car);
        if(!ref) {forget_remembered_vehicle(actor.handle);return false;}
        forget_remembered_vehicle(actor.handle);
        remembered_vehicles_.emplace(actor.handle,RememberedVehicle{actor,std::move(ref)});return true;
    }
    void adopt_vehicle_reference(ObjectiveActor actor,std::unique_ptr<VehicleReference> next) {
        auto old=std::move(vehicle_refs_[actor.handle]);
        if(old && old->snapshot.entity==next->snapshot.entity && old->snapshot.handle==next->snapshot.handle &&
           old->snapshot.generation==next->snapshot.generation &&
           old->cell==next->snapshot.entity && sa_vehicle_reference_live(old->snapshot,&old->cell)) {
            vehicle_refs_[actor.handle]=std::move(old);
            release_reference(next);return;
        }
        if(old)release_reference(old);
        vehicle_refs_[actor.handle]=std::move(next);
    }
    bool ref_is_live(ObjectiveActor actor,SourceEntityIdentity identity,const RouteVehicleSnapshot& snapshot) {
        const auto ref=current_reference(actor,identity);
        return ref && (*ref)->cell==identity.native_entity &&
            vehicle_identity((*ref)->snapshot)==identity && sa_vehicle_reference_live(snapshot,&(*ref)->cell);
    }
    void synchronize_vehicle_reference(ObjectiveActor actor,const OriginalRouteIntents::State& state,
            std::unique_ptr<VehicleReference> accepted_target={}) {
        if((state.current==16 || state.current==17 || state.current==18 || state.current==22) && state.target.valid()) {
            if(accepted_target)adopt_vehicle_reference(actor,std::move(accepted_target));
            return;
        }
        forget_vehicle_reference(actor.handle);
        release_reference(accepted_target);
    }
    void clear_walk_probe(const ObjectiveWatch& watch) {
        if(watch.walking_receipt && watch.walking_receipt->binding.slot<walk_probes_.size() &&
           walk_probes_[watch.walking_receipt->binding.slot].actor==watch.actor)
            walk_probes_[watch.walking_receipt->binding.slot]=WalkProbe{};
    }
    void register_walk_probe(const ObjectiveWatch& watch) {
        if(!watch.walking_receipt || watch.walking_receipt->binding.slot>=walk_probes_.size())return;
        WalkProbe probe;probe.used=true;probe.kind=watch.kind;probe.actor=watch.actor;probe.target=watch.target_actor;
        probe.point=watch.point;probe.epoch=watch.walking_receipt->epoch;probe.armed_ms=watch.armed_ms;
        walk_probes_[watch.walking_receipt->binding.slot]=probe;
    }
    void retire_objective_watch(int32_t handle) {
        const auto found=objective_watches_.find(handle);if(found==objective_watches_.end())return;
        clear_walk_probe(found->second);
        release_ped_reference(found->second.ped_reference);objective_watches_.erase(found);
    }
    void unknown_objective_watch(ObjectiveActor actor,bool preserve_latch=false) {
        bool completed=false;
        const auto old=objective_watches_.find(actor.handle);
        if(preserve_latch && old!=objective_watches_.end() && old->second.actor==actor)
            completed=old->second.completed;
        retire_objective_watch(actor.handle);
        ObjectiveWatch watch;watch.actor=actor;watch.kind=ObjectiveWatchKind::unknown;watch.completed=completed;
        objective_watches_.emplace(actor.handle,std::move(watch));
    }
    static uint32_t frame_now() {const auto clock=sa_frame_clock();return clock?clock->now:0;}
    void watch_source_state(ObjectiveActor actor,const OriginalRouteIntents::State& state,
            const NativeResult* effect=nullptr,const NativeCall* call=nullptr) {
        retire_objective_watch(actor.handle);
        ObjectiveWatch watch;watch.actor=actor;watch.source_revision=state.revision;
        watch.source_current=state.current;watch.target=state.target;
        switch(state.current) {
        case OriginalRouteIntents::none:
            // With no source objective, the script latch is known to be clear.
            watch.kind=ObjectiveWatchKind::never;break;
        case 1:case 3:case 6:case 10:case 11:case 16:case 45:
            // Source ProcessObjective has no path that publishes the script
            // latch for wait, flee, leave-car or aim objectives (the writers are
            // goto-coord/area, goto-char, enter-car, destroy, kill, steal and
            // solicit/attractor; LEAVE_CAR only restores the previous objective).
            // Only the ledger revision can retire this goal, so the latch is a
            // known FALSE, never a guess about SA task progress.
            watch.kind=ObjectiveWatchKind::never;break;
        case OriginalRouteIntents::follow_route:
            // FOLLOW_ROUTE completion is owned by the route job, not the latch.
            break;
        case 12:case 13: {
            // Proximity is only evidence while the exact accepted SA 05D9 task is still the actor's active
            // task, and only for the live weak target reference (source PedAI GOTO_CHAR_ON_FOOT[_WALKING]).
            if(!state.target.valid() || !effect || effect->task_capture!=NativeResult::TaskCapture::captured ||
               !effect->accepted_script_task || effect->accepted_script_task->record.ped!=actor.native_ped ||
               effect->accepted_script_task->record.opcode!=0x05D9)break;
            const auto target=sa_live_objective_actor(state.target.handle);
            if(!target || target->native_ped!=state.target.native_entity || *target==actor)break;
            watch.target_actor=*target;
            watch.ped_reference=std::make_unique<PedReference>();
            watch.ped_reference->target=*target;
            if(!sa_register_actor_reference(*target,&watch.ped_reference->cell)) {
                release_ped_reference(watch.ped_reference);break;
            }
            if(!walking_accept_) {release_ped_reference(watch.ped_reference);break;}
            if(auto receipt=walking_accept_(*effect->accepted_script_task)) {
                watch.kind=ObjectiveWatchKind::goto_char;watch.walking_receipt=*receipt;
                if(call)watch.reissue=*call;
                watch.armed_ms=frame_now();
                register_walk_probe(watch);
            } else release_ped_reference(watch.ped_reference);
            break;
        }
        case 24:case 25:case 49:
            if(state.target_position && effect &&
               effect->task_capture==NativeResult::TaskCapture::captured && effect->accepted_script_task &&
               effect->accepted_script_task->record.ped==actor.native_ped &&
               effect->accepted_script_task->record.opcode==0x05D3 && walking_accept_) {
                if(auto receipt=walking_accept_(*effect->accepted_script_task)) {
                    watch.kind=ObjectiveWatchKind::position;watch.point=*state.target_position;
                    watch.walking_receipt=*receipt;
                    if(call)watch.reissue=*call;
                    watch.armed_ms=frame_now();
                    register_walk_probe(watch);
                }
            }
            break;
        case 17:case 18:
            if(state.target.valid() && current_reference(actor,state.target))
                watch.kind=ObjectiveWatchKind::enter_car;
            break;
        case 22:
            if(state.target.valid() && current_reference(actor,state.target))
                watch.kind=ObjectiveWatchKind::destroy_car;
            break;
        case 8:case 9: {
            if(!state.target.valid())break;
            const auto target=sa_live_objective_actor(state.target.handle);
            if(!target || target->native_ped!=state.target.native_entity)break;
            watch.target_actor=*target;
            watch.ped_reference=std::make_unique<PedReference>();
            watch.ped_reference->target=*target;
            if(!sa_register_actor_reference(*target,&watch.ped_reference->cell)) {
                release_ped_reference(watch.ped_reference);break;
            }
            watch.kind=ObjectiveWatchKind::kill_char;break;
        }
        default:break;
        }
        // The source setter clears the latch and only the ped's next objective
        // update may publish it. The query must not complete a watch before the
        // frame consumer (service_objective_watches) has run once.
        watch.reset_pending=true;
        objective_watches_.emplace(actor.handle,std::move(watch));
    }
    void advance_objective_watch(ObjectiveActor actor,const OriginalRouteIntents::State& state) {
        const auto found=objective_watches_.find(actor.handle);
        if(found==objective_watches_.end() || !(found->second.actor==actor))return;
        found->second.source_revision=state.revision;
        found->second.source_current=state.current;
    }
    void note_script_objective(uint16_t original,const NativeCall& call,bool projected,
            const std::optional<uint64_t>& previous_revision,const NativeResult& result) {
        (void)original;
        if(call.inputs.empty())return;
        const auto live=sa_live_objective_actor(call.inputs[0]);
        if(!live) {retire_objective_watch(call.inputs[0]);return;}
        const auto state=intents_.state(*live);
        if(!projected || !state) {unknown_objective_watch(*live);return;}
        // An ignored source setter still clears the SCM latch before SetObjective.
        // It keeps the real active goal and target, even when the ignored packet
        // describes a different goal (for example a self-target GOTO during KILL).
        if(previous_revision && state->revision==*previous_revision) {
            auto found=objective_watches_.find(live->handle);
            if(found!=objective_watches_.end() && found->second.actor==*live) {
                found->second.completed=false;found->second.reset_pending=true;found->second.source_revision=state->revision;
                found->second.source_current=state->current;return;
            }
        }
        watch_source_state(*live,*state,&result,&call);
    }
    // Event -> task -> active proof of the accepted walking command, through the production receipt observer.
    // lost: ownership cannot be shown (replaced, released, observer unhealthy): never TRUE, never FALSE.
    // pending: the receipt is still the queued event. active: the associated task is the actor's active task.
    enum class WalkingOwnership {lost,pending,inactive,active};
    WalkingOwnership walking_ownership(ObjectiveWatch& watch) {
        if(!watch.walking_receipt || !walking_refresh_)return WalkingOwnership::lost;
        const auto current=walking_refresh_(*watch.walking_receipt);
        if(!current)return WalkingOwnership::lost;
        watch.walking_receipt=*current;
        const auto& record=current->binding.record;
        if(record.ped!=watch.actor.native_ped)return WalkingOwnership::lost;
        if(record.status==0 && record.event && !record.task)return WalkingOwnership::pending;
        const auto owned=sa_active_walking_task(watch.actor,current->binding);
        if(!owned)return WalkingOwnership::lost;
        // A task that is allocated but not in the active primary chain is not proof of release. Do not fold this state
        // together with a retired receipt: the bank record can remain byte-identical while a task is suspended.
        return *owned?WalkingOwnership::active:WalkingOwnership::inactive;
    }
    // ---- walking-objective arrival ------------------------------------------------------------------------------------
    // Live arrival test at this instant (never a cached sample). false while the ped is seated; nullopt when an identity
    // or position needed for the verdict is unreadable. Position goals use the planar 0.5m radius of the source
    // goto-coord completion; GOTO_CHAR uses the source 3D safe distance (2m, 3m to the vehicle of a seated target).
    static std::optional<bool> arrival_now(ObjectiveWatchKind kind,ObjectiveActor actor,ObjectiveActor target,
            const std::array<float,3>& point) {
        const auto ped=sa_route_actor_vehicle_snapshot(actor);
        if(!ped || !(ped->actor==actor) || !ped->alive)return std::nullopt;
        if(ped->in_vehicle)return false;
        if(kind==ObjectiveWatchKind::position) {
            const float dx=point[0]-ped->position[0],dy=point[1]-ped->position[1];
            const float distance=std::sqrt(dx*dx+dy*dy);
            if(!std::isfinite(distance))return std::nullopt;
            return distance<=0.5f;
        }
        if(kind!=ObjectiveWatchKind::goto_char)return std::nullopt;
        const auto live_target=sa_live_objective_actor(target.handle);
        if(!live_target || !(*live_target==target))return std::nullopt;
        const auto goal_ped=sa_route_actor_vehicle_snapshot(target);
        if(!goal_ped || !(goal_ped->actor==target) || !goal_ped->alive)return std::nullopt;
        std::array<float,3> goal=goal_ped->position;float safe=2.0f;
        if(goal_ped->in_vehicle) {
            const auto car=sa_route_vehicle_snapshot(goal_ped->current_vehicle_handle);
            if(!car || car->entity!=goal_ped->current_vehicle_entity || car->generation!=goal_ped->current_vehicle_generation)
                return std::nullopt;
            goal=car->position;safe=3.0f;
        }
        const float dx=goal[0]-ped->position[0],dy=goal[1]-ped->position[1],dz=goal[2]-ped->position[2];
        const float distance=std::sqrt(dx*dx+dy*dy+dz*dz);
        if(!std::isfinite(distance))return std::nullopt;
        return distance<=safe;
    }
    // Called by the task observer from inside the native task-release notification (before the task is destroyed).
    std::optional<int> release_probe(const EpochScriptTask& token) const {
        const auto slot=token.binding.slot;
        if(slot>=walk_probes_.size() || !walk_probes_[slot].used)return std::nullopt;
        const auto& probe=walk_probes_[slot];
        if(probe.epoch!=token.epoch || token.binding.record.ped!=probe.actor.native_ped)return std::nullopt;
        const auto near=arrival_now(probe.kind,probe.actor,probe.target,probe.point);
        if(!near)return std::nullopt;
        if(!*near)return ScriptTaskRelease::not_arrived;
        const bool timed=probe.kind==ObjectiveWatchKind::goto_char;
        return !timed || frame_now()>probe.armed_ms+goto_char_timer_ms?ScriptTaskRelease::arrived:ScriptTaskRelease::arrived_early;
    }
    // SA released the task of this watch's receipt while the source objective is still current. The source objective
    // persists (the LCS ped keeps seeking), so the exact accepted packet is replayed, bounded and paced, once the ped
    // can walk again. Only the frame consumer may do this, never a script query.
    bool rearm_walking(ObjectiveWatch& watch) {
        if(!watch.reissue || !walking_reissue_ || !walking_accept_)return false;
        if(watch.rearms>=rearm_budget)return false;          // a task SA keeps freeing away from the target: unknown, not endless
        if(!servicing_)return true;                           // a script query only reports FALSE; the frame consumer re-arms
        if(service_now_<watch.next_rearm_ms)return true;
        const auto ped=sa_route_actor_vehicle_snapshot(watch.actor);
        if(!ped || !(ped->actor==watch.actor) || !ped->alive)return false;
        if(ped->in_vehicle)return true;                       // the objective waits; SA must be able to walk it again
        if(watch.kind==ObjectiveWatchKind::goto_char && !arrival_now(watch.kind,watch.actor,watch.target_actor,watch.point))
            return true;
        ++watch.rearms;watch.next_rearm_ms=service_now_+rearm_delay_ms;
        const auto effect=walking_reissue_(*watch.reissue);
        if(!effect.executed || effect.task_capture!=NativeResult::TaskCapture::captured || !effect.accepted_script_task ||
           effect.accepted_script_task->record.ped!=watch.actor.native_ped ||
           effect.accepted_script_task->record.opcode!=watch.reissue->opcode) {
            if(effect.native_entered && !effect.executed)return false;      // an uncertain native write is never replayed
            return true;                                                     // refused before native entry: retry later
        }
        const auto receipt=walking_accept_(*effect.accepted_script_task);
        if(!receipt)return false;
        clear_walk_probe(watch);
        watch.walking_receipt=*receipt;watch.armed_ms=service_now_;
        watch.released_arrived=false;watch.rearm_pending=false;
        register_walk_probe(watch);
        return true;
    }
    // Shared by position and GOTO_CHAR. Ownership: active (the associated task is the actor's active task), pending (still the
    // queued event), inactive (allocated but not active), lost (released/replaced/observer unavailable). A lost receipt
    // is completion only through the release evidence recorded by the observer at the moment SA freed the task.
    std::optional<bool> evaluate_walking(ObjectiveWatch& watch,bool timed) {
        auto ownership=walking_ownership(watch);
        if(ownership==WalkingOwnership::lost && !watch.released_arrived && !watch.rearm_pending) {
            const auto release=walking_released_&&watch.walking_receipt?walking_released_(*watch.walking_receipt):std::nullopt;
            if(!release){objective_reason_="walk task replaced or not observed (ownership lost, no release verdict)";return std::nullopt;}     // replaced by a foreign writer, flushed, or observation lost: nothing is proven
            if(release->probed && release->verdict==ScriptTaskRelease::arrived) {
                // SA finished the walk at the target inside one frame and freed the task: that is the completion, whatever
                // the ped does afterwards.
                watch.completed=true;release_ped_reference(watch.ped_reference);clear_walk_probe(watch);
                return true;
            }
            if(release->probed && release->verdict==ScriptTaskRelease::arrived_early)watch.released_arrived=true;
            else watch.rearm_pending=true;
        }
        const auto near=arrival_now(watch.kind,watch.actor,watch.target_actor,watch.point);
        if(!near){objective_reason_="arrival position unreadable";return std::nullopt;}
        const bool timer=!timed || service_now_>watch.armed_ms+goto_char_timer_ms;
        if(ownership==WalkingOwnership::pending || ownership==WalkingOwnership::inactive)return false;
        if(ownership==WalkingOwnership::active || watch.released_arrived) {
            if(*near && timer) {
                watch.completed=true;release_ped_reference(watch.ped_reference);clear_walk_probe(watch);
                return true;
            }
            if(ownership==WalkingOwnership::active)return false;
            // Released at the target before the source timer elapsed: wait for it; if the ped has left meanwhile this
            // was not a completion and the objective is re-armed.
            if(!timer)return false;
            watch.released_arrived=false;watch.rearm_pending=true;
        }
        if(watch.rearm_pending && !rearm_walking(watch)){objective_reason_="walk could not be re-armed";return std::nullopt;}
        return false;
    }
    std::optional<bool> evaluate_objective_watch(ObjectiveWatch& watch) {
        if(watch.kind==ObjectiveWatchKind::unknown){objective_reason_="objective was set by a command the bridge does not track (watch kind unknown)";return std::nullopt;}
        const auto live=sa_live_objective_actor(watch.actor.handle);
        if(!live || !(*live==watch.actor)){objective_reason_="ped identity changed";return std::nullopt;}
        const auto intent=intents_.state(watch.actor);
        if(!intent || intent->revision!=watch.source_revision || intent->current!=watch.source_current){
            objective_reason_=!intent?"no route intent for the ped":"route intent changed since the objective was set";
            return std::nullopt;
        }
        if(watch.completed)return true;
        if(watch.kind==ObjectiveWatchKind::never)return false;
        const auto ped=sa_route_actor_vehicle_snapshot(watch.actor);
        if(!ped || !(ped->actor==watch.actor) || !ped->alive){objective_reason_="ped not alive or unreadable";return std::nullopt;}
        switch(watch.kind) {
        case ObjectiveWatchKind::position:return evaluate_walking(watch,false);
        case ObjectiveWatchKind::enter_car: {
            const auto ref=current_reference(watch.actor,watch.target);
            if(!ref || !(*ref)->cell || !sa_vehicle_reference_live((*ref)->snapshot,&(*ref)->cell))return std::nullopt;
            const auto target=sa_route_vehicle_lookup(watch.target.handle);
            if(target.status!=VehicleLookupStatus::present || !target.snapshot ||
               target.snapshot->entity!=watch.target.native_entity || target.snapshot->generation!=watch.target.generation)
                return std::nullopt;
            watch.completed=ped->in_vehicle && ped->current_vehicle_handle==watch.target.handle &&
                ped->current_vehicle_entity==watch.target.native_entity &&
                ped->current_vehicle_generation==watch.target.generation;
            return watch.completed;
        }
        case ObjectiveWatchKind::destroy_car: {
            const auto ref=current_reference(watch.actor,watch.target);
            if(!ref || !(*ref)->cell || !sa_vehicle_reference_live((*ref)->snapshot,&(*ref)->cell))return std::nullopt;
            const auto target=sa_route_vehicle_lookup(watch.target.handle);
            if(target.status!=VehicleLookupStatus::present || !target.snapshot ||
               target.snapshot->entity!=watch.target.native_entity || target.snapshot->generation!=watch.target.generation)
                return std::nullopt;
            if(!std::isfinite(target.snapshot->health))return std::nullopt;
            watch.completed=target.snapshot->health<=0.0f;return watch.completed;
        }
        case ObjectiveWatchKind::kill_char: {
            const auto& ref=watch.ped_reference;
            if(!ref || !(ref->target==watch.target_actor) )return std::nullopt;
            if(!ref->cell){release_ped_reference(watch.ped_reference);return std::nullopt;}
            if(!sa_actor_reference_live(ref->target,&ref->cell))return std::nullopt;
            const auto target=sa_route_actor_vehicle_snapshot(ref->target);
            if(!target || !(target->actor==ref->target))return std::nullopt;
            const auto source_state=original_cop_state(target->native_state);
            if(!source_state)return std::nullopt;
            watch.completed=is_dying(*source_state);
            if(watch.completed)release_ped_reference(watch.ped_reference);
            return watch.completed;
        }
        case ObjectiveWatchKind::goto_char: {
            const auto& ref=watch.ped_reference;
            if(!ref || !watch.walking_receipt || !(ref->target==watch.target_actor))return std::nullopt;
            if(!ref->cell){release_ped_reference(watch.ped_reference);return std::nullopt;}
            if(!sa_actor_reference_live(ref->target,&ref->cell))return std::nullopt;
            // LCS gates the source completion on m_nPedStateTimer (armed to now+600ms by SetSeek): the same delay counts
            // from the latest (re)issue of the SA task.
            return evaluate_walking(watch,true);
        }
        case ObjectiveWatchKind::unknown:return std::nullopt;
        case ObjectiveWatchKind::never:return false;
        }
        return std::nullopt;
    }
    void retire_entry(int32_t handle) {
        const auto found=entries_.find(handle);
        if(found==entries_.end())return;
        if(found->second.epoch)sa_release_owned_vehicle_entry(found->second.actor,found->second.epoch);
        entries_.erase(found);
    }
    // A command that entered SA but whose result the service refuses to adopt still owns a policy lineage
    // (weak actor reference + private vtable). Retire the policy only; SA keeps the already-assigned task.
    void release_unadopted_entry(ObjectiveActor actor,const NativeResult& effect) {
        if(!effect.accepted_script_task)return;
        const auto epoch=sa_owned_vehicle_entry_epoch(*effect.accepted_script_task,actor);
        if(epoch)sa_release_owned_vehicle_entry(actor,*epoch);
    }
    void remember_entry(ObjectiveActor actor,const OriginalRouteIntents::State& state,const NativeResult& effect) {
        retire_entry(actor.handle);
        uint64_t epoch=0;
        if(effect.accepted_script_task) {
            const auto owned=sa_owned_vehicle_entry_epoch(*effect.accepted_script_task,actor);
            if(owned)epoch=*owned;
        }
        entries_.insert_or_assign(actor.handle,EntryReceipt{state.revision,state.target,actor,epoch});
    }
    bool stop_entry(ObjectiveActor actor) {
        const auto found=entries_.find(actor.handle);
        if(found==entries_.end())return true; // no native entry was assigned
        if(!(found->second.actor==actor) || !found->second.epoch) {
            last_diagnostic_="Vehicle entry has no owned native task lifetime; restoration refused";
            return false;
        }
        const auto cancelled=sa_cancel_owned_vehicle_entry(actor,found->second.epoch);
        if(!cancelled || !*cancelled) {
            last_diagnostic_=cancelled?"Waiting for safe native vehicle-entry cancellation":
                "Owned vehicle-entry cancellation is unavailable";
            return false;
        }
        retire_entry(actor.handle);return true;
    }
    bool commit_source_plan(const OriginalRouteIntents::Plan& plan,ObjectiveActor actor,bool cleanup,
            std::unique_ptr<VehicleReference> target_ref={},std::unique_ptr<PedReference> ped_ref={}) {
        if(!jobs_.source_accepted(plan,cleanup)) {
            release_reference(target_ref);release_ped_reference(ped_ref);return false;
        }
        cleanup_[actor.handle]=cleanup;
        const auto state=intents_.state(actor);
        if(state)advance_objective_watch(actor,*state);
        if(state && (state->current==17 || state->current==18))
            vehicle_actors_.insert_or_assign(actor.handle,actor);
        else vehicle_actors_.erase(actor.handle);
        if(plan.reason()==OriginalRouteIntents::Cause::vehicle && state) {
            const auto facts=sa_route_actor_vehicle_snapshot(actor);
            if(facts && facts->in_vehicle) {
                if(!observe_current_vehicle(actor,*facts)) {
                    release_reference(target_ref);release_ped_reference(ped_ref);return false;
                }
            } else {
                // An accepted asynchronous vehicle task may change occupancy
                // before another callback. Do not retain a guessed empty/old car.
                forget_remembered_vehicle(actor.handle);
            }
        }
        if(plan.reason()==OriginalRouteIntents::Cause::running && state) {
            auto exit=exits_.find(actor.handle);
            if(exit!=exits_.end() && state->current==16 && exit->second.target==state->target)
                exit->second.revision=state->revision;
            else exits_.erase(actor.handle);
            auto entry=entries_.find(actor.handle);
            if(entry!=entries_.end() && (state->current==17 || state->current==18) &&
               entry->second.target==state->target)entry->second.revision=state->revision;
            else retire_entry(actor.handle);
        } else {
            exits_.erase(actor.handle);retire_entry(actor.handle);
        }
        if(state)synchronize_vehicle_reference(actor,*state,std::move(target_ref));
        else forget_vehicle_reference(actor.handle);
        if(state && (state->current==8 || state->current==9) && state->target.valid()) {
            if(ped_ref) {
                forget_ped_reference(actor.handle);
                ped_refs_[actor.handle]=std::move(ped_ref);
            }
        } else forget_ped_reference(actor.handle);
        // A superseded plan must not leave a native reference pointing to
        // the storage of a discarded, unadopted pending registration.
        release_reference(target_ref);release_ped_reference(ped_ref);
        return true;
    }
    // Script entries without a reconstructed source ledger keep the actual
    // private SA task as their authority, including while waiting out of range.
    struct ScriptEntry {ObjectiveActor actor;SourceEntityIdentity target;uint64_t epoch=0;};
    std::unordered_map<int32_t,ScriptEntry> script_entries_;
    static bool script_entry_command(uint16_t opcode) {
        return opcode==COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_DRIVER || opcode==COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_PASSENGER;
    }
    void retire_script_entry(int32_t handle) {
        const auto found=script_entries_.find(handle);if(found==script_entries_.end())return;
        sa_release_owned_vehicle_entry(found->second.actor,found->second.epoch);
        script_entries_.erase(found);
    }
    bool service_script_entries() {
        for(auto it=script_entries_.begin();it!=script_entries_.end();) {
            const auto entry=it->second;++it;
            const auto active=sa_owned_vehicle_entry_active(entry.actor,entry.epoch);
            if(active && !*active){retire_script_entry(entry.actor.handle);continue;}
            // Deliberately bypass actor_: that resolver only grants source-ledger
            // authority, while player/ambient entries need native identity only.
            const auto live=sa_live_objective_actor(entry.actor.handle);
            if(!live)continue;
            if(!(*live==entry.actor)){retire_script_entry(entry.actor.handle);continue;}
            const auto ped=sa_route_actor_vehicle_snapshot(entry.actor);
            if(!ped || !(ped->actor==entry.actor))continue;
            if(!ped->alive){retire_script_entry(entry.actor.handle);continue;}
            if(ped->in_vehicle) {
                (void)sa_set_owned_vehicle_entry_wait(entry.actor,entry.epoch,false);
                retire_script_entry(entry.actor.handle);continue;
            }
            const auto vehicle=sa_route_vehicle_lookup(entry.target.handle);
            if(vehicle.status==VehicleLookupStatus::unavailable)continue;
            bool ended=vehicle.status==VehicleLookupStatus::absent || !vehicle.snapshot;
            if(vehicle.snapshot) {
                const auto& car=*vehicle.snapshot;
                ended=ended || !(vehicle_identity(car)==entry.target) || !car.alive || !(car.health>0.0f);
                if(!ended && !car.is_bike && !car.upside_down.has_value())continue;
                ended=ended || (!car.is_bike && car.upside_down.value_or(false));
            }
            if(ended) {
                const auto cancelled=sa_cancel_owned_vehicle_entry(entry.actor,entry.epoch);
                if(cancelled && *cancelled)retire_script_entry(entry.actor.handle);
                continue;
            }
            const auto& car=*vehicle.snapshot;
            const float dx=car.position[0]-ped->position[0],dy=car.position[1]-ped->position[1],dz=car.position[2]-ped->position[2];
            const float distance=std::sqrt(dx*dx+dy*dy+dz*dz);
            if(!std::isfinite(distance))continue;
            const bool far=distance>30.0f*enter_car_range_multiplier_;
            bool waits=false;
            if(far) {
                if(ped->is_player) {
                    const auto disabled=sa_route_player_controls_disabled();if(!disabled)continue;waits=*disabled;
                } else {
                    // Explicit SA adaptation for actors without LCS provenance:
                    // use SA's actual MISSION_CHAR creation class, not a fake ledger.
                    if(ped->created_by<1 || ped->created_by>3)continue;
                    waits=ped->created_by==2;
                }
                if(!waits) {
                    const auto cancelled=sa_cancel_owned_vehicle_entry(entry.actor,entry.epoch);
                    if(cancelled && *cancelled)retire_script_entry(entry.actor.handle);
                    continue;
                }
            }
            // A lost/replaced bank is never permission to reissue the command.
            // Unknown reads defer; the same epoch must authorize every resume.
            (void)sa_set_owned_vehicle_entry_wait(entry.actor,entry.epoch,far && waits);
        }
        return true;
    }
    bool start_route_assignment(ObjectiveActor actor) {
        if(!jobs_.needs_assignment(actor))return true;
        const auto result=jobs_.assign_pending(actor);
        // Gate denial before factory entry is retried by tick_all. Once the
        // factory entered, a missing accepted epoch/binding is terminal.
        if(!result.native_entered)return true;
        if(!result.executed || !result.accepted_script_task || !result.diagnostic.empty()) {
            last_diagnostic_=result.diagnostic.empty()?
                "Route motion entered SA but no owned task epoch was accepted":result.diagnostic;
            untracked(actor.handle);return false;
        }
        return true;
    }
    bool restore_step(ObjectiveActor actor,const RouteVehicleGoalStep& step,bool cleanup) {
        auto plan=step.plan;
        if(plan.reason()==OriginalRouteIntents::Cause::restore) {
            // A same-frame off-screen warp can update m_leaveCarTimer without
            // changing source revision. RestorePreviousObjective itself leaves
            // that timer untouched, so rebuild this projection after the warp.
            const auto refreshed=intents_.restored(actor);
            if(refreshed.status()!=OriginalRouteIntents::Change::ready ||
               refreshed.proposed().current!=plan.proposed().current)return false;
            plan=refreshed;
        }
        if(plan.status()!=OriginalRouteIntents::Change::ready || !intents_.current(plan))return false;
        const auto destination=plan.proposed().current;
        if(destination==OriginalRouteIntents::follow_route) {
            if(!commit_source_plan(plan,actor,cleanup))return false;
            if(clear_walking_)clear_walking_(actor.handle);
            return start_route_assignment(actor);
        }
        if(destination==OriginalRouteIntents::none) {
            const auto facts=sa_route_actor_vehicle_snapshot(actor);
            if(!facts || !facts->alive || !(facts->actor==actor))return false;
            // Successful occupancy lets SA finish closing the door and driving.
            // On foot, retire only our private entry before publishing NONE;
            // ambient/default tasks and later native assignments stay untouched.
            const bool entered_target=facts->in_vehicle &&
                facts->current_vehicle_handle==plan.previous().target.handle &&
                facts->current_vehicle_entity==plan.previous().target.native_entity &&
                facts->current_vehicle_generation==plan.previous().target.generation;
            if(!entered_target && !stop_entry(actor))return false;
            return commit_source_plan(plan,actor,cleanup);
        }
        // RestorePreviousObjective changes source metadata. The regular
        // ProcessObjective consumer below creates a vehicle task if its gates pass.
        const auto target=step.plan.proposed().target;
        if((destination==16 || destination==17 || destination==18) && target.valid()) {
            const auto ref=current_reference(actor,target);if(!ref || (*ref)->cell!=target.native_entity)return false;
            const auto vehicle=sa_route_vehicle_snapshot(target.handle);
            if(!vehicle || !(vehicle_identity(*vehicle)==target) ||
               !sa_vehicle_reference_live(*vehicle,&(*ref)->cell))return false;
            return commit_source_plan(plan,actor,cleanup);
        }
        // Passive goals (WAIT_ON_FOOT, WAIT_ON_FOOT_FOR_COP, WAIT_IN_CAR): the source objective is only a wait, and
        // SA's idle/seated state after the finished entry task already is that wait. Recording the restored
        // objective is what the script's later checks (and the next SetObjective) read; no task is created.
        // Without this a paramedic that finished entering the ambulance was never released and the hospital
        // sequence stopped there.
        if(destination==1 || destination==2 || destination==6)return commit_source_plan(plan,actor,cleanup);
        last_diagnostic_="RestorePreviousObjective selected a goal without an implemented native consumer (objective "+
            std::to_string(int(destination))+")";
        return false;
    }
    bool exit_current_vehicle(ObjectiveActor actor,const RouteVehicleGoalStep& step,bool cleanup,
            uint64_t revision) {
        const auto previous=exits_.find(actor.handle);
        if(previous!=exits_.end() && previous->second.revision==revision && previous->second.target==step.target)return true;
        const auto live=actor_?actor_(actor.handle):std::nullopt;
        if(!live || !(*live==actor))return false;
        const auto ped=sa_route_actor_vehicle_snapshot(actor);
        if(!ped || !ped->alive || !ped->in_vehicle ||
           ped->current_vehicle_handle!=step.target.handle || ped->current_vehicle_entity!=step.target.native_entity ||
           ped->current_vehicle_generation!=step.target.generation)return false;
        const auto vehicle=sa_route_vehicle_snapshot(step.target.handle);
        if(!vehicle || !(vehicle_identity(*vehicle)==step.target))return false;
        NativeCall call;call.opcode=0x05CD;call.inputs={actor.handle,vehicle->handle};call.mission_context=cleanup;
        const auto result=vehicle_executor_?vehicle_executor_(call):NativeResult{};
        if(result.native_entered && !result.executed) {
            last_diagnostic_="Exit-current-vehicle task entered SA and failed; source goal was revoked";
            untracked(actor.handle);return false;
        }
        if(result.executed && result.native_entered) {
            exits_.insert_or_assign(actor.handle,ExitReceipt{revision,step.target});
            retire_entry(actor.handle);
        }
        return result.executed && result.native_entered;
    }
    bool assign_vehicle_entry(ObjectiveActor actor,const OriginalRouteIntents::State& state,
            const RouteVehicleNativeFacts& facts,bool cleanup) {
        if((state.current!=17 && state.current!=18) || !state.target.valid() || facts.actor.in_vehicle ||
           !facts.target || !ref_is_live(actor,state.target,*facts.target))return true;
        const auto source_state=source_state_?source_state_(actor):std::nullopt;
        if(!source_state || !is_source_control(*source_state))return true;
        const auto receipt=entries_.find(actor.handle);
        if(receipt!=entries_.end() && receipt->second.revision==state.revision &&
           receipt->second.target==state.target)return true;
        NativeCall call;call.mission_context=cleanup;
        if(state.current==17) {call.opcode=0x05CA;call.inputs={actor.handle,state.target.handle,-1,-1};}
        else {call.opcode=0x05CB;call.inputs={actor.handle,state.target.handle,-1};}
        VehicleEffectGate gate{this,actor,state.revision,state.target};
        if(!authorize_vehicle_effect(&gate))return false;
        const auto effect=vehicle_executor_?vehicle_executor_(call):NativeResult{};
        if(effect.native_entered && !effect.executed) {
            last_diagnostic_="Vehicle-entry task entered SA and failed; source goal was revoked";
            release_unadopted_entry(actor,effect);
            untracked(actor.handle);return false;
        }
        if(effect.executed && effect.native_entered)
            remember_entry(actor,state,effect);
        return true;
    }
    bool owns_suspended_route(ObjectiveActor actor)const {
        const auto actors=jobs_.active_actors();
        for(const auto& candidate:actors)if(candidate==actor)return true;
        return false;
    }
    static bool authorize_vehicle_effect(void* opaque) {
        const auto* gate=static_cast<const VehicleEffectGate*>(opaque);
        if(!gate || !gate->self || !gate->target.valid())return false;
        auto& self=*gate->self;
        const auto state=self.intents_.state(gate->actor);
        if(!state || state->revision!=gate->revision)return false;
        const bool restoring_route=gate->restoration_plan!=nullptr;
        if(restoring_route) {
            if(!self.intents_.current(*gate->restoration_plan) ||
               gate->restoration_plan->reason()!=OriginalRouteIntents::Cause::restore ||
               !(gate->restoration_plan->previous().actor==gate->actor) ||
               !self.owns_suspended_route(gate->actor) || !gate->transient_target_cell ||
               *gate->transient_target_cell!=gate->target.native_entity)return false;
        } else if((state->current!=17 && state->current!=18) ||
                  state->previous!=OriginalRouteIntents::follow_route ||
                  !(state->target==gate->target) || !self.owns_suspended_route(gate->actor))return false;
        const auto live=self.actor_?self.actor_(gate->actor.handle):std::nullopt;
        if(!live || !(*live==gate->actor))return false;
        const auto source=self.source_state_?self.source_state_(gate->actor):std::nullopt;
        if(!source || (restoring_route?*source!=gate->source_state:!is_source_control(*source)))return false;
        if(restoring_route) {
            const auto target=sa_route_vehicle_snapshot(gate->target.handle);
            return target && vehicle_identity(*target)==gate->target &&
                sa_vehicle_reference_live(*target,gate->transient_target_cell);
        }
        const auto ref=self.current_reference(gate->actor,gate->target);
        if(!ref || (*ref)->cell!=gate->target.native_entity)return false;
        const auto target=sa_route_vehicle_snapshot(gate->target.handle);
        return target && vehicle_identity(*target)==gate->target &&
            sa_vehicle_reference_live(*target,&(*ref)->cell);
    }
    bool idle_vehicle_goal(ObjectiveActor actor,const OriginalRouteIntents::State& state) {
        if(!stop_entry(actor))return false;
        VehicleEffectGate gate{this,actor,state.revision,state.target};
        const auto actor_facts=sa_route_actor_vehicle_snapshot(actor);
        if(!actor_facts || !(actor_facts->actor==actor))return false;
        const auto result=sa_route_idle_ped_still(*actor_facts,&authorize_vehicle_effect,&gate);
        if(result.native_entered && !result.executed) {
            last_diagnostic_=result.diagnostic.empty()?
                "Vehicle objective idle effect entered SA and failed":result.diagnostic;
            untracked(actor.handle);return false;
        }
        if(!result.executed || !result.native_entered) {
            last_diagnostic_=result.diagnostic.empty()?
                "Vehicle objective idle effect was not authorized":result.diagnostic;
            return false;
        }
        return true;
    }
    bool move_still_vehicle_goal(ObjectiveActor actor,const OriginalRouteIntents::State& state) {
        if(!stop_entry(actor))return false;
        VehicleEffectGate gate{this,actor,state.revision,state.target};
        const auto actor_facts=sa_route_actor_vehicle_snapshot(actor);
        if(!actor_facts || !(actor_facts->actor==actor))return false;
        const auto result=sa_route_move_still(*actor_facts,&authorize_vehicle_effect,&gate);
        if(result.native_entered && !result.executed) {
            last_diagnostic_=result.diagnostic.empty()?
                "Vehicle objective still effect entered SA and failed":result.diagnostic;
            untracked(actor.handle);return false;
        }
        if(!result.executed || !result.native_entered) {
            last_diagnostic_=result.diagnostic.empty()?
                "Vehicle objective still effect was not authorized":result.diagnostic;
            return false;
        }
        return true;
    }
    std::optional<RouteVehicleNativeFacts> vehicle_facts(ObjectiveActor actor,
            const OriginalRouteIntents::State& state,uint32_t now) {
        auto facts=source_actor_facts(actor);if(!facts || !facts->alive)return {};
        RouteVehicleNativeFacts result;result.now=now;result.actor=*facts;
        result.enter_car_range_multiplier=enter_car_range_multiplier_;
        if(state.target.valid()) {
            const auto ref=current_reference(actor,state.target);
            if(!ref)return {};
            if((*ref)->cell==0)result.target_reference_cleared=true;
            else {
                const auto target=sa_route_vehicle_snapshot(state.target.handle);
                if(!target || !(vehicle_identity(*target)==state.target) ||
                   !sa_vehicle_reference_live(*target,&(*ref)->cell))return {};
                result.target=*target;result.target_reference_cleared=false;
                if(state.current==18 && state.previous==OriginalRouteIntents::follow_route &&
                   target->driver_handle>=0 && target->driver_ped)
                    result.target_driver=source_actor_facts(
                        ObjectiveActor{target->driver_handle,target->driver_ped});
            }
        }
        if(facts->is_player)
            result.player_controls_disabled=sa_route_player_controls_disabled();
        if(facts->in_vehicle) {
            const auto current=sa_route_vehicle_snapshot(facts->current_vehicle_handle);
            if(!current || current->entity!=facts->current_vehicle_entity ||
               current->generation!=facts->current_vehicle_generation)return {};
            result.current_vehicle=*current;
            if(facts->is_player) {
                const auto accelerate=sa_scripted_pad_state(22);
                if(accelerate)result.player_accelerating=*accelerate!=0;
            }
            result.bus_jacked=facts->bus_jacked;
        }
        return result;
    }
    static bool bus_leave_deadline(const RouteActorVehicleSnapshot& actor,const RouteVehicleSnapshot& target,
            uint32_t now,uint32_t prior_deadline,uint32_t& result) {
        if(prior_deadline!=0) {result=prior_deadline;return true;}
        if(!target.is_bus)return false;
        if(!*target.is_bus) {result=prior_deadline;return true;}
        for(size_t seat=0;seat<target.passenger_peds.size();++seat) {
            if(target.passenger_peds[seat]!=actor.actor.native_ped)continue;
            const uint32_t delay=uint32_t(1200u*uint32_t(seat));
            result=now+delay;return true;
        }
        result=prior_deadline;return true;
    }
    bool recover_passenger_entry(ObjectiveActor actor,const OriginalRouteIntents::State& state,
            const RouteVehicleNativeFacts& facts,bool cleanup) {
        const auto source_state=original_cop_state(facts.actor.native_state);
        if(state.current!=17 || !state.objective_deadline || facts.now<=state.objective_deadline ||
           !source_state || (!is_source_control(*source_state) && *source_state!=50) ||
           !facts.target || facts.target_reference_cleared==true)return true;
        const auto& target=*facts.target;
        // The full-car branch precedes the fallback and restores immediately.
        if(!facts.actor.in_vehicle && target.num_passengers>=target.max_passengers)return true;
        if(!facts.actor.source_entering_car) {
            last_diagnostic_="Passenger recovery requires the actual source EnteringCar predicate";return false;
        }
        if(*facts.actor.source_entering_car)
            return intents_.record_objective_deadline(actor,state.revision,0);
        std::optional<RouteEntryDoor> door;
        int32_t seat=-1;
        constexpr uint8_t rf=4,lr=2,rr=8;
        const auto free_seat=[&](size_t index,uint8_t flag) {
            return index<target.passenger_peds.size() && !target.passenger_peds[index] &&
                !(target.getting_in_flags&flag);
        };
        if(target.is_bike) {
            if(free_seat(0,rr|lr)) {door=RouteEntryDoor::rear_right;seat=0;}
        } else if(free_seat(0,rf)) {door=RouteEntryDoor::front_right;seat=0;}
        else if(free_seat(1,lr)) {door=RouteEntryDoor::rear_left;seat=1;}
        else if(free_seat(2,rr)) {door=RouteEntryDoor::rear_right;seat=2;}
        // Preserve the retail loop, including its RF override starting at 2.
        if(target.max_passengers>target.passenger_peds.size()) {
            last_diagnostic_="Passenger recovery capacity exceeds the native passenger array";return false;
        }
        for(size_t i=2;i<target.max_passengers;++i)
            if(free_seat(i,rf)) {door=RouteEntryDoor::front_right;seat=0;}
        if(!door)return intents_.record_objective_deadline(actor,state.revision,0);
        if(!target.is_bus) {
            last_diagnostic_="Passenger recovery requires the actual bus model classification";return false;
        }
        const bool explicit_bus_door=*target.is_bus && *door!=RouteEntryDoor::front_right;
        if(*target.is_bus) {
            // Source PedSetInCarCB/AddPassenger uses the first empty bus seat.
            seat=-1;
            for(size_t i=0;i<target.max_passengers;++i)
                if(!target.passenger_peds[i]) {seat=int32_t(i);break;}
            if(seat<0)return false;
        }
        VehicleEffectGate gate{this,actor,state.revision,state.target};
        const auto positioned=sa_route_position_ped_for_car_entry(facts.actor,target,*door,
            &authorize_vehicle_effect,&gate);
        if(!positioned.executed) {
            last_diagnostic_=positioned.diagnostic.empty()?"Passenger recovery positioning was refused":positioned.diagnostic;
            if(positioned.native_entered)untracked(actor.handle);
            return false;
        }
        // Positioning has already mutated SA. Any failure from here is terminal
        // for this source epoch, preventing a replay of half the effect sequence.
        if(!positioned.native_entered || !authorize_vehicle_effect(&gate)) {
            last_diagnostic_="Passenger recovery lost source ownership after positioning";
            untracked(actor.handle);return false;
        }
        NativeCall call;call.opcode=0x05CA;call.inputs={actor.handle,target.handle,-1,seat};call.mission_context=cleanup;
        const auto entered=explicit_bus_door?
            sa_route_enter_passenger_door(facts.actor,target,*door,cleanup,&authorize_vehicle_effect,&gate):
            (vehicle_executor_?vehicle_executor_(call):NativeResult{});
        if(!entered.executed || !entered.native_entered || !authorize_vehicle_effect(&gate)) {
            last_diagnostic_=entered.diagnostic.empty()?"Passenger recovery could not complete its explicit-seat entry":entered.diagnostic;
            release_unadopted_entry(actor,entered);
            untracked(actor.handle);return false;
        }
        remember_entry(actor,state,entered);
        if(!intents_.record_objective_deadline(actor,state.revision,0)) {
            last_diagnostic_="Passenger recovery could not clear its completed source deadline";
            untracked(actor.handle);return false;
        }
        return true;
    }
    bool process_vehicle_goal(ObjectiveActor actor,const OriginalRouteIntents::State& state,
            uint32_t now) {
        auto facts=vehicle_facts(actor,state,now);
        if(!facts) {last_diagnostic_="Source vehicle objective facts or weak reference are unavailable";return false;}
        if(!observe_current_vehicle(actor,facts->actor))return false;
        const auto cleanup=cleanup_.find(actor.handle);
        const bool mission_cleanup=cleanup!=cleanup_.end() && cleanup->second;
        if(!recover_passenger_entry(actor,state,*facts,mission_cleanup))return false;
        // The source recomputes distance after recovery before its far branch.
        facts=vehicle_facts(actor,state,now);
        if(!facts) {last_diagnostic_="Passenger recovery lost live source facts";return false;}
        auto step=vehicle_goals_.tick(actor,*facts,intents_);
        if(step.warp_near_entity_offscreen) {
            if(!facts->target) {
                last_diagnostic_="Off-screen vehicle warp lost its source target snapshot";
                return false;
            }
            VehicleEffectGate gate{this,actor,state.revision,state.target};
            const auto warped=sa_route_warp_ped_near_entity_offscreen(facts->actor,*facts->target,
                now,state.leave_car_deadline,state.leave_car_deadline_known,
                &authorize_vehicle_effect,&gate);
            if(warped.native.native_entered && !warped.native.executed) {
                last_diagnostic_=warped.native.diagnostic.empty()?
                    "Off-screen vehicle warp entered SA and failed":warped.native.diagnostic;
                untracked(actor.handle);return false;
            }
            if(!warped.native.executed) {
                last_diagnostic_=warped.native.diagnostic.empty()?
                    "Off-screen vehicle warp was not authorized":warped.native.diagnostic;
                return false;
            }
            if(warped.leave_car_deadline &&
               !intents_.record_leave_car_deadline(actor,state.revision,*warped.leave_car_deadline)) {
                last_diagnostic_="Completed vehicle warp could not update the source leave-car timer";
                untracked(actor.handle);return false;
            }
        }
        if(step.action==RouteVehicleGoalAction::none)
            return assign_vehicle_entry(actor,state,*facts,mission_cleanup);
        if(step.action==RouteVehicleGoalAction::unavailable) {
            last_diagnostic_="Source vehicle objective requires a native branch that is not proved";
            return false;
        }
        if(step.action==RouteVehicleGoalAction::exit_current_vehicle)
            return exit_current_vehicle(actor,step,mission_cleanup,state.revision);
        if(step.action==RouteVehicleGoalAction::enter_as_passenger) {
            if(!facts->target)return false;
            const auto confirmed_facts=vehicle_facts(actor,state,now);
            if(!confirmed_facts)return false;
            const auto confirmed=vehicle_goals_.tick(actor,*confirmed_facts,intents_);
            if(confirmed.action!=RouteVehicleGoalAction::enter_as_passenger ||
               !(confirmed.driver==step.driver) || !(confirmed.target==step.target)) {
                last_diagnostic_="Source driver changed before passenger conversion";return false;
            }
            NativeCall passenger;passenger.opcode=0x05CA;
            passenger.inputs={actor.handle,state.target.handle,-1,-1};passenger.mission_context=mission_cleanup;
            bool projected=false;
            const auto accepted=execute_source_objective_impl(COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_PASSENGER,
                passenger,projected,[&]{return vehicle_executor_?vehicle_executor_(passenger):NativeResult{};});
            const auto updated=intents_.state(actor);
            if(!accepted.executed || !accepted.native_entered || !projected || !updated || updated->current!=17) {
                last_diagnostic_="Source driver-to-passenger conversion was not accepted";
                if(accepted.native_entered)untracked(actor.handle);
                return false;
            }
            VehicleEffectGate gate{this,actor,updated->revision,updated->target};
            if(!sa_route_clear_car_jacked(*facts->target,&authorize_vehicle_effect,&gate)) {
                last_diagnostic_="Passenger objective entered SA but car-jacked flag could not be cleared";
                untracked(actor.handle);return false;
            }
            return true;
        }
        if(step.action==RouteVehicleGoalAction::restore_previous)
            return restore_step(actor,step,mission_cleanup);
        if(step.action==RouteVehicleGoalAction::move_still)
            return move_still_vehicle_goal(actor,state);
        if(step.action==RouteVehicleGoalAction::idle_still)
            return idle_vehicle_goal(actor,state);
        return true;
    }
    static std::optional<float> kill_distance_squared(const RouteActorVehicleSnapshot& actor,
            const RouteActorVehicleSnapshot& target) {
        auto position=target.position;
        const auto source=original_cop_state(target.native_state);if(!source)return {};
        if(target.in_vehicle && *source!=57) {
            const auto car=sa_route_vehicle_snapshot(target.current_vehicle_handle);
            if(!car || car->entity!=target.current_vehicle_entity || car->generation!=target.current_vehicle_generation)return {};
            position=car->position;
        }
        const float dx=position[0]-actor.position[0],dy=position[1]-actor.position[1],dz=position[2]-actor.position[2];
        const float distance=dx*dx+dy*dy+dz*dz;
        return std::isfinite(distance)?std::optional<float>(distance):std::nullopt;
    }
    bool process_kill_remembered_entry(ObjectiveActor actor,const OriginalRouteIntents::State& state,
            const RouteActorVehicleSnapshot& facts,const RouteActorVehicleSnapshot& target,bool cleanup) {
        if(facts.kinda_stay_in_same_place!=std::optional<bool>(false) ||
           (target.is_player && facts.source_created_by!=std::optional<uint8_t>(2) && facts.native_type!=6)) {
            last_diagnostic_="Far ANY_MEANS entry lacks original stationary/player-police facts";return false;
        }
        const auto remembered=remembered_vehicles_.find(actor.handle);
        if(remembered==remembered_vehicles_.end() || !(remembered->second.actor==actor) ||
           !remembered->second.reference || !remembered->second.reference->cell) {
            last_diagnostic_="Far ANY_MEANS requires a proven remembered vehicle or original vehicle-search consumer";return false;
        }
        const auto expected=vehicle_identity(remembered->second.reference->snapshot);
        const auto car=sa_route_vehicle_snapshot(expected.handle);
        if(!car || !(vehicle_identity(*car)==expected) ||
           !sa_vehicle_reference_live(*car,&remembered->second.reference->cell))return false;
        struct Gate {SaRouteService* self;ObjectiveActor actor;uint64_t revision;SourceEntityIdentity car;};
        Gate gate{this,actor,state.revision,expected};
        const auto authorized=[](void* context) {
            const auto& gate=*static_cast<Gate*>(context);
            const auto state=gate.self->intents_.state(gate.actor);
            if(!state || state->revision!=gate.revision || state->current!=9 || state->previous!=30 ||
               !gate.self->owns_suspended_route(gate.actor))return false;
            const auto remembered=gate.self->remembered_vehicles_.find(gate.actor.handle);
            if(remembered==gate.self->remembered_vehicles_.end() || !(remembered->second.actor==gate.actor) ||
               !remembered->second.reference || !(vehicle_identity(remembered->second.reference->snapshot)==gate.car))return false;
            const auto car=sa_route_vehicle_snapshot(gate.car.handle);
            if(!car || !(vehicle_identity(*car)==gate.car) ||
               !sa_vehicle_reference_live(*car,&remembered->second.reference->cell))return false;
            const auto ped_ref=gate.self->ped_refs_.find(gate.actor.handle);
            if(ped_ref==gate.self->ped_refs_.end() || !ped_ref->second ||
               !(ped_identity(ped_ref->second->target)==state->target) ||
               !sa_actor_reference_live(ped_ref->second->target,&ped_ref->second->cell))return false;
            const auto facts=gate.self->source_actor_facts(gate.actor);
            const auto target=sa_route_actor_vehicle_snapshot(ped_ref->second->target);
            if(!facts || !target || !facts->alive || facts->in_vehicle ||
               facts->kinda_stay_in_same_place!=std::optional<bool>(false) ||
               (target->is_player && facts->source_created_by!=std::optional<uint8_t>(2) && facts->native_type!=6))return false;
            const auto source=original_cop_state(facts->native_state);
            const auto distance=kill_distance_squared(*facts,*target);
            return source && is_source_control(*source) && distance && *distance>900.f;
        };
        const auto stopped=sa_route_stop_vehicle_for_kill_entry(facts,*car,authorized,&gate);
        if(!stopped.executed || !stopped.native_entered || !authorized(&gate)) {
            if(stopped.native_entered)untracked(actor.handle);
            last_diagnostic_="Far ANY_MEANS cruise-stop refused or lost source authority; entered failures are terminal";
            return false;
        }
        NativeCall enter;enter.opcode=0x05CB;enter.inputs={actor.handle,car->handle,-1};enter.mission_context=cleanup;
        bool projected=false;
        const auto result=execute_source_objective_impl(COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_DRIVER,enter,projected,
            [&]{return vehicle_executor_?vehicle_executor_(enter):NativeResult{};});
        if(!result.executed || !projected) {
            untracked(actor.handle);
            last_diagnostic_="Far ANY_MEANS cruise-stop entered but driver objective was not accepted; sequence retired";return false;
        }
        return true;
    }
    bool process_kill_vehicle_exit(ObjectiveActor actor,const OriginalRouteIntents::State& state,
            const RouteActorVehicleSnapshot& facts,const RouteActorVehicleSnapshot& target,bool cleanup) {
        // Source ANY_MEANS evaluates this before its shared on-foot kill
        // case, including when the target is already dying. Preserve the
        // actual car stop and LEAVE transition instead of popping the route.
        const auto car=sa_route_vehicle_snapshot(facts.current_vehicle_handle);
        if(!car || car->entity!=facts.current_vehicle_entity ||
           car->generation!=facts.current_vehicle_generation) {
            last_diagnostic_="ANY_MEANS vehicle exit lacks the actor's live vehicle";return false;
        }
        const auto eligible=[](const RouteActorVehicleSnapshot& ped,const RouteActorVehicleSnapshot& victim,
                const RouteVehicleSnapshot& vehicle)->std::optional<bool> {
            if(!ped.in_vehicle || !vehicle.speed_magnitude_sq_3d ||
               !std::isfinite(*vehicle.speed_magnitude_sq_3d) ||
               *vehicle.speed_magnitude_sq_3d>=0.02f*0.02f)return {};
            if(victim.is_player && ped.source_created_by!=std::optional<uint8_t>(2) && ped.native_type!=6 &&
               ped.kinda_stay_in_same_place!=std::optional<bool>(true))return {};
            auto target_position=victim.position;
            std::optional<RouteVehicleSnapshot> target_car;
            if(victim.in_vehicle) {
                target_car=sa_route_vehicle_snapshot(victim.current_vehicle_handle);
                if(!target_car || target_car->entity!=victim.current_vehicle_entity ||
                   target_car->generation!=victim.current_vehicle_generation)return {};
                const auto target_state=original_cop_state(victim.native_state);
                if(!target_state)return {};
                // Source distance uses the ped position while being dragged,
                // even before bInVehicle clears; its exit test still uses car.
                if(*target_state!=57)target_position=target_car->position;
            }
            const float dx=target_position[0]-vehicle.position[0];
            const float dy=target_position[1]-vehicle.position[1];
            const float dz=target_position[2]-vehicle.position[2];
            const float distance=dx*dx+dy*dy+dz*dz;
            if(!std::isfinite(distance) || distance>=400.f)return {};
            return target_car?target_car->source_can_ped_exit_car:std::optional<bool>(true);
        };
        const auto ready=eligible(facts,target,*car);
        if(!ready) {
            last_diagnostic_="ANY_MEANS requires proven target-car exit facts or an unrepresented chase/player-police transition";
            return false;
        }
        if(!*ready)return true; // Actual source CanPedExitCar(false) refuses: wait.
        struct Gate {
            SaRouteService* self;ObjectiveActor actor;uint64_t revision;
            std::optional<bool>(*eligible)(const RouteActorVehicleSnapshot&,const RouteActorVehicleSnapshot&,const RouteVehicleSnapshot&);
        } gate{this,actor,state.revision,eligible};
        const auto authorized=[](void* context) {
            const auto& gate=*static_cast<Gate*>(context);
            const auto intent=gate.self->intents_.state(gate.actor);
            if(!intent || intent->revision!=gate.revision || intent->current!=9 || intent->previous!=30 ||
               !gate.self->owns_suspended_route(gate.actor))return false;
            const auto found=gate.self->ped_refs_.find(gate.actor.handle);
            if(found==gate.self->ped_refs_.end() || !found->second || !found->second->cell ||
               !(ped_identity(found->second->target)==intent->target) ||
               !sa_actor_reference_live(found->second->target,&found->second->cell))return false;
            const auto ped=gate.self->source_actor_facts(gate.actor);
            const auto victim=sa_route_actor_vehicle_snapshot(found->second->target);
            if(!ped || !victim || !ped->alive)return false;
            const auto source=original_cop_state(ped->native_state);
            if(!source || (!is_source_control(*source) && *source!=50))return false;
            const auto car=sa_route_vehicle_snapshot(ped->current_vehicle_handle);
            return car && car->entity==ped->current_vehicle_entity &&
                car->generation==ped->current_vehicle_generation &&
                gate.eligible(*ped,*victim,*car)==std::optional<bool>(true);
        };
        const auto stopped=sa_route_stop_vehicle_for_kill_exit(facts,*car,authorized,&gate);
        if(!stopped.executed || !stopped.native_entered) {
            last_diagnostic_=stopped.diagnostic.empty()?"ANY_MEANS could not stop its actual vehicle":stopped.diagnostic;
            if(stopped.native_entered)untracked(actor.handle);
            return false;
        }
        if(!authorized(&gate)) {
            untracked(actor.handle);
            last_diagnostic_="ANY_MEANS vehicle-exit authority changed after car stop; sequence retired";
            return false;
        }
        NativeCall leave;leave.opcode=0x05CD;
        leave.inputs={actor.handle,car->handle};leave.mission_context=cleanup;
        bool projected=false;
        const auto result=execute_source_objective_impl(COMMAND_SET_CHAR_OBJ_LEAVE_CAR,leave,projected,
            [&]{return vehicle_executor_?vehicle_executor_(leave):NativeResult{};});
        if(!result.executed || !projected) {
            // The car-stop prefix already entered. Never retry a partially
            // accepted compound source effect against a changed native task.
            untracked(actor.handle);
            last_diagnostic_="ANY_MEANS stopped its car but LEAVE was not accepted; source authority retired";
            return false;
        }
        return true;
    }
    bool process_target_goal(ObjectiveActor actor,const OriginalRouteIntents::State& state) {
        if((state.current!=8 && state.current!=9 && state.current!=22) || state.previous!=30)return true;
        const auto facts=source_actor_facts(actor);
        if(!facts || !(facts->actor==actor))return false;
        if(!observe_current_vehicle(actor,*facts))return false;
        const auto source_state=original_cop_state(facts->native_state);
        if(!source_state)return false;
        if(!is_source_control(*source_state) && *source_state!=50)return true;
        const auto cleanup=cleanup_.find(actor.handle);
        const bool mission_cleanup=cleanup!=cleanup_.end() && cleanup->second;
        if(state.current==8 && facts->in_vehicle) {
            NativeCall leave;leave.opcode=0x05CD;
            leave.inputs={actor.handle,facts->current_vehicle_handle};leave.mission_context=mission_cleanup;
            bool projected=false;
            const auto result=execute_source_objective_impl(COMMAND_SET_CHAR_OBJ_LEAVE_CAR,leave,projected,
                [&]{return vehicle_executor_?vehicle_executor_(leave):NativeResult{};});
            return result.executed && projected;
        }
        bool completed=false;
        if(state.current==22) {
            const auto ref=current_reference(actor,state.target);
            if(!ref) {last_diagnostic_="Destroy objective target reference is unavailable";return false;}
            // Destroyed health is only the SCM latch. Only actual native
            // ResolveReferences clearing this cell proves source completion.
            completed=(*ref)->cell==0;
        } else {
            const auto found=ped_refs_.find(actor.handle);
            if(found==ped_refs_.end() || !found->second ||
               !(ped_identity(found->second->target)==state.target)) {
                last_diagnostic_="Kill objective lacks its registered original ped target";return false;
            }
            const auto& ref=*found->second;
            completed=ref.cell==0;
            if(!completed) {
                if(!sa_actor_reference_live(ref.target,&ref.cell)) {
                    last_diagnostic_="Kill target identity changed without source reference clearing";return false;
                }
                const auto target=sa_route_actor_vehicle_snapshot(ref.target);
                if(!target) {last_diagnostic_="Kill target source state is unavailable";return false;}
                const auto target_state=original_cop_state(target->native_state);
                if(!target_state)return false;
                if(state.current==9 && facts->in_vehicle)
                    return process_kill_vehicle_exit(actor,state,*facts,*target,mission_cleanup);
                if(state.current==9 && facts->kinda_stay_in_same_place!=std::optional<bool>(true)) {
                    const auto distance=kill_distance_squared(*facts,*target);
                    if(!distance)return false;
                    if(*distance>900.f)return process_kill_remembered_entry(actor,state,*facts,*target,mission_cleanup);
                }
                completed=is_dying(*target_state); // DyingOrDead, never health or task loss.
                if(completed && state.current==9) {
                    auto target_position=target->position;
                    if(target->in_vehicle) {
                        const auto car=sa_route_vehicle_snapshot(target->current_vehicle_handle);
                        if(!car || car->entity!=target->current_vehicle_entity ||
                           car->generation!=target->current_vehicle_generation)return false;
                        target_position=car->position;
                    }
                    const float dx=target_position[0]-facts->position[0];
                    const float dy=target_position[1]-facts->position[1];
                    const float dz=target_position[2]-facts->position[2];
                    const float distance=dx*dx+dy*dy+dz*dz;
                    // The original stationary flag short-circuits both
                    // player-police flight and the >30m vehicle search.
                    // It does not bypass the earlier in-vehicle branch.
                    const bool stationary=facts->kinda_stay_in_same_place==std::optional<bool>(true);
                    if(facts->in_vehicle || !std::isfinite(distance) ||
                       (!stationary && (distance>900.0f ||
                        (target->is_player && facts->source_created_by!=std::optional<uint8_t>(2) && facts->native_type!=6)))) {
                        last_diagnostic_="ANY_MEANS death requires an unrepresented vehicle, range, or player-police transition";
                        return false;
                    }
                }
            }
        }
        if(!completed)return true;
        if(!facts->source_created_by) {
            last_diagnostic_="Source completion lacks the actor's original constructor provenance";return false;
        }
        if(*facts->source_created_by==1 && !facts->in_vehicle) {
            last_diagnostic_="Random source objective completion requires the original wander transition";return false;
        }
        RouteVehicleGoalStep restore;restore.action=RouteVehicleGoalAction::restore_previous;
        restore.plan=intents_.restored(actor);
        return restore_step(actor,restore,mission_cleanup);
    }
    bool process_position_goal(ObjectiveActor actor,const OriginalRouteIntents::State& state) {
        if((state.current!=24 && state.current!=25 && state.current!=49) || state.previous!=30)return true;
        const auto facts=source_actor_facts(actor);
        if(!facts || !(facts->actor==actor) || !state.target_position) {
            last_diagnostic_="Source position objective lacks its accepted destination or live actor";return false;
        }
        const auto source_state=original_cop_state(facts->native_state);
        if(!source_state)return false;
        if(!is_source_control(*source_state) && *source_state!=50)return true;
        const auto cleanup=cleanup_.find(actor.handle);
        const bool mission_cleanup=cleanup!=cleanup_.end() && cleanup->second;
        if(facts->in_vehicle) {
            NativeCall leave;leave.opcode=0x05CD;
            leave.inputs={actor.handle,facts->current_vehicle_handle};leave.mission_context=mission_cleanup;
            bool projected=false;
            const auto result=execute_source_objective_impl(COMMAND_SET_CHAR_OBJ_LEAVE_CAR,leave,projected,
                [&]{return vehicle_executor_?vehicle_executor_(leave):NativeResult{};});
            return result.executed && projected;
        }
        const auto reached=[](const RouteActorVehicleSnapshot& current,const std::array<float,3>& destination) {
            const float dx=current.position[0]-destination[0],dy=current.position[1]-destination[1];
            const float distance=dx*dx+dy*dy;
            return std::isfinite(distance) && distance<=0.25f;
        };
        if(!reached(*facts,*state.target_position))return true;
        if(!facts->source_created_by) {
            last_diagnostic_="Source coordinate completion lacks original constructor provenance";return false;
        }
        if(*facts->source_created_by==1) {
            last_diagnostic_="Random source coordinate completion requires the original wander transition";return false;
        }
        struct Gate {SaRouteService* self;ObjectiveActor actor;uint64_t revision;};
        Gate gate{this,actor,state.revision};
        const auto authorized=[](void* context) {
            const auto& gate=*static_cast<Gate*>(context);
            const auto intent=gate.self->intents_.state(gate.actor);
            if(!intent || intent->revision!=gate.revision || intent->previous!=30 ||
               (intent->current!=24 && intent->current!=25 && intent->current!=49) ||
               !intent->target_position || !gate.self->owns_suspended_route(gate.actor))return false;
            const auto live=sa_route_actor_vehicle_snapshot(gate.actor);
            if(!live || !(live->actor==gate.actor) || live->in_vehicle || !live->alive)return false;
            const auto source=original_cop_state(live->native_state);
            if(!source || !is_source_control(*source))return false;
            const float dx=live->position[0]-(*intent->target_position)[0];
            const float dy=live->position[1]-(*intent->target_position)[1];
            const float distance=dx*dx+dy*dy;
            return std::isfinite(distance) && distance<=0.25f;
        };
        const auto stopped=sa_route_move_still(*facts,authorized,&gate);
        if(!stopped.executed || !stopped.native_entered) {
            last_diagnostic_=stopped.diagnostic.empty()?"Source arrival could not apply MoveState STILL":stopped.diagnostic;
            if(stopped.native_entered)untracked(actor.handle);
            return false;
        }
        RouteVehicleGoalStep restore;restore.action=RouteVehicleGoalAction::restore_previous;
        restore.plan=intents_.restored(actor);
        return restore_step(actor,restore,mission_cleanup);
    }
    static NativeResult failed(std::string message) {
        NativeResult result;result.diagnostic=std::move(message);return result;
    }
    NativeResult execute_and_untrack(int32_t handle,const std::function<NativeResult()>& execute_once) {
        const auto result=execute_once?execute_once():NativeResult{};
        if(result.native_entered)untracked(handle);
        return result;
    }

public:
    SaRouteService(OriginalRouteIntents& intents,const OriginalRouteTable& table,OwnedRouteReaders readers,
            ActorResolver actor,SourceStateResolver source_state,ClearWalking clear_walking,
            NativeExecutor vehicle_executor)
        :intents_(intents),table_(table),host_(std::move(readers)),
         jobs_(host_,intents,table,RouteNativeFunctions{sa_route_actor_snapshot,sa_active_walking_task,
             sa_create_route_motion,sa_bind_owned_route_control,sa_unbind_owned_route_control,
             sa_retarget_owned_route_task,true}),actor_(std::move(actor)),source_state_(std::move(source_state)),
         clear_walking_(std::move(clear_walking)),vehicle_executor_(std::move(vehicle_executor)) {}

    ~SaRouteService(){clear();}
    const std::string& diagnostic()const{return last_diagnostic_;}
    void set_walking_receipts(std::function<std::optional<EpochScriptTask>(const CapturedScriptTask&)> accept,
            std::function<std::optional<EpochScriptTask>(const EpochScriptTask&)> refresh,
            std::function<std::optional<ScriptTaskRelease>(const EpochScriptTask&)> released,NativeExecutor reissue) {
        walking_accept_=std::move(accept);walking_refresh_=std::move(refresh);
        walking_released_=std::move(released);walking_reissue_=std::move(reissue);
    }
    // Handed to the task observer: runs inside the native release notification and reads only the probe registry.
    std::function<std::optional<int>(const EpochScriptTask&)> walking_release_probe() {
        return [this](const EpochScriptTask& token){return release_probe(token);};
    }
    bool set_enter_car_range_multiplier(uint16_t value) noexcept {
        enter_car_range_multiplier_=static_cast<float>(value);return true;
    }

    bool constructed(ObjectiveActor actor,std::optional<uint8_t> source_created_by={}) {
        for(const auto& old:jobs_.active_actors())
            if(old.handle==actor.handle || old.native_ped==actor.native_ped)retired(old);
        for(auto it=objective_watches_.begin();it!=objective_watches_.end();) {
            if(it->first==actor.handle || it->second.actor.native_ped==actor.native_ped) {
                const auto handle=it->first;++it;retire_objective_watch(handle);
            } else ++it;
        }
        for(auto it=remembered_vehicles_.begin();it!=remembered_vehicles_.end();) {
            if(it->first==actor.handle || it->second.actor.native_ped==actor.native_ped) {
                release_reference(it->second.reference);it=remembered_vehicles_.erase(it);
            } else ++it;
        }
        if(!intents_.constructed(actor,source_created_by))return false;
        remembered_vehicles_.emplace(actor.handle,RememberedVehicle{actor,{}});return true;
    }
    // A live actor returned by an original character-selection command can
    // receive a source objective without an original CREATE_CHAR callback.
    // The caller supplies provenance established by that selector's accepted
    // native promotion; this records shadow state only and does not construct
    // or retask the ped. Until its setter arrives, completion stays unknown.
    // Repeated access must preserve the same generation, source intent and latch.
    bool acquired(ObjectiveActor actor,std::optional<uint8_t> source_created_by={}) {
        const auto live=sa_live_objective_actor(actor.handle);
        if(!live || !(*live==actor))return false;
        if(intents_.state(actor))return true;
        if(!constructed(actor,source_created_by))return false;
        unknown_objective_watch(actor);
        return true;
    }
    void retired(ObjectiveActor actor) {
        retire_script_entry(actor.handle);
        jobs_.retired(actor);vehicle_actors_.erase(actor.handle);cleanup_.erase(actor.handle);exits_.erase(actor.handle);retire_entry(actor.handle);
        retire_objective_watch(actor.handle);
        forget_vehicle_reference(actor.handle);
        forget_ped_reference(actor.handle);
        forget_remembered_vehicle(actor.handle);
    }
    void untracked(int32_t handle) {
        retire_script_entry(handle);
        jobs_.untracked_objective(handle);vehicle_actors_.erase(handle);cleanup_.erase(handle);exits_.erase(handle);retire_entry(handle);
        const auto live=sa_live_objective_actor(handle);
        if(live)unknown_objective_watch(*live,true);else retire_objective_watch(handle);
        forget_vehicle_reference(handle);
        forget_ped_reference(handle);
        forget_remembered_vehicle(handle);
    }
    bool running(ObjectiveActor actor,bool enabled) {
        const auto plan=intents_.running(actor,enabled);
        if(plan.status()==OriginalRouteIntents::Change::ignored)return true;
        const auto cleanup=cleanup_.find(actor.handle);
        return commit_source_plan(plan,actor,cleanup!=cleanup_.end() && cleanup->second);
    }

    StepResult follow_route(int32_t handle,int32_t route,int32_t mode,bool mission_cleanup) {
        ObjectiveActor actor;std::optional<int32_t> state;std::optional<RouteActorVehicleSnapshot> facts;
        if(!known_actor(handle,actor,state,facts))return StepResult::unsupported;
        // Script2 clears the completion latch before calling SetObjective,
        // even when that source setter returns through an ignored guard.
        if(const auto current=intents_.state(actor)) {
            auto found=objective_watches_.find(actor.handle);
            if(found!=objective_watches_.end() && found->second.actor==actor) {
                found->second.completed=false;found->second.reset_pending=true;found->second.source_revision=current->revision;
                found->second.source_current=current->current;
            } else watch_source_state(actor,*current);
        }
        const auto plan=intents_.follow(actor,route,mode,*state,table_);
        if(plan.status()==OriginalRouteIntents::Change::ignored)return StepResult::continued;
        const bool source_seek=plan.status()==OriginalRouteIntents::Change::ready && *state==6;
        // Reuse is an optimization only when the active seek is already our
        // owned route task and its live XY target matches the new route point.
        // A foreign SA SEEK_POS target is never read or retargeted: the accepted
        // FOLLOW_ROUTE below installs a fresh task through SA's real factory.
        const bool retain_seek=source_seek && jobs_.reusable_route_seek(plan).has_value();
        if(plan.status()==OriginalRouteIntents::Change::ready && !observe_current_vehicle(actor,*facts))
            return StepResult::unsupported;
        if(plan.status()!=OriginalRouteIntents::Change::ready)return StepResult::unsupported;
        struct FollowGate {
            SaRouteService* self;
            const OriginalRouteIntents::Plan* plan;
            ObjectiveActor actor;
        } gate{this,&plan,actor};
        const auto authorized=[](void* context) {
            const auto& gate=*static_cast<FollowGate*>(context);
            const auto live=gate.self->actor_(gate.actor.handle);
            return live && *live==gate.actor && gate.self->intents_.current(*gate.plan);
        };
        // The original setter clears aiming before resetting its timer and
        // publishing FOLLOW_ROUTE. Duplicate/dying guards above have no effect.
        const auto cleared=sa_route_clear_point_gun_at(*facts,authorized,&gate);
        if(!cleared.executed || !cleared.diagnostic.empty()) {
            last_diagnostic_=cleared.diagnostic.empty()?"FOLLOW_ROUTE could not clear point-gun state":cleared.diagnostic;
            if(cleared.native_entered) {untracked(actor.handle);return StepResult::failed;}
            return StepResult::unsupported;
        }
        if(source_seek) {
            const auto after_clear=source_state_?source_state_(actor):std::nullopt;
            if(!after_clear || *after_clear!=6) {
                last_diagnostic_="FOLLOW_ROUTE lost SEEK_POS during clear effects; source state changed before native route assignment";
                untracked(actor.handle);return StepResult::failed;
            }
        }
        if(!jobs_.source_accepted(plan,mission_cleanup,retain_seek)) {
            untracked(actor.handle);return StepResult::failed;
        }
        if(const auto accepted=intents_.state(actor))watch_source_state(actor,*accepted);
        cleanup_[actor.handle]=mission_cleanup;
        if(clear_walking_)clear_walking_(actor.handle);
        if(!start_route_assignment(actor))return StepResult::failed;
        return StepResult::continued;
    }

    NativeResult execute_source_objective_impl(uint16_t original,const NativeCall& call,bool& projected,
            const std::function<NativeResult()>& execute_once) {
        projected=false;
        if(!execute_once || call.inputs.empty())return failed("Source route objective packet has no actor/executor");
        const auto apply_without_projection=[&](){
            if(!script_entry_command(original))return execute_and_untrack(call.inputs[0],execute_once);
            if(call.inputs.size()<2)return failed("Native script entry packet is truncated");
            const auto actor=sa_live_objective_actor(call.inputs[0]);
            const auto vehicle=sa_route_vehicle_lookup(call.inputs[1]);
            if(!actor || !vehicle.snapshot || vehicle.status!=VehicleLookupStatus::present)
                return failed("Native script entry identity unavailable before assignment");
            const auto target=vehicle_identity(*vehicle.snapshot);
            auto effect=execute_and_untrack(call.inputs[0],execute_once);
            if(effect.executed && effect.native_entered) {
                const auto epoch=effect.accepted_script_task?sa_owned_vehicle_entry_epoch(*effect.accepted_script_task,*actor):std::nullopt;
                if(!epoch) {
                    // SA accepted the entry (or found it already running), but the script-task bank cannot
                    // single out the task this call created (for example when several entries are assigned
                    // in the same frame). The entry is real and is never replayed; only its cleanup record is
                    // absent, and the script itself polls IS_CHAR_IN_CAR for the result. Suspending the
                    // script for good over a missing record (ambient or mission alike) would only turn a
                    // working objective into a softlock.
                    last_diagnostic_=call.mission_context?
                        "Mission script entry accepted without private ownership; continued untracked, never replayed":
                        "Ambient script entry accepted without private ownership; continued untracked, never replayed";
                    return effect;
                }
                script_entries_.insert_or_assign(actor->handle,ScriptEntry{*actor,target,*epoch});
            }
            return effect;
        };
        const auto spec=source_objective(original);
        if(!spec)return apply_without_projection();
        ObjectiveActor actor;std::optional<int32_t> source_state;
        std::optional<RouteActorVehicleSnapshot> actor_facts;
        if(!known_actor(call.inputs[0],actor,source_state,actor_facts))return apply_without_projection();
        auto state=intents_.state(actor);if(!state)return apply_without_projection();
        if(!observe_current_vehicle(actor,*actor_facts))return failed("Original occupied vehicle identity is unavailable");

        OriginalRouteIntents::OrdinaryAdmission ordinary;
        ordinary.source_state=*source_state;ordinary.setter=spec->setter;
        ordinary.attached=actor_facts->attached;ordinary.dying=is_dying(*source_state);
        ordinary.is_player=actor_facts->is_player;ordinary.in_vehicle=actor_facts->in_vehicle;
        ordinary.ped_in_control=is_source_control(*source_state);
        if(spec->setter==OriginalRouteIntents::Setter::position) {
            if(call.inputs.size()<4)return failed("Original position objective packet is truncated");
            ordinary.target_position=source_vector(call.inputs.data()+1);
            ordinary.actor_position=actor_facts->position;
            if(!ordinary.target_position)return failed("Original position objective is nonfinite");
        }
        if(spec->target==ObjectiveSpec::Target::ped) {
            if(call.inputs.size()<2)return failed("Original entity objective packet is truncated");
            const auto target=sa_live_objective_actor(call.inputs[1]);if(!target)return apply_without_projection();
            ordinary.target=ped_identity(*target);ordinary.self_target=*target==actor;
        } else if(spec->target==ObjectiveSpec::Target::vehicle && !spec->vehicle) {
            if(call.inputs.size()<2)return failed("Original vehicle objective packet is truncated");
            const auto target=sa_route_vehicle_snapshot(call.inputs[1]);if(!target)return apply_without_projection();
            ordinary.target=vehicle_identity(*target);
        }
        auto frame=sa_frame_clock();
        const uint32_t now=frame?frame->now:0;
        ordinary.objective_deadline=0;
        ordinary.leave_car_deadline=state->leave_car_deadline_known?
            std::optional<uint32_t>(state->leave_car_deadline):std::nullopt;
        ordinary.leave_car_deadline_unknown=!state->leave_car_deadline_known;

        if(spec->vehicle) {
            if(!frame)return apply_without_projection();
            std::optional<RouteVehicleSnapshot> target;
            OriginalRouteIntents::VehicleAdmission admission;
            admission.source_state=*source_state;admission.setter=spec->setter;
            admission.in_vehicle=actor_facts->in_vehicle;admission.attached=actor_facts->attached;
            admission.dying=is_dying(*source_state);admission.is_player=actor_facts->is_player;
            if(spec->target==ObjectiveSpec::Target::vehicle) {
                const auto target_handle=spec->setter==OriginalRouteIntents::Setter::plain?
                    (actor_facts->in_vehicle?actor_facts->current_vehicle_handle:-1):
                    (call.inputs.size()>1?call.inputs[1]:-1);
                if(target_handle>=0) {
                    target=sa_route_vehicle_snapshot(target_handle);
                    if(!target)return apply_without_projection();
                    admission.target=vehicle_identity(*target);
                    admission.target_boat=target->is_boat;admission.target_bus=target->is_bus;
                    admission.on_target_boat=actor_facts->standing_on_entity==target->entity;
                    const auto current_target=current_reference(actor,state->target);
                    admission.same_target=current_target && (*current_target)->cell==state->target.native_entity &&
                        vehicle_identity((*current_target)->snapshot)==admission.target &&
                        sa_vehicle_reference_live((*current_target)->snapshot,&(*current_target)->cell);
                    admission.target_disabled=target->player_disabled;
                    admission.mission_character=actor_facts->mission_character;
                    admission.player_controls_disabled=sa_route_player_controls_disabled();
                    if(state->leave_car_deadline_known) {
                        uint32_t deadline=state->leave_car_deadline;
                        if(spec->goal==16 && !bus_leave_deadline(*actor_facts,*target,now,deadline,deadline))
                            admission.leave_car_deadline_unknown=true;
                        else {admission.leave_car_deadline=deadline;}
                    } else admission.leave_car_deadline_unknown=true;
                } else if(spec->setter==OriginalRouteIntents::Setter::entity) {
                    return apply_without_projection();
                }
            }
            admission.now=now;
            const auto plan=vehicle_goals_.accepted(intents_,actor,spec->goal,admission);
            if(plan.status()==OriginalRouteIntents::Change::ignored) {
                projected=true;return accepted_without_entry();
            }
            if(plan.status()!=OriginalRouteIntents::Change::ready)
                return apply_without_projection();

            std::unique_ptr<VehicleReference> accepted_target;
            if(plan.reason()==OriginalRouteIntents::Cause::restore) {
                const bool restores_active_route=owns_suspended_route(actor) &&
                    (plan.previous().current==OriginalRouteIntents::follow_route ||
                     plan.previous().previous==OriginalRouteIntents::follow_route);
                const bool source_boat_admission=!actor_facts->is_player && target &&
                    admission.target_boat==true && admission.on_target_boat==false;
                if(restores_active_route && source_boat_admission) {
                    auto transient_target=prepare_vehicle_reference(*target);
                    if(!transient_target) {
                        last_diagnostic_="Boat restore target could not be held by a native weak reference";
                        return failed(last_diagnostic_);
                    }
                    VehicleEffectGate gate{this,actor,plan.previous().revision,vehicle_identity(*target)};
                    gate.restoration_plan=&plan;gate.transient_target_cell=&transient_target->cell;
                    gate.source_state=*source_state;
                    const auto live_actor=sa_route_actor_vehicle_snapshot(actor);
                    if(!live_actor || !(live_actor->actor==actor)) {
                        release_reference(transient_target);
                        return failed("Boat restore actor identity changed before source movement promotion");
                    }
                    const auto movement=sa_route_promote_still_to_run(*live_actor,
                        &authorize_vehicle_effect,&gate);
                    if(movement.native_entered && !movement.executed) {
                        last_diagnostic_=movement.diagnostic.empty()?
                            "Boat restore movement promotion entered SA and failed":movement.diagnostic;
                        release_reference(transient_target);untracked(actor.handle);
                        auto terminal=failed(last_diagnostic_);terminal.native_entered=true;return terminal;
                    }
                    if(!movement.executed) {
                        last_diagnostic_=movement.diagnostic.empty()?
                            "Boat restore movement promotion was not authorized":movement.diagnostic;
                        release_reference(transient_target);return failed(last_diagnostic_);
                    }
                    if(!commit_source_plan(plan,actor,call.mission_context)) {
                        release_reference(transient_target);untracked(actor.handle);
                        auto terminal=failed("Boat restore source projection could not be committed");
                        terminal.native_entered=movement.native_entered;return terminal;
                    }
                    release_reference(transient_target);
                    projected=true;return accepted_without_entry();
                }
                return apply_without_projection();
            }
            if(target && !(admission.same_target && current_reference(actor,state->target))) {
                accepted_target=prepare_vehicle_reference(*target);
                if(!accepted_target)return apply_without_projection();
            }
            const bool route_interruption=plan.previous().current==OriginalRouteIntents::follow_route;
            const bool defer_native_task=route_interruption && spec->goal==16;
            if(defer_native_task) {
                if(!commit_source_plan(plan,actor,call.mission_context,std::move(accepted_target))) {
                    untracked(actor.handle);
                    auto terminal=failed("Accepted source vehicle goal could not be committed");
                    return terminal;
                }
                if(clear_walking_)clear_walking_(actor.handle);
                projected=true;return accepted_without_entry();
            }
            const auto effect=execute_once();
            if(effect.executed && effect.native_entered) {
                if(!commit_source_plan(plan,actor,call.mission_context,std::move(accepted_target))) {
                    release_unadopted_entry(actor,effect);
                    projected=false;untracked(actor.handle);
                    auto terminal=failed("Accepted source vehicle goal could not be committed");
                    terminal.native_entered=true;return terminal;
                }
                projected=true;
                if(spec->goal==17 || spec->goal==18) {
                    const auto committed=intents_.state(actor);
                    if(committed)remember_entry(actor,*committed,effect);
                }
            } else if(accepted_target)release_reference(accepted_target);
            if(effect.native_entered && !effect.executed){release_unadopted_entry(actor,effect);untracked(actor.handle);}
            return effect;
        }

        const auto plan=intents_.ordinary(actor,spec->goal,ordinary);
        if(plan.status()==OriginalRouteIntents::Change::ignored) {
            projected=true;return accepted_without_entry();
        }
        if(plan.status()!=OriginalRouteIntents::Change::ready)
            return apply_without_projection();
        if(plan.reason()==OriginalRouteIntents::Cause::running) {
            if(!commit_source_plan(plan,actor,call.mission_context) || !start_route_assignment(actor)) {
                auto terminal=failed("Position objective movement update could not preserve the active source route");
                terminal.native_entered=true;return terminal;
            }
            projected=true;return accepted_without_entry();
        }
        std::unique_ptr<PedReference> accepted_ped;
        std::unique_ptr<VehicleReference> accepted_vehicle;
        if(plan.proposed().previous==30 && (plan.proposed().current==8 || plan.proposed().current==9)) {
            accepted_ped=std::make_unique<PedReference>();
            accepted_ped->target={ordinary.target.handle,ordinary.target.native_entity};
            if(!sa_register_actor_reference(accepted_ped->target,&accepted_ped->cell)) {
                release_ped_reference(accepted_ped);
                return failed("Original kill objective could not register its actual ped target reference");
            }
        } else if(plan.proposed().previous==30 && plan.proposed().current==22) {
            const auto target=sa_route_vehicle_snapshot(ordinary.target.handle);
            if(!target || !(vehicle_identity(*target)==ordinary.target))
                return failed("Original destroy objective target changed before reference registration");
            accepted_vehicle=prepare_vehicle_reference(*target);
            if(!accepted_vehicle)return failed("Original destroy objective could not register its vehicle target reference");
        }
        const auto effect=execute_once();
        if(effect.executed && effect.native_entered) {
            if(!commit_source_plan(plan,actor,call.mission_context,std::move(accepted_vehicle),std::move(accepted_ped))) {
                projected=false;untracked(actor.handle);
                auto terminal=failed("Accepted original objective could not be committed to the route source ledger");
                terminal.native_entered=true;return terminal;
            }
            projected=true;
        }
        release_reference(accepted_vehicle);release_ped_reference(accepted_ped);
        if(effect.native_entered && !effect.executed)untracked(actor.handle);
        return effect;
    }

    // Script entry point. Internal transitions use the private implementation
    // above so they cannot replace the script's objective latch or target.
    NativeResult execute_source_objective(uint16_t original,const NativeCall& call,bool& projected,
            const std::function<NativeResult()>& execute_once) {
        std::optional<uint64_t> previous_revision;
        if(!call.inputs.empty()) {
            const auto live=sa_live_objective_actor(call.inputs[0]);
            if(live)if(const auto state=intents_.state(*live))previous_revision=state->revision;
        }
        auto result=execute_source_objective_impl(original,call,projected,execute_once);
        if(result.executed && !call.inputs.empty())
            note_script_objective(original,call,projected,previous_revision,result);
        return result;
    }

    // nullopt means the current objective or its owner cannot be proved.
    std::optional<bool> objective_passed(int32_t handle) {
        objective_reason_="";
        const auto live=sa_live_objective_actor(handle);
        if(!live){objective_reason_="ped handle is not a live native ped";return std::nullopt;}
        const auto found=objective_watches_.find(handle);
        if(found==objective_watches_.end() || !(found->second.actor==*live)) {
            const auto state=intents_.state(*live);
            if(state && state->current!=OriginalRouteIntents::none){objective_reason_="ped has an active objective but no watch was registered for it";return std::nullopt;}
            return std::optional<bool>(false);
        }
        // Latch ordering: a cleared latch is only republished by the frame
        // consumer, so a query before that tick sees the source reset value.
        // This frame-local FALSE is the setter's reset, not an unsupported
        // completion guess; after the tick, unknown watches stay unavailable.
        if(found->second.reset_pending)
            return false;
        const auto verdict=evaluate_objective_watch(found->second);
        if(!verdict && !*objective_reason_)objective_reason_="watch evaluation unavailable";
        return verdict;
    }
    const char* objective_reason()const{return objective_reason_;}

    void service_objective_watches() {
        servicing_=true;service_now_=frame_now();
        struct Done {SaRouteService* self;~Done(){self->servicing_=false;}} done{this};
        std::vector<int32_t> handles;handles.reserve(objective_watches_.size());
        for(const auto& entry:objective_watches_)handles.push_back(entry.first);
        for(const auto handle:handles) {
            const auto found=objective_watches_.find(handle);if(found==objective_watches_.end())continue;
            const auto live=sa_live_objective_actor(handle);
            if(!live || !(found->second.actor==*live)) {retire_objective_watch(handle);continue;}
            found->second.reset_pending=false;
            (void)evaluate_objective_watch(found->second);
        }
    }

    bool tick_all() {
        if(!service_script_entries())return false;
        const auto clock=sa_frame_clock();if(!clock)return false;
        service_objective_watches();
        auto actors=jobs_.active_actors();
        // Source vehicle objectives also exist before a first FOLLOW_ROUTE.
        // Keep them separate from route jobs; never manufacture a route cursor.
        for(const auto& tracked:vehicle_actors_)
            if(std::find(actors.begin(),actors.end(),tracked.second)==actors.end())actors.push_back(tracked.second);
        for(const auto& actor:actors) {
            const auto live=actor_?actor_(actor.handle):std::nullopt;
            if(!live || !(*live==actor)) {retired(actor);continue;}
            const auto state=intents_.state(actor);
            if(!state) {untracked(actor.handle);continue;}
            const bool active_vehicle_goal=state->current==16 || state->current==17 || state->current==18;
            if(const auto exiting=exits_.find(actor.handle);exiting!=exits_.end()) {
                const auto ped=sa_route_actor_vehicle_snapshot(actor);
                const bool still_same=ped && ped->in_vehicle &&
                    ped->current_vehicle_handle==exiting->second.target.handle &&
                    ped->current_vehicle_entity==exiting->second.target.native_entity &&
                    ped->current_vehicle_generation==exiting->second.target.generation &&
                    state->revision==exiting->second.revision;
                if(!still_same) {
                    const bool completed_wrong_vehicle_exit=ped && ped->alive && !ped->in_vehicle &&
                        state->revision==exiting->second.revision &&
                        (state->current==17 || state->current==18);
                    exits_.erase(exiting);
                    if(completed_wrong_vehicle_exit) {
                        // A real on-foot snapshot after our accepted exit means
                        // ProcessObjective can issue its seek/entry attempt again.
                        // It says nothing about task completion, so only
                        // invalidate the old entry receipt.
                        retire_entry(actor.handle);
                    }
                }
            }
            if(active_vehicle_goal && !process_vehicle_goal(actor,*state,clock->now))return false;
            if(!active_vehicle_goal && !process_position_goal(actor,*state))return false;
            if(!active_vehicle_goal && !process_target_goal(actor,*state))return false;
            // A real source transition can overwrite the single previous
            // objective and discard this route job during the current tick.
            const auto after=intents_.state(actor);
            if(after && after->current!=30 && after->previous!=30)continue;
            if(jobs_.needs_assignment(actor) && !start_route_assignment(actor))return false;
            const auto result=jobs_.tick(actor);
            if(result==OriginalRouteJobs<OwnedRouteTaskHost>::Result::unavailable)return false;
        }
        return true;
    }

    void clear() {
        while(!script_entries_.empty())retire_script_entry(script_entries_.begin()->first);
        jobs_.clear();cleanup_.clear();exits_.clear();vehicle_actors_.clear();
        while(!entries_.empty())retire_entry(entries_.begin()->first);
        for(auto& [handle,ref]:vehicle_refs_){(void)handle;release_reference(ref);}
        vehicle_refs_.clear();
        for(auto& entry:ped_refs_)release_ped_reference(entry.second);
        ped_refs_.clear();
        while(!objective_watches_.empty())retire_objective_watch(objective_watches_.begin()->first);
        walk_probes_={};
        for(auto& entry:remembered_vehicles_)release_reference(entry.second.reference);
        remembered_vehicles_.clear();
    }
};

}
