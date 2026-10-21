#pragma once
#include "objective_receipts.hpp"
#include "route_table.hpp"
#include <unordered_map>

namespace lcs {
struct SourceEntityIdentity {
    int32_t handle=-1;
    uintptr_t native_entity=0;
    uint64_t generation=0;
    bool valid()const{return handle>=0 && native_entity;}
    bool operator==(const SourceEntityIdentity& other)const {
        return handle==other.handle && native_entity==other.native_entity && generation==other.generation;
    }
};
struct RouteSourceIntent {
    ObjectiveActor actor;
    uint64_t revision=0;
    bool current_route=false,temporary=false,running=false;
};
struct RouteIntentReader {
    virtual ~RouteIntentReader()=default;
    virtual std::optional<RouteSourceIntent> route_intent(ObjectiveActor)=0;
};

// Source objective transitions for FOLLOW_ROUTE and its vehicle interruptions.
// This is not an inference from SA task presence, and does not publish the SCM
// completion latch. The caller must supply a known SOURCE state and commit a
// plan only after its actual engine assignment/restoration was accepted.
class OriginalRouteIntents final:public RouteIntentReader {
public:
    static constexpr int32_t none=0,follow_route=30;
    struct State {
        ObjectiveActor actor;
        uint64_t revision=0;
        int32_t current=none,previous=none;
        int16_t route=0,mode=0;
        bool running=false;
        std::optional<uint8_t> source_created_by;
        SourceEntityIdentity target;
        std::optional<std::array<float,3>> target_position;
        uint32_t objective_deadline=0,leave_car_deadline=0;
        bool leave_car_deadline_known=true;
        // Assignment seed only. The live OriginalRouteConsumer owns its
        // advancing cursor; restoration must retain THAT cursor, never this seed.
        std::optional<OriginalRouteCursor> cursor;
    };
    enum class Setter {plain,entity,position};
    struct OrdinaryAdmission {
        int32_t source_state=-1;
        Setter setter=Setter::entity;
        bool attached=false,dying=false,self_target=false,is_player=false,in_vehicle=false,ped_in_control=true;
        SourceEntityIdentity target;
        std::optional<std::array<float,3>> target_position,actor_position;
        uint32_t objective_deadline=0;
        std::optional<uint32_t> leave_car_deadline;
        bool leave_car_deadline_unknown=false;
    };
    struct VehicleAdmission {
        int32_t source_state=-1;
        bool in_vehicle=false,attached=false,self_target=false,same_target=false,dying=false,is_player=false;
        Setter setter=Setter::entity;
        std::optional<bool> target_boat,on_target_boat;
        std::optional<bool> target_bus;
        std::optional<bool> mission_character,target_disabled,player_controls_disabled;
        SourceEntityIdentity target;
        uint32_t now=0;
        std::optional<uint32_t> leave_car_deadline;
        bool leave_car_deadline_unknown=false;
    };
    enum class Change {ignored,ready,unavailable};
    enum class Cause {none,route,ordinary,vehicle,restore,running};
    struct Plan {
        Change status()const{return change;}
        Cause reason()const{return cause;}
        const State& previous()const{return before;}
        const State& proposed()const{return after;}
    private:
        friend class OriginalRouteIntents;
        Change change=Change::unavailable;
        Cause cause=Cause::none;
        State before,after;
    };
private:
    std::unordered_map<int32_t,State> actors_;
    uint64_t next_revision_=1;
    static bool valid(ObjectiveActor a) {
        return a.handle>=0 && !(uint32_t(a.handle)&0x80u) && a.native_ped;
    }
    static int16_t narrow(int32_t value) {
        const auto v=uint16_t(uint32_t(value));
        return v<=0x7fff?int16_t(v):int16_t(int32_t(v)-0x10000);
    }
    static bool dying(int32_t source_state){return source_state==54 || source_state==55;}
    static bool temporary_goal(int32_t objective) {
        return objective==16 || objective==17 || objective==18 || objective==29 || objective==38;
    }
    static bool entity_duplicate_target(int32_t objective) {
        switch(objective) {
        case 8:case 9:case 12:case 13:case 14:case 15:case 23:case 28:case 50:case 51:
        case 17:case 18:case 22:case 31:case 34:return true;
        default:return false;
        }
    }
    static bool entity_attached_allowed(int32_t objective) {
        return objective==8 || objective==9 || objective==21 || objective==22;
    }
    static bool stores_current(int32_t current,bool entering) {
        // Literal retail jump tables 3C7110 / 3C71E0. The two tables differ at
        // KILL_CHAR_ANY_MEANS, FOLLOW_CHAR_IN_FORMATION and LEAVE_CAR.
        switch(current) {
        case 3:case 8:case 10:case 11:case 12:case 13:case 14:case 17:case 18:
        case 24:case 25:case 39:case 40:case 43:case 44:case 45:case 49:case 53:
            return false;
        case 9:case 15:case 16:return entering;
        default:return true;
        }
    }
    static void store(State& state,bool entering) {
        if(state.current!=state.previous && stores_current(state.current,entering))state.previous=state.current;
    }
    bool matches(const State& state)const {
        const auto found=actors_.find(state.actor.handle);
        return found!=actors_.end() && found->second.actor==state.actor &&
            found->second.revision==state.revision && state.revision;
    }
    uint64_t issue() {
        const auto value=next_revision_;
        if(value)next_revision_=value==UINT64_MAX?0:value+1;
        return value;
    }
public:
    static bool temporary(int32_t objective) {
        return temporary_goal(objective);
    }
    // Real source CPed initialization only; never call to bless a polled native
    // actor, after receipt loss, or merely because its task list is empty.
    bool initialized(ObjectiveActor actor) {
        if(!valid(actor) || !next_revision_)return false;
        if(actors_.count(actor.handle))return false;
        for(auto it=actors_.begin();it!=actors_.end();) {
            if(it->second.actor.native_ped==actor.native_ped)it=actors_.erase(it);else ++it;
        }
        State state;state.actor=actor;state.revision=issue();actors_.emplace(actor.handle,state);return true;
    }
    // A new, accepted ORIGINAL constructor may reuse even the same 7-bit pool
    // generation/address after wrap. This authority comes from the producer,
    // never a repeated initialization request or a byte-identical native poll.
    bool constructed(ObjectiveActor actor,std::optional<uint8_t> source_created_by={}) {
        if(!valid(actor))return false;
        for(auto it=actors_.begin();it!=actors_.end();) {
            if(it->first==actor.handle || it->second.actor.native_ped==actor.native_ped)it=actors_.erase(it);else ++it;
        }
        if(!initialized(actor))return false;
        actors_.at(actor.handle).source_created_by=source_created_by;
        return true;
    }
    std::optional<State> state(ObjectiveActor actor)const {
        const auto found=actors_.find(actor.handle);
        if(found==actors_.end() || !(found->second.actor==actor))return {};
        return found->second;
    }
    std::optional<RouteSourceIntent> route_intent(ObjectiveActor actor)override {
        const auto s=state(actor);if(!s)return {};
        return RouteSourceIntent{actor,s->revision,s->current==follow_route,temporary(s->current),s->running};
    }
    Plan follow(ObjectiveActor actor,int32_t route,int32_t mode,int32_t source_state,
            const OriginalRouteTable& table)const {
        const auto current=state(actor);if(!current || source_state<0 || source_state>63)return {};
        Plan plan;plan.before=plan.after=*current;plan.cause=Cause::route;
        const auto id=narrow(route),type=narrow(mode);
        if(dying(source_state) || (current->previous==follow_route) ||
           (current->current==follow_route && current->route==id && current->mode==type)) {
            plan.change=Change::ignored;return plan;
        }
        const auto cursor=OriginalRouteCursor::begin(table,id,type);if(!cursor)return plan;
        if(temporary(current->current))plan.after.previous=follow_route;
        else if(current->current!=follow_route)store(plan.after,false);
        // SetObjective first writes a pending objective for temporary states,
        // but SetFollowRoute at30DD30 ALWAYS overwrites the CURRENT objective.
        // Deferring FOLLOW_ROUTE until the vehicle task disappears is wrong.
        plan.after.current=follow_route;plan.after.route=id;plan.after.mode=type;plan.after.cursor=*cursor;
        // Route overload calls SetObjectiveTimer(0) after duplicate guards.
        // A passenger-entry deadline must never expire a newly accepted route.
        plan.after.objective_deadline=0;
        plan.change=Change::ready;return plan;
    }
    Plan vehicle_interruption(ObjectiveActor actor,int32_t objective,const VehicleAdmission& admission)const {
        const auto current=state(actor);
        if(!current || admission.source_state<0 || admission.source_state>63 ||
           (objective!=16 && objective!=17 && objective!=18 && objective!=38))return {};
        Plan plan;plan.before=plan.after=*current;plan.cause=Cause::vehicle;
        // Retail30836C also rejects source state32. This extra guard is absent
        // from the reconstruction and must not be copied to the route overload.
        if(dying(admission.source_state) || admission.source_state==32 || admission.attached || admission.self_target || current->previous==objective ||
           (current->current==objective && (objective==16 || objective==38 || admission.same_target)) ||
           (current->current!=objective && (objective==16 || objective==38) && !admission.in_vehicle)) {
            plan.change=Change::ignored;return plan;
        }
        if(admission.setter==Setter::entity && !admission.target.valid())return {};
        if((objective==17 || objective==18) && !admission.is_player &&
           (!admission.target_boat || (*admission.target_boat && !admission.on_target_boat)))return {};
        if((objective==17 || objective==18) && !admission.is_player && admission.target_boat==true &&
           admission.on_target_boat==false) {
            // The source setter stores the current objective for an ENTER
            // objective before its boat-surface check. That check then calls
            // RestorePreviousObjective before assigning m_carInObjective, so
            // the restored goal keeps the old target reference.
            if(current->current!=objective)store(plan.after,true);
            plan.after.current=objective;
            plan.after.current=plan.after.previous;plan.after.previous=none;
            if(plan.after.current!=16 && plan.after.current!=17 && plan.after.current!=18 &&
               plan.after.current!=29 && plan.after.current!=38)
                plan.after.target={};
            plan.after.objective_deadline=0;
            if(plan.after.current!=follow_route) {
                plan.after.route=0;plan.after.mode=0;plan.after.cursor.reset();
            }
            plan.change=Change::ready;plan.cause=Cause::restore;return plan;
        }
        uint32_t vehicle_objective_deadline=0;
        if(objective==17) {
            // SetObjective(ENTER_CAR_AS_PASSENGER) gives mission peds a
            // 14-second EnteringCar fallback timeout only for a disabled
            // player vehicle or disabled player controls. It does not delay
            // the initial seek/entry objective. Unknown inputs cannot be
            // treated as false.
            if(!admission.mission_character)return {};
            if(*admission.mission_character) {
                if(!admission.target_disabled || !admission.player_controls_disabled)return {};
                if(*admission.target_disabled || *admission.player_controls_disabled)
                    vehicle_objective_deadline=admission.now+14000;
            }
        }
        if(current->current!=objective) {
            // ForceStoredObjective only applies its selective table for ENTER;
            // LEAVE always stores current, even when it is already temporary.
            if(objective==17 || objective==18)store(plan.after,true);
            else plan.after.previous=plan.after.current;
        }
        plan.after.current=objective;plan.after.target=admission.target;
        plan.after.objective_deadline=vehicle_objective_deadline;
        if(admission.leave_car_deadline) {
            plan.after.leave_car_deadline=*admission.leave_car_deadline;
            plan.after.leave_car_deadline_known=true;
        } else if(admission.leave_car_deadline_unknown) {
            plan.after.leave_car_deadline_known=false;
        }
        if((objective==16 || objective==38) && current->leave_car_deadline==0 &&
           admission.target_bus!=false && !admission.leave_car_deadline &&
           !admission.leave_car_deadline_unknown) {
            plan.after.leave_car_deadline_known=false;
        }
        plan.change=Change::ready;return plan;
    }
    Plan restored(ObjectiveActor actor)const {
        const auto current=state(actor);if(!current)return {};
        Plan plan;plan.before=plan.after=*current;plan.cause=Cause::restore;
        if(current->current==none){plan.change=Change::ignored;return plan;}
        // This source state has no accepted producer in the route adapter.
        if(current->current==7)return plan;
        plan.after.current=current->previous;plan.after.previous=none;
        // RestorePreviousObjective changes only the objective enums. The
        // source has one m_carInObjective reference, so restoring a vehicle
        // objective keeps the current reference; route and non-vehicle goals
        // do not consume it in this projection.
        if(plan.after.current!=16 && plan.after.current!=17 && plan.after.current!=18 &&
           plan.after.current!=29 && plan.after.current!=38)
            plan.after.target={};
        if(plan.after.current!=follow_route) {
            plan.after.route=0;plan.after.mode=0;plan.after.cursor.reset();
        }
        plan.change=Change::ready;return plan;
    }
    Plan running(ObjectiveActor actor,bool enabled)const {
        const auto current=state(actor);if(!current)return {};
        Plan plan;plan.before=plan.after=*current;plan.cause=Cause::running;
        plan.after.running=enabled;
        plan.change=current->running==enabled?Change::ignored:Change::ready;return plan;
    }
    // Project a successfully accepted ordinary SetObjective overload into the
    // original current/previous ledger. Native effects are performed by the
    // caller first; ignored source guards never manufacture a route restore.
    Plan ordinary(ObjectiveActor actor,int32_t objective,const OrdinaryAdmission& admission)const {
        const auto current=state(actor);
        if(!current || objective<0 || objective>63 || admission.source_state<0 || admission.source_state>63)return {};
        if(objective==follow_route)return {};
        Plan plan;plan.before=plan.after=*current;plan.cause=Cause::ordinary;
        const bool attached_rejected=admission.attached &&
            (admission.setter==Setter::plain ||
             (admission.setter==Setter::entity && !entity_attached_allowed(objective)));
        if(admission.dying || dying(admission.source_state) || attached_rejected ||
           (admission.setter==Setter::entity && admission.self_target) ||
           (current->previous==objective && current->previous!=none)) {
            plan.change=Change::ignored;return plan;
        }
        if(admission.setter==Setter::position && (objective==24 || objective==25 || objective==49)) {
            if(!admission.target_position || !admission.actor_position)return {};
            for(size_t i=0;i<3;++i)
                if(!std::isfinite((*admission.target_position)[i]) || !std::isfinite((*admission.actor_position)[i]))return {};
            plan.after.running=objective!=24;
            const float dx=(*admission.target_position)[0]-(*admission.actor_position)[0];
            const float dy=(*admission.target_position)[1]-(*admission.actor_position)[1];
            const float distance=dx*dx+dy*dy;
            if(!std::isfinite(distance))return {};
            if(distance<0.25f) {
                plan.cause=Cause::running;
                plan.change=plan.after.running==current->running?Change::ignored:Change::ready;
                return plan;
            }
            plan.after.target_position=admission.target_position;
        }
        if(admission.setter==Setter::plain && objective!=none && current->current==objective) {
            // The plain overload clears m_objectiveTimer before its duplicate
            // current-objective return, while leaving current/previous intact.
            if(current->objective_deadline==0) {
                plan.change=Change::ignored;return plan;
            }
            plan.after.objective_deadline=0;
            plan.change=Change::ready;return plan;
        }
        if(admission.setter==Setter::entity && current->current==objective) {
            if(objective==16 || objective==38) {plan.change=Change::ignored;return plan;}
            if(entity_duplicate_target(objective)) {
                if(!current->target.valid() || !admission.target.valid())return {};
                if(current->target==admission.target) {
                    plan.change=Change::ignored;return plan;
                }
            }
        }
        if(objective==none) {
            if((current->current==16 || current->current==17 || current->current==18 || current->current==38) &&
               !admission.is_player && !admission.ped_in_control) {
                plan.change=Change::ignored;return plan;
            }
            plan.after.current=none;plan.after.previous=none;plan.after.target={};
            // The source NONE overload changes current/previous only; it does
            // not call SetObjectiveTimer(0).
            plan.after.route=0;plan.after.mode=0;plan.after.cursor.reset();
            plan.change=Change::ready;return plan;
        }
        if(temporary_goal(current->current) && !temporary_goal(objective)) {
            plan.after.previous=objective;
        } else if(current->current!=objective) {
            if(temporary_goal(objective) && (objective==17 || objective==18))store(plan.after,true);
            else if(temporary_goal(objective))plan.after.previous=plan.after.current;
            else store(plan.after,admission.setter==Setter::entity && (objective==17 || objective==18));
            plan.after.current=objective;
        }
        if(admission.setter==Setter::entity && admission.target.valid())plan.after.target=admission.target;
        else if(admission.setter!=Setter::entity)plan.after.target={};
        plan.after.objective_deadline=admission.setter==Setter::plain
            ? 0 : admission.objective_deadline;
        if(admission.leave_car_deadline) {
            plan.after.leave_car_deadline=*admission.leave_car_deadline;
            plan.after.leave_car_deadline_known=true;
        } else if(admission.leave_car_deadline_unknown) {
            plan.after.leave_car_deadline_known=false;
        }
        if(plan.after.current!=follow_route && plan.after.previous!=follow_route) {
            plan.after.route=0;plan.after.mode=0;plan.after.cursor.reset();
        }
        plan.change=Change::ready;return plan;
    }
    // Call exactly once after the corresponding real source/native effect was
    // accepted. An entered failure must quarantine the actor via retired().
    bool accepted(const Plan& plan) {
        if(plan.change!=Change::ready || !matches(plan.before) || !next_revision_ ||
           !(plan.before.actor==plan.after.actor) || plan.before.revision!=plan.after.revision)return false;
        auto value=plan.after;value.revision=issue();actors_.at(value.actor.handle)=value;return true;
    }
    bool current(const Plan& plan)const{return matches(plan.before);}
    // Native warp effects update the same source leave-car timer as CPed. This
    // records that observed effect without changing source-objective revision,
    // so a preflighted same-frame restoration remains valid.
    bool record_leave_car_deadline(ObjectiveActor actor,uint64_t revision,uint32_t deadline) {
        const auto found=actors_.find(actor.handle);
        if(found==actors_.end() || !(found->second.actor==actor) ||
           found->second.revision!=revision || !revision)return false;
        found->second.leave_car_deadline=deadline;
        found->second.leave_car_deadline_known=true;
        return true;
    }
    // Runtime source timer updates are facts from the current objective, not
    // new source revisions. The passenger fallback clears this after its
    // source branch completes, including its no-seat/already-entering cases.
    bool record_objective_deadline(ObjectiveActor actor,uint64_t revision,uint32_t deadline) {
        const auto found=actors_.find(actor.handle);
        if(found==actors_.end() || !(found->second.actor==actor) ||
           found->second.revision!=revision || !revision)return false;
        found->second.objective_deadline=deadline;
        return true;
    }
    void retired(ObjectiveActor actor) {
        const auto found=actors_.find(actor.handle);
        if(found!=actors_.end() && found->second.actor==actor)actors_.erase(found);
    }
    // Real accepted source objective with an as-yet unprojected consumer.
    // Revoke knowledge; never turn a native task poll into replacement NONE.
    void untracked_objective(int32_t handle){actors_.erase(handle);}
    void clear(){actors_.clear();} // serial deliberately survives world cleanup
};
}
