#pragma once
#include "lcs/script_task_capture.hpp"
#include "native.hpp"
#include "audio.hpp"
#include "objective_receipts.hpp"
#include "mission_cleanup_receipt.hpp"
#include "collectives.hpp"
namespace lcs {
// Read-only snapshot on the game thread. It grants neither task ownership
// nor completion; capture around an accepted command still needs review.
std::optional<ScriptTaskSnapshot> sa_script_task_snapshot();
std::optional<ObjectiveActor> sa_live_objective_actor(int32_t handle);
// Game-thread stable weak cell. Caller must retain its address until release
// succeeds, even if registration failed after entering the native service.
bool sa_register_actor_reference(const ObjectiveActor&,uintptr_t* stable_cell);
bool sa_actor_reference_live(const ObjectiveActor&,const uintptr_t* stable_cell);
bool sa_release_actor_reference(uintptr_t* stable_cell);
// Original 1487: inspect live object model of last active pickup in a 3D sphere.
std::optional<int32_t> sa_pickup_model_in_area(const std::array<float,4>&);
// Read-only active primary tree membership. Caller still needs continuously
// observed lifecycle ownership; pointer membership alone is not completion.
std::optional<bool> sa_active_walking_task(const ObjectiveActor&,const CapturedScriptTask&);
// Execute a reviewed walking packet once and capture its native acceptance.
// Capture loss does not replace or replay a successful engine result.
NativeResult sa_execute_walk_with_capture(const NativeCall& call);
// Private source route motion factory. Not registered with the SCM until the
// route controller's lifecycle ownership observations are connected.
struct RouteMotionRequest {
    ObjectiveActor actor;
    std::array<float,3> target{};
    int32_t move_state=0; // observed/translated native WALK/RUN/SPRINT
    bool mission_cleanup=false;
    // Optional for initial isolated construction; a live route replacement
    // must provide this ownership/control gate. Called before allocation and
    // again immediately before assignment, after native construction callbacks.
    bool (*authorized)(void*)=nullptr;
    void* authorization_context=nullptr;
    bool track_owned_lifetime=false;
};
NativeResult sa_create_route_motion(const RouteMotionRequest&);
// Read-only native pose/control. This is not source objective ownership or completion.
struct RouteActorSnapshot {
    ObjectiveActor actor;
    std::array<float,3> position{};
    bool controllable=false,on_foot=false,source_foot_controllable=false;
    // Original SetSeek refuses FOLLOW_PATH even when IsPedInControl is true.
    bool source_seek_allowed=true,source_driving=false;
};
std::optional<RouteActorSnapshot> sa_route_actor_snapshot(const ObjectiveActor&);
// Native membership facts only; source objective/leader semantics are separate.
// group is the native CPedGroups slot (0..7) when member, otherwise -1.
// Generation-bearing SA script handle, never the raw group pool slot.
struct NativePedGroupSnapshot {bool member=false,leader=false;uintptr_t leader_ped=0;int32_t group=-1;bool free_group=false;};
std::optional<NativePedGroupSnapshot> sa_ped_group_snapshot(const ObjectiveActor&);
// Read-only task-sequence pool/global state; nullopt when profile or state is not provable.
std::optional<TaskSequenceState> sa_task_sequence_state();
bool sa_abort_unissued_task_sequence(size_t slot,uint16_t previous_generation,uint8_t previous_issued);
// Raw native facts. Caller must use approved donor/source model mapping and
// independently reviewed state translation before building source eligibility.
struct NativeCopSnapshot {
    ObjectiveActor actor;std::array<float,3> position{};
    uint16_t native_model=0;int32_t native_type=0,native_state=0;uint8_t created_by=0;
    bool controllable=false,removed=false,fading=false;
    NativePedGroupSnapshot group;
};
std::optional<NativeCopSnapshot> sa_cop_snapshot(const ObjectiveActor&);
std::optional<MissionCleanupSnapshot> sa_mission_cleanup_snapshot();
std::optional<uint32_t> sa_mission_ped_count();
struct NativeThreatSnapshot {ObjectiveActor actor;uintptr_t intelligence=0;std::array<uint8_t,3> responses{};};
std::optional<NativeThreatSnapshot> sa_char_threat_snapshot(const ObjectiveActor&);
bool sa_cop_creation_profile_matches();
NativeResult sa_cop_set_mission_creation(const NativeThreatSnapshot&);
NativeResult sa_cop_suppress_threats(const NativeThreatSnapshot&);
NativeResult sa_cop_increment_mission_count(const NativeThreatSnapshot&,uint32_t expected_count);
NativeResult sa_cop_register_cleanup(const NativeThreatSnapshot&,const MissionCleanupSnapshot&,uint32_t expected_count);
// Caller commits the original shared LastRandomPedId before native promotion.
// Selection eligibility and operand/output validation precede this operation.
NativeResult sa_accept_random_cop(const NativeCopSnapshot&,bool mission_cleanup,const std::function<bool()>& commit_selection);
// Raw native facts read by retail 1494 plus type/owner for promotion receipts.
struct NativeUncheckedCharSnapshot {
    ObjectiveActor actor;std::array<float,3> position{};
    int32_t native_type=0;uint8_t created_by=0;bool removed=false,fading=false;
    // 738's SA selection gates; 1494 deliberately does not use these.
    int32_t native_state=0;bool has_vehicle=false;
};
std::optional<NativeUncheckedCharSnapshot> sa_unchecked_char_snapshot(const ObjectiveActor&);
// 1398: SA CPedIntelligence static counter vs the native go-to task limit; nullopt when unreadable.
std::optional<bool> sa_char_stuck(const ObjectiveActor&);
// 1494 promotion: commit (LastRandomPedId) first, then SetCharCreatedBy(2),
// threat suppression, mission count and optional unreserved cleanup.
NativeResult sa_accept_unchecked_char(const NativeUncheckedCharSnapshot&,bool mission_cleanup,const std::function<bool()>& commit_selection);
std::optional<std::vector<ObjectiveActor>> sa_peds_descending();
std::optional<std::vector<CollectiveCandidate>> sa_collective_candidates(float x,float y,float radius);
bool sa_collectives_available();
std::optional<std::array<float,3>> sa_focused_player_position();
struct NativeFrameClock {uint32_t now=0,previous=0,frame=0;};
std::optional<NativeFrameClock> sa_frame_clock();
std::optional<std::array<float,3>> sa_camera_placeable_position();
std::optional<SpatialListener> sa_phone_camera_listener();
std::optional<bool> sa_phone_slow_motion();
NativeResult sa_phone_random_sample();
bool sa_phone_random_ready();
struct NativePhoneBooth {
    int32_t handle=-1;uintptr_t entity=0,matrix=0,rw_object=0;
    uint16_t model=0,seed=0;std::array<float,3> position{};std::optional<float> up_z;
};
// Available empty snapshot means no matching live building; nullopt means
// unknown ABI/corrupt or ambiguous identity. Neither grants a render mutation.
std::optional<NativePhoneBooth> sa_phone_booth_snapshot(int32_t model,const std::array<float,3>& position);
bool sa_phone_render_profile_matches();
struct NativePhoneRenderSnapshot {NativePhoneBooth booth;uintptr_t frame=0,root=0,engine=0;};
std::optional<NativePhoneRenderSnapshot> sa_phone_render_snapshot(const NativePhoneBooth&);
NativeResult sa_phone_apply_visual(const NativePhoneRenderSnapshot&,float up_z);
NativeResult sa_phone_write_up_z(const NativePhoneRenderSnapshot&,float up_z);
NativeResult sa_phone_refresh_visual(const NativePhoneRenderSnapshot&);
// 738: zone = loaded LCS zone box (min xyz, max xyz). `excluded` is LastRandomPedId, in_control the
// native IsPedInControl query, commit stores the new LastRandomPedId before promotion.
NativeResult sa_random_char_in_zone(const std::array<float,6>& zone,const std::array<int32_t,3>& kinds,int32_t excluded,bool mission_cleanup,
    const std::function<std::optional<bool>(int32_t)>& in_control,const std::function<bool(int32_t)>& commit_selection);
// 1486: SA CPhysical::ApplyTurnForce at the ped's offset from the vehicle; random_factor in -1.2..-0.8.
NativeResult sa_apply_car_tilt(int32_t vehicle_handle,int32_t ped_handle,float random_factor);
NativeResult sa_random_cop_in_area(const std::array<float,4>&,const std::array<int32_t,5>&,
    const std::array<int32_t,12>& donors,int32_t excluded,bool mission_cleanup,const std::function<bool(int32_t)>& commit_selection);
// waiting(handle) must answer from the runtime wait-state authority.
NativeResult sa_random_char_unchecked(const std::array<float,4>&,bool mission_cleanup,
    const std::function<std::optional<bool>(int32_t)>& waiting,const std::function<bool(int32_t)>& commit_selection);


// Available only in the 32-bit ASI. Called exclusively from SA's game thread.
NativeResult execute_sa(const NativeCall& call);
NativeResult sa_execute_vehicle_task_with_capture(const NativeCall& call);
NativeResult sa_execute_entity_command(const NativeCall& call);
NativeResult sa_execute_statistic_command(const NativeCall& call);
NativeResult sa_execute_frenzy_command(const NativeCall& call);
NativeResult sa_execute_environment_command(const NativeCall& call);
NativeResult sa_execute_garage_command(const NativeCall& call);
bool sa_garage_vehicle_slice(uint32_t slice,uint32_t slices,std::vector<int32_t>& handles);
std::optional<uintptr_t> sa_garage_ped_identity(int32_t handle);
NativeResult sa_execute_traffic_command(const NativeCall& call);
bool sa_executor_profile_matches();
// Refresh $PLAYER_CHAR/$PLAYER_ACTOR before native/CLEO script processing.
// Empty means no owned bootstrap/live player; no foreign globals are changed.
std::optional<int32_t> sa_sync_native_player_exports();
std::optional<SpatialListener> sa_audio_listener();
std::optional<int32_t> sa_native_weapon_slot(int32_t native_weapon);
std::optional<bool> sa_can_see_dead_char(int32_t actor,int32_t native_type);
bool sa_set_threat_range_multiplier(uint16_t multiplier);
bool sa_tick_threat_ranges();
bool sa_release_threat_ranges();
// Read the actual player restart state, not a remembered character arrest flag.
bool sa_player_is_restarting(bool& restarting);
std::optional<float> sa_frame_time_step();
std::optional<int32_t> sa_saved_player_weapon(int32_t player);
std::optional<int32_t> sa_camera_ped_zoom(std::optional<int32_t> setting);
bool sa_freeze_ped_zoom(bool freeze);
std::optional<std::string> sa_create_garage(const std::array<float,8>& bounds,int32_t type,int32_t original_id);
bool sa_rotate_garage_door(const std::string& name);
bool sa_leave_garage_camera(const std::string& name);
bool sa_set_garage_capacity(const std::string& name,uint8_t capacity);
bool sa_release_garage_capacity();
bool sa_clear_garage_message();
std::optional<int32_t> sa_collectables(bool total,std::optional<int32_t> setting);
std::optional<uint8_t> sa_model_class(int32_t model);
bool sa_switch_angled_roads(const std::array<float,7>&,bool cars,bool off);
std::optional<bool> sa_char_in_control(int32_t ped);
std::optional<bool> sa_char_entering_as_passenger(int32_t ped);
bool sa_set_char_crouch(int32_t ped,bool crouch,int32_t duration);
bool sa_set_char_can_fight(int32_t ped,bool enabled);
bool sa_set_stationary_shooter(int32_t ped,bool enabled);
bool sa_set_char_heed_threats(int32_t ped,bool enabled);
bool sa_set_gang_damage(int32_t ped,int32_t gang,bool enabled);
bool sa_release_gang_damage();
bool sa_set_char_crouch_when_threatened(int32_t ped);
bool sa_set_pednode_seek(int32_t ped,bool enabled);
void sa_erase_pednode_seek(int32_t ped);
bool sa_release_pednode_seek();
bool sa_set_char_personality(int32_t ped,const std::string& name);
bool sa_display_message(const MissionMessage&);
NativeResult sa_display_big_message(const BigMissionMessage&);
bool sa_show_campaign_error(const std::string& message);
std::optional<int32_t> sa_scripted_pad_state(int32_t selector);
std::optional<bool> sa_player_made_safe();
std::optional<int32_t> sa_random_char_in_area(const std::array<float,4>&,const std::array<int32_t,3>&,int32_t excluded,bool mission);
}
