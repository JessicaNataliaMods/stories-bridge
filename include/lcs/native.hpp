#pragma once
#include "lcs/route_vehicle_native.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include "vm.hpp"
#include "phone_frame_coordinator.hpp"
#include "messages.hpp"
#include "entity_commands.hpp"
#include "statistics_commands.hpp"
#include "activity_statistics.hpp"
#include "environment_commands.hpp"
#include "garage_commands.hpp"
#include "traffic_commands.hpp"
#include "script_task_capture.hpp"
#include "script_task_epochs.hpp"
#include "objective_receipts.hpp"
#include "fx_particles.hpp"
#include <unordered_map>
#include <optional>
#include <unordered_set>
#include <istream>

namespace lcs {
// A separate SA command packet. Original LCS instructions never enter SA's VM.
struct NativeCall {
    uint16_t opcode = 0;
    std::vector<int32_t> inputs;
    std::optional<std::string> text;
    size_t text_position = 0;
    size_t outputs = 0;
    bool predicate = false;
    // Only DECLARE_MISSION_FLAG uses the reserved native bootstrap global.
    bool mission_flag_reference = false;
    // Register native entities for mission cleanup, without taking ownership of
    // SA's shared mission-local variable bank. Output locals belong to this call.
    bool mission_context = false;
    std::optional<std::string> replacement_text;
    // Scoped native text policy. Never changes unrelated scripts' next brief.
    bool suppress_previous_brief = false;
    // Source-specific task construction. SA's ordinary 05DE randomizes the
    // direction and 05D0 is empty; these fields select guarded native task
    // constructors before the packet dispatcher, never an ignored operand.
    std::optional<int32_t> source_wander_direction;
    bool source_wait_in_car=false;
    std::optional<EntityCommand> source_entity_command;
    std::optional<SourceStatistic> source_statistic;
    // False selects ordinary source rampages, true requires headshots.
    // SA 0367 is empty; both use a typed Darkel operation.
    std::optional<bool> source_frenzy_headshots;
    std::optional<EnvironmentCommand> source_environment;
    std::optional<GarageCommand> source_garage;
    std::optional<TrafficCommand> source_traffic;
    std::vector<uint8_t> encode() const;
};
struct NativeResult {
    bool executed = false, condition = false;
    std::array<int32_t,32> values{};
    std::string diagnostic;
    // True once SA's dispatcher was entered, even if it faulted or consumed
    // a different packet layout. Failure then cannot be replayed safely.
    bool native_entered=false;
    enum class TaskCapture : uint8_t {not_requested,captured,unavailable_before,unavailable_after,actor_changed,no_unique_record};
    TaskCapture task_capture=TaskCapture::not_requested;
    std::optional<CapturedScriptTask> accepted_script_task;
};
using NativeExecutor=std::function<NativeResult(const NativeCall&)>;
#include "lcs/exclusive_garages.hpp"
#include "lcs/crane_service.hpp"
#include "lcs/ferry_service.hpp"
NativeExecutor guard_native_executor(NativeExecutor);
// Read-only SA script task-sequence pool. active is the global under construction
// (-1 none). open is the per-slot "being constructed" flag (set by 0615, cleared by
// 0616). A slot is allocated while open or while its first task pointer is set
// (the allocator 632E00 tests exactly these). references/pending are the task
// reference count and the deferred-removal flag that 061B uses when a performing
// ped still holds the sequence. generation is the uint16 handle generation; a
// handle is (generation<<16)|slot, so valid handles can be negative int32 values.
struct TaskSequenceState {
    int32_t active=-1;
    std::array<uint8_t,64> open{},pending{},issued{};
    std::array<uint32_t,64> first_task{},references{};
    std::array<uint16_t,64> generation{};
    bool allocated(size_t slot)const{return open[slot] || first_task[slot];}
};
enum class ModelKind {object,ped,vehicle,world_collision};
// Opaque identities for genuine SA vehicles with no LCS donor. They must never
// compare equal to an original mission/export model ID.
constexpr int32_t native_vehicle_model_base=0x40000000;
struct JumpRecords {float distance=0,height=0;int32_t flips=0,spins=0,best_stunt=0;};
class NativeAdapter {
    std::vector<int32_t> vehicle_colour_map_;
    const ScmImage& image_;
    std::unordered_set<int32_t> owned_blips_,owned_spheres_;
    struct Binding {int32_t id;ModelKind kind;};
    std::unordered_map<int32_t,Binding> models_;
    std::unordered_map<std::string,Binding> named_models_;
    std::unordered_map<std::string,std::string> cutscenes_;
    std::unordered_map<int32_t,int32_t> ped_types_;
    std::unordered_map<int32_t,std::string> garages_;
    // A script handle alone does not establish a continuing native pedestrian.
    // Retain the exact live pool identity along with each active LCS seat rule.
    struct SeatRule {ObjectiveActor actor;};
    std::unordered_map<int32_t,SeatRule> rear_seats_;
    std::unordered_map<int32_t,SeatRule> front_seats_;
    // nullopt means the native pool identity cannot be proved; do not assign a
    // potentially wrong passenger seat. A recycled handle drops old policies.
    std::optional<std::pair<bool,bool>> active_seat_rules(int32_t ped);

    std::unordered_map<int32_t,bool> running_chars_;
    std::unordered_map<int32_t,NativeCall> walking_objectives_;
    std::unordered_map<int32_t,CapturedScriptTask> accepted_walk_tasks_;
    void observe_source_goal_unknown(int32_t actor)noexcept;
    void observe_source_actor_acquired(int32_t actor);
    void observe_source_running(int32_t actor,bool enabled)noexcept;
    NativeResult execute_projected_objective(uint16_t,const NativeCall&,bool&,const std::function<NativeResult()>&);
    void forget_walking_objective(int32_t actor){walking_objectives_.erase(actor);accepted_walk_tasks_.erase(actor);kill_objectives_.erase(actor);observe_source_goal_unknown(actor);}
    // 1593 SET_CHAR_ATTACKS_PLAYER_WITH_COPS. The script's explicit value per exact live ped (attacks=true: the native SA
    // reaction is kept; false: the retail "flee while the player has cops" reaction is applied), and accepted kill goals
    // with their exact task receipt so the reaction only concerns a goal the script gave and SA still has active.
    struct AttackPolicy {ObjectiveActor actor;bool attacks=false;};
    std::unordered_map<int32_t,AttackPolicy> attack_policy_;
    struct KillObjective {int32_t target=-1;EpochScriptTask task;};
    std::unordered_map<int32_t,KillObjective> kill_objectives_;
    std::unordered_map<int32_t,bool> pednode_seek_;
    std::unordered_map<int32_t,uint32_t> traffic_waits_;
    struct Companion {
        int32_t player,group,vehicle=-1;
        bool mission=false;
        uint32_t requested_at=0,deferred_at=0,diagnostic_at=0;
        std::string deferred_reason;
    };
    std::unordered_map<int32_t,Companion> companions_;
    struct FrenzyAlias {int32_t actor,weapon,original;std::optional<int32_t> previous;};
    std::optional<FrenzyAlias> frenzy_alias_;
    bool refresh_frenzy_alias();
    // Source globals start at zero; sunny (source type zero) maps to native 1.
    // Order: interpolation, rain, new type, old type; then hours/minutes/seconds.
    std::array<int32_t,4> stored_weather_{{0,0,1,1}};
    std::array<int32_t,3> stored_clock_{};
    bool companion_clock_deferred_=false;
    void defer_companion(int32_t,Companion&,uint32_t,const std::string&);
    bool update_companion_boarding(int32_t,Companion&,uint32_t);
    std::unordered_set<int32_t> frozen_followers_;
    std::optional<std::string> current_cutscene_;
    std::vector<std::array<float,4>> shortcut_pickups_;
    std::optional<std::array<float,4>> shortcut_dropoff_;
    struct Phone {std::array<float,3> position;bool message_removed=false,enabled=false;PhoneFrameCoordinator frames;std::optional<PhoneFrameResult> reported;};
    std::vector<Phone> phones_;
    size_t claimed_phones_=0;
    std::unordered_map<int32_t,int32_t> building_groups_;
    std::unordered_set<int32_t> building_swap_models_;
    int32_t next_garage_=0;
    struct GarageDefinition {
        std::array<float,8> bounds;int32_t type;
        bool camera_follows_player=false;
        uint8_t max_stored_cars=4;
    };
    std::unordered_map<int32_t,GarageDefinition> garage_definitions_;
    struct CraneDeclaration {
        std::array<float,3> origin{};
        int32_t model=0;
        // Original pickup rectangle, drop-off point and heading in radians.
        std::array<float,4> pickup{};
        std::array<float,3> dropoff{};
        float heading=0;
        bool activated=false,crusher=false;
    };
    std::vector<CraneDeclaration> cranes_;
    std::optional<uint32_t> fort_staunton_flag_;
    bool developer_flag_=false;
    int32_t kill_frenzies_total_=0,export_cars_total_=0;
    JumpRecords jump_records_;
    ActivityStatistics activity_statistics_;
    bool rubbish_visible_=true; // SA's CRubbish::SetVisibility is an empty renderer entry point.
    bool cutscene_started_ = false;
    std::array<float,3> scene_light_direction_{};
    int32_t original_radio_channel_=10,original_radio_seek_=-1,original_music_mode_=1;
    int32_t last_random_ped_=-1;
    uint32_t outfit_changes_=0;
    std::string stored_outfit_;
    std::unordered_map<int32_t,std::string> equivalent_outfits_;
    struct WornOutfit {std::string label;int32_t native_model;};
    std::unordered_map<int32_t,WornOutfit> worn_outfits_;
    std::unordered_map<int32_t,std::unordered_map<int32_t,int32_t>> equivalent_weapons_;
    struct OriginalWeaponDefinition {int32_t model=-1,slot=0;};
    std::array<std::optional<OriginalWeaponDefinition>,37> weapon_definitions_{};
    std::unordered_map<int32_t,int32_t> original_vehicle_models_;
    std::unordered_map<int32_t,std::array<int32_t,2>> gang_models_;
    std::unordered_map<int32_t,int32_t> gang_cars_;
    std::unordered_map<int32_t,uint32_t> nonped_threats_;
    struct ZonePopulation {
        std::array<std::optional<std::array<int32_t,12>>,2> pedestrians,cars,civilians;
        std::array<std::optional<int32_t>,2> groups;
    };
    std::unordered_map<std::string,ZonePopulation> zone_population_;
    std::array<std::vector<std::array<float,6>>,2> level_road_regions_;
    struct OriginalZone {std::string name;int32_t type=0,level=0;std::array<float,6> bounds{};};
    std::vector<OriginalZone> zones_;
    std::optional<Binding> binding(int32_t id)const;
    std::vector<uint8_t> save_state_projected(uint32_t now_ms) const;
public:
    // SA facts for the 1593 reaction; the exact ped identity is re-proved by the provider on every read.
    struct AttackPolicyActor {bool alive=false,on_foot=false,cop=false,released=false;};
    struct AttackPolicyWorld {int32_t player_ped=-1;bool player_alive=false,cops_present=false;};
    std::function<std::optional<AttackPolicyActor>(const ObjectiveActor&)> attack_policy_actor;
    std::function<std::optional<AttackPolicyWorld>()> attack_policy_world;
    // The existing task observer gives the accepted capture an epoch, then refreshes event-to-task association before
    // the exact active-primary-task check. Missing ownership never creates a kill goal.
    std::function<std::optional<EpochScriptTask>(const CapturedScriptTask&)> attack_policy_task_accept;
    std::function<std::optional<bool>(const ObjectiveActor&,const EpochScriptTask&)> attack_policy_task_active;
    // Frame consumer of the retail reader at 0x31CE38: a ped released from script control that still carries its exact
    // accepted script task to kill the player, whose value is FALSE while the player has cops, switches to FLEE_ON_FOOT_TILL_SAFE once.
    // Mission-controlled peds, cops, seated peds and TRUE values are never touched. true = nothing blocked the frame.
    bool tick_attack_with_cops();
    NativeResult execute_collective_flee(const ObjectiveActor&,const ObjectiveActor&);
    // Finite SA flee packets shared by 408/606/614; the SA flee tasks own safety/completion.
    NativeResult execute_collective_flee_till_safe(const ObjectiveActor&,const ObjectiveActor&);
    NativeResult execute_flee_point_till_safe(const ObjectiveActor&);
    // Current SA membership; group is its generation-bearing script handle.
    struct PedGroupFacts {bool member=false,leader=false;uintptr_t leader_ped=0;int32_t group=-1;bool free_group=false;};
    std::function<std::optional<PedGroupFacts>(const ObjectiveActor&)> ped_group;
    // Exact native observation needed before opening/recovering a flee sequence.
    std::function<std::optional<TaskSequenceState>()> task_sequence_state;
    // Abort an empty OPEN that faulted before issuing a new script handle.
    std::function<bool(size_t,uint16_t,uint8_t)> abort_unissued_task_sequence;
private:
    bool flee_point_packet(int32_t ped,NativeCall& call,bool& in_vehicle,std::string& diagnostic);
    NativeResult execute_flee(const NativeCall& flee,bool in_vehicle);
    NativeResult execute_task_sequence(int32_t ped,const std::vector<NativeCall>& tasks,bool repeat=false);
    NativeResult guard_spot(int32_t ped,const std::array<int32_t,3>& position_bits);
    NativeResult flee_car(int32_t ped,int32_t car);
    NativeResult leave_all_occupants(int32_t car);
    NativeResult dive_from_car(int32_t ped,int32_t car);
    NativeResult join_leader_group(int32_t follower,int32_t leader,bool mission_context);
public:
    // Becomes true only after a native world loader verifies the mounted world.
    bool world_ready = false;
    bool text_ready = false;
    int32_t unique_jumps_total = 0;
    bool reuse_native_player = false;
    // User-authorized mission-first profile: existing SA vehicles/peds/weapons.
    bool equivalent_entities = false;
    // Current mounted IPLs share SA area zero. LCS island-streaming boundaries
    // remain declarations; they are not native SA interior identifiers.
    bool flat_native_world = false;
    // The user deferred the optional post-death taxi. Its original route
    // declarations still execute and persist; they cannot block story startup.
    bool recovery_taxi_deferred = false;
    size_t shortcut_pickup_count()const {return shortcut_pickups_.size();}
    const std::vector<std::array<float,4>>& shortcut_pickups()const {return shortcut_pickups_;}
    // Read only the loaded world IPLs, never the duplicate aggregate world.ipl.
    void load_phone_placements(std::istream&,int32_t native_phone_model);
    void load_original_zones(std::istream&);
    size_t claimed_phone_count()const {return claimed_phones_;}
    size_t world_phone_count()const {return phones_.size();}
    void tick_phones();
    std::function<bool()> phone_phase_available;
    std::function<PhoneFrameResult(const std::array<float,3>&,PhoneFrameCoordinator&)> update_phone_frame;
    void load_crane_placements(std::istream&,int32_t native_crusher_model);
    size_t active_crane_declarations()const;
    std::optional<uint8_t> declared_garage_capacity(int32_t id)const;
    std::optional<bool> declared_garage_player_camera(int32_t id)const;
    std::optional<uint32_t> fort_staunton_flag()const {return fort_staunton_flag_;}
    int32_t kill_frenzies_total()const {return kill_frenzies_total_;}
    int32_t export_cars_total()const {return export_cars_total_;}
    const JumpRecords& jump_records()const {return jump_records_;}
    const ActivityStatistics& activity_statistics()const {return activity_statistics_;}
    int32_t music_mode()const {return original_music_mode_;}
    std::function<bool(const Thread&)> mission_script;
    std::function<std::optional<int32_t>(int32_t)> stored_player_weapon;
    std::function<std::optional<int32_t>(std::optional<int32_t>)> camera_ped_zoom;
    std::function<bool(bool)> freeze_ped_zoom;
    std::function<bool(bool)> disable_pause_menu;
    std::function<bool(const std::array<float,7>&,bool,bool)> switch_angled_roads;
    std::function<std::optional<ObjectiveActor>(int32_t)> live_ped_identity;
    std::function<std::optional<bool>(int32_t)> char_in_control;
    std::function<std::optional<bool>(int32_t)> char_entering_as_passenger;
    std::function<std::optional<uint8_t>(int32_t)> native_model_class;
    std::function<std::optional<int32_t>(int32_t)> native_weapon_slot;
    // Read-only live pickup query; the returned model is the *native object*
    // model at the last active pickup inside the sphere, not an invented weapon.
    // nullopt means native pool/profile could not be verified; -1 means no hit.
    std::function<std::optional<int32_t>(const std::array<float,4>&)> pickup_model_in_area;

    std::function<std::optional<bool>(int32_t,int32_t)> can_see_dead_char;
    std::function<bool(uint16_t)> set_threat_range_multiplier;
    std::function<void(const std::string&)> diagnostic_event;
    // 1084/930/947: LCS ParticleObject emitters and single particles over SA's FxManager.
    FxParticles fx;
    std::function<std::optional<int32_t>(int32_t)> scripted_pad_state;
    std::function<std::optional<bool>()> player_made_safe;
    std::function<std::optional<int32_t>(const std::array<float,4>&,const std::array<int32_t,3>&,int32_t,bool)> random_char_in_area;
    std::function<std::optional<bool>(const std::array<float,6>&)> sniper_bullet_in_area;
    std::function<NativeResult(const std::array<float,4>&,const std::array<int32_t,5>&,
        const std::array<int32_t,12>&,int32_t,bool,const std::function<bool(int32_t)>&)> random_cop_in_area;
    // 1494: area, mission cleanup, wait-state query, LastRandomPedId commit.
    std::function<NativeResult(const std::array<float,4>&,bool,
        const std::function<std::optional<bool>(int32_t)>&,const std::function<bool(int32_t)>&)> random_char_unchecked;
    // 1486: native turn force (vehicle, ped, random factor) and the SOUND_CAR_JERK one-shot at the vehicle position.
    std::function<NativeResult(int32_t,int32_t,float)> apply_car_tilt;
    std::function<bool(const std::array<float,3>&)> car_jerk_sound;
    // 738: loaded LCS zone box, kind flags, LastRandomPedId, mission cleanup, control query, commit.
    std::function<NativeResult(const std::array<float,6>&,const std::array<int32_t,3>&,int32_t,bool,
        const std::function<std::optional<bool>(int32_t)>&,const std::function<bool(int32_t)>&)> random_char_in_zone;
    std::function<std::optional<std::string>(const std::array<float,8>&,int32_t,int32_t)> create_garage;
    std::function<bool(const std::string&)> rotate_garage_door;
    std::function<bool(const std::string&)> leave_garage_camera;
    std::function<bool(const std::string&,uint8_t)> set_garage_capacity;
    std::function<bool()> clear_garage_message;
    std::function<bool(int32_t,bool)> request_wait_animation;
    std::function<std::optional<bool>(const std::string&,uint16_t)> named_animation;
    std::function<bool(int32_t,int32_t,int32_t,bool)> play_wait_animation;
    std::function<bool(int32_t)> clear_wait_animation;
    // Same owned wait-state records that play/clear_wait_animation mutate.
    std::function<std::optional<bool>(int32_t)> wait_animation_active;
    std::function<bool(int32_t,bool,int32_t)> set_char_crouch;
    std::function<bool(int32_t,bool)> set_char_can_fight;
    std::function<bool(int32_t,bool)> set_stationary_shooter;
    std::function<bool(int32_t,bool)> set_char_heed_threats;
    std::function<bool(int32_t,int32_t,bool)> set_gang_damage;
    std::function<bool(int32_t)> set_char_crouch_when_threatened;
    // 1383: TRUE is served by SA's own acquaintance scanner, whose line of sight already blocks on objects for every
    // actor; FALSE is not served.
    std::function<bool(int32_t,bool)> set_char_threats_behind_objects;
    std::function<bool(int32_t,bool)> set_pednode_seek;
    std::function<void(int32_t)> erase_pednode_seek;
    std::function<bool(int32_t,const std::string&)> set_char_personality;
    std::function<bool(const MissionMessage&)> display_message;
    std::function<NativeResult(const BigMissionMessage&)> display_big_message;
    // Current native player-info count/total; no cached or invented result.
    std::function<std::optional<int32_t>(bool,std::optional<int32_t>)> collectables;
    std::function<bool(const std::string&)> prepare_cutscene_audio;
    std::function<void()> arm_cutscene_audio,clear_cutscene_audio;
    std::function<NativeResult(bool)> ending_tune;
    // 1398: SA stuck evidence for one live ped; nullopt means the native state is unreadable.
    std::function<std::optional<bool>(const ObjectiveActor&)> char_stuck;
    std::function<NativeResult(int32_t)> set_bridge_state;
    std::function<NativeResult(bool)> switch_security_camera;
    // Unsigned source milliseconds; zero restores native SA combat cadence.
    std::function<bool(int32_t,uint32_t)> set_char_attack_timer;
    std::function<bool(int32_t,uint32_t)> set_char_shoot_timer;
    // Retail1554 uses zero for no threat; nullopt means a failed native read.
    std::function<std::optional<int32_t>(int32_t)> get_char_threat_char;
    std::function<NativeResult(int32_t,bool)> set_char_player_friend;
    std::function<void(int32_t)> player_friend_actor_created;
    std::function<NativeResult(int32_t,bool)> set_car_reward;
    // Absence of current or suspended SA script objectives; unavailable is
    // distinct from both TRUE and FALSE and never proves objective completion.
    std::function<std::optional<bool>(int32_t)> char_has_no_objective;
    std::function<NativeResult(int32_t)> override_char_move_anim;
    std::function<NativeResult(int32_t,bool)> set_char_jack_protection;
    std::function<NativeResult(int32_t)> clear_char_move_anim_override;
    std::function<bool(uint16_t)> set_enter_car_range_multiplier;
    // 299: script objective completion observed by the production route service.
    std::function<std::optional<bool>(int32_t)> char_objective_passed;
    ExclusiveGarages exclusive_garages;
    CraneService crane_service;
    FerryService ferry_service;
    std::optional<std::array<float,3>> crane_origin(size_t index)const{return index<cranes_.size()?std::optional<std::array<float,3>>(cranes_[index].origin):std::nullopt;}
    // 1642: keep the selected source island collision resident through SA streaming.
    std::function<NativeResult(int32_t,int32_t)> set_allowed_collision;
    // Observe the original area only after the real setter succeeds. The
    // mounted SA world stays flat; source music selection consumes this ID.
    std::function<void(int32_t)> source_music_area_changed;
    std::function<NativeResult(int32_t,const std::array<float,3>&,float,int32_t,bool)> char_follow_path;
    std::function<NativeResult(int32_t,bool)> hail_taxi;
    std::function<NativeResult(int32_t,int32_t)> world_swap_state;
    std::function<NativeResult()> request_world_swaps,commit_world_sector;
    std::function<std::optional<bool>()> world_streaming_complete;
    std::function<NativeResult(int32_t)> start_credits;
    std::function<NativeResult()> stop_credits;
    std::function<bool()> original_credits_finished;
    std::function<NativeResult(int32_t)> change_music_mode;
    using Executor=std::function<NativeResult(const NativeCall&)>;
    Executor execute;
    Executor execute_walking_objective;
    // The source vehicle controller preflights its original setter before the
    // native task, then commits the source goal after actual acceptance.
    Executor execute_vehicle_objective;
    std::function<NativeResult(uint16_t,const NativeCall&,bool& projected,const std::function<NativeResult()>&)> execute_source_objective;
    // Actual original CREATE_CHAR/DELETE_CHAR acceptance only. Never use a
    // pool poll or promoted ambient ped as a source constructor notification.
    std::function<void(ObjectiveActor)> source_actor_created,source_actor_deleted;
    // A script selected an existing ambient actor; register its lifetime without
    // granting CREATE_CHAR initialization or claiming an ambient objective latch.
    std::function<void(ObjectiveActor)> source_actor_acquired;
    std::function<void(ObjectiveActor,bool)> source_actor_running;
    // Revocation only: an unprojected accepted objective cannot keep a stale
    // source NONE/route declaration or grant fresh initialization authority.
    std::function<void(int32_t)> source_actor_goal_unknown;
    void clear_walking_for_source_route(int32_t actor){walking_objectives_.erase(actor);accepted_walk_tasks_.erase(actor);kill_objectives_.erase(actor);}
    // Private campaign/service state for a paired normal SA save. The image,
    // callbacks and live native pointers are never serialized. now_ms is the
    // current SA timer used to preserve relative service deadlines.
    std::vector<uint8_t> save_state(uint32_t now_ms) const;
    bool restore_state(const std::vector<uint8_t>& bytes,uint32_t now_ms,std::string& diagnostic);
    // Synchronous notifications from an observed native record mutation, on
    // the game thread. Later snapshots cannot substitute for these callbacks.
    void walking_task_associated(size_t slot,const ScriptTaskRecord& before,
                                 const ScriptTaskRecord& after,uint32_t task);
    void walking_task_invalidated(size_t slot);
    // Acceptance metadata only; not proof of a live task or goal completion.
    std::optional<CapturedScriptTask> captured_walking_task(int32_t actor)const {
        const auto found=accepted_walk_tasks_.find(actor);
        return found==accepted_walk_tasks_.end()?std::nullopt:std::optional<CapturedScriptTask>(found->second);
    }
    // Optional original Toni skins; two-phase outfit transition retains actor
    // identity. Only committed visual outfits satisfy IS_PLAYER_WEARING.
    std::function<bool(int32_t,const std::string&)> begin_outfit;
    std::function<bool(int32_t)> finish_outfit;
    std::function<std::optional<std::string>(int32_t)> current_outfit;
    // Reproduce LCS leader transport even when membership was assigned after
    // the native player's car-entry event. Never warp or invent occupancy.
    // Boarding assistance defers a failed query for that actor; it cannot abort
    // the original VM. Actual mission commands retain their normal diagnostics.
    bool tick_scripted_peds(std::string& diagnostic);
    void stop_scripted_ped_updates();
    explicit NativeAdapter(const ScmImage& image) : image_(image) {}
    // Failure cleanup removes only markers successfully created by this VM.
    bool release_script_markers();
    void bind_model(int32_t lcs_id,int32_t sa_id,const std::string& lcs_name,ModelKind kind=ModelKind::object);
    void bind_named_model(int32_t sa_id,const std::string& lcs_name,ModelKind kind);
    bool named_special_characters=false;
    void bind_vehicle_colours(const std::vector<int32_t>& colours);
    // Call only after this named scene's archive, models and animations are mounted.
    void bind_cutscene(const std::string& name,const std::string& native_name="");
    bool has_cutscene(const std::string& name)const;
    void bind_ped_type(int32_t original,int32_t native) {ped_types_[original]=native;}
    void load_weapon_definitions(std::istream&);
    // LCS uses numeric handles; SA garage opcodes consume eight-character names.
    // Call only after the world loader verifies the named native garage.
    void bind_garage(int32_t original,const std::string& native_name);
    bool has_active_cutscene()const {return current_cutscene_.has_value();}
    std::optional<int32_t> model(int32_t id) const;
    std::optional<int32_t> original_vehicle_model(int32_t handle);
    StepResult dispatch(Vm&,Thread&,uint16_t);
};
}
