#include "lcs/native.hpp"
#include "lcs/threat_ranges.hpp"
#include "lcs/route_vehicle_goal.hpp"
#include "ScriptCommands.h"
#include <algorithm>
#include <exception>
#include <stdexcept>
#include <sstream>
#include <cmath>
#include <cstring>
#include <set>

namespace lcs {
void NativeAdapter::observe_source_actor_acquired(int32_t handle) {
    if(handle<0 || !source_actor_acquired)return;
    const auto actor=live_ped_identity?live_ped_identity(handle):std::nullopt;
    if(!actor)throw std::runtime_error("Selected character lifetime unavailable after native acquisition; do not replay");
    source_actor_acquired(*actor);
}
void NativeAdapter::observe_source_goal_unknown(int32_t actor)noexcept {
    try {if(source_actor_goal_unknown)source_actor_goal_unknown(actor);}
    catch(...) {
        try {if(diagnostic_event)diagnostic_event("Source objective observation revoked after accepted effect; no native replay");}catch(...){}
    }
}
void NativeAdapter::observe_source_running(int32_t actor,bool enabled)noexcept {
    if(!source_actor_running)return;
    try {
        const auto live=live_ped_identity?live_ped_identity(actor):std::nullopt;
        if(live)source_actor_running(*live,enabled);else observe_source_goal_unknown(actor);
    }catch(...){observe_source_goal_unknown(actor);}
}
NativeResult NativeAdapter::execute_projected_objective(uint16_t original,const NativeCall& call,
        bool& projected,const std::function<NativeResult()>& native) {
    bool called=false;
    const auto once=[&]() {
        if(called)throw std::runtime_error("Source objective executor cannot replay the same setter");
        called=true;return native();
    };
    if(!execute_source_objective)return once();
    try {
        auto effect=execute_source_objective(original,call,projected,once);
        if(!called && (effect.native_entered || (effect.executed && !projected)))
            throw std::runtime_error("Source objective has no native effect or original ignored-setter receipt");
        if(!effect.executed && (effect.native_entered || projected))
            throw std::runtime_error("Source objective failed after entry; do not replay: "+effect.diagnostic);
        return effect;
    }catch(...){observe_source_goal_unknown(call.inputs[0]);throw;}
}
NativeResult NativeAdapter::execute_collective_flee(const ObjectiveActor& actor,const ObjectiveActor& target){
    NativeResult result;
    const auto current=live_ped_identity?live_ped_identity(actor.handle):std::nullopt;
    const auto current_target=live_ped_identity?live_ped_identity(target.handle):std::nullopt;
    if(!world_ready || !execute || !current || !(*current==actor) || !current_target || !(*current_target==target)){
        result.diagnostic="Collective flee actor/target identity unavailable";return result;
    }
    NativeCall call;call.opcode=0x05DB;call.inputs={actor.handle,target.handle,100,-1};
    result=execute(call);
    if(result.executed)forget_walking_objective(actor.handle);
    return result;
}
void NativeAdapter::walking_task_associated(size_t slot,const ScriptTaskRecord& before,
        const ScriptTaskRecord& after,uint32_t task) {
    if(slot>=128)return;
    for(auto entry=accepted_walk_tasks_.begin();entry!=accepted_walk_tasks_.end();) {
        if(entry->second.slot!=slot){++entry;continue;}
        const auto associated=associate_captured_event(entry->second,slot,before,after,task);
        if(associated){entry->second=*associated;++entry;}
        else entry=accepted_walk_tasks_.erase(entry);
    }
}
void NativeAdapter::walking_task_invalidated(size_t slot) {
    if(slot>=128)return;
    // Set helpers inline their Flush: invalidate on any observed replacement,
    // even when the pool address/opcode/event bits are reused unchanged.
    for(auto entry=accepted_walk_tasks_.begin();entry!=accepted_walk_tasks_.end();) {
        if(entry->second.slot==slot)entry=accepted_walk_tasks_.erase(entry);
        else ++entry;
    }
    for(auto entry=kill_objectives_.begin();entry!=kill_objectives_.end();) {
        if(entry->second.task.binding.slot==slot)entry=kill_objectives_.erase(entry);
        else ++entry;
    }
}
static constexpr int strict_weapon_counterparts[]={0,1,-1,2,3,4,5,-1,-1,-1,8,9,16,39,17,18,-1,22,-1,25,27,26,32,28,-1,29,31,-1,34,-1,35,37,-1,38,40,-1,43};
static constexpr int equivalent_weapon_counterparts[]={0,1,4,2,3,4,5,5,4,8,8,9,16,39,17,18,35,22,24,25,27,26,32,28,28,29,31,30,34,34,35,37,31,38,40,38,43};
static bool common_garage_type(int32_t type) {return (type>=0 && type<=5) || type==11 || (type>=14 && type<=21) || type==23 || (type>=24 && type<=32);}
static std::string lower(std::string s) { for(auto& c:s) c=char(std::tolower(static_cast<unsigned char>(c))); return s; }
static int32_t float_bits(float value) {int32_t encoded;std::memcpy(&encoded,&value,sizeof(encoded));return encoded;}
static float bits_float(int32_t encoded) {float value;std::memcpy(&value,&encoded,sizeof(value));return value;}
// LCS flee state (Ped.cpp Flee): chars use SetFlee(entity,6000) and keep fleeing
// while within 30 m of the threat; the position form is SetFlee(own pos,10000).
// SA's own flee tasks (05DA point, float radius; 05DB char, FILD integer radius)
// receive those finite times; safety and completion remain SA AI.
static constexpr int32_t flee_safe_radius=30,flee_char_ms=6000,flee_point_ms=10000;
// LCS leaves the car first (bFleeAfterExitingCar) and only then flees. SA has no
// single "leave then flee" task, so an occupant gets a real SA script sequence
// built with the reviewed commands 0615 OPEN, 0633 TASK_LEAVE_ANY_CAR, the flee
// task, 0616 CLOSE, 0618 PERFORM and 061B CLEAR. Inside an open sequence SA's
// GivePedScriptedTask (465C20) appends the task when the ped operand is -1.
// Safety and completion of both tasks remain SA AI.
// Ownership: OPEN overwrites the global sequence under construction (8D2E98) and
// CLOSE resets it to -1, so the sequence is opened only when the real native state
// shows nothing open, and only slots that this OPEN newly allocated are ever closed
// or cleared. A handle is (uint16 generation<<16)|slot: negative int32 values are
// valid, -1 is the pool-full sentinel. After a successful PERFORM the performing
// ped holds a reference, so 061B only marks removal pending: the slot legitimately
// stays allocated until SA releases it. Failure after OPEN is a repair error.
NativeResult NativeAdapter::execute_flee(const NativeCall& flee,bool in_vehicle) {
    if(!in_vehicle)return execute(flee);
    if(flee.inputs.empty() || (flee.opcode!=0x05DA && flee.opcode!=0x05DB)){NativeResult result;result.diagnostic="Flee sequence packet is not a reviewed SA flee task";return result;}
    NativeCall task=flee;task.inputs[0]=-1; // sequence operand convention, not an actor handle
    return execute_task_sequence(flee.inputs[0],{NativeCall{0x0633,{-1}},task});
}
// Shared sequence engine (flee, 1136, 1124 delayed exits): `tasks` are SA task
// packets whose ped operand is already -1. Everything below is the approved
// ownership/recovery logic and is unchanged by the callers.
NativeResult NativeAdapter::execute_task_sequence(int32_t ped,const std::vector<NativeCall>& tasks,bool repeat) {
    NativeResult result;
    if(tasks.empty() || tasks.size()>8){result.diagnostic="Task sequence needs one to eight tasks";return result;}
    const auto actor=live_ped_identity?live_ped_identity(ped):std::nullopt;
    if(!actor){result.diagnostic="Flee sequence actor identity unavailable";return result;}
    if(!task_sequence_state){result.diagnostic="Native task-sequence state reader is unavailable";return result;}
    const auto before=task_sequence_state();
    if(!before){result.diagnostic="Native task-sequence state cannot be verified";return result;}
    if(before->active!=-1){result.diagnostic="Another script has a task sequence open; it is not overwritten";return result;}
    const auto make_handle=[](size_t slot,uint16_t generation){return int32_t((uint32_t(generation)<<16)|uint32_t(slot));};
    const auto next_generation=[](uint16_t generation){return generation>=0xFFFE?uint16_t(1):uint16_t(generation+1);};
    // The non-mission allocator scans exactly slots 0..31 in order. Keep the
    // predicted slot as the only candidate this OPEN can own during recovery.
    std::optional<size_t> expected_slot;
    for(size_t slot=0;slot<32;++slot)if(!before->allocated(slot)){expected_slot=slot;break;}
    const uint16_t expected_generation=expected_slot?next_generation(before->generation[*expected_slot]):0;
    const auto owned_slots=[&](const TaskSequenceState& now){
        std::vector<size_t> slots;
        if(!expected_slot)return slots;
        const auto slot=*expected_slot;
        const bool issued_here=now.issued[slot] && now.generation[slot]==expected_generation;
        const bool pending_here=!now.issued[slot] && now.generation[slot]==expected_generation && now.pending[slot] && now.references[slot]>0;
        if(!before->allocated(slot) && now.allocated(slot) && (issued_here || pending_here))slots.push_back(slot);
        return slots;
    };
    // Released = closed and either free or deferred-removal pending behind a performing ped's reference.
    const auto released=[](const TaskSequenceState& now,size_t slot){
        return !now.open[slot] && (!now.allocated(slot) || (now.pending[slot] && now.references[slot]>0));
    };
    const auto settled=[&](const std::optional<TaskSequenceState>& now,const std::vector<size_t>& own){
        if(!now || now->active!=-1)return false;
        for(size_t slot=0;slot<64;++slot) {
            if(std::find(own.begin(),own.end(),slot)!=own.end()){if(!released(*now,slot))return false;}
            else if(now->allocated(slot)!=before->allocated(slot))return false;
        }
        return true;
    };
    const auto unchanged=[&](const std::optional<TaskSequenceState>& now){return settled(now,{});};
    // Close/clear only the predicted slot after its generation proves this
    // OPEN issued the handle. An OPEN fault before issuance uses the narrowly
    // checked backend abort for an empty, still-open record.
    const auto recover=[&]()noexcept->bool{
        try {
            auto now=task_sequence_state();
            if(!now)return false;
            if(expected_slot) {
                const auto slot=*expected_slot;
                const bool pre_handle_open=now->open[slot] &&
                    now->generation[slot]==before->generation[slot] && now->issued[slot]==before->issued[slot];
                if(pre_handle_open) {
                    if(!abort_unissued_task_sequence || !abort_unissued_task_sequence(slot,before->generation[slot],before->issued[slot]))return false;
                    now=task_sequence_state();
                    if(!now)return false;
                }
            }
            const auto own=owned_slots(*now);
            bool complete=true;
            for(const auto slot:own) {
                const auto handle=make_handle(slot,expected_generation);
                if(now->open[slot] && now->active!=int32_t(slot)){complete=false;continue;}
                try{if(now->open[slot] && now->active==int32_t(slot))execute({0x0616,{handle}});}catch(...){complete=false;}
                try{if(!released(*now,slot) || now->open[slot])execute({0x061B,{handle}});}catch(...){complete=false;}
            }
            return complete && settled(task_sequence_state(),own);
        } catch(...) {return false;}
    };
    const auto repair=[&](const std::string& why)->std::runtime_error{
        const bool recovered=recover();
        return std::runtime_error("Native flee sequence requires repair; replay rejected after entry: "+why+
            (recovered?"; owned sequence released":"; owned sequence could not be proven released"));
    };
    NativeResult open;std::exception_ptr open_failure;
    try{open=execute({0x0615,{},{},0,1});}catch(...){open_failure=std::current_exception();}
    const auto after_open=task_sequence_state();
    if(open_failure){
        if(!recover())try{std::rethrow_exception(open_failure);}catch(const std::exception& error){
            throw std::runtime_error(std::string("Native sequence OPEN faulted and ownership could not be proven released: ")+error.what());}
        std::rethrow_exception(open_failure);
    }
    if(!open.executed || open.values[0]==-1) { // refusal or pool-full sentinel
        if(unchanged(after_open)){result.diagnostic=open.executed?"Native task sequence pool is full":"Native task sequence could not be opened";return result;}
        throw repair("OPEN reported no sequence but native state changed");
    }
    const int32_t sequence=open.values[0];
    const uint32_t slot=uint32_t(sequence)&0xFFFFu;
    const uint16_t generation=uint16_t(uint32_t(sequence)>>16);
    if(!expected_slot || slot!=*expected_slot || !after_open || after_open->active!=int32_t(slot) || before->allocated(slot) || !after_open->open[slot] ||
       !after_open->issued[slot] || generation!=expected_generation || after_open->generation[slot]!=generation)
        throw repair("OPEN returned a handle that native state does not show as the newly opened sequence");
    try {
        const auto step=[&](const NativeCall& call,const char* why){
            if(!execute(call).executed)throw std::runtime_error(why);
        };
        for(const auto& task:tasks)step(task,"sequence task could not be added");
        if(repeat)step({0x0643,{sequence,1}},"sequence could not be set to repeat");
        step({0x0616,{sequence}},"sequence could not be closed");
        const auto current=live_ped_identity(ped);
        if(!current || !(*current==*actor))throw std::runtime_error("actor identity changed before the sequence could be performed");
        step({0x0618,{ped,sequence}},"sequence could not be given to the actor");
        step({0x061B,{sequence}},"sequence slot could not be released");
    } catch(const std::exception& error) {throw repair(error.what());}
    catch(...) {throw repair("native executor fault");}
    if(!settled(task_sequence_state(),{size_t(slot)}))throw repair("sequence pool does not show the sequence released after CLEAR");
    result.executed=true;result.native_entered=true;return result;
}
bool NativeAdapter::flee_point_packet(int32_t ped,NativeCall& call,bool& in_vehicle,std::string& diagnostic) {
    const auto in_car=execute({0x00DF,{ped},{},0,0,true});
    if(!in_car.executed){diagnostic="Native vehicle-occupancy query for flee failed";return false;}
    in_vehicle=in_car.condition;
    NativeCall position;position.opcode=0x00A0;position.inputs={ped};position.outputs=3;
    const auto where=execute(position);
    if(!where.executed){diagnostic="Native flee origin is unavailable";return false;}
    for(size_t axis=0;axis<3;++axis) {
        float value;std::memcpy(&value,&where.values[axis],4);
        if(!std::isfinite(value)){diagnostic="Native flee origin is nonfinite";return false;}
    }
    call.opcode=0x05DA;
    call.inputs={ped,where.values[0],where.values[1],where.values[2],float_bits(float(flee_safe_radius)),flee_point_ms};
    call.outputs=0;
    return true;
}
// 483/784: LCS makes the character follow another (m_leader / formation). SA's
// real mechanism is CPedGroupMembership: the target must lead a group and the
// follower must be one of its followers. An existing group is reused; a leader
// without any group gets a new one (SA needs it to represent the relationship).
// SA's own group AI then owns following and positions; no LCS formation state is kept.
NativeResult NativeAdapter::join_leader_group(int32_t follower_handle,int32_t leader_handle,bool mission_context) {
    NativeResult result;
    const auto follower=live_ped_identity?live_ped_identity(follower_handle):std::nullopt;
    const auto leader=live_ped_identity?live_ped_identity(leader_handle):std::nullopt;
    if(!world_ready || !execute || !ped_group || !follower || !leader){result.diagnostic="Native group backend or character identity unavailable";return result;}
    if(*follower==*leader){result.diagnostic="A character cannot follow itself";return result;}
    const auto before_follower=ped_group(*follower),before_leader=ped_group(*leader);
    if(!before_follower || !before_leader){result.diagnostic="Native group membership unavailable";return result;}
    if(before_leader->member && !before_leader->leader){result.diagnostic="Leader belongs to another native group as a follower; that group is not rewritten";return result;}
    if(before_follower->member && before_follower->leader){result.diagnostic="Follower leads a native group; the group is not dissolved";return result;}
    const bool leader_has_group=before_leader->member;
    int32_t group=leader_has_group?before_leader->group:-1;
    if(leader_has_group) {
        if(before_follower->member && before_follower->group==group){result.executed=true;return result;} // already this leader's follower
        const auto exists=execute({0x07FD,{group},{},0,0,true});
        if(!exists.executed || !exists.condition){result.diagnostic="Leader's native group handle is not live";return result;}
    }
    bool entered=false;
    const auto fail=[&](const std::string& why)->NativeResult{
        if(entered)throw std::runtime_error("Native group join requires repair; replay rejected after entry: "+why);
        result.diagnostic=why;return result;
    };
    if(!leader_has_group) {
        if(!before_leader->free_group){result.diagnostic="No native group slot is free for the leader";return result;}
        NativeCall create{0x062F,{0},{},0,1}; // Keep the SA allocator selector used by the source mapping.
        create.mission_context=mission_context;
        const auto created=execute(create);
        entered=created.executed || created.native_entered;
        if(!created.executed)return fail("Native group could not be created for the leader");
        group=created.values[0]; // A complete generation-tagged SCM Group handle, not the native slot.
        const auto exists=execute({0x07FD,{group},{},0,0,true});
        if(!exists.executed || !exists.condition)return fail("Created native group handle is not live");
        if(!execute({0x0630,{group,leader->handle}}).executed)return fail("Native group leader could not be set");
    }
    entered=true;
    if(!execute({0x0687,{follower->handle}}).executed)return fail("Follower's previous scripted task could not be released");
    if(before_follower->member && !execute({0x06C9,{follower->handle}}).executed)return fail("Follower could not leave its previous native group");
    NativeCall join{0x0631,{group,follower->handle}};join.mission_context=mission_context;
    if(!execute(join).executed)return fail("Native follower could not join the leader's group");
    const auto after_follower=ped_group(*follower),after_leader=ped_group(*leader);
    if(!after_follower || !after_leader || !after_follower->member || after_follower->leader || after_follower->group!=group ||
       !after_leader->member || !after_leader->leader || after_follower->leader_ped!=leader->native_ped)
        return fail("Native group state does not show the requested leader after the join");
    result.executed=true;result.native_entered=true;return result;
}
bool NativeAdapter::tick_attack_with_cops() {
    for(auto it=attack_policy_.begin();it!=attack_policy_.end();) {
        const auto live=live_ped_identity?live_ped_identity(it->first):std::nullopt;
        if(!live || !(*live==it->second.actor)){const auto dead=it->first;it=attack_policy_.erase(it);kill_objectives_.erase(dead);continue;}
        ++it;
    }
    std::vector<int32_t> candidates;
    for(const auto& [handle,policy]:attack_policy_)
        if(!policy.attacks && kill_objectives_.count(handle))candidates.push_back(handle);
    if(candidates.empty())return true;
    if(!world_ready || !attack_policy_actor || !attack_policy_world || !attack_policy_task_active)return false;
    const auto world=attack_policy_world();
    if(!world)return false;
    if(!world->player_alive || !world->cops_present)return true;
    for(const auto handle:candidates) {
        const auto policy=attack_policy_.find(handle);const auto goal=kill_objectives_.find(handle);
        if(policy==attack_policy_.end() || goal==kill_objectives_.end() || goal->second.target!=world->player_ped)continue;
        const auto task_active=attack_policy_task_active(policy->second.actor,goal->second.task);
        if(!task_active)return false;
        if(!*task_active){kill_objectives_.erase(goal);continue;}
        const auto facts=attack_policy_actor(policy->second.actor);
        if(!facts)return false;
        // Phase of control: only a released ped, never a mission character; the retail test also excludes cops.
        if(!facts->alive || !facts->on_foot || facts->cop || !facts->released)continue;
        const auto actor=policy->second.actor;
        const auto result=execute_flee_point_till_safe(actor);
        if(result.native_entered && !result.executed)
            throw std::runtime_error("Attack-with-cops flee failed after native entry; replay rejected: "+result.diagnostic);
        if(!result.executed)return false;
        kill_objectives_.erase(handle);                    // SetObjective(FLEE) replaced the kill goal: one reaction per goal
    }
    return true;
}
NativeResult NativeAdapter::execute_flee_point_till_safe(const ObjectiveActor& actor) {
    NativeResult result;
    const auto current=live_ped_identity?live_ped_identity(actor.handle):std::nullopt;
    if(!world_ready || !execute || !current || !(*current==actor)){result.diagnostic="Collective flee actor identity unavailable";return result;}
    NativeCall call;bool in_vehicle=false;
    if(!flee_point_packet(actor.handle,call,in_vehicle,result.diagnostic))return result;
    result=execute_flee(call,in_vehicle);
    if(result.executed)forget_walking_objective(actor.handle);
    return result;
}
NativeResult NativeAdapter::execute_collective_flee_till_safe(const ObjectiveActor& actor,const ObjectiveActor& target) {
    NativeResult result;
    const auto current=live_ped_identity?live_ped_identity(actor.handle):std::nullopt;
    const auto current_target=live_ped_identity?live_ped_identity(target.handle):std::nullopt;
    if(!world_ready || !execute || !current || !(*current==actor) || !current_target || !(*current_target==target)){
        result.diagnostic="Collective flee actor/target identity unavailable";return result;
    }
    const auto in_car=execute({0x00DF,{actor.handle},{},0,0,true});
    if(!in_car.executed){result.diagnostic="Native vehicle-occupancy query for flee failed";return result;}
    NativeCall call;call.opcode=0x05DB;call.inputs={actor.handle,target.handle,flee_safe_radius,flee_char_ms};
    result=execute_flee(call,in_car.condition);
    if(result.executed)forget_walking_objective(actor.handle);
    return result;
}
static bool finite_triplet(const std::array<int32_t,32>& values) {
    for(size_t axis=0;axis<3;++axis){float value;std::memcpy(&value,&values[axis],4);if(!std::isfinite(value))return false;}
    return true;
}
// 409 SET_CHAR_OBJ_GUARD_SPOT (Script2: ground for z<=-100, SetObjective(OBJECTIVE_GUARD_SPOT,pos), guard
// radius 5). LCS keeps the ped at the spot, brings it back when displaced and leaves threat reaction to
// the ped AI. SA adaptation: a repeating native task sequence [walk straight to the spot (untimed), stand
// still 1 s] owned by the SA task manager. It starts by walking to the spot, holds it, and each loop
// returns the ped to the spot after SA's own event responses (fights, pushes) have ended. Threat reaction
// stays SA's ped AI; the LCS radius-5 tolerance is tightened to the spot itself.
NativeResult NativeAdapter::guard_spot(int32_t ped,const std::array<int32_t,3>& at) {
    NativeResult result;
    const auto actor=live_ped_identity?live_ped_identity(ped):std::nullopt;
    if(!world_ready || !execute || !actor){result.diagnostic="Guard-spot actor identity unavailable";return result;}
    for(const auto bits:at)if(!std::isfinite(bits_float(bits))){result.diagnostic="Guard-spot position is not finite";return result;}
    int32_t z=at[2];
    if(bits_float(z)<=-100.0f) {
        const auto ground=execute({0x02CE,{at[0],at[1],float_bits(1000.0f)},{},0,1});
        if(!ground.executed || !std::isfinite(bits_float(ground.values[0])) || bits_float(ground.values[0])<=-1000.0f){result.diagnostic="Native ground for the guarded spot is unavailable";return result;}
        z=ground.values[0];
    }
    // 05D3 with time -2 constructs GoToPointAndStandStill (retail 4907CE -> 668120).
    // That task leaves the actor's vehicle itself before walking; do not reject occupants.
    const auto running=running_chars_.find(ped);
    const int32_t movement=running!=running_chars_.end() && running->second?6:4;
    NativeCall go;go.opcode=0x05D3;go.inputs={-1,at[0],at[1],z,movement,-2};
    NativeCall stand;stand.opcode=0x05BA;stand.inputs={-1,1000};
    return execute_task_sequence(ped,{go,stand},true);
}
// 1136 SET_CHAR_OBJ_FLEE_CAR (Script6/PedAI OBJECTIVE_FLEE_CAR). LCS: an occupant first
// leaves its own vehicle (m_pMyVehicle), then SetFlee(m_pMyVehicle,6000). The SA flee
// task is a point flee (05DA) from the vehicle's position, with the same 30 m radius and
// 6 s duration; the leave-then-flee order is the approved SA sequence. Both operands
// must be live (the original asserts); the flee source is the ped's actual vehicle when
// occupied, otherwise the operand vehicle.
NativeResult NativeAdapter::flee_car(int32_t ped,int32_t car) {
    NativeResult result;
    const auto actor=live_ped_identity?live_ped_identity(ped):std::nullopt;
    if(!world_ready || !execute || !actor){result.diagnostic="Flee-car actor identity unavailable";return result;}
    const auto exists=execute({0x056E,{car},{},0,0,true});
    if(!exists.executed || !exists.condition){result.diagnostic="Flee-car vehicle is not live";return result;}
    const auto in_car=execute({0x00DF,{ped},{},0,0,true});
    if(!in_car.executed){result.diagnostic="Native vehicle-occupancy query for flee-car failed";return result;}
    int32_t source=car;
    if(in_car.condition) {
        const auto own=execute({0x0811,{ped},{},0,1});
        if(!own.executed || own.values[0]==-1){result.diagnostic="Occupied actor's own vehicle is unavailable";return result;}
        const auto own_exists=execute({0x056E,{own.values[0]},{},0,0,true});
        if(!own_exists.executed || !own_exists.condition){result.diagnostic="Occupied actor's own vehicle is not live";return result;}
        source=own.values[0];
    }
    const auto where=execute({0x00AA,{source},{},0,3});
    if(!where.executed || !finite_triplet(where.values)){result.diagnostic="Flee-car vehicle position is unavailable";return result;}
    NativeCall flee;flee.opcode=0x05DA;
    flee.inputs={ped,where.values[0],where.values[1],where.values[2],float_bits(float(flee_safe_radius)),flee_char_ms};
    return execute_flee(flee,in_car.condition);
}
// 1124 SET_ALL_OCCUPANTS_OF_CAR_LEAVE_CAR (CCarAI::TellOccupantsToLeaveCar). LCS: driver
// SetObjective(LEAVE_CAR); then each present passenger (seat order) gets m_leaveCarTimer =
// timer (starts at 100, += random 200..400 per present passenger) before the same objective.
// The timer is an absolute game-time value compared with the current time, so it only
// delays when the game clock is still below it. The same rule is reproduced with SA's
// game timer (01BD) and random generator (0209): a positive remainder becomes a
// stand-still then leave-car sequence, otherwise the ordinary SA leave-car task is used,
// exactly as an individual SET_CHAR_OBJ_LEAVE_CAR. The ambulance driver's speech line is
// SA ambient speech and is not reproduced. After the first accepted occupant a later
// failure is a repair error: accepted exits are never replayed.
NativeResult NativeAdapter::leave_all_occupants(int32_t car) {
    NativeResult result;
    if(!world_ready || !execute || !live_ped_identity){result.diagnostic="Occupant exit backend unavailable";return result;}
    const auto exists=execute({0x056E,{car},{},0,0,true});
    if(!exists.executed || !exists.condition){result.diagnostic="Occupant exit vehicle is not live";return result;}
    struct Occupant {int32_t ped;bool driver;ObjectiveActor actor;};
    std::vector<Occupant> occupants;
    const auto add=[&](const NativeResult& found,bool driver)->bool{
        if(!found.executed)return false;
        if(found.values[0]==-1)return true; // empty seat
        const auto actor=live_ped_identity(found.values[0]);
        if(!actor)return false;
        occupants.push_back({found.values[0],driver,*actor});return true;
    };
    if(!add(execute({0x046C,{car},{},0,1}),true)){result.diagnostic="Driver snapshot is unavailable";return result;}
    const auto seats=execute({0x01EA,{car},{},0,1});
    if(!seats.executed || seats.values[0]<0 || seats.values[0]>8){result.diagnostic="Passenger capacity is unavailable";return result;}
    for(int32_t seat=0;seat<seats.values[0];++seat)
        if(!add(execute({0x0432,{car,seat},{},0,1}),false)){result.diagnostic="Passenger snapshot is unavailable";return result;}
    if(occupants.empty()){result.executed=true;return result;} // original loop has nothing to do
    const auto clock=execute({0x01BD,{},{},0,1});
    if(!clock.executed){result.diagnostic="Native game clock is unavailable";return result;}
    int64_t timer=100;size_t accepted=0;
    for(const auto& occupant:occupants) {
        int64_t remaining=0;
        if(!occupant.driver) {
            remaining=timer-int64_t(uint32_t(clock.values[0]));
            const auto step=execute({0x0209,{200,400},{},0,1});
            if(!step.executed || step.values[0]<200 || step.values[0]>400) {
                if(accepted)throw std::runtime_error("Occupant exit requires repair; replay rejected after entry: random delay unavailable");
                result.diagnostic="Native random delay is unavailable";return result;
            }
            timer+=step.values[0];
        }
        const auto current=live_ped_identity(occupant.ped);
        if(!current || !(*current==occupant.actor)) {
            if(accepted)throw std::runtime_error("Occupant exit requires repair; replay rejected after entry: occupant identity changed");
            result.diagnostic="Occupant identity changed";return result;
        }
        NativeCall leave;leave.opcode=0x05CD;leave.inputs={occupant.ped,car};
        const auto act=[&]()->NativeResult{
            if(remaining<=0)return execute_vehicle_objective?execute_vehicle_objective(leave):execute(leave);
            NativeCall wait;wait.opcode=0x05BA;wait.inputs={-1,int32_t(remaining)};
            NativeCall exit;exit.opcode=0x05CD;exit.inputs={-1,car};
            return execute_task_sequence(occupant.ped,{wait,exit});
        };
        bool projected=false;NativeResult step;
        try{step=execute_projected_objective(COMMAND_SET_CHAR_OBJ_LEAVE_CAR,leave,projected,act);}
        catch(const std::exception& error){
            if(accepted)throw std::runtime_error(std::string("Occupant exit requires repair; replay rejected after entry: ")+error.what());
            throw;
        }
        if(!step.executed) {
            if(accepted)throw std::runtime_error("Occupant exit requires repair; replay rejected after entry: "+step.diagnostic);
            result.diagnostic=step.diagnostic;return result;
        }
        forget_walking_objective(occupant.ped);
        ++accepted;
    }
    result.executed=true;result.native_entered=true;return result;
}
// 1526 SET_CHAR_DIVE_FROM_CAR (Script9 + CPed::SetEvasiveDive(vehicle,1)). LCS always sets
// bRespondsToThreats, then (only if the ped is in control, i.e. not driving/riding) turns
// the ped to a random side perpendicular to the vehicle's movement and dives. SA: the
// same flag through the existing heed-threats producer, then TASK_DIVE_AND_GET_UP (0673)
// with that perpendicular direction (the car -> ped direction when the car is stationary)
// and a 1 ms ground time (LCS gets up 1 ms after the fall state begins). The previous
// LCS state restore, the hands-up variant and cop wanted-level crimes are SA AI/wanted.
NativeResult NativeAdapter::dive_from_car(int32_t ped,int32_t car) {
    NativeResult result;
    const auto actor=live_ped_identity?live_ped_identity(ped):std::nullopt;
    if(!world_ready || !execute || !actor || !set_char_heed_threats || !char_in_control){result.diagnostic="Dive-from-car backend or actor identity unavailable";return result;}
    const auto exists=execute({0x056E,{car},{},0,0,true});
    if(!exists.executed || !exists.condition){result.diagnostic="Dive-from-car vehicle is not live";return result;}
    const auto control=char_in_control(ped);
    if(!control){result.diagnostic="Native ped-in-control state is unavailable";return result;}
    const auto in_car=execute({0x00DF,{ped},{},0,0,true});
    if(!in_car.executed){result.diagnostic="Native vehicle-occupancy query for dive failed";return result;}
    const bool dives=*control && !in_car.condition;
    int32_t dx=0,dy=0;
    if(dives) {
        const auto speed=execute({0x06A2,{car},{},0,3});
        if(!speed.executed || !finite_triplet(speed.values)){result.diagnostic="Vehicle movement vector is unavailable";return result;}
        float vx,vy;std::memcpy(&vx,&speed.values[0],4);std::memcpy(&vy,&speed.values[1],4);
        float x=0,y=0;
        if(std::hypot(vx,vy)>0.1f) {
            const auto side=execute({0x0209,{0,2},{},0,1}); // SA upper bound is exclusive.
            if(!side.executed || (side.values[0]!=0 && side.values[0]!=1)){result.diagnostic="Native random side is unavailable";return result;}
            x=side.values[0]?-vy:vy;y=side.values[0]?vx:-vx; // +/-90 degrees from the movement direction
        } else {
            const auto car_at=execute({0x00AA,{car},{},0,3}),ped_at=execute({0x00A0,{ped},{},0,3});
            if(!car_at.executed || !ped_at.executed || !finite_triplet(car_at.values) || !finite_triplet(ped_at.values)){result.diagnostic="Dive origin positions are unavailable";return result;}
            float cx,cy,px,py;std::memcpy(&cx,&car_at.values[0],4);std::memcpy(&cy,&car_at.values[1],4);std::memcpy(&px,&ped_at.values[0],4);std::memcpy(&py,&ped_at.values[1],4);
            x=px-cx;y=py-cy;
        }
        const float length=std::hypot(x,y);
        if(!std::isfinite(length) || length<0.001f){result.diagnostic="Dive direction cannot be determined";return result;}
        dx=float_bits(x/length);dy=float_bits(y/length);
    }
    if(!set_char_heed_threats(ped,true)){result.diagnostic="Native responds-to-threats flag could not be set";return result;}
    result.native_entered=true; // the original sets the flag before the in-control test
    if(dives) {
        const auto task=execute({0x0673,{ped,dx,dy,1}});
        if(!task.executed)throw std::runtime_error("Dive requires repair; replay rejected after entry: "+task.diagnostic);
        forget_walking_objective(ped);
    }
    result.executed=true;return result;
}
NativeExecutor guard_native_executor(NativeExecutor executor) {
    if(!executor)return {};
    return [executor=std::move(executor)](const NativeCall& call){
        auto result=executor(call);
        if(!result.executed && result.native_entered) {
            std::ostringstream error;error<<"Native execution requires repair; replay rejected after entry: opcode=0x"<<std::hex<<call.opcode;
            if(!result.diagnostic.empty())error<<"; "<<result.diagnostic;
            throw std::runtime_error(error.str());
        }
        return result;
    };
}
void NativeAdapter::tick_phones() {
    if(!phone_phase_available || !phone_phase_available() || !update_phone_frame)return;
    for(size_t i=0;i<claimed_phones_;++i) {
        auto& phone=phones_[i];if(!phone.enabled)continue;
        const auto result=update_phone_frame(phone.position,phone.frames);
        if(phone.reported!=result && diagnostic_event) {
            if(result!=PhoneFrameResult::accepted || phone.reported)
                diagnostic_event("Original phone frame: handle="+std::to_string(i)+" state="+
                    (result==PhoneFrameResult::accepted?"accepted":result==PhoneFrameResult::deferred?"deferred":"quarantined"));
        }
        phone.reported=result;
    }
}
void NativeAdapter::load_weapon_definitions(std::istream& input) {
    static constexpr const char* names[]={
#include "original_weapon_names.inc"
    };
    std::array<std::optional<OriginalWeaponDefinition>,37> definitions{};
    std::string line;size_t count=0;bool ended=false;
    const auto integer=[](const std::string& token){size_t used=0;const auto value=std::stoi(token,&used);if(used!=token.size())throw std::runtime_error("Invalid original weapon integer");return value;};
    while(std::getline(input,line)) {
        if(const auto comment=line.find('#');comment!=std::string::npos)line.resize(comment);
        std::istringstream row(line);std::vector<std::string> fields;std::string field;
        while(row>>field)fields.push_back(field);if(fields.empty())continue;
        if(fields[0]=="ENDWEAPONDATA"){ended=true;break;}
        if(fields.size()!=26)throw std::runtime_error("Original weapon definition requires 26 fields");
        size_t id=0;while(id<std::size(names) && lower(names[id])!=lower(fields[0]))++id;
        if(id==std::size(names) || definitions[id])throw std::runtime_error("Unknown or duplicate original weapon definition");
        const auto model=integer(fields[22]),slot=integer(fields[25]);
        if(model< -1 || model>=20000 || slot<0 || slot>=10)throw std::runtime_error("Original weapon model/slot out of range");
        definitions[id]=OriginalWeaponDefinition{model,slot};++count;
    }
    if(input.bad() || !ended || count!=definitions.size())throw std::runtime_error("Original weapon definitions incomplete");
    weapon_definitions_=definitions;
}
std::vector<uint8_t> NativeCall::encode() const {
    if(source_entity_command || source_statistic || source_frenzy_headshots || source_environment || source_garage || source_traffic)throw std::runtime_error("Source family operations cannot enter the native opcode dispatcher");
    if(suppress_previous_brief && opcode!=0x03E5 && opcode!=0x0512)
        throw std::runtime_error("Previous-brief suppression is restricted to reviewed help commands");
    if(mission_flag_reference) {
        if(opcode!=0x0180 || !inputs.empty() || outputs || text || replacement_text || predicate)
            throw std::runtime_error("Invalid native mission flag declaration");
        return {0x80,0x01,0x02,24,0};
    }
    if(outputs>32 || inputs.size()>32 || opcode>=0x8000 ||
       (text && (text->empty() || text->size()>8 || text_position>inputs.size() || text->find('\0')!=std::string::npos)) ||
       (replacement_text && (!text || replacement_text->empty() || replacement_text->size()>8 || replacement_text->find('\0')!=std::string::npos)))
        throw std::runtime_error("Invalid native command packet");
    std::vector<uint8_t> bytes{uint8_t(opcode),uint8_t(opcode>>8)};
    // SA type 1 stores all four parameter bits. SA's collectors preserve the
    // union bits, including floats; type 6 in LCS has an entirely different meaning.
    for(size_t n=0;n<=inputs.size();++n) {
        if(text && n==text_position) {
            bytes.push_back(9);
            for(size_t i=0;i<8;++i)bytes.push_back(i<text->size() ? uint8_t((*text)[i]) : 0);
            if(replacement_text) {
                bytes.push_back(9);
                for(size_t i=0;i<8;++i)bytes.push_back(i<replacement_text->size() ? uint8_t((*replacement_text)[i]) : 0);
            }
        }
        if(n<inputs.size()) {const auto value=inputs[n];bytes.push_back(1);for(int i=0;i<4;++i)bytes.push_back(uint8_t(uint32_t(value)>>(8*i)));}
    }
    for(size_t i=0;i<outputs;++i) { bytes.push_back(3); bytes.push_back(uint8_t(i)); bytes.push_back(0); }
    return bytes;
}
bool NativeAdapter::refresh_frenzy_alias() {
    if(!frenzy_alias_)return true;
    if(!execute)return false;
    const auto status=execute({0x01FA,{},{},0,1});
    if(!status.executed || status.values[0]<0 || status.values[0]>3)return false;
    if(status.values[0]==1)return true;
    const auto actor=equivalent_weapons_.find(frenzy_alias_->actor);
    if(actor!=equivalent_weapons_.end()) {
        const auto alias=actor->second.find(frenzy_alias_->weapon);
        // Preserve a later explicit weapon assignment by another source command.
        if(alias!=actor->second.end() && alias->second==frenzy_alias_->original) {
            if(frenzy_alias_->previous)alias->second=*frenzy_alias_->previous;
            else actor->second.erase(alias);
        }
    }
    frenzy_alias_.reset();return true;
}
void NativeAdapter::bind_model(int32_t lcs_id,int32_t sa_id,const std::string& name,ModelKind kind) {
    if(lcs_id<0 || lcs_id>=native_vehicle_model_base || sa_id<0 || sa_id>=20000 || name.empty()) throw std::runtime_error("Invalid model binding");
    const auto key=lower(name);
    const auto old=models_.find(lcs_id); const auto named=named_models_.find(key);
    if((old!=models_.end() && (old->second.id!=sa_id || old->second.kind!=kind)) ||
       (named!=named_models_.end() && (named->second.id!=sa_id || named->second.kind!=kind)))throw std::runtime_error("Conflicting model binding: original="+std::to_string(lcs_id)+" name="+name+" native="+std::to_string(sa_id));
    models_[lcs_id]={sa_id,kind}; named_models_[key]={sa_id,kind};
}
void NativeAdapter::bind_named_model(int32_t sa_id,const std::string& name,ModelKind kind) {
    if(sa_id<0 || sa_id>=20000 || name.empty())throw std::runtime_error("Invalid named model binding");
    const auto key=lower(name);const auto old=named_models_.find(key);
    if(old!=named_models_.end() && (old->second.id!=sa_id || old->second.kind!=kind))throw std::runtime_error("Conflicting named model binding: name="+name+" native="+std::to_string(sa_id)+" existing="+std::to_string(old->second.id)+" kind="+std::to_string(int(kind))+" existingKind="+std::to_string(int(old->second.kind)));
    named_models_[key]={sa_id,kind};
}
std::optional<int32_t> NativeAdapter::original_vehicle_model(int32_t handle) {
    if(!execute)return std::nullopt;
    NativeCall call;call.opcode=0x0441;call.inputs={handle};call.outputs=1;
    const auto result=execute(call);
    if(!result.executed)return std::nullopt;
    const auto saved=original_vehicle_models_.find(handle);
    if(saved!=original_vehicle_models_.end()) {
        const auto expected=binding(saved->second);
        if(expected && expected->id==result.values[0])return saved->second;
    }
    int32_t original=-1;
    for(const auto& entity:models_)if(entity.second.kind==ModelKind::vehicle && entity.second.id==result.values[0] && (original<0 || entity.first<original))original=entity.first;
    if(original<0)return std::nullopt;
    return original;
}
std::optional<NativeAdapter::Binding> NativeAdapter::binding(int32_t id) const {
    if(id<0) {
        const auto index=-int64_t(id);
        if(index>=int64_t(image_.objects.size())) return {};
        auto found=named_models_.find(lower(image_.objects[size_t(index)]));
        if(found==named_models_.end()) return {};
        return found->second;
    }
    auto found=models_.find(id); if(found==models_.end()) return {}; return found->second;
}
std::optional<int32_t> NativeAdapter::model(int32_t id)const {const auto found=binding(id);return found ? std::optional<int32_t>(found->id) : std::nullopt;}
void NativeAdapter::bind_cutscene(const std::string& name,const std::string& native_name) {
    auto valid=[](const std::string& label){return !label.empty() && label.size()<=8 && std::all_of(label.begin(),label.end(),[](unsigned char c){return std::isalnum(c) || c=='_';});};
    const auto alias=native_name.empty()?name:native_name;
    if(!valid(name) || !valid(alias))
        throw std::runtime_error("Invalid native cutscene name");
    const auto existing=cutscenes_.find(lower(name));
    if(existing!=cutscenes_.end() && existing->second!=lower(alias))throw std::runtime_error("Conflicting native cutscene alias");
    for(const auto& entry:cutscenes_)if(entry.first!=lower(name) && entry.second==lower(alias))throw std::runtime_error("Native cutscene alias already assigned");
    cutscenes_[lower(name)]=lower(alias);
}
bool NativeAdapter::has_cutscene(const std::string& name)const {return cutscenes_.count(lower(name))!=0;}
void NativeAdapter::load_original_zones(std::istream& input) {
    if(!zones_.empty())throw std::runtime_error("Original zones already loaded");
    std::string line;bool section=false;std::vector<OriginalZone> loaded;
    while(std::getline(input,line)) {
        const auto first=line.find_first_not_of(" \t\r");if(first==std::string::npos || line[first]=='#')continue;
        const auto trimmed=line.substr(first,line.find_last_not_of(" \t\r")-first+1);
        if(trimmed=="zone"){section=true;continue;}if(trimmed=="end"){section=false;continue;}if(!section)throw std::runtime_error("Unexpected original zone section");
        std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);OriginalZone zone;
        if(!(row>>zone.name>>zone.type))throw std::runtime_error("Invalid original zone row");
        for(auto& v:zone.bounds)if(!(row>>v) || !std::isfinite(v))throw std::runtime_error("Invalid original zone bounds");
        if(!(row>>zone.level) || zone.name.empty() || zone.name.size()>8 || zone.type<0 || zone.type>3 || zone.level<0 || zone.level>4)throw std::runtime_error("Invalid original zone identity/level");
        zone.name=lower(zone.name);
        for(int i=0;i<3;++i)if(zone.bounds[i]>zone.bounds[i+3])std::swap(zone.bounds[i],zone.bounds[i+3]);
        loaded.push_back(zone);
    }
    if(input.bad() || section || loaded.empty())throw std::runtime_error("Incomplete original zones");
    zones_=std::move(loaded);
}
void NativeAdapter::load_phone_placements(std::istream& input,int32_t native_phone_model) {
    if(claimed_phones_)throw std::runtime_error("Cannot replace claimed world phones");
    std::vector<Phone> added;std::string line;bool instances=false;
    while(std::getline(input,line)) {
        const auto first=line.find_first_not_of(" \t\r");
        if(first==std::string::npos || line[first]=='#')continue;
        const auto trimmed=line.substr(first,line.find_last_not_of(" \t\r")-first+1);
        if(trimmed=="inst"){instances=true;continue;}
        if(trimmed=="end"){instances=false;continue;}
        if(!instances)continue;
        std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);
        int32_t id=0,interior=0;std::string name;Phone phone{};
        if(!(row>>id))throw std::runtime_error("Invalid mounted IPL model index");
        if(id!=native_phone_model)continue;
        if(!(row>>name>>interior>>phone.position[0]>>phone.position[1]>>phone.position[2]) ||
           interior!=0 || !std::all_of(phone.position.begin(),phone.position.end(),[](float v){return std::isfinite(v);}))
            throw std::runtime_error("Invalid mounted LCS phone position");
        added.push_back(phone);
    }
    if(input.bad() || phones_.size()+added.size()>60)throw std::runtime_error("Mounted LCS phone pool exceeds original capacity");
    phones_.insert(phones_.end(),added.begin(),added.end());
}
void NativeAdapter::bind_garage(int32_t original,const std::string& name) {
    if(original<0 || name.empty() || name.size()>8 ||
       !std::all_of(name.begin(),name.end(),[](unsigned char c){return std::isalnum(c) || c=='_';}))
        throw std::runtime_error("Invalid native garage binding");
    auto normalized=name;
    for(auto& c:normalized)if(c>='a' && c<='z')c-=32;
    const auto existing=garages_.find(original);
    if(existing!=garages_.end() && existing->second!=normalized)throw std::runtime_error("Conflicting garage binding");
    for(const auto& binding:garages_)if(binding.first!=original && binding.second==normalized)
        throw std::runtime_error("Native garage already bound to another LCS handle");
    garages_[original]=normalized;
}
void NativeAdapter::load_crane_placements(std::istream& input,int32_t native_crusher_model) {
    if(active_crane_declarations())throw std::runtime_error("Cannot replace configured LCS cranes");
    std::vector<CraneDeclaration> added;std::string line;bool instances=false;
    while(std::getline(input,line)) {
        const auto first=line.find_first_not_of(" \t\r");if(first==std::string::npos || line[first]=='#')continue;
        const auto trimmed=line.substr(first,line.find_last_not_of(" \t\r")-first+1);
        if(trimmed=="inst"){instances=true;continue;}if(trimmed=="end"){instances=false;continue;}if(!instances)continue;
        std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);int32_t id=0,interior=0;std::string name;CraneDeclaration crane{};
        if(!(row>>id))throw std::runtime_error("Invalid mounted IPL model index");if(id!=native_crusher_model)continue;
        if(!(row>>name>>interior>>crane.origin[0]>>crane.origin[1]>>crane.origin[2]) || interior!=0 ||
           !std::all_of(crane.origin.begin(),crane.origin.end(),[](float v){return std::isfinite(v);}))throw std::runtime_error("Invalid mounted crane position");
        crane.model=id;added.push_back(crane);
    }
    if(input.bad() || cranes_.size()+added.size()>11)throw std::runtime_error("Mounted LCS crane pool exceeds original capacity");
    cranes_.insert(cranes_.end(),added.begin(),added.end());
    for(const auto& crane:added)crane_service.declare(crane.origin);
}
size_t NativeAdapter::active_crane_declarations()const {
    return size_t(std::count_if(cranes_.begin(),cranes_.end(),[](const CraneDeclaration& crane){return crane.activated;}));
}
std::optional<uint8_t> NativeAdapter::declared_garage_capacity(int32_t id)const {
    const auto found=garage_definitions_.find(id);
    return found==garage_definitions_.end()?std::nullopt:std::optional<uint8_t>(found->second.max_stored_cars);
}
std::optional<bool> NativeAdapter::declared_garage_player_camera(int32_t id)const {
    const auto found=garage_definitions_.find(id);
    return found==garage_definitions_.end()?std::nullopt:std::optional<bool>(found->second.camera_follows_player);
}
struct Spec { uint16_t lcs,sa; uint8_t inputs,outputs; int8_t model_index; bool predicate,world; int8_t text_index=-1; bool player_ped=false; int8_t weapon_index=-1; int8_t second_model_index=-1; int8_t garage_index=-1; };
// Only individually checked equivalent operations belong here. Stateful script
// controls (death/arrest, text banks, cleanup, saves) need persistent LCS adapters.
static constexpr Spec specs[] = {
#include "native_common.inc"
    {COMMAND_ADD_BLIP_FOR_COORD_OLD,0x0167,5,1,-1,false,true},
    {COMMAND_START_SCRIPT_FIRE,0x02CF,3,1,-1,false,true},
    {COMMAND_CREATE_PROTECTION_PICKUP,0x04A6,5,1,-1,false,true},
    {COMMAND_GET_CLOSEST_STRAIGHT_ROAD,0x04B9,5,7,-1,false,true},
    {COMMAND_RESET_NUM_OF_MODELS_KILLED_BY_PLAYER,0x0297,0,0,-1,false,true},
    {COMMAND_GET_NUM_OF_MODELS_KILLED_BY_PLAYER,0x0298,1,1,0,false,true},
    {COMMAND_MISSION_HAS_FINISHED,0x00D8,0,0,-1,false,true},
    {COMMAND_PRINT_WITH_NUMBER_BIG,0x01E3,3,0,-1,false,false,0},
    {COMMAND_PRINT_WITH_2_NUMBERS_BIG,0x036D,4,0,-1,false,false,0},
    {COMMAND_PRINT_WITH_NUMBER,0x01E4,3,0,-1,false,false,0},
    {COMMAND_PRINT_WITH_3_NUMBERS,0x02FF,5,0,-1,false,false,0},
    {COMMAND_PRINT_WITH_4_NUMBERS,0x0302,6,0,-1,false,false,0},
    {COMMAND_PRINT_WITH_6_NUMBERS,0x0308,8,0,-1,false,false,0},
    {COMMAND_PRINT_WITH_NUMBER_NOW,0x01E5,3,0,-1,false,false,0},
    {COMMAND_SET_CAR_CHANGE_LANE,0x0466,2,0,-1,false,true},
    {COMMAND_CLEAR_THIS_BIG_PRINT,0x03D6,0,0,-1,false,false,0},
    {COMMAND_PRINT_HELP_FOREVER,0x0512,0,0,-1,false,false,0},
    {COMMAND_PRINT_HELP_FOREVER_NO_BRIEF,0x0512,0,0,-1,false,false,0},
    {COMMAND_PRINT_HELP_ALWAYS_NO_BRIEF,0x03E5,0,0,-1,false,false,0},
    {COMMAND_PRINT_HELP_FOREVER_ALWAYS_NO_BRIEF,0x0512,0,0,-1,false,false,0},
    {COMMAND_PRINT_BIG_Q,0x0217,2,0,-1,false,false,0},
    {COMMAND_ADD_SCORE,0x0109,2,0,-1,false,true},
    {COMMAND_PLAYER_MADE_PROGRESS,0x030C,1,0,-1,false,false},
    {COMMAND_PRINT_SUBTITLE_NOW,0x00BC,2,0,-1,false,false,0},
    {COMMAND_APPLY_BRAKES_TO_PLAYERS_CAR,0x0221,2,0,-1,false,true},
    {COMMAND_IS_PLAYER_STILL_ALIVE,0x0256,1,0,-1,true,true},
    {COMMAND_TURN_PLAYER_TO_FACE_CHAR,0x0639,2,0,-1,false,true,-1,true},
    {COMMAND_TURN_CHAR_TO_FACE_CHAR,0x0639,2,0,-1,false,true},
    {COMMAND_TURN_CHAR_TO_FACE_PLAYER,0x0639,2,0,-1,false,true},
    {COMMAND_TURN_CHAR_TO_FACE_COORD,0x06BA,4,0,-1,false,true},
    {COMMAND_TURN_PLAYER_TO_FACE_COORD,0x06BA,4,0,-1,false,true,-1,true},
    {COMMAND_STOP_PLAYER_LOOKING,0x0647,1,0,-1,false,true,-1,true},
    {COMMAND_CLEAR_ALL_CHAR_ANIMS,0x0687,1,0,-1,false,true},
    {COMMAND_SWITCH_ROADS_ON,0x01E7,6,0,-1,false,true},
    {COMMAND_SWITCH_ROADS_OFF,0x01E8,6,0,-1,false,true},
    {COMMAND_SWITCH_PED_ROADS_ON,0x022A,6,0,-1,false,true},
    {COMMAND_SWITCH_PED_ROADS_OFF,0x022B,6,0,-1,false,true},
    {COMMAND_SWITCH_WORLD_PROCESSING,0x03B7,1,0,-1,false,true},
    {COMMAND_SWITCH_STREAMING,0x03AF,1,0,-1,false,true},
    {COMMAND_FORCE_WEATHER,0x01B5,1,0,-1,false,false},
    {COMMAND_FORCE_WEATHER_NOW,0x01B6,1,0,-1,false,false},
    {COMMAND_RELEASE_WEATHER,0x01B7,0,0,-1,false,false},
    {COMMAND_SET_CAR_STATUS,0x03A2,2,0,-1,false,true},
    {COMMAND_IS_GERMAN_GAME,0x040C,0,0,-1,true,false},
    {COMMAND_ARE_ANY_CAR_CHEATS_ACTIVATED,0x0445,0,0,-1,true,false},
    {COMMAND_READ_KILL_FRENZY_STATUS,0x01FA,0,1,-1,false,false},
    {COMMAND_IS_PLAYER_ON_ANY_BIKE,0x047A,1,0,-1,true,true,-1,true},
    {COMMAND_IS_PLAYER_IN_FLYING_VEHICLE,0x04C8,1,0,-1,true,true,-1,true},
    {COMMAND_IS_PLAYER_IN_ANY_HELI,0x04A9,1,0,-1,true,true,-1,true},
    {COMMAND_IS_PLAYER_IN_ANY_PLANE,0x04AB,1,0,-1,true,true,-1,true},
    {COMMAND_IS_PLAYER_IN_ANY_BOAT,0x04A7,1,0,-1,true,true,-1,true},
    {COMMAND_ADD_SHORT_RANGE_SPRITE_BLIP_FOR_COORD,0x04CE,4,1,-1,false,true},
    {COMMAND_ADD_SPRITE_BLIP_FOR_COORD,0x02A8,4,1,-1,false,true},
    {COMMAND_ADD_SPRITE_BLIP_FOR_CONTACT_POINT,0x02A7,4,1,-1,false,true},
    {COMMAND_ADD_SHORT_RANGE_SPRITE_BLIP_FOR_CONTACT_POINT,0x0570,4,1,-1,false,true},
    {COMMAND_GET_RANDOM_CAR_OF_TYPE_IN_AREA,0x0327,5,1,4,false,true},
    {COMMAND_GET_CAR_MODEL,0x0441,1,1,-1,false,true},
    {COMMAND_STORE_WANTED_LEVEL,0x01C0,1,1,-1,false,true},
    {COMMAND_GET_CONTROLLER_MODE,0x0293,0,1,-1,false,false},
    {COMMAND_GET_DRIVER_OF_CAR,0x046C,1,1,-1,false,true},
    {COMMAND_SET_EXTRA_COLOURS,0x04F9,2,0,-1,false,true},
    {COMMAND_CLEAR_EXTRA_COLOURS,0x04FA,1,0,-1,false,true},
    {COMMAND_ADD_HOSPITAL_RESTART,0x016C,4,0,-1,false,true},
    {COMMAND_ADD_POLICE_RESTART,0x016D,4,0,-1,false,true},
    {COMMAND_OVERRIDE_NEXT_RESTART,0x016E,4,0,-1,false,true},
    {COMMAND_GET_WHEELIE_STATS,0x04FC,1,6,-1,false,true},
    {COMMAND_GET_RADIO_CHANNEL,0x051E,0,1,-1,false,true},
    {COMMAND_PLAY_MISSION_PASSED_TUNE,0x0394,1,0,-1,false,false},
    {COMMAND_FLASH_HUD_OBJECT,0x03E7,1,0,-1,false,true},
    {COMMAND_IS_CAR_UPSIDEDOWN,0x01F4,1,0,-1,true,true},
    {COMMAND_CAN_PLAYER_START_MISSION,0x03EE,1,0,-1,true,true},
    {COMMAND_CREATE_PICKUP,0x0213,5,1,0,false,true},
    {COMMAND_HAS_PICKUP_BEEN_COLLECTED,0x0214,1,0,-1,true,true},
    {COMMAND_REMOVE_PICKUP,0x0215,1,0,-1,false,true},
    {COMMAND_LOCATE_CHAR_ANY_MEANS_2D,0x00EC,6,0,-1,true,true},
    {COMMAND_POINT_CAMERA_AT_PLAYER,0x0159,3,0,-1,false,true,-1,true},
    {COMMAND_PRINT_HELP_ALWAYS,0x03E5,0,0,-1,false,false,0},
    {COMMAND_PRINT_HELP_FOREVER_ALWAYS,0x0512,0,0,-1,false,false,0},
    {COMMAND_CREATE_CHAR_AS_PASSENGER,0x01C8,4,1,2,false,true},
    {COMMAND_CREATE_CHAR_INSIDE_CAR,0x0129,3,1,2,false,true},
    {COMMAND_LOCATE_CAR_2D,0x01AD,6,0,-1,true,true},
    {COMMAND_LOCATE_STOPPED_CAR_2D,0x01AE,6,0,-1,true,true},
    {COMMAND_LOCATE_CAR_3D,0x01AF,8,0,-1,true,true},
    {COMMAND_IS_CAR_STOPPED,0x01C1,1,0,-1,true,true},
    {COMMAND_IS_CAR_IN_AREA_2D,0x00B0,6,0,-1,true,true},
    {COMMAND_IS_CAR_IN_AREA_3D,0x00B1,8,0,-1,true,true},
    {COMMAND_CAR_GOTO_COORDINATES,0x00A7,4,0,-1,false,true},
    {COMMAND_CAR_GOTO_COORDINATES_ACCURATE,0x02C2,4,0,-1,false,true},
    {COMMAND_SET_CAR_MISSION,0x00AF,2,0,-1,false,true},
    {COMMAND_SET_CAR_TEMP_ACTION,0x0477,3,0,-1,false,true},
    {COMMAND_SET_CAR_FORWARD_SPEED,0x04BA,2,0,-1,false,true},
    {COMMAND_SET_CAR_STRAIGHT_LINE_DISTANCE,0x04E0,2,0,-1,false,true},
    {COMMAND_GET_CAR_HEADING,0x0174,1,1,-1,false,true},
    {COMMAND_GET_CHAR_HEADING,0x0172,1,1,-1,false,true},
    {COMMAND_OPEN_GARAGE,0x0360,1,0,-1,false,true,-1,false,-1,-1,0},
    {COMMAND_CLOSE_GARAGE,0x0361,1,0,-1,false,true,-1,false,-1,-1,0},
    {COMMAND_ACTIVATE_GARAGE,0x0299,1,0,-1,false,true,-1,false,-1,-1,0},
    {COMMAND_DEACTIVATE_GARAGE,0x02B9,1,0,-1,false,true,-1,false,-1,-1,0},
    {COMMAND_CHANGE_GARAGE_TYPE,0x02FA,2,0,-1,false,true,-1,false,-1,-1,0},
    {COMMAND_IS_GARAGE_OPEN,0x03B0,1,0,-1,true,true,-1,false,-1,-1,0},
    {COMMAND_IS_GARAGE_CLOSED,0x03B1,1,0,-1,true,true,-1,false,-1,-1,0},
    {COMMAND_SET_TARGET_CAR_FOR_MISSION_GARAGE,0x021B,2,0,-1,false,true,-1,false,-1,-1,0},
    {COMMAND_CHAR_LOOK_AT_CHAR_ALWAYS,0x05BF,2,0,-1,false,true},
    {COMMAND_CHAR_LOOK_AT_PLAYER_ALWAYS,0x05BF,2,0,-1,false,true},
    {COMMAND_PLAYER_LOOK_AT_CHAR_ALWAYS,0x05BF,2,0,-1,false,true,-1,true},
    {COMMAND_STOP_CHAR_LOOKING,0x0647,1,0,-1,false,true},
    {COMMAND_GET_PAD_BUTTON_STATE,0x00E2,1,1,-1,false,true},
    {COMMAND_SET_TOTAL_NUMBER_OF_MISSIONS,0x042C,1,0,-1,false,false},
    {COMMAND_SET_PROGRESS_TOTAL,0x030D,1,0,-1,false,false},
    {COMMAND_REGISTER_MISSION_GIVEN,0x0317,0,0,-1,false,false},
    {COMMAND_GET_TIME_OF_DAY,0x00BF,0,2,-1,false,false},
    {COMMAND_SET_TIME_OF_DAY,0x00C0,2,0,-1,false,false},
    {COMMAND_GET_GAME_TIMER,0x01BD,0,1,-1,false,false},
    {COMMAND_DO_FADE,0x016A,2,0,-1,false,false},
    {COMMAND_SWITCH_WIDESCREEN,0x02A3,1,0,-1,false,false},
    {COMMAND_SET_MAX_WANTED_LEVEL,0x01F0,1,0,-1,false,false},
    {COMMAND_SET_DRAW_HUD,0x0826,1,0,-1,false,false},
    {COMMAND_DISPLAY_RADAR,0x0581,1,0,-1,false,false},
    {COMMAND_SET_CAR_DENSITY_MULTIPLIER,0x01EB,1,0,-1,false,true},
    {COMMAND_SET_PED_DENSITY_MULTIPLIER,0x03DE,1,0,-1,false,true},
    {COMMAND_RESTORE_CAMERA,0x015A,0,0,-1,false,true},
    {COMMAND_RESTORE_CAMERA_JUMPCUT,0x02EB,0,0,-1,false,true},
    {COMMAND_SET_CAMERA_BEHIND_PLAYER,0x0373,0,0,-1,false,true},
    {COMMAND_SET_FADING_COLOUR,0x0169,3,0,-1,false,true},
    {COMMAND_CLEAR_PRINTS,0x00BE,0,0,-1,false,false},
    {COMMAND_USE_TEXT_COMMANDS,0x03F0,1,0,-1,false,false},
    {COMMAND_LOAD_MISSION_TEXT,0x054C,0,0,-1,false,false,0},
    {COMMAND_LOAD_CUTSCENE,0x02E4,0,0,-1,false,true,0},
    {COMMAND_START_CUTSCENE,0x02E7,0,0,-1,false,true},
    {COMMAND_GET_CUTSCENE_TIME,0x02E8,0,1,-1,false,true},
    {COMMAND_HAS_CUTSCENE_FINISHED,0x02E9,0,0,-1,true,true},
    {COMMAND_CLEAR_CUTSCENE,0x02EA,0,0,-1,false,true},
    {COMMAND_HAS_CUTSCENE_LOADED,0x06B9,0,0,-1,true,true},
    {COMMAND_WAS_CUTSCENE_SKIPPED,0x056A,0,0,-1,true,true},
    {COMMAND_PRINT_BIG,0x00BA,2,0,-1,false,false,0},
    {COMMAND_PRINT,0x00BB,2,0,-1,false,false,0},
    {COMMAND_PRINT_NOW,0x00BC,2,0,-1,false,false,0},
    {COMMAND_PRINT_WITH_2_NUMBERS_NOW,0x02FD,4,0,-1,false,false,0},
    {COMMAND_PRINT_HELP,0x03E5,0,0,-1,false,false,0},
    {COMMAND_CLEAR_HELP,0x03E6,0,0,-1,false,false},
    {COMMAND_CLEAR_EXTRA_COLOURS,0x04FA,1,0,-1,false,true},
    {COMMAND_CLEAR_THIS_PRINT,0x03D5,0,0,-1,false,false,0},
    {COMMAND_CLEAR_SMALL_PRINTS,0x03EB,0,0,-1,false,false},
    {COMMAND_DISPLAY_TEXT,0x033E,2,0,-1,false,false,2},
    {COMMAND_SET_TEXT_SCALE,0x033F,2,0,-1,false,false},
    {COMMAND_SET_TEXT_COLOUR,0x0340,4,0,-1,false,false},
    {COMMAND_SET_TEXT_JUSTIFY,0x0341,1,0,-1,false,false},
    {COMMAND_SET_TEXT_CENTRE,0x0342,1,0,-1,false,false},
    {COMMAND_SET_TEXT_WRAPX,0x0343,1,0,-1,false,false},
    {COMMAND_SET_TEXT_CENTRE_SIZE,0x0344,1,0,-1,false,false},
    {COMMAND_SET_TEXT_BACKGROUND,0x0345,1,0,-1,false,false},
    {COMMAND_SET_TEXT_PROPORTIONAL,0x0348,1,0,-1,false,false},
    {COMMAND_SET_TEXT_FONT,0x0349,1,0,-1,false,false},
    {COMMAND_REQUEST_MODEL,0x0247,1,0,0,false,true},
    {COMMAND_HAS_MODEL_LOADED,0x0248,1,0,0,true,true},
    {COMMAND_MARK_MODEL_AS_NO_LONGER_NEEDED,0x0249,1,0,0,false,true},
    {COMMAND_LOAD_ALL_MODELS_NOW,0x038B,0,0,-1,false,true},
    {COMMAND_LOAD_SPECIAL_CHARACTER,0x023C,1,0,-1,false,true,1},
    {COMMAND_HAS_SPECIAL_CHARACTER_LOADED,0x023D,1,0,-1,true,true},
    {COMMAND_UNLOAD_SPECIAL_CHARACTER,0x0296,1,0,-1,false,true},
    {COMMAND_IS_PLAYER_PLAYING,0x0256,1,0,-1,true,true},
    {COMMAND_GET_PLAYER_CHAR,0x01F5,1,1,-1,false,true},
    {COMMAND_IS_PLAYER_DEAD,0x0117,1,0,-1,true,true},
    {COMMAND_IS_PLAYER_ON_FOOT,0x044B,1,0,-1,true,true,-1,true},
    {COMMAND_IS_PLAYER_SHOOTING,0x02E0,1,0,-1,true,true,-1,true},
    {COMMAND_IS_PLAYER_TARGETTING_CHAR,0x0457,2,0,-1,true,true},
    {COMMAND_IS_PLAYER_IN_AREA_2D,0x00A3,6,0,-1,true,true,-1,true},
    {COMMAND_IS_PLAYER_IN_AREA_3D,0x00A4,8,0,-1,true,true,-1,true},
    {COMMAND_IS_WANTED_LEVEL_GREATER,0x010F,2,0,-1,true,true},
    {COMMAND_GET_PLAYER_HEADING,0x0172,1,1,-1,false,true,-1,true},
    {COMMAND_STORE_CAR_PLAYER_IS_IN_NO_SAVE,0x03C0,1,1,-1,false,true,-1,true},
    {COMMAND_IS_CURRENT_PLAYER_WEAPON,0x02D8,2,0,-1,true,true,-1,true,1},
    {COMMAND_GET_CURRENT_PLAYER_WEAPON,0x0470,1,1,-1,false,true,-1,true},
    {COMMAND_GET_CURRENT_CHAR_WEAPON,0x0470,1,1,-1,false,true},
    {COMMAND_IS_CAR_MODEL,0x0137,2,0,1,true,true},
    {COMMAND_GET_PROGRESS_PERCENTAGE,0x058C,0,1,-1,false,false},
    // These two legacy player opcodes are unsupported in original SA. Resolve
    // the player's native ped, then use the supported character operation.
    {COMMAND_GET_PLAYER_COORDINATES,0x0000,1,3,-1,false,true},
    {COMMAND_SET_PLAYER_COORDINATES,0x0000,4,0,-1,false,true},
    {COMMAND_CREATE_PLAYER,0x0053,4,1,-1,false,true},
    {COMMAND_SET_PLAYER_CONTROL,0x01B4,2,0,-1,false,true},
    {COMMAND_IS_SCORE_GREATER,0x010A,2,0,-1,true,true},
    {COMMAND_STORE_SCORE,0x010B,1,1,-1,false,true},
    {COMMAND_ALTER_WANTED_LEVEL,0x010D,2,0,-1,false,true},
    {COMMAND_ALTER_WANTED_LEVEL_NO_DROP,0x010E,2,0,-1,false,true},
    {COMMAND_IS_PLAYER_PRESSING_HORN,0x0122,1,0,-1,true,true},
    {COMMAND_ADD_ARMOUR_TO_PLAYER,0x035F,2,0,-1,false,true,-1,true},
    {COMMAND_IS_PLAYER_HEALTH_GREATER,0x0184,2,0,-1,true,true,-1,true},
    {COMMAND_GET_AMMO_IN_PLAYER_WEAPON,0x041A,2,1,-1,false,true,-1,true,1},
    {COMMAND_SET_PLAYER_AMMO,0x017B,3,0,-1,false,true,-1,true,1},
    {COMMAND_SET_CHAR_AMMO,0x017B,3,0,-1,false,true,-1,false,1},
    {COMMAND_ADD_AMMO_TO_PLAYER,0x0114,3,0,-1,false,true,-1,true,1},
    {COMMAND_ADD_AMMO_TO_CHAR,0x0114,3,0,-1,false,true,-1,false,1},
    {COMMAND_IS_CURRENT_CHAR_WEAPON,0x02D8,2,0,-1,true,true,-1,false,1},
    {COMMAND_HAS_CHAR_SPOTTED_PLAYER,0x0364,2,0,-1,true,true},
    {COMMAND_IS_PLAYER_TOUCHING_OBJECT,0x0179,2,0,-1,true,true,-1,true},
    {COMMAND_IS_PLAYER_TOUCHING_OBJECT_ON_FOOT,0x023B,2,0,-1,true,true,-1,true},
    {COMMAND_IS_PLAYER_TOUCHING_VEHICLE,0x0547,2,0,-1,true,true,-1,true},
    {COMMAND_IS_CHAR_MODEL,0x02F2,2,0,1,true,true},
    {COMMAND_HAS_CHAR_BEEN_DAMAGED_BY_WEAPON,0x031D,2,0,-1,true,true,-1,false,1},
    {COMMAND_HAS_CAR_BEEN_DAMAGED_BY_WEAPON,0x031E,2,0,-1,true,true,-1,false,1},
    {COMMAND_HAS_OBJECT_BEEN_DAMAGED_BY_WEAPON,0x08FF,2,0,-1,true,true,-1,false,1},
    {COMMAND_SET_CHAR_DROPS_WEAPONS_ON_DEATH,0x087E,2,0,-1,false,true},
    {COMMAND_SET_CHAR_ANSWERING_MOBILE,0x0729,2,0,-1,false,true},
    {COMMAND_SET_PLAYER_CAN_DO_DRIVE_BY,0x0501,2,0,-1,false,true},
    {COMMAND_DRAW_CORONA,0x024F,9,0,-1,false,true},
    {COMMAND_DOES_VEHICLE_EXIST,0x056E,1,0,-1,true,true},
    {COMMAND_IS_CHAR_CROUCHING,0x0597,1,0,-1,true,true},
    {COMMAND_IS_PLAYER_SITTING_IN_CAR,0x0448,2,0,-1,true,true,-1,true},
    {COMMAND_IS_PLAYER_SITTING_IN_ANY_CAR,0x0449,1,0,-1,true,true,-1,true},
    {COMMAND_LOCK_CAR_DOORS,0x020A,2,0,-1,false,true},
    {COMMAND_IS_BUTTON_PRESSED,0x00E1,2,0,-1,true,true},
    {COMMAND_GET_DISTANCE_BETWEEN_2D_POINTS,0x0509,4,1,-1,false,false},
    {COMMAND_GET_DISTANCE_BETWEEN_3D_POINTS,0x050A,6,1,-1,false,false},
    {COMMAND_CLEAR_WANTED_LEVEL,0x0110,1,0,-1,false,true},
    {COMMAND_SET_POLICE_IGNORE_PLAYER,0x01F7,2,0,-1,false,true},
    {COMMAND_SET_WANTED_MULTIPLIER,0x03C7,1,0,-1,false,true},
    {COMMAND_SET_EVERYONE_IGNORE_PLAYER,0x03BF,2,0,-1,false,true},
    {COMMAND_CREATE_CAR_GENERATOR,0x014B,12,1,4,false,true},
    {COMMAND_CREATE_PICKUP_WITH_AMMO,0x032B,6,1,0,false,true},
    {COMMAND_SWITCH_CAR_GENERATOR,0x014C,2,0,-1,false,true},
    {COMMAND_REMOVE_SPHERE,0x03BD,1,0,-1,false,true},
    {COMMAND_SET_PLAYER_NEVER_GETS_TIRED,0x0330,2,0,-1,false,true},
    {COMMAND_SET_CURRENT_CHAR_WEAPON,0x01B9,2,0,-1,false,true,-1,false,1},
    {COMMAND_GIVE_WEAPON_TO_CHAR,0x01B2,3,0,-1,false,true,-1,false,1},
    {COMMAND_GIVE_WEAPON_TO_PLAYER,0x01B2,3,0,-1,false,true,-1,true,1},
    {COMMAND_SET_CHAR_OBJ_KILL_CHAR_ON_FOOT,0x05E2,2,0,-1,false,true},
    {COMMAND_SET_CHAR_OBJ_KILL_PLAYER_ON_FOOT,0x05E2,2,0,-1,false,true},
    // SA's native combat task attacks the actual target, including a target
    // seated in a vehicle. Driving pursuit behavior still needs verification.
    {COMMAND_SET_CHAR_OBJ_KILL_CHAR_ANY_MEANS,0x05E2,2,0,-1,false,true},
    {COMMAND_SET_CHAR_OBJ_KILL_PLAYER_ANY_MEANS,0x05E2,2,0,-1,false,true},
    {COMMAND_SET_CHAR_OBJ_DESTROY_CAR,0x0672,2,0,-1,false,true},
    {COMMAND_SET_CHAR_OBJ_WAIT_ON_FOOT,0x05BA,1,0,-1,false,true},
    {COMMAND_CHAR_SET_IDLE,0x05BA,1,0,-1,false,true},
    {COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_DRIVER,0x05CB,2,0,-1,false,true},
    {COMMAND_SET_CHAR_OBJ_LEAVE_CAR,0x05CD,2,0,-1,false,true},
    {COMMAND_LEAVE_GROUP,0x06C9,1,0,-1,false,true},
    {COMMAND_SET_CHAR_OBJ_GOTO_CHAR_ON_FOOT,0x05D9,2,0,-1,false,true},
    {COMMAND_SET_CHAR_OBJ_WALK_TO_CHAR,0x05D9,2,0,-1,false,true},
    {COMMAND_SET_CHAR_OBJ_GOTO_PLAYER_ON_FOOT,0x05D9,2,0,-1,false,true},
    {COMMAND_SET_CHAR_OBJ_FLEE_CHAR_ON_FOOT_ALWAYS,0x05DB,2,0,-1,false,true},
    {COMMAND_SET_CHAR_OBJ_FLEE_PLAYER_ON_FOOT_ALWAYS,0x05DB,2,0,-1,false,true},
    {COMMAND_SET_CHAR_OBJ_FLEE_ON_FOOT_TILL_SAFE,0x05DA,1,0,-1,false,true},
    {COMMAND_REMOVE_ALL_PLAYER_WEAPONS,0x048F,1,0,-1,false,true,-1,true},
    {COMMAND_SET_CURRENT_PLAYER_WEAPON,0x01B9,2,0,-1,false,true,-1,true,1},
    {COMMAND_SET_PLAYER_HEALTH,0x0223,2,0,-1,false,true,-1,true},
    {COMMAND_SET_PLAYER_HEADING,0x0173,2,0,-1,false,true,-1,true},
    {COMMAND_SET_PLAYER_VISIBLE,0x0337,2,0,-1,false,true,-1,true},
    {COMMAND_SET_CHAR_VISIBLE,0x0337,2,0,-1,false,true},
    {COMMAND_SET_CHAR_PROOFS,0x02AB,6,0,-1,false,true},
    {COMMAND_CREATE_CHAR,0x009A,5,1,1,false,true},
    {COMMAND_WARP_CHAR_INTO_CAR,0x036A,2,0,-1,false,true},
    {COMMAND_WARP_CHAR_INTO_CAR_AS_PASSENGER,0x0430,3,0,-1,false,true},
    {COMMAND_SET_CAR_CRUISE_SPEED,0x00AD,2,0,-1,false,true},
    {COMMAND_SET_CAR_DRIVING_STYLE,0x00AE,2,0,-1,false,true},
    {COMMAND_IS_CHAR_IN_ANY_CAR,0x00DF,1,0,-1,true,true},
    {COMMAND_IS_CHAR_IN_CAR,0x00DB,2,0,-1,true,true},
    {COMMAND_IS_CAR_DEAD,0x0119,1,0,-1,true,true},
    {COMMAND_IS_CHAR_IN_MODEL,0x00DD,2,0,1,true,true},
    {COMMAND_REMOVE_BLIP,0x0164,1,0,-1,false,true},
    {COMMAND_REGISTER_MISSION_PASSED,0x0318,0,0,-1,false,false,0},
    {COMMAND_ADD_BLIP_FOR_CAR,0x0186,1,1,-1,false,true},
    {COMMAND_ADD_BLIP_FOR_CHAR,0x0187,1,1,-1,false,true},
    {COMMAND_ADD_BLIP_FOR_COORD,0x018A,3,1,-1,false,true},
    {COMMAND_CHANGE_BLIP_COLOUR,0x0165,2,0,-1,false,true},
    {COMMAND_CHANGE_BLIP_DISPLAY,0x018B,2,0,-1,false,true},
    {COMMAND_ADD_SPHERE,0x03BC,4,1,-1,false,true},
    {COMMAND_GET_FADING_STATUS,0x016B,0,0,-1,true,true},
    {COMMAND_SET_FIXED_CAMERA_POSITION,0x015F,6,0,-1,false,true},
    {COMMAND_SET_AREA_VISIBLE,0x04BB,1,0,-1,false,true},
    {COMMAND_SET_NEAR_CLIP,0x041D,1,0,-1,false,true},
    {COMMAND_SET_INTERPOLATION_PARAMETERS,0x0460,2,0,-1,false,true},
    {COMMAND_SET_VISIBILITY_OF_CLOSEST_OBJECT_OF_TYPE,0x0363,6,0,4,false,true},
    {COMMAND_SWAP_NEAREST_BUILDING_MODEL,0x03B6,6,0,4,false,true,-1,false,-1,5},
    {COMMAND_LOAD_SCENE,0x03CB,3,0,-1,false,true},
    {COMMAND_CLEAR_AREA,0x0395,5,0,-1,false,true},
    {COMMAND_SET_CAR_PROOFS,0x02AC,6,0,-1,false,true},
    {COMMAND_CHANGE_CAR_COLOUR,0x0229,3,0,-1,false,true},
    {COMMAND_DOES_VEHICLE_EXIST,0x056E,1,0,-1,true,true},
    {COMMAND_IS_CAR_IN_WATER,0x02BF,1,0,-1,true,true},
    {COMMAND_SET_CHAR_STAY_IN_CAR_WHEN_JACKED,0x0526,2,0,-1,false,true},
    {COMMAND_SET_CHAR_OBJ_LEAVE_ANY_CAR,0x0633,1,0,-1,false,true},
    {COMMAND_SET_CHAR_OBJ_NO_OBJ,0x0687,1,0,-1,false,true},
    // Original LCS has fewer operands than these SA tasks. The composite
    // adapters below resolve ground Z and supply movement/timeout/seat policy.
    {COMMAND_SET_CHAR_OBJ_GOTO_COORD_ON_FOOT,0x05D3,3,0,-1,false,true},
    {COMMAND_SET_CHAR_OBJ_RUN_TO_COORD,0x05D3,3,0,-1,false,true},
    {COMMAND_SET_CHAR_OBJ_SPRINT_TO_COORD,0x05D3,3,0,-1,false,true},
    {COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_PASSENGER,0x05CA,2,0,-1,false,true},
    {COMMAND_IS_PLAYER_IN_ANY_CAR,0x00DF,1,0,-1,true,true,-1,true},
    {COMMAND_IS_PLAYER_IN_CAR,0x00DB,2,0,-1,true,true,-1,true},
    {COMMAND_LOCATE_PLAYER_ANY_MEANS_CHAR_2D,0x00F2,5,0,-1,true,true,-1,true},
    {COMMAND_LOCATE_PLAYER_ON_FOOT_CHAR_2D,0x00F3,5,0,-1,true,true,-1,true},
    {COMMAND_LOCATE_PLAYER_IN_CAR_CHAR_2D,0x00F4,5,0,-1,true,true,-1,true},
    {COMMAND_LOCATE_PLAYER_ANY_MEANS_CHAR_3D,0x0104,6,0,-1,true,true,-1,true},
    {COMMAND_LOCATE_PLAYER_ON_FOOT_CHAR_3D,0x0105,6,0,-1,true,true,-1,true},
    {COMMAND_LOCATE_PLAYER_IN_CAR_CHAR_3D,0x0106,6,0,-1,true,true,-1,true},
    {COMMAND_LOCATE_PLAYER_ANY_MEANS_CAR_2D,0x0202,5,0,-1,true,true,-1,true},
    {COMMAND_LOCATE_PLAYER_ON_FOOT_CAR_2D,0x0203,5,0,-1,true,true,-1,true},
    {COMMAND_LOCATE_PLAYER_IN_CAR_CAR_2D,0x0204,5,0,-1,true,true,-1,true},
    {COMMAND_LOCATE_PLAYER_ANY_MEANS_CAR_3D,0x0205,6,0,-1,true,true,-1,true},
    {COMMAND_LOCATE_PLAYER_ON_FOOT_CAR_3D,0x0206,6,0,-1,true,true,-1,true},
    {COMMAND_LOCATE_PLAYER_IN_CAR_CAR_3D,0x0207,6,0,-1,true,true,-1,true},
    {COMMAND_IS_PLAYER_STOPPED,0x02A0,1,0,-1,true,true,-1,true},
    {COMMAND_WARP_PLAYER_INTO_CAR,0x036A,2,0,-1,false,true,-1,true},
    {COMMAND_HAS_PLAYER_GOT_WEAPON,0x0491,2,0,-1,true,true,-1,true,1},
    {COMMAND_HAS_CHAR_GOT_WEAPON,0x0491,2,0,-1,true,true,-1,false,1},
    {COMMAND_IS_PLAYER_IN_MODEL,0x00DD,2,0,1,true,true,-1,true},
    {COMMAND_STORE_CAR_PLAYER_IS_IN,0x00D9,1,1,-1,false,true,-1,true},
    {COMMAND_STORE_CAR_CHAR_IS_IN,0x00D9,1,1,-1,false,true},
    {COMMAND_LOCATE_PLAYER_ANY_MEANS_2D,0x00EC,6,0,-1,true,true,-1,true},
    {COMMAND_LOCATE_PLAYER_ON_FOOT_2D,0x00ED,6,0,-1,true,true,-1,true},
    {COMMAND_LOCATE_PLAYER_IN_CAR_2D,0x00EE,6,0,-1,true,true,-1,true},
    {COMMAND_LOCATE_PLAYER_ANY_MEANS_3D,0x00FE,8,0,-1,true,true,-1,true},
    {COMMAND_LOCATE_PLAYER_ON_FOOT_3D,0x00FF,8,0,-1,true,true,-1,true},
    {COMMAND_LOCATE_PLAYER_IN_CAR_3D,0x0100,8,0,-1,true,true,-1,true},
    {COMMAND_LOCATE_STOPPED_PLAYER_ANY_MEANS_2D,0x00EF,6,0,-1,true,true,-1,true},
    {COMMAND_LOCATE_STOPPED_PLAYER_ON_FOOT_2D,0x00F0,6,0,-1,true,true,-1,true},
    {COMMAND_LOCATE_STOPPED_PLAYER_IN_CAR_2D,0x00F1,6,0,-1,true,true,-1,true},
    {COMMAND_LOCATE_STOPPED_PLAYER_ANY_MEANS_3D,0x0101,8,0,-1,true,true,-1,true},
    {COMMAND_LOCATE_STOPPED_PLAYER_ON_FOOT_3D,0x0102,8,0,-1,true,true,-1,true},
    {COMMAND_LOCATE_STOPPED_PLAYER_IN_CAR_3D,0x0103,8,0,-1,true,true,-1,true},
    {COMMAND_IS_PLAYER_SHOOTING_IN_AREA,0x02D6,6,0,-1,true,true,-1,true},
    {COMMAND_LOCATE_CHAR_ANY_MEANS_3D,0x00FE,8,0,-1,true,true},
    {COMMAND_LOCATE_CHAR_ON_FOOT_3D,0x00FF,8,0,-1,true,true},
    {COMMAND_LOCATE_CHAR_IN_CAR_3D,0x0100,8,0,-1,true,true},
    {COMMAND_IS_PLAYER_STOPPED_IN_AREA_2D,0x01A3,6,0,-1,true,true,-1,true},
    {COMMAND_IS_PLAYER_STOPPED_IN_AREA_ON_FOOT_2D,0x01A4,6,0,-1,true,true,-1,true},
    {COMMAND_IS_PLAYER_STOPPED_IN_AREA_IN_CAR_2D,0x01A5,6,0,-1,true,true,-1,true},
    {COMMAND_IS_PLAYER_STOPPED_IN_AREA_3D,0x01A8,8,0,-1,true,true,-1,true},
    {COMMAND_IS_PLAYER_STOPPED_IN_AREA_ON_FOOT_3D,0x01A9,8,0,-1,true,true,-1,true},
    {COMMAND_IS_PLAYER_STOPPED_IN_AREA_IN_CAR_3D,0x01AA,8,0,-1,true,true,-1,true},
    {COMMAND_IS_PLAYER_IN_ANGLED_AREA_2D,0x05F6,7,0,-1,true,true,-1,true},
    {COMMAND_IS_PLAYER_IN_ANGLED_AREA_ON_FOOT_2D,0x05F7,7,0,-1,true,true,-1,true},
    {COMMAND_IS_PLAYER_IN_ANGLED_AREA_IN_CAR_2D,0x05F8,7,0,-1,true,true,-1,true},
    {COMMAND_IS_PLAYER_STOPPED_IN_ANGLED_AREA_2D,0x05F9,7,0,-1,true,true,-1,true},
    {COMMAND_IS_PLAYER_STOPPED_IN_ANGLED_AREA_ON_FOOT_2D,0x05FA,7,0,-1,true,true,-1,true},
    {COMMAND_IS_PLAYER_STOPPED_IN_ANGLED_AREA_IN_CAR_2D,0x05FB,7,0,-1,true,true,-1,true},
    {COMMAND_IS_PLAYER_IN_ANGLED_AREA_3D,0x05FC,9,0,-1,true,true,-1,true},
    {COMMAND_IS_PLAYER_IN_ANGLED_AREA_ON_FOOT_3D,0x05FD,9,0,-1,true,true,-1,true},
    {COMMAND_IS_PLAYER_IN_ANGLED_AREA_IN_CAR_3D,0x05FE,9,0,-1,true,true,-1,true},
    {COMMAND_IS_PLAYER_STOPPED_IN_ANGLED_AREA_3D,0x05FF,9,0,-1,true,true,-1,true},
    {COMMAND_IS_PLAYER_STOPPED_IN_ANGLED_AREA_ON_FOOT_3D,0x0600,9,0,-1,true,true,-1,true},
    {COMMAND_IS_PLAYER_STOPPED_IN_ANGLED_AREA_IN_CAR_3D,0x0601,9,0,-1,true,true,-1,true},
    {COMMAND_WARP_PLAYER_FROM_CAR_TO_COORD,0x0362,4,0,-1,false,true,-1,true},
    {COMMAND_MARK_CHAR_AS_NO_LONGER_NEEDED,0x01C2,1,0,-1,false,true},
    {COMMAND_MARK_CAR_AS_NO_LONGER_NEEDED,0x01C3,1,0,-1,false,true},
    {COMMAND_POINT_CAMERA_AT_CHAR,0x0159,3,0,-1,false,true},
    {COMMAND_POINT_CAMERA_AT_POINT,0x0160,4,0,-1,false,true},
    {COMMAND_POINT_CAMERA_AT_CAR,0x0158,3,0,-1,false,true},
    {COMMAND_DELETE_CHAR,0x009B,1,0,-1,false,true},
    {COMMAND_GET_CHAR_COORDINATES,0x00A0,1,3,-1,false,true},
    {COMMAND_SET_CHAR_COORDINATES,0x00A1,4,0,-1,false,true},
    {COMMAND_SET_CHAR_HEADING,0x0173,2,0,-1,false,true},
    {COMMAND_IS_CHAR_DEAD,0x0118,1,0,-1,true,true},
    {COMMAND_SET_CHAR_HEALTH,0x0223,2,0,-1,false,true},
    {COMMAND_GET_CHAR_HEALTH,0x0226,1,1,-1,false,true},
    {COMMAND_GET_PLAYER_HEALTH,0x0226,1,1,-1,false,true,-1,true},
    {COMMAND_CREATE_CAR,0x00A5,4,1,0,false,true},
    {COMMAND_DELETE_CAR,0x00A6,1,0,-1,false,true},
    {COMMAND_GET_CAR_COORDINATES,0x00AA,1,3,-1,false,true},
    {COMMAND_SET_CAR_COORDINATES,0x00AB,4,0,-1,false,true},
    {COMMAND_SET_CAR_HEADING,0x0175,2,0,-1,false,true},
    {COMMAND_SET_CAR_HEALTH,0x0224,2,0,-1,false,true},
    {COMMAND_GET_CAR_HEALTH,0x0227,1,1,-1,false,true},
    {COMMAND_CREATE_OBJECT,0x0107,4,1,0,false,true},
    {COMMAND_CREATE_OBJECT_NO_OFFSET,0x029B,4,1,0,false,true},
    {COMMAND_DELETE_OBJECT,0x0108,1,0,-1,false,true},
    {COMMAND_SET_OBJECT_COORDINATES,0x01BC,4,0,-1,false,true},
    {COMMAND_SET_OBJECT_HEADING,0x0177,2,0,-1,false,true},
    // Aim continuously at the real target; the LCS objective has no timeout.
    {COMMAND_SET_CHAR_OBJ_AIM_GUN_AT_CHAR,0x0635,2,0,-1,false,true},
    {COMMAND_ADD_EXPLOSION,0x020C,4,0,-1,false,true},
    {COMMAND_IS_EXPLOSION_IN_AREA,0x0356,7,0,-1,true,true},
    {COMMAND_ATTACH_CHAR_TO_CAR,0x0464,8,0,-1,false,true,-1,false,7},
    {COMMAND_BURST_CAR_TYRE,0x04FE,2,0,-1,false,true},
    {COMMAND_GET_POSITION_OF_ANALOGUE_STICKS,0x0494,1,4,-1,false,true},
    {COMMAND_GET_DEBUG_CAMERA_COORDINATES,0x0454,0,3,-1,false,true},
    // Both retail handlers consume, but do not use, the model argument.
    // The two signed-byte extra selectors apply to the next vehicle instance.
    {COMMAND_SET_CAR_MODEL_COMPONENTS,0x0506,3,0,-1,false,true},
    {COMMAND_MAKE_OBJECT_TARGETTABLE,0x035D,1,0,-1,false,true},
};
std::optional<std::pair<bool,bool>> NativeAdapter::active_seat_rules(int32_t ped) {
    const auto front=front_seats_.find(ped),rear=rear_seats_.find(ped);
    if(front==front_seats_.end() && rear==rear_seats_.end())return std::pair{false,false};
    if(!live_ped_identity)return {};
    const auto live=live_ped_identity(ped);
    if(!live || live->handle!=ped || !live->native_ped)return {};
    // The source bits belong to the original CPed, not to a numerical handle.
    // Never carry either bit into a new pedestrian allocated at that handle.
    const bool front_ok=front!=front_seats_.end() && front->second.actor==*live;
    const bool rear_ok=rear!=rear_seats_.end() && rear->second.actor==*live;
    if(front!=front_seats_.end() && !front_ok)front_seats_.erase(ped);
    if(rear!=rear_seats_.end() && !rear_ok)rear_seats_.erase(ped);
    return std::pair{front_ok,rear_ok};
}
StepResult NativeAdapter::dispatch(Vm& vm,Thread& thread,uint16_t opcode) {
    if(opcode==COMMAND_START_CREDITS || (opcode==COMMAND_STOP_CREDITS && stop_credits)) {
        const bool start=opcode==COMMAND_START_CREDITS;
        const auto group=start?vm.parameter(thread):0;
        if(!world_ready || (start?!bool(start_credits):!bool(stop_credits))){thread.diagnostic="Original credits renderer unavailable";return StepResult::unsupported;}
        const auto result=start?start_credits(group):stop_credits();
        if(!result.executed) {
            if(result.native_entered)throw std::runtime_error("Original credits entered failure: "+result.diagnostic);
            thread.diagnostic="Original credits refused: "+result.diagnostic;return StepResult::unsupported;
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_ARE_CREDITS_FINISHED && original_credits_finished) {
        if(!world_ready){thread.diagnostic="Original credits world unavailable";return StepResult::unsupported;}
        vm.compare(thread,original_credits_finished());return StepResult::continued;
    }
    const auto& weapon_counterparts=equivalent_entities ? equivalent_weapon_counterparts : strict_weapon_counterparts;
#include "native_entity_families.inc"
#include "native_garage_family.inc"
#include "native_traffic_family.inc"
#include "native_statistics_family.inc"
#include "native_activity_statistics.inc"
    if(opcode==COMMAND_CHAR_WANDER_DIR || opcode==COMMAND_SET_CHAR_OBJ_WAIT_IN_CAR) {
        const auto actor=vm.parameter(thread);std::optional<int32_t> direction;
        if(opcode==COMMAND_CHAR_WANDER_DIR)direction=vm.parameter(thread);
        if(!world_ready || !execute){thread.diagnostic="Native original pedestrian task backend unavailable";return StepResult::unsupported;}
        const auto exists=execute({0x056D,{actor},{},0,0,true});
        if(!exists.executed || !exists.condition){thread.diagnostic="Original pedestrian task actor is no longer in the native pool";return StepResult::unsupported;}
        NativeCall task;task.opcode=direction?0x05DE:0x05D0;task.inputs={actor};
        task.source_wander_direction=direction;task.source_wait_in_car=!direction;
        task.mission_context=mission_script?mission_script(thread):false;
        bool projected=false;
        const auto result=!direction?execute_projected_objective(opcode,task,projected,[&](){return execute(task);}):execute(task);
        if(!result.executed){thread.diagnostic=result.diagnostic.empty()?"Cannot assign original pedestrian task":result.diagnostic;return StepResult::unsupported;}
        if(projected && !result.native_entered)return StepResult::continued;
        // New original objectives replace any earlier bridge follow/board job.
        if(projected)clear_walking_for_source_route(actor);else forget_walking_objective(actor);
        companions_.erase(actor);frozen_followers_.erase(actor);
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_CHAR_CAN_BE_DAMAGED_BY_MEMBERS_OF_GANG) {
        const auto actor=vm.parameter(thread),gang=vm.parameter(thread);const bool enabled=vm.parameter(thread)!=0;
        if(!world_ready || !set_gang_damage || !set_gang_damage(actor,gang,enabled)) {
            thread.diagnostic="Native actor gang weapon-damage policy unavailable";return StepResult::unsupported;
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_CHAR_HEED_THREATS || opcode==COMMAND_SET_PLAYER_HEED_THREATS) {
        auto actor=vm.parameter(thread);const auto enabled=vm.parameter(thread)!=0;
        if(!world_ready || !set_char_heed_threats){thread.diagnostic="Native threat scanner policy backend unavailable";return StepResult::unsupported;}
        if(opcode==COMMAND_SET_PLAYER_HEED_THREATS) {
            const auto player=execute?execute({0x01F5,{actor},{},0,1}):NativeResult{};
            if(!player.executed || player.values[0]<0){thread.diagnostic="Cannot resolve player threat scanner actor";return StepResult::unsupported;}
            actor=player.values[0];
        }
        if(!set_char_heed_threats(actor,enabled)){thread.diagnostic="Cannot apply native threat scanner policy";return StepResult::unsupported;}
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_CHAR_CROUCH_WHEN_THREATENED) {
        const auto actor=vm.parameter(thread);(void)vm.parameter(thread);
        if(!world_ready || !set_char_crouch_when_threatened || !set_char_crouch_when_threatened(actor)){
            thread.diagnostic="Native frightened-crouch flag backend unavailable";return StepResult::unsupported;
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_POLICE_RADIO_MESSAGE) {
        // PS2 queues this report, but its radio service was removed from the
        // audio update (AudioManager.cpp). Compatibility, not an audio port.
        for(size_t i=0;i<3;++i)
            if(!std::isfinite(bits_float(vm.parameter(thread)))) {
                thread.diagnostic="Police radio position is not finite";return StepResult::unsupported;
            }
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_CHAR_IGNORE_THREATS_BEHIND_OBJECTS) {
        const auto actor=vm.parameter(thread);const bool objects_block=vm.parameter(thread)!=0;
        // CPed::bIgnoreThreatsBehindObjects is read only as the "objects block the line of sight" argument of the ped's
        // threat scan. TRUE is SA's native scan for every actor; FALSE (threats stay visible through objects) is served per
        // actor by the pass-through wrapper on the scan's line-of-sight call (sa_threat_occlusion.cpp).
        if(!world_ready || !set_char_threats_behind_objects || !set_char_threats_behind_objects(actor,objects_block)) {
            thread.diagnostic=objects_block?"Native threat occlusion state could not be restored for the actor":
                "Native threat scan could not be made to see through objects for the actor";
            return StepResult::unsupported;
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_CHAR_ATTACKS_PLAYER_WITH_COPS) {
        const auto handle=vm.parameter(thread);const bool attacks=vm.parameter(thread)!=0;
        const auto actor=live_ped_identity?live_ped_identity(handle):std::nullopt;
        if(!world_ready || !actor || !attack_policy_actor || !attack_policy_world) {
            thread.diagnostic="Attack-with-cops actor identity or native reaction consumer unavailable";return StepResult::unsupported;
        }
        // Exact ped identity: a recycled handle never inherits an earlier value.
        attack_policy_[handle]=AttackPolicy{*actor,attacks};
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_CHAR_STOP_SHOOT_DONT_SEEK_ENTITY) {
        const auto actor=vm.parameter(thread);const bool enabled=vm.parameter(thread)!=0;
        if(!world_ready || !set_stationary_shooter || !set_stationary_shooter(actor,enabled)) {
            thread.diagnostic="Native stationary-shooter flags unavailable";return StepResult::unsupported;
        }
        return StepResult::continued;
    }
    int message_numbers=-1;bool message_soon=true,message_queued=false,message_brief=true;
    switch(opcode) {
    case COMMAND_PRINT_WITH_2_NUMBERS:message_numbers=2;message_queued=true;message_soon=false;break;
    case COMMAND_PRINT_WITH_5_NUMBERS:message_numbers=5;message_queued=true;message_soon=false;break;
    case COMMAND_PRINT_SOON:message_numbers=0;break;
    case COMMAND_PRINT_WITH_NUMBER_SOON:message_numbers=1;break;
    case COMMAND_PRINT_WITH_2_NUMBERS_SOON:message_numbers=2;break;
    case COMMAND_PRINT_WITH_3_NUMBERS_SOON:message_numbers=3;break;
    case COMMAND_PRINT_WITH_4_NUMBERS_SOON:message_numbers=4;break;
    case COMMAND_PRINT_WITH_5_NUMBERS_SOON:message_numbers=5;break;
    case COMMAND_PRINT_WITH_6_NUMBERS_SOON:message_numbers=6;break;
    case COMMAND_PRINT_WITH_3_NUMBERS_NOW:message_numbers=3;message_soon=false;break;
    case COMMAND_PRINT_WITH_4_NUMBERS_NOW:message_numbers=4;message_soon=false;break;
    case COMMAND_PRINT_WITH_5_NUMBERS_NOW:message_numbers=5;message_soon=false;break;
    case COMMAND_PRINT_WITH_6_NUMBERS_NOW:message_numbers=6;message_soon=false;break;
    case COMMAND_PRINT_WITH_NUMBER_NOW_NO_BRIEF:message_numbers=1;message_soon=false;message_brief=false;break;
    case COMMAND_PRINT_WITH_2_NUMBERS_NOW_NO_BRIEF:message_numbers=2;message_soon=false;message_brief=false;break;
    case COMMAND_PRINT_SUBTITLE_NOW_NO_BRIEF:message_numbers=0;message_soon=false;message_brief=false;break;
    }
    if(message_numbers>=0) {
        MissionMessage request;request.key=vm.text_label(thread);request.soon=message_soon;request.queued=message_queued;request.previous_brief=message_brief;
        request.subtitle=opcode==COMMAND_PRINT_SUBTITLE_NOW_NO_BRIEF;
        for(auto& c:request.key)if(c>='a' && c<='z')c-=32;
        for(int i=0;i<message_numbers;++i)request.numbers[size_t(i)]=vm.parameter(thread);
        request.duration=uint32_t(vm.parameter(thread));request.flags=uint16_t(vm.parameter(thread));
        if(!text_ready || !display_message || !display_message(request)) {
            thread.diagnostic="Original ordered mission message renderer is unavailable";return StepResult::unsupported;
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_PRINT_WITH_3_NUMBERS_BIG) {
        BigMissionMessage request;request.key=vm.text_label(thread);
        for(auto& c:request.key)if(c>='a' && c<='z')c-=32;
        for(size_t i=0;i<3;++i)request.numbers[i]=vm.parameter(thread);
        request.duration=uint32_t(vm.parameter(thread));
        request.style=uint16_t(uint16_t(vm.parameter(thread))-1);
        if(!world_ready || !text_ready || !display_big_message || request.style>=7) {
            thread.diagnostic="Original numbered big-message renderer/style unavailable";return StepResult::unsupported;
        }
        const auto result=display_big_message(request);
        if(!result.executed) {
            if(result.native_entered)throw std::runtime_error("Numbered big-message replay rejected after entry; "+result.diagnostic);
            thread.diagnostic=result.diagnostic.empty()?"Numbered big-message renderer rejected before entry":result.diagnostic;
            return StepResult::unsupported;
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_STORE_WEATHER || opcode==COMMAND_RESTORE_WEATHER || opcode==COMMAND_STORE_CLOCK || opcode==COMMAND_RESTORE_CLOCK) {
        if(!world_ready || !execute){thread.diagnostic="Original environment snapshot backend unavailable";return StepResult::unsupported;}
        NativeCall call;
        switch(opcode) {
        case COMMAND_STORE_WEATHER:call.source_environment=EnvironmentCommand::read_weather;call.outputs=4;break;
        case COMMAND_RESTORE_WEATHER:
            // Preserve Script3.cpp's fallthrough into STORE_CLOCK. Capture the
            // current clock within the same guarded operation before writes.
            call.source_environment=EnvironmentCommand::restore_weather_and_store_clock;
            call.inputs.assign(stored_weather_.begin(),stored_weather_.end());call.outputs=3;break;
        case COMMAND_STORE_CLOCK:call.source_environment=EnvironmentCommand::read_clock;call.outputs=3;break;
        case COMMAND_RESTORE_CLOCK:call.source_environment=EnvironmentCommand::restore_clock;
            call.inputs.assign(stored_clock_.begin(),stored_clock_.end());break;
        }
        const auto result=execute(call);
        if(!result.executed){thread.diagnostic="Original environment operation refused: "+result.diagnostic;return StepResult::unsupported;}
        if(opcode==COMMAND_STORE_WEATHER)std::copy_n(result.values.begin(),4,stored_weather_.begin());
        if(opcode==COMMAND_STORE_CLOCK || opcode==COMMAND_RESTORE_WEATHER)std::copy_n(result.values.begin(),3,stored_clock_.begin());
        return StepResult::continued;
    }
    if(opcode==COMMAND_START_KILL_FRENZY || opcode==COMMAND_START_KILL_FRENZY_HEADSHOT) {
        NativeCall call;call.text=vm.text_label(thread);
        for(auto& c:*call.text)if(c>='a' && c<='z')c-=32;
        for(size_t i=0;i<8;++i)call.inputs.push_back(vm.parameter(thread));
        call.source_frenzy_headshots=opcode==COMMAND_START_KILL_FRENZY_HEADSHOT;
        call.mission_context=mission_script && mission_script(thread);
        if(!world_ready || !text_ready || !execute || !refresh_frenzy_alias()) {
            thread.diagnostic="Original rampage world/text/weapon ownership unavailable";return StepResult::unsupported;
        }
        auto& weapon=call.inputs[0];const auto original_weapon=weapon;
        // Damage selectors are not equipable guns. Darkel handles them without
        // changing inventory, except the original UZI drive-by selector.
        if(weapon>=39 && weapon<=47)weapon+=10;
        else if(weapon<0 || size_t(weapon)>=std::size(weapon_counterparts) || weapon_counterparts[weapon]<0) {
            thread.diagnostic="Original rampage weapon has no reviewed native counterpart";return StepResult::unsupported;
        } else weapon=weapon_counterparts[weapon];
        for(size_t i:{size_t(3),size_t(4),size_t(5),size_t(6)})if(call.inputs[i]>=0) {
            const auto target=binding(call.inputs[i]);
            if(!target || (target->kind!=ModelKind::ped && target->kind!=ModelKind::vehicle)) {
                thread.diagnostic="Original rampage target model lacks a ped/vehicle binding";return StepResult::unsupported;
            }
            call.inputs[i]=target->id;
        }
        const auto player=execute({0x01F5,{0},{},0,1});
        if(!player.executed || player.values[0]<0) {
            thread.diagnostic="Cannot resolve actual rampage player";return StepResult::unsupported;
        }
        const auto started=execute(call);
        if(!started.executed){thread.diagnostic="Native rampage start failed: "+started.diagnostic;return StepResult::unsupported;}
        if(equivalent_entities && ((original_weapon>=0 && original_weapon<37) || original_weapon==42)) {
            const auto fixed_weapon=weapon==52?28:weapon,fixed_original=original_weapon==42?23:original_weapon;
            auto& aliases=equivalent_weapons_[player.values[0]];const auto previous=aliases.find(fixed_weapon);
            frenzy_alias_=FrenzyAlias{player.values[0],fixed_weapon,fixed_original,previous==aliases.end()?std::nullopt:std::optional<int32_t>(previous->second)};
            aliases[fixed_weapon]=fixed_original;
        }
        return StepResult::continued;
    }
    if(frenzy_alias_ && (opcode==COMMAND_READ_KILL_FRENZY_STATUS || opcode==COMMAND_GET_CURRENT_PLAYER_WEAPON || opcode==COMMAND_GET_CURRENT_CHAR_WEAPON || opcode==COMMAND_GET_CHAR_WEAPON_IN_SLOT)) {
        auto destinations=thread;
        const size_t inputs=opcode==COMMAND_READ_KILL_FRENZY_STATUS?0:opcode==COMMAND_GET_CHAR_WEAPON_IN_SLOT?2:1;
        const size_t outputs=opcode==COMMAND_GET_CHAR_WEAPON_IN_SLOT?3:1;
        for(size_t i=0;i<inputs;++i)vm.parameter(destinations);
        for(size_t i=0;i<outputs;++i)vm.variable(destinations);
        if(!refresh_frenzy_alias()) {
            thread.diagnostic="Cannot synchronize completed native rampage weapon identity";return StepResult::unsupported;
        }
    }
    if(opcode==COMMAND_SET_THREAT_REACTION_RANGE_MULTIPLIER) {
        const auto encoded=vm.parameter(thread);float value;std::memcpy(&value,&encoded,4);
        const auto multiplier=ThreatRangePolicy::original_multiplier(value);
        if(!multiplier)throw std::runtime_error("Invalid original threat range multiplier");
        if(!world_ready || !set_threat_range_multiplier || !set_threat_range_multiplier(*multiplier)) {
            thread.diagnostic="Native threat scanner radius policy unavailable";return StepResult::unsupported;
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_CAN_CHAR_SEE_DEAD_CHAR) {
        const auto actor=vm.parameter(thread),original_type=vm.parameter(thread);
        const auto custom=ped_types_.find(original_type);
        static constexpr int32_t types[]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,18,19,20,-1,22,23,-1};
        const auto type=custom!=ped_types_.end()?custom->second:original_type>=0 && size_t(original_type)<std::size(types)?types[original_type]:-1;
        if(!world_ready || !can_see_dead_char || type<0){thread.diagnostic="Original dead-character visibility/type backend unavailable";return StepResult::unsupported;}
        const auto visible=can_see_dead_char(actor,type);
        if(!visible){thread.diagnostic="Cannot inspect actual native nearby dead characters";return StepResult::unsupported;}
        vm.compare(thread,*visible);return StepResult::continued;
    }
    if(opcode==COMMAND_GET_CHAR_WEAPON_IN_SLOT) {
        const auto actor=vm.parameter(thread),slot=vm.parameter(thread)-1;
        if(!world_ready || !execute || !native_weapon_slot || slot<0 || slot>=10 || !weapon_definitions_[0]){
            thread.diagnostic="Original weapon slot inventory backend/definitions unavailable";return StepResult::unsupported;
        }
        auto destinations=thread;for(int i=0;i<3;++i)vm.variable(destinations);
        std::set<int32_t> native_slots;
        for(size_t i=0;i<weapon_definitions_.size();++i)if(weapon_definitions_[i] && weapon_definitions_[i]->slot==slot && weapon_counterparts[i]>=0){
            const auto native_slot=native_weapon_slot(weapon_counterparts[i]);
            if(!native_slot || *native_slot<0 || *native_slot>=13){thread.diagnostic="Cannot resolve actual native weapon inventory slot";return StepResult::unsupported;}
            native_slots.insert(*native_slot+1);
        }
        const auto aliases=equivalent_weapons_.find(actor);
        std::optional<std::array<int32_t,3>> selected;
        for(auto native_slot:native_slots){
            const auto query=execute({0x04B8,{actor,native_slot},{},0,3});
            if(!query.executed){thread.diagnostic="Native weapon inventory read failed: "+query.diagnostic;return StepResult::unsupported;}
            const auto native_type=query.values[0];if(native_type==0 && slot!=0)continue;
            int32_t original=-1;
            if(equivalent_entities && aliases!=equivalent_weapons_.end()){
                const auto alias=aliases->second.find(native_type);if(alias!=aliases->second.end())original=alias->second;
            }
            if(original<0){
                bool known=false;
                for(size_t i=0;i<std::size(weapon_counterparts);++i)if(weapon_counterparts[i]==native_type){
                    known=true;if(weapon_definitions_[i]->slot==slot){original=int32_t(i);break;}
                }
                if(original<0 && known)continue;
            }
            if(original<0){thread.diagnostic="Native inventory weapon has no original identity";return StepResult::unsupported;}
            if(weapon_definitions_[size_t(original)]->slot!=slot)continue;
            const std::array<int32_t,3> result{{original,query.values[1],weapon_definitions_[size_t(original)]->model}};
            if(selected && *selected!=result){thread.diagnostic="Multiple native weapons ambiguously occupy one original slot";return StepResult::unsupported;}
            selected=result;
        }
        const auto result=selected.value_or(std::array<int32_t,3>{{0,0,weapon_definitions_[0]->model}});
        for(auto value:result)vm.store(thread,value);return StepResult::continued;
    }
    if(opcode==COMMAND_SET_CHAR_PERSONALITY) {
        static constexpr const char* names[]={
#include "original_personality_names.inc"
        };
        const auto actor=vm.parameter(thread),personality=vm.parameter(thread);
        if(!world_ready || !set_char_personality || personality<0 || size_t(personality)>=std::size(names) || !set_char_personality(actor,names[personality])) {
            thread.diagnostic="Named native personality is unavailable: original="+std::to_string(personality);return StepResult::unsupported;
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_PRINT_STRING_IN_STRING_NOW) {
        NativeCall call;call.opcode=0x0384;call.text=vm.text_label(thread);call.replacement_text=vm.text_label(thread);
        for(auto* label:{&*call.text,&*call.replacement_text})for(auto& c:*label)c=char(std::toupper(static_cast<unsigned char>(c)));
        call.inputs={vm.parameter(thread),vm.parameter(thread)};
        if(!text_ready || !execute || !execute(call).executed){thread.diagnostic="Native message template/replacement playback failed";return StepResult::unsupported;}
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_CHARS_CHATTING) {
        const auto first=vm.parameter(thread),second=vm.parameter(thread),duration=vm.parameter(thread);
        if(!world_ready || !execute || !play_wait_animation || duration<0){thread.diagnostic="Original paired chat backend unavailable or invalid duration";return StepResult::unsupported;}
        for(const auto actor:{first,second}) {
            const auto alive=execute({0x056D,{actor},{},0,0,true});
            if(!alive.executed || !alive.condition){thread.diagnostic="Paired chat actor is unavailable";return StepResult::unsupported;}
        }
        // SA 03F9 is unimplemented; 0677's final parameter is a chat stage,
        // not the LCS timeout. Use timed stand/look tasks with the original
        // IDLE_chat association instead of misinterpreting that parameter.
        for(const auto actor:{first,second}) {
            const auto partner=actor==first?second:first;
            for(const NativeCall& task:std::vector<NativeCall>{{0x05BA,{actor,duration}},{0x0639,{actor,partner}},{0x05BF,{actor,partner,duration}}}) {
                if(!execute(task).executed){thread.diagnostic="Native paired chat task failed";return StepResult::unsupported;}
            }
            if(!play_wait_animation(actor,19,duration,true)){thread.diagnostic="Original paired chat animation unavailable";return StepResult::unsupported;}
            forget_walking_objective(actor);
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_CAR_TILTED_BY_CHAR) {
        // Script8: ApplyTurnForce on the vehicle at the ped's offset, then SOUND_CAR_JERK for the vehicle.
        // Both consumers are required before any effect; a sound failure after the force is never replayed.
        const auto car=vm.parameter(thread),ped=vm.parameter(thread);
        if(!world_ready || !execute || !apply_car_tilt || !car_jerk_sound || !live_ped_identity){thread.diagnostic="Car tilt force/sound backend unavailable";return StepResult::unsupported;}
        if(!live_ped_identity(ped)){thread.diagnostic="Car tilt character identity unavailable";return StepResult::unsupported;}
        const auto exists=execute({0x056E,{car},{},0,0,true});
        if(!exists.executed || !exists.condition){thread.diagnostic="Car tilt vehicle is not live";return StepResult::unsupported;}
        const auto where=execute({0x00AA,{car},{},0,3});
        if(!where.executed || !finite_triplet(where.values)){thread.diagnostic="Car tilt vehicle position unavailable";return StepResult::unsupported;}
        const auto random=execute({0x0209,{0,1000},{},0,1});
        if(!random.executed || random.values[0]<0 || random.values[0]>1000){thread.diagnostic="Car tilt native random source unavailable";return StepResult::unsupported;}
        const float factor=-0.8f-0.4f*float(random.values[0])/1000.0f; // GetRandomNumberInRange(-0.8f,-1.2f)
        const auto force=apply_car_tilt(car,ped,factor);
        if(!force.executed) {
            if(force.native_entered)throw std::runtime_error("Car tilt force failed after native entry; do not replay: "+force.diagnostic);
            thread.diagnostic=force.diagnostic;return StepResult::unsupported;
        }
        std::array<float,3> position{};for(size_t axis=0;axis<3;++axis)std::memcpy(&position[axis],&where.values[axis],4);
        if(!car_jerk_sound(position))throw std::runtime_error("SOUND_CAR_JERK could not be produced after the car tilt force; do not replay");
        return StepResult::continued;
    }
    if(opcode==COMMAND_CREATE_SINGLE_PARTICLE) {
        // Script6: CParticle::AddParticle(type,pos,vel,nil,size,0,0,0,0). One finite SA particle.
        const auto type=vm.parameter(thread);
        FxVec3 position{},velocity{};
        for(auto& value:position)value=bits_float(vm.parameter(thread));
        for(auto& value:velocity)value=bits_float(vm.parameter(thread));
        const float size=bits_float(vm.parameter(thread));
        const auto status=fx.single(type,position,velocity,size);
        if(!status.ok) {
            if(status.native_entered)throw std::runtime_error("Single SA particle failed after native entry; do not replay: "+status.diagnostic);
            thread.diagnostic="Single particle: "+status.diagnostic;return StepResult::unsupported;
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_ADD_MOVING_PARTICLE_EFFECT) {
        // Script4: ground sentinel (z<=-100) -> FindGroundZForCoord, size clamped at 0, type 17 keeps
        // the byte colour; the last operand is the lifetime. A recurring ParticleObject, not one particle.
        const auto type=vm.parameter(thread);
        FxVec3 position{},target{};
        for(auto& value:position)value=bits_float(vm.parameter(thread));
        for(auto& value:target)value=bits_float(vm.parameter(thread));
        float size=bits_float(vm.parameter(thread));
        std::array<int32_t,3> rgb{};
        for(auto& value:rgb)value=vm.parameter(thread)&0xFF;
        const auto life=uint32_t(vm.parameter(thread));
        if(!fx.ready() || !execute){thread.diagnostic="Particle effect backend unavailable";return StepResult::unsupported;}
        if(!fx.supports_object(int32_t(uint16_t(type)))){
            thread.diagnostic="Particle effect type "+std::to_string(int32_t(uint16_t(type)))+" has no verified SA recipe";return StepResult::unsupported;
        }
        if(!(size>0.0f))size=0.0f; // Max(0, size); NaN never reaches the native layer
        if(position[2]<=-100.0f) {
            const auto ground=execute({0x02CE,{float_bits(position[0]),float_bits(position[1]),float_bits(1000.0f)},{},0,1});
            if(!ground.executed){thread.diagnostic="Native ground query for particle effect failed: "+ground.diagnostic;return StepResult::unsupported;}
            position[2]=bits_float(ground.values[0]);
        }
        const auto clock=execute({0x01BD,{},{},0,1});
        if(!clock.executed){thread.diagnostic="Native game clock unavailable for particle effect";return StepResult::unsupported;}
        bool added=false;
        const auto status=fx.add_moving(int32_t(uint16_t(type)),position,target,size,rgb,life,uint32_t(clock.values[0]),added);
        if(!status.ok) {
            if(status.native_entered)throw std::runtime_error("Particle effect failed after native entry; do not replay: "+status.diagnostic);
            thread.diagnostic="Particle effect: "+status.diagnostic;return StepResult::unsupported;
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_REMOVE_PARTICLE_EFFECTS_IN_AREA) {
        std::array<float,6> box{};
        for(auto& value:box)value=bits_float(vm.parameter(thread));
        fx.remove_in_area(box);
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_CHAR_OBJ_FLEE_CAR) {
        const auto ped=vm.parameter(thread),car=vm.parameter(thread);
        NativeCall packet;packet.inputs={ped,car};bool projected=false;
        const auto result=execute_projected_objective(opcode,packet,projected,[&]{return flee_car(ped,car);});
        if(!result.executed){thread.diagnostic="Native SA flee-car failed: "+result.diagnostic;return StepResult::unsupported;}
        if(!projected || result.native_entered)forget_walking_objective(ped);
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_CRUSHER_REWARD_MULTIPLIER) {
        // Source uint8 configuration; its consumer reads this same state when
        // the real crusher completes. Initialization need not wait for streaming.
        exclusive_garages.set_reward_multiplier(vm.parameter(thread));
        return StepResult::continued;
    }
    if(opcode==COMMAND_IS_CAR_CRUSHED) {
        const auto vehicle=vm.parameter(thread);
        if(!world_ready || !exclusive_garages.world.crusher_door || !exclusive_garages.world.entirely_inside) {
            thread.diagnostic="Physical crusher consumer unavailable";return StepResult::unsupported;
        }
        vm.compare(thread,exclusive_garages.car_crushed(vehicle));return StepResult::continued;
    }
    if(opcode==COMMAND_HAS_IMPORT_GARAGE_SLOT_BEEN_FILLED) {
        // CGarages::HasImportExportGarageCollectedThisCar(garage, slot-1): bit slot-1 of the import list that
        // SA saves (CGarages::CarTypesCollected). Only a collector garage this service owns can answer.
        const auto garage=vm.parameter(thread),slot=vm.parameter(thread);
        if(!world_ready || !exclusive_garages.world.collected_read) {
            thread.diagnostic="Import list store unavailable";return StepResult::unsupported;
        }
        const auto filled=slot>=1 && slot<=32?exclusive_garages.slot_filled(garage,slot-1):std::nullopt;
        if(!filled) {
            thread.diagnostic="Import garage slot has no owned collector garage or readable saved list";return StepResult::unsupported;
        }
        vm.compare(thread,*filled);return StepResult::continued;
    }
    if(opcode==COMMAND_ENABLE_FERRY_PATH || opcode==COMMAND_GET_CLOSEST_DOCKED_FERRY || opcode==COMMAND_SKIP_FERRY_TO_NEXT_DOCK ||
       opcode==COMMAND_GET_FERRY_BOARDING_SPACE || opcode==COMMAND_GET_FERRY_HEADING || opcode==COMMAND_SET_FERRIES_DISABLED ||
       opcode==COMMAND_SWITCH_FERRY_COLLISION || opcode==COMMAND_OPEN_FERRY_DOOR || opcode==COMMAND_CLOSE_FERRY_DOOR ||
       opcode==COMMAND_IS_FERRY_DOOR_OPEN || opcode==COMMAND_IS_FERRY_DOOR_CLOSED || opcode==COMMAND_COMPLETE_FERRY_DOOR_MOVEMENT) {
        // Retail CFerry over the FERRY1.DAT track. FerryService decides where each ferry is; every answer that a
        // script can observe (docked ferry, heading, boarding point) is only given while the real SA boat of
        // each ferry exists and is being driven by that service. Without it the command is refused, never answered
        // from a virtual ferry.
        auto store_float=[&](float value){int32_t bits;std::memcpy(&bits,&value,4);vm.store(thread,bits);};
        const auto physical_required=[&]{
            if(!world_ready || !ferry_service.physical()){thread.diagnostic="Physical ferry consumer unavailable";return false;}
            return true;
        };
        switch(opcode) {
        case COMMAND_ENABLE_FERRY_PATH: {
            const auto path=vm.parameter(thread);
            if(!world_ready){thread.diagnostic="Ferry track is not mounted";return StepResult::unsupported;}
            // Success only after both boats physically exist, so a query right after the command is coherent. While the
            // producer streams the model the same instruction is retried on the next tick (idempotent: nothing is created twice).
            switch(ferry_service.enable_path(path)) {
            case FerryService::Enable::ready:return StepResult::continued;
            case FerryService::Enable::pending:
                thread.ip=thread.last_ip;thread.diagnostic="Waiting for the physical ferries";return StepResult::yielded;
            default:thread.diagnostic=ferry_service.diagnostic().empty()?"Ferry production is unavailable":ferry_service.diagnostic();return StepResult::unsupported;
            }
        }
        case COMMAND_GET_CLOSEST_DOCKED_FERRY: {
            const auto x_bits=vm.parameter(thread),y_bits=vm.parameter(thread);auto out=thread;vm.variable(out);
            float x=0,y=0;std::memcpy(&x,&x_bits,4);std::memcpy(&y,&y_bits,4);
            if(!std::isfinite(x) || !std::isfinite(y) || !physical_required())return StepResult::unsupported;
            vm.store(thread,ferry_service.closest_docked(x,y));return StepResult::continued;
        }
        case COMMAND_SKIP_FERRY_TO_NEXT_DOCK: {
            const auto id=vm.parameter(thread);
            if(!physical_required() || !ferry_service.skip_to_next_dock(id)){thread.diagnostic="Ferry does not exist";return StepResult::unsupported;}
            return StepResult::continued;
        }
        case COMMAND_GET_FERRY_BOARDING_SPACE: {
            const auto id=vm.parameter(thread),use=vm.parameter(thread),style=vm.parameter(thread),slot=vm.parameter(thread);
            auto outs=thread;vm.variable(outs);vm.variable(outs);
            if(!physical_required())return StepResult::unsupported;
            const auto space=ferry_service.boarding_space(id,use,style,slot);
            if(!space){thread.diagnostic="Ferry boarding space is unavailable";return StepResult::unsupported;}
            store_float((*space)[0]);store_float((*space)[1]);return StepResult::continued;
        }
        case COMMAND_GET_FERRY_HEADING: {
            const auto id=vm.parameter(thread);auto out=thread;vm.variable(out);
            const auto heading=physical_required()?ferry_service.heading(id):std::nullopt;
            if(!heading){thread.diagnostic="Ferry does not exist";return StepResult::unsupported;}
            store_float(*heading);return StepResult::continued;
        }
        case COMMAND_OPEN_FERRY_DOOR:case COMMAND_CLOSE_FERRY_DOOR:case COMMAND_COMPLETE_FERRY_DOOR_MOVEMENT: {
            // The door/ramp nodes of the physical hull are driven by the service timers (OpenDoor/CloseDoor 10 s); these
            // commands are the production producers of every door transition.
            const auto id=vm.parameter(thread);
            const bool ok=physical_required() && (opcode==COMMAND_OPEN_FERRY_DOOR?ferry_service.open_door(id):
                opcode==COMMAND_CLOSE_FERRY_DOOR?ferry_service.close_door(id):ferry_service.complete_door_movement(id));
            if(!ok){thread.diagnostic="Ferry does not exist";return StepResult::unsupported;}
            return StepResult::continued;
        }
        case COMMAND_IS_FERRY_DOOR_OPEN:case COMMAND_IS_FERRY_DOOR_CLOSED: {
            const auto id=vm.parameter(thread);
            const auto answer=physical_required()?(opcode==COMMAND_IS_FERRY_DOOR_OPEN?ferry_service.door_open(id):ferry_service.door_closed(id)):std::nullopt;
            if(!answer){thread.diagnostic="Ferry does not exist";return StepResult::unsupported;}
            vm.compare(thread,*answer);return StepResult::continued;
        }
        case COMMAND_SET_FERRIES_DISABLED: {
            (void)vm.parameter(thread);const auto disabled=vm.parameter(thread);
            if(!world_ready){thread.diagnostic="Ferry service is not mounted";return StepResult::unsupported;}
            ferry_service.set_disabled(disabled!=0);return StepResult::continued;
        }
        default: {
            const auto type=vm.parameter(thread);
            if(!physical_required() || !ferry_service.switch_collision(type)){thread.diagnostic="Ferry collision model could not be switched";return StepResult::unsupported;}
            return StepResult::continued;
        }
        }
    }
    if(opcode==COMMAND_IS_CRANE_LIFTING_CAR) {
        // CCranes::IsThisCarPickedUp(x,y,car): the exact car, lifted or rotating on the crane near (x,y).
        const auto x_bits=vm.parameter(thread),y_bits=vm.parameter(thread),car=vm.parameter(thread);
        float x=0,y=0;std::memcpy(&x,&x_bits,4);std::memcpy(&y,&y_bits,4);
        if(!world_ready || !crane_service.world.crane_ready || !crane_service.world.vehicle_lookup) {
            thread.diagnostic="Physical crane consumer unavailable";return StepResult::unsupported;
        }
        const auto lifted=crane_service.picked_up(x,y,car);
        if(!lifted){thread.diagnostic="Crane lift evidence unavailable";return StepResult::unsupported;}
        vm.compare(thread,*lifted);return StepResult::continued;
    }
    if(opcode==COMMAND_IS_CHAR_OBJECTIVE_PASSED) {
        const auto actor=vm.parameter(thread);
        if(!world_ready || !char_objective_passed) {
            thread.diagnostic="Native objective completion consumer unavailable";
            return StepResult::unsupported;
        }
        const auto completed=char_objective_passed(actor);
        if(!completed) {
            thread.diagnostic="Native objective completion evidence unavailable";
            return StepResult::unsupported;
        }
        vm.compare(thread,*completed);return StepResult::continued;
    }
    if(opcode==COMMAND_IS_CHAR_STUCK) {
        // Script7: UpdateCompareFlag(ped->m_nWaitState==WAITSTATE_STUCK). SA adaptation: SA's own static
        // counter vs its go-to task limit (see sa_char_stuck). An unreadable native state is a refusal.
        const auto ped=vm.parameter(thread);
        const auto actor=live_ped_identity?live_ped_identity(ped):std::nullopt;
        if(!world_ready || !char_stuck || !actor){thread.diagnostic="Stuck-character identity or native query unavailable";return StepResult::unsupported;}
        const auto stuck=char_stuck(*actor);
        if(!stuck){thread.diagnostic="Native SA stuck evidence could not be read";return StepResult::unsupported;}
        vm.compare(thread,*stuck);return StepResult::continued;
    }
    if(opcode==COMMAND_CHAR_FOLLOW_PATH) {
        const auto ped=vm.parameter(thread);
        std::array<float,3> destination{};for(auto& value:destination)value=bits_float(vm.parameter(thread));
        const auto radius=bits_float(vm.parameter(thread));const auto mode=vm.parameter(thread);
        if(!world_ready || !char_follow_path || !std::isfinite(radius) ||
           !std::all_of(destination.begin(),destination.end(),[](float value){return std::isfinite(value);})){
            thread.diagnostic="Native character path consumer or finite operands unavailable";return StepResult::unsupported;
        }
        NativeCall packet;packet.inputs={ped,float_bits(destination[0]),float_bits(destination[1]),float_bits(destination[2]),float_bits(radius),mode};bool projected=false;
        const auto result=execute_projected_objective(opcode,packet,projected,[&]{return char_follow_path(ped,destination,radius,mode,mission_script && mission_script(thread));});
        if(!result.executed){
            if(result.native_entered)throw std::runtime_error("Character path changed before failure; replay rejected: "+result.diagnostic);
            thread.diagnostic="Native character path refused: "+result.diagnostic;return StepResult::unsupported;
        }
        if(result.native_entered){
            forget_walking_objective(ped);
            if(result.task_capture==NativeResult::TaskCapture::captured && result.accepted_script_task){
                const auto& captured=*result.accepted_script_task;const auto& record=captured.record;
                const bool event=record.status==0 && record.event && !record.task;
                const bool task=(record.status==1 || record.status==2 || (record.status>=4 && record.status<=6)) && record.task && !record.event;
                if(captured.slot<128 && record.opcode==0x0A2E && record.ped && (event || task))accepted_walk_tasks_[ped]=captured;
            }
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_CHAR_OBJ_HAIL_TAXI) {
        const auto ped=vm.parameter(thread);
        if(!world_ready || !hail_taxi){thread.diagnostic="Native taxi-hail cycle consumer unavailable";return StepResult::unsupported;}
        NativeCall packet;packet.inputs={ped};bool projected=false;
        const auto result=execute_projected_objective(opcode,packet,projected,[&]{return hail_taxi(ped,mission_script && mission_script(thread));});
        if(!result.executed){
            if(result.native_entered)throw std::runtime_error("Taxi-hail cycle failed after native entry; replay rejected: "+result.diagnostic);
            thread.diagnostic="Native taxi-hail cycle refused: "+result.diagnostic;return StepResult::unsupported;
        }
        if(!projected || result.native_entered)forget_walking_objective(ped);
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_CHAR_OBJ_GUARD_SPOT) {
        const auto ped=vm.parameter(thread);
        std::array<int32_t,3> at{};for(auto& value:at)value=vm.parameter(thread);
        NativeCall packet;packet.inputs={ped,at[0],at[1],at[2]};bool projected=false;
        const auto result=execute_projected_objective(opcode,packet,projected,[&]{return guard_spot(ped,at);});
        if(!result.executed){thread.diagnostic="Native SA guard spot failed: "+result.diagnostic;return StepResult::unsupported;}
        if(!projected || result.native_entered)forget_walking_objective(ped);
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_ALL_OCCUPANTS_OF_CAR_LEAVE_CAR) {
        const auto car=vm.parameter(thread);
        const auto result=leave_all_occupants(car);
        if(!result.executed){thread.diagnostic="Native SA occupant exit failed: "+result.diagnostic;return StepResult::unsupported;}
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_CHAR_DIVE_FROM_CAR) {
        const auto ped=vm.parameter(thread),car=vm.parameter(thread);
        const auto result=dive_from_car(ped,car);
        if(!result.executed){thread.diagnostic="Native SA dive-from-car failed: "+result.diagnostic;return StepResult::unsupported;}
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_CHAR_AS_LEADER || opcode==COMMAND_SET_CHAR_OBJ_FOLLOW_CHAR_IN_FORMATION) {
        const bool formation_form=opcode==COMMAND_SET_CHAR_OBJ_FOLLOW_CHAR_IN_FORMATION;
        const auto follower=vm.parameter(thread),leader=vm.parameter(thread);
        const auto formation=formation_form?vm.parameter(thread):0;
        // LCS eFormation has UNDEFINED..FRONT (0..8). SA positions come from its
        // group AI, so the value is validated and consumed but not reconstructed.
        if(formation<0 || formation>8){thread.diagnostic="Original formation selector is outside the LCS enum";return StepResult::unsupported;}
        const bool mission=mission_script && mission_script(thread);
        NativeResult result;
        if(formation_form) {
            NativeCall packet;packet.inputs={follower,leader,formation};bool projected=false;
            result=execute_projected_objective(opcode,packet,projected,[&]{return join_leader_group(follower,leader,mission);});
            if(projected && !result.native_entered && result.executed)return StepResult::continued;
        } else result=join_leader_group(follower,leader,mission);
        if(!result.executed){thread.diagnostic="Native SA group join failed: "+result.diagnostic;return StepResult::unsupported;}
        if(result.native_entered){companions_.erase(follower);frozen_followers_.erase(follower);forget_walking_objective(follower);}
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_PLAYER_AS_LEADER || opcode==COMMAND_IS_CHAR_IN_PLAYERS_GROUP) {
        const auto actor=vm.parameter(thread),player=vm.parameter(thread);
        if(!world_ready || !execute){thread.diagnostic="Native player group backend unavailable";return StepResult::unsupported;}
        const auto ped=execute({0x01F5,{player},{},0,1});
        const auto group=execute({0x07AF,{player},{},0,1});
        if(!ped.executed || ped.values[0]<0 || !group.executed || group.values[0]<0){thread.diagnostic="Cannot resolve the existing player/group";return StepResult::unsupported;}
        const auto valid=execute({0x07FD,{group.values[0]},{},0,0,true});
        if(!valid.executed || !valid.condition){thread.diagnostic="Native player group does not exist";return StepResult::unsupported;}
        if(opcode==COMMAND_SET_PLAYER_AS_LEADER) {
            const auto previous=companions_.find(actor);
            if(previous!=companions_.end() && previous->second.player==player && previous->second.group==group.values[0]) {
                const auto member=execute({0x06EE,{actor,group.values[0]},{},0,0,true});
                if(!member.executed){thread.diagnostic="Cannot validate existing native companion membership";return StepResult::unsupported;}
                // LCS can repeat SET_LEADER while approaching/boarding. It is
                // idempotent for the same leader. Clearing SA tasks here would
                // cancel the dealer's door approach on every original retry.
                if(member.condition)return StepResult::continued;
            }
            if(!execute({0x0687,{actor}}).executed){thread.diagnostic="Native companion's previous scripted task could not be released";return StepResult::unsupported;}
            NativeCall member{0x0631,{group.values[0],actor}};member.mission_context=mission_script && mission_script(thread);
            if(!execute(member).executed){thread.diagnostic="Native companion could not join the player's group";return StepResult::unsupported;}
            if(!execute({0x0940,{group.values[0],1}}).executed){thread.diagnostic="Native group passenger following could not be enabled";return StepResult::unsupported;}
            companions_.insert_or_assign(actor,Companion{player,group.values[0],-1,member.mission_context});
            forget_walking_objective(actor);
        } else {
            const auto member=execute({0x06EE,{actor,group.values[0]},{},0,0,true});
            if(!member.executed){thread.diagnostic="Native group membership query failed";return StepResult::unsupported;}
            bool near=false;
            if(member.condition) {
                const auto a=execute({0x00A0,{actor},{},0,3}),b=execute({0x00A0,{ped.values[0]},{},0,3});
                if(!a.executed || !b.executed){thread.diagnostic="Native leader distance query failed";return StepResult::unsupported;}
                float squared=0;for(size_t i=0;i<3;++i){float x,y;std::memcpy(&x,&a.values[i],4);std::memcpy(&y,&b.values[i],4);squared+=(x-y)*(x-y);}
                // LCS CPed::UpdateFromLeader sets WaitForLeader past 30 m.
                near=squared<=30.0f*30.0f;
            }
            vm.compare(thread,member.condition && near);
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_FREEZE_ALL_PLAYER_FOLLOWERS) {
        const auto player=vm.parameter(thread);const bool frozen=vm.parameter(thread)!=0;
        if(!world_ready || !execute){thread.diagnostic="Native follower freeze backend unavailable";return StepResult::unsupported;}
        const auto leader=execute({0x01F5,{player},{},0,1}),group=execute({0x07AF,{player},{},0,1});
        if(!leader.executed || leader.values[0]<0 || !group.executed || group.values[0]<0){thread.diagnostic="Cannot resolve native follower group";return StepResult::unsupported;}
        const auto valid=execute({0x07FD,{group.values[0]},{},0,0,true});
        if(!valid.executed || !valid.condition){thread.diagnostic="Native follower group does not exist";return StepResult::unsupported;}
        // CPedGroupMembership contains seven sparse follower slots and a leader
        // at slot seven. Visit every follower slot; never freeze the player.
        std::vector<int32_t> followers;
        for(int32_t slot=0;slot<7;++slot) {
            const auto member=execute({0x092B,{group.values[0],slot},{},0,1});
            if(!member.executed){thread.diagnostic="Cannot enumerate native followers";return StepResult::unsupported;}
            if(member.values[0]>=0 && member.values[0]!=leader.values[0])followers.push_back(member.values[0]);
        }
        for(auto actor:followers){
            if(!execute({0x04D7,{actor,frozen?1:0}}).executed){thread.diagnostic="Cannot freeze/unfreeze native follower";return StepResult::unsupported;}
            if(frozen)frozen_followers_.insert(actor);else frozen_followers_.erase(actor);
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_CHAR_USE_PEDNODE_SEEK) {
        const auto actor=vm.parameter(thread);const bool enabled=vm.parameter(thread)!=0;
        if(!world_ready || !execute || !set_pednode_seek){thread.diagnostic="Native pedestrian seek policy backend unavailable";return StepResult::unsupported;}
        const auto exists=execute({0x056D,{actor},{},0,0,true});
        if(!exists.executed || !exists.condition || !set_pednode_seek(actor,enabled)){thread.diagnostic="Cannot apply native pedestrian seek policy";return StepResult::unsupported;}
        pednode_seek_[actor]=enabled;return StepResult::continued;
    }
    if(opcode==COMMAND_SET_CHAR_IN_PLAYERS_GROUP_CAN_FIGHT) {
        const auto actor=vm.parameter(thread);const bool enabled=vm.parameter(thread)!=0;
        if(!world_ready || !set_char_can_fight || !set_char_can_fight(actor,enabled)){thread.diagnostic="Native companion fight flag update failed";return StepResult::unsupported;}
        return StepResult::continued;
    }
    if(opcode==COMMAND_REQUEST_ANIMATION || opcode==COMMAND_HAS_ANIMATION_LOADED || opcode==COMMAND_REMOVE_ANIMATION) {
        const auto name=lower(vm.text_label(thread));
        const uint16_t action=opcode==COMMAND_REQUEST_ANIMATION?0:opcode==COMMAND_HAS_ANIMATION_LOADED?1:2;
        if(!world_ready || !named_animation){thread.diagnostic="Original named animation backend unavailable: "+name;return StepResult::unsupported;}
        const auto result=named_animation(name,action);
        if(!result || (action!=1 && !*result)){thread.diagnostic="Original named animation operation failed: "+name;return StepResult::unsupported;}
        if(action==1)vm.compare(thread,*result);
        return StepResult::continued;
    }
    if(opcode==COMMAND_LOAD_NON_STANDARD_PED_ANIM || opcode==COMMAND_UNLOAD_NON_STANDARD_PED_ANIM) {
        const auto state=vm.parameter(thread);
        if(!world_ready || !request_wait_animation || !request_wait_animation(state,opcode==COMMAND_LOAD_NON_STANDARD_PED_ANIM)) {
            thread.diagnostic="Original mission gesture block unavailable: state="+std::to_string(state);return StepResult::unsupported;
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_CLEAR_BIG_MESSAGES) {
        // The original Script9 handler clears only the garage message timer;
        // its two wider HUD clear calls are commented out in reLCS.
        if(!world_ready || !clear_garage_message || !clear_garage_message()) {
            thread.diagnostic="Native garage message clear failed";return StepResult::unsupported;
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_CHAR_CROUCH) {
        const auto actor=vm.parameter(thread),crouch=vm.parameter(thread),duration=vm.parameter(thread);
        if(!world_ready || !set_char_crouch || !set_char_crouch(actor,crouch!=0,duration)) {
            thread.diagnostic="Native secondary crouch task unavailable";return StepResult::unsupported;
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_CHAR_RUNNING) {
        const auto actor=vm.parameter(thread);const bool running=vm.parameter(thread)!=0;
        if(!world_ready || !execute){thread.diagnostic="Native running objective backend missing";return StepResult::unsupported;}
        const auto exists=execute({0x056D,{actor},{},0,0,true});
        if(!exists.executed || !exists.condition){thread.diagnostic="Native running actor is unavailable";return StepResult::unsupported;}
        const auto current=walking_objectives_.find(actor);
        if(current!=walking_objectives_.end()) {
            auto task=current->second;task.inputs[4]=running?6:4;
            if(!execute(task).executed){thread.diagnostic="Cannot update the native walking objective's move state";return StepResult::unsupported;}
            current->second=std::move(task);
            accepted_walk_tasks_.erase(actor); // Native task was replaced without an acceptance capture.
        }
        running_chars_[actor]=running;observe_source_running(actor,running);return StepResult::continued;
    }
    if(equivalent_entities && opcode==COMMAND_GET_PAD_BUTTON_STATE) {
        const auto selector=vm.parameter(thread);auto cursor=thread;vm.variable(cursor);
        if(selector<0 || selector>44 || !world_ready || !scripted_pad_state) {
            thread.diagnostic="Native scripted input backend missing or invalid selector: "+std::to_string(selector);return StepResult::unsupported;
        }
        const auto value=scripted_pad_state(selector);
        if(!value){thread.diagnostic="Native scripted input query failed: "+std::to_string(selector);return StepResult::unsupported;}
        vm.store(thread,*value);return StepResult::continued;
    }
    if(equivalent_entities && opcode==COMMAND_REGISTER_OUTFIT_CHANGE){++outfit_changes_;return StepResult::continued;}
    if(equivalent_entities && opcode==COMMAND_IS_CHANNEL_PLAYING) {
        const auto channel=vm.parameter(thread);
        if(channel<0 || channel>32767){thread.diagnostic="Invalid original streamed-audio channel: "+std::to_string(channel);return StepResult::unsupported;}
        // All original radio channel streams are currently unmounted and the
        // real native radio is muted by the music policy. This queries the
        // original audio registry, not mission success or native speech slots.
        vm.compare(thread,false);return StepResult::continued;
    }
    if(opcode==COMMAND_GET_RANDOM_COP_IN_AREA) {
        std::array<float,4> area{};std::array<int32_t,5> kinds{};
        for(auto& value:area) {
            const auto bits=vm.parameter(thread);std::memcpy(&value,&bits,4);
            if(!std::isfinite(value))throw std::runtime_error("Nonfinite original cop query area");
        }
        for(auto& value:kinds)value=vm.parameter(thread);
        auto cursor=thread;vm.variable(cursor); // validate destination before effects
        if(!world_ready || !random_cop_in_area) {
            thread.diagnostic="Native cop selection backend missing";return StepResult::unsupported;
        }
        std::array<int32_t,12> donors{};
        for(size_t i=0;i<donors.size();++i) {
            const auto found=models_.find(i<4?int32_t(i+1):int32_t(97+i-4));
            if(found==models_.end() || found->second.kind!=ModelKind::ped) {
                thread.diagnostic="Original police donor binding missing";return StepResult::unsupported;
            }
            donors[i]=found->second.id;
        }
        const auto excluded=last_random_ped_;
        bool committed=false;int32_t selected=-1;
        const auto result=random_cop_in_area(area,kinds,donors,excluded,mission_script && mission_script(thread),
            [&](int32_t handle) {
                if(committed || handle<0 || handle==excluded || last_random_ped_!=excluded)return false;
                committed=true;selected=handle;last_random_ped_=handle;return true;
            });
        if(!result.executed) {
            if(result.native_entered || committed)throw std::runtime_error("Cop query failed after acceptance; do not replay: "+result.diagnostic);
            thread.diagnostic=result.diagnostic;return StepResult::unsupported;
        }
        if(result.values[0]<-1 || (result.values[0]>=0 && (!committed || selected!=result.values[0])) ||
            (result.values[0]==-1 && committed))throw std::runtime_error("Cop selection receipt mismatch");
        vm.store(thread,result.values[0]);return StepResult::continued;
    }
    if(opcode==COMMAND_GET_RANDOM_CHAR_IN_AREA_NO_CHECKS) {
        // Retail 2502B0: four float operands, one output, no LastRandomPedId
        // exclusion; the selection itself becomes the shared LastRandomPedId.
        std::array<float,4> area{};
        for(auto& value:area) {
            const auto bits=vm.parameter(thread);std::memcpy(&value,&bits,4);
            if(!std::isfinite(value))throw std::runtime_error("Nonfinite original unchecked character area");
        }
        auto cursor=thread;vm.variable(cursor); // validate destination before effects
        if(!world_ready || !random_char_unchecked) {
            thread.diagnostic="Native unchecked character selection backend missing";return StepResult::unsupported;
        }
        // Approved SA adaptation (18/10): preserve ambient native AI; only
        // script-owned traffic and gesture/RIOT waits exclude candidates.
        // An unmounted gesture owner cannot produce waits; a mounted owner
        // must expose its query, and an unavailable query remains unknown.
        if(play_wait_animation && !wait_animation_active) {
            thread.diagnostic="Original wait-state authority unavailable";return StepResult::unsupported;
        }
        const auto waiting=[this](int32_t handle)->std::optional<bool> {
            if(traffic_waits_.count(handle))return true;
            if(!wait_animation_active)return false;
            return wait_animation_active(handle);
        };
        bool committed=false;int32_t selected=-1;
        const auto result=random_char_unchecked(area,mission_script && mission_script(thread),waiting,
            [&](int32_t handle) {
                if(committed || handle<0)return false;
                committed=true;selected=handle;last_random_ped_=handle;return true;
            });
        if(!result.executed) {
            if(result.native_entered || committed)throw std::runtime_error("Unchecked character query failed after acceptance; do not replay: "+result.diagnostic);
            thread.diagnostic=result.diagnostic;return StepResult::unsupported;
        }
        if(result.values[0]<-1 || (result.values[0]>=0 && (!committed || selected!=result.values[0])) ||
            (result.values[0]==-1 && committed))throw std::runtime_error("Unchecked character selection receipt mismatch");
        observe_source_actor_acquired(result.values[0]);
        vm.store(thread,result.values[0]);return StepResult::continued;
    }
    if(opcode==COMMAND_GET_RANDOM_CHAR_IN_ZONE) {
        // Retail Script3: zone label first, then civilian/gang/criminal flags and one output.
        // The original does not advance past an unknown label (undefined); it is refused here.
        const auto label=lower(vm.text_label(thread));
        std::array<int32_t,3> kinds{};
        for(auto& kind:kinds)kind=vm.parameter(thread);
        auto cursor=thread;vm.variable(cursor); // validate the destination before effects
        if(!world_ready || !random_char_in_zone || !char_in_control){thread.diagnostic="Native random character zone backend missing";return StepResult::unsupported;}
        const auto zone=std::find_if(zones_.begin(),zones_.end(),[&](const OriginalZone& z){return z.type==0 && z.name==label;});
        if(zone==zones_.end()){thread.diagnostic="Unknown original navigation zone: "+label;return StepResult::unsupported;}
        bool committed=false;int32_t selected=-1;
        const auto result=random_char_in_zone(zone->bounds,kinds,last_random_ped_,mission_script && mission_script(thread),char_in_control,
            [&](int32_t handle) {
                if(committed || handle<0)return false;
                committed=true;selected=handle;last_random_ped_=handle;return true;
            });
        if(!result.executed) {
            if(result.native_entered || committed)throw std::runtime_error("Zone character query failed after acceptance; do not replay: "+result.diagnostic);
            thread.diagnostic=result.diagnostic;return StepResult::unsupported;
        }
        if(result.values[0]<-1 || (result.values[0]>=0 && (!committed || selected!=result.values[0])) ||
            (result.values[0]==-1 && committed))throw std::runtime_error("Zone character selection receipt mismatch");
        observe_source_actor_acquired(result.values[0]);
        vm.store(thread,result.values[0]);return StepResult::continued;
    }
    if(opcode==COMMAND_IS_SNIPER_BULLET_IN_AREA) {
        std::array<float,6> bounds{};
        for(auto& coordinate:bounds) {
            coordinate=entity_float(vm.parameter(thread));
            if(!std::isfinite(coordinate))throw std::runtime_error("Nonfinite original sniper query bounds");
        }
        for(size_t axis=0;axis<3;++axis)if(bounds[axis]>bounds[axis+3])std::swap(bounds[axis],bounds[axis+3]);
        if(!world_ready || !sniper_bullet_in_area){thread.diagnostic="Original sniper bullet producer unavailable";return StepResult::unsupported;}
        const auto result=sniper_bullet_in_area(bounds);
        if(!result){thread.diagnostic="Original sniper bullet snapshot unavailable";return StepResult::unsupported;}
        vm.compare(thread,*result);return StepResult::continued;
    }
    if(opcode==COMMAND_GET_RANDOM_CHAR_IN_AREA) {
        std::array<float,4> area{};std::array<int32_t,3> kinds{};
        for(auto& v:area){const auto b=vm.parameter(thread);std::memcpy(&v,&b,4);if(!std::isfinite(v))throw std::runtime_error("Nonfinite original random character area");}
        for(auto& v:kinds)v=vm.parameter(thread);
        auto cursor=thread;vm.variable(cursor);
        if(!world_ready || !random_char_in_area){thread.diagnostic="Native random character selection backend missing";return StepResult::unsupported;}
        const auto handle=random_char_in_area(area,kinds,last_random_ped_,mission_script && mission_script(thread));
        if(!handle){thread.diagnostic="Native random character selection failed";return StepResult::unsupported;}
        if(*handle>=0)last_random_ped_=*handle;
        observe_source_actor_acquired(*handle);
        vm.store(thread,*handle);return StepResult::continued;
    }
    if(opcode==COMMAND_SET_ALLOWED_COLLISION) {
        const auto first=vm.parameter(thread),second=vm.parameter(thread);
        if(!world_ready || !set_allowed_collision) {
            thread.diagnostic="Native island collision residency controller unavailable";
            return StepResult::unsupported;
        }
        const auto result=set_allowed_collision(first,second);
        if(!result.executed) {
            if(result.native_entered)throw std::runtime_error("Collision residency entered before failure; replay rejected: "+result.diagnostic);
            thread.diagnostic=result.diagnostic.empty()?"Native island collision residency refused":result.diagnostic;
            return StepResult::unsupported;
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_ENTER_CAR_RANGE_MULTIPLIER) {
        const auto value=source_enter_car_range_multiplier(entity_float(vm.parameter(thread)));
        // Source global initialization can precede world/actor admission.
        if(!value || !set_enter_car_range_multiplier || !set_enter_car_range_multiplier(*value)) {
            thread.diagnostic="Source vehicle-entry range is invalid or its controller is unavailable";
            return StepResult::unsupported;
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_CHAR_FRIGHTENED_IN_JACKED_CAR) {
        const auto actor=vm.parameter(thread);
        const bool active=vm.parameter(thread)!=0; // PS2 retail24EC24 branches on zero/nonzero.
        if(!world_ready || !set_char_jack_protection) {
            thread.diagnostic="Native passenger jack-protection consumer unavailable";
            return StepResult::unsupported;
        }
        const auto result=set_char_jack_protection(actor,active);
        if(!result.executed) {
            if(result.native_entered)throw std::runtime_error("Passenger jack protection entered before failure; replay rejected: "+result.diagnostic);
            thread.diagnostic=result.diagnostic.empty()?"Native passenger jack protection refused":result.diagnostic;
            return StepResult::unsupported;
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_OVERRIDE_CHAR_MOVE_ANIM) {
        const auto ped=vm.parameter(thread);
        if(!world_ready || !override_char_move_anim || !clear_char_move_anim_override) {
            thread.diagnostic="Native own-group locomotion policy unavailable";
            return StepResult::unsupported;
        }
        const auto result=override_char_move_anim(ped);
        if(!result.executed) {
            if(result.native_entered)throw std::runtime_error("Move animation override entered before failure; replay rejected: "+result.diagnostic);
            thread.diagnostic=result.diagnostic.empty()?"Native move animation override refused":result.diagnostic;
            return StepResult::unsupported;
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_IS_CHAR_OBJ_NO_OBJ) {
        const auto ped=vm.parameter(thread);
        if(!world_ready || !char_has_no_objective) {
            thread.diagnostic="Native script-objective state unavailable";
            return StepResult::unsupported;
        }
        const auto none=char_has_no_objective(ped);
        if(!none) {
            thread.diagnostic="Native current or suspended script objective could not be read safely";
            return StepResult::unsupported;
        }
        vm.compare(thread,*none);return StepResult::continued;
    }
    if(opcode==COMMAND_SET_CAR_IS_REWARD) {
        const auto car=vm.parameter(thread);const bool enabled=vm.parameter(thread)!=0;
        if(!world_ready || !set_car_reward){thread.diagnostic="Native reward vehicle storage consumer unavailable";return StepResult::unsupported;}
        const auto result=set_car_reward(car,enabled);
        if(!result.executed) {
            if(result.native_entered)throw std::runtime_error("Reward vehicle policy changed before failure; replay rejected: "+result.diagnostic);
            thread.diagnostic=result.diagnostic.empty()?"Native reward vehicle refused before entry":result.diagnostic;
            return StepResult::unsupported;
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_CHAR_AS_PLAYER_FRIEND) {
        const auto ped=vm.parameter(thread);
        vm.parameter(thread); // Unused in the source retail handler.
        const bool friendly=vm.parameter(thread)!=0;
        if(!world_ready || !set_char_player_friend) {
            thread.diagnostic="Native player relationship consumer unavailable or refused";
            return StepResult::unsupported;
        }
        const auto result=set_char_player_friend(ped,friendly);
        if(!result.executed) {
            if(result.native_entered)throw std::runtime_error("Player relationship changed before failure; replay rejected: "+result.diagnostic);
            thread.diagnostic=result.diagnostic.empty()?"Native player relationship refused before entry":result.diagnostic;
            return StepResult::unsupported;
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_GET_CHAR_THREAT_CHAR) {
        const auto ped=vm.parameter(thread);auto destination=thread;vm.variable(destination);
        if(!world_ready || !get_char_threat_char){thread.diagnostic="Native current threat event consumer unavailable";return StepResult::unsupported;}
        const auto threat=get_char_threat_char(ped);
        if(!threat){thread.diagnostic="Native current threat event could not be read safely";return StepResult::unsupported;}
        vm.store(thread,*threat);return StepResult::continued;
    }
    if(opcode==COMMAND_SET_CHAR_ATTACK_TIMER || opcode==COMMAND_SET_CHAR_SHOOT_TIMER) {
        const auto& setter=opcode==COMMAND_SET_CHAR_ATTACK_TIMER?set_char_attack_timer:set_char_shoot_timer;
        if(!world_ready || !setter){thread.diagnostic="Native combat timer consumer unavailable";return StepResult::unsupported;}
        const auto ped=vm.parameter(thread);const auto raw=vm.parameter(thread);
        if(!setter(ped,uint32_t(raw))){thread.diagnostic="Native combat timer consumer refused the live actor or hook ownership";return StepResult::unsupported;}
        return StepResult::continued;
    }
    if(opcode==COMMAND_SWITCH_SECURITY_CAMERA) {
        if(!world_ready || !switch_security_camera){thread.diagnostic="Native security-camera effects unavailable";return StepResult::unsupported;}
        const bool enabled=vm.parameter(thread)!=0;
        const auto result=switch_security_camera(enabled);
        if(!result.executed){
            if(result.native_entered)throw std::runtime_error("Security-camera effects changed before failure; replay rejected: "+result.diagnostic);
            thread.diagnostic="Security-camera effects refused: "+result.diagnostic;return StepResult::unsupported;
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_BRIDGE_STATE) {
        const auto state=vm.parameter(thread);
        if(!world_ready || !set_bridge_state || state<0 || state>2){
            thread.diagnostic="LCS lift bridge backend or requested state unavailable";return StepResult::unsupported;
        }
        const auto result=set_bridge_state(state);
        if(!result.executed){
            if(result.native_entered)throw std::runtime_error("Lift bridge changed before failure; replay rejected: "+result.diagnostic);
            thread.diagnostic="Lift bridge state refused: "+result.diagnostic;return StepResult::unsupported;
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_PAD_STICKS_MULTIPLIER) {
        // The original two pad floats have no reader in the reviewed PS2
        // version/source. SA uses those offsets for unrelated shorts.
        const auto first=bits_float(vm.parameter(thread)),second=bits_float(vm.parameter(thread));
        if(!std::isfinite(first) || !std::isfinite(second)) {
            thread.diagnostic="Stick multiplier operands are not finite";return StepResult::unsupported;
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_GANG_ATTACK_PLAYER_WITH_COPS) {
        const auto gang=vm.parameter(thread);(void)vm.parameter(thread);
        if(gang<0 || gang>8) {
            thread.diagnostic="Gang index is outside the nine retail gangs";return StepResult::unsupported;
        }
        // The original changes ambient LCS threat reactions in CivilianPed.
        // Project policy keeps SA ambient AI: decode both states, with no
        // claim that their distinct LCS reactions have been ported.
        return StepResult::continued;
    }
    if(opcode==COMMAND_LINE || opcode==COMMAND_1476 || opcode==COMMAND_1627) {
        // PS2 retail: LINE collects6 raw parameters then returns;1476/1627
        // branch directly to the successful epilogue without collecting any.
        // Decode-only compatibility, no renderer/debug effect or functional credit.
        if(opcode==COMMAND_LINE)for(size_t i=0;i<6;++i)(void)vm.parameter(thread);
        return StepResult::continued;
    }
    if(opcode==COMMAND_SAVE_INT_TO_DEBUG_FILE || opcode==COMMAND_SAVE_NEWLINE_TO_DEBUG_FILE) {
        // Retail PS2 clauses consume one integer / no operand and perform no I/O.
        // These MAIN helpers are mission-reachable: preserve decoding and condition,
        // without creating a debug service or counting compatibility as a port.
        if(opcode==COMMAND_SAVE_INT_TO_DEBUG_FILE)(void)vm.parameter(thread);
        return StepResult::continued;
    }
    if(opcode==COMMAND_CLEAR_NAVIGATION_ARROW) {
        // PS2 handler 24F7D4 calls 151890 (ClearTarget): the single cNavArrow target flag is zeroed.
        // Only 1452 sets it; MAIN.SCM has no 1452 and this runtime has no navigation-arrow producer.
        // Compatibility for the current content's already-empty state, not a functional arrow port.
        // If 1452 is implemented, bind both commands to that real renderer/owner. Leave blips alone.
        return StepResult::continued;
    }
    if(opcode==COMMAND_LOAD_END_OF_GAME_TUNE) {
        // The original clause takes no operands and has no effect (PS2 handler 2692D0 branches to the
        // epilogue with v0=0; reLCS returns 0): the tune is a mounted stream selected later by PLAY.
        // Decode-only compatibility: no invented readiness gate or state mutation. PLAY retains its
        // real owned-audio readiness checks. This empty retail clause receives no functional-port credit.
        return StepResult::continued;
    }
    if(opcode==COMMAND_PLAY_END_OF_GAME_TUNE || opcode==COMMAND_STOP_END_OF_GAME_TUNE) {
        if(!world_ready || !ending_tune){thread.diagnostic="Original finale stream backend unavailable";return StepResult::unsupported;}
        const bool play=opcode==COMMAND_PLAY_END_OF_GAME_TUNE;
        const auto result=ending_tune(play);
        if(!result.executed) {
            if(result.native_entered)throw std::runtime_error("Finale stream entered failure: "+result.diagnostic);
            thread.diagnostic="Original finale stream refused: "+result.diagnostic;return StepResult::unsupported;
        }
        original_music_mode_=play?0:1;return StepResult::continued;
    }
    if(equivalent_entities && (opcode==COMMAND_SWITCH_CAR_RADIO || opcode==COMMAND_SET_RADIO_CHANNEL)) {
        const auto value=vm.parameter(thread);
        const auto seek=opcode==COMMAND_SET_RADIO_CHANNEL ? vm.parameter(thread) : -1;
        if(!execute || (opcode==COMMAND_SWITCH_CAR_RADIO ? value<0 || value>4 : value<0 || value>10)) {
            thread.diagnostic="Invalid original music/radio policy";return StepResult::unsupported;
        }
        // LCS radio streams are not mounted. Keep real native music off during
        // the original mission/cutscene speech, matching the working opening
        // profile, and retain both original station/seek and music mode.
        const auto r=execute({0x041E,{13}});
        if(!r.executed){thread.diagnostic="Native mission radio control failed";return StepResult::unsupported;}
        if(opcode==COMMAND_SET_RADIO_CHANNEL){original_radio_channel_=value;original_radio_seek_=seek;}
        else {
            if(change_music_mode) {
                const auto mode=change_music_mode(value);
                if(!mode.executed)throw std::runtime_error("Music mode failed after native radio entry: "+mode.diagnostic);
            }
            original_music_mode_=value;
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_CHAR_THREAT_SEARCH || opcode==COMMAND_CLEAR_CHAR_THREAT_SEARCH) {
        const auto actor=vm.parameter(thread);
        const auto mask=opcode==COMMAND_SET_CHAR_THREAT_SEARCH ? uint32_t(vm.parameter(thread)) : 0u;
        if(!world_ready || !execute){thread.diagnostic="Native character relationships unavailable";return StepResult::unsupported;}
        const auto exists=execute({0x056D,{actor},{},0,0,true});
        if(!exists.executed || !exists.condition){thread.diagnostic="Threat policy requires an existing native character";return StepResult::unsupported;}
        if(opcode==COMMAND_CLEAR_CHAR_THREAT_SEARCH) {
            if(!execute({0x077C,{actor,4}}).executed){thread.diagnostic="Cannot clear native character hostility";return StepResult::unsupported;}
        } else {
            for(int32_t bit=0;bit<=24;++bit)if(mask&(1u<<bit)) {
                int32_t target=bit<=15 ? bit : bit==16 ? 18 : bit==17 ? 22 : bit==18 ? 20 : bit==19 ? 23 : bit==24 ? 19 : -1;
                const auto custom=ped_types_.find(target);if(custom!=ped_types_.end())target=custom->second;
                if(target<0)continue; // non-character stimuli have no native relationship slot
                if(!execute({0x077A,{actor,4,target}}).executed){thread.diagnostic="Cannot set native character hostility";return StepResult::unsupported;}
            }
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_THREAT_FOR_PED_TYPE || opcode==COMMAND_CLEAR_THREAT_FOR_PED_TYPE) {
        const auto original_type=vm.parameter(thread);const auto mask=uint32_t(vm.parameter(thread));
        const auto translate_type=[&](int32_t type)->int32_t {
            const auto custom=ped_types_.find(type);if(custom!=ped_types_.end())return custom->second;
            static constexpr int32_t types[]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,18,19,20,-1,22,23,-1};
            return type>=0 && size_t(type)<std::size(types)?types[type]:-1;
        };
        const auto source=translate_type(original_type);
        if(!world_ready || !execute || source<0){thread.diagnostic="Invalid native gang relationship translation type="+std::to_string(original_type)+" mask="+std::to_string(mask);return StepResult::unsupported;}
        const bool add=opcode==COMMAND_SET_THREAT_FOR_PED_TYPE;
        // PedType.h flags are not identical to ped type indices after gang 9.
        // Threats to weapons/explosions stay original environmental settings;
        // hostility to actual peds updates SA's real relationship database.
        for(int32_t bit=0;bit<=24;++bit)if(mask&(1u<<bit)) {
            int32_t target=-1;
            if(bit<=15)target=translate_type(bit);
            else if(bit==16)target=18;else if(bit==17)target=22;else if(bit==18)target=20;
            else if(bit==19)target=23;else if(bit==24)target=19;
            if(target<0)continue;
            const auto r=execute({uint16_t(add?0x0746:0x0747),{4,source,target}});
            if(!r.executed){thread.diagnostic="Native gang relationship update failed";return StepResult::unsupported;}
        }
        if(add)nonped_threats_[original_type]|=mask;else nonped_threats_[original_type]&=~mask;
        return StepResult::continued;
    }
    if(flat_native_world && opcode==COMMAND_SET_EXTRA_COLOUR_LIGHT_DIRECTION) {
        // Preserve the original scene lighting request, as the working opening
        // profile does. Native SA timecycle owns the rendered light direction.
        for(auto& value:scene_light_direction_){const auto bits=vm.parameter(thread);std::memcpy(&value,&bits,4);if(!std::isfinite(value))throw std::runtime_error("Nonfinite original scene light direction");}
        return StepResult::continued;
    }
    if(opcode==COMMAND_IS_CHAR_IN_CONTROL || opcode==COMMAND_IS_PLAYER_MADE_SAFE) {
        std::optional<bool> result;
        if(opcode==COMMAND_IS_CHAR_IN_CONTROL){const auto ped=vm.parameter(thread);if(char_in_control)result=char_in_control(ped);}
        else if(player_made_safe)result=player_made_safe();
        if(!result){thread.diagnostic="Native character/control state backend unavailable";return StepResult::unsupported;}
        vm.compare(thread,*result);return StepResult::continued;
    }
    if(opcode==COMMAND_IS_PLAYER_IN_TAXI) {
        const auto player=vm.parameter(thread);
        if(!world_ready || !execute){thread.diagnostic="Native vehicle query requires the mounted world";return StepResult::unsupported;}
        const auto ped=execute({0x01F5,{player},{},0,1});
        const auto in_car=ped.executed ? execute({0x00DF,{ped.values[0]},{},0,0,true}) : NativeResult{};
        if(!in_car.executed){thread.diagnostic="Native vehicle occupancy query failed";return StepResult::unsupported;}
        if(!in_car.condition){vm.compare(thread,false);return StepResult::continued;}
        const auto car=execute({0x03C0,{ped.values[0]},{},0,1});
        const auto car_model=car.executed ? execute({0x0441,{car.values[0]},{},0,1}) : NativeResult{};
        if(!car_model.executed){thread.diagnostic="Native vehicle model query failed";return StepResult::unsupported;}
        bool matches=false;for(const auto original:{151,166,181}){const auto taxi=binding(original);if(taxi && taxi->kind==ModelKind::vehicle && taxi->id==car_model.values[0])matches=true;}
        vm.compare(thread,matches);return StepResult::continued;
    }
    if(opcode==COMMAND_IS_PLAYER_IN_ZONE || opcode==COMMAND_IS_CHAR_IN_ZONE || (flat_native_world && opcode==COMMAND_IS_COLLISION_IN_MEMORY)) {
        const auto argument=vm.parameter(thread);
        const bool named_zone=opcode==COMMAND_IS_PLAYER_IN_ZONE || opcode==COMMAND_IS_CHAR_IN_ZONE;
        const auto label=named_zone ? lower(vm.text_label(thread)) : std::string{};
        if(!world_ready || zones_.empty() || !execute){thread.diagnostic="Original region query requires mounted world zones";return StepResult::unsupported;}
        NativeResult ped;ped.executed=true;ped.values[0]=argument;
        if(opcode!=COMMAND_IS_CHAR_IN_ZONE)ped=execute({0x01F5,{opcode==COMMAND_IS_PLAYER_IN_ZONE ? argument : 0},{},0,1});
        if(!ped.executed){thread.diagnostic="Cannot resolve native player for original region query";return StepResult::unsupported;}
        const auto coordinates=execute({0x00A0,{ped.values[0]},{},0,3});
        if(!coordinates.executed){thread.diagnostic="Cannot read native player position for original region query";return StepResult::unsupported;}
        std::array<float,3> position{};for(int i=0;i<3;++i){std::memcpy(&position[i],&coordinates.values[i],4);if(!std::isfinite(position[i]))return StepResult::unsupported;}
        const auto contains=[&](const OriginalZone& zone){for(int i=0;i<3;++i)if(position[i]<zone.bounds[i] || position[i]>zone.bounds[i+3])return false;return true;};
        if(named_zone) {
            const auto zone=std::find_if(zones_.begin(),zones_.end(),[&](const OriginalZone& z){return z.type==0 && z.name==label;});
            if(zone==zones_.end()){thread.diagnostic="Unknown original navigation zone: "+label;return StepResult::unsupported;}
            vm.compare(thread,contains(*zone));
        } else {
            // Original Script4 queries CGame::currLevel, not a collision pointer.
            // All native island collision is mounted in area zero; keep LCS's
            // spatial island identity from the original ordered map-zone list.
            int32_t level=0;for(const auto& zone:zones_)if(zone.type==3 && contains(zone)){level=zone.level;break;}
            vm.compare(thread,level==argument);
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_SWITCH_PED_ROADS_OFF_ANGLED || opcode==COMMAND_SWITCH_PED_ROADS_ON_ANGLED ||
       opcode==COMMAND_SWITCH_ROADS_OFF_ANGLED || opcode==COMMAND_SWITCH_ROADS_ON_ANGLED) {
        std::array<float,7> region{};
        for(auto& value:region){const auto bits=vm.parameter(thread);std::memcpy(&value,&bits,4);if(!std::isfinite(value))throw std::runtime_error("Nonfinite angled road boundary");}
        const bool cars=opcode==COMMAND_SWITCH_ROADS_OFF_ANGLED || opcode==COMMAND_SWITCH_ROADS_ON_ANGLED;
        const bool off=opcode==COMMAND_SWITCH_ROADS_OFF_ANGLED || opcode==COMMAND_SWITCH_PED_ROADS_OFF_ANGLED;
        if(!world_ready || !switch_angled_roads || !switch_angled_roads(region,cars,off)) {
            thread.diagnostic="Original angled road restriction requires the native path backend";return StepResult::unsupported;
        }
        return StepResult::continued;
    }
    if(flat_native_world && (opcode==COMMAND_MARK_ROADS_BETWEEN_LEVELS || opcode==COMMAND_MARK_PED_ROADS_BETWEEN_LEVELS)) {
        std::array<float,6> region{};
        for(auto& value:region){const auto bits=vm.parameter(thread);std::memcpy(&value,&bits,4);if(!std::isfinite(value))throw std::runtime_error("Nonfinite original island road boundary");}
        for(int i=0;i<3;++i)if(region[i]>region[i+3])std::swap(region[i],region[i+3]);
        level_road_regions_[opcode==COMMAND_MARK_PED_ROADS_BETWEEN_LEVELS].push_back(region);
        // The mounted world keeps all three islands in one native area. These
        // boundaries annotate LCS's level streamer, not a route/arrival result.
        return StepResult::continued;
    }
    if(equivalent_entities && (opcode==COMMAND_SET_ZONE_PED_INFO || opcode==COMMAND_SET_ZONE_CAR_INFO || opcode==COMMAND_SET_ZONE_CIVILIAN_CAR_INFO || opcode==COMMAND_SET_ZONE_GROUP)) {
        const auto zone=lower(vm.text_label(thread));
        if(opcode==COMMAND_SET_ZONE_GROUP) {
            const auto day=vm.parameter(thread),group=vm.parameter(thread);
            zone_population_[zone].groups[day!=0]=group;
        } else {
            std::array<int32_t,12> values{};for(auto& value:values)value=vm.parameter(thread);
            auto& entry=zone_population_[zone];const auto day=values[0]!=0;
            if(opcode==COMMAND_SET_ZONE_PED_INFO)entry.pedestrians[day]=values;
            else if(opcode==COMMAND_SET_ZONE_CAR_INFO)entry.cars[day]=values;
            else entry.civilians[day]=values;
        }
        // Original day/night population data stays in the LCS campaign bank.
        // SA's zone population layout is different. These are declarations;
        // native mission spawn/tasks remain driven by their original scripts.
        return StepResult::continued;
    }
    if(equivalent_entities && (opcode==COMMAND_SET_GANG_PED_MODELS || opcode==COMMAND_SET_GANG_CAR_MODEL || opcode==COMMAND_SET_GANG_WEAPONS)) {
        const auto gang=vm.parameter(thread),first=vm.parameter(thread);
        const auto second=opcode==COMMAND_SET_GANG_CAR_MODEL ? 0 : vm.parameter(thread);
        if(gang<0 || gang>=9){thread.diagnostic="Original gang ID is outside the nine campaign gangs";return StepResult::unsupported;}
        if(opcode==COMMAND_SET_GANG_WEAPONS) {
            if(first<0 || second<0 || size_t(first)>=std::size(weapon_counterparts) || size_t(second)>=std::size(weapon_counterparts))return StepResult::unsupported;
            const auto r=execute({0x0237,{gang,weapon_counterparts[first],weapon_counterparts[second],weapon_counterparts[second]}});
            if(!r.executed){thread.diagnostic="Native gang weapon configuration failed";return StepResult::unsupported;}
        } else {
            const auto a=binding(first),b=opcode==COMMAND_SET_GANG_PED_MODELS ? binding(second) : a;
            const auto kind=opcode==COMMAND_SET_GANG_PED_MODELS ? ModelKind::ped : ModelKind::vehicle;
            if(!a || !b || a->kind!=kind || b->kind!=kind){thread.diagnostic="Original gang entity configuration lacks a typed equivalent";return StepResult::unsupported;}
            // LCS owns its gang population selectors; mission factories already
            // resolve these same originals to native entities. SA's similarly
            // named 0235/0236 are NOPs and must never be used as implementations.
            if(opcode==COMMAND_SET_GANG_PED_MODELS)gang_models_[gang]={first,second};
            else gang_cars_[gang]=first;
        }
        return StepResult::continued;
    }
    if(equivalent_entities && opcode==COMMAND_LOAD_SPECIAL_MODEL) {
        const auto original=vm.parameter(thread);const auto resource=vm.text_label(thread);
        const auto target=binding(original);
        if(!target){thread.diagnostic="Equivalent special model is not configured: "+resource;return StepResult::unsupported;}
        const auto r=execute({0x0247,{target->id}});
        if(!r.executed){thread.diagnostic="Equivalent model request failed";return StepResult::unsupported;}
        return StepResult::continued;
    }
    if((equivalent_entities || begin_outfit) && opcode==COMMAND_STORE_PLAYER_OUTFIT) {
        stored_outfit_=vm.text_label(thread);return StepResult::continued;
    }
    if((equivalent_entities || current_outfit) && opcode==COMMAND_IS_PLAYER_WEARING) {
        const auto player=vm.parameter(thread);const auto label=lower(vm.text_label(thread));
        if(!world_ready || !execute){thread.diagnostic="Outfit identity requires the native player";return StepResult::unsupported;}
        const auto actor=execute({0x01F5,{player},{},0,1});
        if(!actor.executed || actor.values[0]<0){thread.diagnostic="Cannot read native outfit actor";return StepResult::unsupported;}
        if(current_outfit) {
            const auto actual=current_outfit(actor.values[0]);
            vm.compare(thread,actual && lower(*actual)==label);return StepResult::continued;
        }
        const auto model=execute({0x0665,{actor.values[0]},{},0,1});
        if(!model.executed){thread.diagnostic="Cannot read native outfit model";return StepResult::unsupported;}
        const auto worn=worn_outfits_.find(actor.values[0]);
        vm.compare(thread,worn!=worn_outfits_.end() && worn->second.native_model==model.values[0] && worn->second.label==label);
        return StepResult::continued;
    }
    if((equivalent_entities || begin_outfit) && (opcode==COMMAND_UNDRESS_CHAR || opcode==COMMAND_DRESS_CHAR)) {
        const auto ped=vm.parameter(thread);
        const auto outfit=opcode==COMMAND_UNDRESS_CHAR ? vm.text_label(thread) : std::string{};
        const auto exists=execute({0x056D,{ped},{},0,0,true});
        if(!exists.executed || !exists.condition){thread.diagnostic="Outfit change requires an existing native character";return StepResult::unsupported;}
        if(opcode==COMMAND_DRESS_CHAR && !equivalent_outfits_.count(ped)){thread.diagnostic="No original outfit change is pending";return StepResult::unsupported;}
        if(opcode==COMMAND_UNDRESS_CHAR && begin_outfit && !begin_outfit(ped,lower(outfit))) {
            thread.diagnostic="Original Toni outfit could not be requested: "+outfit;return StepResult::unsupported;
        }
        if(opcode==COMMAND_DRESS_CHAR && finish_outfit && !finish_outfit(ped)) {
            execute({0x0337,{ped,1}});
            thread.diagnostic="Original Toni outfit is not loaded/committed; previous actor retained";return StepResult::unsupported;
        }
        const auto visible=execute({0x0337,{ped,opcode==COMMAND_DRESS_CHAR ? 1 : 0}});
        if(!visible.executed){thread.diagnostic="Native outfit visibility transition failed";return StepResult::unsupported;}
        if(opcode==COMMAND_UNDRESS_CHAR)equivalent_outfits_[ped]=outfit;
        else {
            const auto model=execute({0x0665,{ped},{},0,1});
            if(!model.executed){thread.diagnostic="Native outfit model identity is unavailable";return StepResult::unsupported;}
            const auto clear=execute({0x0687,{ped}});
            if(!clear.executed){thread.diagnostic="Native outfit objective reset failed";return StepResult::unsupported;}
            forget_walking_objective(ped);
            worn_outfits_[ped]={lower(equivalent_outfits_.at(ped)),model.values[0]};
            equivalent_outfits_.erase(ped);
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_IS_FINAL_GAME || opcode==COMMAND_IS_E3_BUILD || opcode==COMMAND_IS_DEVELOPER) {
        // This bridge runs the supplied retail LCS campaign, whose FINAL branch
        // differs from the development/E3 mission selectors. These are build
        // configuration queries, not mission objective or completion results.
        vm.compare(thread,opcode==COMMAND_IS_FINAL_GAME || (opcode==COMMAND_IS_DEVELOPER && developer_flag_));
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_DEVELOPER_FLAG) {
        developer_flag_=vm.parameter(thread)!=0;return StepResult::continued;
    }
    if(opcode==COMMAND_DECLARE_FORT_STAUNTON_DESTROYED_FLAG) {
        fort_staunton_flag_=vm.global_reference(thread);return StepResult::continued;
    }
    if(opcode==COMMAND_SET_TOTAL_NUMBER_OF_KILL_FRENZIES || opcode==COMMAND_SET_TOTAL_EXPORT_CARS) {
        const auto value=vm.parameter(thread);
        if(opcode==COMMAND_SET_TOTAL_NUMBER_OF_KILL_FRENZIES)kill_frenzies_total_=value;
        else export_cars_total_=value;
        // These LCS totals have no corresponding SA statistic. Keep the actual
        // original declaration, separately from native passed/collected counts.
        return StepResult::continued;
    }
    if(opcode==COMMAND_ADD_MONEY_SPENT_ON_WEAPONS) {
        const auto amount=vm.parameter(thread);
        if(!execute){thread.diagnostic="Native weapon expenditure statistic is unavailable";return StepResult::unsupported;}
        // LCS CStats::MoneySpentOnWeapons adds a signed integer to its float
        // WeaponBudget. SA's INCREMENT_STAT rejects negative amounts. Read and
        // assign the real float budget instead, preserving the original add
        // and never debiting player cash a second time.
        const auto current=execute({0x0653,{13},{},0,1}); // STAT_WEAPON_BUDGET
        if(!current.executed){thread.diagnostic="Cannot read native weapon expenditure: "+current.diagnostic;return StepResult::unsupported;}
        float budget=0;std::memcpy(&budget,&current.values[0],4);
        budget+=static_cast<float>(amount);
        if(!std::isfinite(budget)){thread.diagnostic="Non-finite native weapon expenditure statistic";return StepResult::unsupported;}
        int32_t value=0;std::memcpy(&value,&budget,4);
        const auto updated=execute({0x062A,{13,value}});
        if(!updated.executed){thread.diagnostic="Cannot assign native weapon expenditure: "+updated.diagnostic;return StepResult::unsupported;}
        return StepResult::continued;
    }
    if((opcode==COMMAND_SET_UNIQUE_JUMPS_FOUND || opcode==COMMAND_REGISTER_UNIQUE_JUMP_FOUND || opcode==COMMAND_ADD_EXPORTED_CARS) && execute) {
        const auto value=opcode==COMMAND_REGISTER_UNIQUE_JUMP_FOUND?1:vm.parameter(thread);
        const int32_t stat=opcode==COMMAND_ADD_EXPORTED_CARS?213:144; // primary SA eStats
        const auto native=opcode==COMMAND_SET_UNIQUE_JUMPS_FOUND?uint16_t(0x0629):uint16_t(0x0623);
        const auto result=execute({native,{stat,value}});
        if(!result.executed){thread.diagnostic="Cannot update the actual native campaign statistic";return StepResult::unsupported;}
        return StepResult::continued;
    }
    if(opcode==COMMAND_REGISTER_JUMP_DISTANCE || opcode==COMMAND_REGISTER_JUMP_HEIGHT ||
       opcode==COMMAND_REGISTER_JUMP_FLIPS || opcode==COMMAND_REGISTER_JUMP_SPINS || opcode==COMMAND_REGISTER_JUMP_STUNT) {
        const auto value=vm.parameter(thread);
        const bool floating=opcode==COMMAND_REGISTER_JUMP_DISTANCE || opcode==COMMAND_REGISTER_JUMP_HEIGHT;
        int32_t stat=opcode==COMMAND_REGISTER_JUMP_DISTANCE?139:opcode==COMMAND_REGISTER_JUMP_HEIGHT?140:
            opcode==COMMAND_REGISTER_JUMP_FLIPS?141:opcode==COMMAND_REGISTER_JUMP_SPINS?142:143;
        float real_value=0;
        if(floating){std::memcpy(&real_value,&value,4);if(!std::isfinite(real_value)){thread.diagnostic="Non-finite original jump record";return StepResult::unsupported;}}
        // Native eStats 139-143 are the same five records. REGISTER_FLOAT_STAT
        // takes real metres before native SA stores its integer display value;
        // retain LCS's full-precision records separately, never reinterpret bits.
        NativeCall call;call.opcode=floating?0x0628:0x0627;call.inputs={stat,value};
        if(!execute || !execute(call).executed){thread.diagnostic="Cannot register the actual native jump record";return StepResult::unsupported;}
        if(opcode==COMMAND_REGISTER_JUMP_DISTANCE)jump_records_.distance=std::max(jump_records_.distance,real_value);
        else if(opcode==COMMAND_REGISTER_JUMP_HEIGHT)jump_records_.height=std::max(jump_records_.height,real_value);
        else if(opcode==COMMAND_REGISTER_JUMP_FLIPS)jump_records_.flips=std::max(jump_records_.flips,value);
        else if(opcode==COMMAND_REGISTER_JUMP_SPINS)jump_records_.spins=std::max(jump_records_.spins,value);
        else jump_records_.best_stunt=std::max(jump_records_.best_stunt,value);
        return StepResult::continued;
    }
    if((opcode==COMMAND_SET_HIDDEN_PACKAGES_COLLECTED || opcode==COMMAND_SET_COLLECTABLE1_TOTAL || opcode==COMMAND_GET_COLLECTABLE1S_COLLECTED) && world_ready) {
        std::optional<int32_t> setting;
        if(opcode==COMMAND_GET_COLLECTABLE1S_COLLECTED){auto cursor=thread;vm.variable(cursor);}
        else setting=vm.parameter(thread);
        if(!collectables){thread.diagnostic="Native player collectable counters are unavailable";return StepResult::unsupported;}
        const auto value=collectables(opcode==COMMAND_SET_COLLECTABLE1_TOTAL,setting);
        if(!value){thread.diagnostic="Cannot access current native player collectable counters";return StepResult::unsupported;}
        if(!setting)vm.store(thread,*value);
        return StepResult::continued;
    }
    if(opcode==COMMAND_ACTIVATE_CRUSHER_CRANE && world_ready) {
        std::array<float,10> p{};for(auto& value:p){const auto bits=vm.parameter(thread);std::memcpy(&value,&bits,4);if(!std::isfinite(value))throw std::runtime_error("Non-finite original crane configuration");}
        const float x=p[0]<=-10000 || p[1]<=-10000?p[6]:p[0],y=p[0]<=-10000 || p[1]<=-10000?p[7]:p[1];
        auto nearest=cranes_.end();float best=100.0f*100.0f;
        for(auto it=cranes_.begin();it!=cranes_.end();++it){const auto dx=x-it->origin[0],dy=y-it->origin[1],d=dx*dx+dy*dy;if(d<best){best=d;nearest=it;}}
        if(nearest==cranes_.end()){thread.diagnostic="Original crusher crane is not present in the mounted world";return StepResult::unsupported;}
        // Configure the original service against a real world site. The crane
        // movement/capture backend is still pending: no pickup result is
        // synthesized; the physical consumer owns the lift state.
        nearest->pickup={std::min(p[2],p[4]),std::max(p[2],p[4]),std::min(p[3],p[5]),std::max(p[3],p[5])};
        nearest->dropoff={p[6],p[7],p[8]};nearest->heading=p[9]*0.017453292519943295f;
        // Retail CCranes::ActivateCrane(crusher): the physical service owns the pickup/lift/rotate/drop cycle.
        if(!crane_service.activate(size_t(nearest-cranes_.begin()),nearest->pickup[0],nearest->pickup[1],nearest->pickup[2],nearest->pickup[3],
                                   nearest->dropoff,nearest->heading,true)) {
            thread.diagnostic="Physical crane service refused the crusher activation";return StepResult::unsupported;
        }
        nearest->activated=true;nearest->crusher=true;return StepResult::continued;
    }
    if(opcode==COMMAND_NO_SPECIAL_CAMERA_FOR_THIS_GARAGE && world_ready) {
        const auto id=vm.parameter(thread);const auto garage=garages_.find(id);
        const auto declaration=garage_definitions_.find(id);
        if(garage==garages_.end() && declaration!=garage_definitions_.end()) {
            // Exclusive LCS services own their configuration. This changes the
            // original flag, not the collector's completion or vehicle state.
            declaration->second.camera_follows_player=true;return StepResult::continued;
        }
        if(garage==garages_.end() || !leave_garage_camera || !leave_garage_camera(garage->second)) {
            thread.diagnostic="Cannot retain the player camera in the original garage";return StepResult::unsupported;
        }
        if(declaration!=garage_definitions_.end())declaration->second.camera_follows_player=true;
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_MAXIMUM_NUMBER_OF_CARS_IN_GARAGE && world_ready) {
        const auto id=vm.parameter(thread),capacity=vm.parameter(thread);
        const auto declaration=garage_definitions_.find(id);
        if(declaration==garage_definitions_.end()) {
            thread.diagnostic="Garage capacity requires an original declaration";return StepResult::unsupported;
        }
        if(garages_.count(id)) {
            if(!set_garage_capacity || !set_garage_capacity(garages_.at(id),uint8_t(capacity))) {
                thread.diagnostic="Native hideout capacity controller unavailable: garage="+std::to_string(id)+" capacity="+std::to_string(capacity);return StepResult::unsupported;
            }
        }
        // Original CGarages setter accepts uint8, including wraparound. The
        // collector/crusher service is LCS-owned and keeps its actual setting.
        declaration->second.max_stored_cars=uint8_t(capacity);return StepResult::continued;
    }
    if(opcode==COMMAND_LOCK_GARAGE && world_ready && execute) {
        const auto id=vm.parameter(thread),locked=vm.parameter(thread);
        const auto garage=garages_.find(id);
        if(garage==garages_.end()){thread.diagnostic="Garage lock requires a real native garage binding";return StepResult::unsupported;}
        // Native deactivation blocks automatic use; its normal door controller
        // finishes closing. Unlock resumes that controller and opens the door.
        for(const auto native:locked?std::array<uint16_t,2>{0x0361,0x02B9}:std::array<uint16_t,2>{0x0299,0x0360}) {
            NativeCall call;call.opcode=native;call.text=garage->second;
            if(!execute(call).executed){thread.diagnostic="Native garage lock operation failed";return StepResult::unsupported;}
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_ROTATING_GARAGE_DOOR && world_ready && rotate_garage_door) {
        const auto id=vm.parameter(thread);const auto garage=garages_.find(id);
        if(garage==garages_.end() || !rotate_garage_door(garage->second)) {
            thread.diagnostic="Cannot set the original garage door rotation policy";return StepResult::unsupported;
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_GARAGE && world_ready && create_garage) {
        std::array<float,8> bounds{};
        for(auto& v:bounds){const auto bits=vm.parameter(thread);std::memcpy(&v,&bits,4);if(!std::isfinite(v))throw std::runtime_error("Non-finite original garage geometry");}
        const auto type=vm.parameter(thread);auto cursor=thread;vm.variable(cursor);
        // Reviewed common garage policies; the collectors and crusher have no
        // equivalent SA service. Never interpret an exclusive type as a hideout.
        const bool common=common_garage_type(type);
        if(type<0 || type>32 || next_garage_>=50){thread.diagnostic="Invalid original garage declaration";return StepResult::unsupported;}
        if(!common) {
            // Registration declares the original service and its geometry. An
            // exclusive collector/crusher receives an LCS-owned ID, never a
            // fake native garage binding. Actual operations remain blocked
            // until the corresponding LCS service is implemented.
            garage_definitions_[next_garage_]={bounds,type};
            if(ExclusiveGarages::owns(type))exclusive_garages.declare(next_garage_,bounds,type);
            vm.store(thread,next_garage_++);return StepResult::continued;
        }
        const auto name=create_garage(bounds,type,next_garage_);
        if(!name){thread.diagnostic="Native original garage registration failed";return StepResult::unsupported;}
        bind_garage(next_garage_,*name);garage_definitions_[next_garage_]={bounds,type};
        vm.store(thread,next_garage_++);return StepResult::continued;
    }
    if(opcode==COMMAND_SWITCH_RUBBISH && world_ready) {
        // Retain the original paper/debris visibility state. Native SA's
        // CRubbish::SetVisibility (0x7204C0) has no renderer implementation;
        // this does not substitute a mission condition or omit any SCM helper.
        rubbish_visible_=vm.parameter(thread)!=0;return StepResult::continued;
    }
    if(opcode==COMMAND_SWAP_BUILDINGS && world_ready) {
        const auto group=vm.parameter(thread),state=vm.parameter(thread);
        if(world_swap_state){
            const auto result=world_swap_state(group,state);
            if(!result.executed){
                if(result.native_entered)throw std::runtime_error("Building swap state changed before failure; replay rejected: "+result.diagnostic);
                thread.diagnostic="Building swap state refused: "+result.diagnostic;return StepResult::unsupported;
            }
        }
        // Preserve the original SCM helper's individual nearest-model swaps as well
        // as the staged source visibility state consumed by the world-sector backend.
        building_groups_[group]=state;return StepResult::continued;
    }
    if(opcode==COMMAND_STREAM_BUILDING_SWAPS && world_ready && execute) {
        bool loaded=true;
        if(request_world_swaps){
            const auto result=request_world_swaps();
            if(!result.executed){
                if(result.native_entered)throw std::runtime_error("Building swap request failed after entry; replay rejected: "+result.diagnostic);
                thread.diagnostic="Cannot request native world swaps: "+result.diagnostic;return StepResult::unsupported;
            }
            loaded=result.condition;
        }
        for(const auto model:building_swap_models_) {
            const auto result=execute({0x0248,{model},{},0,0,true});
            if(!result.executed){thread.diagnostic="Cannot verify native building swap residency";return StepResult::unsupported;}
            loaded=loaded && result.condition;
        }
        vm.compare(thread,loaded);return StepResult::continued;
    }
    if(opcode==COMMAND_IS_WORLD_STREAMING_COMPLETE){
        if(!world_ready || !world_streaming_complete){thread.diagnostic="World-sector readiness consumer unavailable";return StepResult::unsupported;}
        const auto ready=world_streaming_complete();
        if(!ready){thread.diagnostic="Cannot verify pending world-sector resources/entities";return StepResult::unsupported;}
        vm.compare(thread,*ready);return StepResult::continued;
    }
    if(opcode==COMMAND_SWAP_TO_STREAMED_SECTOR){
        if(!world_ready || !commit_world_sector){thread.diagnostic="World-sector commit consumer unavailable";return StepResult::unsupported;}
        const auto result=commit_world_sector();
        if(!result.executed){
            if(result.native_entered)throw std::runtime_error("World-sector commit failed after entry; replay rejected: "+result.diagnostic);
            thread.diagnostic="World-sector commit refused: "+result.diagnostic;return StepResult::unsupported;
        }
        if(!result.condition){
            if(result.native_entered)throw std::runtime_error("World-sector commit cannot be replayed after native entry");
            thread.ip=thread.last_ip;thread.diagnostic="Waiting for requested world-sector resources";return StepResult::yielded;
        }
        return StepResult::continued;
    }
    if(opcode==COMMAND_GRAB_PHONE) {
        if(!world_ready){thread.diagnostic="LCS world phone pool is not mounted";return StepResult::unsupported;}
        std::array<float,2> position{};
        for(auto& v:position){const auto bits=vm.parameter(thread);std::memcpy(&v,&bits,4);if(!std::isfinite(v))throw std::runtime_error("Non-finite script phone position");}
        auto cursor=thread;vm.variable(cursor); // Validate before claiming the booth.
        size_t nearest=phones_.size();float distance=100.0f*100.0f;
        for(size_t i=claimed_phones_;i<phones_.size();++i) {
            const auto dx=phones_[i].position[0]-position[0],dy=phones_[i].position[1]-position[1];
            const auto d=dx*dx+dy*dy;
            if(d<distance){distance=d;nearest=i;}
        }
        if(nearest==phones_.size()){thread.diagnostic="No unclaimed mounted LCS phone within original 100m search";return StepResult::unsupported;}
        phones_[nearest].message_removed=true;
        std::swap(phones_[nearest],phones_[claimed_phones_]);
        vm.store(thread,int32_t(claimed_phones_++));return StepResult::continued;
    }
    if(opcode==COMMAND_TURN_PHONE_ON) {
        const auto id=vm.parameter(thread);
        if(id<0 || size_t(id)>=claimed_phones_){thread.diagnostic="Phone handle was not claimed from the mounted LCS world";return StepResult::unsupported;}
        if(!phone_phase_available || !phone_phase_available() || !update_phone_frame){thread.diagnostic="Original phone late frame provider unavailable";return StepResult::unsupported;}
        auto& phone=phones_[size_t(id)];
        if(phone.frames.quarantined()){thread.diagnostic="Original phone partial frame effect quarantined; cannot replay";return StepResult::failed;}
        phone.enabled=true;return StepResult::continued;
    }
    if(opcode==COMMAND_TURN_PHONE_OFF) {
        const auto id=vm.parameter(thread);
        if(id<0 || size_t(id)>=claimed_phones_){thread.diagnostic="Phone handle was not claimed from the mounted LCS world";return StepResult::unsupported;}
        phones_[size_t(id)].message_removed=true;phones_[size_t(id)].enabled=false;return StepResult::continued;
    }
    if(opcode==COMMAND_SET_SHORTCUT_PICKUP_POINT || opcode==COMMAND_SET_SHORTCUT_DROPOFF_POINT_FOR_MISSION) {
        std::array<float,4> point{};
        for(auto& component:point){const auto bits=vm.parameter(thread);std::memcpy(&component,&bits,4);if(!std::isfinite(component))throw std::runtime_error("Non-finite original shortcut route");}
        if(opcode==COMMAND_SET_SHORTCUT_PICKUP_POINT){if(shortcut_pickups_.size()<16)shortcut_pickups_.push_back(point);}
        else shortcut_dropoff_=point;
        return StepResult::continued;
    }
    if(recovery_taxi_deferred && (opcode==COMMAND_CLEAR_TAXI_SHORTCUT || opcode==COMMAND_DOES_SHORTCUT_TAXI_EXIST)) {
        // The deferred service owns no cab, blip or player-control bit. Report
        // actual absence, never mission success or a fabricated vehicle handle.
        if(opcode==COMMAND_DOES_SHORTCUT_TAXI_EXIST)vm.compare(thread,false);
        return StepResult::continued;
    }
    if(opcode==COMMAND_DISABLE_PAUSE_MENU) {
        const auto disabled=vm.parameter(thread)!=0;
        if(!disable_pause_menu || !disable_pause_menu(disabled)) {
            thread.diagnostic="Native pause-menu activation gate is unavailable";return StepResult::unsupported;
        }
        return StepResult::continued;
    }
    if((opcode==COMMAND_IS_CHAR_STILL_ALIVE || opcode==COMMAND_IS_CAR_STILL_ALIVE) && world_ready && execute) {
        const auto ped=vm.parameter(thread);
        const auto dead=execute({opcode==COMMAND_IS_CAR_STILL_ALIVE?uint16_t(0x0119):uint16_t(0x0118),{ped},{},0,0,true});
        if(!dead.executed){thread.diagnostic="Cannot query actual native character life state";return StepResult::unsupported;}
        vm.compare(thread,!dead.condition);return StepResult::continued;
    }
    if((opcode==COMMAND_GET_CAMERA_PED_ZOOM_INDICATOR || opcode==COMMAND_SET_CAMERA_PED_ZOOM_INDICATOR) && camera_ped_zoom) {
        std::optional<int32_t> setting;
        if(opcode==COMMAND_SET_CAMERA_PED_ZOOM_INDICATOR)setting=vm.parameter(thread);
        else {auto cursor=thread;vm.variable(cursor);}
        const auto value=camera_ped_zoom(setting);
        if(!value){thread.diagnostic="Native camera zoom is unavailable";return StepResult::unsupported;}
        if(!setting)vm.store(thread,*value);
        return StepResult::continued;
    }
    if(opcode==COMMAND_FREEZE_PED_ZOOM_SWITCH && freeze_ped_zoom) {
        const auto freeze=vm.parameter(thread);
        if(!freeze_ped_zoom(freeze!=0)){thread.diagnostic="Native camera zoom freeze failed";return StepResult::unsupported;}
        return StepResult::continued;
    }
    if((opcode==COMMAND_SET_CHAR_WAIT_STATE || opcode==COMMAND_SET_CHAR_WAIT_STATE_REPEAT) && world_ready && execute) {
        const auto ped=vm.parameter(thread),state=vm.parameter(thread),duration=vm.parameter(thread);
        if(opcode==COMMAND_SET_CHAR_WAIT_STATE_REPEAT) {
            const auto exists=execute({0x056D,{ped},{},0,0,true});
            if(!exists.executed){thread.diagnostic="Cannot query the repeating gesture's actor";return StepResult::unsupported;}
            if(!exists.condition)return StepResult::continued; // original optional-ped semantics
        }
        if(state==1) {
            // WAITSTATE_TRAFFIC_LIGHTS is a movement wait, not an animation.
            // LCS ignores the supplied timeout, waits at least 500ms, then
            // resumes on PED_LIGHTS_WALK in its original 16384ms signal cycle.
            const auto exists=execute({0x056D,{ped},{},0,0,true}),clock=execute({0x01BD,{}, {},0,1});
            if(!exists.executed || !exists.condition || !clock.executed || !execute({0x05BA,{ped,500}}).executed){thread.diagnostic="Cannot begin original pedestrian signal wait";return StepResult::unsupported;}
            traffic_waits_[ped]=uint32_t(clock.values[0]);return StepResult::continued;
        }
        traffic_waits_.erase(ped);
        if(state!=0) {
            if(!play_wait_animation || !play_wait_animation(ped,state,duration,opcode==COMMAND_SET_CHAR_WAIT_STATE_REPEAT)){thread.diagnostic="Original LCS wait-state animation unavailable: "+std::to_string(state);return StepResult::unsupported;}
            return StepResult::continued;
        }
        if(clear_wait_animation && !clear_wait_animation(ped)){thread.diagnostic="Cannot clear owned original wait animation";return StepResult::unsupported;}
        (void)duration; // WAITSTATE_FALSE clears the old waiting action regardless of timeout.
        const auto cleared=execute({0x0687,{ped}});
        if(!cleared.executed){thread.diagnostic=cleared.diagnostic;return StepResult::unsupported;}
        forget_walking_objective(ped);
        return StepResult::continued;
    }
    if(opcode==COMMAND_CLEAR_CHAR_WAIT_STATE && world_ready && execute) {
        const auto ped=vm.parameter(thread);
        const auto exists=execute({0x056D,{ped},{},0,0,true});
        if(!exists.executed || !exists.condition){thread.diagnostic="Wait-state reset requires a live native character";return StepResult::unsupported;}
        if(clear_wait_animation && !clear_wait_animation(ped)){thread.diagnostic="Cannot clear owned original wait animation";return StepResult::unsupported;}
        traffic_waits_.erase(ped);
        // Fade only the owned gesture, preserving SA movement and combat tasks.
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_CHAR_ONLY_ALLOWED_TO_SIT_IN_FRONT && world_ready && execute) {
        // LCS retail 1485: CPed +0x1A0 bit 10 equals (argument != 0).
        // Native passenger seat 0 is the front passenger.  Do not merely
        // accept the opcode: both explicit mission entry and companion
        // boarding must consume this policy.
        const auto ped=vm.parameter(thread),front=vm.parameter(thread);
        const auto exists=execute({0x056D,{ped},{},0,0,true});
        if(!exists.executed || !exists.condition){thread.diagnostic="Front-seat policy requires a live native ped";return StepResult::unsupported;}
        const auto identity=live_ped_identity?live_ped_identity(ped):std::nullopt;
        if(!identity || identity->handle!=ped || !identity->native_ped){thread.diagnostic="Cannot prove front-seat policy owner generation";return StepResult::unsupported;}
        // A new identity invalidates all the previous occupant's seat policy.
        const auto previous=active_seat_rules(ped);
        if(!previous){thread.diagnostic="Cannot verify previous passenger policy owner";return StepResult::unsupported;}
        if(front)front_seats_[ped]={*identity};else front_seats_.erase(ped);
        return StepResult::continued;
    }
    if(opcode==COMMAND_SET_CHAR_ONLY_ALLOWED_TO_SIT_BEHIND && world_ready && execute) {
        const auto ped=vm.parameter(thread),rear=vm.parameter(thread);
        const auto exists=execute({0x056D,{ped},{},0,0,true});
        if(!exists.executed || !exists.condition){thread.diagnostic="Seat policy requires a live native ped";return StepResult::unsupported;}
        const auto identity=live_ped_identity?live_ped_identity(ped):std::nullopt;
        if(!identity || identity->handle!=ped || !identity->native_ped){thread.diagnostic="Cannot prove rear-seat policy owner generation";return StepResult::unsupported;}
        const auto previous=active_seat_rules(ped);
        if(!previous){thread.diagnostic="Cannot verify previous passenger policy owner";return StepResult::unsupported;}
        if(rear)rear_seats_[ped]={*identity};else rear_seats_.erase(ped);
        return StepResult::continued;
    }
    if(opcode==COMMAND_GET_WEAPON_TYPE_FOR_PICKUP_IN_AREA) {
        std::array<float,4> sphere{};
        for(auto& coordinate:sphere) {
            const auto bits=vm.parameter(thread);
            std::memcpy(&coordinate,&bits,sizeof(coordinate));
        }
        // The original collects four numeric parameters and then exactly one
        // output reference. Validate it before consulting native memory.
        auto destination=thread;
        vm.variable(destination);
        if(!world_ready || !pickup_model_in_area ||
           !std::all_of(sphere.begin(),sphere.end(),[](float v){return std::isfinite(v);}) ||
           sphere[3]<0) {
            thread.diagnostic="1487 requires finite sphere, mounted world and native pickup pool";
            return StepResult::unsupported;
        }
        const auto model=pickup_model_in_area(sphere);
        if(!model){thread.diagnostic="Native pickup scan/profile unavailable";return StepResult::unsupported;}
        int32_t source=-1;
        if(*model!=-1) {
            bool matched=false;
            // WeaponForModel(original pickup object model) yields the *LCS*
            // weapon index. Compare the verified donor model binding, not
            // GTA SA's weapon ID or a guessed 1:1 native weapon number.
            for(size_t weapon=0;weapon<weapon_definitions_.size();++weapon) {
                if(!weapon_definitions_[weapon] || weapon_definitions_[weapon]->model<0)continue;
                const auto donor=binding(weapon_definitions_[weapon]->model);
                if(!donor || donor->kind!=ModelKind::object || donor->id!=*model)continue;
                if(matched && source!=int32_t(weapon)) {
                    thread.diagnostic="Ambiguous LCS weapons share a native pickup model";
                    return StepResult::unsupported;
                }
                source=int32_t(weapon);matched=true;
            }
            // Retail CPickups::WeaponForModel has three explicit non-weapon
            // model branches. They return source weapon indices 37/38/38;
            // adrenaline deliberately maps to ARMOUR (the retail behavior),
            // not an invented new weapon index. Bind names from the mounted
            // original model index, never assume SA model numbers are shared.
            static constexpr std::pair<const char*,int32_t> specials[]={{"health",37},{"bodyarmour",38},{"adrenaline",38}};
            for(const auto& special:specials) {
                const auto named=named_models_.find(special.first);
                if(named==named_models_.end() || named->second.kind!=ModelKind::object || named->second.id!=*model)continue;
                if(matched && source!=special.second) {
                    thread.diagnostic="Ambiguous source weapon/special pickup donor model";
                    return StepResult::unsupported;
                }
                matched=true;source=special.second;
            }
            // Unsupported source names and conflicting donor identities cannot
            // become a fabricated negative pickup result.
            if(!matched){thread.diagnostic="Live native pickup has no unique LCS weapon/special model binding";return StepResult::unsupported;}
        }
        vm.store(thread,source);return StepResult::continued;
    }
    if(opcode==COMMAND_GET_PLAYER_STORED_WEAPON && stored_player_weapon) {
        const auto player=vm.parameter(thread);auto cursor=thread;vm.variable(cursor);
        const auto native=stored_player_weapon(player);
        if(!native){thread.diagnostic="Cannot read the native saved weapon";return StepResult::unsupported;}
        // UNIDENTIFIED is a damage/saved-weapon sentinel, not an equipable gun.
        if(*native==55){vm.store(thread,45);return StepResult::continued;}
        for(size_t i=0;i<std::size(weapon_counterparts);++i)if(weapon_counterparts[i]==*native){vm.store(thread,int32_t(i));return StepResult::continued;}
        thread.diagnostic="Saved native weapon has no LCS counterpart";return StepResult::unsupported;
    }
    if(opcode==COMMAND_BUILD_WORLD_GEOMETRY && world_ready) {
        (void)vm.parameter(thread);
        // This loader mounts prebuilt converted geometry. Match the original
        // reLCS gBuildWorldGeom=false branch instead of inventing PS2 sectors.
        vm.compare(thread,false);return StepResult::continued;
    }
    if(opcode==COMMAND_SET_UNIQUE_JUMPS_TOTAL) {
        const auto total=vm.parameter(thread);
        if(total<0)throw std::runtime_error("Negative LCS unique jump total");
        unique_jumps_total=total;return StepResult::continued;
    }
    if(opcode==COMMAND_MISSION_HAS_FINISHED) {
        if(!mission_script){thread.diagnostic="Native mission cleanup requires original LCS script ownership";return StepResult::unsupported;}
        // Script.cpp deliberately does nothing for an ordinary MAIN worker.
        // Only a genuine mission script may clean up registered native entities.
        if(!mission_script(thread))return StepResult::continued;
    }
    const auto found=std::find_if(std::begin(specs),std::end(specs),[opcode](const Spec& spec){return spec.lcs==opcode;});
    if(found==std::end(specs)) return StepResult::unsupported;
    if(found->world && !world_ready) { thread.diagnostic="Native LCS world is not mounted"; return StepResult::unsupported; }
    if(found->text_index>=0 && !text_ready) { thread.diagnostic="Native LCS text tables are not mounted";return StepResult::unsupported; }
    if(!execute) { thread.diagnostic="No native SA executor attached"; return StepResult::unsupported; }
    NativeCall call; call.opcode=found->sa; call.outputs=found->outputs; call.predicate=found->predicate;
    call.suppress_previous_brief=opcode==COMMAND_PRINT_HELP_FOREVER_NO_BRIEF ||
        opcode==COMMAND_PRINT_HELP_ALWAYS_NO_BRIEF || opcode==COMMAND_PRINT_HELP_FOREVER_ALWAYS_NO_BRIEF;
    call.mission_context=mission_script && mission_script(thread);
    for(size_t i=0;i<=found->inputs;++i) {
        if(found->text_index>=0 && i==size_t(found->text_index)) {
            call.text=vm.text_label(thread);call.text_position=i;
            for(auto& c:*call.text)if(c>='a' && c<='z')c-=32;
        }
        if(i<found->inputs)call.inputs.push_back(vm.parameter(thread));
    }
    if(opcode==COMMAND_CHANGE_CAR_COLOUR && !vehicle_colour_map_.empty()) {
        for(size_t i=1;i<3;++i) {
            const auto original=call.inputs[i];
            if(original<0 || size_t(original)>=vehicle_colour_map_.size()) {
                thread.diagnostic="Original vehicle colour is outside the mounted LCS palette";
                return StepResult::unsupported;
            }
            call.inputs[i]=vehicle_colour_map_[size_t(original)];
        }
    }
    int32_t original_vehicle=-1;
    if(opcode==COMMAND_ADD_EXPLOSION || opcode==COMMAND_IS_EXPLOSION_IN_AREA) {
        // Native SA inserted WEAK_ROCKET at index three. HELI2/HELI_BOMB
        // have no distinct reviewed counterpart; never silently merge them.
        static constexpr int32_t types[]={0,1,2,4,5,6,7,-1,8,9,10,-1};
        auto& type=call.inputs[opcode==COMMAND_ADD_EXPLOSION?3:0];
        if(!(opcode==COMMAND_IS_EXPLOSION_IN_AREA && type==-1)) {
            if(type<0 || size_t(type)>=std::size(types) || types[type]<0){thread.diagnostic="Original explosion kind requires a distinct implementation";return StepResult::unsupported;}
            type=types[type];
        }
    }
    if(opcode==COMMAND_BURST_CAR_TYRE && (call.inputs[1]<0 || call.inputs[1]>3)){
        thread.diagnostic="Burst tyre requires an individual original wheel selector";return StepResult::unsupported;
    }
    if(opcode==COMMAND_GET_POSITION_OF_ANALOGUE_STICKS && (call.inputs[0]<0 || call.inputs[0]>1)) {
        thread.diagnostic="Original analogue query requires pad zero or one";return StepResult::unsupported;
    }
    if(opcode==COMMAND_MAKE_OBJECT_TARGETTABLE)call.inputs.push_back(1);
    if(opcode==COMMAND_CREATE_CAR || opcode==COMMAND_GET_RANDOM_CAR_OF_TYPE_IN_AREA)original_vehicle=call.inputs[size_t(found->model_index)];
    const auto changed_music_area=opcode==COMMAND_SET_AREA_VISIBLE?std::optional<int32_t>(call.inputs[0]):std::nullopt;
    if(flat_native_world && opcode==COMMAND_SET_AREA_VISIBLE)call.inputs[0]=0;
    if(opcode==COMMAND_ADD_HOSPITAL_RESTART || opcode==COMMAND_ADD_POLICE_RESTART)call.inputs.push_back(0);
    if(opcode==COMMAND_SET_EXTRA_COLOURS && flat_native_world) {
        if(call.inputs[0]<1 || call.inputs[0]>32){thread.diagnostic="Unknown original extra color bank";return StepResult::unsupported;}
        call.inputs[0]=1; // Current native lighting profile, as in the working opening scene.
    }
    if(opcode==COMMAND_FORCE_WEATHER || opcode==COMMAND_FORCE_WEATHER_NOW) {
        // LCS Weather.h: sunny/cloudy/rain/fog/extra sunny/hurricane/extra colors.
        // Explicit SA climate counterparts; LCS numeric weather IDs differ.
        static constexpr int32_t weather[]={1,4,8,9,0,16,21};
        const auto original=call.inputs[0];
        if(original!=-1 && (original<0 || size_t(original)>=std::size(weather))) {
            thread.diagnostic="Unknown original weather type";return StepResult::unsupported;
        }
        if(original>=0)call.inputs[0]=weather[original];
    }
    if(opcode==COMMAND_SET_CAR_STATUS) {
        const auto status=source_vehicle_status(call.inputs[1]);
        if(!status){thread.diagnostic="Original vehicle status has no native counterpart (ferry or unknown status)";return StepResult::unsupported;}
        call.inputs[1]=*status;
    }
    if(opcode==COMMAND_ADD_SHORT_RANGE_SPRITE_BLIP_FOR_COORD || opcode==COMMAND_ADD_SPRITE_BLIP_FOR_COORD ||
       opcode==COMMAND_ADD_SPRITE_BLIP_FOR_CONTACT_POINT || opcode==COMMAND_ADD_SHORT_RANGE_SPRITE_BLIP_FOR_CONTACT_POINT) {
        // The native radar keeps actual handles/positions. Named LCS contacts
        // use SA's mission question-mark icon until their original icons exist.
        auto& sprite=call.inputs[3];
        if(sprite<0 || sprite>62){thread.diagnostic="Unknown original radar sprite";return StepResult::unsupported;}
        const auto original=sprite;sprite=37;
        switch(original) {
        case 0:sprite=0;break;case 1:sprite=2;break;case 2:sprite=3;break;case 3:sprite=4;break;
        case 12:sprite=9;break;case 16:sprite=6;break;case 19:sprite=35;break;
        case 25:sprite=31;break;case 27:sprite=63;break;case 28:case 41:sprite=45;break;
        case 42:sprite=32;break;case 62:sprite=41;break;
        default:break;
        }
    }
    if(opcode==COMMAND_START_SCRIPT_FIRE || opcode==COMMAND_CREATE_PROTECTION_PICKUP || opcode==COMMAND_ADD_BLIP_FOR_COORD_OLD || opcode==COMMAND_GET_CLOSEST_STRAIGHT_ROAD) {
        for(size_t i=0;i<(opcode==COMMAND_GET_CLOSEST_STRAIGHT_ROAD?5:3);++i) {
            if(!std::isfinite(entity_float(call.inputs[i]))){thread.diagnostic="Non-finite source fire/radar/path coordinates";return StepResult::unsupported;}
        }
    }
    if(opcode==COMMAND_START_SCRIPT_FIRE) {
        // Original StartScriptFire(pos,nullptr,0.8f,true); native additions
        // default to no propagation and size one, as with StartCharFire.
        call.inputs.push_back(0);call.inputs.push_back(1);
    }
    if(found->garage_index>=0) {
        if(opcode==COMMAND_CHANGE_GARAGE_TYPE && exclusive_garages.has(call.inputs.at(size_t(found->garage_index)))) {
            // Retail CGarages::ChangeGarageType on an LCS-owned service garage. NONE retires the service; the
            // original leaves the door where it is. Any other common type would need a native garage that
            // was never created for it, so it stays unsupported.
            const auto id=call.inputs.at(size_t(found->garage_index)),type=call.inputs.at(1);
            if(type!=0 && !ExclusiveGarages::owns(type)) {
                thread.diagnostic="Requested garage policy needs a native garage that an exclusive service never created";
                return StepResult::unsupported;
            }
            if(!exclusive_garages.change_type(id,type)) {
                thread.diagnostic="Exclusive garage service refused the garage type change";return StepResult::unsupported;
            }
            const auto declaration=garage_definitions_.find(id);
            if(declaration!=garage_definitions_.end())declaration->second.type=type;
            return StepResult::continued;
        }
        if(opcode==COMMAND_CHANGE_GARAGE_TYPE && !common_garage_type(call.inputs.at(1))) {
            thread.diagnostic="Requested garage policy requires an exclusive LCS service";return StepResult::unsupported;
        }
        const auto position=size_t(found->garage_index);
        const auto garage=garages_.find(call.inputs.at(position));
        if(garage==garages_.end()) {
            thread.diagnostic="LCS garage has no verified native name binding: "+std::to_string(call.inputs[position]);
            return StepResult::unsupported;
        }
        call.text=garage->second;call.text_position=position;
        call.inputs.erase(call.inputs.begin()+position);
    }
    const bool cutscene=opcode==COMMAND_LOAD_CUTSCENE || opcode==COMMAND_START_CUTSCENE ||
        opcode==COMMAND_GET_CUTSCENE_TIME || opcode==COMMAND_HAS_CUTSCENE_FINISHED ||
        opcode==COMMAND_CLEAR_CUTSCENE || opcode==COMMAND_HAS_CUTSCENE_LOADED || opcode==COMMAND_WAS_CUTSCENE_SKIPPED;
    std::string original_scene;
    if(cutscene) {
        if(!text_ready || cutscenes_.empty()) {thread.diagnostic="Native LCS cutscene resources are not mounted";return StepResult::unsupported;}
        if(opcode==COMMAND_LOAD_CUTSCENE) {
            if(!cutscenes_.count(lower(*call.text))) {thread.diagnostic="LCS cutscene has no mounted native resources: "+*call.text;return StepResult::unsupported;}
            if(current_cutscene_) {thread.diagnostic="Clear the previous LCS cutscene before loading another";return StepResult::unsupported;}
            original_scene=lower(*call.text);
            if(prepare_cutscene_audio && !prepare_cutscene_audio(original_scene)) {thread.diagnostic="Original cutscene audio could not be prepared";return StepResult::unsupported;}
            call.text=cutscenes_.at(original_scene);
            for(auto& c:*call.text)if(c>='a' && c<='z')c-=32;
        } else if(opcode!=COMMAND_WAS_CUTSCENE_SKIPPED && !current_cutscene_) {
            if(opcode==COMMAND_HAS_CUTSCENE_LOADED || opcode==COMMAND_HAS_CUTSCENE_FINISHED) {vm.compare(thread,false);return StepResult::continued;}
            if(opcode==COMMAND_CLEAR_CUTSCENE)return StepResult::continued;
            thread.diagnostic="No LCS cutscene is loaded";return StepResult::unsupported;
        }
        if(opcode==COMMAND_GET_CUTSCENE_TIME && !cutscene_started_) {thread.diagnostic="LCS cutscene has not started";return StepResult::unsupported;}
    }
    std::optional<std::pair<int32_t,Binding>> pending_special;
    if(opcode==COMMAND_LOAD_SPECIAL_CHARACTER || opcode==COMMAND_HAS_SPECIAL_CHARACTER_LOADED || opcode==COMMAND_UNLOAD_SPECIAL_CHARACTER) {
        const auto slot=call.inputs[0];
        if(equivalent_entities) {
            auto target=slot>=1 && slot<=21 ? binding(108+slot) : std::nullopt;
            if(!target || target->kind!=ModelKind::ped) {thread.diagnostic="Equivalent special character slot is not configured";return StepResult::unsupported;}
            if(opcode==COMMAND_LOAD_SPECIAL_CHARACTER && call.text) {
                const auto resource=named_models_.find(lower(*call.text));
                if(resource!=named_models_.end() && resource->second.kind==ModelKind::ped) {
                    target=resource->second;pending_special=std::make_pair(108+slot,*target);
                } else if(named_special_characters) {
                    thread.diagnostic="Original mission character has no converted gameplay binding: "+*call.text;return StepResult::unsupported;
                }
            }
            // The equivalent is already a regular SA model. Never ask SA's
            // special-model streamer to load an absent LCS resource name.
            call.opcode=opcode==COMMAND_LOAD_SPECIAL_CHARACTER ? 0x0247 : opcode==COMMAND_HAS_SPECIAL_CHARACTER_LOADED ? 0x0248 : 0x0249;
            call.inputs={target->id};call.text.reset();call.text_position=0;
        } else {
        if(slot<1 || slot>10) {thread.diagnostic="LCS special character slot exceeds native capacity";return StepResult::unsupported;}
        const auto target=binding(108+slot);
        if(!target || target->kind!=ModelKind::ped || target->id!=289+slot) {
            thread.diagnostic="LCS special character slot has no native special-ped binding";return StepResult::unsupported;
        }
        if(call.text) {
            const auto resource=named_models_.find(lower(*call.text));
            if(resource==named_models_.end() || resource->second.kind!=ModelKind::ped || resource->second.id!=target->id) {
                thread.diagnostic="LCS special character model is not mounted in the requested slot: "+*call.text;return StepResult::unsupported;
            }
        }
        }
    }
    for(const auto model_index:{found->model_index,found->second_model_index}) {
      if(model_index>=0) {
        auto& id=call.inputs[size_t(model_index)]; const auto translated=binding(id);
        if(opcode==COMMAND_CREATE_CAR_GENERATOR && id==-1) {
            // -1 is a random vehicle selector here, not a SCM object reference.
        } else {
            if(!translated) { thread.diagnostic="LCS model has no native SA binding: "+std::to_string(id); return StepResult::unsupported; }
            const bool ped=opcode==COMMAND_CREATE_CHAR || opcode==COMMAND_CREATE_CHAR_AS_PASSENGER || opcode==COMMAND_CREATE_CHAR_INSIDE_CAR;
            const bool vehicle=opcode==COMMAND_CREATE_CAR || opcode==COMMAND_GET_RANDOM_CAR_OF_TYPE_IN_AREA || opcode==COMMAND_CREATE_CAR_GENERATOR || opcode==COMMAND_IS_CHAR_IN_MODEL || opcode==COMMAND_IS_PLAYER_IN_MODEL || opcode==COMMAND_IS_CAR_MODEL;
            const bool object=opcode==COMMAND_CREATE_OBJECT || opcode==COMMAND_CREATE_OBJECT_NO_OFFSET || opcode==COMMAND_SWAP_NEAREST_BUILDING_MODEL || opcode==COMMAND_SET_VISIBILITY_OF_CLOSEST_OBJECT_OF_TYPE;
            const bool physical_swap=opcode==COMMAND_SWAP_NEAREST_BUILDING_MODEL && translated->kind==ModelKind::world_collision;
            if((ped && translated->kind!=ModelKind::ped) || (vehicle && translated->kind!=ModelKind::vehicle) || (object && translated->kind!=ModelKind::object && !physical_swap)) {
                thread.diagnostic="LCS model binding has the wrong native entity class";return StepResult::unsupported;
            }
            id=translated->id;
        }
      }
    }
    if(opcode==COMMAND_CREATE_CHAR || opcode==COMMAND_CREATE_CHAR_AS_PASSENGER || opcode==COMMAND_CREATE_CHAR_INSIDE_CAR) {
        // LCS emergency/special types shifted when SA added dealer/biker types.
        // Gang affinity needs an explicit bridge rather than SA's numbered gangs.
        auto& type=call.inputs[opcode==COMMAND_CREATE_CHAR ? 0:1];
        static constexpr int types[]={0,-1,-1,-1,4,5,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,18,19,20,-1,22,23,-1};
        const auto custom=ped_types_.find(type);
        if(custom!=ped_types_.end())type=custom->second;
        else if(type<0 || size_t(type)>=std::size(types) || types[type]<0) {
            thread.diagnostic="LCS ped type requires a native faction binding: "+std::to_string(type);return StepResult::unsupported;
        }
        else type=types[type];
    }
    // Validate all destinations BEFORE any native call creates an entity or
    // changes the game. A malformed destination must have no engine side effects.
    auto cursor=thread;
    for(size_t i=0;i<call.outputs;++i) vm.variable(cursor);
    if(opcode==COMMAND_MAKE_OBJECT_TARGETTABLE) {
        const auto exists=execute({0x03CA,{call.inputs[0]},{},0,0,true});
        if(!exists.executed || !exists.condition){thread.diagnostic="Targettable object must still exist in the native pool";return StepResult::unsupported;}
    }
    if(opcode==COMMAND_CREATE_OBJECT || opcode==COMMAND_CREATE_OBJECT_NO_OFFSET || opcode==COMMAND_CREATE_CHAR ||
       opcode==COMMAND_CREATE_CHAR_INSIDE_CAR || opcode==COMMAND_CREATE_CHAR_AS_PASSENGER || opcode==COMMAND_CREATE_CAR) {
        const auto model=call.inputs.at(size_t(found->model_index));
        const auto request=execute({0x0247,{model}});
        const auto loaded=request.executed ? execute({0x0248,{model},{},0,0,true}) : NativeResult{};
        if(!request.executed || !loaded.executed){thread.diagnostic="Native mission entity model request failed";return StepResult::unsupported;}
        if(!loaded.condition){thread.ip=thread.last_ip;thread.diagnostic="Waiting for original mission entity model";return StepResult::yielded;}
    }
    if(opcode==COMMAND_SWAP_NEAREST_BUILDING_MODEL) {
        bool ready=true;
        for(const auto index:{4,5}) {
            const auto model=call.inputs[size_t(index)];
            const auto request=execute({0x0247,{model}});
            const auto loaded=request.executed ? execute({0x0248,{model},{},0,0,true}) : NativeResult{};
            if(!request.executed || !loaded.executed){thread.diagnostic="Native building swap model request failed";return StepResult::unsupported;}
            building_swap_models_.insert(model);ready=ready && loaded.condition;
        }
        if(!ready){thread.ip=thread.last_ip;thread.diagnostic="Waiting for original building swap models";return StepResult::yielded;}
    }
    if(opcode==COMMAND_START_CUTSCENE) {
        NativeCall loaded;loaded.opcode=0x06B9;loaded.predicate=true;
        const auto ready=execute(loaded);
        if(!ready.executed) {thread.diagnostic="Native cutscene readiness query failed";return StepResult::unsupported;}
        if(!ready.condition) {thread.ip=thread.last_ip;thread.diagnostic="Waiting for native cutscene loading";return StepResult::yielded;}
    }
    if(opcode==COMMAND_SET_CHAR_OBJ_GOTO_COORD_ON_FOOT || opcode==COMMAND_SET_CHAR_OBJ_RUN_TO_COORD || opcode==COMMAND_SET_CHAR_OBJ_SPRINT_TO_COORD) {
        NativeCall ground;ground.opcode=0x02CE;
        ground.inputs={call.inputs[1],call.inputs[2],0x447A0000};ground.outputs=1; // 1000.0f, original 2D ground search
        const auto height=execute(ground);float z=0;
        if(height.executed)std::memcpy(&z,&height.values[0],4);
        if(!height.executed || !std::isfinite(z) || z<=-1000.0f) {
            thread.diagnostic="Native ground for LCS walking objective is unavailable";return StepResult::unsupported;
        }
        // MOVE_STATE_WALK=4. The untimed (-2) native task does not teleport the
        // character to the destination when a finite timeout expires.
        const auto movement=opcode==COMMAND_SET_CHAR_OBJ_SPRINT_TO_COORD?7:opcode==COMMAND_SET_CHAR_OBJ_RUN_TO_COORD || running_chars_[call.inputs[0]]?6:4;
        call.inputs.insert(call.inputs.end(),{height.values[0],movement,-2});
        const auto policy=pednode_seek_.find(call.inputs[0]);
        if(policy!=pednode_seek_.end() && policy->second)call.opcode=0x05F5;
    }
    if(opcode==COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_PASSENGER) {
        // Native timed entry, no specified passenger seat. Repeated attempts
        // and the mission's completion checks remain in the original SCM.
        const auto seat_rules=active_seat_rules(call.inputs[0]);
        if(!seat_rules){thread.diagnostic="Cannot confirm live passenger seat policy identity";return StepResult::unsupported;}
        const auto [front,rear]=*seat_rules;
        if(front && rear){thread.diagnostic="Incompatible front-only and rear-only seat restrictions";return StepResult::unsupported;}
        // -1 lets the native engine choose; 0 is the actual front seat.
        // A front-only character must never fall back to a rear passenger.
        call.inputs.insert(call.inputs.end(),{-1,front?0:(rear?1:-1)});
    }
    if(opcode==COMMAND_GET_PAD_BUTTON_STATE) {
        // LCS selector 44 is the cutscene/dialogue skip control. Native SA
        // button 16 is the action button used by its skip control (Space/Cross).
        if(call.inputs[0]!=44){thread.diagnostic="Unmapped LCS pad selector";return StepResult::unsupported;}
        call.inputs={0,16};
    }
    if(opcode==COMMAND_CHAR_LOOK_AT_CHAR_ALWAYS || opcode==COMMAND_PLAYER_LOOK_AT_CHAR_ALWAYS || opcode==COMMAND_CHAR_LOOK_AT_PLAYER_ALWAYS)call.inputs.push_back(-1);
    // Original LCS exposes permission to change lane. SA exposes its inverse,
    // stay in the fast lane; never forward the original boolean unchanged.
    if(opcode==COMMAND_SET_CAR_CHANGE_LANE)call.inputs[1]=call.inputs[1]?0:1;
    if(opcode==COMMAND_SET_CHAR_OBJ_WAIT_ON_FOOT || opcode==COMMAND_CHAR_SET_IDLE || opcode==COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_DRIVER)call.inputs.push_back(-1);
    if(opcode==COMMAND_SET_CHAR_OBJ_AIM_GUN_AT_CHAR)call.inputs.push_back(-1);
    if(opcode==COMMAND_SET_CHAR_OBJ_GOTO_CHAR_ON_FOOT || opcode==COMMAND_SET_CHAR_OBJ_GOTO_PLAYER_ON_FOOT || opcode==COMMAND_SET_CHAR_OBJ_WALK_TO_CHAR) {
        call.inputs.push_back(-1);call.inputs.push_back(float_bits(1.0f));
    }
    if(opcode==COMMAND_SET_CHAR_OBJ_FLEE_CHAR_ON_FOOT_ALWAYS || opcode==COMMAND_SET_CHAR_OBJ_FLEE_PLAYER_ON_FOOT_ALWAYS) {
        call.inputs.push_back(100);call.inputs.push_back(-1); // Native05DB uses FILD integer-distance conversion.
    }
    bool flee_from_vehicle=false;
    if(opcode==COMMAND_SET_CHAR_OBJ_FLEE_ON_FOOT_TILL_SAFE) {
        NativeCall flee;std::string why;
        if(!flee_point_packet(call.inputs[0],flee,flee_from_vehicle,why)){thread.diagnostic=why;return StepResult::unsupported;}
        call.opcode=flee.opcode;call.inputs=flee.inputs;
    }
    if(opcode==COMMAND_CREATE_PLAYER) {
        if(!reuse_native_player || call.inputs[0]!=0) {
            thread.diagnostic="LCS startup requires the existing native player zero binding";return StepResult::unsupported;
        }
        NativeCall player;player.opcode=0x01F5;player.inputs={0};player.outputs=1;
        const auto ped=execute(player);
        if(!ped.executed || ped.values[0]<0) {thread.diagnostic="Cannot reuse the native bootstrap player";return StepResult::unsupported;}
        NativeCall position;position.opcode=0x00A1;
        position.inputs={ped.values[0],call.inputs[1],call.inputs[2],call.inputs[3]};
        const auto moved=execute(position);
        if(!moved.executed){thread.diagnostic="Native player placement failed: "+moved.diagnostic;return StepResult::unsupported;}
        vm.store(thread,0);return StepResult::continued;
    }
    int32_t original_weapon=-1;
    if(found->weapon_index>=0) {
        auto& weapon=call.inputs[size_t(found->weapon_index)];
        original_weapon=weapon;
        // Script2.cpp selects a slot only if a carried weapon matches. The
        // UNIDENTIFIED sentinel cannot match an inventory slot, so keep it.
        if(weapon==45 && (opcode==COMMAND_SET_CURRENT_PLAYER_WEAPON || opcode==COMMAND_SET_CURRENT_CHAR_WEAPON))return StepResult::continued;
        // Explicit native weapon slot counterparts. Original weapon models and
        // balance are separate assets; exclusive guns need their own profile.
        const bool damage_query=opcode==COMMAND_HAS_CHAR_BEEN_DAMAGED_BY_WEAPON || opcode==COMMAND_HAS_CAR_BEEN_DAMAGED_BY_WEAPON || opcode==COMMAND_HAS_OBJECT_BEEN_DAMAGED_BY_WEAPON;
        if(damage_query && weapon>=39 && weapon<=47)weapon+=10;
        else if(weapon<0 || size_t(weapon)>=std::size(weapon_counterparts) || weapon_counterparts[weapon]<0) {
            thread.diagnostic="LCS weapon has no native SA counterpart: "+std::to_string(weapon);return StepResult::unsupported;
        } else weapon=weapon_counterparts[weapon];
    }
    if(found->player_ped || opcode==COMMAND_GET_PLAYER_COORDINATES || opcode==COMMAND_SET_PLAYER_COORDINATES) {
        NativeCall player; player.opcode=0x01F5; player.inputs={call.inputs[0]};player.outputs=1;
        const auto ped=execute(player);
        if(!ped.executed) {thread.diagnostic="Cannot resolve native player ped: "+ped.diagnostic;return StepResult::unsupported;}
        call.inputs[0]=ped.values[0];
        if(opcode==COMMAND_GET_PLAYER_COORDINATES || opcode==COMMAND_SET_PLAYER_COORDINATES)
            call.opcode=opcode==COMMAND_GET_PLAYER_COORDINATES ? 0x00A0 : 0x00A1;
    }
    if(opcode==COMMAND_SET_CHAR_OBJ_KILL_PLAYER_ON_FOOT || opcode==COMMAND_SET_CHAR_OBJ_KILL_PLAYER_ANY_MEANS ||
       opcode==COMMAND_SET_CHAR_OBJ_GOTO_PLAYER_ON_FOOT || opcode==COMMAND_SET_CHAR_OBJ_FLEE_PLAYER_ON_FOOT_ALWAYS || opcode==COMMAND_HAS_CHAR_SPOTTED_PLAYER || opcode==COMMAND_TURN_CHAR_TO_FACE_PLAYER || opcode==COMMAND_CHAR_LOOK_AT_PLAYER_ALWAYS) {
        const auto ped=execute({0x01F5,{call.inputs[1]},{},0,1});
        if(!ped.executed || ped.values[0]<0) {thread.diagnostic="Cannot resolve real player target for native combat task";return StepResult::unsupported;}
        call.inputs[1]=ped.values[0];
    }
    if(opcode==COMMAND_RESET_NUM_OF_MODELS_KILLED_BY_PLAYER || opcode==COMMAND_GET_NUM_OF_MODELS_KILLED_BY_PLAYER)
    {
        // The retail Darkel bank has 800 model rows, two player columns. Its
        // query indexes without a bounds check; world/model aliases must not
        // escape that storage even if they are valid streaming model IDs.
        if(opcode==COMMAND_GET_NUM_OF_MODELS_KILLED_BY_PLAYER && (call.inputs[0]<0 || call.inputs[0]>=800)) {
            thread.diagnostic="Original kill-counter model exceeds the verified native Darkel bank";return StepResult::unsupported;
        }
        call.inputs.insert(call.inputs.begin(),0); // After source model translation: original single-player Darkel bank.
    }
    const bool walking_acceptance=opcode==COMMAND_SET_CHAR_OBJ_GOTO_COORD_ON_FOOT ||
        opcode==COMMAND_SET_CHAR_OBJ_RUN_TO_COORD || opcode==COMMAND_SET_CHAR_OBJ_SPRINT_TO_COORD;
    const bool char_goto_acceptance=opcode==COMMAND_SET_CHAR_OBJ_GOTO_CHAR_ON_FOOT ||
        opcode==COMMAND_SET_CHAR_OBJ_WALK_TO_CHAR || opcode==COMMAND_SET_CHAR_OBJ_GOTO_PLAYER_ON_FOOT;
    const bool kill_acceptance=opcode==COMMAND_SET_CHAR_OBJ_KILL_CHAR_ON_FOOT || opcode==COMMAND_SET_CHAR_OBJ_KILL_CHAR_ANY_MEANS ||
        opcode==COMMAND_SET_CHAR_OBJ_KILL_PLAYER_ON_FOOT || opcode==COMMAND_SET_CHAR_OBJ_KILL_PLAYER_ANY_MEANS;
    const bool captured_task_acceptance=walking_acceptance || char_goto_acceptance || kill_acceptance;
    const bool vehicle_acceptance=opcode==COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_DRIVER ||
        opcode==COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_PASSENGER || opcode==COMMAND_SET_CHAR_OBJ_LEAVE_CAR ||
        opcode==COMMAND_SET_CHAR_OBJ_LEAVE_ANY_CAR;
    const bool source_objective=vehicle_acceptance || walking_acceptance || opcode==COMMAND_CHAR_SET_IDLE ||
        std::string(opcode_name(opcode)).rfind("COMMAND_SET_CHAR_OBJ_",0)==0;
    bool source_projected=false;
    const auto deleting_actor=opcode==COMMAND_DELETE_CHAR && source_actor_deleted && live_ped_identity?
        live_ped_identity(call.inputs[0]):std::nullopt;
    const auto execute_once=[&]() {
        if(flee_from_vehicle)return execute_flee(call,true);
        if(vehicle_acceptance && execute_vehicle_objective)return execute_vehicle_objective(call);
        if(!captured_task_acceptance || !execute_walking_objective)return execute(call);
        try{return execute_walking_objective(call);}
        catch(...){accepted_walk_tasks_.erase(call.inputs[0]);if(!execute_source_objective)observe_source_goal_unknown(call.inputs[0]);throw;}
    };
    const auto result=source_objective?execute_projected_objective(opcode,call,source_projected,execute_once):execute_once();
    if(!result.executed) {
        if(opcode==COMMAND_LOAD_CUTSCENE && clear_cutscene_audio)clear_cutscene_audio();
        thread.diagnostic="Native SA command failed: "+result.diagnostic;return StepResult::unsupported;
    }
    // A literal source setter can ignore a repeated/inadmissible objective.
    // Its successful no-entry receipt preserves all existing walking policy.
    if(source_projected && !result.native_entered)return StepResult::continued;
    if(changed_music_area && source_music_area_changed)source_music_area_changed(*changed_music_area);
    // These are observations of an ALREADY accepted source effect. Callback
    // failure must not replay a creation/deletion or replace its real result.
    try {
        if(result.native_entered && result.values[0]>=0 && (opcode==COMMAND_CREATE_CHAR ||
                opcode==COMMAND_CREATE_CHAR_AS_PASSENGER || opcode==COMMAND_CREATE_CHAR_INSIDE_CAR)) {
            // Actual construction, including complete handle-generation wrap,
            // retires policies of the previous occupant before any new goal.
            const auto actor=result.values[0];
            walking_objectives_.erase(actor);accepted_walk_tasks_.erase(actor);running_chars_.erase(actor);
            attack_policy_.erase(actor);kill_objectives_.erase(actor);
            companions_.erase(actor);frozen_followers_.erase(actor);traffic_waits_.erase(actor);
            front_seats_.erase(actor);rear_seats_.erase(actor);pednode_seek_.erase(actor);
            worn_outfits_.erase(actor);equivalent_outfits_.erase(actor);equivalent_weapons_.erase(actor);
            if(erase_pednode_seek)erase_pednode_seek(actor);
            if(player_friend_actor_created)player_friend_actor_created(actor);
        }
        if(opcode==COMMAND_CREATE_CHAR && result.values[0]>=0 && source_actor_created && live_ped_identity) {
            const auto actor=result.native_entered?live_ped_identity(result.values[0]):std::nullopt;
            if(actor)source_actor_created(*actor);else observe_source_goal_unknown(result.values[0]);
        }
        if(deleting_actor && result.native_entered)source_actor_deleted(*deleting_actor);
        if(opcode==COMMAND_MARK_CHAR_AS_NO_LONGER_NEEDED && result.native_entered && !call.inputs.empty())
            // Original CleanUpThisPed changes MISSION_CHAR to RANDOM_CHAR
            // and can replace its goal. Retire constructor-derived authority
            // while leaving the native cleanup result and living ped intact.
            observe_source_goal_unknown(call.inputs[0]);
    } catch(...) {
        if(opcode==COMMAND_CREATE_CHAR && result.values[0]>=0)observe_source_goal_unknown(result.values[0]);
        if(deleting_actor)observe_source_goal_unknown(deleting_actor->handle);
        try {if(diagnostic_event)diagnostic_event("Source actor lifecycle observation failed after native acceptance; original effect is not replayed");}catch(...){}
    }
    if(pending_special) {
        // LOAD changes the logical LCS slot only after the real native request.
        // Subsequent HAS/CREATE_CHAR/model comparisons retain that identity.
        models_[pending_special->first]=pending_special->second;
        const auto slot=pending_special->first-108;
        named_models_["special"+(slot<10?std::string("0"):std::string())+std::to_string(slot)]=pending_special->second;
    }
    if(opcode==COMMAND_ADD_SPHERE && result.values[0]>=0)owned_spheres_.insert(result.values[0]);
    if(opcode==COMMAND_REMOVE_SPHERE)owned_spheres_.erase(call.inputs[0]);
    const std::string operation=opcode_name(opcode);
    if(call.outputs==1 && result.values[0]>=0 && operation.rfind("COMMAND_ADD_",0)==0 && operation.find("BLIP")!=std::string::npos)owned_blips_.insert(result.values[0]);
    if(opcode==COMMAND_REMOVE_BLIP)owned_blips_.erase(call.inputs[0]);
    if(opcode==COMMAND_SET_CHAR_OBJ_GOTO_COORD_ON_FOOT)walking_objectives_[call.inputs[0]]=call;
    else if(source_projected) {
        if(result.native_entered) {walking_objectives_.erase(call.inputs[0]);accepted_walk_tasks_.erase(call.inputs[0]);}
    }
    else if(!call.inputs.empty() && (std::string(opcode_name(opcode)).rfind("COMMAND_SET_CHAR_OBJ_",0)==0 || opcode==COMMAND_CHAR_SET_IDLE || opcode==COMMAND_CLEAR_ALL_CHAR_ANIMS || opcode==COMMAND_DELETE_CHAR))forget_walking_objective(call.inputs[0]);
    if(walking_acceptance || kill_acceptance) {
        if(walking_acceptance && !source_projected)observe_source_goal_unknown(call.inputs[0]);
        accepted_walk_tasks_.erase(call.inputs[0]);
        if(result.task_capture==NativeResult::TaskCapture::captured && result.accepted_script_task) {
            const auto& captured=*result.accepted_script_task;const auto& record=captured.record;
            const bool event=record.status==0 && record.event && !record.task;
            const bool task=(record.status==1 || record.status==2 || (record.status>=4 && record.status<=6)) && record.task && !record.event;
            if(captured.slot<128 && record.opcode==call.opcode && record.ped && (event || task))
                accepted_walk_tasks_[call.inputs[0]]=captured;
        }
    }
    if(!call.inputs.empty() && result.native_entered &&
       (std::string(opcode_name(opcode)).rfind("COMMAND_SET_CHAR_OBJ_",0)==0 || opcode==COMMAND_CHAR_SET_IDLE || opcode==COMMAND_DELETE_CHAR)) {
        // Every new script goal replaces the previous one. A kill goal also needs its exact accepted task receipt;
        // a target handle alone is not evidence that this goal remains current in SA.
        kill_objectives_.erase(call.inputs[0]);
        const auto accepted=accepted_walk_tasks_.find(call.inputs[0]);
        if(result.executed && kill_acceptance && call.inputs.size()>=2) {
            const auto captured=accepted==accepted_walk_tasks_.end()?std::nullopt:std::optional<CapturedScriptTask>(accepted->second);
            std::optional<EpochScriptTask> owned;
            if(result.task_capture==NativeResult::TaskCapture::captured && captured && attack_policy_task_accept) {
                try {owned=attack_policy_task_accept(*captured);}catch(...) {}
            }
            if(owned && owned->epoch && owned->binding.slot==captured->slot && owned->binding.record==captured->record &&
               captured->record.opcode==call.opcode && captured->record.ped &&
               ((captured->record.status==0 && captured->record.event && !captured->record.task) ||
                ((captured->record.status==1 || captured->record.status==2 || (captured->record.status>=4 && captured->record.status<=6)) && captured->record.task && !captured->record.event))) {
                kill_objectives_[call.inputs[0]]=KillObjective{call.inputs[1],*owned};
            } else {
                try {if(diagnostic_event)diagnostic_event("Accepted kill objective has no verifiable epoch task receipt; 1593 reaction skipped");}catch(...){}
            }
        }
        if(opcode==COMMAND_DELETE_CHAR)attack_policy_.erase(call.inputs[0]);
    }
    if(opcode==COMMAND_LEAVE_GROUP || opcode==COMMAND_DELETE_CHAR){companions_.erase(call.inputs[0]);frozen_followers_.erase(call.inputs[0]);}
    if(!call.inputs.empty() && (operation.rfind("COMMAND_SET_CHAR_OBJ_",0)==0 || opcode==COMMAND_CHAR_SET_IDLE || opcode==COMMAND_DELETE_CHAR))traffic_waits_.erase(call.inputs[0]);
    if(opcode==COMMAND_DELETE_CHAR){front_seats_.erase(call.inputs[0]);rear_seats_.erase(call.inputs[0]);if(erase_pednode_seek)erase_pednode_seek(call.inputs[0]);pednode_seek_.erase(call.inputs[0]);running_chars_.erase(call.inputs[0]);worn_outfits_.erase(call.inputs[0]);equivalent_outfits_.erase(call.inputs[0]);equivalent_weapons_.erase(call.inputs[0]);}
    if(original_vehicle>=0 && result.values[0]>=0)original_vehicle_models_[result.values[0]]=original_vehicle;
    if(opcode==COMMAND_GET_NUM_OF_MODELS_KILLED_BY_PLAYER) {
        // Retail native handler sign-extends int16. Original RegisteredKills
        // exposes uint16, so recover the same bits before storing into SCM.
        vm.store(thread,int32_t(uint16_t(result.values[0])));return StepResult::continued;
    }
    if(opcode==COMMAND_GET_RADIO_CHANNEL) {
        // Both engines have different station IDs. Preserve native off and
        // map the genres of user-selected SA stations to original counterparts.
        static constexpr int32_t stations[]={10,8,4,0,8,3,8,0,4,2,7,9,10,10};
        const auto id=result.values[0];if(id<0 || size_t(id)>=std::size(stations)){thread.diagnostic="Unknown native radio channel";return StepResult::unsupported;}
        vm.store(thread,stations[id]);return StepResult::continued;
    }
    if(opcode==COMMAND_GET_CAR_MODEL) {
        const auto saved=original_vehicle_models_.find(call.inputs[0]);
        if(saved!=original_vehicle_models_.end()) {
            const auto expected=binding(saved->second);
            if(expected && expected->id==result.values[0]){vm.store(thread,saved->second);return StepResult::continued;}
        }
        int32_t original=-1;
        for(const auto& entity:models_)if(entity.second.kind==ModelKind::vehicle && entity.second.id==result.values[0] && (original<0 || entity.first<original))original=entity.first;
        if(original<0) {
            const auto native=result.values[0];
            const auto kind=native_model_class && native>=0 && native<20000 ? native_model_class(native) : std::optional<uint8_t>();
            if(!kind || *kind!=6){thread.diagnostic="GET_CAR_MODEL returned an invalid/unverified native vehicle model";return StepResult::unsupported;}
            original=native_vehicle_model_base+native;
            // The model remains usable in REQUEST/HAS/CREATE/IS_CAR_MODEL, but
            // is not falsely accepted as one of the original wanted vehicles.
            models_.emplace(original,Binding{native,ModelKind::vehicle});
            if(diagnostic_event)diagnostic_event("Native traffic vehicle identity: native="+std::to_string(native)+" originalAlias="+std::to_string(original));
        }
        vm.store(thread,original);return StepResult::continued;
    }
    if(opcode==COMMAND_DELETE_CAR)original_vehicle_models_.erase(call.inputs[0]);
    if(opcode==COMMAND_GET_CURRENT_CHAR_WEAPON || opcode==COMMAND_GET_CURRENT_PLAYER_WEAPON) {
        if(equivalent_entities) {
            const auto actor=equivalent_weapons_.find(call.inputs[0]);
            if(actor!=equivalent_weapons_.end()) {
                const auto original=actor->second.find(result.values[0]);
                if(original!=actor->second.end()){vm.store(thread,original->second);return StepResult::continued;}
            }
        }
        const auto counterpart=std::find(std::begin(weapon_counterparts),std::end(weapon_counterparts),result.values[0]);
        if(result.values[0]<0 || counterpart==std::end(weapon_counterparts)) {
            thread.diagnostic="Current native weapon has no original LCS counterpart";return StepResult::unsupported;
        }
        vm.store(thread,int32_t(counterpart-std::begin(weapon_counterparts)));return StepResult::continued;
    }
    if(opcode==COMMAND_GIVE_WEAPON_TO_CHAR || opcode==COMMAND_GIVE_WEAPON_TO_PLAYER) {
        NativeCall equip;equip.opcode=0x01B9;equip.inputs={call.inputs[0],call.inputs[1]};equip.mission_context=call.mission_context;
        const auto equipped=execute(equip);
        if(!equipped.executed){thread.diagnostic="Native equipped weapon could not match original GiveWeapon: "+equipped.diagnostic;return StepResult::unsupported;}
    }
    if(equivalent_entities && original_weapon>=0 && (opcode==COMMAND_GIVE_WEAPON_TO_CHAR || opcode==COMMAND_GIVE_WEAPON_TO_PLAYER || opcode==COMMAND_SET_CURRENT_CHAR_WEAPON || opcode==COMMAND_SET_CURRENT_PLAYER_WEAPON))
        equivalent_weapons_[call.inputs[0]][call.inputs[1]]=original_weapon;
    if(equivalent_entities && original_weapon>=0 && opcode==COMMAND_ATTACH_CHAR_TO_CAR)
        equivalent_weapons_[call.inputs[0]][call.inputs[7]]=original_weapon;
    if(opcode==COMMAND_LOAD_CUTSCENE) {current_cutscene_=original_scene;cutscene_started_=false;}
    if(opcode==COMMAND_START_CUTSCENE){cutscene_started_=true;if(arm_cutscene_audio)arm_cutscene_audio();}
    if(opcode==COMMAND_CLEAR_CUTSCENE) {current_cutscene_.reset();cutscene_started_=false;if(clear_cutscene_audio)clear_cutscene_audio();}
    if(opcode==COMMAND_REGISTER_MISSION_PASSED)shortcut_dropoff_.reset();
    if(opcode==COMMAND_CHANGE_GARAGE_TYPE) {
        // The name was resolved before native execution; update the corresponding
        // LCS declaration only after SA accepted the actual change.
        for(const auto& g:garages_)if(g.second==*call.text) {
            const auto declaration=garage_definitions_.find(g.first);
            if(declaration!=garage_definitions_.end())declaration->second.type=call.inputs[0];
        }
    }
    for(size_t i=0;i<call.outputs;++i) vm.store(thread,result.values[i]);
    if(call.predicate) vm.compare(thread,result.condition);
    return StepResult::continued;
}
void NativeAdapter::defer_companion(int32_t actor,Companion& companion,uint32_t now,const std::string& reason) {
    // A query/task rejection in this advisory updater must not stop the VM or
    // change its IP, objectives or mission flags. Retry only after 250 ms and
    // report a persistent failure at most once per five seconds.
    if(companion.deferred_reason!=reason || now-companion.diagnostic_at>=5000) {
        if(diagnostic_event)diagnostic_event("Companion boarding deferred: actor="+std::to_string(actor)+" vehicle="+std::to_string(companion.vehicle)+" reason="+reason);
        companion.diagnostic_at=now;
    }
    companion.deferred_reason=reason;
    companion.deferred_at=now;
}
bool NativeAdapter::update_companion_boarding(int32_t actor,Companion& companion,uint32_t now) {
    auto defer=[&](const std::string& reason,const NativeResult& native=NativeResult{}) {
        defer_companion(actor,companion,now,reason+(native.diagnostic.empty()?"":"; "+native.diagnostic));
        return false;
    };
    if(frozen_followers_.count(actor))return true;
    const auto leader=execute({0x01F5,{companion.player},{},0,1});
    if(!leader.executed || leader.values[0]<0)return defer("Cannot resolve native companion leader",leader);
    const auto leader_car=execute({0x00DF,{leader.values[0]},{},0,0,true}),actor_car=execute({0x00DF,{actor},{},0,0,true});
    if(!leader_car.executed)return defer("Cannot query real leader vehicle occupancy",leader_car);
    if(!actor_car.executed)return defer("Cannot query real companion vehicle occupancy",actor_car);
    if(!leader_car.condition || actor_car.condition) {
        if(actor_car.condition && companion.vehicle>=0 && diagnostic_event)
            diagnostic_event("Companion vehicle occupancy observed: actor="+std::to_string(actor));
        companion.vehicle=-1;
        return true;
    }
    if(!char_in_control)return defer("Native companion control query unavailable");
    const auto control=char_in_control(actor);if(!control)return defer("Cannot query native companion control state");
    if(!*control)return true;
    const auto car=execute({0x03C0,{leader.values[0]},{},0,1});
    if(!car.executed || car.values[0]<0)return defer("Cannot resolve leader's actual vehicle",car);
    const auto driver=execute({0x046C,{car.values[0]},{},0,1});
    if(!driver.executed)return defer("Cannot resolve actual vehicle driver",driver);
    if(driver.values[0]!=leader.values[0])return true;
    const auto a=execute({0x00A0,{actor},{},0,3}),b=execute({0x00A0,{leader.values[0]},{},0,3});
    if(!a.executed)return defer("Cannot query companion boarding position",a);
    if(!b.executed)return defer("Cannot query leader boarding position",b);
    float squared=0;
    for(size_t i=0;i<3;++i) {
        float x,y;std::memcpy(&x,&a.values[i],4);std::memcpy(&y,&b.values[i],4);
        if(!std::isfinite(x) || !std::isfinite(y))return defer("Invalid companion boarding position");
        squared+=(x-y)*(x-y);
    }
    if(squared>30.0f*30.0f)return true;
    if(!char_entering_as_passenger)return defer("Native passenger task tree query unavailable");
    const auto entering=char_entering_as_passenger(actor);
    if(!entering)return defer("Cannot inspect actual native passenger task tree");
    // A live entry tree is stronger evidence than a queued script record. Never
    // restart its walk/entry animation for the same leader vehicle. A queued
    // script status cannot prove that a task is live; do not query 062E here.
    if(*entering && (companion.vehicle<0 || companion.vehicle==car.values[0]))return true;
    if(companion.vehicle==car.values[0] && now-companion.requested_at<2000)return true;
    const auto capacity=execute({0x01EA,{car.values[0]},{},0,1});
    if(!capacity.executed)return defer("Cannot query native passenger capacity",capacity);
    if(capacity.values[0]<0 || capacity.values[0]>8)return defer("Invalid native passenger capacity="+std::to_string(capacity.values[0]));
    // SA indexes a motorcycle's only passenger seat as zero. A rear-seat
    // preference must not exclude it, or a one-passenger vehicle cannot board.
    const auto seat_rules=active_seat_rules(actor);
    if(!seat_rules)return defer("Cannot confirm live companion seat policy identity");
    const auto [front,rear_only]=*seat_rules;
    if(front && rear_only)return defer("Incompatible original front/rear passenger policy");
    const int32_t first=!front && rear_only && capacity.values[0]>1?1:0;
    int32_t seat=-1;
    for(int32_t candidate=first;candidate<capacity.values[0] && (!front || candidate==0);++candidate) {
        const auto free=execute({0x0431,{car.values[0],candidate},{},0,0,true});
        if(!free.executed)return defer("Cannot query actual passenger seat availability",free);
        if(free.condition){seat=candidate;break;}
    }
    if(seat<0)return true;
    const bool retry=companion.vehicle==car.values[0];
    // Record the attempt even on executor rejection: the engine might have
    // accepted a task before a plugin fault. Observe actual state before retry.
    companion.vehicle=car.values[0];companion.requested_at=now;
    NativeCall enter{0x05CA,{actor,car.values[0],-1,seat}};enter.mission_context=companion.mission;
    const auto assigned=execute(enter);
    if(!assigned.executed)return defer("Cannot assign native companion passenger entry",assigned);
    if(diagnostic_event)diagnostic_event("Companion passenger request: actor="+std::to_string(actor)+" vehicle="+std::to_string(car.values[0])+" seat="+std::to_string(seat)+" capacity="+std::to_string(capacity.values[0])+" retry="+(retry?"1":"0"));
    return true;
}
bool NativeAdapter::tick_scripted_peds(std::string& diagnostic) {
    diagnostic.clear();
    auto fail=[&](const char* reason){diagnostic=reason;return false;};
    if(pednode_seek_.empty() && companions_.empty() && traffic_waits_.empty())return true;
    if(!world_ready || !execute)return fail("Native scripted pedestrian update backend unavailable");
    if(!traffic_waits_.empty()) {
        const auto clock=execute({0x01BD,{}, {},0,1});if(!clock.executed)return fail("Cannot read original pedestrian signal clock");
        const auto now=uint32_t(clock.values[0]);
        for(auto it=traffic_waits_.begin();it!=traffic_waits_.end();) {
            const auto exists=execute({0x056D,{it->first},{},0,0,true});
            if(!exists.executed)return fail("Cannot resolve waiting pedestrian");
            if(!exists.condition){it=traffic_waits_.erase(it);continue;}
            if(now-it->second<500){++it;continue;}
            const auto phase=now%16384u;
            if(phase>=12000 && phase<15384){it=traffic_waits_.erase(it);continue;}
            if(!execute({0x05BA,{it->first,500}}).executed)return fail("Cannot maintain original pedestrian signal wait");
            it->second=now;++it;
        }
    }
    for(auto it=pednode_seek_.begin();it!=pednode_seek_.end();) {
        const auto exists=execute({0x056D,{it->first},{},0,0,true});
        if(!exists.executed)return fail("Cannot resolve navigation actor");
        if(!exists.condition){if(erase_pednode_seek)erase_pednode_seek(it->first);it=pednode_seek_.erase(it);continue;}
        const auto dead=execute({0x0118,{it->first},{},0,0,true});
        if(!dead.executed)return fail("Cannot query navigation actor life state");
        if(!dead.condition && (!set_pednode_seek || !set_pednode_seek(it->first,it->second)))return fail("Cannot update native pedestrian seek task");
        ++it;
    }
    uint32_t companion_now=0;
    if(!companions_.empty()) {
        const auto clock=execute({0x01BD,{}, {},0,1});
        if(!clock.executed) {
            if(!companion_clock_deferred_ && diagnostic_event)diagnostic_event("Companion boarding deferred: clock unavailable; "+clock.diagnostic);
            companion_clock_deferred_=true;
            return true;
        }
        if(companion_clock_deferred_ && diagnostic_event)diagnostic_event("Companion boarding clock recovered");
        companion_clock_deferred_=false;
        companion_now=uint32_t(clock.values[0]);
    }
    for(auto it=companions_.begin();it!=companions_.end();) {
        const auto actor=it->first;auto& companion=it->second;
        if(!companion.deferred_reason.empty() && companion_now-companion.deferred_at<250){++it;continue;}
        const auto exists=execute({0x056D,{actor},{},0,0,true});
        if(!exists.executed){defer_companion(actor,companion,companion_now,"Cannot resolve native companion; "+exists.diagnostic);++it;continue;}
        if(!exists.condition){frozen_followers_.erase(actor);it=companions_.erase(it);continue;}
        const auto dead=execute({0x0118,{actor},{},0,0,true}),member=execute({0x06EE,{actor,companion.group},{},0,0,true});
        if(!dead.executed || !member.executed){defer_companion(actor,companion,companion_now,"Cannot query native companion life/group state; "+(!dead.executed?dead.diagnostic:member.diagnostic));++it;continue;}
        if(dead.condition || !member.condition){frozen_followers_.erase(actor);it=companions_.erase(it);continue;}
        ++it;
        if(update_companion_boarding(actor,companion,companion_now) && !companion.deferred_reason.empty()) {
            if(diagnostic_event)diagnostic_event("Companion boarding recovered: actor="+std::to_string(actor));
            companion.deferred_reason.clear();
        }
    }
    return true;
}
void NativeAdapter::stop_scripted_ped_updates(){accepted_walk_tasks_.clear();kill_objectives_.clear();pednode_seek_.clear();traffic_waits_.clear();companions_.clear();frozen_followers_.clear();companion_clock_deferred_=false;}
void NativeAdapter::bind_vehicle_colours(const std::vector<int32_t>& colours) {
    if(colours.empty() || colours.size()>256 ||
       std::any_of(colours.begin(),colours.end(),[](int32_t colour){return colour<0 || colour>255;}))
        throw std::runtime_error("Invalid mounted original vehicle colour map");
    vehicle_colour_map_=colours;
}
bool NativeAdapter::release_script_markers() {
    if(!execute)return owned_blips_.empty() && owned_spheres_.empty();
    for(auto item:std::initializer_list<std::pair<std::unordered_set<int32_t>*,uint16_t>>{{&owned_blips_,0x0164},{&owned_spheres_,0x03BD}}) {
        for(auto it=item.first->begin();it!=item.first->end();) {
            if(execute({item.second,{*it}}).executed)it=item.first->erase(it);else ++it;
        }
    }
    return owned_blips_.empty() && owned_spheres_.empty();
}
}
