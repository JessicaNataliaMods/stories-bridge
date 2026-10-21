#include <windows.h>
#include "lcs/sa_executor.hpp"
#include "lcs/route_vehicle_native.hpp"
#include "lcs/phone_phase_profile.hpp"
#include "lcs/route_native_backend.hpp"
#include "lcs/route_effects.hpp"
#include "lcs/pause_menu.hpp"
#include "lcs/owned_route_task.hpp"
#include "lcs/random_cop_policy.hpp"
#include "lcs/random_char_policy.hpp"
#include "lcs/sniper_query.hpp"
#include "lcs/cop_native_translation.hpp"
#include "lcs/mission_cleanup_receipt.hpp"
#include "lcs/native_memory.hpp"
#include "lcs/sa_task_memory.hpp"
#include "lcs/task_hook_transaction.hpp"
#include "lcs/sa_fx.hpp"
#include "sa_owned_vehicle_entry.hpp"
#include "sa_remote_door_policy.hpp"
#include "lcs/sa_script.hpp"
#include "lcs/locomotion_groups.hpp"
#include "ScriptCommands.h"
#include <array>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <cmath>
#include <sstream>
#include <stdexcept>
// Every mapped page belongs to this process and has no execute permission.
// PE signature bytes are only data. No game module or routine is executed.
namespace lcs {
uintptr_t task_ola_module=0;
std::array<uintptr_t,12> owned_entry_retail_driver{},owned_entry_retail_passenger{};
bool owned_entry_transition_dispatch=false;size_t owned_entry_transition_calls=0;
uintptr_t native_task_limit_adjuster_module(){return task_ola_module;}
uint8_t* owned_entry_ola_flags=nullptr;
// FxManager_c list (g_fxMan+0x0C head, +0x14 count; node +0 prev, +4 next) with FxSystem_c fields
// +8 blueprint, +0x50 play status (1 stopped after init), +0x51 kill flag (2 after Kill).
struct FxAdd {uintptr_t system;float position[3],velocity[3],time_since,multiplier[7],rotation,brightness,size;bool local;};
std::vector<std::string> fx_created;std::vector<std::array<float,3>> fx_created_at;std::vector<FxAdd> fx_adds;
std::vector<uintptr_t> fx_killed;size_t fx_plays=0;
int fx_create_fault=0,fx_create_null=0,fx_play_fault=0,fx_add_fault=0,fx_kill_fault=0;
alignas(16) uint8_t fx_storage[32][0x100];size_t fx_used=0;
void fx_unlink(uintptr_t system) {
    const auto base=native_entity_address(0xA9AE80);
    auto& head=*reinterpret_cast<uintptr_t*>(base+0x0C);auto& count=*reinterpret_cast<uint32_t*>(base+0x14);
    const auto prev=*reinterpret_cast<uintptr_t*>(system),next=*reinterpret_cast<uintptr_t*>(system+4);
    if(prev)*reinterpret_cast<uintptr_t*>(prev+4)=next;else head=next;
    if(next)*reinterpret_cast<uintptr_t*>(next)=prev;
    --count;
}
uintptr_t native_fx_create(const char* name,const float* position) {
    if(fx_create_fault)RaiseException(0xE0465801,0,0,nullptr);
    fx_created.push_back(name);fx_created_at.push_back({{position[0],position[1],position[2]}});
    if(fx_create_null)return 0;
    if(fx_used==32)RaiseException(0xE0465802,0,0,nullptr);
    const auto system=reinterpret_cast<uintptr_t>(fx_storage[fx_used++]);
    std::memset(reinterpret_cast<void*>(system),0,0x100);
    *reinterpret_cast<uintptr_t*>(system+8)=uintptr_t(0x500000+fx_used);
    *reinterpret_cast<uint8_t*>(system+0x50)=1;
    const auto base=native_entity_address(0xA9AE80);
    auto& head=*reinterpret_cast<uintptr_t*>(base+0x0C);
    *reinterpret_cast<uintptr_t*>(system+4)=head;if(head)*reinterpret_cast<uintptr_t*>(head)=system;head=system;
    ++*reinterpret_cast<uint32_t*>(base+0x14);
    return system;
}
void native_fx_play(uintptr_t system) {
    if(fx_play_fault)RaiseException(0xE0465803,0,0,nullptr);
    ++fx_plays;*reinterpret_cast<uint8_t*>(system+0x50)=0;
    // Play stores a per-play random offset at +0x64; +0x78 is the primitive array pointer.
    static uint32_t nonce=0x3F000000u;nonce+=0x1111u;
    *reinterpret_cast<uint32_t*>(system+0x64)=nonce;*reinterpret_cast<uintptr_t*>(system+0x78)=system+0x80;
}
void native_fx_kill(uintptr_t system) {
    if(fx_kill_fault)RaiseException(0xE0465804,0,0,nullptr);
    fx_killed.push_back(system);*reinterpret_cast<uint8_t*>(system+0x50)=1;*reinterpret_cast<uint8_t*>(system+0x51)=2;
}
void native_fx_add_particle(uintptr_t system,const float* position,const float* velocity,float time_since,
        const float* multiplier,float rotation,float brightness,float size,bool local) {
    if(fx_add_fault)RaiseException(0xE0465805,0,0,nullptr);
    FxAdd add{system,{position[0],position[1],position[2]},{velocity[0],velocity[1],velocity[2]},time_since,{},rotation,brightness,size,local};
    std::memcpy(add.multiplier,multiplier,sizeof(add.multiplier));fx_adds.push_back(add);
}
std::vector<uintptr_t> collective_entities;bool collective_query_valid=true;size_t collective_queries=0;
float native_collective_ground(float x,float y){if(x!=10 || y!=20)RaiseException(0xE0434F47,0,0,nullptr);return 33.f;}
std::array<float,3> critical_position{};float critical_heading=0;
size_t critical_overrides=0,critical_failures=0;bool critical_fault=false;
void native_override_restart(const std::array<float,3>& position,float heading) {
    ++critical_overrides;critical_position=position;critical_heading=heading;
    if(critical_fault)RaiseException(0xE0435245,0,0,nullptr);
}
void native_failed_critical(uintptr_t player) {
    ++critical_failures;
    auto* state=reinterpret_cast<uint8_t*>(player+0xDC);
    if(*state==0) {
        *state=3;*reinterpret_cast<uint8_t*>(native_entity_address(0x96A8B0))=3;
        *reinterpret_cast<uint32_t*>(native_entity_address(0x96A8AC))=*reinterpret_cast<uint32_t*>(native_entity_address(0xB7CB84));
    }
}
void native_collective_query(const std::array<float,3>& point,float radius,bool two_d,int16_t* count,int16_t limit,uintptr_t* entities,bool buildings,bool vehicles,bool peds,bool objects,bool dummies){
    if(point!=std::array<float,3>{{10,20,33}} || radius!=30 || !two_d || limit!=64 || buildings || !vehicles || !peds || objects || dummies)RaiseException(0xE0434F51,0,0,nullptr);
    ++collective_queries;*count=collective_query_valid?int16_t(collective_entities.size()):65;
    std::copy(collective_entities.begin(),collective_entities.end(),entities);
}
int phone_visual_fault=0;size_t phone_matrix_calls=0,phone_frame_calls=0;
void native_phone_copy_matrix(uintptr_t matrix,uintptr_t target) {
    ++phone_matrix_calls;
    for(size_t offset:{size_t(0),size_t(0x10),size_t(0x20),size_t(0x30)})std::memcpy(reinterpret_cast<void*>(target+offset),reinterpret_cast<const void*>(matrix+offset),12);
    if(phone_visual_fault==1)RaiseException(0xE050564D,0,0,nullptr);
    if(phone_visual_fault==2)*reinterpret_cast<uintptr_t*>(target-0x10+0xA0)=0;
}
void native_phone_update_frame(uintptr_t entity) {
    ++phone_frame_calls;if(phone_visual_fault==3)RaiseException(0xE0505646,0,0,nullptr);
    const auto rw=*reinterpret_cast<uintptr_t*>(entity+0x18),frame=*reinterpret_cast<uintptr_t*>(rw+4),root=*reinterpret_cast<uintptr_t*>(frame+0xA0);
    const auto engine=*reinterpret_cast<uintptr_t*>(native_entity_address(0xC97B24)),anchor=engine+0xBC,link=root+8;
    *reinterpret_cast<uintptr_t*>(link)=anchor;*reinterpret_cast<uintptr_t*>(link+4)=anchor;
    *reinterpret_cast<uintptr_t*>(anchor)=link;*reinterpret_cast<uintptr_t*>(anchor+4)=link;
    *reinterpret_cast<uint8_t*>(root+3)|=3;*reinterpret_cast<uint8_t*>(frame+3)|=12;
}
int phone_random_fault=0;size_t phone_random_calls=0;
bool phone_tls_ready=true;
bool native_phone_tls_ready(){return phone_tls_ready;}
uint32_t native_phone_random_sample() {
    ++phone_random_calls;if(phone_random_fault==1)RaiseException(0xE050524E,0,0,nullptr);
    return phone_random_fault==2?0x8000:0x1234;
}
size_t route_effect_idle_calls=0,route_effect_move_calls=0;
size_t route_clear_look_calls=0,route_clear_aim_calls=0,route_abort_gun_calls=0,route_fire_fade_calls=0;
int route_clear_gun_fault=0;
bool* route_clear_gate_to_revoke=nullptr;
bool* route_state_gate_to_revoke=nullptr;
std::vector<int32_t> route_clear_state_writes;
void native_route_effect_set_ped_state(uintptr_t ped,int32_t state) {
    if(state!=1 && state!=50)RaiseException(0xE0525354,0,0,nullptr);
    route_clear_state_writes.push_back(state);
    *reinterpret_cast<int32_t*>(ped+0x530)=state;
    if(state==1 && route_state_gate_to_revoke)*route_state_gate_to_revoke=false;
}
void native_route_effect_clear_look_flag(uintptr_t) {
    ++route_clear_look_calls;
    if(route_clear_gate_to_revoke)*route_clear_gate_to_revoke=false;
    if(route_clear_gun_fault==1)RaiseException(0xE052434C,0,0,nullptr);
}
void native_route_effect_clear_aim_flag(uintptr_t) {
    ++route_clear_aim_calls;
    if(route_clear_gun_fault==2)RaiseException(0xE0524341,0,0,nullptr);
}
bool native_route_effect_abort_use_gun(uintptr_t task,uintptr_t,int32_t priority) {
    if(priority!=1)RaiseException(0xE0524347,0,0,nullptr);
    ++route_abort_gun_calls;
    if(route_clear_gun_fault==3)return false;
    *reinterpret_cast<uintptr_t*>(task+0x2C)=0;return true;
}
bool native_route_effect_set_fire_animation_fade(uintptr_t animation,float delta,bool auto_remove) {
    ++route_fire_fade_calls;
    if(delta!=-4.0f || !auto_remove)RaiseException(0xE0524346,0,0,nullptr);
    *reinterpret_cast<float*>(animation+0x18)=delta;
    *reinterpret_cast<uint16_t*>(animation+0x2A)|=0x4;
    return route_clear_gun_fault!=4;
}
bool* route_effect_gate_to_revoke=nullptr;
void native_route_effect_set_idle(uintptr_t ped) {
    ++route_effect_idle_calls;
    *reinterpret_cast<int32_t*>(ped+0x530)=0;
    if(route_effect_gate_to_revoke)*route_effect_gate_to_revoke=false;
}
void native_route_effect_set_move_state(uintptr_t ped,int32_t state) {
    if(state!=1 && state!=6)RaiseException(0xE0524D53,0,0,nullptr);
    ++route_effect_move_calls;
    *reinterpret_cast<int32_t*>(ped+0x534)=state;
}
size_t route_effect_teleports=0,route_effect_ground_queries=0;
size_t route_effect_door_queries=0,route_effect_position_writes=0;
int32_t route_effect_door_node=-1;
bool native_route_effect_get_car_door_position(uintptr_t,int32_t node,float* xyz) {
    ++route_effect_door_queries;route_effect_door_node=node;
    xyz[0]=12.0f;xyz[1]=3.0f;xyz[2]=4.0f;
    return true;
}
void native_route_effect_set_position(uintptr_t ped,float x,float y,float z) {
    ++route_effect_position_writes;
    const float point[3]{x,y,z};std::memcpy(reinterpret_cast<void*>(ped+4),point,sizeof(point));
}
bool route_effect_world_blocked=false;
uintptr_t native_owned_vehicle_entry_allocate(uint32_t bytes);
void owned_entry_free(uintptr_t memory);
extern const uintptr_t owned_entry_passenger_vtable_value;
uintptr_t native_owned_vehicle_entry_passenger_vtable(){return owned_entry_passenger_vtable_value;}
size_t route_passenger_allocations=0,route_passenger_assignments=0,route_passenger_discards=0,route_passenger_releases=0;
int32_t route_passenger_node=-1;
bool route_passenger_carry_on=false,route_passenger_cleanup=false;
bool* route_passenger_revoke_after_construct=nullptr;
int route_passenger_assign_fault=0; // 1: SEH after the bank write, 2: SEH before it
uintptr_t route_passenger_last_task=0;
uintptr_t native_route_effect_allocate_enter_passenger(uint32_t bytes) {
    if(bytes!=0x50)RaiseException(0xE0525041,0,0,nullptr);
    ++route_passenger_allocations;
    return native_owned_vehicle_entry_allocate(bytes);
}
void native_route_effect_construct_enter_passenger(uintptr_t task,uintptr_t vehicle,int32_t node,bool carry_on) {
    std::memset(reinterpret_cast<void*>(task),0,0x50);
    *reinterpret_cast<uintptr_t*>(task)=owned_entry_passenger_vtable_value;
    *reinterpret_cast<uintptr_t*>(task+0x0C)=vehicle;*reinterpret_cast<int32_t*>(task+0x1C)=node;
    route_passenger_node=node;route_passenger_carry_on=carry_on;route_passenger_last_task=task;
    if(route_passenger_revoke_after_construct)*route_passenger_revoke_after_construct=false;
}
void native_route_effect_discard_enter_passenger(uintptr_t task) {++route_passenger_discards;owned_entry_free(task);}
void native_route_effect_release_enter_passenger_memory(uintptr_t memory) {++route_passenger_releases;owned_entry_free(memory);}
void native_route_effect_assign_enter_passenger(SaScript* script,int32_t actor,uintptr_t task,int32_t opcode) {
    if(opcode!=0x05CA || script->mission)RaiseException(0xE052504D,0,0,nullptr);
    ++route_passenger_assignments;route_passenger_cleanup=script->cleanup;
    if(route_passenger_assign_fault==2)RaiseException(0xE0525046,0,0,nullptr);
    const auto pool=*reinterpret_cast<uintptr_t*>(native_entity_address(0xB74490));
    const auto ped=*reinterpret_cast<uintptr_t*>(pool)+uintptr_t(uint32_t(actor)>>8)*0x7C4;
    auto* bank=reinterpret_cast<ScriptTaskRecord*>(native_entity_address(0xC0B1E8));
    bank[127]={opcode,0,uint32_t(task),uint32_t(ped),1};
    if(route_passenger_assign_fault==1)RaiseException(0xE0525047,0,0,nullptr);
}
// ---- private vehicle-entry lifetime backend (src/sa_owned_vehicle_entry.cpp) ----
// The SA task pool is 128-byte slots with a 0x80-free flag byte; the checked 05CA/05CB vtable slots are
// supplied by the fixture (retail code bytes at those addresses are the PE profiles themselves).
alignas(16) std::array<std::array<uint8_t,128>,8> owned_entry_slots{};
std::array<uint8_t,8> owned_entry_flags{};
std::array<uintptr_t,3> owned_entry_pool_header{};
std::array<uintptr_t,8> owned_entry_subtask_table{};
std::array<uintptr_t,8> owned_entry_standstill_table{};
size_t owned_entry_destroyed=0,owned_entry_clones=0,owned_entry_first=0,owned_entry_next=0,owned_entry_controls=0;
size_t owned_entry_aborts=0,owned_entry_assigns=0,owned_entry_discards=0;
size_t owned_entry_child_aborts=0,owned_entry_standstill_constructions=0;
uint32_t owned_entry_last_allocation_bytes=0;
bool owned_entry_allocation_unavailable=false;
int32_t owned_entry_standstill_duration=0;bool owned_entry_standstill_stay=false,owned_entry_standstill_area=false;
float owned_entry_standstill_blend=0;
bool owned_entry_abort_accepts=true,owned_entry_child_abort_accepts=true;
int32_t owned_entry_subtask_type=800;uintptr_t owned_entry_subtask=0;
constexpr uintptr_t owned_entry_driver_vtable=0x86E000,owned_entry_passenger_vtable=0x86E100;
const uintptr_t owned_entry_passenger_vtable_value=owned_entry_passenger_vtable;
uintptr_t owned_entry_slot_address(size_t index){return reinterpret_cast<uintptr_t>(owned_entry_slots[index].data());}
void owned_entry_free(uintptr_t memory) {
    for(size_t i=0;i<owned_entry_slots.size();++i)if(owned_entry_slot_address(i)==memory){owned_entry_flags[i]=0x80;if(owned_entry_ola_flags)owned_entry_ola_flags[2+i]=0x80;}
}
uintptr_t owned_entry_take() {
    for(size_t i=0;i<owned_entry_slots.size();++i)if(owned_entry_flags[i]&0x80){
        owned_entry_flags[i]=0;if(owned_entry_ola_flags)owned_entry_ola_flags[2+i]=0;std::memset(owned_entry_slots[i].data(),0,128);return owned_entry_slot_address(i);
    }
    return 0;
}
uintptr_t __fastcall owned_entry_native_destroy(uintptr_t task,uintptr_t,uint32_t flags) {
    ++owned_entry_destroyed;if(flags&1)owned_entry_free(task);return task;
}
uintptr_t __fastcall owned_entry_native_clone(uintptr_t task,uintptr_t) {
    ++owned_entry_clones;const auto copy=owned_entry_take();if(!copy)return 0;
    std::memcpy(reinterpret_cast<void*>(copy),reinterpret_cast<const void*>(task),0x50);
    const bool driver=(*reinterpret_cast<const uint8_t*>(task+0x10)&1)!=0;
    *reinterpret_cast<uintptr_t*>(copy)=driver?owned_entry_driver_vtable:owned_entry_passenger_vtable; // native class table
    *reinterpret_cast<uintptr_t*>(copy+4)=0;*reinterpret_cast<uintptr_t*>(copy+8)=0;
    return copy;
}
uintptr_t __fastcall owned_entry_native_first(uintptr_t,uintptr_t,uintptr_t){++owned_entry_first;return owned_entry_subtask;}
uintptr_t __fastcall owned_entry_native_transition(uintptr_t task,uintptr_t,uintptr_t ped) {
    if(!task || !ped)RaiseException(0xE0565452,0,0,nullptr);
    ++owned_entry_transition_calls;return task;
}
uintptr_t __fastcall owned_entry_native_next(uintptr_t task,uintptr_t,uintptr_t ped){
    ++owned_entry_next;
    if(owned_entry_transition_dispatch) {
        const auto* table=reinterpret_cast<const uintptr_t*>(*reinterpret_cast<const uintptr_t*>(task));
        return reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t,uintptr_t,uintptr_t)>(table[11])(task,0,ped);
    }
    return 0;
}
uintptr_t __fastcall owned_entry_native_control(uintptr_t,uintptr_t,uintptr_t){++owned_entry_controls;return owned_entry_subtask;}
int32_t __fastcall owned_entry_subtask_task_type(uintptr_t,uintptr_t){return owned_entry_subtask_type;}
int32_t __fastcall owned_entry_standstill_task_type(uintptr_t,uintptr_t){return 203;}
bool __fastcall owned_entry_child_abort(uintptr_t,uintptr_t,uintptr_t,int32_t priority,uintptr_t) {
    if(priority!=1)RaiseException(0xE0564341,0,0,nullptr);
    ++owned_entry_child_aborts;return owned_entry_child_abort_accepts;
}
bool native_owned_vehicle_entry_target_matches(size_t slot,uint16_t opcode,uintptr_t retail) {
    const auto& expected=opcode==0x05CB?owned_entry_retail_driver:owned_entry_retail_passenger;
    return slot<expected.size() && expected[slot]==retail;
}
uintptr_t native_owned_vehicle_entry_expected(size_t slot,uint16_t) {
    switch(slot) {
    case 0:return reinterpret_cast<uintptr_t>(&owned_entry_native_destroy);
    case 1:return reinterpret_cast<uintptr_t>(&owned_entry_native_clone);
    case 8:return reinterpret_cast<uintptr_t>(&owned_entry_native_next);
    case 9:return reinterpret_cast<uintptr_t>(&owned_entry_native_first);
    case 10:return reinterpret_cast<uintptr_t>(&owned_entry_native_control);
    case 11:return reinterpret_cast<uintptr_t>(&owned_entry_native_transition);
    default:return 0x1000+slot;
    }
}
uintptr_t native_owned_vehicle_entry_allocate(uint32_t bytes) {
    if(owned_entry_allocation_unavailable)return 0;
    if(bytes!=0x50 && bytes!=0x20)RaiseException(0xE0564541,0,0,nullptr);
    owned_entry_last_allocation_bytes=bytes;
    return owned_entry_take();
}
void native_owned_vehicle_entry_release(uintptr_t memory){owned_entry_free(memory);}
void native_owned_vehicle_entry_construct_driver(uintptr_t task,uintptr_t vehicle) {
    std::memset(reinterpret_cast<void*>(task),0,0x50);
    *reinterpret_cast<uintptr_t*>(task)=owned_entry_driver_vtable;
    *reinterpret_cast<uintptr_t*>(task+0x0C)=vehicle;*reinterpret_cast<uint8_t*>(task+0x10)=1;
}
int32_t native_owned_vehicle_entry_passenger_door(uintptr_t vehicle,int32_t seat) {
    if(*reinterpret_cast<const uint8_t*>(vehicle+0x429)&2u)return 8;
    if(seat==0)return *reinterpret_cast<const int32_t*>(vehicle+0x590)==9?11:8;
    if(seat==1)return 11;
    if(seat==2)return 9;
    return -1;
}
void native_owned_vehicle_entry_construct_passenger(uintptr_t task,uintptr_t vehicle,int32_t seat,bool) {
    std::memset(reinterpret_cast<void*>(task),0,0x50);
    *reinterpret_cast<uintptr_t*>(task)=owned_entry_passenger_vtable;
    *reinterpret_cast<uintptr_t*>(task+0x0C)=vehicle;*reinterpret_cast<int32_t*>(task+0x1C)=seat;
}
void native_owned_vehicle_entry_construct_standstill(uintptr_t task,int32_t duration,
        bool stay_in_same_place,bool use_entire_area,float blend_delta) {
    std::memset(reinterpret_cast<void*>(task),0,0x20);
    *reinterpret_cast<uintptr_t*>(task)=reinterpret_cast<uintptr_t>(owned_entry_standstill_table.data());
    owned_entry_standstill_duration=duration;owned_entry_standstill_stay=stay_in_same_place;
    owned_entry_standstill_area=use_entire_area;owned_entry_standstill_blend=blend_delta;
    ++owned_entry_standstill_constructions;
}
bool native_owned_vehicle_entry_subtask_table_matches(uintptr_t table,int32_t type) {
    if(type==800)return table==reinterpret_cast<uintptr_t>(owned_entry_subtask_table.data());
    if(type==203)return table==reinterpret_cast<uintptr_t>(owned_entry_standstill_table.data());
    return false;
}
void native_owned_vehicle_entry_discard(uintptr_t task,uint16_t){++owned_entry_discards;owned_entry_free(task);}
bool native_owned_vehicle_entry_assign(SaScript*,int32_t actor,uintptr_t task,int32_t opcode) {
    ++owned_entry_assigns;
    const auto pool=*reinterpret_cast<uintptr_t*>(native_entity_address(0xB74490));
    const auto ped=*reinterpret_cast<uintptr_t*>(pool)+uintptr_t(uint32_t(actor)>>8)*0x7C4;
    auto* bank=reinterpret_cast<ScriptTaskRecord*>(native_entity_address(0xC0B1E8));
    bank[127]={opcode,0,uint32_t(task),uint32_t(ped),1};return true;
}
bool native_owned_vehicle_entry_abort(uintptr_t,uintptr_t,int32_t priority) {
    if(priority!=1)RaiseException(0xE0564142,0,0,nullptr);
    ++owned_entry_aborts;return owned_entry_abort_accepts;
}
bool native_route_effect_is_on_screen(uintptr_t){return false;}
uintptr_t native_route_effect_get_pad(int32_t index){return native_entity_address(0xB73458+uintptr_t(index)*0x134);}
bool native_route_effect_find_z(float* xyz) {
    ++route_effect_ground_queries;xyz[2]=0.0f;return true;
}
bool native_route_effect_sphere_visible(float,float,float,float radius) {
    if(radius!=0.6f)RaiseException(0xE0525356,0,0,nullptr);return false;
}
bool native_route_effect_line_of_sight(float,float,float,float,float,float,bool buildings,bool vehicles,bool peds,
        bool objects,bool dummies,bool see_through,bool camera_ignore) {
    if(!buildings || !vehicles || peds || !objects || dummies || see_through || camera_ignore)
        RaiseException(0xE0524C53,0,0,nullptr);
    return true;
}
uintptr_t native_route_effect_test_sphere(float,float,float,float radius,uintptr_t ignore,
        bool buildings,bool vehicles,bool peds,bool objects,bool dummies,bool camera_ignore) {
    if(radius!=0.6f || !ignore || !buildings || !vehicles || peds || !objects || dummies || camera_ignore)
        RaiseException(0xE0525453,0,0,nullptr);
    return route_effect_world_blocked?uintptr_t(1):uintptr_t(0);
}
void native_route_effect_teleport(uintptr_t ped,float x,float y,float z,bool reset_rotation) {
    if(reset_rotation)RaiseException(0xE0525450,0,0,nullptr);
    ++route_effect_teleports;const float point[3]{x,y,z};std::memcpy(reinterpret_cast<void*>(ped+4),point,sizeof(point));
}
struct TurnForceCall{uintptr_t vehicle;std::array<float,3> force,point;};std::vector<TurnForceCall> turn_force_calls;int turn_force_fault=0;
void native_apply_turn_force(uintptr_t vehicle,float fx,float fy,float fz,float px,float py,float pz){
    turn_force_calls.push_back({vehicle,{fx,fy,fz},{px,py,pz}});if(turn_force_fault)RaiseException(0xE0545246,0,0,nullptr);
}
bool roof_result=false;size_t roof_calls=0;std::array<float,3> roof_last{};
bool native_roof_found(float x,float y,float z){++roof_calls;roof_last={x,y,z};return roof_result;}
int cop_creation_fault=0;size_t cop_creation_calls=0;
int cop_cleanup_fault=0;size_t cop_cleanup_calls=0;
void native_cop_cleanup(int32_t handle) {
    ++cop_cleanup_calls;if(cop_cleanup_fault==1)return;
    auto* table=reinterpret_cast<uint8_t*>(native_entity_address(0xA90850));
    for(size_t i=0;i<75;++i)if(!table[i*8]) {
        table[i*8]=2;std::memcpy(table+i*8+4,&handle,4);++table[600];
        if(cop_cleanup_fault==2)RaiseException(0xE043434C,0,0,nullptr);
        return;
    }
}
void native_cop_mission_creation(uintptr_t ped) {
    ++cop_creation_calls;
    *reinterpret_cast<uint8_t*>(ped+0x484)=2;
    if(cop_creation_fault==1)RaiseException(0xE0434352,0,0,nullptr);
    const auto brain=*reinterpret_cast<uintptr_t*>(ped+0x47C);
    const auto decision=*reinterpret_cast<int32_t*>(brain+0xB4);
    // Mirrors the reviewed 5E47E0 bytes: player types 0/1 get -2 and keep C4/C8.
    const auto type=*reinterpret_cast<int32_t*>(ped+0x598);const bool player_type=type==0 || type==1;
    *reinterpret_cast<int32_t*>(brain+(decision?0xB4:0xB8))=player_type?-2:-1;
    const auto sense=*reinterpret_cast<uint32_t*>(native_entity_address(0x86C92C));
    *reinterpret_cast<uint32_t*>(brain+0xBC)=sense;*reinterpret_cast<uint32_t*>(brain+0xC0)=sense;
    if(!player_type)*reinterpret_cast<uint32_t*>(brain+0xC4)=*reinterpret_cast<uint32_t*>(brain+0xC8)=0;
    if(cop_creation_fault==2)*reinterpret_cast<uintptr_t*>(ped+0x47C)=0;
    if(cop_creation_fault==3)*reinterpret_cast<uint32_t*>(brain+0xC4)=1;
}
std::array<uint8_t,0x28> route_task{};
size_t route_allocations=0,route_assignments=0,route_discards=0;int route_fault=0;
void (*route_during_target)()=nullptr;
int32_t route_move=0;float route_radius=0;std::array<float,3> route_target{};bool route_cleanup=false;
uintptr_t native_route_allocate() {
    ++route_allocations;
    if(route_fault==1)return 0;
    return reinterpret_cast<uintptr_t>(route_task.data());
}
void native_route_construct(uintptr_t,int32_t move,const std::array<float,3>& target,float radius) {
    *reinterpret_cast<uint32_t*>(route_task.data())=0x86FDA8;
    *reinterpret_cast<uint32_t*>(route_task.data()+4)=0;
    *reinterpret_cast<uint32_t*>(route_task.data()+8)=0;
    route_move=move;route_target=target;route_radius=1.5f; // stale identical-target pool contents
    if(route_fault==2)RaiseException(0xE0425254,0,0,nullptr);
}
void native_route_target(uintptr_t,const std::array<float,3>& target,float radius,bool force) {
    if(!force)RaiseException(0xE042464F,0,0,nullptr);
    route_target=target;route_radius=radius;
    if(route_during_target)route_during_target();
    if(route_fault==5)RaiseException(0xE0425256,0,0,nullptr);
}
void native_owned_route_retarget(uintptr_t task,const std::array<float,3>& target,float radius,float move_radius,bool force) {
    native_route_target(task,target,radius,force);
    std::memcpy(reinterpret_cast<void*>(task+0x10),target.data(),12);
    *reinterpret_cast<float*>(task+0x1C)=radius;*reinterpret_cast<float*>(task+0x20)=move_radius;
    *reinterpret_cast<uint8_t*>(task+0x24)|=4;
}
void native_route_discard(uintptr_t task) {
    if(task!=reinterpret_cast<uintptr_t>(route_task.data()))RaiseException(0xE0424449,0,0,nullptr);
    ++route_discards;
}
uintptr_t __fastcall native_owned_route_destroy(uintptr_t task,uintptr_t,uint32_t) {
    native_route_discard(task);return task;
}
uintptr_t __fastcall native_owned_route_clone(uintptr_t,uintptr_t) {return 0;}
void native_route_assign(SaScript* script,int32_t actor,uintptr_t task) {
    ++route_assignments;route_cleanup=script->cleanup;
    if(script->mission)RaiseException(0xE0424C56,0,0,nullptr);
    if(route_fault==3)RaiseException(0xE0425255,0,0,nullptr);
    const auto live=sa_live_objective_actor(actor);
    if(live && route_fault!=4) {
        auto* bank=reinterpret_cast<ScriptTaskRecord*>(native_entity_address(0xC0B1E8));
        bank[23]={0x05D3,0,uint32_t(task),uint32_t(live->native_ped),1};
    }
}
std::vector<std::pair<uintptr_t,int>> clear_object_events;int clear_object_fault=0;bool clear_object_recycle=false,clear_object_promote=false;
void native_clear_object_rw(uintptr_t entity) {
    clear_object_events.emplace_back(entity,1);
    if(clear_object_fault==1)RaiseException(0xE0434F31,0,0,nullptr);
    if(clear_object_promote)*reinterpret_cast<uint8_t*>(entity+0x13C)=2;
    if(clear_object_recycle) {
        const auto pool=*reinterpret_cast<uintptr_t*>(native_entity_address(0xB7449C));
        const auto data=*reinterpret_cast<uintptr_t*>(pool),tags=*reinterpret_cast<uintptr_t*>(pool+4);
        *reinterpret_cast<uint8_t*>(tags+(entity-data)/0x19C)^=1;
    }
}
void native_clear_object_world(uintptr_t entity) {
    clear_object_events.emplace_back(entity,2);
    if(clear_object_fault==2)RaiseException(0xE0434F32,0,0,nullptr);
}
void native_clear_object_delete(uintptr_t entity,bool dummy) {
    clear_object_events.emplace_back(entity,3);
    if(clear_object_fault==3)RaiseException(0xE0434F33,0,0,nullptr);
    const auto pool=*reinterpret_cast<uintptr_t*>(native_entity_address(dummy?0xB744A0:0xB7449C));
    const auto data=*reinterpret_cast<uintptr_t*>(pool),tags=*reinterpret_cast<uintptr_t*>(pool+4);
    *reinterpret_cast<uint8_t*>(tags+(entity-data)/(dummy?0x38:0x19C))|=0x80;
}
// Isolated Win32 tests never execute retail game code. These stubs simulate
// only the reviewed writes of the real native 45AE80->1562160 service.
size_t cancel_releases=0,cancel_camera_restores=0;
int cancel_fault=0;bool cancel_explode_argument=true;
std::vector<int> cancel_sequence;
void native_cancel_remote_release(bool explode) {
    ++cancel_releases;cancel_explode_argument=explode;cancel_sequence.push_back(2);
    if(cancel_fault==1)RaiseException(0xE0435231,0,0,nullptr);
    const auto focus=*reinterpret_cast<uint8_t*>(native_entity_address(0xB7CD74));
    const auto player=native_entity_address(0xB7CD98)+focus*0x190;
    const auto car=*reinterpret_cast<uintptr_t*>(player+0xB0);
    if(!car)RaiseException(0xE043524E,0,0,nullptr);
    if(*reinterpret_cast<uint8_t*>(car+0x4A4)==2) {
        *reinterpret_cast<uint8_t*>(car+0x4A4)=1;
        auto* list=reinterpret_cast<uint8_t*>(native_entity_address(0xA90850));
        const auto pool=*reinterpret_cast<uintptr_t*>(native_entity_address(0xB74494));
        const auto first=*reinterpret_cast<uintptr_t*>(pool);
        const auto tags=*reinterpret_cast<uintptr_t*>(pool+4);
        const auto slot=(car-first)/0xA18;
        const auto handle=int32_t((slot<<8)|*reinterpret_cast<uint8_t*>(tags+slot));
        for(size_t i=0;i<75;++i){auto* record=list+8*i;int32_t stored=0;
            std::memcpy(&stored,record+4,4);
            if(record[0]==1 && stored==handle){std::memset(record,0,8);--list[0x258];break;}
        }
    }
    *reinterpret_cast<uint8_t*>(car+0x428)&=0xF7;
    *reinterpret_cast<uint32_t*>(player+0xE0)=*reinterpret_cast<uint32_t*>(native_entity_address(0xB7CB84));
    *reinterpret_cast<uint8_t*>(player+0xDD)=1;
    *reinterpret_cast<uint8_t*>(player+0xDE)=uint8_t(explode);
    *reinterpret_cast<uint8_t*>(player+0xDF)=1;
    if(cancel_fault==2)RaiseException(0xE0435232,0,0,nullptr);
    if(cancel_fault==3) {
        const auto pool=*reinterpret_cast<uintptr_t*>(native_entity_address(0xB74494));
        auto* generations=reinterpret_cast<uint8_t*>(*reinterpret_cast<uintptr_t*>(pool+4));
        generations[0]++;
    }
    if(cancel_fault==4)*reinterpret_cast<uintptr_t*>(player+0xB0)=0xDEADBEEF;
    if(cancel_fault==8) {
        const auto pool=*reinterpret_cast<uintptr_t*>(native_entity_address(0xB74490));
        const auto first=*reinterpret_cast<uintptr_t*>(pool);
        const auto tags=*reinterpret_cast<uintptr_t*>(pool+4);
        const auto ped=*reinterpret_cast<uintptr_t*>(player);
        ++*reinterpret_cast<uint8_t*>(tags+(ped-first)/0x7C4);
    }
}
void native_cancel_camera_restore() {
    ++cancel_camera_restores;cancel_sequence.push_back(4);
    if(cancel_fault==6)RaiseException(0xE0435243,0,0,nullptr);
}
size_t rc_explosions=0;uintptr_t rc_player=0;bool rc_fault=false;
void native_blow_up_rc(uintptr_t player) {
    ++rc_explosions;rc_player=player;
    if(rc_fault)RaiseException(0xE0425243,0,0,nullptr);
}
uintptr_t isolated_pages=0;
uintptr_t native_entity_address(uintptr_t retail){return isolated_pages+retail-0x400000;}
bool sa_executor_profile_matches(){return true;}
struct TaskObserverRegressionMemory final:TaskHookMemory,TaskHookPreparation {
    static constexpr size_t trampoline_capacity=task_hook_sites.size()*32;
    std::array<uint8_t,trampoline_capacity> trampoline_bytes{};
    uint32_t allocation_address=0;size_t allocation_size=0;uintptr_t ola_image=0;
    bool published=false,prepared=false;
    explicit TaskObserverRegressionMemory(uintptr_t ola=0):ola_image(ola){}
    uint8_t* resolve(uint32_t address,size_t length) {
        if(!address || !length || uint64_t(address)+length>uint64_t(UINT32_MAX)+1)return nullptr;
        if(ola_image && address>=ola_image && uint64_t(address)+length<=uint64_t(ola_image)+0x68000)
            return reinterpret_cast<uint8_t*>(uintptr_t(address));
        if(allocation_address && address>=allocation_address &&
           uint64_t(address)+length<=uint64_t(allocation_address)+allocation_size)
            return trampoline_bytes.data()+(address-allocation_address);
        if(address>=0x400000 && uint64_t(address)+length<=0x1600000)
            return reinterpret_cast<uint8_t*>(native_entity_address(address));
        return nullptr;
    }
    bool read(uint32_t address,uint8_t* bytes,size_t length)override {
        auto* source=resolve(address,length);if(!source || !bytes)return false;
        std::memcpy(bytes,source,length);return true;
    }
    uint32_t allocate(size_t length)override {
        const auto base=reinterpret_cast<uintptr_t>(trampoline_bytes.data());
        if(allocation_address || !length || length>trampoline_bytes.size() || base>UINT32_MAX ||
           uint64_t(base)+length>uint64_t(UINT32_MAX)+1)return 0;
        allocation_address=uint32_t(base);allocation_size=length;return allocation_address;
    }
    bool publish(uint32_t address,const uint8_t* bytes,size_t length)override {
        if(!allocation_address || address!=allocation_address || length!=allocation_size || !bytes)return false;
        std::memcpy(trampoline_bytes.data(),bytes,length);published=true;return true;
    }
    Write exchange(uint32_t address,const uint8_t* expected,const uint8_t* desired,size_t length)override {
        auto* target=resolve(address,length);
        if(!published || !prepared || !target || !expected || !desired)return Write::unchanged;
        if(std::memcmp(target,expected,length))return Write::unchanged;
        std::memcpy(target,desired,length);return Write::written;
    }
    void release(uint32_t address)override {
        if(address==allocation_address){allocation_address=0;allocation_size=0;published=false;}
    }
    bool attach(const std::array<uint32_t,11>&)noexcept override {prepared=true;return true;}
    void detached()noexcept override {prepared=false;}
    void suspended()noexcept override {prepared=false;}
    uint8_t* writable(uint32_t address,size_t length){return resolve(address,length);}
};
TaskHookTransaction* task_memory_profile_transaction=nullptr;
TaskHookMemory* task_memory_profile_memory=nullptr;
bool task_memory_original_prefix_for_test(uintptr_t address,const uint8_t* expected,size_t length) {
    if(!task_memory_profile_transaction || !task_memory_profile_memory)return false;
    // Retail memory is represented by the isolated native-address translation;
    // the registered OLA release-site address remains its actual image address.
    if(address==native_entity_address(0x61A5B0))address=0x61A5B0;
    if(address>UINT32_MAX)return false;
    return task_memory_profile_transaction->owns_original_prefix(*task_memory_profile_memory,
        uint32_t(address),expected,length);
}
size_t big_lookups=0,big_displays=0;bool big_text_missing=false,big_fault=false,big_lookup_fault=false;
BigMissionMessage big_received;std::string big_key;
const char* native_big_message_text(const char* key) {
    ++big_lookups;big_key=key;
    if(big_lookup_fault)RaiseException(0xE0424249,0,0,nullptr);
    return big_text_missing?nullptr:"owned numbered text ~1~ ~1~ ~1~";
}
void native_big_message_display(const char* text,uint32_t duration,uint16_t style,const std::array<int32_t,6>& numbers) {
    ++big_displays;big_received.key=big_key;big_received.duration=duration;big_received.style=style;big_received.numbers=numbers;
    if(big_fault)RaiseException(0xE042424D,0,0,nullptr);
}
size_t frenzy_starts=0;bool frenzy_text_missing=false,frenzy_fault=false,frenzy_bad_flags=false;
std::array<int32_t,8> frenzy_arguments{};bool frenzy_headshots=false;std::string frenzy_key;
const char* native_frenzy_text(const char* key) {frenzy_key=key;return frenzy_text_missing?nullptr:"owned test GXT text";}
size_t garage_checks=0;uintptr_t garage_seen=0,garage_entity_seen=0;float garage_radius=1;bool garage_inside=false,garage_fault=false;
bool native_garage_contains(uintptr_t garage,uintptr_t car,float radius) {
    ++garage_checks;garage_seen=garage;garage_entity_seen=car;garage_radius=radius;
    if(garage_fault)RaiseException(0xE0424343,0,0,nullptr);
    return garage_inside;
}
size_t cleanup_calls=0;int32_t cleanup_seen=-1;bool cleanup_fault=false;
void native_remove_vehicle_cleanup(int32_t handle) {
    ++cleanup_calls;cleanup_seen=handle;
    auto* list=reinterpret_cast<uint8_t*>(native_entity_address(0xA90850));
    for(size_t i=0;i<75;++i) {
        auto* entry=list+i*8;int32_t stored=0;std::memcpy(&stored,entry+4,4);
        if(entry[0]!=1 || stored!=handle)continue;
        std::memset(entry,0,8);--list[0x258];
    }
    if(cleanup_fault)RaiseException(0xE0425452,0,0,nullptr);
}
size_t boot_visual_calls=0;uintptr_t boot_visual_car=0;bool boot_visual_fault=false;
void native_remove_boot_visual(uintptr_t car) {
    ++boot_visual_calls;boot_visual_car=car;
    if(boot_visual_fault)RaiseException(0xE042424F,0,0,nullptr);
}
std::vector<std::pair<uintptr_t,uintptr_t*>> entity_references;
bool actor_reference_mode=false,actor_reference_exhausted=false;
std::array<uintptr_t,2> actor_reference_node{};
bool remote_door_reference_mode=false;size_t remote_door_reference_count=0;
std::array<std::array<uintptr_t,2>,4> remote_door_reference_nodes{};
size_t attach_calls=0,heli_calls=0,reference_cleanups=0;bool attach_fault=false,heli_fault=false,reference_fault=false;
void native_cleanup_reference(uintptr_t entity,uintptr_t* slot) {
    ++reference_cleanups;
    if(cancel_releases)cancel_sequence.push_back(3);
    entity_references.erase(std::remove(entity_references.begin(),entity_references.end(),std::make_pair(entity,slot)),entity_references.end());
    if(cancel_releases || actor_reference_mode) {
        auto* link=reinterpret_cast<uintptr_t*>(entity+0x24);
        for(size_t n=0;*link && n<16;++n) {
            const auto node=*link;
            if(*reinterpret_cast<uintptr_t*>(node+sizeof(uintptr_t))==reinterpret_cast<uintptr_t>(slot)) {
                *link=*reinterpret_cast<uintptr_t*>(node);break;
            }
            link=reinterpret_cast<uintptr_t*>(node);
        }
    }
    if(cancel_fault==5)RaiseException(0xE0435252,0,0,nullptr);
    if(cancel_fault==7)*slot=0xDEADBEEF;
}
void native_register_reference(uintptr_t entity,uintptr_t* slot) {
    if(remote_door_reference_mode) {
        if(remote_door_reference_count>=remote_door_reference_nodes.size())RaiseException(0xE052444E,0,0,nullptr);
        auto& node=remote_door_reference_nodes[remote_door_reference_count++];
        node={{*reinterpret_cast<uintptr_t*>(entity+0x24),reinterpret_cast<uintptr_t>(slot)}};
        *reinterpret_cast<uintptr_t*>(entity+0x24)=reinterpret_cast<uintptr_t>(node.data());
    } else if(actor_reference_mode) {
        if(actor_reference_exhausted)return;
        actor_reference_node={{*reinterpret_cast<uintptr_t*>(entity+0x24),reinterpret_cast<uintptr_t>(slot)}};
        *reinterpret_cast<uintptr_t*>(entity+0x24)=reinterpret_cast<uintptr_t>(actor_reference_node.data());
    }
    const auto entry=std::make_pair(entity,slot);
    if(std::find(entity_references.begin(),entity_references.end(),entry)==entity_references.end())entity_references.push_back(entry);
    if(reference_fault)RaiseException(0xE0425246,0,0,nullptr);
}
uintptr_t rc_spawn_car=0;size_t rc_spawns=0,rc_cameras=0;
bool rc_spawn_fault=false;std::array<uintptr_t,2> rc_spawn_reference{};
std::array<float,3> rc_spawn_position{};float rc_spawn_rotation=0;
uint16_t rc_camera_mode=0,rc_camera_transition=0;int rc_appearance=3;
size_t rc_zooms=0,rc_cleanups=0;
void native_create_remote_car(std::array<float,3> position,float rotation,int16_t model) {
    ++rc_spawns;rc_spawn_position=position;rc_spawn_rotation=rotation;
    if(rc_spawn_fault)RaiseException(0xE0524346,0,0,nullptr);
    const auto focus=*reinterpret_cast<uint8_t*>(native_entity_address(0xB7CD74));
    auto* slot=reinterpret_cast<uintptr_t*>(native_entity_address(0xB7CD98)+focus*0x190+0xB0);
    *slot=rc_spawn_car;
    *reinterpret_cast<int16_t*>(rc_spawn_car+0x22)=model;
    *reinterpret_cast<uint8_t*>(rc_spawn_car+0x36)=uint8_t((8<<3)|2);
    rc_spawn_reference={{0,reinterpret_cast<uintptr_t>(slot)}};
    *reinterpret_cast<uintptr_t*>(rc_spawn_car+0x24)=reinterpret_cast<uintptr_t>(rc_spawn_reference.data());
    *reinterpret_cast<uint8_t*>(native_entity_address(0xB6F028)+0x38)=1;
    *reinterpret_cast<uint8_t*>(native_entity_address(0xB6F028)+0x39)=1;
    *reinterpret_cast<uint32_t*>(native_entity_address(0xB6F028)+0xC4)=99;
    *reinterpret_cast<uint32_t*>(native_entity_address(0xB6F028)+0xD8)=100;
}
void native_remote_camera(uintptr_t car,uint16_t mode,uint16_t transition) {
    if(car!=rc_spawn_car)RaiseException(0xE0524343,0,0,nullptr);
    ++rc_cameras;rc_camera_mode=mode;rc_camera_transition=transition;
}
void native_remote_zoom(int16_t mode) {if(mode)RaiseException(0xE052435A,0,0,nullptr);++rc_zooms;}
int32_t native_remote_appearance(uintptr_t) {return rc_appearance;}
void native_remote_cleanup(int32_t) {++rc_cleanups;}
size_t remote_door_set_calls=0,remote_door_component_calls=0,remote_door_progress_calls=0;
size_t remote_door_alternate_calls=0;uintptr_t remote_door_progress_test_site=0;
uint32_t remote_door_forwarded_status=0;bool remote_door_progress_accept=true;
bool remote_door_progress_fault=false;
void __fastcall remote_door_previous_setter(uintptr_t manager,uintptr_t,int32_t door,uint32_t status) {
    ++remote_door_set_calls;remote_door_forwarded_status=status;
    if(door>=0 && door<6)*reinterpret_cast<uint8_t*>(manager+9+door)=uint8_t(status);
}
void __fastcall remote_door_previous_alternate(uintptr_t manager,uintptr_t,int32_t door,uint32_t status) {
    ++remote_door_alternate_calls;remote_door_previous_setter(manager,0,door,status);
}
void __fastcall remote_door_previous_component(uintptr_t manager,uintptr_t,int32_t node,uint32_t status) {
    ++remote_door_component_calls;remote_door_forwarded_status=status;
    constexpr int doors[]={3,5,2,4};
    if(node>=8 && node<=11)*reinterpret_cast<uint8_t*>(manager+9+doors[node-8])=uint8_t(status);
}
bool __fastcall remote_door_previous_progress(uintptr_t manager,uintptr_t,int32_t door,uintptr_t) {
    ++remote_door_progress_calls;
    if(remote_door_progress_fault)RaiseException(0xE0524450,0,0,nullptr);
    if(door<0 || door>=6)return false;
    auto& status=*reinterpret_cast<uint8_t*>(manager+9+door);
    if(status>=4 || !remote_door_progress_accept)return false;
    status=status==0?2:status==1?3:4;return true;
}
DWORD remote_door_guarded_progress(uintptr_t manager,int32_t door,uintptr_t vehicle,bool* result) {
    __try {*result=reinterpret_cast<SaRemoteDoorProgressCallback>(sa_remote_door_policy_callback_for_site(remote_door_progress_test_site))(manager,0,door,vehicle);return 0;}
    __except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode();}
}
void native_attach_object(uintptr_t object,uintptr_t parent,std::array<float,3> offset) {
    ++attach_calls;
    *reinterpret_cast<uintptr_t*>(object+0xFC)=parent;
    native_register_reference(parent,reinterpret_cast<uintptr_t*>(object+0xFC));
    *reinterpret_cast<uintptr_t*>(object+0x128)=parent;
    std::memcpy(reinterpret_cast<void*>(object+0x100),offset.data(),12);
    std::memset(reinterpret_cast<void*>(object+0x10C),0,12);
    if(attach_fault)RaiseException(0xE0424154,0,0,nullptr);
}
void native_heli_goto(uintptr_t heli,std::array<float,3> position,float low,float high) {
    ++heli_calls;
    std::memcpy(reinterpret_cast<void*>(heli+0x3EC),position.data(),12);
    *reinterpret_cast<uint8_t*>(heli+0x3BA)=20;
    *reinterpret_cast<uint8_t*>(heli+0x3D0)=100;
    *reinterpret_cast<float*>(heli+0x9B0)=low;*reinterpret_cast<float*>(heli+0x9A8)=high;
    if(heli_fault)RaiseException(0xE042484C,0,0,nullptr);
}
size_t character_attach_calls=0;bool character_attach_fault=false,character_controllable=true;
std::vector<int32_t> wreck_visuals;uintptr_t wreck_seen=0;bool wreck_fault=false;
void native_wreck_bumper(uintptr_t car,int32_t panel) {
    wreck_seen=car;wreck_visuals.push_back(panel);
    if(wreck_fault)RaiseException(0xE0425752,0,0,nullptr);
}
void native_wreck_door(uintptr_t car,int32_t door) {
    wreck_seen=car;wreck_visuals.push_back(100+door);
    if(wreck_fault)RaiseException(0xE0425752,0,0,nullptr);
}
uint16_t character_facing=0;float character_angle=0;int32_t character_weapon=0;
uintptr_t native_attach_character(uintptr_t ped,uintptr_t object,std::array<float,3> offset,uint16_t facing,float angle,int32_t weapon) {
    ++character_attach_calls;character_facing=facing;character_angle=angle;character_weapon=weapon;
    if(!character_controllable)return 0;
    *reinterpret_cast<uintptr_t*>(ped+0xFC)=object;
    native_register_reference(object,reinterpret_cast<uintptr_t*>(ped+0xFC));
    std::memcpy(reinterpret_cast<void*>(ped+0x76C),offset.data(),12);
    *reinterpret_cast<uint16_t*>(ped+0x778)=facing;*reinterpret_cast<float*>(ped+0x77C)=angle;
    if(character_attach_fault)RaiseException(0xE0424341,0,0,nullptr);
    return object;
}
void native_frenzy_start(int32_t weapon,int32_t time,uint16_t kills,int32_t model,const char*,int32_t model2,int32_t model3,int32_t model4,bool standard,bool headshots) {
    ++frenzy_starts;frenzy_arguments={{weapon,time,int32_t(kills),model,model2,model3,model4,int32_t(standard)}};frenzy_headshots=headshots;
    // This boundary simulates engine outputs only; no SA routine is executed.
    *reinterpret_cast<uint16_t*>(native_entity_address(0x96A704))=1;
    *reinterpret_cast<uint8_t*>(native_entity_address(0x969A49))=uint8_t(headshots && !frenzy_bad_flags);
    *reinterpret_cast<uint8_t*>(native_entity_address(0x969A4A))=uint8_t(standard);
    constexpr uintptr_t fields[]={0x96A700,0x96A6E8,0x96A6EC,0x96A6FC,0x96A6F8,0x96A6F4,0x96A6F0};
    for(size_t i=0;i<7;++i)*reinterpret_cast<int32_t*>(native_entity_address(fields[i]))=frenzy_arguments[i];
    if(frenzy_fault)RaiseException(0xE0424242,0,0,nullptr);
}
}
#include "lcs/wander_path.hpp"
namespace lcs {
size_t captured_walk_calls=0;int captured_walk_mode=0;uint8_t* captured_walk_generation=nullptr;
NativeResult captured_walk_reply;
NativeCall captured_walk_packet;
NativeResult native_execute_captured_walk(const NativeCall& call) {
    ++captured_walk_calls;captured_walk_packet=call;
    auto reply=captured_walk_reply;
    if(!reply.executed)return reply;
    const auto actor=sa_live_objective_actor(call.inputs[0]);
    ScriptTaskSnapshot bank{};std::memcpy(bank.data(),reinterpret_cast<const void*>(native_entity_address(0xC0B1E8)),sizeof(bank));
    if(captured_walk_mode!=1 && actor)bank[7]={call.opcode,0x123456,0,uint32_t(actor->native_ped),0};
    if(captured_walk_mode==2 && actor)bank[8]={call.opcode,0x123457,0,uint32_t(actor->native_ped),0};
    std::memcpy(reinterpret_cast<void*>(native_entity_address(0xC0B1E8)),bank.data(),sizeof(bank));
    if(captured_walk_mode==3)*reinterpret_cast<uint8_t*>(native_entity_address(0x60872F))^=1;
    if(captured_walk_mode==4 && captured_walk_generation)*captured_walk_generation=28;
    return reply;
}
size_t captured_vehicle_calls=0;int captured_vehicle_mode=0;
NativeResult captured_vehicle_reply;
NativeCall captured_vehicle_packet;
NativeResult native_execute_captured_vehicle(const NativeCall& call) {
    ++captured_vehicle_calls;captured_vehicle_packet=call;
    auto reply=captured_vehicle_reply;
    if(!reply.executed)return reply;
    const auto actor=sa_live_objective_actor(call.inputs[0]);
    ScriptTaskSnapshot bank{};
    std::memcpy(bank.data(),reinterpret_cast<const void*>(native_entity_address(0xC0B1E8)),sizeof(bank));
    if(captured_vehicle_mode!=1 && actor)
        bank[11]={call.opcode,0x234567,0,uint32_t(actor->native_ped),0};
    std::memcpy(reinterpret_cast<void*>(native_entity_address(0xC0B1E8)),bank.data(),sizeof(bank));
    return reply;
}
bool wander_los_clear=true,wander_query_fault=false;
WanderHeight wander_water_result{true,{}},wander_ground_result{true,0.f};
size_t wander_los_calls=0,wander_water_calls=0,wander_ray_calls=0;
WanderPoint wander_seen_origin{},wander_seen_target{};
bool native_wander_los(const WanderPoint& from,const WanderPoint& to) {
    ++wander_los_calls;wander_seen_origin=from;wander_seen_target=to;
    if(wander_query_fault)RaiseException(0xE0425750,0,0,nullptr);
    return wander_los_clear;
}
WanderHeight native_wander_water(const WanderPoint&) {++wander_water_calls;return wander_water_result;}
WanderHeight native_wander_vertical(const WanderPoint&,float) {++wander_ray_calls;return wander_ground_result;}
}
using namespace lcs;
static void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
struct RouteStopAuthorizationFixture {bool allowed=true;int calls=0,deny_on_call=0;};
static bool route_stop_authorized(void* context) {
    auto& fixture=*static_cast<RouteStopAuthorizationFixture*>(context);
    ++fixture.calls;
    return fixture.allowed && fixture.calls!=fixture.deny_on_call;
}
static void profile(const std::vector<uint8_t>& pe,uintptr_t address,size_t length) {
    check(pe.size()>64,"Reference must contain a PE header");uint32_t nt=0;std::memcpy(&nt,pe.data()+60,4);
    check(nt+24<pe.size(),"Reference NT header must fit");auto* header=reinterpret_cast<const IMAGE_NT_HEADERS32*>(pe.data()+nt);
    check(header->Signature==IMAGE_NT_SIGNATURE && header->OptionalHeader.ImageBase==0x400000 && header->FileHeader.Machine==IMAGE_FILE_MACHINE_I386,"Native reference must be a fixed-base x86 PE");
    const size_t sections=nt+24+header->FileHeader.SizeOfOptionalHeader;
    check(sections+header->FileHeader.NumberOfSections*sizeof(IMAGE_SECTION_HEADER)<=pe.size(),"PE section table must fit");
    for(size_t n=0;n<header->FileHeader.NumberOfSections;++n) {
        const auto* s=reinterpret_cast<const IMAGE_SECTION_HEADER*>(pe.data()+sections+n*sizeof(IMAGE_SECTION_HEADER));const auto rva=address-0x400000;
        if(rva<s->VirtualAddress || rva+length>s->VirtualAddress+s->SizeOfRawData)continue;
        const auto offset=s->PointerToRawData+rva-s->VirtualAddress;check(offset+length<=pe.size(),"Native signature must fit the on-disk image");
        std::memcpy(reinterpret_cast<void*>(native_entity_address(address)),pe.data()+offset,length);return;
    }
    throw std::runtime_error("Native profile was not found in on-disk PE data");
}
static void pointer(uintptr_t address,const void* value){*reinterpret_cast<const void**>(native_entity_address(address))=value;}
struct Pool {void* objects;uint8_t* flags;int32_t count;};
static NativeCall operation(EntityCommand action,std::vector<int32_t> inputs,size_t outputs=0) {
    NativeCall c;c.source_entity_command=action;c.inputs=std::move(inputs);c.outputs=outputs;return c;
}
static size_t sniper_updates=0,sniper_collisions=0;
static bool sniper_reset_seen=false;
static const float* sniper_forwarded_start=nullptr;
static const float* sniper_forwarded_end=nullptr;
static void __cdecl previous_sniper_update() {
    ++sniper_updates;
    const auto result=sa_sniper_bullet_in_area({-1,-1,-1,1,1,1});
    sniper_reset_seen=result && !*result;
}
static void __cdecl previous_sniper_collision(const float* start,const float* end) {
    ++sniper_collisions;sniper_forwarded_start=start;sniper_forwarded_end=end;
}
int main(int argc,char** argv){try{
    check(argc==2 || argc==3,"On-disk native PE reference and optional installed OLA PE are required");std::ifstream in(argv[1],std::ios::binary);check(bool(in),"Native reference must open as data");
    const std::vector<uint8_t> pe((std::istreambuf_iterator<char>(in)),{});
    isolated_pages=reinterpret_cast<uintptr_t>(VirtualAlloc(nullptr,0x1200000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    check(isolated_pages!=0,"Owned non-executable native data pages must allocate");
    profile(pe,0x61A55A,3); // Native 128-byte task slots, independently pinned from OLA blocks.
    profile(pe,0x64F190,80); // Native SCM seat to car-door selector (including bus/bike paths).
    profile(pe,0x86EAAC,48);profile(pe,0x86EADC,48);
    std::memcpy(owned_entry_retail_driver.data(),reinterpret_cast<void*>(native_entity_address(0x86EAAC)),48);
    std::memcpy(owned_entry_retail_passenger.data(),reinterpret_cast<void*>(native_entity_address(0x86EADC)),48);
    check(owned_entry_retail_driver[0]==0x643C60 && owned_entry_retail_passenger[0]==0x643C80 &&
          owned_entry_retail_driver[11]==0x63F970 && owned_entry_retail_passenger[11]==0x63F970,
          "Actual native PE defines twelve entry methods and class-specific deleting destructors");
    profile(pe,0x694444,10);profile(pe,0x694471,10); // exact CPed bus-jacked writer and reader
    // Entry subtasks and the route task are different classes: the table named for a subtask type must
    // be the class whose GetTaskType returns that type (CreateSubTask 0x63E040 builds 800 with 0x645780).
    profile(pe,0x86EC4C,48);profile(pe,0x86DD2C,44);profile(pe,0x86FDA8,44);
    profile(pe,0x645830,6);profile(pe,0x62F350,6);profile(pe,0x668230,6);
    {
        const auto task_type=[](uintptr_t table)->int32_t {
            const auto getter=*reinterpret_cast<const uintptr_t*>(native_entity_address(table)+4*sizeof(uintptr_t));
            const auto* code=reinterpret_cast<const uint8_t*>(native_entity_address(getter));
            if(code[0]!=0xB8 || code[5]!=0xC3)return -1;
            int32_t type=0;std::memcpy(&type,code+1,4);return type;
        };
        check(task_type(sa_owned_entry_subtask_vtable(800))==800,"Entry approach table must be the class reporting task type 800");
        check(task_type(sa_owned_entry_subtask_vtable(203))==203,"Entry stand-still table must be the class reporting task type 203");
        check(task_type(0x86FDA8)==903 && sa_owned_entry_subtask_vtable(800)!=0x86FDA8,"The route GoToPoint table is another class, never the entry approach");
    }

    {
        const std::array<float,6> box{{-1,-1,-1,1,1,1}};
        check(!sa_sniper_bullet_in_area(box),"899 refuses a missing native producer before installation");
        profile(pe,0x53C059,17);profile(pe,0x7367C0,64);profile(pe,0x73677C,4);profile(pe,0x73679F,2);
        size_t count=0;const auto sites=sa_sniper_query_call_sites(&count);
        check(count==2 && sites[0]==0x53C05C && sites[1]==0x7367E3,"Sniper uses the actual weapon-update and segment-consumer calls");
        const std::array<uintptr_t,2> retail_targets{{0x73A360,0x6C6890}};
        const std::array<uintptr_t,2> delegates{{reinterpret_cast<uintptr_t>(&previous_sniper_update),reinterpret_cast<uintptr_t>(&previous_sniper_collision)}};
        for(size_t i=0;i<count;++i) {
            auto* code=reinterpret_cast<uint8_t*>(native_entity_address(sites[i]));int32_t old_relative=0;
            std::memcpy(&old_relative,code+1,4);
            check(code[0]==0xE8 && uintptr_t(int64_t(sites[i]+5)+old_relative)==retail_targets[i],"Independent PE data proves the two real native producers");
            check(sa_sniper_query_bind_callsite(sites[i],delegates[i]),"Each sniper callback retains its previous compiled target before publication");
            const auto relative=int32_t(sa_sniper_query_callback_for_site(sites[i])-(sites[i]+5));std::memcpy(code+1,&relative,4);
            if(i==0)check(!sa_sniper_query_calls_healthy(),"A partial sniper hook installation cannot admit the query");
        }
        check(sa_sniper_query_calls_healthy() && !sa_sniper_bullet_in_area(box),"Installed hooks require an actual weapon-update boundary before querying");
        const auto update=reinterpret_cast<void(__cdecl*)()>(sa_sniper_query_callback_for_site(sites[0]));
        const auto capture=reinterpret_cast<void(__cdecl*)(const float*,const float*)>(sa_sniper_query_callback_for_site(sites[1]));
        update();check(sniper_updates==1 && sniper_reset_seen,"Weapon update clears the latch before chaining the previous mod target");
        float start[3]{-5,100,0},end[3]{-4,101,0};capture(start,end);
        check(sniper_collisions==1 && sniper_forwarded_start==start && sniper_forwarded_end==end,"Capture preserves both pointer arguments and the prior line consumer");
        const auto historical=sa_sniper_bullet_in_area(box);
        check(historical && *historical,"Retail scalar clipping deliberately preserves its historical true result for this geometric miss");
        start[0]=start[1]=start[2]=end[0]=end[1]=end[2]=0;capture(start,end);
        const auto degenerate=sa_sniper_bullet_in_area({0,0,0,0,0,0});
        check(degenerate && !*degenerate,"The last sniper segment wins and the original degenerate-X early-out is retained");
        const auto y_path=sa_sniper_bullet_in_area({-1,0,-1,1,0,1});
        check(y_path && *y_path,"Retail's Y path has no FIX_BUGS early-out");
        start[0]=std::numeric_limits<float>::quiet_NaN();capture(start,end);
        check(!sa_sniper_bullet_in_area(box),"A corrupt latest segment is unavailable, without reusing the earlier good segment");
        update();const auto empty=sa_sniper_bullet_in_area(box);
        check(empty && !*empty && sniper_updates==2,"The next weapon update clears even invalid segment state");
        auto* code=reinterpret_cast<uint8_t*>(native_entity_address(sites[1]));const auto saved=*code;*code=0x90;
        start[0]=0;capture(start,end);
        check(!sa_sniper_query_calls_healthy() && !sa_sniper_bullet_in_area(box) && sniper_collisions==4,"Later mod chaining still delegates, while lost hook ownership refuses a stale query");
        *code=saved;check(sa_sniper_query_calls_healthy(),"Restoring the reviewed callback restores availability");
        auto* filter=reinterpret_cast<uint8_t*>(native_entity_address(0x73677C));const auto original_filter=*filter;*filter^=1;
        check(!sa_sniper_query_calls_healthy(),"An altered sniper-type producer filter cannot admit other weapon segments");*filter=original_filter;
    }
    {
        // SA FxManager backend for 1084/930/947: retail entry profiles come from the on-disk PE as data.
        const auto near_equal=[](float a,float b){return std::fabs(a-b)<.0001f;};
        FxParticleRequest blood;blood.kind=FxSystemKind::blood;blood.position={{1,2,3}};blood.velocity={{.1f,.2f,.3f}};blood.size=.5f;
        auto refused=sa_fx_emit(blood);
        check(!refused.ok && !refused.native_entered && fx_created.empty(),"AddParticle refuses before any native call while the retail profiles are absent");
        profile(pe,0x4A9BE0,16);profile(pe,0x4AA2F0,16);profile(pe,0x4AA3F0,16);profile(pe,0x4AA440,16);
        check(sa_fx_profiles_match(),"The PE data holds the four reviewed FxManager/FxSystem entry profiles");
        auto emitted=sa_fx_emit(blood);
        check(emitted.ok && fx_created==std::vector<std::string>({"prt_blood"}) && fx_plays==1 && fx_adds.size()==1 &&
              fx_created_at[0]==std::array<float,3>{{0,0,0}},"First blood particle creates and starts SA's prt_blood at the origin, then adds one particle");
        const auto& add=fx_adds[0];
        check(add.system==reinterpret_cast<uintptr_t>(fx_storage[0]) && near_equal(add.position[2],3) && near_equal(add.velocity[0],3) && near_equal(add.velocity[1],6) && near_equal(add.velocity[2],9) &&
              add.time_since==0 && near_equal(add.multiplier[4],5) && add.multiplier[0]==1 && add.multiplier[3]==1 && add.multiplier[5]==0 && add.multiplier[6]==1 && add.rotation==-1 && !add.local,
              "Position, 30 Hz velocity, blueprint-relative size and FxPrtMult fields reach SA's AddParticle");
        check(sa_fx_emit(blood).ok && fx_created.size()==1 && fx_adds.size()==2,"The owned system is reused");
        FxParticleRequest smoke;smoke.kind=FxSystemKind::smoke_dark;smoke.size=4;smoke.has_color=true;smoke.red=16;smoke.green=16;smoke.blue=16;smoke.alpha=255;
        check(sa_fx_emit(smoke).ok && fx_created.back()=="prt_smokeII_3_expand" && near_equal(fx_adds.back().multiplier[4],2) && near_equal(fx_adds.back().multiplier[0],16/255.0f) && fx_adds.back().multiplier[3]==1,"Dark smoke uses prt_smokeII_3_expand with the colour and normalised size");
        smoke.kind=FxSystemKind::smoke_light;smoke.size=0;smoke.has_color=false;
        check(sa_fx_emit(smoke).ok && fx_created.back()=="prt_collisionsmoke" && fx_adds.back().multiplier[4]==1 && fx_adds.back().multiplier[1]==1,"Light smoke uses prt_collisionsmoke and the blueprint default size");
        const auto before_adds=fx_adds.size(),before_created=fx_created.size();
        auto bad=blood;bad.velocity[1]=std::numeric_limits<float>::infinity();
        check(!sa_fx_emit(bad).ok && fx_adds.size()==before_adds,"Non-finite vectors never reach SA");
        bad=blood;bad.size=-1;check(!sa_fx_emit(bad).ok && fx_adds.size()==before_adds,"Negative sizes never reach SA");
        // SA frees or kills the system by itself: the stale pointer is never fed again.
        fx_unlink(reinterpret_cast<uintptr_t>(fx_storage[0]));
        check(sa_fx_emit(blood).ok && fx_created.size()==before_created+1 && fx_adds.back().system!=reinterpret_cast<uintptr_t>(fx_storage[0]),"A system SA removed from its list is recreated, never reused");
        *reinterpret_cast<uint8_t*>(fx_adds.back().system+0x51)=2;
        check(sa_fx_emit(blood).ok && fx_created.size()==before_created+2,"A system carrying SA's kill flag is recreated");
        fx_create_fault=1;*reinterpret_cast<uint8_t*>(fx_adds.back().system+0x51)=2;
        auto create_failed=sa_fx_emit(blood);
        check(!create_failed.ok && create_failed.native_entered,"A faulting CreateFxSystem is an entered failure");fx_create_fault=0;
        fx_create_null=1;auto no_blueprint=sa_fx_emit(blood);fx_create_null=0;
        check(!no_blueprint.ok && no_blueprint.native_entered && no_blueprint.diagnostic.find("blueprint")!=std::string::npos,"A name SA cannot resolve is an entered failure, never a fake system");
        fx_play_fault=1;const auto kills_before=fx_killed.size();
        auto play_failed=sa_fx_emit(blood);
        check(!play_failed.ok && fx_killed.size()==kills_before+1,"A system whose Play faults is killed again and never fed");fx_play_fault=0;
        fx_add_fault=1;auto add_failed=sa_fx_emit(blood);fx_add_fault=0;
        check(!add_failed.ok && add_failed.native_entered && add_failed.diagnostic.find("AddParticle")!=std::string::npos,"A faulting AddParticle reports an entered failure");
        // Fires: SA's own CFire size rule picks the blueprint; tokens carry a generation.
        FxStatus fs;
        const auto small=sa_fx_fire_start({{5,6,7}},.5f,fs);const auto medium=sa_fx_fire_start({{5,6,7}},1.5f,fs);const auto large=sa_fx_fire_start({{5,6,7}},3.0f,fs);
        check(small && medium && large && fx_created[fx_created.size()-3]=="fire" && fx_created[fx_created.size()-2]=="fire_med" && fx_created.back()=="fire_large" &&
              fx_created_at.back()==std::array<float,3>{{5,6,7}},"Fire strength <=1, <=2 and above select fire, fire_med and fire_large at the requested position");
        check(small!=medium && medium!=large,"Fire tokens are distinct");
        const auto kill_mark=fx_killed.size();
        check(sa_fx_fire_stop(medium) && fx_killed.size()==kill_mark+1,"Stopping a fire kills exactly that SA system");
        check(sa_fx_fire_stop(medium) && fx_killed.size()==kill_mark+1,"A stale token is already finished and never kills a reused slot");
        const auto reuse=sa_fx_fire_start({{1,1,1}},.2f,fs);
        check(reuse && reuse!=medium && (reuse&0xFFFF)==(medium&0xFFFF) && sa_fx_fire_stop(medium) && fx_killed.size()==kill_mark+1,"A token from before slot reuse cannot stop the new fire");
        fx_kill_fault=1;check(!sa_fx_fire_stop(large),"A faulting Kill reports retry");fx_kill_fault=0;
        const auto after_fault=fx_killed.size();check(sa_fx_fire_stop(large) && fx_killed.size()==after_fault+1,"The retry kills the fire exactly once");
        FxStatus no_fire;auto none=sa_fx_fire_start({{0,0,0}},std::numeric_limits<float>::infinity(),no_fire);
        check(none==0 && !no_fire.diagnostic.empty(),"A non-finite fire size starts nothing");
        const auto owned_left=fx_killed.size();
        sa_fx_release_all();
        check(fx_killed.size()>owned_left,"Release kills the bridge-owned systems that are still alive");
        const auto after_release=fx_killed.size();sa_fx_release_all();
        check(fx_killed.size()==after_release,"A second release has nothing left to kill");
        check(sa_fx_pending()==0,"Nothing is pending before the ownership scenarios");
        // AF2: Play and Kill both fail. The created system stays referenced until a Kill is confirmed.
        fx_play_fault=1;fx_kill_fault=1;
        const auto orphan_index=fx_used;
        const auto both=sa_fx_emit(blood);
        const auto orphan=reinterpret_cast<uintptr_t>(fx_storage[orphan_index]);
        check(!both.ok && both.native_entered && sa_fx_pending()==1,
              "Play and Kill failing together keeps the created system referenced");
        const auto kills_mark=fx_killed.size();
        for(int attempt=0;attempt<3;++attempt)sa_fx_service_pending();
        check(sa_fx_pending()==1 && fx_killed.size()==kills_mark,"A refused Kill is retried without dropping the reference or reporting a stop");
        fx_play_fault=0;fx_kill_fault=0;
        sa_fx_service_pending();
        check(sa_fx_pending()==0 && fx_killed.size()==kills_mark+1 && fx_killed.back()==orphan,"A later confirmed Kill releases the retained orphan exactly once");
        sa_fx_service_pending();check(fx_killed.size()==kills_mark+1,"The confirmed orphan is not killed again");
        // AF3: release/abort while Kill is refused repeatedly, then recovery without any campaign.
        FxStatus own;
        const auto foreign_system=native_fx_create("foreign",std::array<float,3>{{0,0,0}}.data());native_fx_play(foreign_system);
        const auto pool_emit=sa_fx_emit(blood);
        const auto pool_system=fx_adds.back().system;
        const auto fire_a=sa_fx_fire_start({{4,4,4}},.5f,own);
        const auto fire_a_system=reinterpret_cast<uintptr_t>(fx_storage[fx_used-1]);
        check(pool_emit.ok && fire_a,"Fixture owns one pool system and one fire");
        fx_kill_fault=1;
        const auto kills_before_abort=fx_killed.size();
        check(!sa_fx_release_all() && sa_fx_pending()==2 && fx_killed.size()==kills_before_abort,"Release with Kill refused keeps every reference and reports it");
        for(int attempt=0;attempt<3;++attempt)sa_fx_service_pending();
        check(sa_fx_pending()==2,"Repeated refusals keep ownership with no halted-campaign tick involved");
        check(!sa_fx_fire_stop(fire_a),"The fire token stays valid (same generation) while its stop is unconfirmed");
        const auto fire_b=sa_fx_fire_start({{5,5,5}},.5f,own);
        check(fire_b && (fire_b&0xFFFF)!=(fire_a&0xFFFF),"A fire slot with an unconfirmed stop is never reused");
        fx_kill_fault=0;
        sa_fx_service_pending();
        check(sa_fx_pending()==0 && fx_killed.size()==kills_before_abort+2 &&
              std::find(fx_killed.begin(),fx_killed.end(),pool_system)!=fx_killed.end() &&
              std::find(fx_killed.begin(),fx_killed.end(),fire_a_system)!=fx_killed.end(),
              "Recovery kills exactly the retained pool system and fire");
        check(std::find(fx_killed.begin(),fx_killed.end(),foreign_system)==fx_killed.end() &&
              *reinterpret_cast<uint8_t*>(foreign_system+0x51)==0,"A system this backend did not create is never killed");
        check(sa_fx_fire_stop(fire_a) && fx_killed.size()==kills_before_abort+2,"The recovered fire's token is now stale and kills nothing");
        check(sa_fx_fire_stop(fire_b) && sa_fx_release_all() && sa_fx_pending()==0,"Remaining fire and pool release cleanly");
        // Address reuse. The fingerprint (blueprint + Play nonce + primitive array) narrows it; it is not proof.
        check(sa_fx_emit(blood).ok,"Fixture pool system for the reuse scenarios");
        const auto reused=fx_adds.back().system;const auto created_mark=fx_created.size();
        *reinterpret_cast<uint32_t*>(reused+0x64)^=0x00FFu; // same address, same blueprint, a different SA allocation
        const auto replaced=sa_fx_emit(blood);
        check(replaced.ok && fx_created.size()==created_mark+1 && fx_adds.back().system!=reused &&
              std::find(fx_killed.begin(),fx_killed.end(),reused)==fx_killed.end(),
              "A reused address with another Play nonce is neither fed nor killed");
        const auto fed_system=fx_adds.back().system;
        *reinterpret_cast<uint32_t*>(fed_system+0x64)^=0x00FFu;*reinterpret_cast<uint32_t*>(fed_system+0x64)^=0x00FFu; // identical fingerprint
        const auto created_now=fx_created.size();
        check(sa_fx_emit(blood).ok && fx_created.size()==created_now && fx_adds.back().system==fed_system,
              "Documented limit: an allocation with an identical fingerprint is indistinguishable, so this is hardening, not an ABA proof");
        sa_fx_release_all();
        auto* altered=reinterpret_cast<uint8_t*>(native_entity_address(0x4AA440));const auto saved_byte=*altered;*altered^=1;
        check(!sa_fx_profiles_match() && !sa_fx_emit(blood).ok,"A changed AddParticle profile refuses emission");*altered=saved_byte;
    }
    profile(pe,0x48548A,28);
    profile(pe,0x54FFF0,37);profile(pe,0x550050,37);profile(pe,0x54FF90,37);profile(pe,0x48567E,23);profile(pe,0x558E40,33);profile(pe,0x583C70,23);
    {
        auto query=operation(EntityCommand::save_game_finished,{});query.predicate=true;
        auto* active=reinterpret_cast<uint8_t*>(native_entity_address(0xBA67A4));
        auto* saving=reinterpret_cast<uint8_t*>(native_entity_address(0xBA67A7));
        for(auto menu:{uint8_t(0),uint8_t(1)})for(auto save:{uint8_t(0),uint8_t(1)}) {
            *active=menu;*saving=save;
            const auto result=sa_execute_entity_command(query);
            check(result.executed && !result.native_entered && result.condition==(!menu && !save) &&
                  *active==menu && *saving==save,
                  "0990 checks both live SA 03D9 menu states without mutating either owner");
        }
        auto* signature=reinterpret_cast<uint8_t*>(native_entity_address(0x48548A));
        const auto original=*signature;*signature^=1;
        const auto invalid=sa_execute_entity_command(query);
        check(!invalid.executed && !invalid.native_entered,
              "Changed native SA 03D9 handler signature refuses without a fake predicate");
        *signature=original;
        auto malformed=query;malformed.inputs={1};
        check(!sa_execute_entity_command(malformed).executed,
              "0990 consumes zero source operands");
        *active=0;*saving=0;
    }
    profile(pe,0x4552A0,5);profile(pe,0x1564BB0,37);profile(pe,0x4740F6,21);
    profile(pe,0x43D3B0,67);profile(pe,0x43D465,25);profile(pe,0x43D1E0,7);profile(pe,0x6A0050,20);
    profile(pe,0x72A494,5);profile(pe,0x72A4B1,5);profile(pe,0x72A4F5,19);profile(pe,0x52D020,40);
    profile(pe,0x447C90,28);profile(pe,0x447E10,36);profile(pe,0x448BE0,30);profile(pe,0x44B49D,9);profile(pe,0x6D24F0,17);profile(pe,0x6D2593,25);profile(pe,0x481811,14);
    profile(pe,0x56A490,13);profile(pe,0x5674E0,12);profile(pe,0x407C37,13);profile(pe,0x6EB690,17);
    profile(pe,0x608710,17);profile(pe,0x60872F,9);
    {
        auto* bank=reinterpret_cast<void*>(native_entity_address(0xC0B1E8));
        ScriptTaskSnapshot preserved{};preserved[127]={0x5D3,0x1234,0,0x5678,0};
        std::memcpy(bank,preserved.data(),sizeof(preserved));const auto snapshot=sa_script_task_snapshot();
        check(snapshot && *snapshot==preserved && std::memcmp(bank,preserved.data(),sizeof(preserved))==0,"Compiled provider copies all 128 records from owned native data pages without mutations");
        for(uintptr_t address:{uintptr_t(0x608710),uintptr_t(0x60872F)}) {
            auto* byte=reinterpret_cast<uint8_t*>(native_entity_address(address));const auto saved=*byte;*byte^=1;
            check(!sa_script_task_snapshot().has_value() && std::memcmp(bank,preserved.data(),sizeof(preserved))==0,"Modified native table layout refuses snapshot without overwriting another mod");*byte=saved;
        }
        DWORD previous=0,ignored=0;
        auto* page=reinterpret_cast<void*>(native_entity_address(0xC0B000));
        check(VirtualProtect(page,0x1000,PAGE_NOACCESS,&previous)!=0,"Task table fault injection protects only owned test data");
        check(!sa_script_task_snapshot().has_value(),"Unreadable native task bank does not escape as access violation or a false completion");
        check(VirtualProtect(page,0x1000,previous,&ignored)!=0,"Owned task bank protection restored");
    }
    profile(pe,0x421339,7);profile(pe,0x421346,6);profile(pe,0x53FB70,16);profile(pe,0x53F910,10);
    profile(pe,0x4654B0,5);profile(pe,0x156E810,41);profile(pe,0x6AF145,7);profile(pe,0x401860,10);profile(pe,0x6C21C0,24);profile(pe,0x6A4510,10);
    profile(pe,0x50BFF0,7);profile(pe,0x5E873A,7);profile(pe,0x56E210,28);profile(pe,0x43CE90,18);
    profile(pe,0x6B1600,40);profile(pe,0x6B17E5,13);profile(pe,0x6C2709,6);profile(pe,0x6C272C,4);
    profile(pe,0x73C252,14);profile(pe,0x73A31B,10);profile(pe,0x489410,14);profile(pe,0x53FD30,10);
    profile(pe,0x4D3A20,16);profile(pe,0x47EF71,9);profile(pe,0x5E4ACA,16);
    profile(pe,0x56F46C,13);
    profile(pe,0x571A00,14);profile(pe,0x571B70,26);
    profile(pe,0x54D570,19);profile(pe,0x54D585,16);profile(pe,0x54D61F,6);
    profile(pe,0x6A2390,17);profile(pe,0x6A23D9,20);
    profile(pe,0x432CB8,30);profile(pe,0x432D31,6);profile(pe,0x432D8B,1);profile(pe,0x432D74,4);
    profile(pe,0x5E7CB0,21);profile(pe,0x401697,11);profile(pe,0x5E7CCE,16);
    profile(pe,0x6A7ADA,23);profile(pe,0x6C2608,12);profile(pe,0x6C268A,10);
    profile(pe,0x6B3C22,16);profile(pe,0x6B3D27,6);profile(pe,0x6B1350,17);profile(pe,0x409B1D,11);
    profile(pe,0x6C26A0,19);profile(pe,0x6C26E8,8);profile(pe,0x6C26C5,12);
    profile(pe,0x6C26F0,19);profile(pe,0x6C2728,24);profile(pe,0x6C2703,36);
    profile(pe,0x6A0050,20);profile(pe,0x69E5F0,14);profile(pe,0x48330A,12);
    {
        BigMissionMessage request;request.key="RACEREC";request.duration=UINT32_MAX;
        request.numbers={{INT32_MIN,20000001,INT32_MAX,-1,-1,-1}};
        for(uint16_t style=0;style<7;++style) {
            request.style=style;const auto calls=big_displays;
            const auto result=sa_display_big_message(request);
            check(result.executed && result.native_entered && big_displays==calls+1 && big_received.key==request.key &&
                big_received.duration==UINT32_MAX && big_received.style==style && big_received.numbers==request.numbers,
                "Compiled big-message boundary preserves all signed substitutions, full duration and every valid style");
        }
        for(uint16_t style:{uint16_t(7),uint16_t(65535)}) {
            request.style=style;const auto calls=big_lookups;
            const auto result=sa_display_big_message(request);
            check(!result.executed && !result.native_entered && big_lookups==calls,"Invalid style cannot access the native seven-style array");
        }
        request.style=0;
        for(const auto& key:{std::string{},std::string(9,'X'),std::string("A\0B",3)}) {
            request.key=key;const auto calls=big_lookups;
            check(!sa_display_big_message(request).executed && big_lookups==calls,"Bad text identity rejects before native lookup");
        }
        request.key="RACEREC";big_text_missing=true;const auto calls=big_displays;
        auto result=sa_display_big_message(request);big_text_missing=false;
        check(!result.executed && !result.native_entered && big_displays==calls,"Unmounted GXT text cannot grant a display receipt");
        big_lookup_fault=true;result=sa_display_big_message(request);big_lookup_fault=false;
        check(!result.executed && !result.native_entered && big_displays==calls,"Lookup boundary fault remains before message mutation");
        big_fault=true;result=sa_display_big_message(request);big_fault=false;
        check(!result.executed && result.native_entered && big_displays==calls+1,
            "Failure after real display entry retains its mutation receipt and cannot be replayed");
        *reinterpret_cast<uint8_t*>(native_entity_address(0x48330A))=0;
        const auto lookups=big_lookups;
        result=sa_display_big_message(request);
        check(!result.executed && !result.native_entered && big_lookups==lookups,"Foreign message ABI cannot enter lookup or display");profile(pe,0x48330A,12);
    }
    std::array<uint8_t,0xA18*3> vehicles{};std::array<uint8_t,0x19C*2> objects{};
    std::array<uint8_t,3> vehicle_flags{{7,0x8A,12}};std::array<uint8_t,2> object_flags{{19,0x88}};
    Pool car_pool{vehicles.data(),vehicle_flags.data(),3},object_pool{objects.data(),object_flags.data(),2};
    pointer(0xB74494,&car_pool);pointer(0xB7449C,&object_pool);vehicles[0x36]=vehicles[2*0xA18+0x36]=2;objects[0x36]=4;
    {
        // Native 1487 probe: only owned, non-executable test pages and PE
        // signatures copied as data, never a call to the retail game's code.
        auto* pickups=reinterpret_cast<uint8_t*>(native_entity_address(0x9788C0));
        const std::array<float,4> sphere{{10.f,-20.f,5.f,9.f}};
        check(sa_pickup_model_in_area(sphere)==-1,
              "1487 empty native pickup pool retains source no-match sentinel");
        auto put=[&](size_t slot,uintptr_t object,int16_t x,int16_t y,int16_t z){
            auto* entry=pickups+slot*0x20;
            *reinterpret_cast<uintptr_t*>(entry+4)=object;
            auto* pos=reinterpret_cast<int16_t*>(entry+0x10);
            pos[0]=x;pos[1]=y;pos[2]=z;
            entry[0x1C]=3; // genuine native PICKUP_ONCE
        };
        *reinterpret_cast<int16_t*>(objects.data()+0x22)=346;
        put(4,reinterpret_cast<uintptr_t>(objects.data()),80,-160,40);
        check(sa_pickup_model_in_area(sphere)==346,
              "1487 uses signed compressed XYZ and reads the live object's model field");
        put(4,reinterpret_cast<uintptr_t>(objects.data()),0,0,0);
        const std::array<float,4> rounded_boundary{{1.f,2.f,0.f,std::sqrt(5.f)}};
        check(sa_pickup_model_in_area(rounded_boundary)==-1,
              "1487 preserves original single-precision sqrt then strict compare at rounded sphere boundary");
        put(4,reinterpret_cast<uintptr_t>(objects.data()),80,-160,40);
        put(11,reinterpret_cast<uintptr_t>(objects.data()),80+9*8,-160,40);
        check(sa_pickup_model_in_area(sphere)==346,
              "1487 excludes pickups exactly on its sphere boundary");
        pickups[11*0x20+0x1C]=0;
        object_flags[1]=20;objects[0x19C+0x36]=4;
        *reinterpret_cast<int16_t*>(objects.data()+0x19C+0x22)=349;
        put(619,reinterpret_cast<uintptr_t>(objects.data()+0x19C),80,-160,40);
        check(sa_pickup_model_in_area(sphere)==349,
              "1487 traverses all 620 slots, retaining the last matching weapon model");
        object_flags[1]|=0x80;
        check(!sa_pickup_model_in_area(sphere).has_value(),
              "1487 rejects recycled native object rather than fabricating a weapon");
        object_flags[1]=20;pickups[619*0x20+0x1C]=0;
        check(sa_pickup_model_in_area(sphere)==346,
              "1487 ignores inactive entries with stale object pointers");
        auto* signature=reinterpret_cast<uint8_t*>(native_entity_address(0x1564BB0));
        const auto saved=*signature;*signature^=0x80;
        check(!sa_pickup_model_in_area(sphere).has_value(),
              "Changed native pickup ABI signature refuses read");
        *signature=saved;
        auto nan=sphere;nan[2]=std::numeric_limits<float>::quiet_NaN();
        check(!sa_pickup_model_in_area(nan).has_value(),
              "1487 nonfinite sphere is rejected before touching native memory");
        std::memset(pickups+4*0x20,0,0x20);
        std::memset(pickups+11*0x20,0,0x20);
        std::memset(pickups+619*0x20,0,0x20);
        object_flags[1]=0x88;
    }
    {
    auto collision=operation(EntityCommand::car_collision,{7},1);
    auto* hit=reinterpret_cast<uintptr_t*>(vehicles.data()+0x50C);
    auto answer=sa_execute_entity_command(collision);
    check(answer.executed && !answer.native_entered && answer.values[0]==-1,"No last damage entity is the original negative sentinel");
    *hit=reinterpret_cast<uintptr_t>(vehicles.data()+2*0xA18);native_register_reference(*hit,hit);
    const auto before_collision=vehicles;const auto cleanups_before=reference_cleanups;
    answer=sa_execute_entity_command(collision);
    check(answer.executed && answer.native_entered && answer.values[0]==0x20C && !*hit && reference_cleanups==cleanups_before+1,
        "Actual live damage target converts to its current vehicle generation and consumes its reference");
    for(size_t i=0;i<vehicles.size();++i)if(i<0x50C || i>=0x510)check(vehicles[i]==before_collision[i],"Damage target query leaves damage intensity, weapon, physical contacts and car status unchanged");
    check(sa_execute_entity_command(collision).values[0]==-1,"Repeated consumed collision cannot replay an old vehicle target");
    *hit=reinterpret_cast<uintptr_t>(objects.data());native_register_reference(*hit,hit);
    answer=sa_execute_entity_command(collision);
    check(answer.executed && answer.values[0]==-1 && !*hit,"A valid object target is released without constructing a fake vehicle handle");
    for(uintptr_t bad:{uintptr_t(0xDEADBEEF),reinterpret_cast<uintptr_t>(vehicles.data()+1),reinterpret_cast<uintptr_t>(vehicles.data()+0xA18)}) {
        *hit=bad;const auto unchanged=vehicles;answer=sa_execute_entity_command(collision);
        check(!answer.executed && !answer.native_entered && vehicles==unchanged,"Invalid/unallocated/misaligned collision pointers reject before dereferencing target or clearing state");
    }
    *hit=0;collision.inputs[0]=8;
    check(!sa_execute_entity_command(collision).native_entered && !sa_execute_entity_command(collision).executed,"Collision query rejects recycled source car");collision.inputs[0]=7;
    auto wreck=operation(EntityCommand::car_wreck,{7,441});
    *reinterpret_cast<int16_t*>(vehicles.data()+0x22)=445;
    vehicles[0x36]=0x9A;vehicles[0x43]=0x84;vehicles[0x428]=0xFD;vehicles[0x42D]=0xFF;vehicles[0x4A8]=0xEB;
    *reinterpret_cast<float*>(vehicles.data()+0x4C0)=876;*reinterpret_cast<uint16_t*>(vehicles.data()+0x4DE)=123;
    for(size_t i=0;i<0x18;++i)vehicles[0x5A0+i]=uint8_t(0x80+i);
    const auto before_wreck=vehicles;
    answer=sa_execute_entity_command(wreck);
    check(answer.executed && answer.native_entered && wreck_seen==reinterpret_cast<uintptr_t>(vehicles.data()) &&
        wreck_visuals==std::vector<int32_t>{5,6,100,101,102,103,104,105},"Car wreck processes real bumper and door visuals in source order");
    check(vehicles[0x36]==0x2A && vehicles[0x43]==0xA4 && vehicles[0x428]==0xAD && vehicles[0x42D]==0x7F && vehicles[0x4A8]==0xE8 &&
        *reinterpret_cast<float*>(vehicles.data()+0x4C0)==0 && *reinterpret_cast<uint16_t*>(vehicles.data()+0x4DE)==0,
        "Wrecked status/scorch, engine/lights/siren/bomb and health use bounded field masks");
    check(vehicles[0x5A4]==250 && vehicles[0x5A5]==2 && vehicles[0x5A6]==before_wreck[0x5A6] &&
        vehicles[0x5A7]==before_wreck[0x5A7] && vehicles[0x5A8]==before_wreck[0x5A8] &&
        *reinterpret_cast<uint32_t*>(vehicles.data()+0x5B0)==0 && *reinterpret_cast<uint32_t*>(vehicles.data()+0x5B4)==0,
        "Retail damage contract keeps the other wheels, clears light/panel states and avoids native randomized wheel selection");
    for(size_t i=0;i<6;++i)check(vehicles[0x5A9+i]==4,"All source missing doors translate to native NOTPRESENT, not swinging damage");
    for(size_t i=0;i<vehicles.size();++i)if(i!=0x36 && i!=0x43 && i!=0x428 && i!=0x42D && i!=0x4A8 &&
        !(i>=0x4C0 && i<0x4C4) && !(i>=0x4DE && i<0x4E0) && i!=0x5A4 && i!=0x5A5 &&
        !(i>=0x5A9 && i<0x5AF) && !(i>=0x5B0 && i<0x5B8))check(vehicles[i]==before_wreck[i],
            "Wreck preserves render culling, transforms, occupants, time of explosion, velocity and unrelated physics/vehicle flags");
    vehicles=before_wreck;*reinterpret_cast<int16_t*>(vehicles.data()+0x22)=441;wreck_visuals.clear();
    check(sa_execute_entity_command(wreck).executed && wreck_visuals.empty(),"Original RC Bandit skips visual removal using its translated model identity");
    vehicles=before_wreck;*reinterpret_cast<int32_t*>(vehicles.data()+0x590)=9;
    const auto bike_before=vehicles;
    check(!sa_execute_entity_command(wreck).executed && vehicles==bike_before && wreck_visuals.empty(),"A bike cannot access automobile damage fields");
    vehicles=before_wreck;*reinterpret_cast<uintptr_t*>(vehicles.data()+0x648+10*4)=1;
    const auto missing_model=vehicles;
    check(!sa_execute_entity_command(wreck).native_entered && vehicles==missing_model,"A present native door node requires live model structure, handling and clump before any write");
    vehicles=before_wreck;wreck_fault=true;answer=sa_execute_entity_command(wreck);
    check(!answer.executed && answer.native_entered && vehicles[0x36]==0x2A,"Fault in a visual boundary after damage is entered and cannot be replayed as preflight rejection");
    wreck_fault=false;wreck_visuals.clear();vehicles=before_wreck;
    *reinterpret_cast<uint8_t*>(native_entity_address(0x6C2608))=0x90;
    check(!sa_execute_entity_command(wreck).native_entered && vehicles==before_wreck,"Foreign damage-manager layout rejects before source writes");profile(pe,0x6C2608,12);
    *reinterpret_cast<uint8_t*>(native_entity_address(0x6C2728))=0;
    check(!sa_execute_entity_command(wreck).executed && vehicles==before_wreck,"Changed door node table cannot bypass visual dependency guards");profile(pe,0x6C2728,24);
    vehicles.fill(0);vehicles[0x36]=vehicles[2*0xA18+0x36]=2;
    }
    auto speed=operation(EntityCommand::car_move_speed,{7,entity_bits(100),entity_bits(-50),entity_bits(25)});
    auto moved=sa_execute_entity_command(speed);std::array<float,3> actual{};std::memcpy(actual.data(),vehicles.data()+0x44,12);
    check(moved.executed && moved.native_entered && actual==std::array<float,3>{{2,-1,.5f}},"Compiled backend updates vehicle velocity with source units");
    for(int32_t handle:{-1,8,0x18A,0x20B,0x307,0x7FFFFF07}) {
        const auto before=vehicles;speed.inputs[0]=handle;const auto rejected=sa_execute_entity_command(speed);
        check(!rejected.executed && !rejected.native_entered && vehicles==before,"Invalid/free/recycled/out-of-bounds handles reject before entity writes");
    }
    speed.inputs[0]=7;vehicles[0x36]=3;check(!sa_execute_entity_command(speed).executed,"A ped-shaped slot cannot be accepted as a vehicle");vehicles[0x36]=2;
    *reinterpret_cast<uint8_t*>(native_entity_address(0x54FFF0))=0x90;check(!sa_execute_entity_command(speed).native_entered,"A changed pool profile cannot grant layout access");profile(pe,0x54FFF0,37);
    auto turn=operation(EntityCommand::object_turn_speed,{19,entity_bits(-25),entity_bits(50),entity_bits(100)});
    check(sa_execute_entity_command(turn).executed,"Object physics uses the verified object pool stride");std::memcpy(actual.data(),objects.data()+0x50,12);
    check(actual==std::array<float,3>{{-.5f,1,2}},"Compiled object velocity retains all components");
    objects[0x1D]=0xD5;check(sa_execute_entity_command(operation(EntityCommand::object_render_damaged,{19})).executed && objects[0x1D]==0xD7,"Damaged flag preserves neighboring render flags");
    check(sa_execute_entity_command(operation(EntityCommand::car_alarm,{7})).executed && *reinterpret_cast<uint16_t*>(vehicles.data()+0x45C)==0xFFFF,"Native alarm field is two bytes");
    for(auto type:{0,5,9,10,11}) {
        *reinterpret_cast<int32_t*>(vehicles.data()+0x590)=type;
        for(auto action:{EntityCommand::car_is_boat,EntityCommand::car_is_bike,EntityCommand::car_being_jacked}) {
            vehicles[0x42A]=0xF7;auto call=operation(action,{7});call.predicate=true;
            const auto before=vehicles;const auto answer=sa_execute_entity_command(call);
            const bool expected=action==EntityCommand::car_is_boat?type==5:action==EntityCommand::car_is_bike?(type==9 || type==10):false;
            check(answer.executed && !answer.native_entered && answer.condition==expected && vehicles==before,"Actual predicate reads live vehicle type/jack flag without mutating vehicle");
            if(action==EntityCommand::car_being_jacked){vehicles[0x42A]|=8;check(sa_execute_entity_command(call).condition,"Carjacking query sees native jack flag, not fade/damage flags");}
            call.inputs[0]=8;check(!sa_execute_entity_command(call).executed,"Vehicle predicate rejects a stale generation without inventing false success");
        }
    }
    *reinterpret_cast<int32_t*>(vehicles.data()+0x590)=5;vehicles[0x5AC]=0xFE;
    auto boat=operation(EntityCommand::boat_in_water,{7});boat.predicate=true;
    check(sa_execute_entity_command(boat).executed && !sa_execute_entity_command(boat).condition,"Boat not on water reads actual boat flag");
    vehicles[0x5AC]|=1;check(sa_execute_entity_command(boat).condition,"Boat water state does not use vehicle drowning or engine flags");
    *reinterpret_cast<int32_t*>(vehicles.data()+0x590)=0;
    check(!sa_execute_entity_command(boat).executed,"Boat query rejects another class before reading subclass storage");
    auto rotor=operation(EntityCommand::heli_rotor_full_speed,{7});const auto before_car=vehicles;
    const auto nonheli=sa_execute_entity_command(rotor);
    check(nonheli.executed && !nonheli.native_entered && vehicles==before_car,"Source rotor command leaves non-heli vehicles untouched");
    *reinterpret_cast<int32_t*>(vehicles.data()+0x590)=3;*reinterpret_cast<int32_t*>(vehicles.data()+0x594)=4;
    const auto inconsistent=vehicles;
    const auto wrong_layout=sa_execute_entity_command(rotor);
    check(!wrong_layout.executed && !wrong_layout.native_entered && vehicles==inconsistent,"Inconsistent helicopter subclass rejects before rotor write");
    *reinterpret_cast<int32_t*>(vehicles.data()+0x594)=3;
    const auto spun=sa_execute_entity_command(rotor);
    check(spun.executed && spun.native_entered && *reinterpret_cast<float*>(vehicles.data()+0x84C)==.22f,"Rotor speed uses the actual native automobile rotor field");
    *reinterpret_cast<uint8_t*>(native_entity_address(0x4740F6))=0x90;
    check(!sa_execute_entity_command(rotor).executed,"Changed rotor layout rejects before mutation");profile(pe,0x4740F6,21);
    auto* pickups=reinterpret_cast<uint8_t*>(native_entity_address(0x9788C0));
    for(size_t slot:{size_t(0),size_t(619)}) {
        auto* pickup=pickups+slot*0x20;std::memset(pickup,0xCD,0x20);uint16_t generation=0xFFFE;
        std::memcpy(pickup+0x1A,&generation,2);*reinterpret_cast<float*>(pickup)=-123.75f;
        const auto handle=int32_t((uint32_t(generation)<<16)|uint32_t(slot));
        const auto got=sa_execute_entity_command(operation(EntityCommand::get_pickup_value,{handle},1));
        check(got.executed && !got.native_entered && got.values[0]==-123,"Source pickup GET truncates float revenue towards zero");
        std::array<uint8_t,0x20> before{};std::memcpy(before.data(),pickup,0x20);
        const auto set=sa_execute_entity_command(operation(EntityCommand::set_pickup_value,{handle,456}));
        check(set.executed && set.native_entered && *reinterpret_cast<float*>(pickup)==456.f && std::memcmp(pickup+4,before.data()+4,0x1C)==0,"Pickup SET changes revenue only, preserving ammo/object/type/reference");
        const auto recycled=sa_execute_entity_command(operation(EntityCommand::get_pickup_value,{handle^0x10000},1));
        check(recycled.executed && !recycled.native_entered && recycled.values[0]==0,"Original invalid pickup GET returns zero without reading recycled revenue");
        const auto ignored=sa_execute_entity_command(operation(EntityCommand::set_pickup_value,{handle^0x10000,999}));
        check(ignored.executed && !ignored.native_entered && *reinterpret_cast<float*>(pickup)==456.f,"Original invalid SET leaves recycled pickup untouched");
        for(float bad:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),2147483648.f}) {
            *reinterpret_cast<float*>(pickup)=bad;const auto invalid=sa_execute_entity_command(operation(EntityCommand::get_pickup_value,{handle},1));
            check(!invalid.executed && !invalid.native_entered,"Non-finite/out-of-range pickup revenue cannot invoke undefined integer conversion");
        }
    }
    for(int32_t handle:{-1,620,0x7FFF}) {
        const auto absent=sa_execute_entity_command(operation(EntityCommand::get_pickup_value,{handle},1));
        check(absent.executed && !absent.native_entered && absent.values[0]==0,"Pickup low-word index is bounded before storage access");
    }
    *reinterpret_cast<uint8_t*>(native_entity_address(0x4552A0))=0x90;
    check(!sa_execute_entity_command(operation(EntityCommand::get_pickup_value,{-1},1)).executed,"Foreign pickup table profile cannot grant access");profile(pe,0x4552A0,5);
    auto* traces=reinterpret_cast<uint8_t*>(native_entity_address(0xBA86F0));
    for(size_t slot:{size_t(0),size_t(174)}) {
        auto* trace=traces+slot*0x28;uint16_t generation=0xFFFE;std::memcpy(trace+0x14,&generation,2);trace[0x25]=0xFE;
        auto call=operation(EntityCommand::radar_brightness,{int32_t((uint32_t(generation)<<16)|uint32_t(slot)),0});
        check(sa_execute_entity_command(call).executed && trace[0x25]==0xFF,"Blip accepts signed handles with current generation and sets only bright bit");
        call.inputs[1]=1;check(sa_execute_entity_command(call).executed && trace[0x25]==0xFE,"Source brightness one clears only bright bit");
        call.inputs[0]^=0x10000;const auto ignored=sa_execute_entity_command(call);
        check(ignored.executed && !ignored.native_entered && trace[0x25]==0xFE,"Removed/recycled blip is ignored exactly as source CRadar does");
    }
    for(int32_t handle:{-1,175,0x7FFF}) {
        const auto ignored=sa_execute_entity_command(operation(EntityCommand::radar_brightness,{handle,0}));
        check(ignored.executed && !ignored.native_entered,"Out-of-bounds blip never indexes trace storage");
    }
    *reinterpret_cast<uint8_t*>(native_entity_address(0x583C70))=0x90;
    check(!sa_execute_entity_command(operation(EntityCommand::radar_brightness,{-1,0})).executed,"Altered native radar profile cannot grant memory writes");profile(pe,0x583C70,23);
    std::array<float,16> matrix{};matrix[1]=1;matrix[4]=-1;matrix[10]=1;matrix[12]=20;matrix[13]=-10;
    *reinterpret_cast<const void**>(vehicles.data()+0x14)=matrix.data();
    auto orient=sa_execute_entity_command(operation(EntityCommand::get_car_orientation,{7},3));
    check(orient.executed && !orient.native_entered && std::fabs(entity_float(orient.values[2])-90)<.0002f,"Read-only orientation uses actual matrix axes without a write receipt");
    const auto bounds=std::vector<int32_t>{entity_bits(30),entity_bits(0),entity_bits(10),entity_bits(-20)};
    auto found=sa_execute_entity_command(operation(EntityCommand::random_car_unchecked,bounds,1));
    check(found.executed && !found.native_entered && found.values[0]==7,"No-save query honors reversed area and actual generation handle");
    matrix[12]=31;found=sa_execute_entity_command(operation(EntityCommand::random_car_unchecked,bounds,1));
    check(found.executed && found.values[0]==-1,"Absent actual vehicle returns source sentinel without ownership");
    auto* ped=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));check(ped!=nullptr,"Owned ped page must allocate");
    ped[0x36]=3;*reinterpret_cast<const void**>(ped+0x480)=ped+0x600;uint8_t ped_flag=27;Pool ped_pool{ped,&ped_flag,1};pointer(0xB74490,&ped_pool);pointer(0xB7CD98,ped);
    {
        profile(pe,0x564A20,11);profile(pe,0x569660,11);
        const auto saved=std::vector<uint8_t>(vehicles.begin(),vehicles.end());
        const auto saved_flags=vehicle_flags;
        vehicle_flags[0]=7;vehicles[0x36]=2;
        *reinterpret_cast<uintptr_t*>(vehicles.data()+0x460)=reinterpret_cast<uintptr_t>(ped);
        *reinterpret_cast<uintptr_t*>(vehicles.data()+0x464)=reinterpret_cast<uintptr_t>(ped);
        *reinterpret_cast<uintptr_t*>(vehicles.data()+0x468)=1; // not part of NumPassengers
        vehicles[0x484]=1;ped[0x484]=1;*reinterpret_cast<int32_t*>(ped+0x598)=4;
        collective_entities={reinterpret_cast<uintptr_t>(vehicles.data()),reinterpret_cast<uintptr_t>(ped)};
        auto selected=sa_collective_candidates(10,20,30);
        check(selected && selected->size()==3 && (*selected)[0].actor.handle==27 && (*selected)[2].actor.handle==27,"Production collective query expands driver/current passengers and preserves entity order/duplicates");
        check((*selected)[0].type==4 && (*selected)[0].created_by==1,"Production collective snapshot reads actual ped type and creation owner");
        vehicles[0x484]=2;check(!sa_collective_candidates(10,20,30),"Invalid passenger identity cannot become an empty successful group");vehicles[0x484]=1;
        collective_query_valid=false;check(!sa_collective_candidates(10,20,30),"Oversized native result contained");collective_query_valid=true;
        auto* signature=reinterpret_cast<uint8_t*>(native_entity_address(0x564A20));*signature^=1;
        const auto before=collective_queries;check(!sa_collective_candidates(10,20,30) && collective_queries==before,"Modified native world query refuses before entry");*signature^=1;
        std::copy(saved.begin(),saved.end(),vehicles.begin());vehicle_flags=saved_flags;ped[0x484]=0;*reinterpret_cast<int32_t*>(ped+0x598)=0;
    }
    {
        profile(pe,0x5E3960,38);profile(pe,0x4012D0,11);
        const auto preserved=std::vector<uint8_t>(ped,ped+0x7C4);
        ObjectiveActor actor{27,reinterpret_cast<uintptr_t>(ped)};
        {
            actor_reference_mode=true;*reinterpret_cast<uintptr_t*>(ped+0x24)=0;
            uintptr_t weak=0;
            check(sa_register_actor_reference(actor,&weak) && sa_actor_reference_live(actor,&weak),"Actual reference list admits actor lifetime, independent of native task presence");
            // Native ResolveReferences clears the registered cell on deletion.
            weak=0;*reinterpret_cast<uintptr_t*>(ped+0x24)=0;
            check(!sa_actor_reference_live(actor,&weak),"Identical handle/address after native deletion cannot resurrect a cleared weak cell");
            check(sa_release_actor_reference(&weak),"Native-retired weak storage can be released");
            actor_reference_exhausted=true;
            check(!sa_register_actor_reference(actor,&weak) && weak==actor.native_ped,"Void native registration with exhausted pool cannot grant lifetime");
            check(sa_release_actor_reference(&weak) && weak==0,"Unregistered cell is explicitly cleaned before freeing storage");
            actor_reference_exhausted=false;
            check(sa_register_actor_reference(actor,&weak),"New constructor can register a fresh cell");
            auto* cleanup_profile=reinterpret_cast<uint8_t*>(native_entity_address(0x571A00));
            const auto saved_byte=*cleanup_profile;*cleanup_profile^=1;
            check(!sa_release_actor_reference(&weak) && weak==actor.native_ped,"Changed native cleanup profile requires retaining registered storage");
            *cleanup_profile=saved_byte;
            check(sa_release_actor_reference(&weak) && !*reinterpret_cast<uintptr_t*>(ped+0x24),"Confirmed native unregister removes cell before release");
            actor_reference_mode=false;entity_references.clear();
        }
        std::array<float,3> position{{1,2,3}};
        std::memcpy(ped+4,position.data(),12);*reinterpret_cast<uintptr_t*>(ped+0x14)=0;
        *reinterpret_cast<float*>(ped+0x540)=100;*reinterpret_cast<int32_t*>(ped+0x530)=0;ped[0x46D]=0;
        const auto before=std::vector<uint8_t>(ped,ped+0x7C4);
        auto state=sa_route_actor_snapshot(actor);
        check(state && state->position==position && state->controllable && state->on_foot && !std::memcmp(ped,before.data(),before.size()),"Read-only route pose/control snapshot preserves ped/export bytes");
        std::array<uint8_t,0x40> matrix{};position={{4,5,6}};std::memcpy(matrix.data()+0x30,position.data(),12);
        *reinterpret_cast<uintptr_t*>(ped+0x14)=reinterpret_cast<uintptr_t>(matrix.data());state=sa_route_actor_snapshot(actor);
        check(state && state->position==position,"Allocated matrix position takes precedence over simple transform");
        for(int32_t value:{54,55,63}) {*reinterpret_cast<int32_t*>(ped+0x530)=value;state=sa_route_actor_snapshot(actor);check(state && !state->controllable,"Reviewed dying/dead/arrested native states suspend route control");}
        *reinterpret_cast<int32_t*>(ped+0x530)=0;
        for(uint8_t flags:{uint8_t(2),uint8_t(4)}) {ped[0x46D]=flags;state=sa_route_actor_snapshot(actor);check(state && !state->controllable && state->on_foot,"Air/landing flags suspend native control without inventing vehicle state");}
        ped[0x46D]=1;state=sa_route_actor_snapshot(actor);check(state && state->controllable && !state->on_foot,"Native control and source route on-foot gate are independent");ped[0x46D]=0;
        *reinterpret_cast<float*>(ped+0x540)=0;state=sa_route_actor_snapshot(actor);check(state && !state->controllable,"Source zero-health control gate preserved");
        *reinterpret_cast<float*>(ped+0x540)=std::numeric_limits<float>::infinity();check(!sa_route_actor_snapshot(actor),"Nonfinite health is unavailable rather than invented control");*reinterpret_cast<float*>(ped+0x540)=100;
        position[0]=std::numeric_limits<float>::infinity();std::memcpy(matrix.data()+0x30,position.data(),12);check(!sa_route_actor_snapshot(actor),"Nonfinite position never participates in arrival");position[0]=4;std::memcpy(matrix.data()+0x30,position.data(),12);
        *reinterpret_cast<uintptr_t*>(ped+0x14)=1;check(!sa_route_actor_snapshot(actor),"Dangling matrix AV contained by read-only boundary");*reinterpret_cast<uintptr_t*>(ped+0x14)=reinterpret_cast<uintptr_t>(matrix.data());
        ++actor.native_ped;check(!sa_route_actor_snapshot(actor),"Actor pointer mismatch cannot reuse another character snapshot");--actor.native_ped;
        ped_flag|=0x80;check(!sa_route_actor_snapshot(actor),"Freed ped generation refuses snapshot");ped_flag=27;
        for(uintptr_t address:{uintptr_t(0x5E3960),uintptr_t(0x4012D0)}) {auto* byte=reinterpret_cast<uint8_t*>(native_entity_address(address));const auto old=*byte;*byte^=1;check(!sa_route_actor_snapshot(actor),"Foreign control/trampoline ABI refused without calling it");*byte=old;}
        std::memcpy(ped,preserved.data(),preserved.size());
    }
    {
        // Route vehicle facts and weak references use only owned mapped pages;
        // PE bytes below are copied as data and no retail routine is executed.
        profile(pe,0x5E2A61,43);profile(pe,0x6B0C6E,38);
        const auto saved_vehicle=vehicles;const auto saved_vehicle_flags=vehicle_flags;
        const auto saved_ped=std::vector<uint8_t>(ped,ped+0x7C4);const auto saved_ped_flag=ped_flag;
        const auto saved_entity_references=entity_references;const auto saved_reference_mode=actor_reference_mode;
        std::array<uintptr_t,16> route_effect_intelligence{};
        *reinterpret_cast<uintptr_t*>(ped+0x47C)=reinterpret_cast<uintptr_t>(route_effect_intelligence.data());
        vehicles.fill(0);vehicle_flags[0]=7;vehicles[0x36]=uint8_t(2|(11<<3));
        *reinterpret_cast<int32_t*>(vehicles.data()+0x590)=0;
        *reinterpret_cast<float*>(vehicles.data()+0x4C0)=100;
        const float velocity[3]{0.003f,0.004f,0.0f};std::memcpy(vehicles.data()+0x44,velocity,sizeof(velocity));
        *reinterpret_cast<uint8_t*>(vehicles.data()+0x484)=0;*reinterpret_cast<uint8_t*>(vehicles.data()+0x488)=8;
        *reinterpret_cast<int32_t*>(ped+0x530)=0;*reinterpret_cast<int32_t*>(ped+0x598)=6;
        *reinterpret_cast<float*>(ped+0x540)=100;ped[0x46D]=0;ped[0x484]=1;
        *reinterpret_cast<uintptr_t*>(ped+0x14)=0;*reinterpret_cast<uintptr_t*>(ped+0x58C)=0;
        const ObjectiveActor actor{27,reinterpret_cast<uintptr_t>(ped)};
        vehicles[0x486]=0x0B;
        const auto vehicle=sa_route_vehicle_snapshot(7);
        check(vehicle && vehicle->getting_in_flags==0x0B,"Passenger fallback reads actual native getting-in door flags");
        auto lookup=sa_route_vehicle_lookup(7);
        check(lookup.status==VehicleLookupStatus::present && lookup.snapshot &&
              lookup.snapshot->entity==reinterpret_cast<uintptr_t>(vehicles.data()) &&
              lookup.snapshot->generation==7,
              "Vehicle identity lookup reports present only with a complete live snapshot");
        check(sa_route_vehicle_lookup(0x0107).status==VehicleLookupStatus::absent,
              "A free native vehicle pool slot is proven absent");
        vehicle_flags[0]=8;
        check(sa_route_vehicle_lookup(7).status==VehicleLookupStatus::absent,
              "A vehicle slot with a different generation is proven absent");
        vehicle_flags[0]=0x87;
        check(sa_route_vehicle_lookup(7).status==VehicleLookupStatus::absent,
              "A free flag with the old generation remains absent");
        vehicle_flags[0]=7;
        auto* vehicle_profile_byte=reinterpret_cast<uint8_t*>(native_entity_address(0x54FFF0));
        const auto saved_vehicle_profile_byte=*vehicle_profile_byte;*vehicle_profile_byte^=1;
        lookup=sa_route_vehicle_lookup(7);
        *vehicle_profile_byte=saved_vehicle_profile_byte;
        check(lookup.status==VehicleLookupStatus::unavailable && !lookup.snapshot,
              "An unrecognized native pool profile remains unavailable rather than deleting identity");
        const auto saved_vehicle_x=*reinterpret_cast<float*>(vehicles.data()+4);
        *reinterpret_cast<float*>(vehicles.data()+4)=std::numeric_limits<float>::quiet_NaN();
        lookup=sa_route_vehicle_lookup(7);
        *reinterpret_cast<float*>(vehicles.data()+4)=saved_vehicle_x;
        check(lookup.status==VehicleLookupStatus::unavailable && !lookup.snapshot,
              "A live slot with unusable NaN snapshot data remains unavailable rather than absent");
        bool conversion_allowed=false;
        const auto conversion_authorized=[](void* context){return *static_cast<bool*>(context);};
        const auto prior_vehicle_flags=vehicles[0x42A];vehicles[0x42A]=0xED;
        check(!sa_route_clear_car_jacked(*vehicle,conversion_authorized,&conversion_allowed) && vehicles[0x42A]==0xED,
              "Unowned driver conversion cannot mutate vehicle car-jacked flag");
        conversion_allowed=true;
        check(sa_route_clear_car_jacked(*vehicle,conversion_authorized,&conversion_allowed) && vehicles[0x42A]==0xE5,
              "Accepted source passenger conversion clears only CVehicle car-jacked bit");
        vehicles[0x42A]=prior_vehicle_flags;
        for(const auto entry: {std::pair{0,false},std::pair{57,true},std::pair{59,true},std::pair{58,false}}) {
            *reinterpret_cast<int32_t*>(ped+0x530)=entry.first;
            const auto entering=sa_route_actor_vehicle_snapshot(actor);
            check(entering && entering->source_entering_car==std::optional<bool>{entry.second},
                  "Passenger fallback translates native state through source enum before EnteringCar");
        }
        *reinterpret_cast<int32_t*>(ped+0x530)=0;
        check(vehicle && vehicle->speed_magnitude_sq_2d &&
              *vehicle->speed_magnitude_sq_2d==0.003f*0.003f+0.004f*0.004f &&
              vehicle->speed_magnitude_sq_3d &&
              *vehicle->speed_magnitude_sq_3d==0.003f*0.003f+0.004f*0.004f &&
              vehicle->player_disabled==std::optional<bool>{true},
              "Vehicle snapshot preserves horizontal/3D speed squared and mapped STATUS_PLAYER_DISABLED fact");
        const auto actor_facts=sa_route_actor_vehicle_snapshot(actor);
        check(actor_facts && !actor_facts->source_created_by && !actor_facts->mission_character,
              "Native snapshot never fabricates original constructor provenance or source mission flag");
        check(actor_facts && actor_facts->native_type==6,
              "Route actor snapshot exposes the same raw native ped type used by the reviewed cop snapshot");

        const auto vehicle_before_exit_facts=vehicles;
        std::array<float,16> exit_matrix{};
        const auto read_exit_fact=[&](int32_t type,std::optional<float> up_z,
                const std::array<float,3>& move,const std::array<float,3>& turn) {
            *reinterpret_cast<int32_t*>(vehicles.data()+0x590)=type;
            std::memcpy(vehicles.data()+0x44,move.data(),sizeof(move));
            std::memcpy(vehicles.data()+0x50,turn.data(),sizeof(turn));
            if(up_z) {
                exit_matrix.fill(0.0f);
                exit_matrix[10]=*up_z;
                *reinterpret_cast<uintptr_t*>(vehicles.data()+0x14)=reinterpret_cast<uintptr_t>(exit_matrix.data());
            } else {
                *reinterpret_cast<uintptr_t*>(vehicles.data()+0x14)=0;
            }
            return sa_route_vehicle_snapshot(7);
        };
        const std::array<float,3> still_move{{0,0,0}},still_turn{{0,0,0}};
        const auto missing_exit_matrix=read_exit_fact(0,{},still_move,still_turn);
        check(missing_exit_matrix && !missing_exit_matrix->source_can_ped_exit_car,
              "CanPedExitCar source fact stays unknown when its required vehicle matrix is absent");
        const std::array<float,3> turn_at_limit{{0.01f,0,0}};
        const auto aligned_positive_edge=read_exit_fact(0,0.1f,still_move,turn_at_limit);
        const auto aligned_negative_edge=read_exit_fact(0,-0.1f,still_move,turn_at_limit);
        check(aligned_positive_edge && aligned_positive_edge->source_can_ped_exit_car==std::optional<bool>{false} &&
              aligned_negative_edge && aligned_negative_edge->source_can_ped_exit_car==std::optional<bool>{false},
              "Exact up.z +/-0.1 stays aligned and exact 0.01 turn is rejected on that path");
        const auto tilted_positive_edge=read_exit_fact(0,std::nextafter(0.1f,1.0f),still_move,turn_at_limit);
        const auto tilted_negative_edge=read_exit_fact(0,std::nextafter(-0.1f,-1.0f),still_move,turn_at_limit);
        check(tilted_positive_edge && tilted_positive_edge->source_can_ped_exit_car==std::optional<bool>{true} &&
              tilted_negative_edge && tilted_negative_edge->source_can_ped_exit_car==std::optional<bool>{true},
              "Just beyond either up.z edge uses the tilted path and accepts exact 0.01 turn");
        const std::array<float,3> speed_at_limit{{0.05f,std::nextafter(0.05f,0.0f),0}};
        const auto aligned_speed_edge=read_exit_fact(0,0.0f,speed_at_limit,still_turn);
        const auto tilted_speed_edge=read_exit_fact(0,0.2f,speed_at_limit,still_turn);
        check(aligned_speed_edge && aligned_speed_edge->speed_magnitude_sq_3d==std::optional<float>{0.005f} &&
              aligned_speed_edge->source_can_ped_exit_car==std::optional<bool>{false} &&
              tilted_speed_edge && tilted_speed_edge->speed_magnitude_sq_3d==std::optional<float>{0.005f} &&
              tilted_speed_edge->source_can_ped_exit_car==std::optional<bool>{true},
              "Exact 0.005 speed squared is inclusive rejection when aligned and allowed when tilted");
        const std::array<float,3> vertical_spin{{0,0,0.0101f}};
        const auto spinning=read_exit_fact(0,0.2f,still_move,vertical_spin);
        check(spinning && spinning->source_can_ped_exit_car==std::optional<bool>{false},
              "CanPedExitCar includes vertical turn speed in the tilted-path threshold");
        const float nonfinite=std::numeric_limits<float>::infinity();
        const std::array<float,3> unavailable_speed{{nonfinite,nonfinite,nonfinite}};
        const std::array<float,3> unavailable_turn{{nonfinite,nonfinite,nonfinite}};
        const auto sideways_boat=read_exit_fact(5,0.2f,unavailable_speed,unavailable_turn);
        check(sideways_boat && sideways_boat->is_boat && !sideways_boat->speed_magnitude_sq_3d &&
              sideways_boat->source_can_ped_exit_car==std::optional<bool>{true},
              "Tilted boat takes source CanPedExitCar short circuit without speed or turn reads");
        vehicles=vehicle_before_exit_facts;

        // Source ANY_MEANS only stops its current vehicle here; the actor may
        // be a passenger. The backend owns exactly the SDK autopilot byte pair.
        const auto actor_before_stop=std::vector<uint8_t>(ped,ped+0x7C4);
        const auto vehicle_before_stop=vehicles;
        ped[0x46D]|=1;
        *reinterpret_cast<uintptr_t*>(ped+0x58C)=reinterpret_cast<uintptr_t>(vehicles.data());
        *reinterpret_cast<uintptr_t*>(vehicles.data()+0x464)=reinterpret_cast<uintptr_t>(ped);
        vehicles[0x484]=1;vehicles[0x488]=8;vehicles[0x486]=0;
        vehicles[0x3BA]=21;vehicles[0x3D0]=73;
        const auto stop_actor=sa_route_actor_vehicle_snapshot(actor);
        const auto stop_vehicle=sa_route_vehicle_snapshot(7);
        RouteStopAuthorizationFixture stop_authorization;
        check(stop_actor && stop_vehicle && stop_actor->in_vehicle &&
              stop_actor->current_vehicle_entity==stop_vehicle->entity && stop_vehicle->driver_handle==-1 &&
              stop_vehicle->passenger_handles[0]==27,
              "Kill-exit stop accepts a live passenger and matching current-vehicle identity");
        const float vertical_velocity[3]{0,0,0.02f};
        std::memcpy(vehicles.data()+0x44,vertical_velocity,sizeof(vertical_velocity));
        const auto vertical_boundary_vehicle=sa_route_vehicle_snapshot(7);
        check(vertical_boundary_vehicle && vertical_boundary_vehicle->speed_magnitude_sq_3d &&
              *vertical_boundary_vehicle->speed_magnitude_sq_3d==0.02f*0.02f,
              "Three-dimensional source speed includes vertical vehicle movement at the exact threshold");
        const auto vertical_boundary=sa_route_stop_vehicle_for_kill_exit(*stop_actor,*vertical_boundary_vehicle,
            route_stop_authorized,&stop_authorization);
        check(!vertical_boundary.executed && !vertical_boundary.native_entered &&
              vehicles[0x3BA]==21 && vehicles[0x3D0]==73,
              "Exact 0.02 speed boundary refuses both autopilot writes");

        const float slow_velocity[3]{0.003f,0.004f,0};
        std::memcpy(vehicles.data()+0x44,slow_velocity,sizeof(slow_velocity));
        const auto ready_vehicle=sa_route_vehicle_snapshot(7);
        const auto ready_actor=sa_route_actor_vehicle_snapshot(actor);
        check(ready_actor && ready_vehicle && ready_vehicle->speed_magnitude_sq_3d &&
              *ready_vehicle->speed_magnitude_sq_3d<0.02f*0.02f,
              "Source kill-exit stop has a fresh, slow in-vehicle actor snapshot");
        auto stale_actor=*ready_actor;++stale_actor.actor.native_ped;
        const auto stale_stop=sa_route_stop_vehicle_for_kill_exit(stale_actor,*ready_vehicle,
            route_stop_authorized,&stop_authorization);
        check(!stale_stop.executed && !stale_stop.native_entered &&
              vehicles[0x3BA]==21 && vehicles[0x3D0]==73,
              "Recycled actor identity refuses before autopilot mutation");

        auto* vehicle_pool_profile=reinterpret_cast<uint8_t*>(native_entity_address(0x54FFF0));
        const auto saved_pool_profile=*vehicle_pool_profile;*vehicle_pool_profile^=0x80;
        const auto profile_refusal=sa_route_stop_vehicle_for_kill_exit(*ready_actor,*ready_vehicle,
            route_stop_authorized,&stop_authorization);
        *vehicle_pool_profile=saved_pool_profile;
        check(!profile_refusal.executed && !profile_refusal.native_entered &&
              vehicles[0x3BA]==21 && vehicles[0x3D0]==73,
              "Changed native vehicle-pool profile refuses before autopilot mutation");

        RouteStopAuthorizationFixture revoked_after_cruise;revoked_after_cruise.deny_on_call=3;
        const auto partial_stop=sa_route_stop_vehicle_for_kill_exit(*ready_actor,*ready_vehicle,
            route_stop_authorized,&revoked_after_cruise);
        check(!partial_stop.executed && partial_stop.native_entered &&
              vehicles[0x3D0]==0 && vehicles[0x3BA]==21,
              "Authorization loss after the first write reports a terminal partial stop without writing mission");
        vehicles[0x3BA]=21;vehicles[0x3D0]=73;

        const auto before_accepted_stop=vehicles;
        const auto ped_before_accepted_stop=std::vector<uint8_t>(ped,ped+0x7C4);
        stop_authorization={};
        const auto accepted_stop=sa_route_stop_vehicle_for_kill_exit(*ready_actor,*ready_vehicle,
            route_stop_authorized,&stop_authorization);
        check(accepted_stop.executed && accepted_stop.native_entered &&
              vehicles[0x3D0]==0 && vehicles[0x3BA]==0,
              "Accepted source stop writes cruise speed zero then MISSION_NONE");
        for(size_t i=0;i<vehicles.size();++i)
            if(i!=0x3D0 && i!=0x3BA)check(vehicles[i]==before_accepted_stop[i],
                "Kill-exit stop preserves every unrelated native vehicle, autopilot, status and velocity field");
        check(!std::memcmp(ped,ped_before_accepted_stop.data(),ped_before_accepted_stop.size()),
              "Kill-exit autopilot stop does not alter actor identity, occupancy or task fields");

        std::memcpy(ped,actor_before_stop.data(),actor_before_stop.size());
        vehicles=vehicle_before_stop;
        vehicles[0x3D0]=73;vehicles[0x3BA]=21;
        const auto entry_actor=sa_route_actor_vehicle_snapshot(actor);
        const auto entry_car=sa_route_vehicle_snapshot(7);
        check(entry_actor && entry_actor->alive && !entry_actor->in_vehicle &&
              entry_car && entry_car->entity==reinterpret_cast<uintptr_t>(vehicles.data()),
              "Kill-entry stop snapshots a live on-foot actor and the caller's source-owned car identity");
        if(entry_actor && entry_car) {
            auto stale_entry_actor=*entry_actor;++stale_entry_actor.actor.native_ped;
            const auto stale_entry=sa_route_stop_vehicle_for_kill_entry(stale_entry_actor,*entry_car,
                route_stop_authorized,&stop_authorization);
            check(!stale_entry.executed && !stale_entry.native_entered && vehicles[0x3D0]==73,
                  "Recycled on-foot actor identity refuses before the remembered-car cruise write");
            auto stale_entry_car=*entry_car;++stale_entry_car.entity;
            const auto stale_car_entry=sa_route_stop_vehicle_for_kill_entry(*entry_actor,stale_entry_car,
                route_stop_authorized,&stop_authorization);
            check(!stale_car_entry.executed && !stale_car_entry.native_entered && vehicles[0x3D0]==73,
                  "Stale source-owned car identity refuses before the cruise write");
            RouteStopAuthorizationFixture denied_entry;denied_entry.allowed=false;
            const auto refused_entry=sa_route_stop_vehicle_for_kill_entry(*entry_actor,*entry_car,
                route_stop_authorized,&denied_entry);
            check(!refused_entry.executed && !refused_entry.native_entered && vehicles[0x3D0]==73,
                  "Source authorization refusal leaves cruise speed untouched");
            const auto vehicle_before_entry=vehicles;
            const auto ped_before_entry=std::vector<uint8_t>(ped,ped+0x7C4);
            RouteStopAuthorizationFixture revoked_after_entry_cruise;revoked_after_entry_cruise.deny_on_call=3;
            const auto partial_entry=sa_route_stop_vehicle_for_kill_entry(*entry_actor,*entry_car,
                route_stop_authorized,&revoked_after_entry_cruise);
            check(!partial_entry.executed && partial_entry.native_entered && vehicles[0x3D0]==0 &&
                  vehicles[0x3BA]==21,
                  "Authorization loss after the sole cruise write reports an entered terminal result");
            vehicles[0x3D0]=73;
            stop_authorization={};
            const auto accepted_entry=sa_route_stop_vehicle_for_kill_entry(*entry_actor,*entry_car,
                route_stop_authorized,&stop_authorization);
            check(accepted_entry.executed && accepted_entry.native_entered && vehicles[0x3D0]==0 &&
                  vehicles[0x3BA]==21,
                  "Accepted source kill-entry stop writes cruise speed zero and preserves mission");
            for(size_t i=0;i<vehicles.size();++i)
                if(i!=0x3D0)check(vehicles[i]==vehicle_before_entry[i],
                    "Kill-entry stop preserves every unrelated car, autopilot, status and motion field");
            check(!std::memcmp(ped,ped_before_entry.data(),ped_before_entry.size()),
                  "Kill-entry cruise stop does not alter the on-foot actor");
            vehicles=vehicle_before_stop;
        }
        const auto old_bus_flags=ped[0x46F];
        ped[0x46F]=uint8_t(old_bus_flags & ~uint8_t(0x04));
        const auto not_bus_jacked=sa_route_actor_vehicle_snapshot(actor);
        check(not_bus_jacked && not_bus_jacked->bus_jacked==std::optional<bool>{false},
              "Source leave-player fact reads CPed bus-jacked flag clear");
        ped[0x46F]|=0x04;
        const auto bus_jacked=sa_route_actor_vehicle_snapshot(actor);
        check(bus_jacked && bus_jacked->bus_jacked==std::optional<bool>{true} &&
              ped[0x46F]==uint8_t((old_bus_flags & ~uint8_t(0x04)) | 0x04),
              "CPed bus-jacked flag is independent of vehicle car-jacking and snapshot does not mutate it");
        ped[0x46F]=old_bus_flags;
        const auto old_stationary_flags=ped[0x46E];
        ped[0x46E]=uint8_t((old_stationary_flags|0x20u)&~0x40u);
        const auto stay_only=sa_route_actor_vehicle_snapshot(actor);
        check(stay_only && stay_only->kinda_stay_in_same_place==std::optional<bool>{false},
            "Separate stay-in-place bit cannot manufacture original stationary-shooter fact");
        ped[0x46E]|=0x40;
        const auto stationary=sa_route_actor_vehicle_snapshot(actor);
        check(stationary && stationary->kinda_stay_in_same_place==std::optional<bool>{true} &&
              ped[0x46E]==uint8_t(old_stationary_flags|0x60u),
            "Production route snapshot reads stationary flag without altering packed native flags");
        ped[0x46E]=old_stationary_flags;
        profile(pe,0x5E7980,0x60);profile(pe,0x5DEC00,0x60);
        bool route_effect_allowed=true;
        const auto effect_authorized=[](void* context){return *static_cast<bool*>(context);};
        {
            profile(pe,0x5DEF20,24);profile(pe,0x5E1950,22);
            profile(pe,0x4096D5,24);profile(pe,0x624E30,24);profile(pe,0x86D724,28);
            const auto saved_intelligence=route_effect_intelligence;
            route_effect_intelligence[0]=reinterpret_cast<uintptr_t>(ped);
            route_effect_intelligence[12]=reinterpret_cast<uintptr_t>(ped);
            route_effect_intelligence[6]=0;
            route_effect_intelligence[7]=0x12345678;
            const auto look_before=route_clear_look_calls,aim_before=route_clear_aim_calls;
            auto cleared=sa_route_clear_point_gun_at(*actor_facts,effect_authorized,&route_effect_allowed);
            if(!cleared.executed)throw std::runtime_error("ClearPointGunAt fixture: "+cleared.diagnostic);
            check(cleared.executed && cleared.native_entered && route_clear_look_calls==look_before+1 &&
                route_clear_aim_calls==aim_before+1 && route_effect_intelligence[7]==0x12345678,
                "Route setter clears native look/aim and preserves unrelated secondary tasks");
            std::array<uint8_t,0x40> gun{},animation{};
            const auto gun_address=reinterpret_cast<uintptr_t>(gun.data());
            *reinterpret_cast<uintptr_t*>(gun.data())=native_entity_address(0x86D724);
            gun[0x0F]=1;gun[0x10]=2;
            *reinterpret_cast<uintptr_t*>(gun.data()+0x2C)=reinterpret_cast<uintptr_t>(animation.data());
            route_effect_intelligence[6]=gun_address;
            const auto abort_before=route_abort_gun_calls,fade_before=route_fire_fade_calls;
            cleared=sa_route_clear_point_gun_at(*actor_facts,effect_authorized,&route_effect_allowed);
            check(cleared.executed && cleared.native_entered && route_abort_gun_calls==abort_before+1 &&
                route_fire_fade_calls==fade_before+1 && *reinterpret_cast<float*>(animation.data()+0x18)==-4 &&
                (*reinterpret_cast<uint16_t*>(animation.data()+0x2A)&4)!=0 && route_effect_intelligence[7]==0x12345678,
                "Validated native UseGun urgent abort releases FIRE association then fades it at original -4");
            const auto no_effect_look=route_clear_look_calls;
            *reinterpret_cast<uintptr_t*>(gun.data())=native_entity_address(0x86D724)+4;
            cleared=sa_route_clear_point_gun_at(*actor_facts,effect_authorized,&route_effect_allowed);
            check(!cleared.executed && !cleared.native_entered && route_clear_look_calls==no_effect_look,
                "Foreign attack task refuses before any native side effect");
            *reinterpret_cast<uintptr_t*>(gun.data())=native_entity_address(0x86D724);
            gun[0x0F]=gun[0x10]=6;
            cleared=sa_route_clear_point_gun_at(*actor_facts,effect_authorized,&route_effect_allowed);
            check(!cleared.executed && !cleared.native_entered,"Non-fire UseGun command is preserved");
            gun[0x0F]=1;gun[0x10]=2;
            for(int state:{16,22}) {
                *reinterpret_cast<int32_t*>(ped+0x530)=state;
                *reinterpret_cast<uintptr_t*>(gun.data()+0x2C)=reinterpret_cast<uintptr_t>(animation.data());
                route_clear_state_writes.clear();
                const auto state_look_before=route_clear_look_calls,state_aim_before=route_clear_aim_calls;
                const auto state_abort_before=route_abort_gun_calls,state_fade_before=route_fire_fade_calls;
                cleared=sa_route_clear_point_gun_at(*actor_facts,effect_authorized,&route_effect_allowed);
                check(cleared.executed && cleared.native_entered &&
                    *reinterpret_cast<int32_t*>(ped+0x530)==state && route_clear_state_writes.empty() &&
                    route_clear_look_calls==state_look_before+1 && route_clear_aim_calls==state_aim_before+1 &&
                    route_abort_gun_calls==state_abort_before+1 && route_fire_fade_calls==state_fade_before+1 &&
                    route_effect_intelligence[6]==gun_address && route_effect_intelligence[7]==0x12345678 &&
                    !*reinterpret_cast<uintptr_t*>(gun.data()+0x2C) &&
                    (*reinterpret_cast<uint16_t*>(animation.data()+0x2A)&4)!=0,
                    "On-foot AIM_GUN/ATTACK preserves native state while vanilla UseGun releases its validated animation");
            }
            *reinterpret_cast<int32_t*>(ped+0x530)=0;
            profile(pe,0x5E4500,18);profile(pe,0x5E456D,11);
            const auto saved_seated_flags=ped[0x46D];
            const auto saved_seated_vehicle=*reinterpret_cast<uintptr_t*>(ped+0x58C);
            ped[0x46D]|=1;*reinterpret_cast<uintptr_t*>(ped+0x58C)=reinterpret_cast<uintptr_t>(vehicles.data());
            for(int state:{16,22}) {
                *reinterpret_cast<int32_t*>(ped+0x530)=state;route_clear_state_writes.clear();
                const auto seated=sa_route_actor_vehicle_snapshot(actor);
                check(seated && seated->in_vehicle,"Occupied aim restoration receives an actual valid vehicle identity");
                cleared=sa_route_clear_point_gun_at(*seated,effect_authorized,&route_effect_allowed);
                check(cleared.executed && cleared.native_entered &&
                    route_clear_state_writes==std::vector<int32_t>({1,50}) &&
                    *reinterpret_cast<int32_t*>(ped+0x530)==50 && (ped[0x46D]&1),
                    "Original occupied AIM_GUN/ATTACK restores IDLE then DRIVING independently of saved on-foot state");
            }
            *reinterpret_cast<int32_t*>(ped+0x530)=22;route_clear_state_writes.clear();
            const auto seated_state_actor=sa_route_actor_vehicle_snapshot(actor);
            check(seated_state_actor && seated_state_actor->in_vehicle,
                "Occupied state-setter profile fixture retains a live vehicle identity");
            auto* state_code=reinterpret_cast<uint8_t*>(native_entity_address(0x5E4500));const auto saved_state_code=*state_code;
            const auto before_bad_state_look=route_clear_look_calls;
            *state_code^=1;
            cleared=sa_route_clear_point_gun_at(*seated_state_actor,effect_authorized,&route_effect_allowed);
            *state_code=saved_state_code;
            check(!cleared.executed && !cleared.native_entered && route_clear_state_writes.empty() &&
                route_clear_look_calls==before_bad_state_look,"Changed occupied state setter refuses before clear-look or source restoration writes");
            route_state_gate_to_revoke=&route_effect_allowed;
            cleared=sa_route_clear_point_gun_at(*seated_state_actor,effect_authorized,&route_effect_allowed);
            route_state_gate_to_revoke=nullptr;
            check(!cleared.executed && cleared.native_entered && route_clear_state_writes==std::vector<int32_t>({1}) &&
                *reinterpret_cast<int32_t*>(ped+0x530)==1,"Revoked authorization after IDLE blocks DRIVING without replaying the entered prefix");
            route_effect_allowed=true;
            ped[0x46D]=saved_seated_flags;*reinterpret_cast<uintptr_t*>(ped+0x58C)=saved_seated_vehicle;
            *reinterpret_cast<int32_t*>(ped+0x530)=0;
            for(int fault:{1,2,3}) {
                route_clear_gun_fault=fault;
                cleared=sa_route_clear_point_gun_at(*actor_facts,effect_authorized,&route_effect_allowed);
                check(!cleared.executed && cleared.native_entered,"Entered clear/abort failure is terminal rather than replayable");
            }
            route_clear_gun_fault=0;
            route_clear_gate_to_revoke=&route_effect_allowed;
            const auto before_revoke_aim=route_clear_aim_calls;
            cleared=sa_route_clear_point_gun_at(*actor_facts,effect_authorized,&route_effect_allowed);
            route_clear_gate_to_revoke=nullptr;
            check(!cleared.executed && cleared.native_entered && route_clear_aim_calls==before_revoke_aim,
                "Authorization revoked inside ClearLook prevents ClearAim and gun abort");
            route_effect_allowed=true;
            auto* aim_code=reinterpret_cast<uint8_t*>(native_entity_address(0x5DEF20));const auto saved_aim_code=*aim_code;
            *aim_code^=1;
            cleared=sa_route_clear_point_gun_at(*actor_facts,effect_authorized,&route_effect_allowed);
            *aim_code=saved_aim_code;
            check(!cleared.executed && !cleared.native_entered,"Modified aim routine refuses before native entry");
            route_effect_intelligence=saved_intelligence;
        }
        const auto idle_before=route_effect_idle_calls,move_before=route_effect_move_calls;
        *reinterpret_cast<int32_t*>(ped+0x534)=1;
        auto promotion=sa_route_promote_still_to_run(*actor_facts,effect_authorized,&route_effect_allowed);
        check(promotion.executed && promotion.native_entered && *reinterpret_cast<int32_t*>(ped+0x534)==6,
              "Source boat admission promotes native STILL to RUN");
        promotion=sa_route_promote_still_to_run(*actor_facts,effect_authorized,&route_effect_allowed);
        check(promotion.executed && !promotion.native_entered && route_effect_move_calls==move_before+1,
              "Source movement promotion preserves an actor already running");
        auto idle_effect=sa_route_idle_ped_still(*actor_facts,effect_authorized,&route_effect_allowed);
        check(idle_effect.executed && idle_effect.native_entered && route_effect_idle_calls==idle_before+1 &&
              route_effect_move_calls==move_before+2,
              "Exact native SetIdle then SetMoveState(STILL) run once under source authorization");
        route_effect_gate_to_revoke=&route_effect_allowed;
        idle_effect=sa_route_idle_ped_still(*actor_facts,effect_authorized,&route_effect_allowed);
        route_effect_gate_to_revoke=nullptr;
        check(!idle_effect.executed && idle_effect.native_entered && route_effect_idle_calls==idle_before+2 &&
              route_effect_move_calls==move_before+2,
              "Source authorization revoked inside SetIdle blocks second native mutation and retains entered receipt");
        for(const auto address:{uintptr_t(0x616920),uintptr_t(0x420D40),uintptr_t(0x56A490),uintptr_t(0x569E20),uintptr_t(0x5E4110)})profile(pe,address,16);
        profile(pe,0x534540,17);
        const auto ped_model=*reinterpret_cast<const uint16_t*>(ped+0x22);
        auto* ped_model_cell=reinterpret_cast<uintptr_t*>(native_entity_address(0xA9B0C8)+uintptr_t(ped_model)*4);
        auto* car_model_cell=reinterpret_cast<uintptr_t*>(native_entity_address(0xA9B0C8));
        const auto saved_ped_model_info=*ped_model_cell,saved_car_model_info=*car_model_cell;
        *ped_model_cell=1;*car_model_cell=1;
        const auto saved_ped_vtable=*reinterpret_cast<uintptr_t*>(ped);
        auto* teleport_slot=reinterpret_cast<uintptr_t*>(native_entity_address(0x86C358)+14*sizeof(uintptr_t));
        const auto saved_teleport_slot=*teleport_slot;
        // Keep the retail vtable identity in the ped, as the production PE
        // does; mapping a fixture pointer twice depends on allocation address.
        *reinterpret_cast<uintptr_t*>(ped)=0x86C358;
        *teleport_slot=native_entity_address(0x5E4110);
        const float warp_origin[3]{0,0,0},warp_target[3]{40,0,0};
        std::memcpy(ped+4,warp_origin,12);std::memcpy(vehicles.data()+4,warp_target,12);
        auto warp_actor=sa_route_actor_vehicle_snapshot(actor);auto warp_vehicle=sa_route_vehicle_snapshot(7);
        check(warp_actor && warp_vehicle && warp_actor->on_screen==std::optional<bool>{false} &&
              warp_vehicle->on_screen==std::optional<bool>{false},"Real native snapshots feed off-screen warp facts");
        route_effect_allowed=true;
        profile(pe,0x64E740,16);profile(pe,0x4241C0,16);
        const auto position_writes_before=route_effect_position_writes;
        for(const auto entry: {std::pair{RouteEntryDoor::front_right,8},
                              std::pair{RouteEntryDoor::rear_left,11},
                              std::pair{RouteEntryDoor::rear_right,9}}) {
            auto positioned=sa_route_position_ped_for_car_entry(*warp_actor,*warp_vehicle,entry.first,
                effect_authorized,&route_effect_allowed);
            check(positioned.executed && positioned.native_entered && route_effect_door_node==entry.second &&
                  *reinterpret_cast<float*>(ped+4)==12.0f && *reinterpret_cast<float*>(ped+8)==3.0f,
                  "Source entry door maps to exact SA node and SetPosn, never Teleport or warp into car");
        }
        route_effect_allowed=false;
        const auto refused_position=sa_route_position_ped_for_car_entry(*warp_actor,*warp_vehicle,
            RouteEntryDoor::front_right,effect_authorized,&route_effect_allowed);
        check(!refused_position.executed && !refused_position.native_entered &&
              route_effect_position_writes==position_writes_before+3,
              "Revoked source route cannot reposition actor for passenger fallback");
        route_effect_allowed=true;std::memcpy(ped+4,warp_origin,12);
        {
            // Private vehicle-entry lifetime backend (src/sa_owned_vehicle_entry.cpp), exercised directly.
            // Real PE bytes pin every entry point; the fixture only supplies the callable vtable slots.
            for(const auto address:{uintptr_t(0x61A5A0),uintptr_t(0x61A5B0),uintptr_t(0x465C20),uintptr_t(0x63DFA0),
                    uintptr_t(0x643C80),uintptr_t(0x63A730),uintptr_t(0x63A890),uintptr_t(0x643A60),uintptr_t(0x63E990),
                    uintptr_t(0x6402F0),uintptr_t(0x640340),uintptr_t(0x643780),uintptr_t(0x6437F0),
                    uintptr_t(0x62F310),uintptr_t(0x643C60),uintptr_t(0x63F970)})profile(pe,address,32);
            profile(pe,0x86DD2C,44);
            profile(pe,0x4B0A30,6);
            auto* pool_slot=reinterpret_cast<uintptr_t*>(native_entity_address(0xB744A8));const auto saved_pool_slot=*pool_slot;
            owned_entry_pool_header={{owned_entry_slot_address(0),reinterpret_cast<uintptr_t>(owned_entry_flags.data()),8}};
            *pool_slot=reinterpret_cast<uintptr_t>(owned_entry_pool_header.data());
            owned_entry_flags.fill(0x80);
            owned_entry_subtask_table.fill(0);owned_entry_subtask_table[4]=reinterpret_cast<uintptr_t>(&owned_entry_subtask_task_type);
            owned_entry_subtask_table[6]=reinterpret_cast<uintptr_t>(&owned_entry_child_abort);
            owned_entry_standstill_table.fill(0);owned_entry_standstill_table[4]=reinterpret_cast<uintptr_t>(&owned_entry_standstill_task_type);
            for(const auto& [retail,opcode]:{std::pair<uintptr_t,uint16_t>{owned_entry_driver_vtable,0x05CB},
                                              std::pair<uintptr_t,uint16_t>{owned_entry_passenger_vtable,0x05CA}}) {
                auto* table=reinterpret_cast<uintptr_t*>(native_entity_address(retail));table[-1]=0x5A5A;
                for(size_t i=0;i<12;++i)table[i]=native_owned_vehicle_entry_expected(i,opcode);
            }
            const auto saved_reference_mode_entry=actor_reference_mode;actor_reference_mode=true;
            *reinterpret_cast<uintptr_t*>(ped+0x24)=0;
            *reinterpret_cast<uint32_t*>(ped+0x474)&=~0x800000u;
            auto* entry_bank=reinterpret_cast<ScriptTaskRecord*>(native_entity_address(0xC0B1E8));
            const auto saved_entry_bank=entry_bank[127];
            const auto saved_entry_flags=ped[0x46D];const auto saved_entry_vehicle=*reinterpret_cast<uintptr_t*>(ped+0x58C);
            ped[0x46D]&=~1u;*reinterpret_cast<uintptr_t*>(ped+0x58C)=0;
            // Explicit-door passenger factory: production tracking, receipts and policy release run on the
            // same pool and callable vtables as the entry backend; only SA's engine calls are fixtures.
            {
                auto* passenger_bank=reinterpret_cast<ScriptTaskRecord*>(native_entity_address(0xC0B1E8));
                const auto saved_passenger_record=passenger_bank[127];
                const auto old_bus_model_flags=vehicles[0x429];vehicles[0x429]|=2;
                const auto bus_vehicle=sa_route_vehicle_snapshot(7);
                check(bus_vehicle && bus_vehicle->is_bus==std::optional<bool>{true},"Actual bus flag feeds explicit-door task factory");
                const auto native_destroy=[&](uintptr_t task) { // SA deleting the task through its own vtable
                    const auto table=*reinterpret_cast<const uintptr_t*>(task);
                    reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t,uintptr_t,uint32_t)>(reinterpret_cast<const uintptr_t*>(table)[0])(task,0,1);
                };
                const auto assignments_before=route_passenger_assignments;
                auto effect=sa_route_enter_passenger_door(*warp_actor,*bus_vehicle,RouteEntryDoor::rear_left,true,
                    effect_authorized,&route_effect_allowed);
                if(!effect.executed)std::cerr<<"passenger task: "<<effect.diagnostic<<'\n';
                check(effect.executed && effect.native_entered && effect.accepted_script_task &&
                      effect.accepted_script_task->record.opcode==0x05CA && route_passenger_node==11 &&
                      route_passenger_carry_on && route_passenger_cleanup && route_passenger_assignments==assignments_before+1,
                      "Bus rear-left task preserves explicit node, carry-on flag, C6 cleanup and accepted 05CA script-bank record");
                const auto adopted=effect.accepted_script_task?sa_owned_vehicle_entry_epoch(*effect.accepted_script_task,actor):std::nullopt;
                check(adopted.has_value() && !entity_references.empty(),"The accepted explicit-door receipt resolves to its private lineage, which keeps the weak actor reference");
                native_destroy(effect.accepted_script_task->record.task);
                sa_release_owned_vehicle_entry(actor,*adopted);
                check(entity_references.empty(),"An adopted explicit-door lineage releases its weak reference when its last task retires");
                const auto allocations_before=route_passenger_allocations;
                route_effect_allowed=false;
                effect=sa_route_enter_passenger_door(*warp_actor,*bus_vehicle,RouteEntryDoor::rear_right,true,
                    effect_authorized,&route_effect_allowed);
                check(!effect.executed && !effect.native_entered && route_passenger_allocations==allocations_before,
                      "Revoked route cannot allocate or assign an explicit bus-door task");
                passenger_bank[127]=ScriptTaskRecord{}; // a retired record must not alias the reused pool slot
                route_effect_allowed=true;route_passenger_revoke_after_construct=&route_effect_allowed;
                const auto destroyed_before=owned_entry_destroyed;
                effect=sa_route_enter_passenger_door(*warp_actor,*bus_vehicle,RouteEntryDoor::rear_right,true,
                    effect_authorized,&route_effect_allowed);
                route_passenger_revoke_after_construct=nullptr;
                check(!effect.executed && owned_entry_destroyed==destroyed_before+1 &&
                      route_passenger_assignments==assignments_before+1 && entity_references.empty() &&
                      owned_entry_flags[0]==0x80,
                      "Epoch revoked after private construction destroys the owned task before scripted assignment and releases its lineage");
                route_effect_allowed=true;
                // Post-assignment failures return no adoptable result: the policy lineage must still be retired
                // (the task itself stays with SA), so that its last retirement frees the weak actor reference.
                for(const int fault:{1,2}) {
                    passenger_bank[127]=ScriptTaskRecord{};route_passenger_assign_fault=fault;
                    effect=sa_route_enter_passenger_door(*warp_actor,*bus_vehicle,RouteEntryDoor::rear_left,true,
                        effect_authorized,&route_effect_allowed);
                    route_passenger_assign_fault=0;
                    check(!effect.executed && effect.native_entered && bool(effect.accepted_script_task)==(fault==1),
                          "A faulted explicit-door assignment is a failed command with the receipt only when SA recorded it");
                    native_destroy(route_passenger_last_task);
                    check(entity_references.empty(),
                          "A failed explicit-door command leaves no lineage holding a weak actor reference after the task retires");
                    sa_release_owned_vehicle_entry(actor,1); // harmless on a collected/unknown epoch
                }
                vehicles[0x429]=old_bus_model_flags;passenger_bank[127]=saved_passenger_record;
                for(size_t slot=0;slot<owned_entry_slots.size();++slot)owned_entry_free(owned_entry_slot_address(slot));
                // Discard of a still-private task must not depend on the actor's identity surviving.
                {
                    const auto vehicle_address=reinterpret_cast<uintptr_t>(vehicles.data());
                    const auto lose_actor=[&] { // native deletion: slot generation changes and weak cells are cleared
                        ped_flag=28;
                        for(auto& reference:entity_references)*reference.second=0;
                        entity_references.clear();*reinterpret_cast<uintptr_t*>(ped+0x24)=0;
                    };
                    const auto restore_actor=[&] {ped_flag=27;};
                    const auto destroyed_start=owned_entry_destroyed;
                    const auto lost=owned_entry_take();
                    native_owned_vehicle_entry_construct_passenger(lost,vehicle_address,8,true);
                    check(sa_track_private_vehicle_entry(lost,actor,0x05CA)!=0,"Private task is tracked while its actor is alive");
                    lose_actor();
                    check(!sa_live_objective_actor(actor.handle),"The actor identity is gone");
                    check(sa_discard_private_vehicle_entry(lost,actor,0x05CA) && owned_entry_destroyed==destroyed_start+1 &&
                          owned_entry_flags[0]==0x80,"A still-private tracked task is discarded after the actor identity was lost");
                    restore_actor();
                    const auto native_lost=owned_entry_take();
                    native_owned_vehicle_entry_construct_passenger(native_lost,vehicle_address,8,true);
                    ped_flag=28;
                    check(sa_discard_private_vehicle_entry(native_lost,actor,0x05CA) && owned_entry_flags[0]==0x80,
                          "A constructed-but-untracked task is discarded after actor loss");
                    restore_actor();
                    // Preservation: assigned, foreign and multi-instance tasks are never destroyed.
                    const auto assigned=owned_entry_take();
                    native_owned_vehicle_entry_construct_passenger(assigned,vehicle_address,8,true);
                    check(sa_track_private_vehicle_entry(assigned,actor,0x05CA)!=0,"Task to be assigned is tracked");
                    passenger_bank[127]={0x05CA,0,uint32_t(assigned),uint32_t(actor.native_ped),1};
                    check(sa_owned_vehicle_entry_epoch(CapturedScriptTask{127,passenger_bank[127]},actor).has_value(),"The assigned task's receipt binds its lineage");
                    ped_flag=28;const auto kept_destroyed=owned_entry_destroyed;
                    check(!sa_discard_private_vehicle_entry(assigned,actor,0x05CA) && owned_entry_destroyed==kept_destroyed,
                          "A task already in the script-task bank is SA's and is never discarded");
                    restore_actor();
                    passenger_bank[127]=ScriptTaskRecord{};
                    // settle: SA retires the assigned task; its lineage is released by its owner
                    native_destroy(assigned);
                    for(auto& reference:entity_references)*reference.second=0;
                    entity_references.clear();*reinterpret_cast<uintptr_t*>(ped+0x24)=0;
                    for(size_t slot=0;slot<owned_entry_slots.size();++slot)owned_entry_free(owned_entry_slot_address(slot));
                }
            }
            struct Entry {uintptr_t task=0,table=0;uint64_t epoch=0;CapturedScriptTask captured;};
            const auto make_entry=[&](uint16_t opcode,int32_t seat=2)->Entry {
                entry_bank[127]=ScriptTaskRecord{}; // a fresh bank state: the assignment must be an observable change
                NativeCall call;call.opcode=opcode;call.inputs=opcode==0x05CB?std::vector<int32_t>{actor.handle,7,-1}:std::vector<int32_t>{actor.handle,7,-1,seat};
                const auto made=sa_create_owned_vehicle_entry(call);
                check(made.executed && made.native_entered && made.accepted_script_task && made.accepted_script_task->record.opcode==opcode,
                      "Owned entry factory builds, tracks and assigns an untimed task with a unique bank receipt");
                Entry entry;entry.task=made.accepted_script_task->record.task;entry.captured=*made.accepted_script_task;
                entry.table=*reinterpret_cast<uintptr_t*>(entry.task);
                const auto epoch=sa_owned_vehicle_entry_epoch(*made.accepted_script_task,actor);
                check(epoch.has_value(),"The accepted receipt resolves to this exact private task lineage");
                entry.epoch=*epoch;return entry;
            };
            const auto observer_callbacks=[](){
                std::array<uint32_t,task_hook_sites.size()> callbacks{};
                for(size_t i=0;i<callbacks.size();++i)callbacks[i]=0x30001000u+uint32_t(i*0x100);
                return callbacks;
            };
            const auto slot_of=[](uintptr_t table,size_t index){return reinterpret_cast<const uintptr_t*>(table)[index];};
            const auto destroy_task=[&](const Entry& entry,uintptr_t task){
                reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t,uintptr_t,uint32_t)>(slot_of(entry.table,0))(task,0,1);
            };
            // Exercise the production release/allocator profiles while the real transaction
            // owns all eleven retail observer redirects. Its registered test callback delegates
            // to owns_original_prefix, so it authenticates this transaction's entry and trampoline.
            {
                for(const auto& site:task_hook_sites) {
                    profile(pe,site.address,site.length);
                    check(!std::memcmp(reinterpret_cast<const void*>(native_entity_address(site.address)),
                        site.prefix.data(),site.length),"Retail observer sites match the reviewed PE prefixes");
                }
                profile(pe,0x61A5A0,11);
                TaskObserverRegressionMemory memory;TaskHookTransaction transaction;
                const auto callbacks=observer_callbacks();
                check(transaction.install(memory,callbacks,memory),"The actual transaction installs its complete retail observer set");
                const auto previous_prefix=task_memory_original_prefix;
                task_memory_profile_transaction=&transaction;task_memory_profile_memory=&memory;
                task_memory_original_prefix=&task_memory_original_prefix_for_test;
                check(sa_task_allocator_profile_matches() && sa_task_release_profile_matches(),
                      "Retail allocator and authenticated release profiles survive observer installation");
                for(const auto opcode:{uint16_t(0x05CB),uint16_t(0x05CA)}) {
                    const auto entry=make_entry(opcode);
                    check(sa_task_slot_live(entry.task),"Driver and passenger task membership remains valid under the retail observer");
                    destroy_task(entry,entry.task);sa_release_owned_vehicle_entry(actor,entry.epoch);
                }
                auto* release_entry=memory.writable(0x61A5B0,6);uint8_t saved_entry[6]{};
                std::memcpy(saved_entry,release_entry,6);release_entry[1]^=0x40;
                check(!sa_task_release_profile_matches(),"A foreign retail release redirect is rejected despite the registered observer callback");
                std::memcpy(release_entry,saved_entry,6);
                auto* trampoline=memory.writable(transaction.trampoline(8),11);uint8_t saved_trampoline[11]{};
                std::memcpy(saved_trampoline,trampoline,11);trampoline[0]^=1;
                check(!sa_task_release_profile_matches(),"A changed retail release trampoline invalidates original-prefix ownership");
                std::memcpy(trampoline,saved_trampoline,11);
                check(transaction.remove(memory),"Retail observer transaction restores every original entry before its fixture ends");
                task_memory_original_prefix=previous_prefix;task_memory_profile_transaction=nullptr;task_memory_profile_memory=nullptr;
            }
            // Regression: the task constructor takes a native door node, not the
            // SCM seat ordinal. In particular SCM -1 becomes automatic door0,
            // never a -1 door that indexes a handling float as an animation ID.
            for(const auto [seat,door]:{std::pair{-1,0},std::pair{0,8},std::pair{1,11},std::pair{2,9}}) {
                const auto entry=make_entry(0x05CA,seat);
                check(*reinterpret_cast<const int32_t*>(entry.task+0x1C)==door,
                      "SCM passenger seat is converted to the native door selector before construction");
                destroy_task(entry,entry.task);sa_release_owned_vehicle_entry(actor,entry.epoch);
            }
            {
                const auto vehicle_address=reinterpret_cast<uintptr_t>(vehicles.data());
                const auto saved_class=*reinterpret_cast<int32_t*>(vehicle_address+0x590);
                *reinterpret_cast<int32_t*>(vehicle_address+0x590)=9;
                const auto bike=make_entry(0x05CA,0);
                check(*reinterpret_cast<int32_t*>(bike.task+0x1C)==11,"Bike seat zero uses the native rear door node");
                destroy_task(bike,bike.task);sa_release_owned_vehicle_entry(actor,bike.epoch);
                *reinterpret_cast<int32_t*>(vehicle_address+0x590)=saved_class;
                const auto saved_bus=vehicles[0x429];vehicles[0x429]|=2;
                const auto bus=make_entry(0x05CA,5);
                check(*reinterpret_cast<int32_t*>(bus.task+0x1C)==8,"Bus higher seat ordinals use native bus entry door");
                destroy_task(bus,bus.task);sa_release_owned_vehicle_entry(actor,bus.epoch);vehicles[0x429]=saved_bus;
                NativeCall invalid;invalid.opcode=0x05CA;invalid.inputs={actor.handle,7,-1,3};
                const auto rejected=sa_create_owned_vehicle_entry(invalid);
                check(!rejected.executed && !rejected.native_entered && owned_entry_flags[0]==0x80,
                      "A seat without a native car door is refused before construction and assignment");
            }
            // Regression for63ED3E: native CreateNextSubTask calls slot11.
            // Run that nested dispatch through each private root and its clone;
            // it must reach the copied native method, not Lineage metadata.
            for(const auto opcode:{uint16_t(0x05CA),uint16_t(0x05CB)}) {
                const auto entry=make_entry(opcode);
                check(slot_of(entry.table,11)==native_owned_vehicle_entry_expected(11,opcode),"Private entry retains the twelfth native method");
                owned_entry_transition_dispatch=true;const auto calls=owned_entry_transition_calls;
                const auto result=reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t,uintptr_t,uintptr_t)>(slot_of(entry.table,8))(entry.task,0,actor.native_ped);
                check(result==entry.task && owned_entry_transition_calls==calls+1,"Native next-subtask dispatch reaches private slot11 without executing a data address");
                owned_entry_transition_dispatch=false;
                destroy_task(entry,entry.task);sa_release_owned_vehicle_entry(actor,entry.epoch);
            }
            {
                auto* table=reinterpret_cast<uintptr_t*>(native_entity_address(owned_entry_passenger_vtable));
                const auto transition=table[11];table[11]=0x86EAE0;
                NativeCall call;call.opcode=0x05CA;call.inputs={actor.handle,7,-1,-1};
                const auto assigned_before=owned_entry_assigns;const auto refused=sa_create_owned_vehicle_entry(call);
                check(!refused.executed && owned_entry_assigns==assigned_before,"A vtable data address cannot replace native slot11 and reach assignment");
                table[11]=transition;for(size_t slot=0;slot<owned_entry_slots.size();++slot)owned_entry_free(owned_entry_slot_address(slot));
            }
            // No receipt without every entry-core profile: refusal happens before allocation or native entry.
            {
                auto* byte=reinterpret_cast<uint8_t*>(native_entity_address(0x643A60));const auto saved=*byte;*byte^=1;
                NativeCall call;call.opcode=0x05CB;call.inputs={actor.handle,7,-1};
                const auto before=owned_entry_assigns;const auto refused=sa_create_owned_vehicle_entry(call);
                *byte=saved;
                check(!refused.executed && !refused.native_entered && owned_entry_assigns==before && owned_entry_flags[0]==0x80,
                      "An altered CreateFirstSubTask profile refuses before allocating or assigning");
            }
            {
                NativeCall malformed;malformed.opcode=0x05CB;malformed.inputs={actor.handle,7,5};
                check(!sa_create_owned_vehicle_entry(malformed).executed,"A timed entry is not the reviewed untimed task");
                malformed.opcode=0x05CA;malformed.inputs={actor.handle,7,-1,9};
                check(!sa_create_owned_vehicle_entry(malformed).executed,"A passenger seat outside the array is refused");
                malformed.opcode=0x05CB;malformed.inputs={actor.handle,0x7777,-1};
                check(!sa_create_owned_vehicle_entry(malformed).executed && owned_entry_flags[0]==0x80,"A missing vehicle generation is refused before allocation");
            }
            // Reproduce the installed OLA Task=unlimited hooks using its actual
            // PE bytes on non-executable pages. Native calls remain the existing
            // fixture boundaries; profile and multi-block reads are production code.
            if(argc==3) {
                std::ifstream ola_input(argv[2],std::ios::binary);
                const std::vector<uint8_t> disk((std::istreambuf_iterator<char>(ola_input)),{});
                check(disk.size()>0x100,"Installed adjuster PE must open as data");
                const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(disk.data());
                check(dos->e_lfanew>0 && size_t(dos->e_lfanew)+sizeof(IMAGE_NT_HEADERS32)<disk.size(),"Adjuster PE headers fit input");
                const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(disk.data()+dos->e_lfanew);
                check(nt->OptionalHeader.SizeOfImage==0x68000,"Expected installed adjuster image");
                std::vector<uint8_t> image(0x68000);
                std::memcpy(image.data(),disk.data(),nt->OptionalHeader.SizeOfHeaders);
                const auto sections=IMAGE_FIRST_SECTION(nt);
                for(size_t n=0;n<nt->FileHeader.NumberOfSections;++n) {
                    const auto& section=sections[n];
                    check(uint64_t(section.PointerToRawData)+section.SizeOfRawData<=disk.size() &&
                          uint64_t(section.VirtualAddress)+section.SizeOfRawData<=image.size(),"Adjuster section bounds");
                    std::memcpy(image.data()+section.VirtualAddress,disk.data()+section.PointerToRawData,section.SizeOfRawData);
                }
                task_ola_module=reinterpret_cast<uintptr_t>(image.data());
                const auto singleton=task_ola_module+0x5F84C;
                for(const auto operand:{uintptr_t(0x26F62),uintptr_t(0x27068),uintptr_t(0x26EBF)})
                    std::memcpy(image.data()+operand,&singleton,4); // Relocated absolute operands.
                std::array<std::array<uint8_t,5>,3> saved{};
                const uintptr_t sites[]{0x61A5A0,0x61A5B0,0x61A4B0},targets[]{0x26F60,0x27060,0x26EB0};
                for(size_t n=0;n<3;++n) {
                    auto* code=reinterpret_cast<uint8_t*>(native_entity_address(sites[n]));
                    std::memcpy(saved[n].data(),code,5);code[0]=0xE9;
                    const uint32_t delta=uint32_t(task_ola_module+targets[n]-reinterpret_cast<uintptr_t>(code)-5);
                    std::memcpy(code+1,&delta,4);
                }
                std::array<uint8_t,256> first_block{};
                std::array<uintptr_t,2> begins{{reinterpret_cast<uintptr_t>(first_block.data()),owned_entry_slot_address(0)}};
                std::array<uintptr_t,2> ends{{begins[0]+256,begins[1]+8*128}};
                std::array<uint8_t,10> flags{};flags.fill(0x80);
                std::array<uintptr_t,8> dynamic_pool{{0,reinterpret_cast<uintptr_t>(flags.data()),10,0,1,2,
                    reinterpret_cast<uintptr_t>(begins.data()),reinterpret_cast<uintptr_t>(ends.data())}};
                const auto dynamic_address=reinterpret_cast<uintptr_t>(dynamic_pool.data());
                std::memcpy(reinterpret_cast<void*>(singleton),&dynamic_address,4);
                owned_entry_ola_flags=flags.data();
                check(sa_task_allocator_profile_matches() && sa_task_release_profile_matches(),"Installed OLA ASLR hooks retain native allocation/release ABI");
                for(const auto& site:task_hook_sites)if(site.address!=0x61A5B0) {
                    profile(pe,site.address,site.length);
                    check(!std::memcmp(reinterpret_cast<const void*>(native_entity_address(site.address)),
                        site.prefix.data(),site.length),"OLA observer retail sites match the reviewed PE prefixes");
                }
                const auto ola_release=sa_task_ola_release_entry();
                check(ola_release==task_ola_module+0x27060 && ola_release<=UINT32_MAX,
                      "Only the verified installed OLA release body is eligible for the observer override");
                const TaskHookSite ola_release_site{uint32_t(ola_release),6,{{0x55,0x8B,0xEC,0xFF,0x75,0x08}}};
                TaskObserverRegressionMemory observer_memory(task_ola_module);TaskHookTransaction observer_transaction;
                const auto callbacks=observer_callbacks();
                check(observer_transaction.install(observer_memory,callbacks,observer_memory,ola_release_site),
                      "The actual transaction hooks the verified OLA release body with its copied six-byte prefix");
                const auto previous_prefix=task_memory_original_prefix;
                task_memory_profile_transaction=&observer_transaction;task_memory_profile_memory=&observer_memory;
                task_memory_original_prefix=&task_memory_original_prefix_for_test;
                check(sa_task_allocator_profile_matches() && sa_task_release_profile_matches(),
                      "OLA allocator and authenticated release profiles survive observer installation");
                for(const auto opcode:{uint16_t(0x05CA),uint16_t(0x05CB)}) {
                    // make_entry runs the driver or passenger common/core profile gate under the observer;
                    // the resulting live task also exercises OLA's multi-block membership profile.
                    const auto entry=make_entry(opcode);
                    check(sa_task_slot_live(entry.task),"Owned real entry is live in the second OLA block");
                    const auto slot=(entry.task-begins[1])/128;
                    flags[2+slot]|=0x80;
                    check(!sa_task_slot_live(entry.task),"OLA membership rejects freed second-block slot");
                    flags[2+slot]&=~0x80;
                    check(!sa_task_slot_live(entry.task+1),"OLA membership rejects misaligned pointers");
                    const auto old_count=dynamic_pool[2];dynamic_pool[2]=9;
                    check(!sa_task_slot_live(entry.task),"Malformed dynamic block inventory cannot authorize a task");dynamic_pool[2]=old_count;
                    destroy_task(entry,entry.task);sa_release_owned_vehicle_entry(actor,entry.epoch);
                }
                const auto live_entry=make_entry(0x05CA);
                check(sa_task_slot_live(live_entry.task),"A fresh passenger task is live before OLA observer tamper checks");
                auto* live_release=observer_memory.writable(ola_release_site.address,6);uint8_t saved_release[6]{};
                std::memcpy(saved_release,live_release,6);live_release[1]^=0x40;
                check(!sa_task_allocator_profile_matches() && !sa_task_release_profile_matches() &&
                      !sa_task_slot_live(live_entry.task),
                      "A foreign OLA release redirect invalidates allocator, release, and live-slot profiles");
                std::memcpy(live_release,saved_release,6);
                auto* release_trampoline=observer_memory.writable(observer_transaction.trampoline(8),11);
                uint8_t saved_trampoline[11]{};std::memcpy(saved_trampoline,release_trampoline,11);
                release_trampoline[0]^=1;
                check(!sa_task_allocator_profile_matches() && !sa_task_release_profile_matches() &&
                      !sa_task_slot_live(live_entry.task),
                      "A changed OLA release trampoline cannot authenticate the observer-owned prefix");
                std::memcpy(release_trampoline,saved_trampoline,11);
                const auto old_tail=image[0x27060+6];image[0x27060+6]^=1;
                check(!sa_task_allocator_profile_matches() && !sa_task_release_profile_matches() &&
                      !sa_task_slot_live(live_entry.task),
                      "A changed OLA release tail beyond the six-byte detour invalidates the complete original profile");
                image[0x27060+6]=old_tail;
                check(sa_task_allocator_profile_matches() && sa_task_release_profile_matches() &&
                      sa_task_slot_live(live_entry.task),
                      "Restoring the OLA entry, trampoline, and full original tail restores verified profiles");
                destroy_task(live_entry,live_entry.task);sa_release_owned_vehicle_entry(actor,live_entry.epoch);
                check(observer_transaction.remove(observer_memory),"OLA observer transaction restores the verified release body and all retail entries");
                task_memory_original_prefix=previous_prefix;task_memory_profile_transaction=nullptr;task_memory_profile_memory=nullptr;
                auto* membership=reinterpret_cast<uint8_t*>(native_entity_address(0x61A4B0));membership[1]^=1;
                check(!sa_task_allocator_profile_matches(),"Allocator alone cannot authorize foreign or changed membership hook");membership[1]^=1;
                const auto old_byte=image[0x27060];image[0x27060]^=1;
                check(!sa_task_release_profile_matches(),"Modified adjuster callback is refused");image[0x27060]=old_byte;
                for(size_t n=0;n<3;++n)std::memcpy(reinterpret_cast<void*>(native_entity_address(sites[n])),saved[n].data(),5);
                owned_entry_ola_flags=nullptr;task_ola_module=0;
                check(sa_task_allocator_profile_matches(),"Retail allocator profile restored after OLA test");
                auto* stride=reinterpret_cast<uint8_t*>(native_entity_address(0x61A55A));const auto saved_stride=stride[2];stride[2]=6;
                check(!sa_task_allocator_profile_matches(),"Retail allocator cannot authorize a foreign task slot stride");stride[2]=saved_stride;
            }
            // Lifetime: clones stay in the lineage; the lineage dies only with its last native clone.
            {
                const auto entry=make_entry(0x05CB);
                check(entry.table!=owned_entry_driver_vtable && slot_of(entry.table,0)!=reinterpret_cast<uintptr_t>(&owned_entry_native_destroy),
                      "The task now carries the private vtable");
                const auto copy=reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t,uintptr_t)>(slot_of(entry.table,1))(entry.task,0);
                check(copy && *reinterpret_cast<uintptr_t*>(copy)==entry.table && owned_entry_clones==1,
                      "A native clone re-enters the same private lineage instead of escaping it");
                owned_entry_transition_dispatch=true;const auto transition_before=owned_entry_transition_calls;
                const auto continued=reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t,uintptr_t,uintptr_t)>(slot_of(entry.table,8))(copy,0,actor.native_ped);
                check(continued==copy && owned_entry_transition_calls==transition_before+1,"Cloned entry preserves the twelfth native method through nested next-subtask dispatch");
                owned_entry_transition_dispatch=false;
                const auto destroyed_before=owned_entry_destroyed;
                destroy_task(entry,copy);
                check(owned_entry_destroyed==destroyed_before+1 && sa_cancel_owned_vehicle_entry(actor,entry.epoch)==std::optional<bool>(false),
                      "While one clone is alive the lineage is not retired");
                destroy_task(entry,entry.task);
                check(sa_cancel_owned_vehicle_entry(actor,entry.epoch)==std::optional<bool>(true),
                      "Retirement is confirmed only after every clone ran its destructor");
                sa_release_owned_vehicle_entry(actor,entry.epoch);
                check(!sa_cancel_owned_vehicle_entry(actor,entry.epoch).has_value() && !sa_owned_vehicle_entry_epoch(entry.captured,actor).has_value(),
                      "A released lineage is no longer addressable and its weak actor cell is freed");
            }
            // Cancellation before SA created the approach: CreateFirstSubTask is suppressed, nothing started.
            {
                const auto entry=make_entry(0x05CA);
                check(sa_cancel_owned_vehicle_entry(actor,entry.epoch)==std::optional<bool>(false),"Pre-start cancellation waits for retirement");
                const auto first_before=owned_entry_first;
                const auto started=reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t,uintptr_t,uintptr_t)>(slot_of(entry.table,9))(entry.task,0,actor.native_ped);
                check(started==0 && owned_entry_first==first_before,"A cancelled entry never creates its door approach");
                destroy_task(entry,entry.task);
                check(sa_cancel_owned_vehicle_entry(actor,entry.epoch)==std::optional<bool>(true),"The suppressed task retires");
                sa_release_owned_vehicle_entry(actor,entry.epoch);
            }
            // Waiting reuses the same accepted root/epoch: a 1ms native standstill loops,
            // and resume calls CreateFirstSubTask without reissuing the script opcode.
            {
                const auto entry=make_entry(0x05CA);
                const auto assignments_before=owned_entry_assigns;
                const auto constructs_before=owned_entry_standstill_constructions;
                check(sa_set_owned_vehicle_entry_wait(actor,entry.epoch,true)==std::optional<bool>(true),
                      "An exact, queued private entry accepts a wait policy");
                const auto first=reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t,uintptr_t,uintptr_t)>(
                    slot_of(entry.table,9))(entry.task,0,actor.native_ped);
                check(first && owned_entry_last_allocation_bytes==0x20 &&
                      owned_entry_standstill_duration==1 && !owned_entry_standstill_stay &&
                      !owned_entry_standstill_area && owned_entry_standstill_blend==8.0f &&
                      owned_entry_standstill_constructions==constructs_before+1,
                      "Wait starts with the retail 1ms standstill child allocated at its profiled 0x20 size");
                *reinterpret_cast<uintptr_t*>(entry.task+8)=first;
                const auto second=reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t,uintptr_t,uintptr_t)>(
                    slot_of(entry.table,8))(entry.task,0,actor.native_ped);
                check(second && second!=first && owned_entry_standstill_constructions==constructs_before+2,
                      "A completed short standstill is replaced by another native standstill while wait remains set");
                *reinterpret_cast<uintptr_t*>(entry.task+8)=second;owned_entry_free(first);
                auto* approach=reinterpret_cast<uintptr_t*>(owned_entry_take());
                *approach=reinterpret_cast<uintptr_t>(owned_entry_subtask_table.data());
                owned_entry_subtask=reinterpret_cast<uintptr_t>(approach);owned_entry_subtask_type=800;
                const auto first_calls=owned_entry_first;
                check(sa_set_owned_vehicle_entry_wait(actor,entry.epoch,false)==std::optional<bool>(true),
                      "The same owned task accepts resume after its pause child is current");
                const auto resumed=reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t,uintptr_t,uintptr_t)>(
                    slot_of(entry.table,10))(entry.task,0,actor.native_ped);
                check(resumed==reinterpret_cast<uintptr_t>(approach) && owned_entry_first==first_calls+1 &&
                      owned_entry_assigns==assignments_before &&
                      sa_owned_vehicle_entry_epoch(entry.captured,actor)==std::optional<uint64_t>(entry.epoch),
                      "Resume restarts the native child on the existing root and preserves its binding epoch without a new 05CA assignment");
                *reinterpret_cast<uintptr_t*>(entry.task+8)=resumed;
                destroy_task(entry,entry.task);sa_release_owned_vehicle_entry(actor,entry.epoch);
                owned_entry_free(second);owned_entry_free(reinterpret_cast<uintptr_t>(approach));owned_entry_subtask=0;
            }
            {
                auto entry=make_entry(0x05CB);
                check(sa_set_owned_vehicle_entry_wait(actor,entry.epoch,true)==std::optional<bool>(true),"Entry accepts wait before pool exhaustion");
                const auto paused=reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t,uintptr_t,uintptr_t)>(slot_of(entry.table,9))(entry.task,0,actor.native_ped);
                *reinterpret_cast<uintptr_t*>(entry.task+8)=paused;
                owned_entry_allocation_unavailable=true;
                const auto next=reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t,uintptr_t,uintptr_t)>(slot_of(entry.table,8))(entry.task,0,actor.native_ped);
                owned_entry_allocation_unavailable=false;
                check(paused && !next,"Pool exhaustion never returns the completed child that SA will delete before SetSubTask");
                destroy_task(entry,entry.task);sa_release_owned_vehicle_entry(actor,entry.epoch);owned_entry_free(paused);
            }
            {
                auto entry=make_entry(0x05CB);
                check(sa_set_owned_vehicle_entry_wait(actor,entry.epoch,true)==std::optional<bool>(true),"Queued wait policy accepted before campaign retirement");
                sa_release_owned_vehicle_entry(actor,entry.epoch);
                const auto first=reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t,uintptr_t,uintptr_t)>(slot_of(entry.table,9))(entry.task,0,actor.native_ped);
                check(!first,"Retired queued wait cannot strand a player or NPC in a newly allocated standstill");
                destroy_task(entry,entry.task);
            }
            {
                auto entry=make_entry(0x05CB);
                check(sa_set_owned_vehicle_entry_wait(actor,entry.epoch,true)==std::optional<bool>(true),"Live wait policy accepted before campaign retirement");
                const auto paused=reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t,uintptr_t,uintptr_t)>(slot_of(entry.table,9))(entry.task,0,actor.native_ped);
                *reinterpret_cast<uintptr_t*>(entry.task+8)=paused;
                sa_release_owned_vehicle_entry(actor,entry.epoch);
                const auto controlled=reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t,uintptr_t,uintptr_t)>(slot_of(entry.table,10))(entry.task,0,actor.native_ped);
                check(paused && !controlled,"Campaign retirement terminates only its owned wait child without restarting car entry");
                destroy_task(entry,entry.task);owned_entry_free(paused);
            }
            // A running door approach pauses only after its own MakeAbortable accepts URGENT.
            {
                const auto entry=make_entry(0x05CB);
                const auto approach=owned_entry_take();
                *reinterpret_cast<uintptr_t*>(approach)=reinterpret_cast<uintptr_t>(owned_entry_subtask_table.data());
                *reinterpret_cast<uintptr_t*>(entry.task+8)=approach;
                owned_entry_subtask=approach;owned_entry_subtask_type=800;
                owned_entry_child_aborts=0;owned_entry_child_abort_accepts=false;
                const auto constructs_before=owned_entry_standstill_constructions;
                const auto controls_before=owned_entry_controls;
                check(sa_set_owned_vehicle_entry_wait(actor,entry.epoch,true)==std::optional<bool>(true),
                      "A live profiled door approach accepts a wait request");
                check(reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t,uintptr_t,uintptr_t)>(
                          slot_of(entry.table,10))(entry.task,0,actor.native_ped)==approach &&
                      owned_entry_child_aborts==1 && owned_entry_controls==controls_before+1 &&
                      owned_entry_standstill_constructions==constructs_before,
                      "A refused child abort leaves SA control intact and does not claim a pause");
                owned_entry_child_abort_accepts=true;
                const auto paused=reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t,uintptr_t,uintptr_t)>(
                    slot_of(entry.table,10))(entry.task,0,actor.native_ped);
                check(paused && paused!=approach && owned_entry_child_aborts==2 &&
                      owned_entry_last_allocation_bytes==0x20,
                      "An accepted child abort swaps only this owned approach for the native standstill");
                *reinterpret_cast<uintptr_t*>(entry.task+8)=paused;
                const auto first_calls=owned_entry_first;
                check(sa_set_owned_vehicle_entry_wait(actor,entry.epoch,false)==std::optional<bool>(true),
                      "Resume clears only this accepted entry's wait policy");
                check(reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t,uintptr_t,uintptr_t)>(
                          slot_of(entry.table,10))(entry.task,0,actor.native_ped)==approach &&
                      owned_entry_first==first_calls+1 &&
                      sa_owned_vehicle_entry_epoch(entry.captured,actor)==std::optional<uint64_t>(entry.epoch),
                      "Active-task resume creates the original entry child under the same private vtable and epoch");
                destroy_task(entry,entry.task);sa_release_owned_vehicle_entry(actor,entry.epoch);
                owned_entry_free(paused);owned_entry_free(approach);owned_entry_subtask=0;
                owned_entry_child_abort_accepts=true;
            }
            // Foreign bank replacement cannot clear an already-held wait policy.
            {
                const auto entry=make_entry(0x05CB);
                check(sa_set_owned_vehicle_entry_wait(actor,entry.epoch,true)==std::optional<bool>(true),
                      "An exact owned entry queues a wait before its first child");
                const auto saved_record=entry_bank[127];entry_bank[127].task=uint32_t(saved_record.task+0x80);
                check(!sa_set_owned_vehicle_entry_wait(actor,entry.epoch,false).has_value(),
                      "A replacement in the script task bank cannot authorize resuming the old entry");
                entry_bank[127]=saved_record;
                const auto constructs_before=owned_entry_standstill_constructions;
                const auto paused=reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t,uintptr_t,uintptr_t)>(
                    slot_of(entry.table,9))(entry.task,0,actor.native_ped);
                check(paused && owned_entry_standstill_constructions==constructs_before+1,
                      "Rejected foreign replacement leaves the original wait policy intact");
                destroy_task(entry,entry.task);sa_release_owned_vehicle_entry(actor,entry.epoch);
                owned_entry_free(paused);
            }
            // Pause-only profiles do not gate ordinary ENTER_CAR construction or ownership.
            // Waiting still refuses before it changes the task policy if either native standstill pin differs.
            for(const auto address:{uintptr_t(0x62F310),uintptr_t(0x86DD2C)}) {
                auto* byte=reinterpret_cast<uint8_t*>(native_entity_address(address));const auto saved=*byte;*byte^=1;
                const auto entry=make_entry(0x05CB);
                const auto ready=sa_owned_vehicle_entry_wait_ready();
                const auto waiting=sa_set_owned_vehicle_entry_wait(actor,entry.epoch,true);
                *byte=saved;
                check(entry.task && !ready && !waiting.has_value(),
                      "A standstill constructor/vtable mismatch leaves ordinary entry usable but blocks pause policy");
                destroy_task(entry,entry.task);sa_release_owned_vehicle_entry(actor,entry.epoch);
            }
            // A changed standstill constructor profile makes waiting unavailable before policy changes.
            {
                const auto entry=make_entry(0x05CB);
                auto* byte=reinterpret_cast<uint8_t*>(native_entity_address(0x62F310));const auto saved=*byte;*byte^=1;
                const auto waiting=sa_set_owned_vehicle_entry_wait(actor,entry.epoch,true);
                *byte=saved;
                const auto first_before=owned_entry_first;
                const auto native_first=reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t,uintptr_t,uintptr_t)>(
                    slot_of(entry.table,9))(entry.task,0,actor.native_ped);
                check(!waiting.has_value() && owned_entry_first==first_before+1 &&
                      native_first==owned_entry_subtask,
                      "Unavailable native standstill data leaves the original entry policy unchanged");
                destroy_task(entry,entry.task);sa_release_owned_vehicle_entry(actor,entry.epoch);
            }
            // Cancellation of a running approach: URGENT abort once, then Control and CreateNext end the task.
            {
                const auto entry=make_entry(0x05CB);
                owned_entry_subtask=owned_entry_take();*reinterpret_cast<uintptr_t*>(owned_entry_subtask)=reinterpret_cast<uintptr_t>(owned_entry_subtask_table.data());
                *reinterpret_cast<uintptr_t*>(entry.task+8)=owned_entry_subtask;
                owned_entry_subtask_type=999;owned_entry_aborts=0;
                check(sa_cancel_owned_vehicle_entry(actor,entry.epoch)==std::optional<bool>(false) && owned_entry_aborts==0,
                      "Only the door-approach subtask (800) may be aborted");
                owned_entry_subtask_type=800;owned_entry_abort_accepts=false;
                const auto controls_before=owned_entry_controls;
                check(sa_cancel_owned_vehicle_entry(actor,entry.epoch)==std::optional<bool>(false) && owned_entry_aborts==1,"A refused URGENT abort is reported, not assumed");
                reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t,uintptr_t,uintptr_t)>(slot_of(entry.table,10))(entry.task,0,actor.native_ped);
                check(owned_entry_controls==controls_before+1,"A refused abort leaves SA's own control path untouched");
                owned_entry_abort_accepts=true;
                check(sa_cancel_owned_vehicle_entry(actor,entry.epoch)==std::optional<bool>(false) && owned_entry_aborts==2,"An accepted abort is recorded");
                check(sa_cancel_owned_vehicle_entry(actor,entry.epoch)==std::optional<bool>(false) && owned_entry_aborts==2,"The abort is never repeated");
                const auto controls_accepted=owned_entry_controls;const auto next_before=owned_entry_next;
                check(reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t,uintptr_t,uintptr_t)>(slot_of(entry.table,10))(entry.task,0,actor.native_ped)==0 &&
                      owned_entry_controls==controls_accepted,"After an accepted abort ControlSubTask ends the approach");
                check(reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t,uintptr_t,uintptr_t)>(slot_of(entry.table,8))(entry.task,0,actor.native_ped)==0 &&
                      owned_entry_next==next_before,"After an accepted abort CreateNextSubTask cannot chain a new approach");
                destroy_task(entry,entry.task);
                check(sa_cancel_owned_vehicle_entry(actor,entry.epoch)==std::optional<bool>(true),"The aborted task retires");
                sa_release_owned_vehicle_entry(actor,entry.epoch);owned_entry_free(owned_entry_subtask);owned_entry_subtask=0;
            }
            // Occupancy hands control to SA: a queued pre-start cancellation is withdrawn and nothing is aborted.
            {
                const auto entry=make_entry(0x05CB);
                check(sa_cancel_owned_vehicle_entry(actor,entry.epoch)==std::optional<bool>(false),"Queue a pre-start cancellation");
                ped[0x46D]|=1;*reinterpret_cast<uintptr_t*>(ped+0x58C)=reinterpret_cast<uintptr_t>(vehicles.data());
                const auto aborts_before=owned_entry_aborts;const auto first_before=owned_entry_first;owned_entry_subtask=0;
                check(sa_cancel_owned_vehicle_entry(actor,entry.epoch)==std::optional<bool>(false) && owned_entry_aborts==aborts_before,
                      "An occupied actor is never aborted out of the vehicle");
                reinterpret_cast<uintptr_t(__fastcall*)(uintptr_t,uintptr_t,uintptr_t)>(slot_of(entry.table,9))(entry.task,0,actor.native_ped);
                check(owned_entry_first==first_before+1,"The withdrawn request lets SA create its normal close-door/drive sequence");
                ped[0x46D]&=~1u;*reinterpret_cast<uintptr_t*>(ped+0x58C)=0;
                destroy_task(entry,entry.task);sa_release_owned_vehicle_entry(actor,entry.epoch);
            }
            // A bank slot reassigned to another task is never authority to abort anything.
            {
                const auto entry=make_entry(0x05CB);
                const auto saved_record=entry_bank[127];entry_bank[127].task=uint32_t(saved_record.task+0x80);
                owned_entry_subtask=owned_entry_take();*reinterpret_cast<uintptr_t*>(owned_entry_subtask)=reinterpret_cast<uintptr_t>(owned_entry_subtask_table.data());
                *reinterpret_cast<uintptr_t*>(entry.task+8)=owned_entry_subtask;const auto aborts_before=owned_entry_aborts;
                check(sa_cancel_owned_vehicle_entry(actor,entry.epoch)==std::optional<bool>(false) && owned_entry_aborts==aborts_before,
                      "A replacement task in the bank slot is never aborted");
                entry_bank[127]=saved_record;
                destroy_task(entry,entry.task);sa_release_owned_vehicle_entry(actor,entry.epoch);owned_entry_free(owned_entry_subtask);owned_entry_subtask=0;
            }
            // Discard of an unassigned private task: only before CreateFirst and with a single instance.
            {
                const auto discards_before=owned_entry_discards;
                const auto destroyed_before=owned_entry_destroyed;
                const auto fresh=owned_entry_take();
                native_owned_vehicle_entry_construct_driver(fresh,reinterpret_cast<uintptr_t>(vehicles.data()));
                const auto tracked_epoch=sa_track_private_vehicle_entry(fresh,actor,0x05CB);
                check(tracked_epoch!=0 && sa_discard_private_vehicle_entry(fresh,actor,0x05CB) && owned_entry_destroyed==destroyed_before+1,
                      "An untouched tracked task is destroyed through its own destructor");
                const auto untracked=owned_entry_take();
                native_owned_vehicle_entry_construct_passenger(untracked,reinterpret_cast<uintptr_t>(vehicles.data()),2,true);
                check(sa_discard_private_vehicle_entry(untracked,actor,0x05CA) && owned_entry_discards==discards_before+1,
                      "A constructed-but-untracked task is discarded through the retail destructor");
                check(!sa_track_private_vehicle_entry(owned_entry_take(),actor,0x05CB),"A task SA did not construct is never tracked");
            }
            // Foreign-vtable and thread authority.
            {
                const auto foreign=owned_entry_take();
                native_owned_vehicle_entry_construct_driver(foreign,reinterpret_cast<uintptr_t>(vehicles.data()));
                *reinterpret_cast<uintptr_t*>(foreign)=0x86E200;
                auto* table=reinterpret_cast<uintptr_t*>(native_entity_address(0x86E200));table[-1]=1;
                for(size_t i=0;i<12;++i)table[i]=native_owned_vehicle_entry_expected(i,0x05CB);table[0]=0xDEADBEEF; // another mod's slot
                check(!sa_track_private_vehicle_entry(foreign,actor,0x05CB),"A vtable whose slots are not the reviewed retail methods is never wrapped");
            }
            *pool_slot=saved_pool_slot;actor_reference_mode=saved_reference_mode_entry;
            entry_bank[127]=saved_entry_bank;ped[0x46D]=saved_entry_flags;*reinterpret_cast<uintptr_t*>(ped+0x58C)=saved_entry_vehicle;
        }
        const auto teleports_before=route_effect_teleports,ground_before=route_effect_ground_queries;
        auto warp=sa_route_warp_ped_near_entity_offscreen(*warp_actor,*warp_vehicle,100,0,true,effect_authorized,&route_effect_allowed);
        if(!warp.native.executed || !warp.teleported || route_effect_teleports!=teleports_before+10)
            std::cerr<<"warp: "<<warp.native.diagnostic<<" entered="<<warp.native.native_entered
                <<" teleports="<<route_effect_teleports-teleports_before<<" ground="<<route_effect_ground_queries-ground_before
                <<" x="<<*reinterpret_cast<float*>(ped+4)<<'\n';
        check(warp.native.executed && warp.native.native_entered && warp.teleported && warp.leave_car_deadline==3100 &&
              route_effect_teleports==teleports_before+10 && route_effect_ground_queries==ground_before+10 &&
              *reinterpret_cast<float*>(ped+4)==20.0f,
              "Literal source warp tests ten two-metre candidates, can teleport repeatedly, and returns original 3000ms timer");
        std::memcpy(ped+4,warp_origin,12);route_effect_world_blocked=true;
        warp=sa_route_warp_ped_near_entity_offscreen(*warp_actor,*warp_vehicle,100,0,true,effect_authorized,&route_effect_allowed);
        route_effect_world_blocked=false;
        check(warp.native.executed && !warp.native.native_entered && !warp.teleported && warp.leave_car_deadline==3100 &&
              route_effect_teleports==teleports_before+10 && *reinterpret_cast<float*>(ped+4)==0,
              "Blocked warp preserves position and still returns source timer after completing candidate loop");
        *teleport_slot=native_entity_address(0x5E7980);
        warp=sa_route_warp_ped_near_entity_offscreen(*warp_actor,*warp_vehicle,100,0,true,effect_authorized,&route_effect_allowed);
        check(!warp.native.executed && !warp.native.native_entered && !warp.leave_car_deadline &&
              route_effect_teleports==teleports_before+10,
              "Foreign native Teleport vtable slot is preserved without physical mutation or timer publication");
        *teleport_slot=saved_teleport_slot;*reinterpret_cast<uintptr_t*>(ped)=saved_ped_vtable;
        *ped_model_cell=saved_ped_model_info;*car_model_cell=saved_car_model_info;
        vehicles[0x36]=uint8_t(2|(8<<3));
        const auto enabled=sa_route_vehicle_snapshot(7);
        check(enabled && enabled->player_disabled==std::optional<bool>{false},
              "A different native vehicle status is not mistaken for player-disabled");
        vehicles[0x36]=uint8_t(2|(11<<3));

        actor_reference_mode=true;*reinterpret_cast<uintptr_t*>(vehicles.data()+0x24)=0;
        uintptr_t vehicle_weak=0;
        check(sa_register_vehicle_reference(7,&vehicle_weak) && sa_vehicle_reference_live(*vehicle,&vehicle_weak),
              "Vehicle objective target receives a live native weak reference");
        vehicle_flags[0]=8;
        check(!sa_vehicle_reference_live(*vehicle,&vehicle_weak),
              "Recycled vehicle handle generation cannot inherit an existing weak reference");
        vehicle_flags[0]=7;
        check(sa_vehicle_reference_live(*vehicle,&vehicle_weak),
              "Restoring the original generation preserves the still-registered weak cell");
        vehicle_weak=0;*reinterpret_cast<uintptr_t*>(vehicles.data()+0x24)=0;
        check(!sa_vehicle_reference_live(*vehicle,&vehicle_weak) && sa_release_vehicle_reference(*vehicle,&vehicle_weak),
              "Native-cleared vehicle reference cannot be resurrected by an identical handle/address");
        entity_references=saved_entity_references;actor_reference_mode=saved_reference_mode;

        auto* task_bank=reinterpret_cast<ScriptTaskRecord*>(native_entity_address(0xC0B1E8));
        ScriptTaskSnapshot saved_bank{};std::memcpy(saved_bank.data(),task_bank,sizeof(saved_bank));
        ScriptTaskSnapshot empty_bank{};std::memcpy(task_bank,empty_bank.data(),sizeof(empty_bank));
        NativeCall enter;enter.opcode=0x05CB;enter.inputs={27,7,-1};
        captured_vehicle_reply={};captured_vehicle_reply.executed=true;captured_vehicle_reply.native_entered=true;
        captured_vehicle_mode=0;const auto before_calls=captured_vehicle_calls;
        auto captured=sa_execute_vehicle_task_with_capture(enter);
        check(captured.executed && captured.native_entered && captured.accepted_script_task &&
              captured.task_capture==NativeResult::TaskCapture::captured &&
              captured_vehicle_calls==before_calls+1 && captured_vehicle_packet.inputs==enter.inputs,
              "Vehicle wrapper enters one reviewed task and captures only its actual native binding");
        std::memcpy(task_bank,empty_bank.data(),sizeof(empty_bank));captured_vehicle_mode=1;
        const auto before_missing_capture=captured_vehicle_calls;
        captured=sa_execute_vehicle_task_with_capture(enter);
        check(captured.executed && captured.native_entered && !captured.accepted_script_task &&
              captured.task_capture==NativeResult::TaskCapture::no_unique_record &&
              captured_vehicle_calls==before_missing_capture+1,
              "Missing advisory task capture retains the successful native receipt without replay");
        captured_vehicle_mode=0;captured_vehicle_reply={};
        std::memcpy(task_bank,saved_bank.data(),sizeof(saved_bank));
        vehicles=saved_vehicle;vehicle_flags=saved_vehicle_flags;
        std::memcpy(ped,saved_ped.data(),saved_ped.size());ped_flag=saved_ped_flag;
    }
    {
        profile(pe,0x61A5A0,11);profile(pe,0x668120,15);profile(pe,0x465C20,13);profile(pe,0x46FE60,19);
        std::array<uintptr_t,16> route_intelligence{};*reinterpret_cast<uintptr_t*>(ped+0x47C)=reinterpret_cast<uintptr_t>(route_intelligence.data());
        const auto previous_health=*reinterpret_cast<float*>(ped+0x540);*reinterpret_cast<float*>(ped+0x540)=100;
        const auto before_ped=std::vector<uint8_t>(ped,ped+0x7C4);
        RouteMotionRequest request{{27,reinterpret_cast<uintptr_t>(ped)},{{10,20,30}},4,true};
        auto* bank=reinterpret_cast<ScriptTaskRecord*>(native_entity_address(0xC0B1E8));const auto old=bank[23];
        for(int move:{4,6,7}) {
            bank[23]={};request.move_state=move;const auto allocations=route_allocations;
            const auto result=sa_create_route_motion(request);
            check(result.executed && result.native_entered && result.accepted_script_task && result.accepted_script_task->slot==23 &&
                route_allocations==allocations+1 && route_move==move && route_target==request.target && route_radius==0.8f && route_cleanup,
                "Native route factory constructs exactly one owned seek task with observed movement, copied XYZ, original radius and private cleanup context");
            check(std::memcmp(ped,before_ped.data(),before_ped.size())==0,"Factory never writes ped flags, mission locals or exports directly");
        }
        profile(pe,0x66DB80,30);
        struct Authorization {int calls=0,allowed=0;};
        request.authorized=[](void* context){auto& a=*static_cast<Authorization*>(context);return ++a.calls<=a.allowed;};
        for(int allowed:{0,1,2}) {
            Authorization authorization{0,allowed};request.authorization_context=&authorization;
            const auto assignments=route_assignments,allocations=route_allocations,discards=route_discards;
            const auto guarded=sa_create_route_motion(request);
            check(authorization.calls==(allowed?2:1),"Ownership is checked before entry and after native construction at the actual assignment boundary");
            check(route_assignments==assignments+(allowed==2) && guarded.executed==(allowed==2),"Lost ownership never overwrites another command's native task");
            check(route_allocations==allocations+(allowed!=0) && guarded.native_entered==(allowed!=0),"Pre-entry and entered refusal remain distinct and cannot be replayed interchangeably");
            check(route_discards==discards+(allowed==1),"Lost ownership after construction discards exactly the still-private unassigned task");
        }
        Authorization authorization{0,2};request.authorization_context=&authorization;
        auto* discard_byte=reinterpret_cast<uint8_t*>(native_entity_address(0x66DB80));const auto saved_discard=*discard_byte;*discard_byte^=1;
        auto guarded=sa_create_route_motion(request);*discard_byte=saved_discard;
        check(!guarded.native_entered && !authorization.calls,"Foreign disposal ABI refuses before calling authorization or allocating private tasks");
        request.authorized=nullptr;request.authorization_context=nullptr;
        {
            // Actual factory -> native bank capture -> task lifetime receipt.
            // Only native engine calls/table method destinations are mocked.
            profile(pe,0x86FDA4,48);
            auto* native_table=reinterpret_cast<uintptr_t*>(native_entity_address(0x86FDA8));
            std::array<uintptr_t,11> original_table{};std::copy_n(native_table,11,original_table.begin());
            check(original_table[0]==0x66DB80 && original_table[1]==0x66CEA0,"Original PE vtable supplies reviewed destructor/Clone ABI");
            native_table[0]=reinterpret_cast<uintptr_t>(&native_owned_route_destroy);
            native_table[1]=reinterpret_cast<uintptr_t>(&native_owned_route_clone);
            uint8_t generation=0;Pool task_pool{route_task.data(),&generation,1};pointer(0xB744A8,&task_pool);
            const auto old_foreign_seek=bank[22];
            const ScriptTaskRecord foreign_seek{0x05D3,0,0x765432,uint32_t(request.actor.native_ped),1};
            bank[22]=foreign_seek;
            bank[23]={};request.move_state=4;request.track_owned_lifetime=true;
            *reinterpret_cast<int32_t*>(ped+0x530)=16;
            const auto refused_tracking=sa_create_route_motion(request);
            check(!refused_tracking.executed && !refused_tracking.native_entered,"Tracked publication requires explicit live source authorization before native allocation");
            request.authorized=[](void*){return true;};
            *reinterpret_cast<int32_t*>(ped+0x530)=6;
            const auto foreign_slot=sa_create_route_motion(request);
            const CapturedScriptTask foreign_binding{22,foreign_seek};
            check(foreign_slot.executed && foreign_slot.accepted_script_task && foreign_slot.accepted_script_task->slot==23 &&
                sa_owned_route_binding_epoch(*foreign_slot.accepted_script_task,request.actor).has_value() &&
                !sa_owned_route_binding_epoch(foreign_binding,request.actor).has_value() && bank[22]==foreign_seek &&
                *reinterpret_cast<int32_t*>(ped+0x530)==6,
                "SA factory installs and tracks a fresh route task from native SEEK_POS while leaving the foreign bank record and CPed state untouched");
            const auto foreign_wrapper=*reinterpret_cast<const uintptr_t*>(route_task.data());
            reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t,uint32_t)>(*reinterpret_cast<const uintptr_t*>(foreign_wrapper))(
                reinterpret_cast<uintptr_t>(route_task.data()),1);
            check(!sa_owned_route_binding_epoch(*foreign_slot.accepted_script_task,request.actor),
                "Fresh SEEK_POS assignment lifetime is revoked by its actual native destructor callback");
            *reinterpret_cast<int32_t*>(ped+0x530)=16;
            const auto tracked=sa_create_route_motion(request);
            check(tracked.executed && tracked.accepted_script_task &&
                sa_owned_route_binding_epoch(*tracked.accepted_script_task,request.actor).has_value() &&
                *reinterpret_cast<int32_t*>(ped+0x530)==16,"Real owned route factory accepts on-foot ATTACK and publishes its task epoch without writing CPed state");
            const auto first_epoch=sa_owned_route_binding_epoch(*tracked.accepted_script_task,request.actor);
            const auto wrapper=*reinterpret_cast<const uintptr_t*>(route_task.data());
            reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t,uint32_t)>(*reinterpret_cast<const uintptr_t*>(wrapper))(
                reinterpret_cast<uintptr_t>(route_task.data()),1);
            check(!sa_owned_route_binding_epoch(*tracked.accepted_script_task,request.actor),"Actual destructor callback revokes captured bank record even while its raw bytes are unchanged");
            const auto reused_bytes=bank[23];
            *reinterpret_cast<int32_t*>(ped+0x530)=22;
            const auto reassigned=sa_create_route_motion(request);
            check(reassigned.executed && reassigned.accepted_script_task && bank[23]==reused_bytes &&
                sa_owned_route_binding_epoch(*reassigned.accepted_script_task,request.actor)!=first_epoch &&
                *reinterpret_cast<int32_t*>(ped+0x530)==22,
                "Real owned route factory also accepts on-foot AIM_GUN and preserves CPed state across byte-identical task reuse");
            const auto replacement_wrapper=*reinterpret_cast<const uintptr_t*>(route_task.data());
            reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t,uint32_t)>(*reinterpret_cast<const uintptr_t*>(replacement_wrapper))(
                reinterpret_cast<uintptr_t>(route_task.data()),1);
            *reinterpret_cast<int32_t*>(ped+0x530)=0;
            Authorization lost_during_tracking{0,2};request.authorization_context=&lost_during_tracking;
            request.authorized=[](void* context){auto& gate=*static_cast<Authorization*>(context);return ++gate.calls<=gate.allowed;};
            const auto assigned_before=route_assignments,discarded_before=route_discards;
            const auto refused_after_tracking=sa_create_route_motion(request);
            check(!refused_after_tracking.executed && refused_after_tracking.native_entered && route_assignments==assigned_before && route_discards==discarded_before+1,
                "Source authority is checked again AFTER tracking allocation; only our still-private wrapped task is destroyed when it is lost");
            request.track_owned_lifetime=false;request.authorized=nullptr;request.authorization_context=nullptr;pointer(0xB744A8,nullptr);
            std::copy(original_table.begin(),original_table.end(),native_table);bank[23]={};bank[22]=old_foreign_seek;
        }
        // Compose the real pose and factory with a lifecycle model; no game
        // hooks are installed by this isolated test process.
        struct Intent final:RouteIntentReader {
            RouteSourceIntent value;bool available=true;
            std::optional<RouteSourceIntent> route_intent(ObjectiveActor)override{return available?std::optional<RouteSourceIntent>(value):std::nullopt;}
        } intent;
        struct Host {
            std::optional<EpochScriptTask> owned;uint64_t serial=20;size_t accepts=0;
            std::optional<EpochScriptTask> refresh(const EpochScriptTask& expected) {
                return owned && owned->epoch==expected.epoch && owned->binding.slot==expected.binding.slot?owned:std::nullopt;
            }
            std::optional<EpochScriptTask> accept(const CapturedScriptTask& captured) {
                ++accepts;owned=EpochScriptTask{captured,++serial};return owned;
            }
        } host;
        const auto actor=request.actor;
        const EpochScriptTask existing{{22,{0x05D3,0,0x1234,uint32_t(actor.native_ped),1}},19};
        host.owned=existing;intent.value={actor,1,true,false,false};
        profile(pe,0x5E3960,38);profile(pe,0x4012D0,11);
        RouteNativeFunctions functions;functions.active=[](const ObjectiveActor&,const CapturedScriptTask&)->std::optional<bool>{return true;};
        NativeRouteConsumerBackend<Host> backend(host,intent,1,functions);
        auto observed=backend.observe(actor,existing);
        check(observed && observed->current_route && observed->controllable && observed->on_foot && observed->motion_active,"Native route backend composes live pose, observed binding and authoritative source intent");
        const auto writes=route_assignments;
        intent.available=false;check(!backend.observe(actor,existing) && !backend.replace(actor,existing,{40,50,60},false,true).entered,"No invented source intent or movement when source ledger is unavailable");intent.available=true;
        intent.value.temporary=true;
        check(backend.observe(actor,existing)->temporary && !backend.replace(actor,existing,{40,50,60},false,true).entered && route_assignments==writes,"Temporary source objective suspends movement without taking native task ownership");intent.value.temporary=false;
        static Host* changing_host=nullptr;changing_host=&host;
        route_during_target=[](){changing_host->owned.reset();};
        const auto before_discards=route_discards;
        auto replaced=backend.replace(actor,existing,{40,50,60},false,true);route_during_target=nullptr;
        check(replaced.entered && !replaced.executed && !replaced.accepted && route_assignments==writes && route_discards==before_discards+1,"Observer loss during real native construction prevents assignment and disposes the private task without replay");
        host.owned=existing;
        static Intent* changing_intent=nullptr;changing_intent=&intent;
        route_during_target=[](){++changing_intent->value.revision;};
        replaced=backend.replace(actor,existing,{40,50,60},false,true);route_during_target=nullptr;
        check(replaced.entered && !replaced.executed && route_assignments==writes,"Same-looking source objective with a new revision cannot receive an old route's replacement");
        check(!backend.observe(actor,existing) && !backend.replace(actor,existing,{40,50,60},false,true).entered,"Old source revision cannot resume on a later tick even if native task bytes match");
        // A separately accepted source command would construct a new backend;
        // this lifecycle fixture supplies its new source revision explicitly.
        NativeRouteConsumerBackend<Host> restored(host,intent,intent.value.revision,functions);
        bank[23]={};replaced=restored.replace(actor,existing,{40,50,60},false,true);
        check(replaced.executed && replaced.entered && replaced.accepted && host.accepts==1 && route_move==4 && route_cleanup,"Actual factory capture passes through lifecycle acceptance exactly once with source walking and cleanup");
        host.owned=existing;intent.value.running=true;bank[23]={};
        check(!restored.replace(actor,existing,{40,50,60},false,true).entered,"Changed source running preference refuses stale movement requests");
        replaced=restored.replace(actor,existing,{40,50,60},true,false);
        check(replaced.executed && replaced.accepted && route_move==6 && !route_cleanup,"Source running translates to native RUN, not the ped's possibly idle current move state");

        OriginalRouteIntents source_intents;OriginalRouteTable source_routes;
        const auto actual_pose=sa_route_actor_snapshot(actor);check(bool(actual_pose),"Real native source pose available for route composition");
        auto next_point=actual_pose->position;next_point[0]+=10.f;
        check(source_routes.add(7,{0,0,0}) && source_routes.add(7,actual_pose->position) && source_routes.add(7,next_point),"Actual source route table contains pose and next target");
        check(source_intents.initialized(actor) && source_intents.accepted(source_intents.follow(actor,7,1,0,source_routes)),"Commit genuine source initialization and reviewed route transition");
        host.owned=existing;bank[23]={};
        NativeRouteConsumerBackend<Host> composed(host,source_intents,source_intents.state(actor)->revision,functions);
        auto live_route=OriginalRouteConsumer::accepted(*source_intents.state(actor)->cursor,actor,existing,true);
        const auto source_assignments=route_assignments;
        check(live_route && live_route->tick(source_routes,composed)==OriginalRouteConsumer::Result::advanced &&
            route_assignments==source_assignments+1 && route_target==next_point && route_radius==0.8f &&
            route_move==4 && route_cleanup,"Real source ledger and route consumer feed actual native pose, factory, target setter, assignment and capture");
        const auto saved_cursor=live_route->cursor();
        OriginalRouteIntents::VehicleAdmission interruption;
        interruption.source_state=0;interruption.target={42,0x234500,42};
        interruption.target_boat=false;interruption.target_bus=false;
        check(source_intents.accepted(source_intents.vehicle_interruption(actor,18,interruption)),"Accepted vehicle interruption suspends actual source route");
        check(live_route->tick(source_routes,composed)==OriginalRouteConsumer::Result::unavailable && route_assignments==source_assignments+1,"Source intent change revokes prior native route authority immediately");
        check(source_intents.accepted(source_intents.restored(actor)),"Actual restoration commits previous route source intent");
        check(saved_cursor.passed()==1 && saved_cursor.target()==next_point && source_intents.state(actor)->current==30,"Restoration retains the real advanced cursor instead of reinitializing from a stale first target");
        host.owned=existing;bank[23]={};
        NativeRouteConsumerBackend<Host> resumed_backend(host,source_intents,source_intents.state(actor)->revision,functions);
        static OriginalRouteIntents* changing_source=nullptr;changing_source=&source_intents;
        static ObjectiveActor changing_actor;changing_actor=actor;
        route_during_target=[](){changing_source->retired(changing_actor);};
        replaced=resumed_backend.replace(actor,existing,next_point,false,true);route_during_target=nullptr;
        check(replaced.entered && !replaced.executed && route_assignments==source_assignments+1,"Real source retirement during target construction blocks native assignment at its actual write boundary");

        request.move_state=3;auto result=sa_create_route_motion(request);
        check(!result.executed && !result.native_entered,"Unreviewed movement cannot be silently replaced with walking");request.move_state=4;
        request.target[1]=std::numeric_limits<float>::infinity();result=sa_create_route_motion(request);
        check(!result.executed && !result.native_entered,"Nonfinite target refuses before native allocation");request.target[1]=20;
        ++request.actor.native_ped;result=sa_create_route_motion(request);
        check(!result.executed && !result.native_entered,"Recycled or mismatched actor identity cannot receive a route task");--request.actor.native_ped;
        for(uint8_t flag:{uint8_t(1),uint8_t(2),uint8_t(4)}) {
            ped[0x46D]=flag;result=sa_create_route_motion(request);
            check(!result.executed && !result.native_entered,"Vehicle/airborne/landing state cannot cause automatic route exit or movement");
        }
        ped[0x46D]=0;
        for(int32_t state:{11,54,55,63}) {
            *reinterpret_cast<int32_t*>(ped+0x530)=state;result=sa_create_route_motion(request);
            check(!result.executed && !result.native_entered,"FOLLOW_PATH, dying, dead or arrested actor cannot receive a new route seek task");
        }
        *reinterpret_cast<int32_t*>(ped+0x530)=0;
        *reinterpret_cast<float*>(ped+0x540)=0;result=sa_create_route_motion(request);
        check(!result.executed && !result.native_entered,"Zero health refuses before allocation");*reinterpret_cast<float*>(ped+0x540)=100;
        const auto previous_policy=*reinterpret_cast<uint32_t*>(ped+0x474);
        *reinterpret_cast<uint32_t*>(ped+0x474)|=0x800000u;result=sa_create_route_motion(request);
        check(!result.executed && !result.native_entered,"Script-brain rejection cannot silently delete the task while reporting route assignment");
        *reinterpret_cast<uint32_t*>(ped+0x474)=previous_policy;
        for(uintptr_t address:{uintptr_t(0x61A5A0),uintptr_t(0x668120),uintptr_t(0x465C20),uintptr_t(0x46FE60)}) {
            auto* byte=reinterpret_cast<uint8_t*>(native_entity_address(address));const auto saved=*byte;*byte^=1;
            result=sa_create_route_motion(request);check(!result.executed && !result.native_entered,"Foreign native factory ABI is refused without rewriting hooks");*byte=saved;
        }
        for(int fault:{1,2,3,5}) {
            route_fault=fault;const auto allocations=route_allocations;result=sa_create_route_motion(request);
            check(!result.executed && result.native_entered && route_allocations==allocations+1,"Allocation/construction/assignment failures retain entry receipt and do not replay the factory");
        }
        route_fault=4;bank[23]={};result=sa_create_route_motion(request);route_fault=0;
        check(result.executed && result.native_entered && !result.accepted_script_task,"Successful native assignment without capture is not replayed or fabricated as ownership");
        bank[23]=old;*reinterpret_cast<uintptr_t*>(ped+0x47C)=0;*reinterpret_cast<float*>(ped+0x540)=previous_health;
    }
    {
        profile(pe,0x5F7E40,55);profile(pe,0x5F7E80,87);profile(pe,0x401881,10);profile(pe,0x5F69A0,4);profile(pe,0x483933,38);
        auto* active=reinterpret_cast<uint8_t*>(native_entity_address(0xC098E0));
        auto* groups=reinterpret_cast<uint8_t*>(native_entity_address(0xC09920));
        const auto saved_active=std::vector<uint8_t>(active,active+8),saved_groups=std::vector<uint8_t>(groups,groups+8*0x2D4);
        std::memset(active,0,8);std::memset(groups,0,8*0x2D4);
        const ObjectiveActor actor{27,reinterpret_cast<uintptr_t>(ped)};
        auto descending=sa_peds_descending();
        for(const auto spec:std::array<std::pair<uintptr_t,size_t>,4>{{{0x59AD70,0x54},{0x7F18A0,12},{0x532B00,18},{0x7F0910,0x7D}}})profile(pe,spec.first,spec.second);
        check(sa_phone_render_profile_matches(),"Complete native matrix copy/flag update/entity frame/RW frame bodies are reviewed before visual effects");
        for(uintptr_t address:{uintptr_t(0x59AD70),uintptr_t(0x7F18A0),uintptr_t(0x532B00),uintptr_t(0x7F0910)}) {
            auto* code=reinterpret_cast<uint8_t*>(native_entity_address(address));const auto saved=*code;*code^=1;
            check(!sa_phone_render_profile_matches(),"Changed render service refuses before booth/RNG mutations");*code=saved;
        }
        profile(pe,0x404090,11);profile(pe,0x403FF4,14);
        std::array<uint8_t,0x38*2> booth{};uint8_t booth_tag=27;
        std::array<uintptr_t,3> booth_pool{reinterpret_cast<uintptr_t>(booth.data()),reinterpret_cast<uintptr_t>(&booth_tag),1};
        auto* booth_global=reinterpret_cast<uintptr_t*>(native_entity_address(0xB74498));const auto saved_booths=*booth_global;*booth_global=reinterpret_cast<uintptr_t>(booth_pool.data());
        booth[0x36]=1;*reinterpret_cast<uint16_t*>(booth.data()+0x22)=10000;*reinterpret_cast<uint16_t*>(booth.data()+0x20)=41;
        auto live_booth=sa_phone_booth_snapshot(10000,{0,0,0});
        check(live_booth && live_booth->entity==reinterpret_cast<uintptr_t>(booth.data()) && live_booth->handle==27 && live_booth->seed==41 && !live_booth->matrix && !live_booth->rw_object,"Live booth identity preserves generation/model/seed and distinguishes missing renderer");
        std::array<uint8_t,0x48> booth_matrix{};*reinterpret_cast<float*>(booth_matrix.data()+0x28)=1.05f;
        *reinterpret_cast<uintptr_t*>(booth.data()+0x14)=reinterpret_cast<uintptr_t>(booth_matrix.data());
        *reinterpret_cast<uintptr_t*>(booth.data()+0x18)=0x1234;
        live_booth=sa_phone_booth_snapshot(10000,{0,0,0});
        check(live_booth && live_booth->up_z==1.05f && live_booth->rw_object==0x1234,"Booth snapshot reads actual matrix/RW identity without granting an arbitrary render call");
        std::array<uint8_t,0x10> booth_atomic{};std::array<uint8_t,0xA4> booth_frame{};std::array<uint8_t,0xC4> booth_engine{};
        booth_atomic[0]=1;*reinterpret_cast<uintptr_t*>(booth_atomic.data()+4)=reinterpret_cast<uintptr_t>(booth_frame.data());
        *reinterpret_cast<uintptr_t*>(booth_frame.data()+0xA0)=reinterpret_cast<uintptr_t>(booth_frame.data());
        const auto dirty_anchor=reinterpret_cast<uintptr_t>(booth_engine.data()+0xBC);
        *reinterpret_cast<uintptr_t*>(dirty_anchor)=dirty_anchor;*reinterpret_cast<uintptr_t*>(dirty_anchor+4)=dirty_anchor;
        auto* render_global=reinterpret_cast<uintptr_t*>(native_entity_address(0xC97B24));const auto saved_engine=*render_global;*render_global=reinterpret_cast<uintptr_t>(booth_engine.data());
        *reinterpret_cast<uintptr_t*>(booth.data()+0x18)=reinterpret_cast<uintptr_t>(booth_atomic.data());live_booth=sa_phone_booth_snapshot(10000,{0,0,0});
        check(live_booth && sa_phone_render_snapshot(*live_booth),"Reviewed atomic/frame/root/dirty-list linkage provides native render preflight");
        for(int fault:{0,1,2,3}) {
            std::memset(booth_frame.data(),0,booth_frame.size());*reinterpret_cast<uintptr_t*>(booth_frame.data()+0xA0)=reinterpret_cast<uintptr_t>(booth_frame.data());
            *reinterpret_cast<uintptr_t*>(dirty_anchor)=dirty_anchor;*reinterpret_cast<uintptr_t*>(dirty_anchor+4)=dirty_anchor;
            *reinterpret_cast<float*>(booth_matrix.data()+0x28)=1.05f;
            live_booth=sa_phone_booth_snapshot(10000,{0,0,0});const auto visual=sa_phone_render_snapshot(*live_booth);check(visual.has_value(),"Visual effect fixture preflights actual render graph");
            phone_visual_fault=fault;const auto frames=phone_frame_calls;const auto applied=sa_phone_apply_visual(*visual,1.02f);
            check(applied.native_entered && applied.executed==(fault==0) && *reinterpret_cast<float*>(booth_matrix.data()+0x28)==1.02f,"Typed visual effect preserves partial matrix/copy/frame failures without replay");
            if(fault==1 || fault==2)check(phone_frame_calls==frames,"Copy failure or root replacement stops before frame refresh");
        }
        phone_visual_fault=0;std::memset(booth_frame.data(),0,booth_frame.size());*reinterpret_cast<uintptr_t*>(booth_frame.data()+0xA0)=reinterpret_cast<uintptr_t>(booth_frame.data());
        *reinterpret_cast<uintptr_t*>(dirty_anchor)=dirty_anchor;*reinterpret_cast<uintptr_t*>(dirty_anchor+4)=dirty_anchor;
        live_booth=sa_phone_booth_snapshot(10000,{0,0,0});const auto staged=sa_phone_render_snapshot(*live_booth);
        const auto staged_copies=phone_matrix_calls,staged_frames=phone_frame_calls;
        auto cpu_effect=sa_phone_write_up_z(*staged,1.03f);
        check(cpu_effect.executed && cpu_effect.native_entered && phone_matrix_calls==staged_copies && phone_frame_calls==staged_frames,"CPU phone stage leaves native renderer untouched so original ring audio can occur between stages");
        live_booth=sa_phone_booth_snapshot(10000,{0,0,0});const auto after_cpu=sa_phone_render_snapshot(*live_booth);
        auto refresh=sa_phone_refresh_visual(*after_cpu);
        check(refresh.executed && refresh.native_entered && phone_matrix_calls==staged_copies+1 && phone_frame_calls==staged_frames+1 && *reinterpret_cast<float*>(booth_matrix.data()+0x28)==1.03f,"Separate RW stage copies accepted CPU pose without rewriting or replaying it");
        *reinterpret_cast<uintptr_t*>(dirty_anchor+4)=0;check(!sa_phone_render_snapshot(*live_booth),"Broken renderer list refuses before matrix/RNG effects");*reinterpret_cast<uintptr_t*>(dirty_anchor+4)=dirty_anchor;
        booth_frame[3]=3;check(!sa_phone_render_snapshot(*live_booth),"Already dirty root requires valid reciprocal list links");booth_frame[3]=0;
        booth_atomic[0]=2;check(!sa_phone_render_snapshot(*live_booth),"Unreviewed clump is not treated as a building atomic");booth_atomic[0]=1;
        *render_global=saved_engine;
        *reinterpret_cast<float*>(booth_matrix.data()+0x28)=std::numeric_limits<float>::quiet_NaN();
        check(!sa_phone_booth_snapshot(10000,{0,0,0}),"Corrupt booth transform does not become absent streaming");
        *reinterpret_cast<float*>(booth_matrix.data()+0x28)=1.f;
        std::array<uint8_t,2> duplicate_tags{27,28};std::memcpy(booth.data()+0x38,booth.data(),0x38);
        booth_pool[1]=reinterpret_cast<uintptr_t>(duplicate_tags.data());booth_pool[2]=2;
        check(!sa_phone_booth_snapshot(10000,{0,0,0}),"Duplicate live canonical placements refuse arbitrary booth selection");
        booth_pool[1]=reinterpret_cast<uintptr_t>(&booth_tag);booth_pool[2]=1;
        booth_tag|=0x80;live_booth=sa_phone_booth_snapshot(10000,{0,0,0});
        check(live_booth && !live_booth->entity,"Unallocated booth is genuine absent streaming, not unknown ABI");booth_tag=27;
        booth[0x36]=2;check(!sa_phone_booth_snapshot(10000,{0,0,0}),"Foreign building-pool entity type cannot acquire booth ownership");booth[0x36]=1;
        auto* booth_code=reinterpret_cast<uint8_t*>(native_entity_address(0x404090));const auto booth_byte=*booth_code;*booth_code^=1;
        check(!sa_phone_booth_snapshot(10000,{0,0,0}),"Foreign building pool ABI cannot fabricate absent streaming");*booth_code=booth_byte;*booth_global=saved_booths;
        profile(pe,0x821B1E,34);
        profile(pe,0x827B3D,113);
        phone_tls_ready=false;const auto unready_calls=phone_random_calls;
        check(!sa_phone_random_ready() && !sa_phone_random_sample().native_entered && phone_random_calls==unready_calls,"Missing original CRT TLS context refuses before RNG and never allocates/fabricates a seed");phone_tls_ready=true;
        for(int fault:{0,1,2}) {
            phone_random_fault=fault;const auto calls=phone_random_calls;const auto sample=sa_phone_random_sample();
            check(sample.native_entered && sample.executed==(fault==0) && phone_random_calls==calls+1,"Native RNG returns actual sample and retains exception/range-failure entry without replay");
        }
        phone_random_fault=0;auto* rng_code=reinterpret_cast<uint8_t*>(native_entity_address(0x821B1E));const auto rng_byte=*rng_code;
        *rng_code^=1;const auto rng_calls=phone_random_calls;const auto missing_rng=sa_phone_random_sample();
        check(!missing_rng.executed && !missing_rng.native_entered && phone_random_calls==rng_calls,"Foreign RNG profile cannot produce an invented sample");*rng_code=rng_byte;
        profile(pe,0x4090A0,30);
        auto* camera_slot=reinterpret_cast<uintptr_t*>(native_entity_address(0xB6F03C));const auto saved_camera=*camera_slot;
        std::array<float,16> camera_matrix{};camera_matrix[12]=10;camera_matrix[13]=-20;camera_matrix[14]=30;
        *camera_slot=reinterpret_cast<uintptr_t>(camera_matrix.data());
        check(sa_camera_placeable_position()==std::array<float,3>{10,-20,30},"Camera reader follows actual placeable matrix rather than debug/active CCam slots");
        camera_matrix[2]=-1;
        const auto phone_listener=sa_phone_camera_listener();
        check(phone_listener && phone_listener->right==std::array<float,3>{0,0,-1} && phone_listener->position==std::array<float,3>{10,-20,30},"Phone listener copies coherent actual placeable matrix position and pitched right basis");
        camera_matrix[2]=0;check(!sa_phone_camera_listener(),"Degenerate phone camera basis cannot invent stereo orientation");
        *camera_slot=0;check(!sa_phone_camera_listener(),"Position-only camera fallback does not fabricate a right basis");
        *camera_slot=reinterpret_cast<uintptr_t>(camera_matrix.data());
        camera_matrix[12]=std::numeric_limits<float>::quiet_NaN();check(!sa_camera_placeable_position(),"Unknown camera coordinates cannot invent phone visibility");
        *camera_slot=1;check(!sa_camera_placeable_position(),"Unreadable camera matrix is contained before phone effects");*camera_slot=saved_camera;
        profile(pe,0x561AA0,25);profile(pe,0x561C5D,14);
        profile(pe,0x53C0FB,9);profile(pe,0x53C109,2);profile(pe,0x53C145,19);
        for(uintptr_t site:{0x53C095u,0x53C104u,0x53C10Bu,0x53C136u,0x53C13Bu,0x53C140u})profile(pe,site,5);
        auto phase_match=[](uintptr_t address,std::initializer_list<uint8_t> bytes){return std::memcmp(reinterpret_cast<const void*>(native_entity_address(address)),bytes.begin(),bytes.size())==0;};
        check(original_phone_phase_profile(phase_match),"Reviewed late camera/world/shadows context accepts phone phase");
        auto* phase_call=reinterpret_cast<uint8_t*>(native_entity_address(0x53C140));const auto old_target_byte=phase_call[1];
        phase_call[1]^=1;check(original_phone_phase_profile(phase_match),"Existing chained mod CALL target is preserved rather than forced back to retail");phase_call[1]=old_target_byte;
        auto* phase_branch=reinterpret_cast<uint8_t*>(native_entity_address(0x53C109));const auto old_branch=*phase_branch;
        *phase_branch^=1;check(!original_phone_phase_profile(phase_match),"Foreign camera control flow refuses late phone hook");*phase_branch=old_branch;
        profile(pe,0x561AD0,25);profile(pe,0x858624,4);
        auto* time_scale=reinterpret_cast<float*>(native_entity_address(0xB7CB64));const auto saved_scale=*time_scale;
        *time_scale=0.5f;check(sa_phone_slow_motion()==true,"Phone slow-motion reader follows actual native time-scale predicate");
        *time_scale=1.f;check(sa_phone_slow_motion()==false,"Genuine normal speed remains available false");
        *time_scale=std::numeric_limits<float>::quiet_NaN();check(!sa_phone_slow_motion(),"Unknown time scale cannot invent phone playback frequency");*time_scale=saved_scale;
        auto clock=sa_frame_clock();check(clock.has_value(),"Reviewed native clock/history/frame ABI provides an actual snapshot including genuine zero");
        auto* clock_code=reinterpret_cast<uint8_t*>(native_entity_address(0x561AA0));const auto clock_byte=*clock_code;
        *clock_code^=1;check(!sa_frame_clock(),"Foreign clock history writer does not invent a previous-frame time");*clock_code=clock_byte;
        profile(pe,0x56E010,0x7C);
        const auto original_focus=*reinterpret_cast<uint8_t*>(native_entity_address(0xB7CD74));
        *reinterpret_cast<uint8_t*>(native_entity_address(0xB7CD74))=0;
        auto player_position=sa_focused_player_position();
        check(player_position.has_value(),"Focused player coordinates read genuine native ped transform");
        const auto coordinate_ped=std::vector<uint8_t>(ped,ped+0x7C4);const auto coordinate_vehicle=vehicles;
        *reinterpret_cast<uintptr_t*>(ped+0x14)=0;*reinterpret_cast<float*>(ped+0xC)=2;
        *reinterpret_cast<uintptr_t*>(vehicles.data()+0x14)=0;*reinterpret_cast<float*>(vehicles.data()+0xC)=37;
        ped[0x46D]|=1;*reinterpret_cast<uintptr_t*>(ped+0x58C)=reinterpret_cast<uintptr_t>(vehicles.data());
        player_position=sa_focused_player_position();check(player_position && (*player_position)[2]==37,"In-vehicle player uses vehicle height rather than ped height");
        *reinterpret_cast<uintptr_t*>(ped+0x58C)=1;check(!sa_focused_player_position(),"Stale player vehicle cannot fabricate query height");
        ped[0x46D]&=~1u;*reinterpret_cast<uintptr_t*>(ped+0x58C)=0;*reinterpret_cast<float*>(ped+0xC)=std::numeric_limits<float>::quiet_NaN();
        check(!sa_focused_player_position(),"Invalid player coordinate is unavailable");
        std::memcpy(ped,coordinate_ped.data(),coordinate_ped.size());vehicles=coordinate_vehicle;
        *reinterpret_cast<uint8_t*>(native_entity_address(0xB7CD74))=2;
        check(!sa_focused_player_position(),"Invalid player focus never fabricates zero height");
        *reinterpret_cast<uint8_t*>(native_entity_address(0xB7CD74))=original_focus;
        check(descending && !descending->empty() && descending->back()==actor,"Native descending ped enumeration includes live pool slot zero with generation identity");
        auto state=sa_ped_group_snapshot(actor);check(state && !state->member && !state->leader && !state->leader_ped,"Unassigned ped has no invented group/leader");
        active[2]=1;*reinterpret_cast<uintptr_t*>(groups+2*0x2D4+0x28)=actor.native_ped;
        state=sa_ped_group_snapshot(actor);check(state && state->member && state->leader && state->leader_ped==actor.native_ped,"Exact active native leader slot is recognized, without using bloody-footprint flag");
        active[2]=0;state=sa_ped_group_snapshot(actor);check(state && !state->member,"Inactive group bytes do not grant current membership");
        active[2]=1;*reinterpret_cast<uintptr_t*>(groups+2*0x2D4+0x28)=0;*reinterpret_cast<uintptr_t*>(groups+2*0x2D4+0xC)=actor.native_ped;
        state=sa_ped_group_snapshot(actor);check(state && state->member && !state->leader && !state->leader_ped,"Follower and leaderless group are distinct native facts, not a fabricated source leader flag");
        *reinterpret_cast<uintptr_t*>(groups+2*0x2D4+0x28)=1;
        check(!sa_ped_group_snapshot(actor),"Dangling group leader is unavailable, not an ungrouped ped eligible for theft");
        *reinterpret_cast<uintptr_t*>(groups+2*0x2D4+0x28)=0;active[3]=1;*reinterpret_cast<uintptr_t*>(groups+3*0x2D4+0xC)=actor.native_ped;
        check(!sa_ped_group_snapshot(actor),"Duplicate active memberships are rejected");
        for(uintptr_t address:{uintptr_t(0x5F7E40),uintptr_t(0x5F7E80),uintptr_t(0x401881),uintptr_t(0x5F69A0),uintptr_t(0x483933)}) {
            auto* byte=reinterpret_cast<uint8_t*>(native_entity_address(address));const auto saved=*byte;*byte^=1;
            check(!sa_ped_group_snapshot(actor),"Foreign group layout/getter/trampoline profile is not called or patched");*byte=saved;
        }
        active[2]=active[3]=0;
        {
            // 804 through the real backend and the same native group reader, with two live pool peds.
            const auto pool_flags=ped_pool.flags;const auto pool_count=ped_pool.count;
            uint8_t flags2[2]={ped_flag,28};ped_pool.flags=flags2;ped_pool.count=2;
            uint8_t* ped2=ped+0x7C4;std::memset(ped2,0,0x7C4);ped2[0x36]=3;
            const auto follower=reinterpret_cast<uintptr_t>(ped),leader=reinterpret_cast<uintptr_t>(ped2);
            const auto saved_groups_804=std::vector<uint8_t>(groups,groups+8*0x2D4),saved_ped_page=std::vector<uint8_t>(ped,ped+0x1000);
            std::memset(groups,0,8*0x2D4);std::memset(active,0,8);
            auto* membership=reinterpret_cast<uintptr_t*>(groups+2*0x2D4+0xC);auto* group_leader=reinterpret_cast<uintptr_t*>(groups+2*0x2D4+0x28);
            // Read-only contract: neither the groups nor the NPC page may change, and no engine entry is reported.
            auto query=[&](int32_t who,int32_t lead){
                const auto g0=std::vector<uint8_t>(groups,groups+8*0x2D4),a0=std::vector<uint8_t>(active,active+8),p0=std::vector<uint8_t>(ped,ped+0x1000);
                auto c=operation(EntityCommand::char_in_chars_group,{who,lead});c.predicate=true;const auto r=sa_execute_entity_command(c);
                check(!r.native_entered && std::equal(g0.begin(),g0.end(),groups) && std::equal(a0.begin(),a0.end(),active) && std::equal(p0.begin(),p0.end(),ped),
                      "Group membership query mutates no group, NPC or task state");
                return r;
            };
            check(query(27,284).executed && !query(27,284).condition,"No group at all is a proven false, not a failed read");
            active[2]=1;*membership=follower;*group_leader=leader;
            auto r=query(27,284);check(r.executed && r.condition,"Non-leader member of the named native leader's group is true");
            r=query(284,27);check(r.executed && !r.condition,"Leader is not a member of the follower's group");
            r=query(27,27);check(r.executed && !r.condition,"Member of someone else's group is not in its own");
            *group_leader=0;r=query(27,284);check(r.executed && !r.condition,"Leaderless group does not match any leader");
            *group_leader=follower;r=query(27,284);check(r.executed && !r.condition,"The first character being the leader itself is false");
            r=query(27,27);check(r.executed && !r.condition,"Leader querying itself as leader is false");
            *group_leader=leader;active[2]=0;r=query(27,284);check(r.executed && !r.condition,"Inactive group bytes give a proven false");active[2]=1;
            for(const auto ids:{std::pair<int32_t,int32_t>{27,26},{26,284},{-1,284},{27,-1},{27,0x7FFFFF1C},{0x11B,284}}) {
                r=query(ids.first,ids.second);check(!r.executed && !r.condition,"Invalid or stale character identity is unavailable, not false");
            }
            flags2[1]|=0x80;r=query(27,284);check(!r.executed,"Released leader slot is unavailable");flags2[1]=28;
            flags2[0]|=0x80;r=query(27,284);check(!r.executed,"Released queried slot is unavailable");flags2[0]=ped_flag;
            ped2[0x36]=2;r=query(27,284);check(!r.executed,"Wrong live entity kind is corruption, not false");ped2[0x36]=3;
            *group_leader=1;r=query(27,284);check(!r.executed,"Dangling group leader is a failed read, not an absent association");*group_leader=leader;
            active[3]=1;*reinterpret_cast<uintptr_t*>(groups+3*0x2D4+0xC)=follower;
            r=query(27,284);check(!r.executed,"Contradictory duplicate memberships are unavailable");active[3]=0;
            {
                auto* byte=reinterpret_cast<uint8_t*>(native_entity_address(0x5F7E80));const auto saved=*byte;*byte^=1;
                r=query(27,284);check(!r.executed,"Foreign group reader profile makes the query unavailable");*byte=saved;
            }
            auto bad_layout=operation(EntityCommand::char_in_chars_group,{27});bad_layout.predicate=true;
            check(!sa_execute_entity_command(bad_layout).executed,"Wrong operand layout is refused");
            auto bad_predicate=operation(EntityCommand::char_in_chars_group,{27,284});
            check(!sa_execute_entity_command(bad_predicate).executed,"Non-predicate packet is refused");
            r=query(27,284);check(r.executed && r.condition,"Valid state still answers after every refusal");
            std::memcpy(groups,saved_groups_804.data(),saved_groups_804.size());std::memset(active,0,8);std::memcpy(ped,saved_ped_page.data(),saved_ped_page.size());
            ped_pool.flags=pool_flags;ped_pool.count=pool_count;
        }
        {
            // 483/784 through the adapter, the real native group reader and a model of the SA group script natives
            // that writes the simulated CPedGroups memory. 804 (sa_execute_entity_command) verifies the result.
            const auto pool_flags=ped_pool.flags;const auto pool_count=ped_pool.count;
            uint8_t flags2[2]={ped_flag,28};ped_pool.flags=flags2;ped_pool.count=2;
            uint8_t* ped2=ped+0x7C4;std::memset(ped2,0,0x7C4);ped2[0x36]=3;
            const auto saved_groups_five=std::vector<uint8_t>(groups,groups+8*0x2D4),saved_ped_page=std::vector<uint8_t>(ped,ped+0x1000);
            const auto saved_active_five=std::vector<uint8_t>(active,active+8);
            auto* group_refs=reinterpret_cast<uint16_t*>(native_entity_address(0xC098D0));
            const auto saved_group_refs=std::array<uint16_t,8>{group_refs[0],group_refs[1],group_refs[2],group_refs[3],group_refs[4],group_refs[5],group_refs[6],group_refs[7]};
            std::memset(groups,0,8*0x2D4);std::memset(active,0,8);std::fill(group_refs,group_refs+8,0);
            const auto slot_of=[&](int group,int slot){return reinterpret_cast<uintptr_t*>(groups+group*0x2D4+0xC+slot*4);};
            const auto handle_of=[&](int slot){return static_cast<int32_t>((uint32_t(group_refs[slot])<<16)|uint32_t(slot));};
            const auto follower=reinterpret_cast<uintptr_t>(ped),leader=reinterpret_cast<uintptr_t>(ped2);
            group_refs[0]=0x1233;group_refs[5]=0x5678;
            active[5]=1;*slot_of(5,0)=0x1234;*slot_of(5,7)=0; // unrelated group that must never change
            const auto unrelated=std::vector<uint8_t>(groups+5*0x2D4,groups+6*0x2D4);
            ScmImage image;NativeAdapter adapter(image);adapter.world_ready=true;
            adapter.live_ped_identity=sa_live_objective_actor;
            adapter.ped_group=[](const ObjectiveActor& who)->std::optional<NativeAdapter::PedGroupFacts>{
                const auto g=sa_ped_group_snapshot(who);if(!g)return std::nullopt;
                return NativeAdapter::PedGroupFacts{g->member,g->leader,g->leader_ped,g->group,g->free_group};
            };
            adapter.mission_script=[](const Thread&){return true;};
            int creates=0,leaders=0,joins=0,removes=0,clears=0,unexpected=0;bool join_noop=false,join_fail=false,create_entered_failure=false,create_mission_context=false,create_allocator_ok=true;
            adapter.execute=[&](const NativeCall& c)->NativeResult{
                NativeResult r;
                const auto ped_of=[&](int32_t h)->uintptr_t{const auto a=sa_live_objective_actor(h);return a?a->native_ped:0;};
                const auto slot_from=[&](int32_t g){return uint32_t(g)&0xFFFFu;};
                const auto valid=[&](int32_t g){const auto bits=uint32_t(g);const auto slot=bits&0xFFFFu;return slot<8 && active[slot] && group_refs[slot]==uint16_t(bits>>16);};
                switch(c.opcode) {
                case 0x07FD:r.executed=true;r.condition=valid(c.inputs[0]);return r;
                case 0x062F:
                    create_mission_context|=c.mission_context;create_allocator_ok&=c.inputs[0]==0;
                    if(create_entered_failure){++creates;r.native_entered=true;return r;}
                    for(int i=0;i<8;++i)if(!active[i]){active[i]=1;std::memset(groups+i*0x2D4,0,0x2D4);group_refs[i]=group_refs[i]>=0xFFFE?1:uint16_t(group_refs[i]+1);++creates;r.executed=true;r.values[0]=handle_of(i);return r;}
                    return r;
                case 0x0630:
                    if(!valid(c.inputs[0]) || !ped_of(c.inputs[1]))return r;
                    *slot_of(slot_from(c.inputs[0]),7)=ped_of(c.inputs[1]);++leaders;r.executed=true;return r;
                case 0x0631: {
                    if(join_fail || !valid(c.inputs[0]) || !ped_of(c.inputs[1]))return r;
                    ++joins;r.executed=true;if(join_noop)return r;
                    for(int s=0;s<7;++s)if(!*slot_of(slot_from(c.inputs[0]),s)){*slot_of(slot_from(c.inputs[0]),s)=ped_of(c.inputs[1]);break;}
                    return r;
                }
                case 0x06C9:
                    for(int g=0;g<8;++g)if(active[g])for(int s=0;s<8;++s)if(*slot_of(g,s)==ped_of(c.inputs[0]))*slot_of(g,s)=0;
                    ++removes;r.executed=true;return r;
                case 0x0687:++clears;r.executed=true;return r;
                default:++unexpected;return r;
                }
            };
            const auto run=[&](uint16_t op,std::initializer_list<int32_t> args){
                std::vector<uint8_t> code(64);code.push_back(uint8_t(op));code.push_back(uint8_t(op>>8));
                for(auto v:args){code.push_back(6);for(int j=0;j<4;++j)code.push_back(uint8_t(uint32_t(v)>>(8*j)));}
                Vm vm(code,64,64);Thread thread;thread.ip=64;
                vm.backend=[&](Vm& v,Thread& t,uint16_t o){return adapter.dispatch(v,t,o);};
                return vm.step(thread,0);
            };
            const auto membership=[&](int32_t who,int32_t lead){
                auto c=operation(EntityCommand::char_in_chars_group,{who,lead});c.predicate=true;
                const auto r=sa_execute_entity_command(c);return r.executed && r.condition;
            };
            const auto counters=[&]{return std::array<int,6>{creates,leaders,joins,removes,clears,unexpected};};
            const std::array<int,6> none{0,0,0,0,0,0};

            check(run(COMMAND_SET_CHAR_AS_LEADER,{27,284})==StepResult::continued,"483 accepts a live follower and ungrouped leader");
            check(creates==1 && leaders==1 && joins==1 && clears==1 && removes==0 && unexpected==0 && create_mission_context && create_allocator_ok,"Leader without a group receives one generation-tagged native group, with mission context and the existing allocator selector");
            check(handle_of(0)==0x12340000,"The mocked 062F increments the slot generation and returns the complete script group handle");
            check(membership(27,284) && !membership(284,27),"804 reads the real SA group: follower follows leader, not the reverse");
            const auto after_first=counters();
            check(run(COMMAND_SET_CHAR_AS_LEADER,{27,284})==StepResult::continued && counters()==after_first,"Repeating 483 for the same leader is idempotent and does not touch native groups");
            check(std::equal(unrelated.begin(),unrelated.end(),groups+5*0x2D4) && active[5]==1,"Unrelated native group is unchanged");

            // 784 reuses the leader's existing group rather than creating another.
            *slot_of(0,1)=0x4321; // a foreign follower already in the leader's group
            check(run(COMMAND_SET_CHAR_OBJ_FOLLOW_CHAR_IN_FORMATION,{27,284,3})==StepResult::continued && counters()==after_first,"784 on an existing follower of that leader is idempotent");
            *slot_of(0,0)=0;
            check(run(COMMAND_SET_CHAR_OBJ_FOLLOW_CHAR_IN_FORMATION,{27,284,8})==StepResult::continued && creates==1 && joins==2 && *slot_of(0,1)==0x4321 && *slot_of(0,0)==follower,
                  "784 joins the existing leader group without replacing other followers");
            check(membership(27,284),"784 result is visible through 804");

            // Move a follower between native groups.
            *slot_of(0,0)=0;active[1]=1;*slot_of(1,0)=follower; // leaderless old group holding the follower
            const auto before_move=counters();
            check(run(COMMAND_SET_CHAR_AS_LEADER,{27,284})==StepResult::continued && removes==before_move[3]+1 && *slot_of(1,0)==0 && *slot_of(0,0)==follower,
                  "A follower of another group is removed from it before joining the requested leader");
            check(membership(27,284) && std::equal(unrelated.begin(),unrelated.end(),groups+5*0x2D4),"Moved follower follows the new leader; uninvolved group intact");
            active[1]=0;std::memset(groups+1*0x2D4,0,0x2D4);

            // Signed-negative handles remain valid because the high word is a generation, not a failure code.
            std::memset(groups,0,8*0x2D4);std::memset(active,0,8);active[2]=1;group_refs[2]=0x8001;*slot_of(2,7)=leader;
            const auto negative_group=handle_of(2);
            check(negative_group<0 && run(COMMAND_SET_CHAR_OBJ_FOLLOW_CHAR_IN_FORMATION,{27,284,3})==StepResult::continued && joins==4 && *slot_of(2,0)==follower && membership(27,284),
                  "784 preserves and validates a live group handle whose generation makes its int32 value negative");

            // Refusals leave native state untouched.
            const auto snapshot=[&]{return std::make_pair(std::vector<uint8_t>(groups,groups+8*0x2D4),std::vector<uint8_t>(active,active+8));};
            const auto refused=[&](uint16_t op,std::initializer_list<int32_t> args,const char* why){
                const auto groups_before=snapshot();const auto page_before=std::vector<uint8_t>(ped,ped+0x1000);const auto before=counters();
                const auto status=run(op,args);
                check(status==StepResult::unsupported && counters()==before && snapshot()==groups_before && std::equal(page_before.begin(),page_before.end(),ped),why);
            };
            refused(COMMAND_SET_CHAR_AS_LEADER,{27,27},"A character cannot follow itself");
            refused(COMMAND_SET_CHAR_AS_LEADER,{27,26},"Stale leader identity refuses before native entry");
            refused(COMMAND_SET_CHAR_AS_LEADER,{26,284},"Stale follower identity refuses before native entry");
            refused(COMMAND_SET_CHAR_AS_LEADER,{27,-1},"Absent leader refuses instead of fabricating a group");
            refused(COMMAND_SET_CHAR_OBJ_FOLLOW_CHAR_IN_FORMATION,{27,284,9},"Out-of-range LCS formation refuses before native entry");
            refused(COMMAND_SET_CHAR_OBJ_FOLLOW_CHAR_IN_FORMATION,{27,284,-1},"Negative LCS formation refuses before native entry");
            std::memset(groups,0,8*0x2D4);std::fill(active,active+8,1);
            refused(COMMAND_SET_CHAR_AS_LEADER,{27,284},"A leader with no group refuses creation when all eight native group slots are occupied");
            std::memset(groups,0,8*0x2D4);std::memset(active,0,8);
            // Leader is merely somebody else's follower: its group is not rewritten.
            *slot_of(0,0)=0;*slot_of(0,7)=0;active[2]=1;*slot_of(2,7)=follower;*slot_of(2,0)=leader;
            refused(COMMAND_SET_CHAR_AS_LEADER,{27,284},"A leader who is only a follower elsewhere refuses");
            *slot_of(2,0)=0;*slot_of(2,7)=0;active[2]=0;
            // Follower leads its own group: not dissolved.
            active[2]=1;*slot_of(2,7)=follower;
            refused(COMMAND_SET_CHAR_AS_LEADER,{27,284},"A follower who leads a group refuses");
            active[2]=0;*slot_of(2,7)=0;
            { auto* byte=reinterpret_cast<uint8_t*>(native_entity_address(0x5F7E80));const auto saved=*byte;*byte^=1;
              refused(COMMAND_SET_CHAR_AS_LEADER,{27,284},"Unavailable native group reader refuses before mutation");*byte=saved; }
            { adapter.ped_group=nullptr;refused(COMMAND_SET_CHAR_AS_LEADER,{27,284},"Missing group reader callback refuses");
              adapter.ped_group=[](const ObjectiveActor& who)->std::optional<NativeAdapter::PedGroupFacts>{
                  const auto g=sa_ped_group_snapshot(who);if(!g)return std::nullopt;
                  return NativeAdapter::PedGroupFacts{g->member,g->leader,g->leader_ped,g->group,g->free_group};}; }

            // Failure after entry and unobservable results must not replay or claim success.
            std::memset(groups,0,8*0x2D4);std::memset(active,0,8);
            join_fail=true;const auto joins_before=joins,creates_before=creates;
            const bool thrown=run(COMMAND_SET_CHAR_AS_LEADER,{27,284})==StepResult::failed; // the VM turns the repair exception into a failed step
            check(thrown &&joins==joins_before && creates==creates_before+1,"A member write that fails after group creation requires repair and is not replayed");
            std::memset(groups,0,8*0x2D4);std::memset(active,0,8);create_entered_failure=true;
            check(run(COMMAND_SET_CHAR_AS_LEADER,{27,284})==StepResult::failed,"A failed 062F callback that reports native entry is not treated as safe to retry");
            create_entered_failure=false;
            join_fail=false;std::memset(groups,0,8*0x2D4);std::memset(active,0,8);join_noop=true;
            const bool unconfirmed=run(COMMAND_SET_CHAR_OBJ_FOLLOW_CHAR_IN_FORMATION,{27,284,1})==StepResult::failed;
            check(unconfirmed && !membership(27,284),"An accepted write which the native group reader cannot confirm is not reported as success");
            join_noop=false;check(unexpected==0,"Only the reviewed SA group natives were requested");

            std::memcpy(groups,saved_groups_five.data(),saved_groups_five.size());std::memcpy(active,saved_active_five.data(),saved_active_five.size());std::copy(saved_group_refs.begin(),saved_group_refs.end(),group_refs);std::memcpy(ped,saved_ped_page.data(),saved_ped_page.size());
            ped_pool.flags=pool_flags;ped_pool.count=pool_count;
        }
        {
            // Native task-sequence pool reader used by the 408/606/614 occupant sequence (real PE profile bytes).
            const std::array<std::pair<uintptr_t,size_t>,6> code{{{0x4918A5,7},{0x4918B4,6},{0x491933,17},{0x48382C,63},{0x632E26,32},{0x491A71,36}}};
            for(const auto& spec:code)profile(pe,spec.first,spec.second);
            auto* active=reinterpret_cast<int32_t*>(native_entity_address(0x8D2E98));
            auto* open=reinterpret_cast<uint8_t*>(native_entity_address(0xC17898));
            const auto record=[&](size_t slot){return native_entity_address(0xC178F0+slot*0x40);}; // task record: +10 first task, +38 pending, +3C references
            const auto handle_generation=[&](size_t slot){return reinterpret_cast<uint16_t*>(native_entity_address(0xA43F6A+slot*4));};
            const auto handle_issued=[&](size_t slot){return reinterpret_cast<uint8_t*>(native_entity_address(0xA43F68+slot*4));};
            const auto saved_active=*active;const auto saved_open=std::vector<uint8_t>(open,open+64);
            std::vector<uint8_t> saved_records(64*0x40),saved_handles(64*4);
            std::memcpy(saved_records.data(),reinterpret_cast<void*>(record(0)),saved_records.size());
            std::memcpy(saved_handles.data(),reinterpret_cast<void*>(native_entity_address(0xA43F68)),saved_handles.size());
            *active=-1;std::memset(open,0,64);std::memset(reinterpret_cast<void*>(record(0)),0,saved_records.size());std::memset(reinterpret_cast<void*>(native_entity_address(0xA43F68)),0,saved_handles.size());
            auto state=sa_task_sequence_state();
            check(state && state->active==-1 && !state->allocated(0) && !state->allocated(63),"An idle native sequence pool reads as nothing open or allocated");
            *active=3;open[3]=1;*handle_issued(3)=1;*handle_generation(3)=0x8000;
            state=sa_task_sequence_state();
            check(state && state->active==3 && state->open[3] && state->issued[3] && state->generation[3]==0x8000 && state->allocated(3) && !state->allocated(2) &&
                  int32_t((uint32_t(state->generation[3])<<16)|3u)<0,"Open sequence, issued flag and uint16 generation (negative-handle range) are read exactly");
            *active=-1;open[3]=0;*reinterpret_cast<uint32_t*>(record(5)+0x10)=0x1234;*reinterpret_cast<uint8_t*>(record(5)+0x38)=1;*reinterpret_cast<uint32_t*>(record(5)+0x3C)=2;
            state=sa_task_sequence_state();
            check(state && state->allocated(5) && !state->open[5] && state->pending[5] && state->references[5]==2 && state->first_task[5]==0x1234,"A closed sequence with tasks is allocated; pending removal and references are read");
            for(const auto bad:{-2,64,1000}){*active=bad;check(!sa_task_sequence_state(),"An out-of-range construction global is unavailable");}
            *active=4;check(!sa_task_sequence_state(),"A construction global pointing at a closed slot is contradictory");*active=-1;
            open[6]=2;check(!sa_task_sequence_state(),"A non-boolean open flag is unavailable");open[6]=0;
            *reinterpret_cast<uint8_t*>(record(7)+0x38)=2;check(!sa_task_sequence_state(),"A non-boolean pending flag is unavailable");*reinterpret_cast<uint8_t*>(record(7)+0x38)=0;
            *handle_issued(8)=2;check(!sa_task_sequence_state(),"A non-boolean handle-issued flag is unavailable");*handle_issued(8)=0;
            for(const auto& spec:code) {
                auto* byte=reinterpret_cast<uint8_t*>(native_entity_address(spec.first));const auto saved=*byte;*byte^=1;
                check(!sa_task_sequence_state(),"A modified OPEN/CLOSE/CLEAR/handle/allocator routine makes the sequence state unavailable");*byte=saved;
            }
            check(sa_task_sequence_state().has_value(),"The reader recovers once the reviewed profile is restored");
            // Fault before OPEN issues a new handle: only the proven empty
            // construction slot may be restored, including stale issued metadata.
            *active=3;open[3]=1;
            check(sa_abort_unissued_task_sequence(3,0x8000,1) && *active==-1 && !open[3] &&
                  *handle_issued(3)==1 && *handle_generation(3)==0x8000 &&
                  *reinterpret_cast<uint32_t*>(record(5)+0x10)==0x1234,
                  "Pre-handle abort restores only the empty OPEN flags and preserves handle metadata and foreign tasks");
            *active=6;open[6]=1;open[3]=1;
            check(!sa_abort_unissued_task_sequence(3,0x8000,1) && *active==6 && open[6] && open[3],
                  "Abort never resets a foreign active sequence");
            open[6]=0;*active=3;
            check(!sa_abort_unissued_task_sequence(3,0x7FFF,1) && !sa_abort_unissued_task_sequence(3,0x8000,0),
                  "Changed generation or issued metadata cannot establish the pre-handle slot");
            *reinterpret_cast<uint32_t*>(record(3)+0x14)=0x4567;
            check(!sa_abort_unissued_task_sequence(3,0x8000,1) && open[3],"Abort checks every task pointer, not just the allocator's first pointer");
            *reinterpret_cast<uint32_t*>(record(3)+0x14)=0;
            *reinterpret_cast<uint32_t*>(record(3)+0x3C)=1;
            check(!sa_abort_unissued_task_sequence(3,0x8000,1),"Abort refuses a referenced sequence");
            *reinterpret_cast<uint32_t*>(record(3)+0x3C)=0;
            *reinterpret_cast<uint8_t*>(record(3)+0x38)=1;
            check(!sa_abort_unissued_task_sequence(3,0x8000,1),"Abort refuses pending removal");
            *reinterpret_cast<uint8_t*>(record(3)+0x38)=0;
            check(!sa_abort_unissued_task_sequence(32,0,0) && !sa_abort_unissued_task_sequence(64,0,0),
                  "Abort is restricted to the non-mission pool used by flee OPEN");
            check(sa_abort_unissued_task_sequence(3,0x8000,1),"Valid empty OPEN can still be recovered after refusals");
            *active=saved_active;std::memcpy(open,saved_open.data(),64);std::memcpy(reinterpret_cast<void*>(record(0)),saved_records.data(),saved_records.size());
            std::memcpy(reinterpret_cast<void*>(native_entity_address(0xA43F68)),saved_handles.data(),saved_handles.size());
        }
        const auto saved_ped=std::vector<uint8_t>(ped,ped+0x7C4);
        profile(pe,0x6337A0,34);
        std::array<uint8_t,0x248> threat_intelligence{};
        *reinterpret_cast<uintptr_t*>(ped+0x47C)=reinterpret_cast<uintptr_t>(threat_intelligence.data());
        threat_intelligence[0x244]=1;threat_intelligence[0x246]=1;
        auto threats=sa_char_threat_snapshot(actor);
        check(threats && threats->responses==std::array<uint8_t,3>{1,0,1},"Threat reader preserves three distinct actual response flags");
        const auto saved_created=ped[0x484];const auto saved_type=*reinterpret_cast<int32_t*>(ped+0x598);
        ped[0x484]=1;*reinterpret_cast<int32_t*>(ped+0x598)=6;
        auto suppressed=sa_cop_suppress_threats(*threats);
        check(!suppressed.executed && !suppressed.native_entered,"Threat suppression cannot steal a random cop before mission creation");
        ped[0x484]=2;threat_intelligence[0x243]=0xAB;threat_intelligence[0x247]=0xCD;
        suppressed=sa_cop_suppress_threats(*threats);
        check(suppressed.executed && suppressed.native_entered && !threat_intelligence[0x244] && !threat_intelligence[0x245] && !threat_intelligence[0x246],"Mission cop threat suppression applies all three real response flags");
        check(threat_intelligence[0x243]==0xAB && threat_intelligence[0x247]==0xCD,"Threat suppression preserves adjacent intelligence/task bytes");
        suppressed=sa_cop_suppress_threats(*threats);
        check(!suppressed.executed && !suppressed.native_entered,"Stale pre-suppression snapshot cannot be replayed");
        threat_intelligence[0x244]=1;threat_intelligence[0x246]=1;ped[0x484]=saved_created;*reinterpret_cast<int32_t*>(ped+0x598)=saved_type;
        threat_intelligence[0x245]=2;check(!sa_char_threat_snapshot(actor),"Malformed threat boolean is unavailable rather than normalized");threat_intelligence[0x245]=0;
        check(!sa_char_threat_snapshot({28,actor.native_ped}),"Stale actor generation cannot read another ped intelligence");
        auto* threat_writer=reinterpret_cast<uint8_t*>(native_entity_address(0x6337A0));const auto threat_byte=*threat_writer;
        *threat_writer^=1;check(!sa_char_threat_snapshot(actor),"Foreign threat writer profile refuses before effects");*threat_writer=threat_byte;
        *reinterpret_cast<uintptr_t*>(ped+0x47C)=0;check(!sa_char_threat_snapshot(actor),"Absent intelligence is not an invented disabled threat policy");
        *reinterpret_cast<float*>(ped+0x540)=100;ped[0x46D]=0;*reinterpret_cast<int32_t*>(ped+0x530)=40;
        auto foot=sa_route_actor_snapshot(actor);
        check(foot && foot->controllable && !foot->source_foot_controllable,"SA seated native control cannot advance an original route requiring state below 39");
        *reinterpret_cast<int32_t*>(ped+0x530)=0;foot=sa_route_actor_snapshot(actor);
        check(foot && foot->source_foot_controllable,"Reviewed early native states satisfy the separate retail foot-control gate");
        *reinterpret_cast<int32_t*>(ped+0x530)=11;foot=sa_route_actor_snapshot(actor);
        check(foot && foot->source_foot_controllable && !foot->source_seek_allowed,
            "Actual FOLLOW_PATH pose remains controllable for route arrival but refuses source SetSeek");
        *reinterpret_cast<int32_t*>(ped+0x530)=12;foot=sa_route_actor_snapshot(actor);
        check(foot && foot->source_seek_allowed,"SNIPER_MODE is distinct from FOLLOW_PATH in the original enum");

        *reinterpret_cast<float*>(ped+0x540)=100;ped[0x46D]=0;ped[0x470]=0;ped[0x1D]=0;
        *reinterpret_cast<uint16_t*>(ped+0x22)=280;*reinterpret_cast<int32_t*>(ped+0x598)=6;
        *reinterpret_cast<int32_t*>(ped+0x530)=50;ped[0x484]=1;
        auto raw=sa_cop_snapshot(actor);
        check(raw && raw->native_model==280 && raw->native_type==6 && raw->native_state==50 && raw->created_by==1 &&
            !raw->removed && !raw->fading && !raw->group.member,"Cop snapshot copies native identity/state/model/ownership without pretending donor 280 equals source model 1");
        ped[0x470]|=8;ped[0x1D]|=8;raw=sa_cop_snapshot(actor);
        check(raw && raw->fading && raw->removed,"Exact independent fade/remove flags are preserved as native facts");
        std::memcpy(ped,saved_ped.data(),saved_ped.size());
        std::memcpy(active,saved_active.data(),8);std::memcpy(groups,saved_groups.data(),saved_groups.size());
    }
    {
        const std::array<int32_t,12> donors{280,285,286,287,281,282,283,284,288,62,66,67};
        const auto models=SourceCopModelMap::approved(donors);check(bool(models),"Approved canonical police donor map is complete and unique");
        for(size_t i=0;i<donors.size();++i)check(models->original(uint16_t(donors[i]))==int32_t(i<4?i+1:97+i-4),"Every native donor resolves to its actual source category/model identity");
        auto ambiguous=donors;ambiguous[4]=280;check(!SourceCopModelMap::approved(ambiguous),"Ambiguous donor identity cannot choose a source model by first match");
        NativeCopSnapshot raw;raw.actor={27,0xABC0};raw.native_model=280;raw.native_type=6;raw.created_by=1;raw.controllable=true;
        raw.native_state=40;auto source=source_cop_candidate(raw,*models);
        check(source && !source->controllable && source->original_state==40,"Native control in seated state is not original foot control");
        raw.native_state=0;source=source_cop_candidate(raw,*models);check(source && source->controllable,"Reviewed early state retains source control when native health/air gates pass");
        raw.native_state=56;check(!source_cop_candidate(raw,*models),"SA-only stealth dying state never gets an invented source state");
        raw.native_state=63;source=source_cop_candidate(raw,*models);check(source && source->original_state==62 && !source->controllable,"Native arrested state translates past SA-only inserted state and remains excluded");
        raw.native_state=50;source=source_cop_candidate(raw,*models);check(source && source->original_state==50 && !source->controllable,"Driving is handled by the original explicit exception, not a false control predicate");
        raw.native_model=240;check(!source_cop_candidate(raw,*models),"Foreign police-looking model cannot be mapped to source cop by appearance");
        raw.native_model=280;raw.group.member=true;check(!source_cop_candidate(raw,*models),"Leaderless active native group is not stolen during membership transition");
        MissionCleanupSnapshot cleanup;
        for(const auto spec:std::array<std::pair<uintptr_t,size_t>,4>{{{0x5E47E0,0x98},{0x600B50,0x52},{0x600BE0,0xD},{0x600BF0,0xD}}})profile(pe,spec.first,spec.second);
        check(sa_cop_creation_profile_matches(),"Complete reviewed mission creation setter and transitive services match");
        profile(pe,0x6337A0,34);profile(pe,0x46772B,21);
        const auto creation_ped=std::vector<uint8_t>(ped,ped+0x7C4);
        std::array<uint8_t,0x248> creation_brain{};
        NativeThreatSnapshot creation_request;creation_request.actor={27,reinterpret_cast<uintptr_t>(ped)};
        creation_request.intelligence=reinterpret_cast<uintptr_t>(creation_brain.data());
        for(int fault:{0,1,2,3}) {
            *reinterpret_cast<uintptr_t*>(ped+0x47C)=creation_request.intelligence;
            *reinterpret_cast<int32_t*>(ped+0x598)=6;ped[0x484]=1;
            *reinterpret_cast<int32_t*>(creation_brain.data()+0xB4)=7;
            cop_creation_fault=fault;const auto calls=cop_creation_calls;
            const auto effect=sa_cop_set_mission_creation(creation_request);
            check(effect.native_entered && effect.executed==(fault==0) && cop_creation_calls==calls+1,"Creation effect verifies all setter effects and preserves entered failure without replay");
        }
        cop_creation_fault=0;const auto calls=cop_creation_calls;
        auto denied=sa_cop_set_mission_creation(creation_request);
        check(!denied.executed && !denied.native_entered && cop_creation_calls==calls,"Already promoted cop cannot be counted by replaying creation");
        auto* effect_counter=reinterpret_cast<uint32_t*>(native_entity_address(0xC0EC24));const auto preserved_counter=*effect_counter;
        *effect_counter=9;auto accounted=sa_cop_increment_mission_count(creation_request,9);
        check(accounted.executed && accounted.native_entered && *effect_counter==10,"Mission count increments the actual reviewed counter after creation and threat suppression");
        accounted=sa_cop_increment_mission_count(creation_request,9);
        check(!accounted.executed && !accounted.native_entered && *effect_counter==10,"Stale count receipt cannot increment twice");
        *effect_counter=UINT32_MAX;accounted=sa_cop_increment_mission_count(creation_request,UINT32_MAX);
        check(!accounted.executed && !accounted.native_entered && *effect_counter==UINT32_MAX,"Mission count never wraps on overflow");
        *effect_counter=9;creation_brain[0x244]=1;accounted=sa_cop_increment_mission_count(creation_request,9);
        check(!accounted.executed && !accounted.native_entered && *effect_counter==9,"Unfinished threat suppression refuses mission accounting");creation_brain[0x244]=0;
        ped[0x484]=1;accounted=sa_cop_increment_mission_count(creation_request,9);
        check(!accounted.executed && !accounted.native_entered && *effect_counter==9,"Random cop ownership cannot be counted as a mission ped");
        *effect_counter=preserved_counter;
        std::memcpy(ped,creation_ped.data(),creation_ped.size());
        for(uintptr_t address:{uintptr_t(0x5E47E0),uintptr_t(0x600B50),uintptr_t(0x600BE0),uintptr_t(0x600BF0)}) {
            auto* code=reinterpret_cast<uint8_t*>(native_entity_address(address));const auto saved=*code;*code^=1;
            check(!sa_cop_creation_profile_matches(),"Modified creation/decision/seeing/hearing routine refuses before promotion");*code=saved;
        }
        profile(pe,0x46772B,21);
        auto* mission_counter=reinterpret_cast<uint32_t*>(native_entity_address(0xC0EC24));const auto saved_count=*mission_counter;
        *mission_counter=0;check(sa_mission_ped_count()==0,"Empty mission population is a genuine available counter");
        *mission_counter=17;check(sa_mission_ped_count()==17,"Native counter reader reports the actual mission count");
        *mission_counter=UINT32_MAX;check(sa_mission_ped_count()==UINT32_MAX,"Counter overflow boundary remains observable for acceptance to refuse increment");
        auto* accounting_code=reinterpret_cast<uint8_t*>(native_entity_address(0x46772B));const auto accounting_byte=*accounting_code;
        *accounting_code^=1;check(!sa_mission_ped_count(),"Altered mission counter ABI does not invent a zero or permit accounting writes");*accounting_code=accounting_byte;
        *mission_counter=saved_count;
        profile(pe,0x4637E0,5);profile(pe,0x1565D40,44);
        auto* cleanup_table=reinterpret_cast<uint8_t*>(native_entity_address(0xA90850));
        MissionCleanupSnapshot saved_cleanup;std::memcpy(saved_cleanup.bytes.data(),cleanup_table,saved_cleanup.bytes.size());
        std::memset(cleanup_table,0,saved_cleanup.bytes.size());
        const auto cleanup_ped=std::vector<uint8_t>(ped,ped+0x7C4);
        std::array<uint8_t,0x248> cleanup_brain{};
        NativeThreatSnapshot cleanup_actor;cleanup_actor.actor={27,reinterpret_cast<uintptr_t>(ped)};
        cleanup_actor.intelligence=reinterpret_cast<uintptr_t>(cleanup_brain.data());
        *reinterpret_cast<uintptr_t*>(ped+0x47C)=cleanup_actor.intelligence;ped[0x484]=2;*reinterpret_cast<int32_t*>(ped+0x598)=6;
        for(int fault:{0,1,2}) {
            std::memset(cleanup_table,0,saved_cleanup.bytes.size());cop_cleanup_fault=fault;
            const auto effect=sa_cop_register_cleanup(cleanup_actor,MissionCleanupSnapshot{},saved_count);
            check(effect.native_entered && effect.executed==(fault==0),"Cleanup verifies actual insertion and retains silent-refusal/partial-exception entry receipts");
        }
        cop_cleanup_fault=0;std::memset(cleanup_table,0,saved_cleanup.bytes.size());
        const auto acceptance_count=*reinterpret_cast<uint32_t*>(native_entity_address(0xC0EC24));
        size_t selection_commits=0;
        for(int fault:{0,1}) {
            std::memset(cleanup_table,0,saved_cleanup.bytes.size());
            *reinterpret_cast<uint32_t*>(native_entity_address(0xC0EC24))=9;
            *reinterpret_cast<float*>(ped+0x540)=100;*reinterpret_cast<int32_t*>(ped+0x530)=0;
            ped[0x46D]=0;ped[0x470]=0;ped[0x1D]=0;ped[0x484]=1;
            cleanup_brain[0x244]=1;cleanup_brain[0x246]=1;
            cop_cleanup_fault=fault;const auto chosen=sa_cop_snapshot(cleanup_actor.actor);
            check(chosen.has_value(),"Acceptance fixture has a real native cop snapshot");
            *reinterpret_cast<uint16_t*>(ped+0x22)=280;
            const auto accepted=sa_random_cop_in_area({-100000,-100000,100000,100000},{1,1,1,1,1},
                {280,285,286,287,281,282,283,284,288,62,66,67},-1,true,[&](int32_t handle) {
                check(handle==27,"Cop selector includes the original live slot zero");
                check(ped[0x484]==1 && *reinterpret_cast<uint32_t*>(native_entity_address(0xC0EC24))==9,"Shared source selection commits before native promotion/accounting");
                ++selection_commits;return true;
            });
            check(accepted.native_entered && accepted.executed==(fault==0) && ped[0x484]==2 &&
                *reinterpret_cast<uint32_t*>(native_entity_address(0xC0EC24))==10,"Composed promotion preserves original effect order and partial cleanup failure");
        }
        check(selection_commits==2,"Each accepted selection commits shared identity once");
        cop_cleanup_fault=0;*reinterpret_cast<uint32_t*>(native_entity_address(0xC0EC24))=acceptance_count;
        std::memset(cleanup_table,0,saved_cleanup.bytes.size());
        cleanup_table[0]=1;cleanup_table[600]=1;const auto cleanup_calls=cop_cleanup_calls;
        auto stale_cleanup=sa_cop_register_cleanup(cleanup_actor,MissionCleanupSnapshot{},saved_count);
        check(!stale_cleanup.executed && !stale_cleanup.native_entered && cop_cleanup_calls==cleanup_calls,"Changed cleanup reservation refuses before engine call");
        std::memcpy(ped,cleanup_ped.data(),cleanup_ped.size());std::memset(cleanup_table,0,saved_cleanup.bytes.size());
        check(sa_mission_cleanup_snapshot().has_value(),"Reviewed cleanup trampoline and target permit a read-only snapshot");
        cleanup_table[MissionCleanupSnapshot::count_offset]=1;
        check(!sa_mission_cleanup_snapshot(),"Native reader rejects inconsistent active-count snapshots");
        cleanup_table[0]=1;
        check(sa_mission_cleanup_snapshot().has_value(),"Consistent occupied native table remains readable");
        auto* writer_code=reinterpret_cast<uint8_t*>(native_entity_address(0x1565D40));const auto writer_byte=*writer_code;
        *writer_code^=1;check(!sa_mission_cleanup_snapshot(),"Foreign cleanup writer refuses without executing a trampoline");*writer_code=writer_byte;
        auto* cleanup_entry=reinterpret_cast<uint8_t*>(native_entity_address(0x4637E0));const auto entry_byte=*cleanup_entry;
        *cleanup_entry^=1;check(!sa_mission_cleanup_snapshot(),"Foreign cleanup entry refuses before native use");*cleanup_entry=entry_byte;
        std::memcpy(cleanup_table,saved_cleanup.bytes.data(),saved_cleanup.bytes.size());
        // Nonzero padding/free-slot handles are retained by the native writer.
        cleanup.bytes[1]=0xEE;cleanup.bytes[4]=0xFF;
        const auto reservation=reserve_char_cleanup(cleanup,27);
        check(reservation && reservation->slot==0 && reservation->count==0,"Cleanup reserves the actual first free slot including slot zero");
        auto inserted=cleanup;inserted.bytes[0]=2;inserted.bytes[4]=27;
        inserted.bytes[MissionCleanupSnapshot::count_offset]=1;
        check(char_cleanup_inserted(cleanup,inserted,*reservation),"Native cleanup receipt verifies handle/type/count and preserved padding");
        check(!char_cleanup_inserted(cleanup,cleanup,*reservation),"Silent no-insertion return is not an accepted cleanup effect");
        auto callback_change=inserted;callback_change.bytes[9]=1;
        check(!char_cleanup_inserted(cleanup,callback_change,*reservation),"Unrelated callback mutation invalidates exact cleanup receipt");
        check(!reserve_char_cleanup(inserted,27),"Duplicate CHAR ownership cannot increment cleanup count twice");
        auto inconsistent=cleanup;inconsistent.bytes[MissionCleanupSnapshot::count_offset]=1;
        check(!reserve_char_cleanup(inconsistent,27),"Corrupt count is refused before promotion");
        auto full=cleanup;for(size_t j=0;j<MissionCleanupSnapshot::slots;++j)full.bytes[j*8]=1;
        full.bytes[MissionCleanupSnapshot::count_offset]=75;
        check(!reserve_char_cleanup(full,27),"Full native cleanup table refuses before any mission-ped effect");
        check(!reserve_char_cleanup(cleanup,-1),"Unavailable handle never reserves cleanup ownership");
        auto wrong=*reservation;wrong.slot=1;
        check(!char_cleanup_inserted(cleanup,inserted,wrong),"Reservation is rechecked rather than trusting a stale or forged slot");
        OriginalCopCandidate cop{0,1,6,1,0,{0,0,10},true,false,false,false,false};
        const std::array<float,4> bounds{10,10,-10,-10};const std::array<int32_t,5> all{1,1,1,1,1};
        check(original_cop_eligible(cop,bounds,all,0,-1)==true,"Retail eligible slot zero and inclusive Z +10 remain eligible; normalized XY is not civilian +/-5");
        for(int model:{1,2,3,4,97,98,99,100,101,102,103,104}) {cop.original_model=model;check(original_cop_eligible(cop,bounds,all,0,-1)==true,"All five original police model categories are recognized through source model identity");}
        cop.original_model=7;check(original_cop_eligible(cop,bounds,all,0,-1)==false,"Civilian or story skin is not made a cop by appearance");cop.original_model=1;
        cop.controllable=false;for(int state:{50,39}){cop.original_state=state;check(original_cop_eligible(cop,bounds,all,0,-1)==true,"Retail driving/abseil exceptions do not require native foot control");}
        cop.original_state=0;check(original_cop_eligible(cop,bounds,all,0,-1)==false,"No control and no reviewed exception excludes the ped");cop.controllable=true;
        cop.position[2]=10.01f;check(original_cop_eligible(cop,bounds,all,0,-1)==false,"Source vertical boundary is strict outside inclusive +/-10");cop.position[2]=-10;
        check(original_cop_eligible(cop,bounds,all,0,0)==false,"Shared LastRandomPedId excludes even slot zero");
        cop.created_by=2;check(original_cop_eligible(cop,bounds,all,0,-1)==false,"Mission-owned cops are never selected again");cop.created_by=1;
        cop.leader=true;check(original_cop_eligible(cop,bounds,all,0,-1)==false,"Group leaders are not stolen");cop.leader=false;cop.has_leader=true;
        check(original_cop_eligible(cop,bounds,all,0,-1)==false,"Followers are not stolen");cop.has_leader=false;
        check(!original_cop_eligible(cop,bounds,all,std::numeric_limits<float>::quiet_NaN(),-1),"Unavailable player height is not a fabricated selection predicate");
        {
            // 1494 / 0x05D6 retail 2502B0: civilian promotion, wait-state gate,
            // inclusive Z +/-5, no RANDOM_CHAR filter, shared commit first.
            const auto saved_1494=std::vector<uint8_t>(ped,ped+0x7C4);const auto saved_vehicles=vehicles;
            auto* table=reinterpret_cast<uint8_t*>(native_entity_address(0xA90850));
            MissionCleanupSnapshot table_saved;std::memcpy(table_saved.bytes.data(),table,table_saved.bytes.size());
            auto* counter=reinterpret_cast<uint32_t*>(native_entity_address(0xC0EC24));const auto counter_saved=*counter;
            std::array<uint8_t,0x248> brain{};
            auto reset=[&](float z,uint8_t created,int32_t type) {
                std::memcpy(ped,saved_1494.data(),saved_1494.size());vehicles=saved_vehicles;
                *reinterpret_cast<uintptr_t*>(ped+0x14)=0;const float position[3]{5,-5,z};std::memcpy(ped+4,position,12);
                // Player height 37 comes from the occupied vehicle, so ped Z varies independently.
                *reinterpret_cast<uintptr_t*>(vehicles.data()+0x14)=0;*reinterpret_cast<float*>(vehicles.data()+0xC)=37;
                ped[0x46D]|=1;*reinterpret_cast<uintptr_t*>(ped+0x58C)=reinterpret_cast<uintptr_t>(vehicles.data());
                *reinterpret_cast<int32_t*>(ped+0x598)=type;ped[0x484]=created;ped[0x1D]&=~8u;ped[0x470]&=~8u;
                brain.fill(0);brain[0x244]=1;brain[0x246]=1;*reinterpret_cast<int32_t*>(brain.data()+0xB4)=3;
                *reinterpret_cast<uint32_t*>(brain.data()+0xC4)=0x1234;
                *reinterpret_cast<uintptr_t*>(ped+0x47C)=reinterpret_cast<uintptr_t>(brain.data());
                std::memset(table,0,table_saved.bytes.size());*counter=4;cop_creation_fault=0;cop_cleanup_fault=0;
            };
            const std::array<float,4> reversed{10,10,-10,-10};
            size_t commits=0,queries=0;bool waiting_now=false;
            auto waiting=[&](int32_t handle)->std::optional<bool>{++queries;check(handle==27,"Wait state is queried for the actual pool identity");return waiting_now;};
            auto commit=[&](int32_t handle) {
                check(handle==27,"Selection includes live slot zero");
                check(*counter==4 && brain[0x244]==1 && brain[0x246]==1,"Shared LastRandomPedId commits before creation/threat/count");
                ++commits;return true;
            };
            reset(42.f,1,7);
            auto chosen=sa_random_char_unchecked(reversed,true,waiting,commit);
            check(chosen.executed && chosen.values[0]==27 && commits==1,"Civilian on the inclusive +5 boundary is selected from normalized XY bounds");
            check(ped[0x484]==2 && *reinterpret_cast<int32_t*>(brain.data()+0xB4)==-1 && *reinterpret_cast<uint32_t*>(brain.data()+0xC4)==0,"Civilian becomes MISSION_CHAR through the real setter with civilian decision effects");
            check(!brain[0x244] && !brain[0x245] && !brain[0x246] && *counter==5,"Civilian stops responding to threats and is counted once");
            check(table[0]==2 && *reinterpret_cast<int32_t*>(table+4)==27 && table[600]==1,"Mission script registers CLEANUP_CHAR for the civilian");
            for(float z:{42.01f,31.99f}) {
                reset(z,1,7);commits=0;
                chosen=sa_random_char_unchecked(reversed,true,waiting,commit);
                check(chosen.executed && chosen.values[0]==-1 && !commits && ped[0x484]==1 && *counter==4 && !table[600],"Outside the +/-5 window is the genuine -1 sentinel without effects");
            }
            for(float z:{32.f,40.f}) {
                reset(z,1,7);commits=0;
                chosen=sa_random_char_unchecked(reversed,false,waiting,commit);
                check(chosen.executed && chosen.values[0]==27 && commits==1 && *counter==5 && !table[600],"Inside +/-5 (beyond the rejected +/-2) is selected; non-mission script adds no cleanup");
            }
            reset(37.f,1,7);commits=0;waiting_now=true;queries=0;
            chosen=sa_random_char_unchecked(reversed,true,waiting,commit);
            check(chosen.executed && chosen.values[0]==-1 && queries==1 && !commits && ped[0x484]==1 && brain[0x244]==1 && *counter==4,"Non-FALSE wait state excludes an otherwise eligible civilian");
            waiting_now=false;
            chosen=sa_random_char_unchecked(reversed,true,[](int32_t)->std::optional<bool>{return {};},commit);
            check(!chosen.executed && !chosen.native_entered && !commits && ped[0x484]==1,"Unknown wait state refuses instead of assuming WAITSTATE_FALSE");
            reset(37.f,1,7);ped[0x1D]|=8;check(sa_random_char_unchecked(reversed,true,waiting,commit).values[0]==-1 && !commits,"bRemoveFromWorld excludes");
            reset(37.f,1,7);ped[0x470]|=8;check(sa_random_char_unchecked(reversed,true,waiting,commit).values[0]==-1 && !commits,"bFadeOut excludes");
            reset(37.f,2,7);table[0]=2;*reinterpret_cast<int32_t*>(table+4)=27;table[600]=1;
            chosen=sa_random_char_unchecked(reversed,true,waiting,commit);
            check(chosen.executed && chosen.values[0]==27 && commits==1 && *counter==5,"Already MISSION_CHAR is not filtered by an invented RANDOM_CHAR predicate");
            check(table[8]==2 && *reinterpret_cast<int32_t*>(table+12)==27 && table[600]==2,"Source AddEntityToList appends without duplicate filtering");
            reset(37.f,1,7);commits=0;for(size_t i=0;i<75;++i)table[i*8]=1;table[600]=75;
            std::vector<uint8_t> full(table,table+601);
            chosen=sa_random_char_unchecked(reversed,true,waiting,commit);
            check(chosen.executed && chosen.values[0]==27 && *counter==5 && std::equal(full.begin(),full.end(),table),"Full cleanup list silently refuses as in the source while promotion completes");
            reset(37.f,1,0);commits=0;
            chosen=sa_random_char_unchecked(reversed,false,waiting,commit);
            check(chosen.executed && *reinterpret_cast<int32_t*>(brain.data()+0xB4)==-2 && *reinterpret_cast<uint32_t*>(brain.data()+0xC4)==0x1234,"Player-type receipt follows the native setter's -2 branch");
            reset(37.f,1,7);commits=0;cop_cleanup_fault=1;
            chosen=sa_random_char_unchecked(reversed,true,waiting,commit);
            check(!chosen.executed && chosen.native_entered && commits==1 && ped[0x484]==2 && *counter==5,"Silent cleanup refusal after commit is an entered failure, never replayed");
            reset(37.f,1,7);commits=0;
            chosen=sa_random_char_unchecked(reversed,true,waiting,[&](int32_t){return false;});
            check(!chosen.executed && !chosen.native_entered && ped[0x484]==1 && *counter==4,"Refused shared commit leaves the ped untouched");
            OriginalUncheckedCharCandidate policy{0,{0,0,5},false,false,false};
            check(original_unchecked_char_eligible(policy,reversed,0)==true,"Policy: slot zero and +5 inclusive");
            policy.position[2]=-5.0001f;check(original_unchecked_char_eligible(policy,reversed,0)==false,"Policy: below -5 excluded");
            policy.position={10,-10,0};check(original_unchecked_char_eligible(policy,reversed,0)==true,"Policy: normalized XY endpoints included");
            policy.waiting=true;check(original_unchecked_char_eligible(policy,reversed,0)==false,"Policy: waiting excluded");
            check(!original_unchecked_char_eligible(policy,reversed,std::numeric_limits<float>::quiet_NaN()),"Policy: unknown player height unavailable");
            std::memcpy(ped,saved_1494.data(),saved_1494.size());vehicles=saved_vehicles;
            std::memcpy(table,table_saved.bytes.data(),table_saved.bytes.size());*counter=counter_saved;cop_cleanup_fault=0;cop_creation_fault=0;
        }
        {
            // 1398 backend: SA's own CPedIntelligence static counter against the native go-to limit.
            // The producer (ProcessStaticCounter 0x605650) and one consumer (0x657650 compares the counter
            // with the global at 0x86C938) come from the on-disk PE as data.
            profile(pe,0x605650,24);profile(pe,0x657650,12);
            const auto saved_page=std::vector<uint8_t>(ped,ped+0x1000);
            std::array<uint8_t,0x300> brain_stuck{};
            const ObjectiveActor actor{27,reinterpret_cast<uintptr_t>(ped)};
            auto* limit=reinterpret_cast<int32_t*>(native_entity_address(0x86C938));const auto limit_saved=*limit;*limit=30;
            *reinterpret_cast<uintptr_t*>(ped+0x47C)=reinterpret_cast<uintptr_t>(brain_stuck.data());
            *reinterpret_cast<uintptr_t*>(brain_stuck.data())=reinterpret_cast<uintptr_t>(ped);
            auto counter_at=[&](int32_t value){*reinterpret_cast<int32_t*>(brain_stuck.data()+0x274)=value;};
            counter_at(0);auto answer=sa_char_stuck(actor);
            check(answer && !*answer,"1398: a ped that is not blocked is not stuck");
            counter_at(30);answer=sa_char_stuck(actor);check(answer && !*answer,"1398: the native limit itself is not yet stuck (strict >)");
            counter_at(31);answer=sa_char_stuck(actor);check(answer && *answer,"1398: counter above the live native limit is stuck");
            *limit=60;answer=sa_char_stuck(actor);check(answer && !*answer,"1398: the limit is read live from the native global, not copied");
            *limit=0;check(!sa_char_stuck(actor),"1398: an implausible native limit is unreadable, never FALSE");*limit=30;
            counter_at(-1);check(!sa_char_stuck(actor),"1398: a negative counter is unreadable, never FALSE");counter_at(31);
            auto stale=actor;stale.native_ped++;check(!sa_char_stuck(stale),"1398: an identity that does not match the pool slot is unreadable");
            ped_flag|=0x80;check(!sa_char_stuck(actor),"1398: a freed ped generation is unreadable");ped_flag=27;
            *reinterpret_cast<uintptr_t*>(brain_stuck.data())=0;check(!sa_char_stuck(actor),"1398: intelligence that does not point back to the ped is unreadable");
            *reinterpret_cast<uintptr_t*>(brain_stuck.data())=reinterpret_cast<uintptr_t>(ped);
            *reinterpret_cast<uintptr_t*>(ped+0x47C)=0;check(!sa_char_stuck(actor),"1398: a ped without intelligence is unreadable");
            *reinterpret_cast<uintptr_t*>(ped+0x47C)=reinterpret_cast<uintptr_t>(brain_stuck.data());
            for(const uintptr_t address:{uintptr_t(0x605650),uintptr_t(0x657650)}) {
                auto* code=reinterpret_cast<uint8_t*>(native_entity_address(address));const auto saved=*code;*code^=1;
                check(!sa_char_stuck(actor),"1398: an altered counter producer/consumer profile refuses");*code=saved;
            }
            answer=sa_char_stuck(actor);check(answer && *answer,"1398: restoring the reviewed profiles restores the answer");
            *limit=limit_saved;std::memcpy(ped,saved_page.data(),saved_page.size());ped_flag=27;
        }
        {
            // 738 GET_RANDOM_CHAR_IN_ZONE backend: loaded LCS zone box, SA group/control/roof facts and the approved 1494 promotion.
            profile(pe,0x569750,62);
            const auto saved_page=std::vector<uint8_t>(ped,ped+0x1000);const auto saved_vehicles=vehicles;
            const auto pool_flags=ped_pool.flags;const auto pool_count=ped_pool.count;
            uint8_t flags2[2]={ped_flag,28};ped_pool.flags=flags2;ped_pool.count=2;
            uint8_t* ped2=ped+0x7C4;
            auto* table=reinterpret_cast<uint8_t*>(native_entity_address(0xA90850));
            MissionCleanupSnapshot table_saved;std::memcpy(table_saved.bytes.data(),table,table_saved.bytes.size());
            auto* counter=reinterpret_cast<uint32_t*>(native_entity_address(0xC0EC24));const auto counter_saved=*counter;
            auto* focus=reinterpret_cast<uint8_t*>(native_entity_address(0xB7CD74));const auto focus_saved=*focus;*focus=0;
            auto* active=reinterpret_cast<uint8_t*>(native_entity_address(0xC098E0));
            auto* groups=reinterpret_cast<uint8_t*>(native_entity_address(0xC09920));
            const auto saved_active=std::vector<uint8_t>(active,active+8),saved_groups=std::vector<uint8_t>(groups,groups+8*0x2D4);
            std::array<uint8_t,0x248> brain2{},brain0{};
            const std::array<float,6> zone{0,-20,30,20,10,45};const std::array<int32_t,3> civilians{1,0,0};
            auto arm=[&](float z,uint8_t created,int32_t type) { // slot 1 candidate; slot 0 (player ped) stays ineligible by position unless a test moves it
                std::memcpy(ped,saved_page.data(),saved_page.size());vehicles=saved_vehicles;ped_pool.flags=flags2;ped_pool.count=2;
                *reinterpret_cast<uintptr_t*>(ped+0x14)=0;*reinterpret_cast<float*>(ped+4)=5;*reinterpret_cast<float*>(ped+8)=-5;*reinterpret_cast<float*>(ped+0xC)=37; // player height 37
                ped[0x46D]&=~1u;ped[0x484]=1;*reinterpret_cast<int32_t*>(ped+0x598)=4;
                std::memset(ped2,0,0x7C4);ped2[0x36]=3;*reinterpret_cast<uintptr_t*>(ped2+0x14)=0;
                const float position[3]{5,-5,z};std::memcpy(ped2+4,position,12);
                *reinterpret_cast<int32_t*>(ped2+0x598)=type;ped2[0x484]=created;
                brain2.fill(0);brain2[0x244]=1;brain2[0x246]=1;*reinterpret_cast<int32_t*>(brain2.data()+0xB4)=3;*reinterpret_cast<uint32_t*>(brain2.data()+0xC4)=0x1234;
                *reinterpret_cast<uintptr_t*>(ped2+0x47C)=reinterpret_cast<uintptr_t>(brain2.data());
                brain0.fill(0);*reinterpret_cast<uintptr_t*>(ped+0x47C)=reinterpret_cast<uintptr_t>(brain0.data());
                std::memset(table,0,table_saved.bytes.size());*counter=4;cop_creation_fault=0;cop_cleanup_fault=0;
                std::memset(active,0,8);std::memset(groups,0,8*0x2D4);roof_result=false;roof_calls=0;
            };
            std::map<int32_t,bool> control{{27,true},{284,true}};std::optional<bool> unknown_control;bool use_unknown=false;
            auto in_control=[&](int32_t handle)->std::optional<bool>{if(use_unknown)return unknown_control;return control.at(handle);};
            size_t commits=0;int32_t last_commit=-1;
            auto commit=[&](int32_t handle){check(*counter==4 && brain2[0x244]==1,"Shared LastRandomPedId commits before creation/threat/count");++commits;last_commit=handle;return true;};
            auto run=[&](std::array<int32_t,3> kinds,int32_t excluded,bool mission){commits=0;return sa_random_char_in_zone(zone,kinds,excluded,mission,in_control,commit);};
            arm(40.f,1,4);
            auto chosen=run(civilians,-1,true);
            check(chosen.executed && chosen.values[0]==284 && commits==1 && last_commit==284,"Eligible civilian inside the LCS zone is selected from slot one");
            check(ped2[0x484]==2 && *reinterpret_cast<int32_t*>(brain2.data()+0xB4)==-1 && !brain2[0x244] && !brain2[0x246] && *counter==5,"Selection promotes through the approved real mission setter, threat suppression and counter");
            check(table[0]==2 && *reinterpret_cast<int32_t*>(table+4)==284 && table[600]==1,"Mission script registers CLEANUP_CHAR for the selected ped");
            check(roof_calls==1 && roof_last==std::array<float,3>{{5,-5,40}},"Roof is queried at the candidate position like the SA handler");
            check(ped[0x484]==1 && brain0[0x244]==0 && !*reinterpret_cast<uint8_t*>(&brain0[0x246]),"Slot zero is never examined or promoted");
            arm(40.f,1,4);chosen=run(civilians,-1,false);
            check(chosen.executed && chosen.values[0]==284 && !table[600],"A non-mission script adds no cleanup entry");
            struct KindRow{int32_t type;std::array<int32_t,3> kinds;bool selected;};
            for(const auto& row:std::vector<KindRow>{{4,{1,0,0},true},{5,{1,0,0},true},{7,{0,1,0},true},{15,{0,1,0},true},{20,{0,0,1},true},{22,{0,0,1},true},
                                                                                         {4,{0,1,1},false},{8,{1,0,1},false},{6,{1,1,1},false},{16,{1,1,1},false},{21,{1,1,1},false}}) {
                arm(40.f,1,row.type);chosen=run(row.kinds,-1,false);
                check(chosen.executed && (chosen.values[0]==284)==row.selected && (chosen.values[0]==-1)==!row.selected,"Ped type is selected only by the matching civilian/gang/criminal flag");
            }
            for(float x:{0.f,20.f}) {arm(40.f,1,4);const float position[3]{x,-5,40.f};std::memcpy(ped2+4,position,12);chosen=run(civilians,-1,false);check(chosen.values[0]==284,"Zone X bounds are inclusive");}
            for(float x:{-0.01f,20.01f}) {arm(40.f,1,4);const float position[3]{x,-5,40.f};std::memcpy(ped2+4,position,12);chosen=run(civilians,-1,false);check(chosen.executed && chosen.values[0]==-1 && !commits,"Outside the zone is the genuine -1");}
            for(float z:{32.f,42.f}) {arm(z,1,4);chosen=run(civilians,-1,false);check(chosen.values[0]==284,"Player Z window is inclusive");}
            for(float z:{31.99f,42.01f}) {arm(z,1,4);chosen=run(civilians,-1,false);check(chosen.executed && chosen.values[0]==-1,"Outside the player Z window is excluded even inside the zone Z range");}
            arm(40.f,2,4);chosen=run(civilians,-1,false);check(chosen.executed && chosen.values[0]==-1 && ped2[0x484]==2 && !commits,"Only RANDOM_CHAR peds are selected: mission-owned peds are skipped");
            arm(40.f,1,4);ped2[0x1D]|=8;chosen=run(civilians,-1,false);check(chosen.values[0]==-1 && !commits,"bRemoveFromWorld excludes");
            arm(40.f,1,4);ped2[0x470]|=8;chosen=run(civilians,-1,false);check(chosen.values[0]==-1 && !commits,"bFadeOut excludes");
            for(int32_t state:{54,55}) {
                arm(40.f,1,4);*reinterpret_cast<int32_t*>(ped2+0x530)=state;chosen=run(civilians,-1,false);
                check(chosen.executed && chosen.values[0]==-1 && !commits && *counter==4,"738 never missionizes a dying/dead ped even if the control callback reports true");
            }
            arm(40.f,1,4);ped2[0x46D]|=1;chosen=run(civilians,-1,false);
            check(chosen.executed && chosen.values[0]==-1 && !commits,"738 excludes the native in-vehicle flag");
            arm(40.f,1,4);*reinterpret_cast<uintptr_t*>(ped2+0x58C)=reinterpret_cast<uintptr_t>(vehicles.data());chosen=run(civilians,-1,false);
            check(chosen.executed && chosen.values[0]==-1 && !commits,"738 excludes a native vehicle reference even before the occupancy flag is set");
            arm(40.f,1,4);chosen=run(civilians,284,false);check(chosen.executed && chosen.values[0]==-1 && !commits,"The shared LastRandomPedId is excluded");
            chosen=run(civilians,283,false);check(chosen.values[0]==284,"Another LastRandomPedId does not exclude");
            arm(40.f,1,4);control[284]=false;chosen=run(civilians,-1,false);check(chosen.executed && chosen.values[0]==-1 && !commits,"A ped that is not in control is skipped");control[284]=true;
            arm(40.f,1,4);use_unknown=true;chosen=run(civilians,-1,false);check(!chosen.executed && !chosen.native_entered && ped2[0x484]==1,"Unknown control state refuses instead of assuming control");use_unknown=false;
            arm(40.f,1,4);active[2]=1;*reinterpret_cast<uintptr_t*>(groups+2*0x2D4+0xC)=reinterpret_cast<uintptr_t>(ped2);
            chosen=run(civilians,-1,false);check(chosen.executed && chosen.values[0]==-1 && !commits,"A native group member (leader or follower) is not stolen");
            {auto* byte=reinterpret_cast<uint8_t*>(native_entity_address(0x5F7E80));const auto saved=*byte;*byte^=1;chosen=run(civilians,-1,false);
             check(!chosen.executed && !chosen.native_entered && ped2[0x484]==1,"An unreadable native group profile refuses before promotion");*byte=saved;}
            arm(40.f,1,4);roof_result=true;chosen=run(civilians,-1,false);check(chosen.executed && chosen.values[0]==-1 && roof_calls==1 && !commits,"A roof above the candidate excludes it");roof_result=false;
            {auto* byte=reinterpret_cast<uint8_t*>(native_entity_address(0x569750));const auto saved=*byte;*byte^=1;arm(40.f,1,4);chosen=run(civilians,-1,false);
             check(!chosen.executed && !chosen.native_entered && !roof_calls && ped2[0x484]==1,"A modified roof routine refuses before any query or promotion");*byte=saved;}
            arm(40.f,1,4);{auto bad=zone;bad[3]=std::numeric_limits<float>::infinity();commits=0;chosen=sa_random_char_in_zone(bad,civilians,-1,false,in_control,commit);
             check(!chosen.executed && !chosen.native_entered && !commits,"Nonfinite zone bounds are unavailable");}
            // Slot zero (the player's pool slot) is never selected even when it is the only eligible ped.
            arm(60.f,1,4);{const float position[3]{5,-5,40.f};std::memcpy(ped+4,position,12);*reinterpret_cast<float*>(ped+0xC)=37.f;}
            chosen=run(civilians,-1,false);check(chosen.executed && chosen.values[0]==-1 && !commits && ped[0x484]==1,"Pool slot zero is skipped like the original while(--i) loop");
            arm(40.f,1,4);chosen=sa_random_char_in_zone(zone,civilians,-1,false,in_control,[&](int32_t){return false;});
            check(!chosen.executed && !chosen.native_entered && ped2[0x484]==1 && *counter==4,"A refused shared commit leaves the ped untouched");
            arm(40.f,1,4);cop_cleanup_fault=1;chosen=run(civilians,-1,true);
            check(!chosen.executed && chosen.native_entered && commits==1 && ped2[0x484]==2,"Promotion failure after the commit is an entered failure that is never replayed");
            std::memcpy(ped,saved_page.data(),saved_page.size());vehicles=saved_vehicles;ped_pool.flags=pool_flags;ped_pool.count=pool_count;
            std::memcpy(table,table_saved.bytes.data(),table_saved.bytes.size());*counter=counter_saved;*focus=focus_saved;cop_cleanup_fault=0;cop_creation_fault=0;
            std::memcpy(active,saved_active.data(),8);std::memcpy(groups,saved_groups.data(),saved_groups.size());
        }
        {
            // 1486 SET_CAR_TILTED_BY_CHAR backend: ApplyTurnForce(0,0,factor*ped.mass, ped.xy-vehicle.xy, 0).
            profile(pe,0x54FFF0,37);profile(pe,0x542A50,48);
            const auto saved_page=std::vector<uint8_t>(ped,ped+0x7C4);const auto saved_vehicles=vehicles;const auto saved_vehicle_flags=vehicle_flags;
            vehicle_flags[0]=7;vehicles[0x36]=2;
            auto arm=[&]{
                std::memcpy(ped,saved_page.data(),saved_page.size());vehicles=saved_vehicles;vehicle_flags[0]=7;vehicles[0x36]=2;ped[0x36]=3;
                *reinterpret_cast<uintptr_t*>(vehicles.data()+0x14)=0;*reinterpret_cast<uintptr_t*>(ped+0x14)=0;
                const float car[3]{10,20,3},person[3]{13,16,3};std::memcpy(vehicles.data()+4,car,12);std::memcpy(ped+4,person,12);
                *reinterpret_cast<float*>(ped+0x8C)=70.f;turn_force_calls.clear();turn_force_fault=0;
            };
            arm();auto result=sa_apply_car_tilt(7,27,-1.0f);
            check(result.executed && result.native_entered && turn_force_calls.size()==1 && turn_force_calls[0].vehicle==reinterpret_cast<uintptr_t>(vehicles.data()),"Tilt reaches the real CPhysical turn-force entry on the vehicle");
            check(turn_force_calls[0].force==std::array<float,3>{{0.f,0.f,-70.f}} && turn_force_calls[0].point==std::array<float,3>{{3.f,-4.f,0.f}},"Force is factor times the ped's native mass on Z and the point is the ped's XY offset from the vehicle");
            for(const float factor:{-0.8f,-1.2f}){arm();result=sa_apply_car_tilt(7,27,factor);check(result.executed && turn_force_calls.size()==1 && turn_force_calls[0].force[2]==factor*70.f,"Both ends of the original random range are accepted");}
            for(const float factor:{-0.7f,-1.3f,0.f,1.f,std::numeric_limits<float>::quiet_NaN()}){arm();result=sa_apply_car_tilt(7,27,factor);check(!result.executed && !result.native_entered && turn_force_calls.empty(),"A factor outside the original range never reaches the engine");}
            arm();result=sa_apply_car_tilt(8,27,-1.f);check(!result.executed && !result.native_entered && turn_force_calls.empty(),"Stale vehicle generation is refused");
            result=sa_apply_car_tilt(7,26,-1.f);check(!result.executed && turn_force_calls.empty(),"Stale ped generation is refused");
            result=sa_apply_car_tilt(27,7,-1.f);check(!result.executed && turn_force_calls.empty(),"Swapped vehicle/ped handles are refused");
            arm();ped[0x36]=2;result=sa_apply_car_tilt(7,27,-1.f);check(!result.executed && turn_force_calls.empty(),"Wrong live ped kind is refused");
            arm();vehicles[0x36]=3;result=sa_apply_car_tilt(7,27,-1.f);check(!result.executed && turn_force_calls.empty(),"Wrong live vehicle kind is refused");
            arm();*reinterpret_cast<float*>(ped+0x8C)=std::numeric_limits<float>::quiet_NaN();result=sa_apply_car_tilt(7,27,-1.f);check(!result.executed && !result.native_entered && turn_force_calls.empty(),"Non-finite mass is refused");
            arm();*reinterpret_cast<float*>(vehicles.data()+4)=std::numeric_limits<float>::infinity();result=sa_apply_car_tilt(7,27,-1.f);check(!result.executed && turn_force_calls.empty(),"Non-finite vehicle position is refused");
            arm();{std::array<uint8_t,0x40> matrix{};const float person[3]{20,-5,3};std::memcpy(matrix.data()+0x30,person,12);*reinterpret_cast<uintptr_t*>(ped+0x14)=reinterpret_cast<uintptr_t>(matrix.data());
              result=sa_apply_car_tilt(7,27,-1.f);check(result.executed && turn_force_calls.size()==1 && turn_force_calls[0].point==std::array<float,3>{{10.f,-25.f,0.f}},"A ped matrix supplies the position when present");}
            arm();turn_force_fault=1;result=sa_apply_car_tilt(7,27,-1.f);check(!result.executed && result.native_entered && turn_force_calls.size()==1,"A fault inside the native entry is an entered failure");
            {auto* byte=reinterpret_cast<uint8_t*>(native_entity_address(0x542A50));const auto saved=*byte;*byte^=1;arm();result=sa_apply_car_tilt(7,27,-1.f);
             check(!result.executed && !result.native_entered && turn_force_calls.empty(),"A modified turn-force routine is refused before any call");*byte=saved;}
            std::memcpy(ped,saved_page.data(),saved_page.size());vehicles=saved_vehicles;vehicle_flags=saved_vehicle_flags;turn_force_calls.clear();turn_force_fault=0;
        }    }
    {
        for(const auto spec:std::array<std::pair<uintptr_t,size_t>,8>{{{0x534030,20},{0x563280,35},{0x5A2BD0,14},{0x59EB50,15},{0x866F60,40},{0x866E78,40},{0x59F120,31},{0x5326F0,20}}})profile(pe,spec.first,spec.second);
        profile(pe,0x550050,37);
        std::array<uint8_t,0x19C*4> objects{};std::array<uint8_t,0x38*2> dummies{};
        std::array<uint8_t,4> object_tags{};std::array<uint8_t,2> dummy_tags{};
        std::array<uintptr_t,4> object_pool{{reinterpret_cast<uintptr_t>(objects.data()),reinterpret_cast<uintptr_t>(object_tags.data()),4,0}};
        std::array<uintptr_t,4> dummy_pool{{reinterpret_cast<uintptr_t>(dummies.data()),reinterpret_cast<uintptr_t>(dummy_tags.data()),2,0}};
        auto* object_global=reinterpret_cast<uintptr_t*>(native_entity_address(0xB7449C));const auto old_objects=*object_global;
        auto* dummy_global=reinterpret_cast<uintptr_t*>(native_entity_address(0xB744A0));const auto old_dummies=*dummy_global;
        *object_global=reinterpret_cast<uintptr_t>(object_pool.data());*dummy_global=reinterpret_cast<uintptr_t>(dummy_pool.data());
        for(size_t i=0;i<4;++i) {
            auto* obj=objects.data()+i*0x19C;*reinterpret_cast<uintptr_t*>(obj)=0x866F60;obj[0x36]=4;obj[0x13C]=i==2?2:1;
            auto* pos=reinterpret_cast<float*>(obj+4);pos[0]=i==3?100.f:float(i);pos[1]=0;pos[2]=0;
        }
        for(size_t i=0;i<2;++i) {
            auto* obj=dummies.data()+i*0x38;*reinterpret_cast<uintptr_t*>(obj)=0x866E78;obj[0x36]=5;
            auto* pos=reinterpret_cast<float*>(obj+4);pos[0]=i?100.f:0.f;pos[1]=0;pos[2]=0;
        }
        auto clear=operation(EntityCommand::clear_area_objects,{entity_bits(10.f),entity_bits(10.f),entity_bits(10.f),entity_bits(-10.f),entity_bits(-10.f),entity_bits(-10.f)});
        clear_object_events.clear();auto result=sa_execute_entity_command(clear);
        const std::vector<std::pair<uintptr_t,int>> expected{{reinterpret_cast<uintptr_t>(objects.data()+0x19C),1},{reinterpret_cast<uintptr_t>(objects.data()+0x19C),2},{reinterpret_cast<uintptr_t>(objects.data()+0x19C),3},
            {reinterpret_cast<uintptr_t>(objects.data()),1},{reinterpret_cast<uintptr_t>(objects.data()),2},{reinterpret_cast<uintptr_t>(objects.data()),3},
            {reinterpret_cast<uintptr_t>(dummies.data()),1},{reinterpret_cast<uintptr_t>(dummies.data()),2},{reinterpret_cast<uintptr_t>(dummies.data()),3}};
        check(result.executed && result.native_entered && clear_object_events==expected && !object_tags[2] && !object_tags[3] && !dummy_tags[1],"Clear source box uses inclusive normalized XYZ, descending objects then dummies and exact RW/world/delete order while preserving mission/outside objects");
        object_tags.fill(0);dummy_tags.fill(0);clear_object_events.clear();
        *reinterpret_cast<uintptr_t*>(objects.data())=0x12345678;
        result=sa_execute_entity_command(clear);
        check(!result.executed && !result.native_entered && clear_object_events.empty(),"Late foreign candidate is rejected during complete preflight before any prior candidate is destroyed");
        *reinterpret_cast<uintptr_t*>(objects.data())=0x866F60;
        for(uint8_t created:{uint8_t(2),uint8_t(4),uint8_t(5),uint8_t(6)}) {
            objects[0x13C]=created;object_tags.fill(0);dummy_tags.fill(0);clear_object_events.clear();result=sa_execute_entity_command(clear);
            check(result.executed && !object_tags[0],"Every native mission/cutscene/decoration/mission2 object is protected");
        }
        objects[0x13C]=1;object_tags.fill(0);dummy_tags.fill(0);
        clear.inputs[0]=entity_bits(std::numeric_limits<float>::quiet_NaN());clear_object_events.clear();result=sa_execute_entity_command(clear);
        check(!result.native_entered && !result.executed && clear_object_events.empty(),"Nonfinite clear bounds refuse before all native effects");clear.inputs[0]=entity_bits(10.f);
        for(int fault:{1,2,3}) {
            object_tags.fill(0);dummy_tags.fill(0);clear_object_events.clear();clear_object_fault=fault;result=sa_execute_entity_command(clear);
            check(!result.executed && result.native_entered && clear_object_events.size()==size_t(fault),"Destruction faults preserve native entry and stop remaining effects without replay");
        }
        clear_object_fault=0;clear_object_recycle=true;object_tags.fill(0);dummy_tags.fill(0);clear_object_events.clear();result=sa_execute_entity_command(clear);clear_object_recycle=false;
        check(!result.executed && result.native_entered && clear_object_events.size()==1,"Recycled object after RW deletion cannot be removed from world or destroyed through a stale pointer");
        object_tags.fill(0);dummy_tags.fill(0);clear_object_events.clear();clear_object_promote=true;result=sa_execute_entity_command(clear);clear_object_promote=false;
        check(!result.executed && result.native_entered && clear_object_events.size()==1 && !object_tags[1],"Object promoted to mission ownership by a callback is preserved before world removal/deletion");
        object_tags.fill(0x80);dummy_tags.fill(0x80);clear_object_events.clear();result=sa_execute_entity_command(clear);
        check(result.executed && !result.native_entered && clear_object_events.empty(),"Empty live pool is a legitimate clear with no invented engine call");
        for(uintptr_t address:{uintptr_t(0x534030),uintptr_t(0x563280),uintptr_t(0x5A2BD0),uintptr_t(0x59EB50),uintptr_t(0x866F60),uintptr_t(0x866E78),uintptr_t(0x59F120),uintptr_t(0x5326F0)}) {
            auto* byte=reinterpret_cast<uint8_t*>(native_entity_address(address));const auto saved=*byte;*byte^=1;
            result=sa_execute_entity_command(clear);*byte=saved;
            check(!result.executed && !result.native_entered && clear_object_events.empty(),"Modified native destruction ABI/table never receives an engine call or rewrites a foreign hook");
        }
        const auto normalized=source_clear_object_box(clear.inputs.data());
        check(normalized && source_clear_object_contains(*normalized,{-10,-10,-10}) && source_clear_object_contains(*normalized,{10,10,10}) &&
            !source_clear_object_contains(*normalized,{10.01f,0,0}) && !source_clear_object_contains(*normalized,{0,0,10.01f}),"Object origins on XYZ boundaries are included, while every just-outside coordinate remains untouched");
        *object_global=old_objects;*dummy_global=old_dummies;
    }
    {
        profile(pe,0x56EA30,78);profile(pe,0x45AE80,5);profile(pe,0x1562160,19);profile(pe,0x15621F7,31);
        for(const auto spec:std::array<std::pair<uintptr_t,size_t>,10>{{
            {0x1562160,27},{0x15621CA,27},{0x6D5D70,18},{0x4654B0,5},
            {0x156E810,17},{0x48970E,7},{0x56E044,12},
            {0x47C4C7,12},{0x50B930,14},{0x571A00,14}}})profile(pe,spec.first,spec.second);

        {
            profile(pe,0x45AB10,12);profile(pe,0x50C7C0,12);profile(pe,0x50C1B0,12);profile(pe,0x569660,11);
            profile(pe,0x6D1080,12);
            profile(pe,0x571B70,25);
            const auto player=native_entity_address(0xB7CD98);
            auto* slot=reinterpret_cast<uintptr_t*>(player+0xB0);
            const auto saved_vehicles=vehicles;
            std::array<uint8_t,32> model{};*reinterpret_cast<uintptr_t*>(model.data()+0x1C)=1;
            pointer(0xA9B0C8+441*4,model.data());
            *reinterpret_cast<uint8_t*>(native_entity_address(0xB7CD74))=0;
            pointer(0xB7CD98,reinterpret_cast<void*>(ped));*slot=0;
            actor_reference_mode=true;
            rc_spawn_car=reinterpret_cast<uintptr_t>(vehicles.data());
            const auto camera=native_entity_address(0xB6F028);
            *reinterpret_cast<uint8_t*>(camera+0x38)=0;*reinterpret_cast<uint8_t*>(camera+0x39)=0;
            *reinterpret_cast<uint32_t*>(camera+0xC4)=123;*reinterpret_cast<uint32_t*>(camera+0xD8)=456;
            auto create=operation(EntityCommand::create_remote_car,{99,entity_bits(10),entity_bits(20),entity_bits(-100),entity_bits(90),441});
            create.outputs=1;
            auto result=sa_execute_entity_command(create);
            check(result.executed && result.native_entered && result.values[0]==7 && rc_spawns==1 && rc_cameras==1 && rc_camera_mode==3 && rc_camera_transition==1 &&
                rc_spawn_position==std::array<float,3>{{10,20,33}} && std::fabs(rc_spawn_rotation-1.57079637f)<.00001f,
                "0273 uses actual focus, translated model, original ground sentinel/radians, live pool handle and original camera mode");
            check(!*reinterpret_cast<uint8_t*>(camera+0x38) && !*reinterpret_cast<uint8_t*>(camera+0x39) &&
                *reinterpret_cast<uint32_t*>(camera+0xC4)==123 && *reinterpret_cast<uint32_t*>(camera+0xD8)==456,
                "Ground RC creation preserves the script zoom policies changed only by SA's factory");
            *slot=reinterpret_cast<uintptr_t>(vehicles.data()+2*0xA18);
            result=sa_execute_entity_command(create);
            check(!result.executed && !result.native_entered && rc_spawns==1,"A foreign existing RC reference is not replaced");
            *slot=rc_spawn_car;
            {
                *reinterpret_cast<uintptr_t*>(rc_spawn_car+0x384)=1;
                *reinterpret_cast<uint8_t*>(ped+0x46D)&=uint8_t(~1u);
                auto override_car=operation(EntityCommand::override_remote_car,{7,0});override_car.mission_context=true;
                rc_appearance=3;*reinterpret_cast<uint8_t*>(native_entity_address(0xC1CC01))=77;
                result=sa_execute_entity_command(override_car);
                check(result.executed && result.native_entered && rc_camera_mode==0x12 && rc_camera_transition==2 && rc_zooms==1 && rc_cleanups==1 &&
                    *reinterpret_cast<uint8_t*>(native_entity_address(0xC1CC00))==1 && *reinterpret_cast<uint8_t*>(native_entity_address(0xC1CC01))==77,
                    "1511 aerial branch uses the actual detonation flag, registered RC, jump-cut camera, source zoom0 and mission cleanup");
                rc_appearance=1;const auto before_cameras=rc_cameras;
                result=sa_execute_entity_command(override_car);
                check(!result.executed && !result.native_entered && rc_cameras==before_cameras,
                    "Unbound ground-door policy refuses before status, camera, cleanup or flag mutations");
                size_t site_count=0;
                const auto sites=sa_remote_door_policy_call_sites(&site_count);
                check(sites && site_count==53,"All three native door producers have their closed set of callsites");
                uintptr_t setter_site=0,alternate_setter_site=0,component_site=0,progress_site=0;
                for(size_t n=0;n<site_count;++n) {
                    const auto& site=sites[n];profile(pe,site.address,5);
                    auto* code=reinterpret_cast<uint8_t*>(native_entity_address(site.address));
                    int32_t old_relative=0;std::memcpy(&old_relative,code+1,4);
                    const auto original=uintptr_t(int64_t(site.address+5)+old_relative);
                    uintptr_t expected=0,previous=0,callback=0;
                    switch(site.kind) {
                    case SaRemoteDoorPolicyCallKind::set_door_status:
                        expected=0x6C21C0;previous=reinterpret_cast<uintptr_t>(&remote_door_previous_setter);
                        if(!alternate_setter_site){alternate_setter_site=site.address;previous=reinterpret_cast<uintptr_t>(&remote_door_previous_alternate);}
                        setter_site=site.address;break;
                    case SaRemoteDoorPolicyCallKind::component_set_door_status:
                        expected=0x6C21E0;previous=reinterpret_cast<uintptr_t>(&remote_door_previous_component);
                        component_site=site.address;break;
                    case SaRemoteDoorPolicyCallKind::progress_door_damage:
                        expected=0x6C2320;previous=reinterpret_cast<uintptr_t>(&remote_door_previous_progress);
                        progress_site=site.address;break;
                    }
                    callback=sa_remote_door_policy_callback_for_site(site.address);
                    check(code[0]==0xE8 && original==expected,"Each declared native producer site is a genuine PE CALL to its reviewed method");
                    check(sa_remote_door_policy_bind_callsite(site.address,previous),"Chained thiscall target binds before publishing its callback");
                    const auto relative=int32_t(callback-(site.address+5));std::memcpy(code+1,&relative,4);
                    if(n+1<site_count)check(!sa_remote_door_policy_calls_healthy(),"Partial hook publication cannot admit the ground command");
                }
                check(sa_remote_door_policy_calls_healthy(),"All callback targets must remain owned before ground admission");
                remote_door_reference_mode=true;remote_door_reference_count=0;
                result=sa_execute_entity_command(override_car);
                check(result.executed && result.native_entered && rc_camera_mode==0x10 && rc_camera_transition==2 && rc_zooms==1 &&
                    sa_remote_door_policy_vehicle_live(rc_spawn_car) && sa_remote_door_policy_runtime_healthy(),
                    "1511 ground binds its live native policy and original first-person camera without aerial zoom effects");
                const auto manager=rc_spawn_car+0x5A0;
                auto* status=reinterpret_cast<uint8_t*>(manager+9);
                const auto setter=reinterpret_cast<SaRemoteDoorStatusCallback>(sa_remote_door_policy_callback_for_site(setter_site));
                setter(manager,0,2,2);
                check(status[2]==1 && remote_door_forwarded_status==1,"Owned smashed door delegates the source swinging state before native consumption");
                setter(manager,0,0,2);
                check(status[0]==2 && remote_door_forwarded_status==2,"Bonnet is excluded from the original door policy");
                const auto other_manager=reinterpret_cast<uintptr_t>(vehicles.data()+2*0xA18)+0x5A0;
                setter(other_manager,0,2,2);
                check(*reinterpret_cast<uint8_t*>(other_manager+11)==2 && remote_door_forwarded_status==2,
                    "Unrelated native vehicles receive the exact original delegate arguments");
                reinterpret_cast<SaRemoteDoorStatusCallback>(sa_remote_door_policy_callback_for_site(alternate_setter_site))(other_manager,0,2,2);
                check(remote_door_alternate_calls==1,"Each native callsite retains its own pre-existing mod delegate");
                constexpr int mapped_doors[]={3,5,2,4};
                for(int node=8;node<=11;++node) {
                    reinterpret_cast<SaRemoteDoorStatusCallback>(sa_remote_door_policy_callback_for_site(component_site))(manager,0,node,2);
                    check(status[mapped_doors[node-8]]==1,"Component-node writes use the native four-way door map");
                }
                remote_door_progress_test_site=progress_site;
                const auto progress=reinterpret_cast<SaRemoteDoorProgressCallback>(sa_remote_door_policy_callback_for_site(progress_site));
                status[2]=0;remote_door_progress_accept=true;
                check(progress(manager,0,2,rc_spawn_car) && status[2]==1,"First source damage hit opens the intact door");
                check(progress(manager,0,2,rc_spawn_car) && status[2]==4,"Next source hit removes the swinging door without an extra SA damage stage");
                check(!progress(manager,0,2,rc_spawn_car) && status[2]==4,"Missing source doors never fabricate another progress effect");
                status[2]=2;remote_door_progress_accept=false;
                check(progress(manager,0,2,rc_spawn_car) && status[2]==1,
                    "SA random no-progress is replaced by the real deterministic source store and true result before visual consumption");
                status[2]=2;bool fault_result=false;remote_door_progress_fault=true;
                const auto fault_code=remote_door_guarded_progress(manager,2,rc_spawn_car,&fault_result);remote_door_progress_fault=false;
                check(fault_code==0xE0524450 && !fault_result && status[2]==2,
                    "A delegate fault is never mistaken for SA random false or converted into a successful source store");
                *reinterpret_cast<uint8_t*>(other_manager+11)=2;
                check(!progress(other_manager,0,2,reinterpret_cast<uintptr_t>(vehicles.data()+2*0xA18)) &&
                    *reinterpret_cast<uint8_t*>(other_manager+11)==2,"Foreign vehicles retain the delegate's random false result and unchanged state");
                remote_door_progress_accept=true;
                auto* hooked=reinterpret_cast<uint8_t*>(native_entity_address(setter_site));const auto saved=*hooked;*hooked=0x90;
                check(!sa_remote_door_policy_calls_healthy() && !sa_remote_door_policy_runtime_healthy(),
                    "A lost native consumer is detected by real hook-byte checks while the source policy is active");
                const auto delegated_before=remote_door_set_calls;setter(manager,0,2,2);
                check(remote_door_set_calls==delegated_before+1 && status[2]==2,
                    "A later mod can chain our per-site callback from its own code without an origin lookup crash or bypassing its original delegate");
                *hooked=saved;
                check(sa_remote_door_policy_runtime_healthy(),"Restored original owned CALL repairs the source dependency");
            }
            rc_spawn_car=reinterpret_cast<uintptr_t>(vehicles.data()+2*0xA18);
            result=sa_execute_entity_command(create);
            check(result.executed && result.values[0]==524 && rc_spawns==2 && *slot==rc_spawn_car &&
                sa_remote_door_policy_vehicle_live(reinterpret_cast<uintptr_t>(vehicles.data())),
                "Replacing the source RC detaches its controller references but retains the original old vehicle's door policy");
            // Simulate the native entity destructor clearing both real weak
            // cells; no stale ownership may survive pool-slot reuse.
            for(auto car:{reinterpret_cast<uintptr_t>(vehicles.data()),rc_spawn_car})
                for(auto node=*reinterpret_cast<uintptr_t*>(car+0x24);node;node=*reinterpret_cast<uintptr_t*>(node))
                    **reinterpret_cast<uintptr_t**>(node+sizeof(uintptr_t))=0;
            check(!sa_remote_door_policy_vehicle_live(reinterpret_cast<uintptr_t>(vehicles.data())) && sa_remote_door_policy_runtime_healthy(),
                "Native destruction clears the persistent policy and permits later pool reuse without inherited ownership");
            remote_door_reference_mode=false;
            vehicles=saved_vehicles;
            *reinterpret_cast<uintptr_t*>(model.data()+0x1C)=0;
            result=sa_execute_entity_command(create);
            check(!result.executed && !result.native_entered && rc_spawns==2,"Unstreamed RC model refuses before constructor entry");
            *reinterpret_cast<uintptr_t*>(model.data()+0x1C)=1;
            create.inputs[3]=entity_bits(1);rc_spawn_fault=true;
            result=sa_execute_entity_command(create);rc_spawn_fault=false;
            check(!result.executed && result.native_entered && rc_spawns==3,"Entered RC factory fault is retained and cannot invite replay");
            pointer(0xA9B0C8+441*4,nullptr);*slot=0;vehicles=saved_vehicles;
            actor_reference_mode=false;
        }
        auto command=operation(EntityCommand::blow_up_rc_buggy,{});
        const auto player=native_entity_address(0xB7CD98);
        auto* remote=reinterpret_cast<uintptr_t*>(player+0xB0);
        *reinterpret_cast<uint8_t*>(native_entity_address(0xB7CD74))=0;
        *remote=0;auto result=sa_execute_entity_command(command);
        check(result.executed && !result.native_entered && rc_explosions==0,"Absent RC car is the original legitimate idle branch");
        *remote=reinterpret_cast<uintptr_t>(vehicles.data());const auto preserved=vehicles;
        result=sa_execute_entity_command(command);
        check(result.executed && result.native_entered && rc_explosions==1 && rc_player==player && vehicles==preserved,
            "Real source operation enters the explosion service once for the focused player, without local damage/camera substitutes");
        vehicles[0x1D]|=8;result=sa_execute_entity_command(command);
        check(result.executed && !result.native_entered && rc_explosions==1,"Removed RC vehicle is not exploded again");vehicles[0x1D]&=~8u;
        for(uintptr_t bad:{uintptr_t(0xDEADBEEF),reinterpret_cast<uintptr_t>(vehicles.data()+1),reinterpret_cast<uintptr_t>(vehicles.data()+0xA18),reinterpret_cast<uintptr_t>(objects.data())}) {
            *remote=bad;result=sa_execute_entity_command(command);
            check(!result.executed && !result.native_entered && rc_explosions==1,"Dangling, unallocated, misaligned or nonvehicle RC pointer never reaches the engine");
        }
        *remote=reinterpret_cast<uintptr_t>(vehicles.data());ped_flag|=0x80;result=sa_execute_entity_command(command);
        check(!result.executed && !result.native_entered && rc_explosions==1,"Missing focused actor cannot be passed as the explosion creator");ped_flag=27;
        *reinterpret_cast<uint8_t*>(native_entity_address(0xB7CD74))=2;result=sa_execute_entity_command(command);
        check(!result.executed && !result.native_entered,"Corrupted focus cannot index beyond the two native players");
        *reinterpret_cast<uint8_t*>(native_entity_address(0xB7CD74))=1;
        pointer(0xB7CD98+0x190,ped);pointer(0xB7CD98+0x190+0xB0,vehicles.data());
        result=sa_execute_entity_command(command);
        check(result.executed && rc_player==player+0x190 && rc_explosions==2,"Focused player one is preserved instead of assuming player zero");
        *reinterpret_cast<uint8_t*>(native_entity_address(0xB7CD74))=0;
        for(uintptr_t address:{uintptr_t(0x56EA30),uintptr_t(0x45AE80),uintptr_t(0x1562160),uintptr_t(0x56EA73),uintptr_t(0x56EA45),uintptr_t(0x56EA58),uintptr_t(0x15621F7)}) {
            auto* byte=reinterpret_cast<uint8_t*>(native_entity_address(address));const auto saved=*byte;*byte^=1;
            result=sa_execute_entity_command(command);check(!result.executed && !result.native_entered && rc_explosions==2,"Changed native service or ABI rejects without modifying a foreign hook");*byte=saved;
        }
        command.inputs={1};result=sa_execute_entity_command(command);
        check(!result.executed && !result.native_entered,"RC explosion has no extra source player operand");command.inputs.clear();
        rc_fault=true;result=sa_execute_entity_command(command);rc_fault=false;
        check(!result.executed && result.native_entered && rc_explosions==3,"Exception after entry preserves the mutation receipt and cannot invite automatic replay");
        *remote=0;pointer(0xB7CD98+0x190,nullptr);pointer(0xB7CD98+0x190+0xB0,nullptr);
    }
    {
        // 1512: real source cancellation (no SCM operands); native helper is
        // stubbed, and PE signatures are held in non-executable owned pages.
        const auto command=operation(EntityCommand::cancel_remote_mode,{});
        auto* focus=reinterpret_cast<uint8_t*>(native_entity_address(0xB7CD74));*focus=0;
        const auto player=native_entity_address(0xB7CD98);
        const auto remote_car=reinterpret_cast<uintptr_t>(vehicles.data());
        const auto physical_car=reinterpret_cast<uintptr_t>(vehicles.data()+2*0xA18);
        auto* remote_slot=reinterpret_cast<uintptr_t*>(player+0xB0);
        auto* ped_car=reinterpret_cast<uintptr_t*>(ped+0x58C);
        auto* cleanup=reinterpret_cast<uint8_t*>(native_entity_address(0xA90850));
        std::array<uintptr_t,2> remote_refnode{};
        const auto init=[&](bool in_car=true,bool with_remote=true){
            cancel_fault=0;cancel_releases=cancel_camera_restores=0;cancel_sequence.clear();
            entity_references.clear();std::memset(cleanup,0,0x25C);
            vehicle_flags[0]=7;vehicle_flags[2]=12;ped_flag=27;
            vehicles[0x36]=vehicles[2*0xA18+0x36]=2;
            vehicles[0x1D]&=~8u;
            vehicles[0x4A4]=2;vehicles[0x428]=0xFF;
            vehicles[2*0xA18+0x42A]=0;
            ped[0x46D]=in_car?uint8_t(ped[0x46D]|1):uint8_t(ped[0x46D]&~1u);
            *ped_car=in_car?physical_car:uintptr_t(0);
            *remote_slot=with_remote?remote_car:uintptr_t(0);
            *reinterpret_cast<uint8_t*>(player+0xDD)=8;
            *reinterpret_cast<uint8_t*>(player+0xDE)=8;
            *reinterpret_cast<uint8_t*>(player+0xDF)=8;
            *reinterpret_cast<uint32_t*>(native_entity_address(0xB7CB84))=0xAABBCCDD;
            *reinterpret_cast<uint32_t*>(player+0xE0)=0;
            *reinterpret_cast<uintptr_t*>(remote_car+0x24)=0;
            if(with_remote){native_register_reference(remote_car,remote_slot);
                remote_refnode={{0,reinterpret_cast<uintptr_t>(remote_slot)}};
                *reinterpret_cast<uintptr_t*>(remote_car+0x24)=reinterpret_cast<uintptr_t>(remote_refnode.data());
                cleanup[0]=1;*reinterpret_cast<int32_t*>(cleanup+4)=7;cleanup[0x258]=1;}
        };
        init();auto result=sa_execute_entity_command(command);
        check(result.executed && result.native_entered && cancel_releases==1 && !cancel_explode_argument &&
              cancel_camera_restores==1 && *remote_slot==0 &&
              *reinterpret_cast<uint8_t*>(player+0xDD)==1 &&
              *reinterpret_cast<uint8_t*>(player+0xDE)==0 &&
              *reinterpret_cast<uint8_t*>(player+0xDF)==0 &&
              *reinterpret_cast<uint32_t*>(player+0xE0)==0xAABBCCDD &&
              (vehicles[2*0xA18+0x42A]&0x20) &&
              (vehicles[0x428]&8)==0 && vehicles[0x4A4]==1 &&
              cleanup[0]==0 && cleanup[0x258]==0 && entity_references.empty() &&
              cancel_sequence==std::vector<int>{2,3,4},
              "1512 restores physical damage, releases RC without explosion, removes ownership and reference, retains DD and clears DF, then restores camera");
        init(false,false);const auto before_refs=reference_cleanups;
        result=sa_execute_entity_command(command);
        check(result.executed && result.native_entered && cancel_releases==0 && cancel_camera_restores==1 &&
              reference_cleanups==before_refs && *reinterpret_cast<uint8_t*>(player+0xDD)==1 &&
              *reinterpret_cast<uint8_t*>(player+0xDE)==0 && *reinterpret_cast<uint8_t*>(player+0xDF)==0 &&
              *reinterpret_cast<uint32_t*>(player+0xE0)==0xAABBCCDD,
              "No RC: preserve original state transition and camera without dereferencing native null remote");
        init();*remote_slot=0xDEADBEEF;result=sa_execute_entity_command(command);
        check(!result.executed && !result.native_entered && !cancel_releases && !cancel_camera_restores &&
              vehicles[2*0xA18+0x42A]==0,"Stale RC pointer refuses without mutating a physical car");
        init();*reinterpret_cast<uintptr_t*>(remote_car+0x24)=0;
        result=sa_execute_entity_command(command);
        check(!result.executed && !result.native_entered && !cancel_releases && !cancel_camera_restores &&
              vehicles[2*0xA18+0x42A]==0,
              "Recycled pool allocation without original player reference cannot capture remote ownership");
        init();vehicle_flags[0]|=0x80;result=sa_execute_entity_command(command);
        check(!result.executed && !result.native_entered && !cancel_releases && !cancel_camera_restores,
              "Deallocated remote pool slot cannot be released");
        init();ped_flag|=0x80;result=sa_execute_entity_command(command);
        check(!result.executed && !result.native_entered && !cancel_releases && !cancel_camera_restores,
              "Missing focused ped refuses before native effects");
        init();*focus=2;result=sa_execute_entity_command(command);
        check(!result.executed && !result.native_entered && !cancel_releases && !cancel_camera_restores,
              "Out-of-bounds focused player cannot index player array");*focus=0;
        for(auto changed:{uintptr_t(0x45AE80),uintptr_t(0x1562160),uintptr_t(0x15621CA),
             uintptr_t(0x15621F7),uintptr_t(0x6D5D70),uintptr_t(0x4654B0),
             uintptr_t(0x156E810),uintptr_t(0x48970E),uintptr_t(0x56E044),
             uintptr_t(0x47C4C7),uintptr_t(0x50B930),uintptr_t(0x571A00)}){
            init();auto* byte=reinterpret_cast<uint8_t*>(native_entity_address(changed));
            const auto saved=*byte;*byte^=1;result=sa_execute_entity_command(command);
            check(!result.executed && !result.native_entered && cancel_releases==0 && cancel_camera_restores==0 &&
                  vehicles[2*0xA18+0x42A]==0 && *remote_slot==remote_car,
                  "Changed remote release, ownership, camera or reference ABI rejects without native entry");
            *byte=saved;
        }
        for(int fault:{1,2,3,4,5,6,7}){
            init();cancel_fault=fault;result=sa_execute_entity_command(command);
            check(!result.executed && result.native_entered && cancel_releases==1,
                  "Faulted remote operation records native entry and never claims successful cancellation");
            if(fault==3 || fault==4 || fault==5)
                check(cancel_camera_restores==0,"Recycled/foreign RC pointer or reference fault must not restore camera");
        }
        init();cancel_fault=8;result=sa_execute_entity_command(command);
        check(!result.executed && result.native_entered && cancel_releases==1 && !cancel_camera_restores && *remote_slot==remote_car,
              "Recycled focused ped at identical pointer must stop cancellation before reference cleanup or camera restore");
        init();*focus=1;pointer(0xB7CD98+0x190,ped);pointer(0xB7CD98+0x190+0xB0,vehicles.data());
        const auto p1=native_entity_address(0xB7CD98+0x190);
        native_cleanup_reference(remote_car,remote_slot);
        auto* p1_slot=reinterpret_cast<uintptr_t*>(p1+0xB0);
        native_register_reference(remote_car,p1_slot);
        remote_refnode[1]=reinterpret_cast<uintptr_t>(p1_slot);
        *reinterpret_cast<uint8_t*>(p1+0xDD)=7;*reinterpret_cast<uint8_t*>(p1+0xDF)=7;
        result=sa_execute_entity_command(command);
        check(result.executed && !*reinterpret_cast<uintptr_t*>(p1+0xB0) &&
              *reinterpret_cast<uint8_t*>(p1+0xDD)==1 && *reinterpret_cast<uint8_t*>(p1+0xDF)==0 &&
              *reinterpret_cast<uint8_t*>(player+0xDD)==8,
              "1512 targets only the focused native player, not hardcoded player zero");
        *focus=0;pointer(0xB7CD98+0x190,nullptr);pointer(0xB7CD98+0x190+0xB0,nullptr);
        *remote_slot=0;*ped_car=0;ped[0x46D]&=~1u;
        cancel_fault=0;cancel_releases=cancel_camera_restores=0;
    }
    {
        NativeCall walk;walk.opcode=0x05D3;walk.inputs={27,entity_bits(10.f),entity_bits(20.f),entity_bits(3.f),4,-2};walk.mission_context=true;
        captured_walk_generation=&ped_flag;captured_walk_reply={};captured_walk_reply.executed=true;captured_walk_reply.native_entered=true;captured_walk_reply.values[0]=321;
        const auto reset_bank=[](){ScriptTaskSnapshot empty{};std::memcpy(reinterpret_cast<void*>(native_entity_address(0xC0B1E8)),empty.data(),sizeof(empty));};
        reset_bank();const auto calls=captured_walk_calls;
        auto result=sa_execute_walk_with_capture(walk);
        check(result.executed && result.native_entered && result.values[0]==321 && captured_walk_calls==calls+1 &&
            result.task_capture==NativeResult::TaskCapture::captured && result.accepted_script_task && result.accepted_script_task->slot==7 &&
            result.accepted_script_task->record.ped==uint32_t(reinterpret_cast<uintptr_t>(ped)) && captured_walk_packet.inputs==walk.inputs && captured_walk_packet.mission_context,
            "Compiled wrapper executes reviewed packet once and captures only its actual new native binding; engine boundary mocked");
        for(int mode:{1,2}) {
            reset_bank();captured_walk_mode=mode;const auto before=captured_walk_calls;result=sa_execute_walk_with_capture(walk);
            check(result.executed && result.native_entered && !result.accepted_script_task && result.task_capture==NativeResult::TaskCapture::no_unique_record && captured_walk_calls==before+1,
                "Missing/ambiguous acceptance capture does not replay or fail a successful objective command");
        }
        captured_walk_mode=0;reset_bank();auto* lookup=reinterpret_cast<uint8_t*>(native_entity_address(0x608710));const auto original=*lookup;*lookup^=1;
        result=sa_execute_walk_with_capture(walk);check(result.executed && !result.accepted_script_task && result.task_capture==NativeResult::TaskCapture::unavailable_before,
            "Unavailable advisory snapshot before command does not suppress its real acceptance");*lookup=original;
        captured_walk_mode=3;reset_bank();auto* bounds=reinterpret_cast<uint8_t*>(native_entity_address(0x60872F));const auto bounds_byte=*bounds;
        result=sa_execute_walk_with_capture(walk);check(result.executed && result.native_entered && !result.accepted_script_task && result.task_capture==NativeResult::TaskCapture::unavailable_after,
            "Snapshot loss after accepted native command retains successful receipt without replay");*bounds=bounds_byte;
        captured_walk_mode=4;reset_bank();result=sa_execute_walk_with_capture(walk);
        check(result.executed && !result.accepted_script_task && result.task_capture==NativeResult::TaskCapture::actor_changed,"Ped recycled during engine entry cannot acquire old objective ownership");ped_flag=27;
        captured_walk_mode=0;captured_walk_reply.executed=false;captured_walk_reply.native_entered=true;captured_walk_reply.diagnostic="engine fault";
        result=sa_execute_walk_with_capture(walk);check(!result.executed && result.native_entered && result.diagnostic=="engine fault" && !result.accepted_script_task,
            "Entered engine fault is preserved rather than presented as successful capture");captured_walk_reply.executed=true;
        const auto before_invalid=captured_walk_calls;walk.inputs[0]=28;
        check(!sa_execute_walk_with_capture(walk).executed && captured_walk_calls==before_invalid,"Invalid ped generation rejects before native execution");walk.inputs[0]=27;
        walk.inputs[1]=entity_bits(std::numeric_limits<float>::quiet_NaN());
        check(!sa_execute_walk_with_capture(walk).executed && captured_walk_calls==before_invalid,"Non-finite goal refuses before native task construction");walk.inputs[1]=entity_bits(10.f);
        walk.source_entity_command=EntityCommand::char_drowning;
        check(!sa_execute_walk_with_capture(walk).executed && captured_walk_calls==before_invalid,"Mixed service modes cannot enter acceptance wrapper");walk.source_entity_command.reset();
        walk.inputs[4]=99;check(!sa_execute_walk_with_capture(walk).executed && captured_walk_calls==before_invalid,"Unreviewed movement enum cannot construct a native AI task");walk.inputs[4]=4;
        walk.inputs[5]=0;check(!sa_execute_walk_with_capture(walk).executed && captured_walk_calls==before_invalid,"Finite/instant timeout cannot substitute original untimed walking objective");walk.inputs[5]=-2;
        {
            // TASK_GOTO_CHAR (05D9): reviewed (actor, live target, untimed, radius 1.0) packet only.
            NativeCall goto_char;goto_char.opcode=0x05D9;goto_char.inputs={27,27,-1,0x3F800000};goto_char.mission_context=true;
            captured_walk_mode=0;captured_walk_reply.executed=true;reset_bank();
            result=sa_execute_walk_with_capture(goto_char);
            check(result.executed && result.native_entered && result.task_capture==NativeResult::TaskCapture::captured &&
                  result.accepted_script_task && result.accepted_script_task->record.opcode==0x05D9 && captured_walk_packet.inputs==goto_char.inputs,
                  "GOTO_CHAR executes its reviewed packet once and captures the unique 05D9 binding");
            const auto invalid_calls=captured_walk_calls;
            for(const auto& [index,value]:{std::pair<size_t,int32_t>{2,0},std::pair<size_t,int32_t>{3,0x3F000000},std::pair<size_t,int32_t>{1,28}}) {
                auto bad=goto_char;bad.inputs[index]=value;
                check(!sa_execute_walk_with_capture(bad).executed && captured_walk_calls==invalid_calls,
                      "A timed/foreign-radius/dead-target GOTO_CHAR packet is refused before native entry");
            }
            auto stale_opcode=goto_char;stale_opcode.opcode=0x05D8;
            check(!sa_execute_walk_with_capture(stale_opcode).executed && captured_walk_calls==invalid_calls,"Other opcodes cannot use the GOTO_CHAR acceptance shape");
        }
        {
            // The four source kill objectives share SA's reviewed 05E2 (actor, live target) packet.
            NativeCall kill;kill.opcode=0x05E2;kill.inputs={27,27};kill.mission_context=true;
            captured_walk_mode=0;captured_walk_reply.executed=true;reset_bank();
            result=sa_execute_walk_with_capture(kill);
            check(result.executed && result.native_entered && result.task_capture==NativeResult::TaskCapture::captured &&
                  result.accepted_script_task && result.accepted_script_task->record.opcode==0x05E2 &&
                  captured_walk_packet.inputs==kill.inputs,"KILL_CHAR acceptance captures its unique 05E2 task receipt");
            const auto invalid_calls=captured_walk_calls;auto bad=kill;bad.inputs[1]=28;
            check(!sa_execute_walk_with_capture(bad).executed && captured_walk_calls==invalid_calls,
                  "KILL_CHAR with a stale target is rejected before native entry");
        }
        reset_bank();captured_walk_generation=nullptr;captured_walk_reply={};
    }
    {
        profile(pe,0x681720,12);profile(pe,0x61A5A0,11);profile(pe,0x61A55A,3);profile(pe,0x421190,4);
        std::array<uint8_t,384> tasks{};std::array<uint8_t,3> generations{{1,2,3}};
        Pool task_pool{tasks.data(),generations.data(),3};pointer(0xB744A8,&task_pool);
        std::array<uintptr_t,13> intelligence{};*reinterpret_cast<uintptr_t*>(ped+0x47C)=reinterpret_cast<uintptr_t>(intelligence.data());
        auto* table=reinterpret_cast<uintptr_t*>(native_entity_address(0x86FDA8));
        const auto old_getter=table[2];table[2]=0x421190;
        const auto root=reinterpret_cast<uintptr_t>(tasks.data()),target=root+128;
        *reinterpret_cast<uintptr_t*>(root)=reinterpret_cast<uintptr_t>(table);
        *reinterpret_cast<uintptr_t*>(root+8)=target;
        *reinterpret_cast<uintptr_t*>(target)=reinterpret_cast<uintptr_t>(table);
        *reinterpret_cast<uintptr_t*>(target+4)=root;intelligence[1]=root;
        auto* bank=reinterpret_cast<ScriptTaskRecord*>(native_entity_address(0xC0B1E8));
        const ScriptTaskRecord previous=bank[7];
        bank[7]={0x05D3,0,uint32_t(target),uint32_t(reinterpret_cast<uintptr_t>(ped)),2};
        const CapturedScriptTask owned{7,bank[7]};const ObjectiveActor actor{27,reinterpret_cast<uintptr_t>(ped)};
        const auto preserved=tasks;
        check(sa_active_walking_task(actor,owned)==std::optional<bool>{true} && tasks==preserved,
            "Owned accepted task in active primary tree is read without executing virtual getters or mutating task storage");
        bank[7].opcode=0x0A2E;
        const CapturedScriptTask node_route_owned{7,bank[7]};
        check(sa_active_walking_task(actor,node_route_owned)==std::optional<bool>{true},
              "Accepted radius-aware node route uses the same read-only active-task consumer");
        bank[7].opcode=0x05E2;
        const CapturedScriptTask kill_owned{7,bank[7]};
        check(sa_active_walking_task(actor,kill_owned)==std::optional<bool>{true},
              "Accepted kill objective uses the same exact active-primary-task reader");
        bank[7]=owned.record;
        intelligence[1]=0;intelligence[2]=root;
        check(sa_active_walking_task(actor,owned)==std::optional<bool>{true},"First actual non-null primary root is followed");
        intelligence[1]=root+256;*reinterpret_cast<uintptr_t*>(root+256)=reinterpret_cast<uintptr_t>(table);
        check(sa_active_walking_task(actor,owned)==std::optional<bool>{false},"Suspended lower-priority goal does not count as active ownership");
        intelligence[1]=root;generations[1]|=0x80;
        check(!sa_active_walking_task(actor,owned),"Freed accepted task slot does not become a valid negative query");generations[1]=2;
        *reinterpret_cast<uintptr_t*>(target+4)=0;
        check(!sa_active_walking_task(actor,owned),"Inconsistent child parent link defers ownership query");*reinterpret_cast<uintptr_t*>(target+4)=root;
        const auto getter_byte=*reinterpret_cast<uint8_t*>(native_entity_address(0x421190));
        *reinterpret_cast<uint8_t*>(native_entity_address(0x421190))=0xE9;
        check(!sa_active_walking_task(actor,owned),"Foreign getter hook is neither executed nor interpreted as the original tree");
        *reinterpret_cast<uint8_t*>(native_entity_address(0x421190))=getter_byte;
        bank[7].task=uint32_t(root+256);
        check(!sa_active_walking_task(actor,owned),"Record changed to another task cannot prove original task membership");bank[7]=owned.record;
        check(!sa_active_walking_task({28,actor.native_ped},owned),"Recycled actor generation cannot lend ownership to a new ped");
        DWORD protection=0,ignored=0;check(VirtualProtect(ped,4096,PAGE_NOACCESS,&protection)!=0,"Protect owned pedestrian fixture for read fault");
        check(!sa_active_walking_task(actor,owned),"Unreadable actor memory is contained without entering any engine routine");
        check(VirtualProtect(ped,4096,protection,&ignored)!=0,"Restore owned actor fixture");
        bank[7]=previous;table[2]=old_getter;*reinterpret_cast<uintptr_t*>(ped+0x47C)=0;pointer(0xB744A8,nullptr);
    }
    {
        profile(pe,0x681720,12);profile(pe,0x61A5A0,11);profile(pe,0x61A55A,3);profile(pe,0x62F240,6);profile(pe,0x421190,4);
        std::array<uint8_t,384> tasks{};std::array<uint8_t,3> generations{{1,2,3}};
        Pool task_pool{tasks.data(),generations.data(),3};pointer(0xB744A8,&task_pool);
        std::array<uintptr_t,13> intelligence{},other_table{},die_table{};
        const auto root=reinterpret_cast<uintptr_t>(tasks.data()),child=root+128;
        *reinterpret_cast<uintptr_t*>(ped+0x47C)=reinterpret_cast<uintptr_t>(intelligence.data());
        other_table[2]=0x421190;other_table[4]=0x681740;
        die_table[2]=0x421190;die_table[4]=0x62F240;
        const std::array<uint8_t,6> other_type{{0xB8,0xC0,0x02,0,0,0xC3}}; // ordinary leave-car 704
        std::array<uint8_t,6> old_type{};std::memcpy(old_type.data(),reinterpret_cast<const void*>(native_entity_address(0x681740)),6);
        std::memcpy(reinterpret_cast<void*>(native_entity_address(0x681740)),other_type.data(),6);
        *reinterpret_cast<uintptr_t*>(root)=reinterpret_cast<uintptr_t>(other_table.data());
        *reinterpret_cast<uintptr_t*>(root+8)=child;
        *reinterpret_cast<uintptr_t*>(child)=reinterpret_cast<uintptr_t>(die_table.data());
        *reinterpret_cast<uintptr_t*>(child+4)=root;
        intelligence[1]=root;
        auto query=operation(EntityCommand::char_leaving_vehicle_to_die,{27});query.predicate=true;
        for(const auto address:{uintptr_t(0x681720),uintptr_t(0x61A5A0),uintptr_t(0x61A55A),uintptr_t(0x62F240)}) {
            auto* byte=reinterpret_cast<uint8_t*>(native_entity_address(address));const auto saved=*byte;*byte^=1;
            const auto refused=sa_execute_entity_command(query);
            check(!refused.executed && !refused.native_entered,"Changed primary-tree/pool/type profile cannot grant a goal predicate");*byte=saved;
        }
        const auto saved_tasks=tasks;const auto saved_intelligence=intelligence;
        auto answer=sa_execute_entity_command(query);
        check(answer.executed && !answer.native_entered && answer.condition && tasks==saved_tasks && intelligence==saved_intelligence,
            "Leave-and-die query reads actual native goal in a nested primary tree without executing virtual calls or changing tasks");
        intelligence[1]=root+256;*reinterpret_cast<uintptr_t*>(root+256)=reinterpret_cast<uintptr_t>(other_table.data());intelligence[4]=root;
        answer=sa_execute_entity_command(query);check(answer.executed && answer.condition,"Suspended primary objective remains visible behind a physical response");
        intelligence.fill(0);*reinterpret_cast<float*>(ped+0x540)=0; // zero health alone is not this objective
        answer=sa_execute_entity_command(query);check(answer.executed && !answer.condition,"No leave-and-die goal is false, regardless of zero health");
        intelligence[1]=root;*reinterpret_cast<uintptr_t*>(root+8)=0;
        answer=sa_execute_entity_command(query);check(answer.executed && !answer.condition,"Ordinary leave-car is distinct from leave-and-die");
        *reinterpret_cast<uintptr_t*>(root+8)=child;generations[1]|=0x80;
        answer=sa_execute_entity_command(query);check(!answer.executed && !answer.native_entered,"Freed child cannot manufacture a false goal query");generations[1]=2;
        *reinterpret_cast<uintptr_t*>(child+4)=0;
        check(!sa_execute_entity_command(query).executed,"Malformed parent link defers native goal inspection");*reinterpret_cast<uintptr_t*>(child+4)=root;
        *reinterpret_cast<uint8_t*>(native_entity_address(0x681740))=0xE9;
        check(!sa_execute_entity_command(query).executed,"Foreign virtual getter is not invoked or interpreted as a constant task type");
        std::memcpy(reinterpret_cast<void*>(native_entity_address(0x681740)),other_type.data(),6);
        query.inputs[0]=28;check(!sa_execute_entity_command(query).executed,"Recycled ped generation is refused before tree access");query.inputs[0]=27;
        DWORD protection=0,ignored=0;check(VirtualProtect(ped,4096,PAGE_NOACCESS,&protection)!=0,"Protect owned query fixture");
        check(!sa_execute_entity_command(query).executed,"Unreadable goal state stays contained rather than crashing native mods");
        check(VirtualProtect(ped,4096,protection,&ignored)!=0,"Restore query fixture");
        std::memcpy(reinterpret_cast<void*>(native_entity_address(0x681740)),old_type.data(),6);
        *reinterpret_cast<uintptr_t*>(ped+0x47C)=0;pointer(0xB744A8,nullptr);
    }
    {
        NativeCall path=operation(EntityCommand::char_wander_path,{27,entity_bits(10.f),entity_bits(0.f),entity_bits(.5f),entity_bits(1.f)});path.predicate=true;
        WanderPoint origin{{0,0,.5f}};std::memcpy(ped+4,origin.data(),12);
        std::array<uint8_t,0x7C4> before{};std::memcpy(before.data(),ped,before.size());
        auto answer=sa_execute_entity_command(path);
        check(answer.executed && answer.native_entered && answer.condition && wander_los_calls==1 && wander_water_calls==3 && wander_ray_calls==4 &&
            wander_seen_origin==origin && wander_seen_target==WanderPoint{{10,0,.5f}} && std::memcmp(before.data(),ped,before.size())==0,
            "Original ped wander query uses live position and bounded actual engine queries, preserving ped state");
        std::array<float,16> matrix{};matrix[12]=2.f;matrix[13]=3.f;matrix[14]=.5f;*reinterpret_cast<const void**>(ped+0x14)=matrix.data();
        answer=sa_execute_entity_command(path);check(answer.executed && wander_seen_origin==WanderPoint{{2,3,.5f}},"Matrix position takes precedence over stale placement coordinates");*reinterpret_cast<uintptr_t*>(ped+0x14)=0;
        for(int32_t handle:{-1,28,0x80,0x11B}) {
            path.inputs[0]=handle;const auto calls=wander_los_calls;answer=sa_execute_entity_command(path);
            check(!answer.executed && !answer.native_entered && wander_los_calls==calls,"Free, recycled and out-of-range ped cannot enter world query");
        }path.inputs[0]=27;
        path.inputs[1]=entity_bits(std::numeric_limits<float>::quiet_NaN());const auto calls=wander_los_calls;
        check(!sa_execute_entity_command(path).executed && wander_los_calls==calls,"Invalid SCM target refused before native world query");path.inputs[1]=entity_bits(10.f);
        wander_los_clear=false;answer=sa_execute_entity_command(path);check(answer.executed && !answer.condition,"Actual obstructed LOS is a valid false predicate");wander_los_clear=true;
        wander_ground_result={false,{}};answer=sa_execute_entity_command(path);check(!answer.executed && answer.native_entered,"Query failure after engine entry cannot invent an unobstructed mission predicate");wander_ground_result={true,0.f};
        wander_query_fault=true;answer=sa_execute_entity_command(path);check(!answer.executed && answer.native_entered,"World query exception contained with entered receipt");wander_query_fault=false;
        for(uintptr_t address:{uintptr_t(0x56A490),uintptr_t(0x5674E0),uintptr_t(0x407C37),uintptr_t(0x6EB690)}) {
            auto* byte=reinterpret_cast<uint8_t*>(native_entity_address(address));const auto saved=*byte;*byte^=1;
            answer=sa_execute_entity_command(path);check(!answer.executed && !answer.native_entered,"Modified world helper or Hoodlum trampoline refused without patching another mod");*byte=saved;
        }
        std::memset(ped+4,0,12);
    }
    auto* player=reinterpret_cast<uint8_t*>(native_entity_address(0xB7CD98));player[0x14F]=100;player[0x150]=50;
    NativeCall frenzy;frenzy.source_frenzy_headshots=false;frenzy.text="RAMPAGE";frenzy.inputs={22,120000,65537,-1,105,445,-2,238};
    auto* frenzy_status=reinterpret_cast<uint16_t*>(native_entity_address(0x96A704));
    for(bool headshots:{false,true}) {
        *frenzy_status=0;frenzy.source_frenzy_headshots=headshots;const auto calls=frenzy_starts;
        const auto started=sa_execute_frenzy_command(frenzy);
        check(started.executed && started.native_entered && frenzy_starts==calls+1 && frenzy_headshots==headshots && frenzy_key=="RAMPAGE" && frenzy_arguments==std::array<int32_t,8>{{22,120000,1,-1,105,445,-2,1}},"Compiled frenzy preserves source uint16 kills, all four model selectors, sound and headshot flags");
        const auto collision=sa_execute_frenzy_command(frenzy);
        check(!collision.executed && !collision.native_entered && frenzy_starts==calls+1,"Existing native rampage cannot be displaced or duplicated");
    }
    for(uint16_t foreign:{uint16_t(4),uint16_t(5),uint16_t(65535)}) {*frenzy_status=foreign;const auto calls=frenzy_starts;
        check(!sa_execute_frenzy_command(frenzy).executed && frenzy_starts==calls,"Coop or unknown native status cannot be overwritten");}
    *frenzy_status=2;frenzy_text_missing=true;auto missing=sa_execute_frenzy_command(frenzy);frenzy_text_missing=false;
    check(!missing.executed && !missing.native_entered && *frenzy_status==2,"Unavailable text refuses before a state/inventory change");
    pointer(0xB7CD98+0x190,ped);const auto calls=frenzy_starts;
    check(!sa_execute_frenzy_command(frenzy).executed && frenzy_starts==calls,"Source single-player rampage refuses a native second player");pointer(0xB7CD98+0x190,nullptr);
    ped_flag|=0x80;check(!sa_execute_frenzy_command(frenzy).executed,"Released native player slot cannot enter Darkel");ped_flag=27;
    *reinterpret_cast<uint8_t*>(native_entity_address(0x43D465))=0x90;
    check(!sa_execute_frenzy_command(frenzy).executed,"Altered headshot ABI cannot enter rampage");profile(pe,0x43D465,25);
    frenzy_bad_flags=true;auto incomplete=sa_execute_frenzy_command(frenzy);frenzy_bad_flags=false;
    check(!incomplete.executed && incomplete.native_entered,"Ignored headshot flag is detected after entry and forbids replay");
    *frenzy_status=0;frenzy_fault=true;const auto faulted=sa_execute_frenzy_command(frenzy);frenzy_fault=false;
    check(!faulted.executed && faulted.native_entered && *frenzy_status==1,"Injected engine fault preserves entered receipt and actual changed state");
    *frenzy_status=0;frenzy.inputs[0]=47;const auto invalid_weapon=sa_execute_frenzy_command(frenzy);
    check(!invalid_weapon.executed && !invalid_weapon.native_entered,"Native weapon terminator cannot enter inventory lookup");frenzy.inputs[0]=22;
    frenzy.outputs=1;check(!sa_execute_frenzy_command(frenzy).executed,"Frenzy cannot invent a source output");frenzy.outputs=0;
    frenzy.source_statistic=SourceStatistic::taxi_money;check(!sa_execute_frenzy_command(frenzy).executed,"Mixed typed operation cannot bypass mode guards");frenzy.source_statistic.reset();
    check(sa_execute_entity_command(operation(EntityCommand::player_max_health,{0,20})).executed && player[0x14F]==120 &&
          *reinterpret_cast<float*>(ped+0x540)==120 && *reinterpret_cast<int16_t*>(native_entity_address(0xBAB1DC))==4,"Health reward updates maximum, ped health and HUD flash");
    check(sa_execute_entity_command(operation(EntityCommand::player_max_armour,{0,10})).executed && player[0x150]==60 &&
          *reinterpret_cast<float*>(ped+0x548)==60 && *reinterpret_cast<int16_t*>(native_entity_address(0xBAB1DC))==3,"Armour reward updates its maximum, ped armour and HUD item");
    check(sa_execute_entity_command(operation(EntityCommand::player_fireproof,{0,1})).executed && player[0x14E]==1,"Fireproof changes actual player-info flag");
    check(sa_execute_entity_command(operation(EntityCommand::get_out_of_jail_free,{0,1})).executed && player[0x151]==1,"Jail reward changes actual player-info flag");
    ped_flag|=0x80;const auto before_max=player[0x14F];auto invalid_player=sa_execute_entity_command(operation(EntityCommand::player_max_health,{0,20}));
    check(!invalid_player.executed && !invalid_player.native_entered && player[0x14F]==before_max,"Removed player cannot receive a reward through a stale pointer");ped_flag=27;
    DWORD old=0;check(VirtualProtect(ped,0x1000,PAGE_READONLY,&old)!=0,"Fault injection must protect our owned ped page");
    auto partial=sa_execute_entity_command(operation(EntityCommand::player_max_health,{0,1}));
    check(!partial.executed && partial.native_entered && player[0x14F]==121 && *reinterpret_cast<float*>(ped+0x540)==120,"Backend reports partial entry when the second reward write faults");
    auto guarded=guard_native_executor(sa_execute_entity_command);bool prevented=false;
    try{guarded(operation(EntityCommand::player_max_armour,{0,1}));}catch(const std::runtime_error&){prevented=true;}
    check(prevented,"Actual backend partial fault reaches the no-replay repair guard");
    check(VirtualProtect(ped,0x1000,old,&old)!=0,"Owned page protection must restore");
    // The garage family below also needs this live generation-owned player.
    // Retain its page until those scenarios finish.
    for(auto stat:{SourceStatistic::taxi_money,SourceStatistic::taxi_passengers,SourceStatistic::ambulance_lives,SourceStatistic::vigilante_criminals,
                  SourceStatistic::extinguished_fires,SourceStatistic::vigilante_level,SourceStatistic::ambulance_level,SourceStatistic::fire_level}) {
        const auto slot=*source_stat_slot(stat);auto* counter=reinterpret_cast<int32_t*>(native_entity_address(0xB79000)+slot*4);*counter=20000000;
        NativeCall update;update.source_statistic=stat;update.inputs={source_stat_record(stat)?20000001:1};auto result=sa_execute_statistic_command(update);
        check(result.executed && result.native_entered && *counter==20000001,"Compiled statistic write preserves large integer values");
        *reinterpret_cast<uint8_t*>(native_entity_address(0x558E40))=0x90;auto refused=sa_execute_statistic_command(update);
        check(!refused.executed && !refused.native_entered && *counter==20000001,"Changed stat profile rejects without updating memory");profile(pe,0x558E40,33);
    }
    NativeCall environment;environment.source_environment=EnvironmentCommand::read_weather;environment.outputs=4;
    auto* interpolation=reinterpret_cast<int32_t*>(native_entity_address(0xC8130C));auto* rain=reinterpret_cast<int32_t*>(native_entity_address(0xC81324));
    auto* new_type=reinterpret_cast<int16_t*>(native_entity_address(0xC8131C));auto* old_type=reinterpret_cast<int16_t*>(native_entity_address(0xC81320));
    auto* forced=reinterpret_cast<int16_t*>(native_entity_address(0xC81318));auto* force_rain=reinterpret_cast<uint8_t*>(native_entity_address(0xC812AC));
    *interpolation=entity_bits(.375f);*rain=entity_bits(.625f);*new_type=8;*old_type=4;*forced=16;*force_rain=1;
    const auto captured=sa_execute_environment_command(environment);
    check(captured.executed && !captured.native_entered && captured.values[0]==*interpolation && captured.values[1]==*rain && captured.values[2]==8 && captured.values[3]==4,"Production environment capture reads actual float bits and int16 weather fields");
    auto* clock=reinterpret_cast<uint8_t*>(native_entity_address(0xB70144));std::memset(clock,0x5A,0x20);
    *reinterpret_cast<uint16_t*>(clock+0xC)=65535;clock[0xE]=59;clock[0xF]=23;
    const std::array<uint8_t,8> saved_clock{{clock[0],clock[4],clock[5],clock[6],clock[7],clock[8],clock[9],clock[0xA]}};
    const auto month=clock[0x11],day=clock[0x10];const auto last_tick=*reinterpret_cast<const uint32_t*>(clock+0x14);
    environment.source_environment=EnvironmentCommand::restore_weather_and_store_clock;environment.outputs=3;
    environment.inputs={entity_bits(.8f),entity_bits(.2f),9,8};
    const auto restored=sa_execute_environment_command(environment);
    check(restored.executed && restored.native_entered && restored.values[0]==23 && restored.values[1]==59 && restored.values[2]==65535 &&
        *interpolation==environment.inputs[0] && *rain==environment.inputs[1] && *new_type==9 && *old_type==8 && *forced==16 && *force_rain==1,"Weather restore captures exact current source-width clock and preserves forced weather/rain policy");
    *reinterpret_cast<uint8_t*>(native_entity_address(0x52D020))=0x90;const auto before_weather=*interpolation;
    check(!sa_execute_environment_command(environment).executed && *interpolation==before_weather,"Combined restore preflights its clock access before changing weather");profile(pe,0x52D020,40);
    environment.inputs[0]=entity_bits(std::numeric_limits<float>::quiet_NaN());
    check(!sa_execute_environment_command(environment).executed && *interpolation==before_weather,"Non-finite weather cannot poison native climate fields");environment.inputs[0]=entity_bits(.4f);
    environment.inputs[2]=23;check(!sa_execute_environment_command(environment).executed && *new_type==9,"Unknown weather type cannot index SA climate tables");environment.inputs[2]=9;
    environment.source_environment=EnvironmentCommand::read_clock;environment.inputs.clear();environment.outputs=3;
    const auto clock_read=sa_execute_environment_command(environment);
    check(clock_read.executed && !clock_read.native_entered && clock_read.values[2]==65535,"Read preserves full unsigned sixteen-bit source seconds");
    environment.source_environment=EnvironmentCommand::restore_clock;environment.inputs={7,8,321};environment.outputs=0;
    const auto clock_restored=sa_execute_environment_command(environment);
    check(clock_restored.executed && clock_restored.native_entered && clock[0xF]==7 && clock[0xE]==8 && *reinterpret_cast<uint16_t*>(clock+0xC)==321 &&
        clock[0x10]==day && clock[0x11]==month && *reinterpret_cast<const uint32_t*>(clock+0x14)==last_tick &&
        saved_clock==std::array<uint8_t,8>{{clock[0],clock[4],clock[5],clock[6],clock[7],clock[8],clock[9],clock[0xA]}},"Source RestoreClock leaves SA calendar, clock tick and mods' saved-clock bank intact");
    environment.inputs[2]=65536;check(!sa_execute_environment_command(environment).executed,"Clock operand must fit actual source seconds field");environment.inputs[2]=321;
    environment.source_entity_command=EntityCommand::car_alarm;check(!sa_execute_environment_command(environment).executed,"Mixed environment mode refuses before native memory access");environment.source_entity_command.reset();
    DWORD climate_old=0;check(VirtualProtect(reinterpret_cast<void*>(native_entity_address(0xB70000)),0x1000,PAGE_READONLY,&climate_old)!=0,"Clock fault injection only protects owned data pages");
    const auto clock_fault=sa_execute_environment_command(environment);
    check(!clock_fault.executed && clock_fault.native_entered,"Entered clock write fault carries a no-replay receipt");
    check(VirtualProtect(reinterpret_cast<void*>(native_entity_address(0xB70000)),0x1000,climate_old,&climate_old)!=0,"Owned clock page protection must restore");
    environment.source_environment=EnvironmentCommand::read_weather;environment.outputs=4;environment.inputs.clear();*rain=entity_bits(std::numeric_limits<float>::infinity());
    check(!sa_execute_environment_command(environment).executed,"Capture rejects non-finite live rain rather than committing invalid source snapshot");
    auto garage_call=[](GarageCommand action,std::vector<int32_t> inputs={},std::optional<std::string> name={}) {
        NativeCall c;c.source_garage=action;c.inputs=std::move(inputs);c.text=std::move(name);
        c.predicate=action==GarageCommand::car_delivered || action==GarageCommand::respray_happened || action==GarageCommand::contains_car || action==GarageCommand::car_bomb_type;return c;
    };
    auto* garage_count=reinterpret_cast<int32_t*>(native_entity_address(0x96C024));*garage_count=2;
    auto* garage_data=reinterpret_cast<uint8_t*>(native_entity_address(0x96C048));
    std::memcpy(garage_data+0x44,"NATIVE01",8);garage_data[0x4C]=19;
    for(uint8_t state=0;state<=5;++state){garage_data[0x4D]=state;const auto status=sa_execute_garage_command(garage_call(GarageCommand::car_delivered,{},"NATIVE01"));
        check(status.executed && !status.native_entered && status.condition==(state==5),"Delivery reads actual native closed-with-delivered-car state only");}
    garage_data[0x4D]=6;check(!sa_execute_garage_command(garage_call(GarageCommand::car_delivered,{},"NATIVE01")).executed,"Unknown native door state cannot become a mission result");garage_data[0x4D]=5;
    garage_data[0x4E]=0xEF;const auto resprayed=sa_execute_garage_command(garage_call(GarageCommand::respray_happened,{},"NATIVE01"));
    check(resprayed.executed && resprayed.native_entered && resprayed.condition && garage_data[0x4E]==0xEB,"Respray consumes exactly native bit 2 and preserves other garage policies");
    const auto repeated_respray=sa_execute_garage_command(garage_call(GarageCommand::respray_happened,{},"NATIVE01"));
    check(repeated_respray.executed && !repeated_respray.condition,"Consumed respray flag does not report success twice");
    for(int32_t invalid:{-1,51}){*garage_count=invalid;check(!sa_execute_garage_command(garage_call(GarageCommand::car_delivered,{},"NATIVE01")).executed,"Garage count checked before indexing");}*garage_count=2;
    check(!sa_execute_garage_command(garage_call(GarageCommand::car_delivered,{},"UNKNOWN")).executed,"Unknown name does not fall back to index zero");
    std::memcpy(garage_data+0xD8+0x44,"NATIVE01",8);garage_data[0xD8+0x4C]=19;
    check(!sa_execute_garage_command(garage_call(GarageCommand::respray_happened,{},"NATIVE01")).executed,"Duplicate native names refuse a destructive predicate");garage_data[0xD8+0x44]=0;
    auto* free_bombs=reinterpret_cast<uint8_t*>(native_entity_address(0x96C00A));
    auto* free_resprays=reinterpret_cast<uint8_t*>(native_entity_address(0x96C009));
    auto* no_resprays=reinterpret_cast<uint8_t*>(native_entity_address(0x96C008));
    *no_resprays=1;*free_bombs=0xA5;
    for(int32_t enabled:{0,1,-200,INT32_MIN,INT32_MAX}) {
        const auto free=sa_execute_garage_command(garage_call(GarageCommand::free_resprays,{enabled}));
        check(free.executed && free.native_entered && *free_resprays==uint8_t(enabled!=0) && *no_resprays==1 && *free_bombs==0xA5,
            "Real Pay and Spray charging policy preserves global disable, bombshop policy and all boolean values");
    }
    auto* free_profile=reinterpret_cast<uint8_t*>(native_entity_address(0x481811));const auto free_byte=*free_profile;
    *free_profile^=1;*free_resprays=0;
    check(!sa_execute_garage_command(garage_call(GarageCommand::free_resprays,{1})).executed && !*free_resprays,
        "Foreign respray handler profile refuses policy write before entry");*free_profile=free_byte;
    for(int32_t enabled:{0,1,-200}){const auto free=sa_execute_garage_command(garage_call(GarageCommand::free_bombs,{enabled}));
        check(free.executed && free.native_entered && *free_bombs==uint8_t(enabled!=0),"Source boolean changes actual native bombshop free-bomb policy");}
    *reinterpret_cast<uint16_t*>(vehicles.data()+0x22)=400;std::array<uint8_t,32> model_info{},collision{};
    *reinterpret_cast<const void**>(model_info.data()+0x14)=collision.data();pointer(0xA9B0C8+400*4,model_info.data());
    auto contains_packet=garage_call(GarageCommand::contains_car,{7},"NATIVE01");
    for(bool expected:{false,true}){garage_inside=expected;const auto containment=sa_execute_garage_command(contains_packet);
        check(containment.executed && containment.native_entered && containment.condition==expected && garage_seen==uintptr_t(garage_data) && garage_entity_seen==uintptr_t(vehicles.data()) && garage_radius==0.f,"Containment calls the actual registered garage and vehicle, with source radius zero; native geometry boundary mocked");}
    const auto before_geometry=garage_checks;
    for(int32_t invalid:{-1,0x80,0x107,0x301}){contains_packet.inputs[0]=invalid;check(!sa_execute_garage_command(contains_packet).executed && garage_checks==before_geometry,"Missing/recycled/out-of-bounds vehicle never enters garage geometry");}contains_packet.inputs[0]=7;
    *reinterpret_cast<const void**>(model_info.data()+0x14)=nullptr;
    check(!sa_execute_garage_command(contains_packet).executed && garage_checks==before_geometry,"Absent collision refused before native geometry");*reinterpret_cast<const void**>(model_info.data()+0x14)=collision.data();
    *reinterpret_cast<float*>(garage_data)=std::numeric_limits<float>::quiet_NaN();check(!sa_execute_garage_command(contains_packet).executed,"Non-finite garage geometry refused");*reinterpret_cast<float*>(garage_data)=0.f;
    garage_fault=true;const auto geometry_failure=sa_execute_garage_command(contains_packet);
    check(!geometry_failure.executed && geometry_failure.native_entered,"Fault within native geometry reports entered failure rather than fabricating containment");garage_fault=false;
    auto* bomb_flags=vehicles.data()+0x4A8;auto* bomb_owner=reinterpret_cast<uintptr_t*>(vehicles.data()+0x4CC);
    auto* fuse=reinterpret_cast<uint16_t*>(vehicles.data()+0x4DE);*fuse=321;*reinterpret_cast<int32_t*>(vehicles.data()+0x590)=0;
    for(int32_t type=0;type<=5;++type) {
        *bomb_flags=0xF8;const auto armed=sa_execute_garage_command(garage_call(GarageCommand::arm_car_bomb,{7,type}));
        if(!armed.executed || !armed.native_entered || *bomb_flags!=uint8_t(0xF8|type) || *bomb_owner!=uintptr_t(ped) || *fuse!=321)
            std::cerr<<"Bomb diagnostic: type="<<type<<" executed="<<armed.executed<<" entered="<<armed.native_entered<<" flags="<<int(*bomb_flags)<<" owner="<<*bomb_owner<<" player="<<uintptr_t(ped)<<" fuse="<<*fuse<<" diagnostic="<<armed.diagnostic<<'\n';
        check(armed.executed && armed.native_entered && *bomb_flags==uint8_t(0xF8|type) && *bomb_owner==uintptr_t(ped) && *fuse==321,"Bomb type/real player owner assigned while lights, winch and fuse remain unchanged");
        check(sa_execute_garage_command(garage_call(GarageCommand::car_bomb_type,{7,type})).condition,"Exact source bomb type reads native three bits");
        check(!sa_execute_garage_command(garage_call(GarageCommand::car_bomb_type,{7,(type+1)%6})).condition,"A different installed bomb type is false");
    }
    for(int32_t kind:{0,1,2,9,10}){*reinterpret_cast<int32_t*>(vehicles.data()+0x590)=kind;
        check(sa_execute_garage_command(garage_call(GarageCommand::arm_car_bomb,{7,2})).executed,"Native automobile/bike family supports original car bombs");}
    *reinterpret_cast<int32_t*>(vehicles.data()+0x590)=5;const auto prior_flags=*bomb_flags;
    check(!sa_execute_garage_command(garage_call(GarageCommand::arm_car_bomb,{7,1})).executed && *bomb_flags==prior_flags,"Boat cannot be interpreted as original bomb-capable automobile");*reinterpret_cast<int32_t*>(vehicles.data()+0x590)=0;
    const auto disarmed=sa_execute_garage_command(garage_call(GarageCommand::disarm_car_bomb,{7}));
    check(disarmed.executed && disarmed.native_entered && *bomb_flags==0xF8 && *bomb_owner==0 && *fuse==321,"Original disarm clears bomb and installer, without changing an existing fuse");
    *bomb_owner=uintptr_t(ped);const auto already_empty=sa_execute_garage_command(garage_call(GarageCommand::disarm_car_bomb,{7}));
    check(already_empty.executed && !already_empty.native_entered && *bomb_owner==uintptr_t(ped),"Original already-empty bomb leaves installer unchanged");
    const auto player_export=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB7CD98));pointer(0xB7CD98,nullptr);
    check(!sa_execute_garage_command(garage_call(GarageCommand::arm_car_bomb,{7,3})).executed && *bomb_flags==0xF8,"Unavailable live player cannot become a stale bomb installer");pointer(0xB7CD98,reinterpret_cast<const void*>(player_export));
    auto mixed=garage_call(GarageCommand::free_bombs,{1});mixed.source_environment=EnvironmentCommand::read_clock;
    check(!sa_execute_garage_command(mixed).executed && !sa_execute_environment_command(mixed).executed,"Mixed source service modes cannot bypass preflight");
    for(auto address:{uintptr_t(0x447C90),uintptr_t(0x447E10),uintptr_t(0x448BE0),uintptr_t(0x44B49D),uintptr_t(0x6D24F0),uintptr_t(0x6D2593)}) {
        auto* code=reinterpret_cast<uint8_t*>(native_entity_address(address));const auto original=*code;*code^=0x80;
        const auto call=address==0x44B49D?garage_call(GarageCommand::free_bombs,{1}):address>=0x6D24F0?garage_call(GarageCommand::arm_car_bomb,{7,1}):contains_packet;
        check(!sa_execute_garage_command(call).executed,"Altered retail service profile rejected before native access");*code=original;
    }
    DWORD garage_previous=0,garage_ignored=0;check(VirtualProtect(reinterpret_cast<void*>(native_entity_address(0x96C000)),4096,PAGE_READONLY,&garage_previous)!=0,"Owned garage state page becomes read-only");
    const auto respray_fault=sa_execute_garage_command(garage_call(GarageCommand::respray_happened,{},"NATIVE01"));
    check(!respray_fault.executed && respray_fault.native_entered,"Destructive respray write fault is observable and cannot be replayed");
    const auto charging_fault=sa_execute_garage_command(garage_call(GarageCommand::free_resprays,{0}));
    check(!charging_fault.executed && charging_fault.native_entered,"Respray policy write fault retains entered failure receipt");
    check(VirtualProtect(reinterpret_cast<void*>(native_entity_address(0x96C000)),4096,garage_previous,&garage_ignored)!=0,"Owned garage state protection restored");
    auto traffic_call=[](TrafficCommand action,std::vector<int32_t> inputs){NativeCall c;c.source_traffic=action;c.inputs=std::move(inputs);return c;};
    for(auto action:{TrafficCommand::emergency_vehicles,TrafficCommand::cars_around_camera}) {
        const auto address=native_entity_address(action==TrafficCommand::emergency_vehicles?0x8A5B28:0x9690C1);
        auto* flag=reinterpret_cast<uint8_t*>(address);flag[-1]=0xAB;flag[1]=0xCD;
        for(int32_t enabled:{0,1,-123}) {
            const auto changed=sa_execute_traffic_command(traffic_call(action,{enabled}));
            check(changed.executed && changed.native_entered && *flag==uint8_t(enabled!=0) && flag[-1]==0xAB && flag[1]==0xCD,"Native traffic policy is one boolean byte, preserving adjacent counters/flags");
        }
    }
    auto* pads=reinterpret_cast<uint8_t*>(native_entity_address(0xB73458));std::memset(pads,0xAD,0x268);
    for(int32_t pad_index:{0,1})for(int32_t delay:{0,9}) {
        const auto before=std::vector<uint8_t>(pads,pads+0x268);
        const auto delayed=sa_execute_traffic_command(traffic_call(TrafficCommand::drunk_input_delay,{pad_index,delay}));
        auto expected=before;std::memcpy(expected.data()+pad_index*0x134+0x74,&delay,4);
        check(delayed.executed && delayed.native_entered && std::memcmp(pads,expected.data(),expected.size())==0,"Only the selected native pad's four-byte drunk delay changes; steering samples and other pad preserved");
    }
    for(auto values:{std::vector<int32_t>{-1,0},std::vector<int32_t>{2,0},std::vector<int32_t>{0,-1},std::vector<int32_t>{0,10}}) {
        const auto invalid=sa_execute_traffic_command(traffic_call(TrafficCommand::drunk_input_delay,values));
        check(!invalid.executed && !invalid.native_entered,"Pad/buffer boundaries checked in production backend too");
    }
    for(int32_t kind:{0,1,2}) {
        *reinterpret_cast<int32_t*>(vehicles.data()+0x590)=*reinterpret_cast<int32_t*>(vehicles.data()+0x594)=kind;
        for(int32_t enabled:{0,1,-1}) {
            vehicles[0x868]=0xD5;const auto changed=sa_execute_traffic_command(traffic_call(TrafficCommand::tank_detonates_cars,{7,enabled}));
            check(changed.executed && changed.native_entered && vehicles[0x868]==uint8_t(0xD5|(enabled?0x20:0)),"Tank collision policy preserves taxi/damage/watertight flags on automobile-derived classes");
        }
        for(uint8_t state=0;state<=4;++state) {
            vehicles[0x5AA]=state;vehicles[0x5A9]=0xA7;vehicles[0x5AB]=0xB9;*reinterpret_cast<float*>(vehicles.data()+0x5E4)=17;
            const auto popped=sa_execute_traffic_command(traffic_call(TrafficCommand::pop_boot_physics,{7}));
            check(popped.executed && popped.native_entered && vehicles[0x5AA]==((state==0 || state==2)?1:state) &&
                *reinterpret_cast<float*>(vehicles.data()+0x5E4)==-2 && vehicles[0x5A9]==0xA7 && vehicles[0x5AB]==0xB9,"Original boot opens intact/damaged closed states, preserves open/missing states and adjacent doors, always sets angular velocity");
        }
    }
    vehicles[0x5AA]=5;const auto unknown_boot=sa_execute_traffic_command(traffic_call(TrafficCommand::pop_boot_physics,{7}));
    check(!unknown_boot.executed && !unknown_boot.native_entered,"Unknown door state refuses before writes");vehicles[0x5AA]=0;
    for(int32_t kind:{3,5,9}) {
        *reinterpret_cast<int32_t*>(vehicles.data()+0x590)=*reinterpret_cast<int32_t*>(vehicles.data()+0x594)=kind;
        const auto before=vehicles;
        for(auto action:{TrafficCommand::pop_boot_physics,TrafficCommand::tank_detonates_cars}) {
            const auto refused=sa_execute_traffic_command(traffic_call(action,action==TrafficCommand::pop_boot_physics?std::vector<int32_t>{7}:std::vector<int32_t>{7,1}));
            check(!refused.executed && !refused.native_entered && vehicles==before,"Other vehicle classes cannot access automobile boot/tank storage");
        }
    }
    *reinterpret_cast<int32_t*>(vehicles.data()+0x590)=0;*reinterpret_cast<int32_t*>(vehicles.data()+0x594)=2;
    check(!sa_execute_traffic_command(traffic_call(TrafficCommand::tank_detonates_cars,{7,1})).executed,"Inconsistent automobile subclass rejects");*reinterpret_cast<int32_t*>(vehicles.data()+0x594)=0;
    auto* cleanup=reinterpret_cast<uint8_t*>(native_entity_address(0xA90850));std::memset(cleanup,0,0x25C);
    auto cleanup_entry=[&](size_t slot,uint8_t type,int32_t handle){cleanup[slot*8]=type;std::memcpy(cleanup+slot*8+4,&handle,4);};
    cleanup_entry(0,1,7);cleanup_entry(1,2,7);cleanup_entry(2,1,0x20c);cleanup_entry(74,1,7);cleanup[0x258]=4;
    const auto released=sa_execute_traffic_command(traffic_call(TrafficCommand::release_vehicle_cleanup,{7}));
    check(released.executed && released.native_entered && cleanup_seen==7 && cleanup[0x258]==2 && cleanup[0]==0 && cleanup[74*8]==0 &&
        cleanup[8]==2 && cleanup[16]==1,"Vehicle cleanup uses full generational handle, removes all matching vehicle records and preserves same-handle ped/other vehicle; native cleanup boundary mocked");
    const auto empty_release=sa_execute_traffic_command(traffic_call(TrafficCommand::release_vehicle_cleanup,{7}));
    check(empty_release.executed && cleanup[0x258]==2,"Valid vehicle absent from cleanup is an actual successful no-change operation, as original");
    const auto before_release=cleanup_calls;
    for(int32_t handle:{-1,8,0x18A,0x20B,0x307}) {
        const auto bad=sa_execute_traffic_command(traffic_call(TrafficCommand::release_vehicle_cleanup,{handle}));
        check(!bad.executed && !bad.native_entered && cleanup_calls==before_release,"Cleanup cannot send recycled/free/out-of-range handle to the unchecked native pool");
    }
    cleanup[0x258]=76;check(!sa_execute_traffic_command(traffic_call(TrafficCommand::release_vehicle_cleanup,{7})).executed,"Cleanup count bounded to 75 records");
    cleanup[0x258]=1;check(!sa_execute_traffic_command(traffic_call(TrafficCommand::release_vehicle_cleanup,{7})).executed,"Inconsistent cleanup occupancy refuses before destructive recount");cleanup[0x258]=2;
    cleanup_entry(0,1,7);cleanup[0x258]=3;cleanup_fault=true;
    const auto interrupted=sa_execute_traffic_command(traffic_call(TrafficCommand::release_vehicle_cleanup,{7}));
    check(!interrupted.executed && interrupted.native_entered && cleanup[0x258]==2,"Cleanup fault after entry retains receipt; cannot be repeated as success");cleanup_fault=false;
    auto mixed_traffic=traffic_call(TrafficCommand::drunk_input_delay,{0,1});mixed_traffic.source_garage=GarageCommand::free_bombs;
    check(!sa_execute_traffic_command(mixed_traffic).executed && !sa_execute_garage_command(mixed_traffic).executed,"Traffic/garage mixed modes cannot bypass either backend");
    for(auto address:{uintptr_t(0x421339),uintptr_t(0x421346),uintptr_t(0x53FB70),uintptr_t(0x53F910),uintptr_t(0x4654B0),uintptr_t(0x156E810),uintptr_t(0x6AF145),uintptr_t(0x401860),uintptr_t(0x6C21C0),uintptr_t(0x6A4510)}) {
        auto* code=reinterpret_cast<uint8_t*>(native_entity_address(address));const auto saved=*code;*code^=0x80;
        const auto action=address==0x421339 || address==0x421346?TrafficCommand::emergency_vehicles:
            address==0x53FB70 || address==0x53F910?TrafficCommand::drunk_input_delay:
            address==0x4654B0 || address==0x156E810?TrafficCommand::release_vehicle_cleanup:
            address==0x6AF145?TrafficCommand::tank_detonates_cars:TrafficCommand::pop_boot_physics;
        const auto inputs=action==TrafficCommand::drunk_input_delay?std::vector<int32_t>{0,1}:
            action==TrafficCommand::tank_detonates_cars?std::vector<int32_t>{7,1}:std::vector<int32_t>{7};
        const auto bad=sa_execute_traffic_command(traffic_call(action,inputs));
        check(!bad.executed && !bad.native_entered,"Altered traffic/door/cleanup/steering profile refuses before entry");*code=saved;
    }
    DWORD traffic_previous=0,traffic_ignored=0;check(VirtualProtect(reinterpret_cast<void*>(native_entity_address(0xB73000)),4096,PAGE_READONLY,&traffic_previous)!=0,"Protect only owned native-pad data page");
    const auto delay_fault=sa_execute_traffic_command(traffic_call(TrafficCommand::drunk_input_delay,{1,3}));
    check(!delay_fault.executed && delay_fault.native_entered,"Drunk-delay write fault returns entered receipt");
    check(VirtualProtect(reinterpret_cast<void*>(native_entity_address(0xB73000)),4096,traffic_previous,&traffic_ignored)!=0,"Restore owned pad page protection");
    auto predicate_operation=[&](EntityCommand action,int32_t value){auto c=operation(action,{value});c.predicate=true;return sa_execute_entity_command(c);};
    auto* weapon_mode=reinterpret_cast<uint16_t*>(native_entity_address(0xB6F028)+0x830);
    for(unsigned mode=0;mode<=0xFFFF;++mode) {
        *weapon_mode=uint16_t(mode);const auto before=*weapon_mode;const auto result=predicate_operation(EntityCommand::first_person_weapon_camera,-999);
        const bool expected=mode==7 || mode==34 || mode==8 || mode==45 || mode==46;
        check(result.executed && !result.native_entered && result.condition==expected && *weapon_mode==before,"First-person query uses exactly the five source modes, excluding SA HS/attached modes and all unrelated modes; argument unused as in source");
    }
    for(unsigned bits=0;bits<256;++bits) {
        ped[0x472]=uint8_t(bits);const auto result=predicate_operation(EntityCommand::char_drowning,27);
        check(result.executed && !result.native_entered && result.condition==bool(bits&8) && ped[0x472]==bits,"Drowning query reads only its real packed flag and preserves every neighboring flag");
    }
    for(int32_t handle:{-1,26,0x11B,0x7FFFFF1B}) {
        const auto result=predicate_operation(EntityCommand::char_drowning,handle);
        check(result.executed && !result.native_entered && !result.condition,"Original absent/stale/out-of-range drowning predicate is false without unchecked pool lookup");
    }
    ped_flag|=0x80;check(!predicate_operation(EntityCommand::char_drowning,27).condition,"Released pedestrian cannot retain a drowning state");ped_flag=27;
    ped[0x36]=2;check(!predicate_operation(EntityCommand::char_drowning,27).executed,"Wrong live entity kind is corruption, not a fabricated false predicate");ped[0x36]=3;
    const auto ped_count=ped_pool.count;ped_pool.count=0x800000;check(!predicate_operation(EntityCommand::char_drowning,27).executed,"Drowning scan bounds corrupted pool size");ped_pool.count=ped_count;
    auto* focus=reinterpret_cast<uint8_t*>(native_entity_address(0xB7CD74));*focus=0;
    for(uint8_t slot=0;slot<13;++slot)for(int32_t amount:{0,-1,1,2147483647}) {
        ped[0x718]=slot;std::memset(ped+0x5A0,0xA5,13*0x1C);std::array<uint8_t,13*0x1C> before{};std::memcpy(before.data(),ped+0x5A0,before.size());
        const auto result=sa_execute_entity_command(operation(EntityCommand::player_clip_ammo,{amount}));
        check(result.executed && result.native_entered==bool(amount),"Original zero clip is preserved; all other 32-bit values write only selected weapon");
        for(size_t b=0;b<before.size();++b) {
            const bool changed=amount && b>=slot*0x1C+8 && b<slot*0x1C+12;
            check(ped[0x5A0+b]==(changed?uint8_t(uint32_t(amount)>>((b-slot*0x1C-8)*8)):before[b]),"Ammo clip preserves total ammo, weapon identity/state, FX pointer and other slots");
        }
    }
    for(uint8_t slot:{uint8_t(13),uint8_t(128),uint8_t(255)}) {
        ped[0x718]=slot;auto bad=sa_execute_entity_command(operation(EntityCommand::player_clip_ammo,{5}));check(!bad.executed && !bad.native_entered,"Invalid selected slot refuses before signed native indexing");
    }
    ped[0x718]=2;*focus=2;check(!sa_execute_entity_command(operation(EntityCommand::player_clip_ammo,{5})).executed,"Invalid focused player refuses");
    *focus=1;pointer(0xB7CD98,nullptr);pointer(0xB7CD98+0x190,ped);
    check(sa_execute_entity_command(operation(EntityCommand::player_clip_ammo,{123})).executed && *reinterpret_cast<uint32_t*>(ped+0x5A8+2*0x1C)==123,"Current clip follows actual PlayerInFocus, not a hardcoded player zero");
    *focus=0;check(!sa_execute_entity_command(operation(EntityCommand::player_clip_ammo,{5})).executed,"Missing focused player is not silently replaced with another player");pointer(0xB7CD98,ped);pointer(0xB7CD98+0x190,nullptr);
    ped_flag|=0x80;check(!sa_execute_entity_command(operation(EntityCommand::player_clip_ammo,{5})).executed,"Released focused player refuses clip write");ped_flag=27;
    DWORD ammo_previous=0,ammo_ignored=0;check(VirtualProtect(ped,4096,PAGE_READONLY,&ammo_previous)!=0,"Protect owned weapon data");
    auto ammo_fault=sa_execute_entity_command(operation(EntityCommand::player_clip_ammo,{5}));check(!ammo_fault.executed && ammo_fault.native_entered,"Clip write fault reports entered receipt");
    check(VirtualProtect(ped,4096,ammo_previous,&ammo_ignored)!=0,"Restore owned weapon data");
    *reinterpret_cast<int32_t*>(vehicles.data()+0x590)=0;*reinterpret_cast<int32_t*>(vehicles.data()+0x594)=0;*reinterpret_cast<uintptr_t*>(vehicles.data()+0x68C)=0;
    for(uint8_t state=0;state<=4;++state) {
        vehicles[0x5AA]=state;const auto before=vehicles;const auto calls_before=boot_visual_calls;
        const auto result=sa_execute_entity_command(operation(EntityCommand::remove_car_boot,{7}));
        check(result.executed && result.native_entered && vehicles[0x5AA]==4 && boot_visual_calls==calls_before+1 && boot_visual_car==uintptr_t(vehicles.data()),"Boot removal sets missing state even with no boot frame, then calls guarded visual removal boundary");
        for(size_t b=0;b<vehicles.size();++b)if(b!=0x5AA)check(vehicles[b]==before[b],"Boot removal preserves all unrelated vehicle bytes before native visual callback");
    }
    vehicles[0x5AA]=5;const auto calls_before=boot_visual_calls;check(!sa_execute_entity_command(operation(EntityCommand::remove_car_boot,{7})).executed && boot_visual_calls==calls_before,"Invalid damage state refuses before native visual entry");vehicles[0x5AA]=0;
    for(int32_t handle:{-1,8,0x18A,0x307})check(!sa_execute_entity_command(operation(EntityCommand::remove_car_boot,{handle})).executed,"Boot removal refuses invalid/free/stale vehicle handle");
    for(int32_t kind:{3,5,9}) {*reinterpret_cast<int32_t*>(vehicles.data()+0x590)=kind;check(!sa_execute_entity_command(operation(EntityCommand::remove_car_boot,{7})).executed,"Boot removal refuses other vehicle class layouts");}*reinterpret_cast<int32_t*>(vehicles.data()+0x590)=0;
    *reinterpret_cast<uintptr_t*>(vehicles.data()+0x68C)=uintptr_t(ped);*reinterpret_cast<uint16_t*>(vehicles.data()+0x22)=445;
    auto* model=reinterpret_cast<uint8_t*>(native_entity_address(0xD01000));*reinterpret_cast<uintptr_t*>(model+0x5C)=uintptr_t(model+0x100);pointer(0xA9B0C8+445*4,model);
    *reinterpret_cast<uintptr_t*>(vehicles.data()+0x384)=0;check(!sa_execute_entity_command(operation(EntityCommand::remove_car_boot,{7})).executed,"Existing boot frame requires initialized handling before visual removal");
    *reinterpret_cast<uintptr_t*>(vehicles.data()+0x384)=uintptr_t(model+0x200);
    check(sa_execute_entity_command(operation(EntityCommand::remove_car_boot,{7})).executed,"Initialized frame/model/handling passes native visual entry preflight; callback mocked");
    *reinterpret_cast<uintptr_t*>(model+0x5C)=0;check(!sa_execute_entity_command(operation(EntityCommand::remove_car_boot,{7})).executed,"Missing vehicle model component structure refuses before damage write");*reinterpret_cast<uintptr_t*>(model+0x5C)=uintptr_t(model+0x100);
    boot_visual_fault=true;vehicles[0x5AA]=0;const auto boot_fault=sa_execute_entity_command(operation(EntityCommand::remove_car_boot,{7}));
    check(!boot_fault.executed && boot_fault.native_entered && vehicles[0x5AA]==4,"Visual fault after damage write reports partial entry for repair, never false success or replay");boot_visual_fault=false;
    for(const auto pair:{std::pair<uintptr_t,EntityCommand>{0x50BFF0,EntityCommand::first_person_weapon_camera},{0x5E873A,EntityCommand::char_drowning},
        {0x56E210,EntityCommand::player_clip_ammo},{0x43CE90,EntityCommand::player_clip_ammo},{0x6B1600,EntityCommand::remove_car_boot},
        {0x6B17E5,EntityCommand::remove_car_boot},{0x6C2709,EntityCommand::remove_car_boot},{0x6C272C,EntityCommand::remove_car_boot}}) {
        auto* code=reinterpret_cast<uint8_t*>(native_entity_address(pair.first));const auto saved=*code;*code^=0x80;
        auto c=operation(pair.second,{pair.second==EntityCommand::char_drowning?27:7});c.predicate=pair.second==EntityCommand::char_drowning || pair.second==EntityCommand::first_person_weapon_camera;
        const auto bad=sa_execute_entity_command(c);check(!bad.executed && !bad.native_entered,"Altered new camera/drowning/clip/boot profile refuses before entry");*code=saved;
    }
    auto* photograph=reinterpret_cast<uint8_t*>(native_entity_address(0xC8A7C0));
    auto* credits=reinterpret_cast<uint8_t*>(native_entity_address(0xC6E97C));
    auto* pad_zero=reinterpret_cast<uint8_t*>(native_entity_address(0xB73458));
    auto photo_query=operation(EntityCommand::photograph_taken,{});photo_query.predicate=true;
    auto credits_query=operation(EntityCommand::credits_finished,{});credits_query.predicate=true;
    auto camera_enable=operation(EntityCommand::enable_control_camera,{});
    for(unsigned bits=0;bits<256;++bits) {
        photograph[-1]=0xAE;photograph[0]=uint8_t(bits);photograph[1]=0x5D;photograph[2]=0xBE;
        const auto photo_result=sa_execute_entity_command(photo_query);
        check(photo_result.executed && photo_result.native_entered && photo_result.condition==bool(bits) && !photograph[0] &&
            photograph[-1]==0xAE && photograph[1]==0x5D && photograph[2]==0xBE,"Photo query consumes actual latch once and preserves native TakePhoto pipeline and neighboring storage");
        const auto consumed=sa_execute_entity_command(photo_query);
        check(consumed.executed && consumed.native_entered && !consumed.condition,"Next photo query observes consumed latch instead of a fabricated repeat");
        credits[-1]=0xEB;credits[0]=uint8_t(bits);credits[1]=0xEF;
        const auto finished=sa_execute_entity_command(credits_query);
        check(finished.executed && !finished.native_entered && finished.condition==!bits && credits[-1]==0xEB && credits[0]==bits && credits[1]==0xEF,"Credits query reads real running flag without modifying completion or neighboring storage");
    }
    std::memset(pad_zero,0xAD,2*0x134);
    for(unsigned bits=0;bits<65536;++bits) {
        *reinterpret_cast<uint16_t*>(pad_zero+0x10E)=uint16_t(bits);
        const auto enabled=sa_execute_entity_command(camera_enable);
        check(enabled.executed && enabled.native_entered && *reinterpret_cast<uint16_t*>(pad_zero+0x10E)==uint16_t(bits&~1u),"Camera enable clears only original camera ownership bit; all other control flags survive");
        for(size_t b=0;b<2*0x134;++b)if(b!=0x10E && b!=0x10F)check(pad_zero[b]==0xAD,"Camera enable preserves all input/steering/drunk fields and entire pad one");
    }
    for(auto call:{photo_query,credits_query,camera_enable}) {
        call.inputs={0};const auto malformed=sa_execute_entity_command(call);check(!malformed.executed && !malformed.native_entered,"Zero-operand state commands reject unexpected operands");
        call.inputs.clear();call.outputs=1;check(!sa_execute_entity_command(call).executed,"State commands cannot accept an output variable");
        call.outputs=0;call.predicate=!call.predicate;check(!sa_execute_entity_command(call).executed,"State commands enforce actual predicate contract");
    }
    for(const auto pair:{std::pair<uintptr_t,EntityCommand>{0x73C252,EntityCommand::photograph_taken},{0x73A31B,EntityCommand::photograph_taken},
                       {0x489410,EntityCommand::credits_finished},{0x53FD30,EntityCommand::enable_control_camera},{0x53FB70,EntityCommand::enable_control_camera}}) {
        auto* code=reinterpret_cast<uint8_t*>(native_entity_address(pair.first));const auto saved=*code;*code^=0x80;
        auto call=operation(pair.second,{});call.predicate=pair.second!=EntityCommand::enable_control_camera;
        const auto refused=sa_execute_entity_command(call);check(!refused.executed && !refused.native_entered,"Altered photo/credits/pad profile refuses before reading/writing engine state");*code=saved;
    }
    for(auto call:{photo_query,camera_enable}) {
        const auto data=call.source_entity_command==EntityCommand::photograph_taken?uintptr_t(photograph):uintptr_t(pad_zero);
        void* page=reinterpret_cast<void*>(data&~uintptr_t(4095));DWORD previous=0,ignored=0;
        photograph[0]=1;check(VirtualProtect(page,4096,PAGE_READONLY,&previous)!=0,"Protect owned latch/control page");
        const auto fault=sa_execute_entity_command(call);
        check(!fault.executed && fault.native_entered,"Latch/control write fault reports entered receipt, never success or replay");
        check(VirtualProtect(page,4096,previous,&ignored)!=0,"Restore owned latch/control page");
    }
    // Different native order proves source numbers never become native IDs.
    auto* groups=reinterpret_cast<uint8_t*>(native_entity_address(0x8AA5A8));
    auto* group_count=reinterpret_cast<int32_t*>(native_entity_address(0xB4EA28));
    profile(pe,0x8AA5A8,146*0x30);
    check(!std::strcmp(reinterpret_cast<char*>(groups+54*0x30),"player"),"Native built-in player group has a different original ID");
    for(size_t n=118*0x30;n<145*0x30;++n)
        check(groups[n]==0,"Retail walk-cycle slots are separate from built-in definitions");
    for(uint32_t n=0;n<4;++n) {
        uint32_t descriptor=0;std::memcpy(&descriptor,groups+145*0x30+n*4,4);
        check(descriptor==n,"Adjacent animation descriptors bound the fixed definition storage");
    }
    std::memset(groups,0,145*0x30);*group_count=19;ped_flag=27;ped[0x36]=3;
    std::strcpy(reinterpret_cast<char*>(groups),"unrelated");
    const std::vector<int32_t> source_styles={53,52,51,50,49,48,47,46,45,44,43,42,41,40,39,38,31,0};
    for(size_t index=0;index<source_styles.size();++index)
        std::strcpy(reinterpret_cast<char*>(groups+(index+1)*0x30),original_locomotion_group(source_styles[index]));
    for(size_t index=0;index<source_styles.size();++index) {
        std::array<uint8_t,0x7C4> before{};std::memcpy(before.data(),ped,before.size());
        const auto changed=sa_execute_entity_command(operation(EntityCommand::char_locomotion_group,{27,source_styles[index]}));
        check(changed.executed && changed.native_entered && *reinterpret_cast<int32_t*>(ped+0x4D4)==int32_t(index+1),"Original style resolves by registered name, including nine-character oldfatman and reordered groups");
        for(size_t n=0;n<before.size();++n)if(n<0x4D4 || n>=0x4D8)
            check(ped[n]==before[n],"Style assignment preserves rig, culling, tasks, flags, player data and neighboring fields");
    }
    auto style=operation(EntityCommand::char_locomotion_group,{27,43});
    auto reject_style=[&](const NativeCall& call,const char* reason){
        std::array<uint8_t,0x7C4> before{};std::memcpy(before.data(),ped,before.size());
        const auto rejected=sa_execute_entity_command(call);
        check(!rejected.executed && !rejected.native_entered && !std::memcmp(ped,before.data(),before.size()),reason);
    };
    for(int32_t group:{INT32_MIN,-1,1,30,32,37,54,INT32_MAX})
        reject_style(operation(EntityCommand::char_locomotion_group,{27,group}),"Weapon/gesture/cutscene or invalid groups never substitute a walk cycle");
    for(int32_t handle:{-1,28,0x11B,0x7FFFFF1B})
        reject_style(operation(EntityCommand::char_locomotion_group,{handle,43}),"Style setter validates actual pedestrian pool generation and bounds");
    for(int32_t count:{-1,0,146,INT32_MAX}){*group_count=count;reject_style(style,"Invalid/expanded definition storage refuses before indexing");}
    *group_count=19;const auto fat_name=groups+11*0x30;
    fat_name[0]='X';reject_style(style,"Absent style refuses instead of writing native count as an invalid group");fat_name[0]='o';
    std::memset(groups+0x30,'X',16);reject_style(style,"Unterminated registered name refuses boundedly");
    std::memset(groups+0x30,0,16);std::strcpy(reinterpret_cast<char*>(groups+0x30),"skate");
    for(uintptr_t address:{uintptr_t(0x4D3A20),uintptr_t(0x47EF71),uintptr_t(0x5E4ACA),uintptr_t(0x54FF90)}) {
        auto* code=reinterpret_cast<uint8_t*>(native_entity_address(address));const auto saved=*code;*code^=0x80;
        reject_style(style,"Changed pool/name/field profile cannot grant style storage access");*code=saved;
    }
    auto malformed_style=style;malformed_style.inputs.pop_back();reject_style(malformed_style,"Truncated native style packet refuses");
    malformed_style=style;malformed_style.predicate=true;reject_style(malformed_style,"Style setter is never a fabricated predicate");
    DWORD old_protection=0,ignored_protection=0;
    check(VirtualProtect(ped,4096,PAGE_READONLY,&old_protection)!=0,"Protect owned pedestrian style field");
    const auto style_fault=sa_execute_entity_command(style);
    check(!style_fault.executed && style_fault.native_entered,"Style write fault after entry requires repair and cannot replay");
    check(VirtualProtect(ped,4096,old_protection,&ignored_protection)!=0,"Restore owned pedestrian page");
    pointer(0xB7CD98+0x190,ped);
    for(int32_t selector:{0,1})for(uint32_t value:{0u,16777217u,0x80000000u,0xFFFFFFFFu}) {
        auto* havoc=reinterpret_cast<uint32_t*>(player+selector*0x190+0x140);*havoc=value;
        std::array<uint8_t,0x320> before{};std::memcpy(before.data(),player,before.size());
        const auto observed=sa_execute_entity_command(operation(EntityCommand::get_player_havoc,{selector},1));
        uint32_t bits=0;std::memcpy(&bits,observed.values.data(),4);
        check(observed.executed && !observed.native_entered && bits==value && !std::memcmp(player,before.data(),before.size()),"Havoc query returns actual unsigned counter bits without float rounding, mutation or invented result");
        const auto cleared=sa_execute_entity_command(operation(EntityCommand::reset_player_havoc,{selector}));
        check(cleared.executed && cleared.native_entered && *havoc==0,"Havoc reset clears only the selected real player counter");
        for(size_t n=0;n<before.size();++n)if(n<size_t(selector)*0x190+0x140 || n>=size_t(selector)*0x190+0x144)
            check(player[n]==before[n],"Havoc reset preserves both players, money, wanted state, media/chase value, controls and neighbors");
    }
    auto reject_havoc=[&](const NativeCall& call,const char* reason){
        std::array<uint8_t,0x320> before{};std::memcpy(before.data(),player,before.size());
        const auto rejected=sa_execute_entity_command(call);
        check(!rejected.executed && !rejected.native_entered && !std::memcmp(player,before.data(),before.size()),reason);
    };
    for(int32_t selector:{INT32_MIN,-1,2,INT32_MAX})for(auto action:{EntityCommand::reset_player_havoc,EntityCommand::get_player_havoc})
        reject_havoc(operation(action,{selector},action==EntityCommand::get_player_havoc?1:0),"Invalid havoc player selector refuses before storage access");
    for(uintptr_t address:{uintptr_t(0x56E210),uintptr_t(0x56F46C),uintptr_t(0x54FF90)}) {
        auto* code=reinterpret_cast<uint8_t*>(native_entity_address(address));const auto saved=*code;*code^=0x80;
        reject_havoc(operation(EntityCommand::reset_player_havoc,{0}),"Changed player/storage/pool profile refuses havoc writes");*code=saved;
    }
    ped_flag|=0x80;reject_havoc(operation(EntityCommand::get_player_havoc,{1},1),"Stale player pointer cannot return a fabricated zero counter");ped_flag=27;
    pointer(0xB7CD98+0x190,nullptr);reject_havoc(operation(EntityCommand::reset_player_havoc,{1}),"Missing player is never replaced with the focused player");
    auto malformed_havoc=operation(EntityCommand::get_player_havoc,{0},0);reject_havoc(malformed_havoc,"Havoc output arity validates before access");
    malformed_havoc=operation(EntityCommand::reset_player_havoc,{0,1});reject_havoc(malformed_havoc,"Havoc input arity validates before access");
    void* havoc_page=reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(player)&~uintptr_t(4095));DWORD havoc_protection=0;
    check(VirtualProtect(havoc_page,4096,PAGE_READONLY,&havoc_protection)!=0,"Protect owned havoc storage page");
    const auto havoc_fault=sa_execute_entity_command(operation(EntityCommand::reset_player_havoc,{0}));
    check(!havoc_fault.executed && havoc_fault.native_entered,"Entered havoc write fault cannot grant success or replay");
    check(VirtualProtect(havoc_page,4096,havoc_protection,&ignored_protection)!=0,"Restore owned havoc storage page");
    // Source attachments and vehicle controllers: the compiled backend owns
    // preflight/lifetime checks, with engine boundaries replaced by mocks.
    vehicles.fill(0);objects.fill(0);vehicle_flags={{7,0x8A,12}};object_flags={{19,0x88}};
    vehicles[0x36]=vehicles[2*0xA18+0x36]=2;objects[0x36]=4;
    const auto car=reinterpret_cast<uintptr_t>(vehicles.data()),other=car+2*0xA18,obj=reinterpret_cast<uintptr_t>(objects.data());
    auto attached=operation(EntityCommand::attach_object_car,{19,7,entity_bits(1.25f),entity_bits(-2.5f),entity_bits(3)});
    check(sa_execute_entity_command(attached).executed && attach_calls==1 && entity_references.size()==1,
        "Object attachment enters actual physical service and registers the live parent reference");
    std::memcpy(actual.data(),objects.data()+0x100,12);
    check(actual==std::array<float,3>{{1.25f,-2.5f,3}} && *reinterpret_cast<uintptr_t*>(obj+0xFC)==car &&
        *reinterpret_cast<uintptr_t*>(obj+0x128)==car,"Attachment uses source local offset and collision parent, without invented relative rotation");
    check(sa_execute_entity_command(attached).executed && entity_references.size()==1,"Repeated attachment replaces the same slot without leaking references");
    attached.inputs[1]=0x20C;
    check(sa_execute_entity_command(attached).executed && entity_references.size()==1 && entity_references[0].first==other,
        "Reparenting unregisters old target before writing and registering its replacement");
    const auto before_detach=objects;
    auto detached=operation(EntityCommand::detach_object_car,{19});
    check(sa_execute_entity_command(detached).executed && entity_references.empty(),"Detach releases its own native reference");
    for(size_t n=0;n<objects.size();++n)if((n<0xFC || n>=0x100) && (n<0x128 || n>=0x12C))
        check(objects[n]==before_detach[n],"Original detach preserves velocities, orientation, physics, rig/culling flags and every other field");
    check(!*reinterpret_cast<uintptr_t*>(obj+0xFC) && !*reinterpret_cast<uintptr_t*>(obj+0x128),"Original detach clears only attachment/collision parent");
    check(sa_execute_entity_command(detached).executed && !sa_execute_entity_command(detached).native_entered,
        "Already detached original object returns without velocity changes or forced progress");
    auto absent_detach=operation(EntityCommand::detach_object_car,{-1});
    check(sa_execute_entity_command(absent_detach).executed && !sa_execute_entity_command(absent_detach).native_entered,
        "Retail missing-object detach branch returns safely");
    auto rejects_without_mutation=[&](const NativeCall& call,const char* reason) {
        const auto before_v=vehicles;const auto before_o=objects;
        const auto calls=attach_calls+heli_calls+character_attach_calls;const auto refs=entity_references;
        const auto result=sa_execute_entity_command(call);
        check(!result.executed && !result.native_entered && before_v==vehicles && before_o==objects && calls==attach_calls+heli_calls+character_attach_calls && refs==entity_references,reason);
    };
    attached.inputs[1]=7;
    for(int32_t bad:{-1,8,0x18A,0x20B,0x307,INT32_MAX}) {
        auto invalid=attached;invalid.inputs[1]=bad;rejects_without_mutation(invalid,"Attachment validates parent pool generation before any reference or physics mutation");
        invalid=attached;invalid.inputs[0]=bad;rejects_without_mutation(invalid,"Attachment validates child object generation before native entry");
    }
    for(size_t axis=2;axis<5;++axis) {
        auto invalid=attached;invalid.inputs[axis]=entity_bits(std::numeric_limits<float>::quiet_NaN());
        rejects_without_mutation(invalid,"Attachment rejects non-finite source offset before engine entry");
    }
    *reinterpret_cast<uintptr_t*>(car+0xFC)=obj;
    rejects_without_mutation(attached,"Attachment cycle cannot reach recursive native positioning");*reinterpret_cast<uintptr_t*>(car+0xFC)=0;
    *reinterpret_cast<uintptr_t*>(obj+0xFC)=1;
    rejects_without_mutation(attached,"Unknown old parent is never dereferenced for reference cleanup");
    rejects_without_mutation(detached,"Corrupt detach parent requires repair instead of arbitrary native reference access");*reinterpret_cast<uintptr_t*>(obj+0xFC)=0;
    attach_fault=true;const auto attach_failed=sa_execute_entity_command(attached);attach_fault=false;
    check(!attach_failed.executed && attach_failed.native_entered,"Attachment engine fault after entry cannot report success or permit replay");
    check(sa_execute_entity_command(detached).executed,"Owned test attachment can be explicitly detached after a boundary fault");
    for(uintptr_t address:{uintptr_t(0x54D570),uintptr_t(0x54D585),uintptr_t(0x54D61F),uintptr_t(0x571A00),uintptr_t(0x571B70)}) {
        auto* byte=reinterpret_cast<uint8_t*>(native_entity_address(address));const auto saved=*byte;*byte^=0x80;
        rejects_without_mutation(attached,"Altered attachment/reference profiles refuse before touching objects");*byte=saved;
    }
    auto heli=operation(EntityCommand::heli_goto,{7,entity_bits(10),entity_bits(-20),entity_bits(70),300});
    *reinterpret_cast<int32_t*>(car+0x590)=*reinterpret_cast<int32_t*>(car+0x594)=3;
    *reinterpret_cast<float*>(car+0x9B0)=15.5f;*reinterpret_cast<float*>(car+0x9A8)=90.25f;*reinterpret_cast<float*>(car+0x8A8)=1;
    for(int32_t speed:{INT32_MIN,-1,0,127,128,255,256,300,INT32_MAX}) {
        heli.inputs[4]=speed;const auto result=sa_execute_entity_command(heli);
        std::memcpy(actual.data(),vehicles.data()+0x3EC,12);
        check(result.executed && result.native_entered && actual==std::array<float,3>{{10,-20,70}} && vehicles[0x3BA]==20 && vehicles[0x3D0]==uint8_t(speed),
            "Helicopter command enters live flight controller and retains original low-byte speed semantics");
        check(*reinterpret_cast<float*>(car+0x9B0)==15.5f && *reinterpret_cast<float*>(car+0x9A8)==90.25f && *reinterpret_cast<float*>(car+0x8A8)==1,
            "Original speed is never interpreted as an altitude or used to replace existing orientation/altitude limits");
    }
    *reinterpret_cast<int32_t*>(car+0x594)=4;rejects_without_mutation(heli,"Mismatched heli subclass cannot grant helicopter storage access");*reinterpret_cast<int32_t*>(car+0x594)=3;
    *reinterpret_cast<float*>(car+0x8A8)=0;rejects_without_mutation(heli,"Uninitialized flight heading requires a live finite matrix before native atan2");
    std::array<float,16> heli_matrix{};heli_matrix[5]=1;*reinterpret_cast<void**>(car+0x14)=heli_matrix.data();
    check(sa_execute_entity_command(heli).executed,"Finite live helicopter matrix permits native heading initialization");
    heli_matrix[4]=std::numeric_limits<float>::infinity();rejects_without_mutation(heli,"Non-finite helicopter basis cannot enter native heading calculation");heli_matrix[4]=0;
    *reinterpret_cast<float*>(car+0x8A8)=1;
    for(size_t axis=1;axis<4;++axis) {
        auto invalid=heli;invalid.inputs[axis]=entity_bits(std::numeric_limits<float>::infinity());rejects_without_mutation(invalid,"Flight destination validates all original axes before native entry");
    }
    heli_fault=true;const auto heli_failed=sa_execute_entity_command(heli);heli_fault=false;
    check(!heli_failed.executed && heli_failed.native_entered,"Flight engine fault is entered and cannot replay as a refused instruction");
    for(uintptr_t address:{uintptr_t(0x6A2390),uintptr_t(0x6A23D9)}) {
        auto* byte=reinterpret_cast<uint8_t*>(native_entity_address(address));const auto saved=*byte;*byte^=0x80;
        rejects_without_mutation(heli,"Altered flight service refuses before changing autopilot");*byte=saved;
    }
    auto ram=operation(EntityCommand::car_ram_car,{7,0x20C});
    for(uint8_t speed:{uint8_t(0),uint8_t(5),uint8_t(6),uint8_t(100),uint8_t(127),uint8_t(128),uint8_t(255)}) {
        vehicles[0x3D0]=speed;vehicles[0x428]=0xA1;const auto before=vehicles;
        const auto result=sa_execute_entity_command(ram);
        check(result.executed && result.native_entered && vehicles[0x3BA]==15 && vehicles[0x3D0]==uint8_t(int8_t(speed)<6?6:speed) && vehicles[0x428]==0xB1 && *reinterpret_cast<uintptr_t*>(car+0x41C)==other,
            "RAM target, engine flag and signed-byte minimum speed follow original helper, never SA's empty 032C handler");
        check(entity_references.size()==1 && entity_references[0]==std::make_pair(other,reinterpret_cast<uintptr_t*>(car+0x41C)),
            "RAM target owns exactly one reference across repeated assignments");
        for(size_t n=0;n<vehicles.size();++n)if(n!=0x3D0 && n!=0x428 && n!=0x3BA && (n<0x41C || n>=0x420))
            check(vehicles[n]==before[n],"RAM assignment preserves controls, existing road state, velocity, status, models and all adjacent fields");
    }
    for(int32_t bad:{-1,8,0x18A,0x20B,0x307,INT32_MAX}) {
        auto invalid=ram;invalid.inputs[0]=bad;rejects_without_mutation(invalid,"RAM attacker must be a live vehicle generation");
        invalid=ram;invalid.inputs[1]=bad;rejects_without_mutation(invalid,"RAM target must be a live vehicle generation");
    }
    native_cleanup_reference(other,reinterpret_cast<uintptr_t*>(car+0x41C));*reinterpret_cast<uintptr_t*>(car+0x41C)=1;
    rejects_without_mutation(ram,"Arbitrary old RAM target cannot be dereferenced for reference cleanup");*reinterpret_cast<uintptr_t*>(car+0x41C)=0;
    reference_fault=true;const auto reference_failed=sa_execute_entity_command(ram);reference_fault=false;
    check(!reference_failed.executed && reference_failed.native_entered,"Target reference fault after mutation cannot fabricate success or replay");
    for(uintptr_t address:{uintptr_t(0x432CB8),uintptr_t(0x432D31),uintptr_t(0x432D8B),uintptr_t(0x432D74),uintptr_t(0x571A00),uintptr_t(0x571B70)}) {
        auto* byte=reinterpret_cast<uint8_t*>(native_entity_address(address));const auto saved=*byte;*byte^=0x80;
        rejects_without_mutation(ram,"Altered mission/target/reference layout cannot grant RAM controller writes");*byte=saved;
    }
    // Simulate native deletion's reference resolution. Old attachment refs
    // were removed, and only the currently registered target slot is cleared.
    for(auto ref:entity_references)*ref.second=0;entity_references.clear();
    check(!*reinterpret_cast<uintptr_t*>(car+0x41C) && !*reinterpret_cast<uintptr_t*>(obj+0xFC),"Target destruction cannot leave stale native controller or attachment pointers");
    *reinterpret_cast<uint32_t*>(ped+0x598)=4;*reinterpret_cast<uintptr_t*>(ped+0xFC)=0;
    auto character=operation(EntityCommand::char_attach_object,{27,19,entity_bits(1),entity_bits(2),entity_bits(3),0x10005,entity_bits(90),22});
    std::array<uint8_t,0x7C4> before_character{};std::memcpy(before_character.data(),ped,before_character.size());
    const auto character_attached=sa_execute_entity_command(character);
    check(character_attached.executed && character_attached.native_entered && character_attached.condition && character_attach_calls==1 &&
        character_facing==5 && character_weapon==22 && character_angle==90*entity_float(0x3C8EFA36),
        "Character attachment uses source ushort facing, actual float degree operand/multiplier and translated native weapon");
    for(size_t n=0;n<before_character.size();++n)if((n<0xFC || n>=0x100) && (n<0x76C || n>=0x77A) && (n<0x77C || n>=0x780))
        check(ped[n]==before_character[n],"Character attachment boundary preserves rig, culling and unrelated native fields");
    auto reject_character=[&](const NativeCall& call,const char* reason) {
        std::array<uint8_t,0x7C4> before{};std::memcpy(before.data(),ped,before.size());
        rejects_without_mutation(call,reason);check(!std::memcmp(before.data(),ped,before.size()),"Character preflight refusal cannot mutate pedestrian");
    };
    for(size_t n=2;n<=6;++n)if(n!=5) {
        auto invalid=character;invalid.inputs[n]=entity_bits(std::numeric_limits<float>::quiet_NaN());
        reject_character(invalid,"Character attachment rejects malformed offset/angle before native entry");
    }
    auto invalid_character=character;invalid_character.inputs[0]=28;reject_character(invalid_character,"Character attachment rejects recycled actor generation");
    invalid_character=character;invalid_character.inputs[1]=20;reject_character(invalid_character,"Character attachment rejects recycled object generation");
    *reinterpret_cast<uintptr_t*>(obj+0xFC)=reinterpret_cast<uintptr_t>(ped);reject_character(character,"Character/object attachment cycle cannot reach engine");*reinterpret_cast<uintptr_t*>(obj+0xFC)=0;
    for(uintptr_t address:{uintptr_t(0x5E7CB0),uintptr_t(0x401697),uintptr_t(0x5E7CCE),uintptr_t(0x54FF90),uintptr_t(0x550050)}) {
        auto* byte=reinterpret_cast<uint8_t*>(native_entity_address(address));const auto saved=*byte;*byte^=0x80;
        reject_character(character,"Unknown character helper/trampoline/pool refuses before calling native code");*byte=saved;
    }
    character_controllable=false;const auto character_before=entity_references;
    const auto ignored_character=sa_execute_entity_command(character);character_controllable=true;
    check(ignored_character.executed && !ignored_character.condition && character_before==entity_references,
        "Real helper's non-controllable return does not fabricate attachment or discard the old parent reference");
    character_attach_fault=true;const auto character_failed=sa_execute_entity_command(character);character_attach_fault=false;
    check(!character_failed.executed && character_failed.native_entered,"Character helper fault after native entry prevents instruction replay");
    for(const auto& call:{attached,detached,heli,ram,character}) {
        auto malformed=call;malformed.inputs.push_back(9);rejects_without_mutation(malformed,"Vehicle/attachment packet rejects extra source operands");
        malformed=call;malformed.outputs=1;rejects_without_mutation(malformed,"Vehicle/attachment packet cannot invent an output");
        malformed=call;malformed.predicate=true;rejects_without_mutation(malformed,"Vehicle/attachment setter cannot fabricate a predicate");
    }
    {
        profile(pe,0x48542C,90);profile(pe,0x48548A,28);
        profile(pe,0x56E210,28);profile(pe,0x558E40,33);profile(pe,0x54FF90,37);profile(pe,0x54FFF0,37);
        auto activate=operation(EntityCommand::activate_save_menu,{});
        auto query=operation(EntityCommand::save_game_finished,{});query.predicate=true;
        auto* focus_slot=reinterpret_cast<uint8_t*>(native_entity_address(0xB7CD74));
        auto* menu_active=reinterpret_cast<uint8_t*>(native_entity_address(0xBA67A4));
        auto* save_active=reinterpret_cast<uint8_t*>(native_entity_address(0xBA67A7));
        auto* visits=reinterpret_cast<int32_t*>(native_entity_address(0xB79040));
        const auto saved_ped=std::vector<uint8_t>(ped,ped+0x7C4);
        const auto saved_vehicles=vehicles;const auto saved_vehicle_flags=vehicle_flags;
        const auto saved_ped_flag=ped_flag;
        *focus_slot=0;pointer(0xB7CD98,ped);pointer(0xB7CD98+0x190,nullptr);
        ped_flag=27;ped[0x36]=3;*reinterpret_cast<const void**>(ped+0x480)=ped+0x600;
        ped[0x46D]&=~1u;*reinterpret_cast<uintptr_t*>(ped+0x58C)=0;
        *menu_active=0;*save_active=0;*visits=41;
        save_active[-1]=0xAA;save_active[1]=0xBB;visits[-1]=111;visits[1]=222;
        const std::array<float,3> initial_move{{1.5f,-2.25f,3.75f}},initial_turn{{-0.5f,0.25f,1.25f}};
        std::memcpy(ped+0x44,initial_move.data(),12);std::memcpy(ped+0x50,initial_turn.data(),12);
        std::array<uint8_t,0x7C4> expected_ped{};std::memcpy(expected_ped.data(),ped,0x7C4);
        std::memset(expected_ped.data()+0x44,0,24);
        check(sa_execute_entity_command(query).condition,"0990 reports save finished before 0989 activates save menu");
        auto activated=sa_execute_entity_command(activate);
        check(activated.executed && activated.native_entered && *visits==42 && *save_active==1 && *menu_active==0 &&
              save_active[-1]==0xAA && save_active[1]==0xBB && visits[-1]==111 && visits[1]==222 &&
              !std::memcmp(ped,expected_ped.data(),0x7C4),
              "0989 increments SafeHouseVisits, sets m_bActivateSaveMenu, zeroes move/turn speeds and preserves all adjacent ped/stat/menu state");
        const auto after_activate_query=sa_execute_entity_command(query);
        check(after_activate_query.executed && !after_activate_query.native_entered && !after_activate_query.condition,
              "0990 consumer immediately observes 0989's live m_bActivateSaveMenu latch as save-in-progress");
        PauseMenuGate save_gate;save_gate.disabled=true;
        check(save_gate.delegate(false,false,false,*save_active!=0) && !save_gate.delegate(false,false,false),
              "Explicit 0989 save request reaches native frontend even while manual pause activation is disabled");
        *visits=INT32_MAX;*save_active=0;
        std::memcpy(ped+0x44,initial_move.data(),12);std::memcpy(ped+0x50,initial_turn.data(),12);
        activated=sa_execute_entity_command(activate);
        check(activated.executed && activated.native_entered && *visits==INT32_MIN && *save_active==1,
              "0989 preserves 32-bit wrapping int32 increment on SafeHouseVisits");
        *focus_slot=1;pointer(0xB7CD98,nullptr);pointer(0xB7CD98+0x190,ped);
        *visits=7;*save_active=0;
        std::memcpy(ped+0x44,initial_move.data(),12);std::memcpy(ped+0x50,initial_turn.data(),12);
        activated=sa_execute_entity_command(activate);
        check(activated.executed && activated.native_entered && *visits==8 && *save_active==1 &&
              !std::memcmp(ped,expected_ped.data(),0x7C4),
              "0989 resolves the focused player slot via PlayerInFocus rather than assuming player zero");
        *focus_slot=0;pointer(0xB7CD98,nullptr);pointer(0xB7CD98+0x190,nullptr);
        *visits=19;*save_active=0;
        std::memcpy(ped+0x44,initial_move.data(),12);std::memcpy(ped+0x50,initial_turn.data(),12);
        const auto absent_player=sa_execute_entity_command(activate);
        check(absent_player.executed && !absent_player.native_entered && *visits==19 && *save_active==0 &&
              !std::memcmp(ped+0x44,initial_move.data(),12) && !std::memcmp(ped+0x50,initial_turn.data(),12),
              "0989 treats a null FindPlayerPed() slot as the original clean no-op without mutating stats, menu or velocity");
        pointer(0xB7CD98,ped);
        vehicle_flags[0]=7;vehicles[0x36]=2;
        ped[0x46D]|=1u;*reinterpret_cast<uintptr_t*>(ped+0x58C)=reinterpret_cast<uintptr_t>(vehicles.data());
        const auto in_car=sa_execute_entity_command(activate);
        check(in_car.executed && !in_car.native_entered && *visits==19 && *save_active==0 &&
              !std::memcmp(ped+0x44,initial_move.data(),12) && !std::memcmp(ped+0x50,initial_turn.data(),12),
              "0989 treats an in-vehicle player as the original clean no-op without mutating stats, menu or velocity");
        *reinterpret_cast<uintptr_t*>(ped+0x58C)=0;
        const auto inconsistent_flag=sa_execute_entity_command(activate);
        check(inconsistent_flag.executed && !inconsistent_flag.native_entered && *visits==19 && *save_active==0,
              "0989 occupied flag is a clean no-op even when the vehicle reference is null");
        ped[0x46D]&=~1u;*reinterpret_cast<uintptr_t*>(ped+0x58C)=reinterpret_cast<uintptr_t>(vehicles.data());
        const auto inconsistent_ptr=sa_execute_entity_command(activate);
        check(inconsistent_ptr.executed && inconsistent_ptr.native_entered && *visits==20 && *save_active==1 &&
              *reinterpret_cast<uintptr_t*>(ped+0x58C)==reinterpret_cast<uintptr_t>(vehicles.data()) &&
              !std::memcmp(ped+0x44,expected_ped.data()+0x44,24),
              "0989 on-foot activation ignores and preserves a retained last-vehicle reference");
        *visits=19;*save_active=0;
        std::memcpy(ped+0x44,initial_move.data(),12);std::memcpy(ped+0x50,initial_turn.data(),12);
        ped[0x46D]|=1u;vehicle_flags[0]|=0x80u;
        const auto dead_car=sa_execute_entity_command(activate);
        check(dead_car.executed && !dead_car.native_entered && *visits==19 && *save_active==0,
              "0989 occupied no-op does not dereference a deallocated vehicle reference");
        vehicle_flags[0]=7;ped[0x46D]&=~1u;*reinterpret_cast<uintptr_t*>(ped+0x58C)=0;
        *focus_slot=2;
        check(!sa_execute_entity_command(activate).executed && *visits==19 && *save_active==0,
              "0989 refuses out-of-range PlayerInFocus before indexing player info");
        *focus_slot=0;ped_flag|=0x80u;
        check(!sa_execute_entity_command(activate).executed && *visits==19 && *save_active==0,
              "0989 refuses deallocated player ped pool slot instead of treating it as null FindPlayerPed()");
        ped_flag=27;
        for(uintptr_t address:{uintptr_t(0x48542C),uintptr_t(0x485469),uintptr_t(0x48547F),uintptr_t(0x48548A),
                               uintptr_t(0x56E210),uintptr_t(0x558E40),uintptr_t(0x54FF90),uintptr_t(0x54FFF0)}) {
            auto* code=reinterpret_cast<uint8_t*>(native_entity_address(address));const auto saved=*code;*code^=0x80;
            const auto bad=sa_execute_entity_command(activate);*code=saved;
            check(!bad.executed && !bad.native_entered && *visits==19 && *save_active==0,
                  "0989 refuses altered save/stat/speed/player/pool profile before any mutation");
        }
        for(auto malformed:{operation(EntityCommand::activate_save_menu,{1}),operation(EntityCommand::activate_save_menu,{},1)}) {
            check(!sa_execute_entity_command(malformed).executed && *visits==19 && *save_active==0,
                  "0989 enforces zero-input, zero-output operand layout");
        }
        auto predicate_mismatch=activate;predicate_mismatch.predicate=true;
        check(!sa_execute_entity_command(predicate_mismatch).executed && *visits==19 && *save_active==0,
              "0989 refuses predicate layout mismatch");
        DWORD save_prev=0,save_ign=0;
        auto* save_page=reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(save_active)&~uintptr_t(4095));
        check(VirtualProtect(save_page,4096,PAGE_READONLY,&save_prev)!=0,"Protect owned menu page for the first source write");
        const auto menu_fault=sa_execute_entity_command(activate);
        check(VirtualProtect(save_page,4096,save_prev,&save_ign)!=0,"Restore owned menu page protection");
        check(!menu_fault.executed && menu_fault.native_entered && *visits==19 && *save_active==0 &&
              !std::memcmp(ped+0x44,initial_move.data(),12),
              "0989 menu write fault preserves counter/speeds because the original menu store precedes stat increment");
        check(VirtualProtect(ped,4096,PAGE_READONLY,&save_prev)!=0,"Protect owned ped page for 0989 partial-write fault test");
        const auto faulted=sa_execute_entity_command(activate);
        check(!faulted.executed && faulted.native_entered && *visits==20 && *save_active==1,
              "0989 velocity write fault after stat/menu writes reports entered failure and prevents replay");
        check(VirtualProtect(ped,4096,save_prev,&save_ign)!=0,"Restore owned ped page protection");
        *menu_active=0;*save_active=0;*visits=0;
        std::memcpy(ped,saved_ped.data(),saved_ped.size());
        vehicles=saved_vehicles;vehicle_flags=saved_vehicle_flags;ped_flag=saved_ped_flag;
    }
    {
        profile(pe,0x56E830,39);profile(pe,0x47C95E,9);profile(pe,0x47C9CD,5);profile(pe,0x569660,11);
        auto* focus=reinterpret_cast<uint8_t*>(native_entity_address(0xB7CD74));
        auto* state0=reinterpret_cast<uint8_t*>(native_entity_address(0xB7CD98+0xDC));
        auto* state1=state0+0x190;
        auto request=operation(EntityCommand::restart_critical_mission,{entity_bits(10),entity_bits(20),entity_bits(7),entity_bits(90)});
        *focus=1;*state0=0;*state1=0;*reinterpret_cast<uint32_t*>(native_entity_address(0xB7CB84))=12345;
        check(sa_execute_entity_command(request).executed && critical_overrides==1 && critical_failures==1 &&
              critical_position==std::array<float,3>{10,20,7} && critical_heading==90 && *state0==0 && *state1==3 &&
              *reinterpret_cast<uint32_t*>(native_entity_address(0x96A8AC))==12345,
              "Critical restart arms original coordinates/heading then enters focused native mission-failure state");
        request.inputs[2]=entity_bits(-100);*state1=2;
        check(sa_execute_entity_command(request).executed && critical_position[2]==33 && *state1==2,
              "Critical restart resolves low Z through native ground and preserves a non-playing player state");
        const auto before=critical_overrides;request.inputs[0]=entity_bits(NAN);
        check(!sa_execute_entity_command(request).native_entered && critical_overrides==before,"Non-finite restart refuses before mutation");
        request.inputs[0]=entity_bits(10);request.inputs[2]=entity_bits(7);*focus=2;
        check(!sa_execute_entity_command(request).native_entered && critical_overrides==before,"Invalid player focus refuses before override");
        *focus=0;critical_fault=true;const auto failure_count=critical_failures;
        const auto partial=sa_execute_entity_command(request);
        check(!partial.executed && partial.native_entered && critical_failures==failure_count,
              "An override fault is an entered failure, never replayable native success");
        critical_fault=false;request.inputs.push_back(0);
        check(!sa_execute_entity_command(request).native_entered,"Critical restart rejects malformed operand layout");
    }
    VirtualFree(ped,0,MEM_RELEASE);pointer(0xB7CD98,nullptr);
    std::cout<<"PASS: compiled native entity/statistics/rampage/environment/garage/traffic backends on owned non-executable pages; native engine boundaries mocked; GTA not loaded/executed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
