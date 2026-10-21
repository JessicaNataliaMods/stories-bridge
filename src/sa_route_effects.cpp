#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include "lcs/native_memory.hpp"
#include "lcs/sa_task_memory.hpp"
#include "lcs/route_effects.hpp"
#include "sa_owned_vehicle_entry.hpp"
#include "lcs/sa_script.hpp"
#include "lcs/script_task_capture.hpp"

namespace lcs {

#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
void native_route_effect_set_idle(uintptr_t ped);
void native_route_effect_set_move_state(uintptr_t ped,int32_t state);
bool native_route_effect_find_z(float* xyz);
bool native_route_effect_sphere_visible(float x,float y,float z,float radius);
bool native_route_effect_line_of_sight(float from_x,float from_y,float from_z,
        float to_x,float to_y,float to_z,bool buildings,bool vehicles,bool peds,
        bool objects,bool dummies,bool see_through,bool camera_ignore);
uintptr_t native_route_effect_test_sphere(float x,float y,float z,float radius,
        uintptr_t ignore,bool buildings,bool vehicles,bool peds,bool objects,
        bool dummies,bool camera_ignore);
void native_route_effect_teleport(uintptr_t ped,float x,float y,float z,bool reset_rotation);
bool native_route_effect_get_car_door_position(uintptr_t vehicle,int32_t sa_node,float* xyz);
void native_route_effect_set_position(uintptr_t ped,float x,float y,float z);
uintptr_t native_route_effect_allocate_enter_passenger(uint32_t bytes);
void native_route_effect_release_enter_passenger_memory(uintptr_t memory);
void native_route_effect_construct_enter_passenger(uintptr_t task,uintptr_t vehicle,
        int32_t target_seat,bool carry_on_after_falling_off);
void native_route_effect_discard_enter_passenger(uintptr_t task);
void native_route_effect_assign_enter_passenger(SaScript* script,int32_t actor,
        uintptr_t task,int32_t opcode);
void native_route_effect_clear_look_flag(uintptr_t ped);
void native_route_effect_clear_aim_flag(uintptr_t ped);
void native_route_effect_set_ped_state(uintptr_t ped,int32_t state);
bool native_route_effect_abort_use_gun(uintptr_t task,uintptr_t ped,int32_t priority);
bool native_route_effect_set_fire_animation_fade(uintptr_t association,float delta,bool auto_remove);
#endif

namespace {
constexpr int32_t native_pedmove_still=1;

struct NativeVector { float x,y,z; };

// Short retail 1.0 US entry profiles for the exact methods used here. These
// are checked independently of the executor/dispatcher profile so an inline
// foreign hook cannot receive a partially completed source effect.
constexpr uint8_t set_idle_profile[]={
    0x8B,0x81,0x30,0x05,0x00,0x00,0xBA,0x01,0x00,0x00,0x00,0x3B,0xC2,0x74,0x1B,0x83};
constexpr uint8_t set_move_state_profile[]={
    0x8B,0x44,0x24,0x04,0x89,0x81,0x34,0x05,0x00,0x00,0xC2,0x04,0x00,0x90,0x90,0x90};
constexpr uint8_t find_z_profile[]={
    0x83,0xEC,0x4C,0x56,0x8B,0x74,0x24,0x54,0xD9,0x46,0x04,0x8B,0x06,0x6A,0x00,0x6A};
constexpr uint8_t sphere_visible_profile[]={
    0x53,0x8B,0x5C,0x24,0x08,0x56,0x57,0x8B,0x7C,0x24,0x14,0x8B,0xF1,0x8D,0x86,0x4C};
constexpr uint8_t line_of_sight_profile[]={
    0x83,0xEC,0x5C,0x66,0x81,0x3D,0x78,0xCD,0xB7,0x00,0xFF,0xFF,0x73,0x09,0x66,0xFF};
constexpr uint8_t test_sphere_profile[]={
    0x83,0xEC,0x1C,0xD9,0x44,0x24,0x20,0x53,0xD8,0x64,0x24,0x30,0x55,0x56,0x57,0xD8};
constexpr uint8_t teleport_profile[]={
    0x56,0x8B,0xF1,0x8B,0x86,0x98,0x05,0x00,0x00,0x85,0xC0,0x57,0x74,0x1C,0x83,0xF8};
constexpr uint8_t get_car_door_position_profile[]={
    0x83,0xEC,0x30,0x53,0x55,0x56,0x8B,0x74,0x24,0x44,0x0F,0xBF,0x46,0x22,0x57,0x8B};
constexpr uint8_t set_position_profile[]={
    0x8B,0x44,0x24,0x04,0xD9,0x40,0x08,0xD9,0x40,0x04,0xD9,0x00,0x8B,0x41,0x14,0x85};
constexpr uint8_t passenger_task_allocator_profile[]={
    0x8B,0x0D,0xA8,0x44,0xB7,0x00,0xE9,0x55,0xFF,0xFF,0xFF};
constexpr uint8_t passenger_task_release_profile[]={0x8B,0x0D,0xA8,0x44,0xB7,0x00};
constexpr uint8_t passenger_task_constructor_profile[]={
    0x8B,0x44,0x24,0x0C,0x56,0x50,0x6A,0x00,0x6A,0x00,0x8B,0xF1,0x8B,0x4C,0x24,0x14};
constexpr uint8_t passenger_task_delete_profile[]={
    0x56,0x8B,0xF1,0xE8,0xF8,0xC6,0xFF,0xFF,0xF6,0x44,0x24,0x08,
    0x01,0x74,0x09,0x56,0xE8,0x1B,0x69,0xFD,0xFF,0x83,0xC4,0x04};
constexpr uint8_t passenger_task_assign_profile[]={
    0x64,0xA1,0x00,0x00,0x00,0x00,0x6A,0xFF,0x68,0x18,0x7D,0x83,0x00};
constexpr uint8_t clear_aim_flag_profile[]={
    0x8B,0x81,0x6C,0x04,0x00,0x00,0x33,0xD2,0xA8,0x10,0x74,0x19,
    0x83,0xE0,0xEF,0x83,0xC8,0x20,0x89,0x81,0x6C,0x04,0x00,0x00};
constexpr uint8_t clear_look_flag_profile[]={
    0x90,0x90,0xE9,0x7E,0x7D,0xE2,0xFF,0x0F,0x84,0x95,0x00,0x00,
    0x00,0x8D,0x49,0x00,0x8B,0x91,0x70,0x04,0x00,0x00};
constexpr uint8_t clear_look_flag_target_profile[]={
    0xF6,0x81,0x6C,0x04,0x00,0x00,0x04,0xE9,0x76,0x82,0x1D,0x00,
    0xC7,0x40,0x10,0x00,0x00,0x00,0x00,0xE9,0x8C,0x08,0x0A,0x00};
constexpr uint8_t use_gun_abort_profile[]={
    0x53,0x8B,0x5C,0x24,0x0C,0x83,0xFB,0x01,0x56,0x8B,0xF1,0x74,
    0x10,0x83,0xFB,0x02,0x74,0x0B,0xC6,0x46,0x0F,0x06,0x5E,0x32};

constexpr int32_t native_pedstate_attack=16;
constexpr int32_t native_pedstate_aim_gun=22;
constexpr uint8_t set_ped_state_entry[]={
    0x53,0x8B,0x5C,0x24,0x08,0x83,0xFB,0x37,0x56,0x8B,0xF1,0x74,0x05,0x83,0xFB,0x36,0x75,0x5B};
constexpr uint8_t set_ped_state_store[]={0x89,0x9E,0x30,0x05,0x00,0x00,0x5E,0x5B,0xC2,0x04,0x00};
constexpr int8_t native_gun_command_aim=1;
constexpr int8_t native_gun_command_fire=2;
constexpr int8_t native_gun_command_fire_burst=3;
constexpr int32_t native_abort_priority_urgent=1;
constexpr uint16_t native_anim_blend_auto_remove=0x0004;

bool profile(uintptr_t address,const uint8_t* bytes,size_t length);

bool set_ped_state_profile_matches() {
    // For IDLE=1 and DRIVING=50, both branches skip the death-only services
    // and reach the reviewed state store directly (SDK CPed::SetPedState).
    return profile(0x5E4500,set_ped_state_entry,sizeof(set_ped_state_entry)) &&
        profile(0x5E456D,set_ped_state_store,sizeof(set_ped_state_store));
}

bool invoke_set_ped_state(uintptr_t ped,int32_t state) {
    __try {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        native_route_effect_set_ped_state(ped,state);
#else
        reinterpret_cast<void(__thiscall*)(uintptr_t,int32_t)>(native_entity_address(0x5E4500))(ped,state);
#endif
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool clear_point_gun_profiles_match() {
    return profile(0x5DEF20,clear_aim_flag_profile,sizeof(clear_aim_flag_profile)) &&
        profile(0x5E1950,clear_look_flag_profile,sizeof(clear_look_flag_profile)) &&
        profile(0x4096D5,clear_look_flag_target_profile,sizeof(clear_look_flag_target_profile)) &&
        profile(0x624E30,use_gun_abort_profile,sizeof(use_gun_abort_profile));
}

bool use_gun_vtable_matches(uintptr_t task) {
    __try {
        const auto table=native_entity_address(0x86D724);
        return *reinterpret_cast<const uintptr_t*>(task)==table &&
            *reinterpret_cast<const uintptr_t*>(table+6*sizeof(uintptr_t))==0x624E30;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool invoke_clear_look_flag(uintptr_t ped,bool* entered) {
    __try {
        *entered=true;
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        native_route_effect_clear_look_flag(ped);
#else
        reinterpret_cast<void(__thiscall*)(uintptr_t)>(native_entity_address(0x5E1950))(ped);
#endif
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool invoke_clear_aim_flag(uintptr_t ped,bool* entered) {
    __try {
        *entered=true;
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        native_route_effect_clear_aim_flag(ped);
#else
        reinterpret_cast<void(__thiscall*)(uintptr_t)>(native_entity_address(0x5DEF20))(ped);
#endif
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool invoke_abort_use_gun(uintptr_t task,uintptr_t ped,bool* entered,bool* aborted) {
    __try {
        *entered=true;
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        *aborted=native_route_effect_abort_use_gun(task,ped,native_abort_priority_urgent);
#else
        *aborted=reinterpret_cast<bool(__thiscall*)(uintptr_t,uintptr_t,int32_t,uintptr_t)>(
            native_entity_address(0x624E30))(task,ped,native_abort_priority_urgent,0);
#endif
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool invoke_fire_animation_fade(uintptr_t association,bool* entered) {
    __try {
        *entered=true;
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        return native_route_effect_set_fire_animation_fade(association,-4.0f,true);
#else
        auto* const blend_delta=reinterpret_cast<float*>(association+0x18);
        auto* const flags=reinterpret_cast<uint16_t*>(association+0x2A);
        *blend_delta=-4.0f;
        *flags=static_cast<uint16_t>(*flags|native_anim_blend_auto_remove);
        return true;
#endif
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// CTaskSimpleUseGun layout from the SA ABI: eGunCommand Next/Last are bytes at
// +0x0F/+0x10, CAnimBlendAssociation* m_Anim is +0x2C. StartAnim selects the
// weapon group's FIRE/CROUCHFIRE association for AIM/FIRE/FIREBURST commands.
bool route_use_gun_slot(uintptr_t ped,uintptr_t& task,uintptr_t& fire_animation) {
    task=0;
    fire_animation=0;
    __try {
        const auto intelligence=*reinterpret_cast<const uintptr_t*>(ped+0x47C);
        if(!intelligence || *reinterpret_cast<const uintptr_t*>(intelligence)!=ped)return false;
        const auto manager=intelligence+4;
        // TaskManager secondary attack slot zero; its owner is at manager+0x2C.
        if(*reinterpret_cast<const uintptr_t*>(manager+0x2C)!=ped)return false;
        task=*reinterpret_cast<const uintptr_t*>(manager+0x14);
        if(!task)return true;
        if(!use_gun_vtable_matches(task))return false;
        const auto next=*reinterpret_cast<const int8_t*>(task+0x0F);
        const auto last=*reinterpret_cast<const int8_t*>(task+0x10);
        fire_animation=*reinterpret_cast<const uintptr_t*>(task+0x2C);
        const auto is_fire_command=[](int8_t command) {
            return command==native_gun_command_aim || command==native_gun_command_fire ||
                command==native_gun_command_fire_burst;
        };
        if(fire_animation) {
            // The task producer creates only FIRE/CROUCHFIRE associations for
            // these current commands; RELOAD and PISTOLWHIP are left untouched.
            const auto delta=*reinterpret_cast<const float*>(fire_animation+0x18);
            const auto blend=*reinterpret_cast<const float*>(fire_animation+0x14);
            (void)*reinterpret_cast<const volatile uint16_t*>(fire_animation+0x2A);
            return is_fire_command(last) && std::isfinite(delta) && std::isfinite(blend);
        }
        return is_fire_command(next) || is_fire_command(last);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool route_use_gun_slot_still(uintptr_t ped,uintptr_t expected_task) {
    __try {
        const auto intelligence=*reinterpret_cast<const uintptr_t*>(ped+0x47C);
        if(!intelligence || *reinterpret_cast<const uintptr_t*>(intelligence)!=ped)return false;
        const auto manager=intelligence+4;
        return *reinterpret_cast<const uintptr_t*>(manager+0x2C)==ped &&
            *reinterpret_cast<const uintptr_t*>(manager+0x14)==expected_task &&
            (!expected_task || use_gun_vtable_matches(expected_task));
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool route_use_gun_animation_released(uintptr_t task) {
    __try {return *reinterpret_cast<const uintptr_t*>(task+0x2C)==0;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

bool profile(uintptr_t address,const uint8_t* bytes,size_t length) {
    __try {
        return std::memcmp(reinterpret_cast<const void*>(native_entity_address(address)),bytes,length)==0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool authorized_now(RouteEffectAuthorization authorized,void* context) {
    if(!authorized)return false;
    __try { return authorized(context); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool refresh_route_actor(const RouteActorVehicleSnapshot& expected,
        RouteActorVehicleSnapshot& live,bool require_controllable) {
    if(expected.actor.handle<0 || !expected.actor.native_ped || !expected.alive ||
       expected.attached || expected.in_vehicle || (require_controllable && !expected.controllable))return false;
    const auto current=sa_route_actor_vehicle_snapshot(expected.actor);
    if(!current || !(current->actor==expected.actor) || !current->alive || current->attached || current->in_vehicle ||
       (require_controllable && !current->controllable))return false;
    live=*current;
    return true;
}

// ClearPointGunAt belongs to the objective setter, which also accepts attached
// and seated actors. It does not inherit the movement-effect admission gate.
bool refresh_point_gun_actor(const RouteActorVehicleSnapshot& expected,RouteActorVehicleSnapshot& live) {
    if(expected.actor.handle<0 || !expected.actor.native_ped || !expected.alive)return false;
    const auto current=sa_route_actor_vehicle_snapshot(expected.actor);
    if(!current || !(current->actor==expected.actor) || !current->alive)return false;
    live=*current;return true;
}

bool refresh_route_vehicle(const RouteVehicleSnapshot& expected,RouteVehicleSnapshot& live) {
    if(expected.handle<0 || !expected.entity || !expected.alive)return false;
    const auto current=sa_route_vehicle_snapshot(expected.handle);
    if(!current || current->handle!=expected.handle || current->entity!=expected.entity ||
       current->generation!=expected.generation || !current->alive)return false;
    live=*current;
    return true;
}

bool idle_profile_matches() {
    return profile(0x5E7980,set_idle_profile,sizeof(set_idle_profile));
}
bool move_profile_matches() {
    return profile(0x5DEC00,set_move_state_profile,sizeof(set_move_state_profile));
}
bool warp_query_profiles_match() {
    return profile(0x616920,find_z_profile,sizeof(find_z_profile)) &&
        profile(0x420D40,sphere_visible_profile,sizeof(sphere_visible_profile)) &&
        profile(0x56A490,line_of_sight_profile,sizeof(line_of_sight_profile)) &&
        profile(0x569E20,test_sphere_profile,sizeof(test_sphere_profile));
}
bool car_entry_position_profiles_match() {
    return profile(0x64E740,get_car_door_position_profile,sizeof(get_car_door_position_profile)) &&
        profile(0x4241C0,set_position_profile,sizeof(set_position_profile));
}
bool passenger_entry_profiles_match() {
    return sa_task_allocator_profile_matches() &&
        sa_task_release_profile_matches() &&
        profile(0x640340,passenger_task_constructor_profile,sizeof(passenger_task_constructor_profile)) &&
        profile(0x643C80,passenger_task_delete_profile,sizeof(passenger_task_delete_profile)) &&
        profile(0x465C20,passenger_task_assign_profile,sizeof(passenger_task_assign_profile));
}

std::optional<int32_t> native_car_door_node(RouteEntryDoor door,bool is_bike) {
    // LCS Vehicle.h source nodes RF/RR/LR are 11/12/16. San Andreas
    // CAutomobile.h eCarNodes are RF/RR/LR 8/9/11; never pass source ordinals
    // directly to CCarEnterExit.
    switch(door) {
    case RouteEntryDoor::front_right:return 8;
    // The source bike fallback chooses RR while SA's passenger-seat zero
    // targets its rear-left node (with both rear getting-in flags set).
    case RouteEntryDoor::rear_right:return is_bike?11:9;
    case RouteEntryDoor::rear_left:return 11;
    }
    return std::nullopt;
}

bool invoke_set_idle(uintptr_t ped,bool* entered) {
    __try {
        *entered=true;
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        native_route_effect_set_idle(ped);
#else
        reinterpret_cast<void(__thiscall*)(uintptr_t)>(native_entity_address(0x5E7980))(ped);
#endif
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool invoke_move_state(uintptr_t ped,int32_t state,bool* entered) {
    __try {
        *entered=true;
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        native_route_effect_set_move_state(ped,state);
#else
        reinterpret_cast<void(__thiscall*)(uintptr_t,int32_t)>(native_entity_address(0x5DEC00))(ped,state);
#endif
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool invoke_find_z(NativeVector& position) {
    __try {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        (void)native_route_effect_find_z(&position.x);
#else
        (void)reinterpret_cast<bool(__cdecl*)(NativeVector*)>(native_entity_address(0x616920))(&position);
#endif
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool invoke_sphere_visible(const NativeVector& position,bool& visible) {
    __try {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        visible=native_route_effect_sphere_visible(position.x,position.y,position.z,0.6f);
#else
        visible=reinterpret_cast<bool(__thiscall*)(uintptr_t,const NativeVector&,float)>(
            native_entity_address(0x420D40))(native_entity_address(0xB6F028),position,0.6f);
#endif
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool invoke_line_of_sight(const NativeVector& from,const NativeVector& to,bool& clear) {
    __try {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        clear=native_route_effect_line_of_sight(from.x,from.y,from.z,to.x,to.y,to.z,
            true,true,false,true,false,false,false);
#else
        clear=reinterpret_cast<bool(__cdecl*)(const NativeVector&,const NativeVector&,
            bool,bool,bool,bool,bool,bool,bool)>(native_entity_address(0x56A490))(
                from,to,true,true,false,true,false,false,false);
#endif
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool invoke_test_sphere(const NativeVector& position,uintptr_t ignore,bool& clear) {
    __try {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        clear=native_route_effect_test_sphere(position.x,position.y,position.z,0.6f,ignore,
            true,true,false,true,false,false)==0;
#else
        clear=reinterpret_cast<uintptr_t(__cdecl*)(NativeVector,float,uintptr_t,
            bool,bool,bool,bool,bool,bool)>(native_entity_address(0x569E20))(
                position,0.6f,ignore,true,true,false,true,false,false)==0;
#endif
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool teleport_slot_matches(uintptr_t ped) {
    if(!profile(0x5E4110,teleport_profile,sizeof(teleport_profile)))return false;
    __try {
        const auto vtable=*reinterpret_cast<const uintptr_t*>(ped);
        // The isolated fixture stores translated vtable pointers directly;
        // retail objects store preferred-base VAs that need translation there.
        const auto native_vtable=vtable<0x02000000u?native_entity_address(vtable):vtable;
        if(!vtable || native_vtable>UINTPTR_MAX-15u*sizeof(uintptr_t))return false;
        // CPed's Teleport is its virtual CEntity slot 14. Dispatch only when
        // that live slot still names the profiled retail implementation;
        // player overrides and third-party vtable hooks remain untouched.
        return *reinterpret_cast<const uintptr_t*>(native_vtable+14u*sizeof(uintptr_t))==native_entity_address(0x5E4110);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool invoke_teleport(uintptr_t ped,const NativeVector& position,bool* entered) {
    __try {
        const auto vtable=*reinterpret_cast<const uintptr_t*>(ped);
        const auto native_vtable=vtable<0x02000000u?native_entity_address(vtable):vtable;
        if(!vtable || native_vtable>UINTPTR_MAX-15u*sizeof(uintptr_t))return false;
        const auto target=*reinterpret_cast<const uintptr_t*>(native_vtable+14u*sizeof(uintptr_t));
        if(target!=native_entity_address(0x5E4110))return false;
        *entered=true;
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        native_route_effect_teleport(ped,position.x,position.y,position.z,false);
#else
        reinterpret_cast<void(__thiscall*)(uintptr_t,NativeVector,bool)>(target)(ped,position,false);
#endif
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool invoke_car_door_position(uintptr_t vehicle,int32_t sa_node,NativeVector& position) {
    __try {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        return native_route_effect_get_car_door_position(vehicle,sa_node,&position.x);
#else
        auto* const returned=reinterpret_cast<NativeVector*(__cdecl*)(NativeVector*,uintptr_t,int32_t)>(
            native_entity_address(0x64E740))(&position,vehicle,sa_node);
        return returned==&position;
#endif
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool invoke_set_position(uintptr_t ped,const NativeVector& position,bool* entered) {
    __try {
        *entered=true;
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        native_route_effect_set_position(ped,position.x,position.y,position.z);
#else
        reinterpret_cast<void(__thiscall*)(uintptr_t,const NativeVector&)>(
            native_entity_address(0x4241C0))(ped,position);
#endif
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

uintptr_t invoke_allocate_passenger_task() {
    __try {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        return native_route_effect_allocate_enter_passenger(0x50);
#else
        return reinterpret_cast<uintptr_t(__cdecl*)(uint32_t)>(
            native_entity_address(0x61A5A0))(0x50);
#endif
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

bool invoke_release_passenger_memory(uintptr_t memory) {
    __try {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        native_route_effect_release_enter_passenger_memory(memory);
#else
        reinterpret_cast<void(__cdecl*)(uintptr_t)>(native_entity_address(0x61A5B0))(memory);
#endif
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool invoke_construct_passenger_task(uintptr_t task,uintptr_t vehicle,int32_t node,
        bool* entered) {
    __try {
        *entered=true;
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        native_route_effect_construct_enter_passenger(task,vehicle,node,true);
#else
        reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t,uintptr_t,int32_t,bool)>(
            native_entity_address(0x640340))(task,vehicle,node,true);
#endif
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool discard_unassigned_passenger_task(uintptr_t task) {
    __try {
        if(*reinterpret_cast<const uintptr_t*>(task)!=sa_owned_entry_passenger_vtable())return false;
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        native_route_effect_discard_enter_passenger(task);
#else
        reinterpret_cast<void(__thiscall*)(uintptr_t,uint32_t)>(
            native_entity_address(0x643C80))(task,1);
#endif
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool invoke_assign_passenger_task(SaScript* script,int32_t actor,uintptr_t task,
        bool* entered) {
    __try {
        *entered=true;
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        native_route_effect_assign_enter_passenger(script,actor,task,0x05CA);
#else
        reinterpret_cast<void(__thiscall*)(SaScript*,int32_t,uintptr_t,int32_t)>(
            native_entity_address(0x465C20))(script,actor,task,0x05CA);
#endif
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool scripted_passenger_assignment_available(uintptr_t ped) {
    __try {
        return ped && *reinterpret_cast<const uintptr_t*>(ped+0x47C) &&
            !(*reinterpret_cast<const uint32_t*>(ped+0x474)&0x800000u);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool passenger_task_shape_matches(uintptr_t task,uintptr_t vehicle,int32_t node) {
    __try {
        return *reinterpret_cast<const uintptr_t*>(task)==sa_owned_entry_passenger_vtable() &&
            !*reinterpret_cast<const uintptr_t*>(task+0x04) &&
            !*reinterpret_cast<const uintptr_t*>(task+0x08) &&
            *reinterpret_cast<const uintptr_t*>(task+0x0C)==vehicle &&
            *reinterpret_cast<const int32_t*>(task+0x1C)==node;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

NativeResult move_still_after_gate(const RouteActorVehicleSnapshot& expected,
        RouteEffectAuthorization authorized,void* authorization_context) {
    NativeResult result;
    RouteActorVehicleSnapshot live;
    if(!sa_executor_profile_matches() || !move_profile_matches() || !authorized ||
       !refresh_route_actor(expected,live,true) || !authorized_now(authorized,authorization_context)) {
        result.diagnostic="SetMoveState(STILL) lacks a live controllable actor and authorized route epoch";
        return result;
    }
    bool entered=false;
    if(!invoke_move_state(expected.actor.native_ped,native_pedmove_still,&entered)) {
        result.native_entered=entered;
        result.diagnostic="SetMoveState(STILL) entered SA and faulted";
        return result;
    }
    result.native_entered=true;
    result.executed=true;
    return result;
}
}

NativeResult sa_route_promote_still_to_run(const RouteActorVehicleSnapshot& expected,
        RouteEffectAuthorization authorized,void* authorization_context) {
    NativeResult result;
    if(!sa_executor_profile_matches() || !move_profile_matches() || !authorized ||
       !authorized_now(authorized,authorization_context)) {
        result.diagnostic="Source movement promotion lacks an authorized actor/profile";return result;
    }
    const auto live=sa_route_actor_vehicle_snapshot(expected.actor);
    if(!live || !(live->actor==expected.actor) || !live->alive ||
       !authorized_now(authorized,authorization_context)) {
        result.diagnostic="Source actor changed before movement promotion";return result;
    }
    if(live->native_move_state!=native_pedmove_still) {result.executed=true;return result;}
    bool entered=false;
    if(!invoke_move_state(expected.actor.native_ped,6,&entered)) {
        result.native_entered=entered;result.diagnostic="SetMoveState(RUN) entered SA and faulted";return result;
    }
    result.native_entered=result.executed=true;return result;
}

NativeResult sa_route_move_still(const RouteActorVehicleSnapshot& expected,
        RouteEffectAuthorization authorized,void* authorization_context) {
    return move_still_after_gate(expected,authorized,authorization_context);
}

NativeResult sa_route_idle_ped_still(const RouteActorVehicleSnapshot& expected,
        RouteEffectAuthorization authorized,void* authorization_context) {
    NativeResult result;
    RouteActorVehicleSnapshot live;
    if(!sa_executor_profile_matches() || !idle_profile_matches() || !move_profile_matches() ||
       !authorized || !refresh_route_actor(expected,live,true) ||
       !authorized_now(authorized,authorization_context)) {
        result.diagnostic="Mission-ped idle effect lacks a live controllable actor and authorized route epoch";
        return result;
    }

    bool entered=false;
    if(!invoke_set_idle(expected.actor.native_ped,&entered)) {
        result.native_entered=entered;
        result.diagnostic="SetIdle entered SA and faulted";
        return result;
    }
    result.native_entered=true;

    // SetIdle may invoke task callbacks. Refresh identity and source ownership
    // before the second engine call; an entered first half is never replayed.
    if(!sa_executor_profile_matches() || !move_profile_matches() ||
       !refresh_route_actor(expected,live,true) || !authorized_now(authorized,authorization_context)) {
        result.diagnostic="SetIdle completed but the actor or route epoch changed before SetMoveState(STILL)";
        return result;
    }
    entered=false;
    if(!invoke_move_state(expected.actor.native_ped,native_pedmove_still,&entered)) {
        result.diagnostic="SetIdle completed but SetMoveState(STILL) faulted";
        return result;
    }
    result.executed=true;
    return result;
}

NativeResult sa_route_position_ped_for_car_entry(const RouteActorVehicleSnapshot& actor,
        const RouteVehicleSnapshot& vehicle,RouteEntryDoor door,
        RouteEffectAuthorization authorized,void* authorization_context) {
    NativeResult result;
    const auto sa_node=native_car_door_node(door,vehicle.is_bike);
    RouteActorVehicleSnapshot live_actor;
    RouteVehicleSnapshot live_vehicle;
    if(!sa_node || !sa_executor_profile_matches() || !car_entry_position_profiles_match() ||
       !authorized || !refresh_route_actor(actor,live_actor,true) ||
       !refresh_route_vehicle(vehicle,live_vehicle) || live_vehicle.is_bike!=vehicle.is_bike ||
       !authorized_now(authorized,authorization_context)) {
        result.diagnostic="Car-entry positioning lacks a valid source door, live actor/vehicle identities, native profiles, or route authorization";
        return result;
    }

    NativeVector door_position{};
    if(!invoke_car_door_position(live_vehicle.entity,*sa_node,door_position)) {
        result.diagnostic="CCarEnterExit::GetPositionToOpenCarDoor failed before the source entry position mutation";
        return result;
    }
    if(!std::isfinite(door_position.x) || !std::isfinite(door_position.y) ||
       !std::isfinite(door_position.z)) {
        result.diagnostic="CCarEnterExit returned a non-finite source entry position";
        return result;
    }

    // The position query can call into model/vehicle code. Refresh weak
    // identities, exact native profiles, and source ownership at the mutation
    // boundary so a retired epoch or replaced target cannot move the ped.
    if(!sa_executor_profile_matches() || !car_entry_position_profiles_match() ||
       !refresh_route_actor(actor,live_actor,true) ||
       !refresh_route_vehicle(vehicle,live_vehicle) || live_vehicle.is_bike!=vehicle.is_bike ||
       !authorized_now(authorized,authorization_context)) {
        result.diagnostic="Actor, target vehicle, native profile, or route epoch changed before CPed position mutation";
        return result;
    }

    bool entered=false;
    if(!invoke_set_position(live_actor.actor.native_ped,door_position,&entered)) {
        result.native_entered=entered;
        result.diagnostic="CPlaceable::SetPosn entered SA and faulted during source car-entry positioning";
        return result;
    }
    result.native_entered=true;
    result.executed=true;
    return result;
}

NativeResult sa_route_enter_passenger_door(const RouteActorVehicleSnapshot& actor,
        const RouteVehicleSnapshot& vehicle,RouteEntryDoor door,bool mission_cleanup,
        RouteEffectAuthorization authorized,void* authorization_context) {
    NativeResult result;
    const auto node=native_car_door_node(door,vehicle.is_bike);
    RouteActorVehicleSnapshot live_actor;
    RouteVehicleSnapshot live_vehicle;
    if(!node || !sa_executor_profile_matches() || !passenger_entry_profiles_match() ||
       !authorized || !refresh_route_actor(actor,live_actor,true) ||
       !refresh_route_vehicle(vehicle,live_vehicle) || live_vehicle.is_bike!=vehicle.is_bike ||
       !scripted_passenger_assignment_available(live_actor.actor.native_ped) ||
       !authorized_now(authorized,authorization_context)) {
        result.diagnostic="Explicit passenger-door task lacks a live actor/vehicle identity, script-capable ped, native profile, or route authorization";
        return result;
    }

    const auto task=invoke_allocate_passenger_task();
    if(!task) {
        result.diagnostic="SA task allocator refused the exact passenger task allocation";
        return result;
    }

    // Allocation may run observers. Revalidate before the constructor adds a
    // weak vehicle reference to this still-private task object.
    if(!sa_executor_profile_matches() || !passenger_entry_profiles_match() ||
       !refresh_route_actor(actor,live_actor,true) ||
       !refresh_route_vehicle(vehicle,live_vehicle) || live_vehicle.is_bike!=vehicle.is_bike ||
       !scripted_passenger_assignment_available(live_actor.actor.native_ped) ||
       !authorized_now(authorized,authorization_context)) {
        if(!passenger_entry_profiles_match() || !invoke_release_passenger_memory(task))
            result.native_entered=true;
        result.diagnostic="Actor, vehicle, native profile, or route epoch changed before passenger-task construction";
        return result;
    }

    bool construct_entered=false;
    if(!invoke_construct_passenger_task(task,live_vehicle.entity,*node,&construct_entered)) {
        // A fault inside the constructor can leave a partially initialized
        // native object; do not guess its destructor shape or replay it.
        result.native_entered=construct_entered;
        result.diagnostic="CTaskComplexEnterCarAsPassenger constructor entered SA and faulted";
        return result;
    }

    uint64_t owned_entry_epoch=0;
    const auto discard_private_task=[&]() {
        const bool discarded=passenger_entry_profiles_match() && (owned_entry_epoch?
            sa_discard_private_vehicle_entry(task,actor.actor,0x05CA):discard_unassigned_passenger_task(task));
        // A refused discard leaves the task to SA/the pool; its policy lineage must still be retired.
        if(owned_entry_epoch)sa_release_owned_vehicle_entry(actor.actor,owned_entry_epoch);
        if(!discarded) {
            result.native_entered=true;
            return false;
        }
        return true;
    };

    if(!passenger_task_shape_matches(task,live_vehicle.entity,*node)) {
        result.native_entered=true;
        result.diagnostic="Passenger-task constructor returned an unexpected class, vehicle reference, or target node";
        return result;
    }

    owned_entry_epoch=sa_track_private_vehicle_entry(task,actor.actor,0x05CA);
    if(!owned_entry_epoch) {
        discard_private_task();
        result.diagnostic="Private passenger entry could not establish its native task lifetime";
        return result;
    }

    if(!sa_executor_profile_matches() || !passenger_entry_profiles_match() ||
       !refresh_route_actor(actor,live_actor,true) ||
       !refresh_route_vehicle(vehicle,live_vehicle) || live_vehicle.is_bike!=vehicle.is_bike ||
       !scripted_passenger_assignment_available(live_actor.actor.native_ped) ||
       !authorized_now(authorized,authorization_context)) {
        discard_private_task();
        result.diagnostic="Actor, vehicle, native profile, or route epoch changed before passenger-task assignment";
        return result;
    }

    const auto before=sa_script_task_snapshot();
    if(!before) {
        result.task_capture=NativeResult::TaskCapture::unavailable_before;
        discard_private_task();
        result.diagnostic="Native passenger task was constructed but the script-task baseline is unavailable";
        return result;
    }
    // The baseline read itself is a boundary: do not assign a private task to
    // an actor or route epoch replaced while the script bank was sampled.
    if(!sa_executor_profile_matches() || !passenger_entry_profiles_match() ||
       !refresh_route_actor(actor,live_actor,true) ||
       !refresh_route_vehicle(vehicle,live_vehicle) || live_vehicle.is_bike!=vehicle.is_bike ||
       !scripted_passenger_assignment_available(live_actor.actor.native_ped) ||
       !authorized_now(authorized,authorization_context)) {
        discard_private_task();
        result.diagnostic="Actor, vehicle, native profile, or route epoch changed after the passenger-task baseline";
        return result;
    }

    SaScript script(nullptr,mission_cleanup); // private C6 cleanup; DC remains false
    bool assign_entered=false;
    const bool assigned=invoke_assign_passenger_task(&script,actor.actor.handle,task,&assign_entered);
    result.native_entered=assign_entered;

    // Assignment can have transferred or deleted the task even if it faulted;
    // only the before/after bank delta can establish a reusable receipt.
    const auto after=sa_script_task_snapshot();
    if(!after) {
        result.task_capture=NativeResult::TaskCapture::unavailable_after;
        sa_release_owned_vehicle_entry(actor.actor,owned_entry_epoch); // the caller cannot adopt a missing receipt
        result.diagnostic=assigned?
            "Passenger task entered SA but the accepted-task snapshot is unavailable":
            "Passenger-task assignment entered SA and faulted; the accepted-task snapshot is unavailable";
        return result;
    }
    bool ambiguous_entry=false;
    for(size_t slot=0;slot<after->size();++slot) {
        CapturedScriptTask candidate{slot,(*after)[slot]};
        if(sa_owned_vehicle_entry_epoch(candidate,actor.actor)!=owned_entry_epoch)continue;
        if(result.accepted_script_task){ambiguous_entry=true;break;}
        result.accepted_script_task=candidate;
    }
    if(ambiguous_entry)result.accepted_script_task.reset();
    result.task_capture=result.accepted_script_task?NativeResult::TaskCapture::captured:
        NativeResult::TaskCapture::no_unique_record;
    if(!assigned) {
        sa_release_owned_vehicle_entry(actor.actor,owned_entry_epoch); // failed command: no caller adopts it
        result.diagnostic=result.accepted_script_task?
            "Passenger task was recorded before the scripted assignment faulted":
            "Passenger-task assignment entered SA and faulted without a unique accepted-task record";
        return result;
    }
    if(!result.accepted_script_task) {
        sa_release_owned_vehicle_entry(actor.actor,owned_entry_epoch);
        result.diagnostic="Passenger task entered SA without a unique accepted 05CA script-task record";
        return result;
    }

    if(!sa_executor_profile_matches() || !passenger_entry_profiles_match() ||
       !refresh_route_actor(actor,live_actor,true) ||
       !refresh_route_vehicle(vehicle,live_vehicle) || live_vehicle.is_bike!=vehicle.is_bike ||
       !authorized_now(authorized,authorization_context)) {
        sa_release_owned_vehicle_entry(actor.actor,owned_entry_epoch); // executed stays false: not adopted
        result.diagnostic="Passenger task was accepted, but actor, vehicle, profile, or route epoch changed before commit";
        return result;
    }
    result.executed=true;
    return result;
}

NativeResult sa_route_clear_point_gun_at(const RouteActorVehicleSnapshot& expected,
        RouteEffectAuthorization authorized,void* authorization_context) {
    NativeResult result;
    RouteActorVehicleSnapshot live;
    if(!sa_executor_profile_matches() || !clear_point_gun_profiles_match() || !authorized ||
       !refresh_point_gun_actor(expected,live) ||
       !authorized_now(authorized,authorization_context)) {
        result.diagnostic="ClearPointGunAt lacks a live actor, exact SA profiles, or current route authorization";
        return result;
    }

    const bool restore_state=live.native_state==native_pedstate_attack || live.native_state==native_pedstate_aim_gun;
    if(restore_state && live.in_vehicle && !set_ped_state_profile_matches()) {
        result.diagnostic="Occupied AIM_GUN/ATTACK restoration requires the native state-setter profile";
        return result;
    }

    uintptr_t task=0,fire_animation=0;
    if(!route_use_gun_slot(live.actor.native_ped,task,fire_animation)) {
        result.diagnostic="Secondary attack slot is foreign, modified, or holds a non-fire UseGun animation; it was preserved";
        return result;
    }

    bool entered=false;
    if(!invoke_clear_look_flag(live.actor.native_ped,&entered)) {
        result.native_entered=entered;
        result.diagnostic="CPedIntelligence::ClearLookFlag entered SA and faulted";
        return result;
    }
    result.native_entered=true;

    if(!clear_point_gun_profiles_match() || !refresh_point_gun_actor(expected,live) ||
       !authorized_now(authorized,authorization_context)) {
        result.diagnostic="Actor, native profile, or route authorization changed after ClearLookFlag";
        return result;
    }
    entered=false;
    if(!invoke_clear_aim_flag(live.actor.native_ped,&entered)) {
        result.native_entered=result.native_entered||entered;
        result.diagnostic="CPedIntelligence::ClearAimFlag entered SA and faulted";
        return result;
    }
    result.native_entered=true;

    if(restore_state && live.in_vehicle) {
        // After SetPedState(IDLE), source RestorePreviousState always selects
        // DRIVING for an occupied actor, independently of last-state/move or
        // creation provenance. On foot, preserve SA's live CPed state and let
        // the accepted native route task consume the cleared look/aim effects.
        for(int32_t state:{1,50}) {
            if(!refresh_point_gun_actor(expected,live) || !live.in_vehicle ||
               !set_ped_state_profile_matches() || !authorized_now(authorized,authorization_context) ||
               (state==1 && live.native_state!=native_pedstate_attack && live.native_state!=native_pedstate_aim_gun) ||
               (state==50 && live.native_state!=1)) {
                result.diagnostic="Occupied source state restoration lost actor, seat, state or authorization";
                return result;
            }
            if(!invoke_set_ped_state(live.actor.native_ped,state)) {
                result.diagnostic="CPed::SetPedState entered and faulted during occupied state restoration";
                return result;
            }
        }
        if(!refresh_point_gun_actor(expected,live) || !live.in_vehicle || live.native_state!=50 ||
           !authorized_now(authorized,authorization_context)) {
            result.diagnostic="Occupied source state restoration did not retain DRIVING and actor ownership";
            return result;
        }
    }

    if(task) {
        uintptr_t live_task=0,live_animation=0;
        if(!clear_point_gun_profiles_match() || !refresh_point_gun_actor(expected,live) ||
           !route_use_gun_slot(live.actor.native_ped,live_task,live_animation) ||
           live_task!=task || live_animation!=fire_animation ||
           !authorized_now(authorized,authorization_context)) {
            result.diagnostic="Actor, vanilla UseGun slot, native profile, or route authorization changed before abort";
            return result;
        }
        bool aborted=false;
        entered=false;
        if(!invoke_abort_use_gun(task,live.actor.native_ped,&entered,&aborted)) {
            result.native_entered=result.native_entered||entered;
            result.diagnostic="CTaskSimpleUseGun::MakeAbortable(URGENT) entered SA and faulted";
            return result;
        }
        result.native_entered=true;
        if(!aborted) {
            result.diagnostic="Vanilla CTaskSimpleUseGun refused the urgent abort; no animation pointer was changed";
            return result;
        }

        if(fire_animation) {
            if(!clear_point_gun_profiles_match() || !refresh_point_gun_actor(expected,live) ||
               !route_use_gun_slot_still(live.actor.native_ped,task) || !route_use_gun_animation_released(task) ||
               !authorized_now(authorized,authorization_context)) {
                result.diagnostic="Actor, UseGun task, native profile, or route authorization changed before fire-animation fade";
                return result;
            }
            entered=false;
            if(!invoke_fire_animation_fade(fire_animation,&entered)) {
                result.native_entered=result.native_entered||entered;
                result.diagnostic="Validated UseGun FIRE/CROUCHFIRE association fade entered SA and faulted";
                return result;
            }
            result.native_entered=true;
        }
    }

    if(!refresh_point_gun_actor(expected,live) ||
       !route_use_gun_slot_still(live.actor.native_ped,task) ||
       !authorized_now(authorized,authorization_context)) {
        result.diagnostic="Actor, attack task, or source authorization changed during ClearPointGunAt";
        return result;
    }
    result.executed=true;
    return result;
}

RouteWarpEffectResult sa_route_warp_ped_near_entity_offscreen(
        const RouteActorVehicleSnapshot& actor,const RouteVehicleSnapshot& vehicle,
        uint32_t now,uint32_t leave_car_deadline,bool leave_car_deadline_known,
        RouteEffectAuthorization authorized,void* authorization_context) {
    RouteWarpEffectResult result;
    RouteActorVehicleSnapshot live_actor;
    RouteVehicleSnapshot live_vehicle;
    if(!sa_executor_profile_matches() || !warp_query_profiles_match() || !authorized ||
       !refresh_route_actor(actor,live_actor,false) || !refresh_route_vehicle(vehicle,live_vehicle) ||
       live_vehicle.driver_handle!=-1 || live_actor.on_screen!=std::optional<bool>(false) ||
       live_vehicle.on_screen!=std::optional<bool>(false) || !leave_car_deadline_known ||
       !authorized_now(authorized,authorization_context)) {
        result.native.diagnostic="Off-screen vehicle warp lacks live source identities, empty/off-screen vehicle facts, timer state, or route authorization";
        return result;
    }

    // The original helper checks its own ped visibility and timer before
    // starting the candidate loop. Its caller has already required the target
    // vehicle to be off-screen and empty.
    if(live_actor.on_screen.value_or(true) || leave_car_deadline>now) {
        result.native.executed=true;
        return result;
    }

    NativeVector target{live_vehicle.position[0],live_vehicle.position[1],live_vehicle.position[2]};
    NativeVector origin{live_actor.position[0],live_actor.position[1],live_actor.position[2]};
    const float dx=target.x-origin.x,dy=target.y-origin.y,dz=target.z-origin.z;
    const float distance=std::sqrt(dx*dx+dy*dy+dz*dz);
    const float half_distance=distance*0.5f;
    const NativeVector step{dx/half_distance,dy/half_distance,dz/half_distance};
    if(!std::isfinite(half_distance) || half_distance>=static_cast<float>(std::numeric_limits<int32_t>::max())) {
        result.native.diagnostic="Off-screen warp source distance is outside the native loop range";
        return result;
    }
    const int32_t half_distance_count=static_cast<int32_t>(half_distance);
    const int32_t try_count=half_distance_count<10?half_distance_count:10;
    NativeVector appropriate=origin;

    for(int32_t i=0;i<try_count;++i) {
        appropriate.x+=step.x;
        appropriate.y+=step.y;
        appropriate.z+=step.z;
        NativeVector z_corrected=appropriate;
        if(!invoke_find_z(z_corrected)) {
            result.native.native_entered=result.teleported;
            result.native.diagnostic="FindZCoorForPed faulted inside off-screen vehicle warp";
            return result;
        }

        if(std::fabs(z_corrected.z-target.z)<3.0f || std::fabs(z_corrected.z-appropriate.z)<3.0f) {
            appropriate.z=z_corrected.z;
            bool visible=false;
            if(!invoke_sphere_visible(appropriate,visible)) {
                result.native.native_entered=result.teleported;
                result.native.diagnostic="Camera sphere query faulted inside off-screen vehicle warp";
                return result;
            }
            if(!visible) {
                bool los_clear=false;
                if(!invoke_line_of_sight(appropriate,target,los_clear)) {
                    result.native.native_entered=result.teleported;
                    result.native.diagnostic="Line-of-sight query faulted inside off-screen vehicle warp";
                    return result;
                }
                if(los_clear) {
                    bool sphere_clear=false;
                    if(!invoke_test_sphere(appropriate,actor.actor.native_ped,sphere_clear)) {
                        result.native.native_entered=result.teleported;
                        result.native.diagnostic="World sphere query faulted inside off-screen vehicle warp";
                        return result;
                    }
                    if(sphere_clear) {
                        RouteActorVehicleSnapshot before_teleport_actor;
                        RouteVehicleSnapshot before_teleport_vehicle;
                        if(!sa_executor_profile_matches() || !teleport_slot_matches(actor.actor.native_ped) ||
                           !refresh_route_actor(actor,before_teleport_actor,false) ||
                           !refresh_route_vehicle(vehicle,before_teleport_vehicle) ||
                           before_teleport_vehicle.driver_handle!=-1 ||
                           !authorized_now(authorized,authorization_context)) {
                            result.native.native_entered=result.teleported;
                            result.native.diagnostic="Actor, vehicle, Teleport slot, or route epoch changed before off-screen warp";
                            return result;
                        }

                        bool entered=false;
                        if(!invoke_teleport(actor.actor.native_ped,appropriate,&entered)) {
                            result.native.native_entered=result.teleported || entered;
                            result.native.diagnostic="CPed::Teleport entered SA and faulted inside off-screen vehicle warp";
                            return result;
                        }
                        result.teleported=true;

                        // The source continues testing later two-metre steps,
                        // including multiple teleports. Stop only if callbacks
                        // replaced this actor/vehicle or route ownership.
                        RouteActorVehicleSnapshot after_teleport_actor;
                        RouteVehicleSnapshot after_teleport_vehicle;
                        if(!sa_executor_profile_matches() ||
                           !refresh_route_actor(actor,after_teleport_actor,false) ||
                           !refresh_route_vehicle(vehicle,after_teleport_vehicle) ||
                           !authorized_now(authorized,authorization_context)) {
                            result.native.native_entered=true;
                            result.native.diagnostic="Teleport completed but actor, vehicle, or route epoch changed before the next source step";
                            return result;
                        }
                    }
                }
            }
        }
    }

    // The original sets this after the loop even when try_count is zero or no
    // safe point was found. It does not break after a successful Teleport.
    RouteActorVehicleSnapshot final_actor;
    RouteVehicleSnapshot final_vehicle;
    if(!sa_executor_profile_matches() || !refresh_route_actor(actor,final_actor,false) ||
       !refresh_route_vehicle(vehicle,final_vehicle) || final_vehicle.driver_handle!=-1 ||
       !authorized_now(authorized,authorization_context)) {
        result.native.native_entered=result.teleported;
        result.native.diagnostic="Actor, vehicle, or route epoch changed before off-screen warp timer commit";
        return result;
    }
    result.native.executed=true;
    result.native.native_entered=result.teleported;
    result.leave_car_deadline=now+3000u;
    return result;
}

}
