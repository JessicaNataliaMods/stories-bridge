#include <windows.h>

#include "sa_owned_vehicle_entry.hpp"
#include "lcs/native_memory.hpp"
#include "lcs/sa_task_memory.hpp"
#include "lcs/route_vehicle_native.hpp"
#include "lcs/sa_script.hpp"

#include <array>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>

namespace lcs {

#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
uintptr_t native_owned_vehicle_entry_allocate(uint32_t bytes);
void native_owned_vehicle_entry_release(uintptr_t memory);
int32_t native_owned_vehicle_entry_passenger_door(uintptr_t vehicle,int32_t seat);
void native_owned_vehicle_entry_construct_driver(uintptr_t task,uintptr_t vehicle);
void native_owned_vehicle_entry_construct_passenger(uintptr_t task,uintptr_t vehicle,
        int32_t seat,bool carry_on_after_falling_off);
void native_owned_vehicle_entry_discard(uintptr_t task,uint16_t opcode);
bool native_owned_vehicle_entry_assign(SaScript* script,int32_t actor,
        uintptr_t task,int32_t opcode);
bool native_owned_vehicle_entry_abort(uintptr_t task,uintptr_t ped,int32_t priority);
void native_owned_vehicle_entry_construct_standstill(uintptr_t task,int32_t duration,
        bool stay_in_same_place,bool use_entire_area,float blend_delta);
bool native_owned_vehicle_entry_subtask_table_matches(uintptr_t table,int32_t type);
// The isolated fixture cannot place executable retail code at the retail addresses (their bytes are PE
// profiles), so it supplies the callable stand-ins that the checked vtable slots must contain.
uintptr_t native_owned_vehicle_entry_expected(size_t slot,uint16_t opcode);
bool native_owned_vehicle_entry_target_matches(size_t slot,uint16_t opcode,uintptr_t retail);
#endif

namespace {
constexpr uintptr_t native_task_allocator=0x61A5A0;
constexpr uintptr_t native_task_release=0x61A5B0;
constexpr uintptr_t native_driver_constructor=0x6402F0;
constexpr uintptr_t native_passenger_constructor=0x640340;
constexpr uintptr_t native_driver_clone=0x643780;
constexpr uintptr_t native_passenger_clone=0x6437F0;
constexpr uintptr_t native_enter_car_abort=0x63A730;
constexpr uintptr_t native_enter_car_control=0x63A890;
constexpr uintptr_t native_driver_destructor=0x643C60;
constexpr uintptr_t native_enter_car_transition=0x63F970;
constexpr uintptr_t native_enter_car_create_next=0x63E990;
constexpr uintptr_t native_enter_car_create_first=0x643A60;
constexpr uintptr_t native_enter_car_destructor=0x63DFA0;
constexpr uintptr_t native_passenger_destructor=0x643C80;
constexpr uintptr_t native_standstill_constructor=0x62F310;
constexpr uintptr_t native_assign_script_task=0x465C20;
constexpr uintptr_t native_task_pool=0xB744A8;
constexpr uintptr_t native_script_event_type=0x85B378;
constexpr uint32_t task_bytes=0x50;
constexpr uint32_t standstill_task_bytes=0x20;
constexpr int32_t abort_priority_urgent=1;
constexpr int32_t approach_task_type=800; // TASK_COMPLEX_GO_TO_CAR_DOOR_AND_STAND_STILL
constexpr int32_t standstill_task_type=203; // TASK_SIMPLE_STAND_STILL

// Retail 1.0 US fingerprints supplied by PE inspection and the existing
// passenger-task path. Constructor output is checked again before ownership is
// attached; no native task address is inferred from a guessed layout.
constexpr uint8_t allocator_profile[]={
    0x8B,0x0D,0xA8,0x44,0xB7,0x00,0xE9,0x55,0xFF,0xFF,0xFF};
constexpr uint8_t release_profile[]={0x8B,0x0D,0xA8,0x44,0xB7,0x00};
constexpr uint8_t driver_destructor_profile[]={
    0x56,0x8B,0xF1,0xE8,0xC8,0xC6,0xFF,0xFF,0xF6,0x44,0x24,0x08,0x01,0x74,0x09,0x56,
    0xE8,0x3B,0x69,0xFD,0xFF,0x83,0xC4,0x04,0x8B,0xC6,0x5E,0xC2,
};
constexpr uint8_t entry_transition_profile[]={
    0x6A,0xFF,0x68,0xA5,0x1E,0x84,0x00,0x64,0xA1,0x00,0x00,0x00,0x00,0x50,0x64,0x89,
    0x25,0x00,0x00,0x00,0x00,0x83,0xEC,0x4C,0x53,0x55,0x56,0x57,0x8B,0x7C,0x24,0x6C,
};
// SA05CA maps SCM passenger ordinals through ComputePassengerDoorToEnter
// (0x64F190); -1 skips that query and passes door selector0 to the task.
constexpr uint8_t passenger_door_profile[]={
    0x8B,0x54,0x24,0x04,0x8A,0x8A,0x29,0x04,0x00,0x00,0x83,0xC8,0xFF,0xF6,0xC1,0x02,
    0x74,0x06,0xB8,0x08,0x00,0x00,0x00,0xC3,0x8B,0x4C,0x24,0x08,0x83,0xE9,0x00,0x74,
    0x0C,0x49,0x74,0x26,0x49,0x75,0x28,0xB8,0x09,0x00,0x00,0x00,0xC3,0x83,0xBA,0x90,
    0x05,0x00,0x00,0x09,0x74,0x14,0x8B,0x82,0x84,0x03,0x00,0x00,0x8B,0x88,0xCC,0x00,
    0x00,0x00,0xC1,0xE9,0x09,0xF6,0xC1,0x01,0x74,0xC8,0xB8,0x0B,0x00,0x00,0x00,0xC3,
};
constexpr uint8_t driver_constructor_profile[]={
    0x8B,0x44,0x24,0x04,0x56,0x6A,0x00,0x6A,0x00,0x6A,0x00,0x6A,
    0x01,0x50,0x8B,0xF1,0xE8,0x1B,0x9F,0xFF,0xFF,0xC7,0x06,0xAC};
constexpr uint8_t passenger_constructor_profile[]={
    0x8B,0x44,0x24,0x0C,0x56,0x50,0x6A,0x00,0x6A,0x00,0x8B,0xF1,
    0x8B,0x4C,0x24,0x14,0x6A,0x00,0x51,0x8B,0xCE,0xE8,0xC6,0x9E};
constexpr uint8_t driver_clone_profile[]={
    0x6A,0xFF,0x68,0x4B,0x22,0x84,0x00,0x64,0xA1,0x00,0x00,0x00,
    0x00,0x50,0x64,0x89,0x25,0x00,0x00,0x00};
constexpr uint8_t passenger_clone_profile[]={
    0x6A,0xFF,0x68,0x6B,0x22,0x84,0x00,0x64,0xA1,0x00,0x00,0x00,
    0x00,0x50,0x64,0x89,0x25,0x00,0x00,0x00};
constexpr uint8_t enter_car_destructor_profile[]={
    0x6A,0xFF,0xE9,0xA1,0x31,0xDC,0xFF,0x64,0xA1,0x00,0x00,0x00,
    0x00,0x50,0x64,0x89,0x25,0x00,0x00,0x00};
constexpr uint8_t passenger_destructor_profile[]={
    0x56,0x8B,0xF1,0xE8,0xF8,0xC6,0xFF,0xFF,0xF6,0x44,0x24,0x08,
    0x01,0x74,0x09,0x56,0xE8,0x1B,0x69,0xFD,0xFF,0x83,0xC4,0x04};
constexpr uint8_t abort_profile[]={
    0x64,0xA1,0x00,0x00,0x00,0x00,0x6A,0xFF,0x68,0x98,0x13,0x84,
    0x00,0x50,0x64,0x89,0x25,0x00,0x00,0x00};
constexpr uint8_t control_profile[]={
    0x6A,0xFF,0x68,0xD6,0x13,0x84,0x00,0x64,0xA1,0x00,0x00,0x00,
    0x00,0x50,0x64,0x89,0x25,0x00,0x00,0x00};
constexpr uint8_t first_subtask_profile[]={
    0x6A,0xFF,0x64,0xA1,0x00,0x00,0x00,0x00,0x68,0x13,0x23,0x84,
    0x00,0x50,0xA1,0x84,0xCB,0xB7,0x00,0x64,0x89,0x25,0x00,0x00};
constexpr uint8_t next_subtask_profile[]={
    0x64,0xA1,0x00,0x00,0x00,0x00,0x6A,0xFF,0x68,0x25,0x1E,0x84,
    0x00,0x50,0x64,0x89,0x25,0x00,0x00,0x00};
constexpr uint8_t script_assign_profile[]={
    0x64,0xA1,0x00,0x00,0x00,0x00,0x6A,0xFF,0x68,0x18,0x7D,0x83,0x00};
constexpr uint8_t event_type_profile[]={0xB8,0x20,0x00,0x00,0x00,0xC3};
// Retail CreateSubTask at 0x6682D0 allocates 0x20 bytes with 0x61A5A0 and
// calls this constructor. Its ordinary standstill branch passes (1,false,false,8.0f).
constexpr uint8_t standstill_constructor_profile[]={
    0x56,0x8B,0xF1,0xE8,0x78,0xB0,0xFE,0xFF,0x8B,0x44,0x24,0x08,
    0x8A,0x4C,0x24,0x0C,0x8A,0x54,0x24,0x10,0x89,0x46,0x08,0x33,
    0xC0,0xC7,0x06,0x2C,0xDD,0x86,0x00,0x89};
constexpr uint8_t standstill_vtable_profile[]={
    0x80,0x74,0x63,0x00,0xF0,0x5C,0x63,0x00,0x00,0xE3,0x43,0x00,
    0x10,0xE3,0x43,0x00,0x50,0xF3,0x62,0x00,0x80,0x11,0x42,0x00,
    0x90,0x86,0x4B,0x00,0x70,0xF3,0x62,0x00,0x20,0xE3,0x43,0x00,
    0xA0,0x74,0x63,0x00,0xD0,0x5B,0x63,0x00};

struct Lineage;
struct PrivateTable {
    Lineage* owner=nullptr;
    uintptr_t prefix=0;
    std::array<uintptr_t,12> entries{};
};
static_assert(offsetof(PrivateTable,entries)==2*sizeof(uintptr_t),
        "Private task vtable header must keep its owner immediately before entries");

struct Lineage {
    PrivateTable table;
    uintptr_t native_vtable=0;
    std::array<uintptr_t,12> original{};
    ObjectiveActor actor;
    uintptr_t actor_cell=0;
    uintptr_t accepted_task=0;
    uint16_t opcode=0;
    uint64_t epoch=0;
    DWORD thread=0;
    size_t bound_slot=128;
    CapturedScriptTask binding{};
    std::atomic<uint32_t> instances{1};
    std::atomic<bool> healthy{true};
    std::atomic<bool> first_called{false};
    std::atomic<bool> cancel_requested{false};
    std::atomic<bool> abort_accepted{false};
    std::atomic<bool> wait_requested{false};
    std::unordered_map<uintptr_t,uintptr_t> pause_children;
    bool binding_bound=false; // construction/inspection game thread only
    std::atomic<bool> release_requested{false};
};

std::unordered_map<uintptr_t,std::unique_ptr<Lineage>> lineages;
std::atomic<DWORD> registry_thread{0};
uint64_t serial=0;

bool on_registry_thread() {
    DWORD first=0;
    registry_thread.compare_exchange_strong(first,GetCurrentThreadId(),std::memory_order_acq_rel);
    return registry_thread.load(std::memory_order_acquire)==GetCurrentThreadId();
}
bool callback_thread(Lineage* lineage) {
    if(GetCurrentThreadId()==lineage->thread)return true;
    lineage->healthy.store(false,std::memory_order_release);
    return false;
}
int native_fault(Lineage* lineage) {
    lineage->healthy.store(false,std::memory_order_release);
    return EXCEPTION_CONTINUE_SEARCH;
}
bool profile(uintptr_t address,const uint8_t* bytes,size_t length) {
    __try {
        return std::memcmp(reinterpret_cast<const void*>(native_entity_address(address)),bytes,length)==0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool common_profiles_match(const char** mismatch=nullptr) {
    const auto check=[&](const char* name,uintptr_t address,const uint8_t* bytes,size_t length) {
        if(profile(address,bytes,length))return true;
        if(mismatch)*mismatch=name;
        return false;
    };
    // These are required to construct, assign, and own an ENTER_CAR root.
    // The standstill task is only used by the optional out-of-range wait path.
    if(!sa_task_allocator_profile_matches()) {
        if(mismatch)*mismatch="task allocator 0x61A5A0";
        return false;
    }
    if(!sa_task_release_profile_matches()) {
        if(mismatch)*mismatch="task release 0x61A5B0";
        return false;
    }
    return check("script task assignment 0x465C20",native_assign_script_task,script_assign_profile,sizeof(script_assign_profile)) &&
        check("entry destructor 0x63DFA0",native_enter_car_destructor,enter_car_destructor_profile,sizeof(enter_car_destructor_profile)) &&
        check("passenger destructor 0x643C80",native_passenger_destructor,passenger_destructor_profile,sizeof(passenger_destructor_profile)) &&
        check("entry abort 0x63A730",native_enter_car_abort,abort_profile,sizeof(abort_profile)) &&
        check("entry control 0x63A890",native_enter_car_control,control_profile,sizeof(control_profile)) &&
        check("entry transition 0x63F970",native_enter_car_transition,entry_transition_profile,sizeof(entry_transition_profile)) &&
        check("first subtask 0x643A60",native_enter_car_create_first,first_subtask_profile,sizeof(first_subtask_profile)) &&
        check("next subtask 0x63E990",native_enter_car_create_next,next_subtask_profile,sizeof(next_subtask_profile));
}
bool wait_profiles_match(const char** mismatch=nullptr) {
    if(!profile(native_standstill_constructor,standstill_constructor_profile,
            sizeof(standstill_constructor_profile))) {
        if(mismatch)*mismatch="standstill constructor 0x62F310";
        return false;
    }
    if(!profile(0x86DD2C,standstill_vtable_profile,sizeof(standstill_vtable_profile))) {
        if(mismatch)*mismatch="standstill vtable 0x86DD2C";
        return false;
    }
    return true;
}
bool type_profiles_match(uint16_t opcode,const char** mismatch=nullptr) {
    if(opcode==0x05CB) {
        if(!profile(native_driver_destructor,driver_destructor_profile,sizeof(driver_destructor_profile))) {
            if(mismatch)*mismatch="driver deleting destructor 0x643C60";
            return false;
        }
        if(!profile(native_driver_constructor,driver_constructor_profile,sizeof(driver_constructor_profile))) {
            if(mismatch)*mismatch="driver entry constructor 0x6402F0";
            return false;
        }
        if(!profile(native_driver_clone,driver_clone_profile,sizeof(driver_clone_profile))) {
            if(mismatch)*mismatch="driver entry clone 0x643780";
            return false;
        }
        return true;
    }
    if(opcode==0x05CA) {
        if(!profile(0x64F190,passenger_door_profile,sizeof(passenger_door_profile))) {
            if(mismatch)*mismatch="passenger seat-to-door selector 0x64F190";
            return false;
        }
        if(!profile(native_passenger_constructor,passenger_constructor_profile,sizeof(passenger_constructor_profile))) {
            if(mismatch)*mismatch="passenger entry constructor 0x640340";
            return false;
        }
        if(!profile(native_passenger_clone,passenger_clone_profile,sizeof(passenger_clone_profile))) {
            if(mismatch)*mismatch="passenger entry clone 0x6437F0";
            return false;
        }
        return true;
    }
    return false;
}
bool entry_profiles_match(uint16_t opcode,const char** mismatch=nullptr) {
    return common_profiles_match(mismatch) && type_profiles_match(opcode,mismatch);
}

Lineage* embedded_owner(uintptr_t task) {
    __try {
        if(!task)return nullptr;
        const auto table=*reinterpret_cast<const uintptr_t*>(task);
        if(!table)return nullptr;
        auto* lineage=*reinterpret_cast<Lineage*const*>(table-2*sizeof(uintptr_t));
        return lineage && reinterpret_cast<uintptr_t>(lineage->table.entries.data())==table ? lineage : nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
bool read_task_vtable(uintptr_t task,uintptr_t& table) {
    __try { if(!task)return false;table=*reinterpret_cast<const uintptr_t*>(task);return table!=0; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool private_shape(uintptr_t task,uintptr_t private_table) {
    __try {
        return task && *reinterpret_cast<const uintptr_t*>(task)==private_table;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
Lineage* registered_owner(uintptr_t task) {
    if(!on_registry_thread())return nullptr;
    uintptr_t table=0;
    if(!read_task_vtable(task,table))return nullptr;
    const auto found=lineages.find(table);
    return found==lineages.end()?nullptr:found->second.get();
}
bool task_pool_contains(uintptr_t task) { return sa_task_slot_live(task); }

bool valid_subtask_type(uintptr_t subtask,int32_t expected);
bool valid_vehicle_approach(uintptr_t subtask);
bool is_pause_child(Lineage& lineage,uintptr_t root_task,uintptr_t subtask);
uintptr_t create_pause_child(Lineage& lineage,uintptr_t root_task);
uintptr_t restart_entry_subtask(Lineage& lineage,uintptr_t root_task,uintptr_t ped);
bool invoke_subtask_abort(uintptr_t subtask,uintptr_t ped);
uintptr_t expected_entry(size_t slot,uint16_t opcode,uintptr_t retail) {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
    return native_owned_vehicle_entry_target_matches(slot,opcode,retail)?native_owned_vehicle_entry_expected(slot,opcode):0;
#else
    (void)slot;(void)opcode;return native_entity_address(retail);
#endif
}
bool read_native_table(uintptr_t task,uint16_t opcode,uintptr_t& native_vtable,
        std::array<uintptr_t,12>& entries,uintptr_t& prefix) {
    __try {
        native_vtable=*reinterpret_cast<const uintptr_t*>(task);
        if(!native_vtable || native_vtable==native_entity_address(0x86FDA8))return false;
        const auto* table=reinterpret_cast<const uintptr_t*>(native_entity_address(native_vtable));
        for(size_t i=0;i<entries.size();++i)entries[i]=table[i];
        prefix=table[-1];
        // These two native classes each have twelve virtual methods. Slot11
        // is called by native CreateNextSubTask at63ED3E; it must remain a
        // native method, never the adjacent Lineage metadata after the array.
        const uintptr_t expected[]{opcode==0x05CB?native_driver_destructor:native_passenger_destructor,
            opcode==0x05CB?native_driver_clone:native_passenger_clone,0x421190,0x4211A0,
            opcode==0x05CB?uintptr_t(0x640320):uintptr_t(0x640370),0x421180,native_enter_car_abort,
            0x61A430,native_enter_car_create_next,native_enter_car_create_first,native_enter_car_control,
            native_enter_car_transition};
        static_assert(sizeof(expected)/sizeof(expected[0])==12,"Complete native entry vtable required");
        for(size_t slot=0;slot<entries.size();++slot)
            if(entries[slot]!=expected_entry(slot,opcode,expected[slot]))return false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

uintptr_t invoke_native_destructor(uintptr_t function,uintptr_t task,uint32_t flags) {
    return reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t,uint32_t)>(function)(task,flags);
}
uintptr_t __fastcall destroy(uintptr_t task,uintptr_t,uint32_t flags) {
    auto* lineage=embedded_owner(task);
    if(!lineage)return 0;
    const bool same_thread=callback_thread(lineage);
    if(same_thread)lineage->pause_children.erase(task);
    // Restore the genuine class vtable before native cleanup can re-enter a
    // virtual slot. Other clones retain this lineage until their own destructor.
    *reinterpret_cast<uintptr_t*>(task)=lineage->native_vtable;
    uintptr_t result=0;
    __try {
        result=invoke_native_destructor(lineage->original[0],task,flags);
    } __except(native_fault(lineage)) {}
    auto count=lineage->instances.load(std::memory_order_acquire);
    while(count && !lineage->instances.compare_exchange_weak(count,count-1,
            std::memory_order_acq_rel,std::memory_order_acquire)) {}
    if(count==1 && lineage->release_requested.load(std::memory_order_acquire) && callback_thread(lineage))
        (void)sa_release_actor_reference(&lineage->actor_cell);
    return result;
}
uintptr_t __fastcall clone(uintptr_t task,uintptr_t) {
    auto* lineage=embedded_owner(task);
    if(!lineage)return 0;
    const bool same_thread=callback_thread(lineage);
    uintptr_t copy=0;
    __try {
        copy=reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t)>(lineage->original[1])(task);
    } __except(native_fault(lineage)) {}
    if(!same_thread || !copy || !lineage->healthy.load(std::memory_order_acquire))return copy;
    __try {
        if(*reinterpret_cast<const uintptr_t*>(copy)!=lineage->native_vtable ||
           *reinterpret_cast<const uintptr_t*>(copy+4) ||
           *reinterpret_cast<const uintptr_t*>(copy+8)) {
            lineage->healthy.store(false,std::memory_order_release);
            return copy;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        lineage->healthy.store(false,std::memory_order_release);
        return copy;
    }
    const auto count=lineage->instances.load(std::memory_order_acquire);
    if(!count || count==UINT32_MAX) {
        lineage->healthy.store(false,std::memory_order_release);
        return copy;
    }
    lineage->instances.fetch_add(1,std::memory_order_acq_rel);
    *reinterpret_cast<uintptr_t*>(copy)=reinterpret_cast<uintptr_t>(lineage->table.entries.data());
    return copy;
}
uintptr_t __fastcall create_first_subtask(uintptr_t task,uintptr_t,uintptr_t ped) {
    auto* lineage=embedded_owner(task);
    if(!lineage)return 0;
    const bool same_thread=callback_thread(lineage);
    lineage->first_called.store(true,std::memory_order_release);
    if(lineage->cancel_requested.load(std::memory_order_acquire))return 0;
    if(lineage->release_requested.load(std::memory_order_acquire) && lineage->wait_requested.load(std::memory_order_acquire))return 0;
    if(same_thread && ped==lineage->actor.native_ped &&
       lineage->wait_requested.load(std::memory_order_acquire)) {
        const auto existing=lineage->pause_children.find(task);
        if(existing!=lineage->pause_children.end()) {
            if(task_pool_contains(existing->second))return existing->second;
            lineage->pause_children.erase(existing);
        }
        const auto paused=create_pause_child(*lineage,task);
        if(paused)return paused;
        lineage->healthy.store(false,std::memory_order_release);
        return 0;
    }
    uintptr_t result=0;
    __try {
        result=reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t,uintptr_t)>(
            lineage->original[9])(task,ped);
    } __except(native_fault(lineage)) {}
    return result;
}
uintptr_t __fastcall create_next_subtask(uintptr_t task,uintptr_t,uintptr_t ped) {
    auto* lineage=embedded_owner(task);
    if(!lineage)return 0;
    const bool same_thread=callback_thread(lineage);
    if(lineage->cancel_requested.load(std::memory_order_acquire) &&
       lineage->abort_accepted.load(std::memory_order_acquire))return 0;
    uintptr_t current=0;
    __try { current=*reinterpret_cast<const uintptr_t*>(task+8); }
    __except(native_fault(lineage)) { return 0; }
    if(same_thread && ped==lineage->actor.native_ped) {
        if(is_pause_child(*lineage,task,current)) {
            if(lineage->release_requested.load(std::memory_order_acquire))return 0;
            if(!lineage->wait_requested.load(std::memory_order_acquire))
                return restart_entry_subtask(*lineage,task,ped);
            const auto paused=create_pause_child(*lineage,task);
            if(paused)return paused;
            // CreateNext's old child is deleted by SetSubTask. Returning that
            // same pointer would reattach freed memory on pool exhaustion.
            lineage->healthy.store(false,std::memory_order_release);
            return 0;
        }
        if(lineage->wait_requested.load(std::memory_order_acquire)) {
            if(current && !valid_vehicle_approach(current)) {
                lineage->wait_requested.store(false,std::memory_order_release);
                goto native_next;
            }
            const auto paused=create_pause_child(*lineage,task);
            if(paused)return paused;
            lineage->healthy.store(false,std::memory_order_release);
            return 0;
        }
    }
native_next:;
    uintptr_t result=0;
    __try {
        result=reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t,uintptr_t)>(
            lineage->original[8])(task,ped);
    } __except(native_fault(lineage)) {}
    return result;
}
uintptr_t __fastcall control_subtask(uintptr_t task,uintptr_t,uintptr_t ped) {
    auto* lineage=embedded_owner(task);
    if(!lineage)return 0;
    const bool same_thread=callback_thread(lineage);
    if(same_thread && ped==lineage->actor.native_ped &&
       lineage->healthy.load(std::memory_order_acquire) &&
       lineage->cancel_requested.load(std::memory_order_acquire) &&
       lineage->abort_accepted.load(std::memory_order_acquire))return 0;
    uintptr_t current=0;
    if(same_thread && ped==lineage->actor.native_ped) {
        __try { current=*reinterpret_cast<const uintptr_t*>(task+8); }
        __except(native_fault(lineage)) { return 0; }
        if(is_pause_child(*lineage,task,current)) {
            if(lineage->release_requested.load(std::memory_order_acquire))return 0;
            if(lineage->wait_requested.load(std::memory_order_acquire))return current;
            return restart_entry_subtask(*lineage,task,ped);
        }
        if(lineage->wait_requested.load(std::memory_order_acquire)) {
            if(current && !valid_vehicle_approach(current)) {
                lineage->wait_requested.store(false,std::memory_order_release);
                goto native_control;
            }
            if(lineage->release_requested.load(std::memory_order_acquire)) {
                if(!current || invoke_subtask_abort(current,ped))return 0;
                goto native_control;
            }
            if(current && !invoke_subtask_abort(current,ped)) {
                uintptr_t unchanged=0;
                __try {
                    unchanged=reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t,uintptr_t)>(
                        lineage->original[10])(task,ped);
                } __except(native_fault(lineage)) {}
                return unchanged;
            }
            const auto paused=create_pause_child(*lineage,task);
            if(paused)return paused;
            lineage->healthy.store(false,std::memory_order_release);
            return 0;
        }
    }
native_control:;
    uintptr_t result=0;
    __try {
        result=reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t,uintptr_t)>(
            lineage->original[10])(task,ped);
    } __except(native_fault(lineage)) {}
    return result;
}

bool pin_module() {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
    return true;
#else
    HMODULE module=nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&sa_track_private_vehicle_entry),&module)!=FALSE;
#endif
}
void collect_released_lineages() {
    for(auto it=lineages.begin();it!=lineages.end();) {
        const auto& lineage=*it->second;
        if(lineage.release_requested.load(std::memory_order_acquire) && !lineage.instances.load(std::memory_order_acquire) &&
           !lineage.actor_cell)it=lineages.erase(it);
        else ++it;
    }
}
Lineage* find_lineage(ObjectiveActor actor,uint64_t epoch) {
    for(auto& [table,lineage]:lineages) {
        (void)table;
        if(lineage->epoch==epoch && lineage->actor==actor)return lineage.get();
    }
    return nullptr;
}
bool resolve_passenger_door(uintptr_t vehicle,int32_t seat,int32_t& door) {
    if(seat==-1){door=0;return true;} // Native timed wrapper's automatic-door selector.
    if(!profile(0x64F190,passenger_door_profile,sizeof(passenger_door_profile)))return false;
    __try {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        door=native_owned_vehicle_entry_passenger_door(vehicle,seat);
#else
        door=reinterpret_cast<int32_t(__cdecl*)(uintptr_t,int32_t)>(0x64F190)(vehicle,seat);
#endif
        return door==8 || door==9 || door==11;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool task_shape_matches(uintptr_t task,uintptr_t native_vtable,uint16_t opcode,
        uintptr_t vehicle,int32_t seat) {
    __try {
        if(*reinterpret_cast<const uintptr_t*>(task)!=native_vtable ||
           *reinterpret_cast<const uintptr_t*>(task+4) ||
           *reinterpret_cast<const uintptr_t*>(task+8) ||
           *reinterpret_cast<const uintptr_t*>(task+0x0C)!=vehicle)return false;
        const auto flags=*reinterpret_cast<const uint8_t*>(task+0x10);
        if(bool(flags&1)!=(opcode==0x05CB))return false;
        return opcode!=0x05CA || ((seat==0 || seat==8 || seat==9 || seat==11) &&
            *reinterpret_cast<const int32_t*>(task+0x1C)==seat);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool read_entry_target(uintptr_t task,uintptr_t& vehicle,int32_t& seat) {
    __try {
        vehicle=*reinterpret_cast<const uintptr_t*>(task+0x0C);
        seat=*reinterpret_cast<const int32_t*>(task+0x1C);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool actor_assignable(uintptr_t ped) {
    __try {
        return ped && *reinterpret_cast<const uintptr_t*>(ped+0x47C) &&
            !(*reinterpret_cast<const uint32_t*>(ped+0x474)&0x800000u);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
uintptr_t invoke_allocate_task(uint32_t bytes) {
    __try {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        return native_owned_vehicle_entry_allocate(bytes);
#else
        return reinterpret_cast<uintptr_t(__cdecl*)(uint32_t)>(
            native_entity_address(native_task_allocator))(bytes);
#endif
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
uintptr_t invoke_allocate_task() { return invoke_allocate_task(task_bytes); }
bool invoke_release_memory(uintptr_t memory) {
    __try {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        native_owned_vehicle_entry_release(memory);
#else
        reinterpret_cast<void(__cdecl*)(uintptr_t)>(
            native_entity_address(native_task_release))(memory);
#endif
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool invoke_constructor(uintptr_t task,uintptr_t vehicle,uint16_t opcode,int32_t seat,
        bool* entered) {
    __try {
        *entered=true;
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        if(opcode==0x05CB)native_owned_vehicle_entry_construct_driver(task,vehicle);
        else native_owned_vehicle_entry_construct_passenger(task,vehicle,seat,true);
#else
        if(opcode==0x05CB)
            reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t,uintptr_t)>(
                native_entity_address(native_driver_constructor))(task,vehicle);
        else
            reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t,uintptr_t,int32_t,bool)>(
                native_entity_address(native_passenger_constructor))(task,vehicle,seat,true);
#endif
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool invoke_assign(SaScript* script,int32_t actor,uintptr_t task,uint16_t opcode,
        bool* entered) {
    __try {
        *entered=true;
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        return native_owned_vehicle_entry_assign(script,actor,task,opcode);
#else
        reinterpret_cast<void(__thiscall*)(SaScript*,int32_t,uintptr_t,int32_t)>(
            native_entity_address(native_assign_script_task))(script,actor,task,opcode);
        return true;
#endif
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool invoke_untracked_discard(uintptr_t task,uint16_t opcode,
        const std::array<uintptr_t,12>& original) {
    __try {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        native_owned_vehicle_entry_discard(task,opcode);
#else
        invoke_native_destructor(original[0],task,1);
#endif
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool invoke_abort(uintptr_t task,uintptr_t ped,uintptr_t method) {
    __try {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        (void)method;
        return native_owned_vehicle_entry_abort(task,ped,abort_priority_urgent);
#else
        return reinterpret_cast<bool(__thiscall*)(uintptr_t,uintptr_t,int32_t,uintptr_t)>(
            method)(task,ped,abort_priority_urgent,0);
#endif
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool invoke_subtask_abort(uintptr_t subtask,uintptr_t ped) {
    __try {
        const auto table=*reinterpret_cast<const uintptr_t*>(subtask);
        if(!table)return false;
        const auto method=*reinterpret_cast<const uintptr_t*>(table+6*sizeof(uintptr_t));
        if(!method)return false;
        return reinterpret_cast<bool(__thiscall*)(uintptr_t,uintptr_t,int32_t,uintptr_t)>(
            method)(subtask,ped,abort_priority_urgent,0);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool invoke_standstill_constructor(uintptr_t task) {
    __try {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        native_owned_vehicle_entry_construct_standstill(task,1,false,false,8.0f);
#else
        reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t,int32_t,bool,bool,float)>(
            native_entity_address(native_standstill_constructor))(task,1,false,false,8.0f);
#endif
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool resolve_task_from_record(const ScriptTaskRecord& record,uint16_t opcode,
        uintptr_t ped,uintptr_t& task) {
    __try {
        if(record.opcode!=opcode || record.ped!=uint32_t(ped))return false;
        task=record.task;
        if(record.status==0 && record.event && !task) {
            if(record.event>UINTPTR_MAX-0x10 ||
               !profile(0x4B0A30,event_type_profile,sizeof(event_type_profile)) ||
               *reinterpret_cast<const uintptr_t*>(record.event)!=native_entity_address(native_script_event_type))
                return false;
            task=*reinterpret_cast<const uintptr_t*>(record.event+0x10);
        } else if(record.event || !task || (record.status!=1 && record.status!=2 &&
                  record.status!=4 && record.status!=5 && record.status!=6))return false;
        return task_pool_contains(task);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool resolve_binding_task(const CapturedScriptTask& binding,ObjectiveActor actor,
        uintptr_t& task) {
    if(binding.slot>=128 || binding.record.ped!=uint32_t(actor.native_ped) ||
       (binding.record.opcode!=0x05CA && binding.record.opcode!=0x05CB))return false;
    return resolve_task_from_record(binding.record,uint16_t(binding.record.opcode),
        actor.native_ped,task);
}
bool current_lineage_task(Lineage& lineage,uintptr_t& task) {
    const auto snapshot=sa_script_task_snapshot();
    if(!snapshot || lineage.bound_slot>=snapshot->size())return false;
    const auto& record=(*snapshot)[lineage.bound_slot];
    if(!resolve_task_from_record(record,lineage.opcode,lineage.actor.native_ped,task))return false;
    return private_shape(task,reinterpret_cast<uintptr_t>(lineage.table.entries.data()));
}
bool valid_subtask_type(uintptr_t subtask,int32_t expected) {
    __try {
        if(!task_pool_contains(subtask))return false;
        const auto table=*reinterpret_cast<const uintptr_t*>(subtask);
        if(!table)return false;
        const auto type=reinterpret_cast<int32_t(__thiscall*)(uintptr_t)>(
            *reinterpret_cast<const uintptr_t*>(table+4*sizeof(uintptr_t)))(subtask);
        return type==expected;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool subtask_table_matches(uintptr_t table,int32_t type) {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
    return native_owned_vehicle_entry_subtask_table_matches(table,type);
#else
    const uintptr_t expected=sa_owned_entry_subtask_vtable(type);
    return expected && table==native_entity_address(expected);
#endif
}
bool valid_vehicle_approach(uintptr_t subtask) {
    return valid_subtask_type(subtask,approach_task_type);
}
bool valid_standstill_task(uintptr_t subtask) {
    __try {
        if(!task_pool_contains(subtask))return false;
        const auto table=*reinterpret_cast<const uintptr_t*>(subtask);
        return subtask_table_matches(table,standstill_task_type);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool is_pause_child(Lineage& lineage,uintptr_t root_task,uintptr_t subtask) {
    if(!subtask)return false;
    const auto found=lineage.pause_children.find(root_task);
    return found!=lineage.pause_children.end() && found->second==subtask &&
        task_pool_contains(subtask) && valid_standstill_task(subtask);
}
uintptr_t create_pause_child(Lineage& lineage,uintptr_t root_task) {
    if(!lineage.wait_requested.load(std::memory_order_acquire) ||
       !callback_thread(&lineage) ||
       !private_shape(root_task,reinterpret_cast<uintptr_t>(lineage.table.entries.data())) ||
       !task_pool_contains(root_task) || !entry_profiles_match(lineage.opcode) ||
       !wait_profiles_match())return 0;
    const auto child=invoke_allocate_task(standstill_task_bytes);
    if(!child)return 0;
    if(!invoke_standstill_constructor(child) || !valid_standstill_task(child)) {
        lineage.healthy.store(false,std::memory_order_release);
        // The constructor profile and allocator are pinned, and this simple
        // task owns no external resource; return its pool slot if its shape is
        // not what the reviewed constructor produced.
        (void)invoke_release_memory(child);
        return 0;
    }
    try {
        lineage.pause_children.insert_or_assign(root_task,child);
    } catch(...) {
        lineage.healthy.store(false,std::memory_order_release);
        (void)invoke_release_memory(child);
        return 0;
    }
    return child;
}
uintptr_t restart_entry_subtask(Lineage& lineage,uintptr_t root_task,uintptr_t ped) {
    if(!callback_thread(&lineage) || ped!=lineage.actor.native_ped ||
       !private_shape(root_task,reinterpret_cast<uintptr_t>(lineage.table.entries.data())) ||
       !task_pool_contains(root_task))return 0;
    const auto found=lineage.pause_children.find(root_task);
    if(found==lineage.pause_children.end())return 0;
    lineage.pause_children.erase(found);
    lineage.first_called.store(true,std::memory_order_release);
    uintptr_t result=0;
    __try {
        result=reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t,uintptr_t)>(
            lineage.original[9])(root_task,ped);
    } __except(native_fault(&lineage)) {}
    return result;
}

uint64_t track_private_task(uintptr_t task,ObjectiveActor actor,uint16_t opcode) {
    if(!on_registry_thread() || !task || actor.handle<0 || !actor.native_ped ||
       (opcode!=0x05CA && opcode!=0x05CB) || !entry_profiles_match(opcode))return 0;
    const auto live=sa_live_objective_actor(actor.handle);
    if(!live || !(*live==actor))return 0;
    collect_released_lineages();
    uintptr_t native_vtable=0,prefix=0;
    std::array<uintptr_t,12> original{};
    if(!read_native_table(task,opcode,native_vtable,original,prefix) ||
       !task_pool_contains(task))return 0;
    uintptr_t vehicle=0;
    int32_t seat=-1;
    if(!read_entry_target(task,vehicle,seat))return 0;
    if(!vehicle || !task_shape_matches(task,native_vtable,opcode,vehicle,seat) || !pin_module())return 0;
    std::unique_ptr<Lineage> lineage;
    try { lineage=std::make_unique<Lineage>(); }
    catch(...) { return 0; }
    lineage->native_vtable=native_vtable;
    lineage->original=original;
    lineage->actor=actor;
    lineage->opcode=opcode;
    lineage->thread=GetCurrentThreadId();
    lineage->table.owner=lineage.get();
    lineage->table.prefix=prefix;
    lineage->table.entries=original;
    lineage->table.entries[0]=reinterpret_cast<uintptr_t>(&destroy);
    lineage->table.entries[1]=reinterpret_cast<uintptr_t>(&clone);
    lineage->table.entries[8]=reinterpret_cast<uintptr_t>(&create_next_subtask);
    lineage->table.entries[9]=reinterpret_cast<uintptr_t>(&create_first_subtask);
    lineage->table.entries[10]=reinterpret_cast<uintptr_t>(&control_subtask);
    if(serial==UINT64_MAX)return 0;
    lineage->epoch=++serial;
    const auto table=reinterpret_cast<uintptr_t>(lineage->table.entries.data());
    const auto epoch=lineage->epoch;
    try {
        auto inserted=lineages.emplace(table,std::move(lineage));
        if(!inserted.second)return 0;
    } catch(...) {
        return 0;
    }
    auto& published=*lineages.at(table);
    if(!sa_register_actor_reference(actor,&published.actor_cell)) {
        // RegisterReference may have stored this cell before failing. Keep its
        // address stable until native cleanup proves the reference is gone.
        published.release_requested.store(true,std::memory_order_release);
        published.instances.store(0,std::memory_order_release);
        if(sa_release_actor_reference(&published.actor_cell))collect_released_lineages();
        return 0;
    }
    *reinterpret_cast<uintptr_t*>(task)=table; // publish only after all allocations and pins
    return epoch;
}

} // namespace

uint64_t sa_track_private_vehicle_entry(uintptr_t task,ObjectiveActor actor,
        uint16_t opcode) {
    return track_private_task(task,actor,opcode);
}

bool sa_discard_private_vehicle_entry(uintptr_t task,ObjectiveActor actor,
        uint16_t opcode) {
    // Ownership is proven by the task itself (our private table or the reviewed unassigned native shape),
    // never by the actor still being alive: losing the actor is exactly why a private task is discarded.
    if(!on_registry_thread() || !task || actor.handle<0 || !actor.native_ped ||
       (opcode!=0x05CA && opcode!=0x05CB) || !entry_profiles_match(opcode) ||
       !task_pool_contains(task))return false;
    if(auto* lineage=registered_owner(task)) {
        if(!(lineage->actor==actor) || lineage->opcode!=opcode ||
           lineage->instances.load(std::memory_order_acquire)!=1 ||
           lineage->first_called.load(std::memory_order_acquire) ||
           !private_shape(task,reinterpret_cast<uintptr_t>(lineage->table.entries.data())))return false;
        __try {
            if(*reinterpret_cast<const uintptr_t*>(task+4) ||
               *reinterpret_cast<const uintptr_t*>(task+8))return false;
        } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
        // A receipt bound to this lineage means SA already owns an assigned task (a retired bank record may
        // alias a recycled pool address, so the bank itself is not consulted for our own table).
        if(lineage->accepted_task || lineage->binding_bound)return false;
        lineage->release_requested.store(true,std::memory_order_release);
        destroy(task,0,1);
        collect_released_lineages();
        return true;
    }
    uintptr_t native_vtable=0,prefix=0;
    std::array<uintptr_t,12> original{};
    if(!read_native_table(task,opcode,native_vtable,original,prefix))return false;
    __try {
        if(*reinterpret_cast<const uintptr_t*>(task+4) ||
           *reinterpret_cast<const uintptr_t*>(task+8))return false;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    // Callers pass only a task they allocated and constructed in the same command, before assignment; a retired
    // bank record may alias a recycled pool address, so the bank cannot prove assignment for a native table.
    return invoke_untracked_discard(task,opcode,original);
}

std::optional<uint64_t> sa_owned_vehicle_entry_epoch(
        const CapturedScriptTask& binding,ObjectiveActor actor) {
    if(!on_registry_thread() || actor.handle<0 || !actor.native_ped)return {};
    const auto live=sa_live_objective_actor(actor.handle);
    if(!live || !(*live==actor))return {};
    uintptr_t task=0;
    if(!resolve_binding_task(binding,actor,task))return {};
    uintptr_t table=0;
    if(!read_task_vtable(task,table))return {};
    for(auto& [table_key,lineage_ptr]:lineages) {
        auto& lineage=*lineage_ptr;
        if(table_key!=table || !(lineage.actor==actor) ||
           lineage.opcode!=uint16_t(binding.record.opcode) ||
           !lineage.healthy.load(std::memory_order_acquire) ||
           !lineage.instances.load(std::memory_order_acquire) ||
           !sa_actor_reference_live(actor,&lineage.actor_cell))continue;
        if(!lineage.accepted_task)lineage.accepted_task=task;
        if(lineage.accepted_task!=task)return {};
        if(lineage.binding_bound && lineage.bound_slot!=binding.slot)return {};
        lineage.binding=binding;
        lineage.bound_slot=binding.slot;
        lineage.binding_bound=true;
        return lineage.epoch;
    }
    return {};
}

namespace {
bool read_owned_entry_child(const Lineage& lineage,uintptr_t task,uintptr_t* child) {
    __try {
        if(!child || !private_shape(task,reinterpret_cast<uintptr_t>(lineage.table.entries.data())) || !task_pool_contains(task))return false;
        *child=*reinterpret_cast<const uintptr_t*>(task+8);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
}
std::optional<bool> sa_set_owned_vehicle_entry_wait(ObjectiveActor actor,
        uint64_t epoch,bool waiting) {
    if(!on_registry_thread() || !epoch || actor.handle<0 || !actor.native_ped)return {};
    auto* lineage=find_lineage(actor,epoch);
    if(!lineage || lineage->thread!=GetCurrentThreadId() ||
       lineage->release_requested.load(std::memory_order_acquire) ||
       !lineage->binding_bound || !lineage->healthy.load(std::memory_order_acquire) ||
       !lineage->instances.load(std::memory_order_acquire) ||
       !sa_actor_reference_live(actor,&lineage->actor_cell))return {};
    if(!entry_profiles_match(lineage->opcode))return {};
    const auto live=sa_live_objective_actor(actor.handle);
    if(!live || !(*live==actor))return {};
    if(lineage->cancel_requested.load(std::memory_order_acquire) ||
       lineage->abort_accepted.load(std::memory_order_acquire))return false;
    uintptr_t task=0;
    if(!current_lineage_task(*lineage,task))return {};
    uintptr_t subtask=0;
    if(!read_owned_entry_child(*lineage,task,&subtask))return {};
    const bool paused_child=is_pause_child(*lineage,task,subtask);
    if(waiting) {
        if(!wait_profiles_match())return {};
        if(subtask && !paused_child && !valid_vehicle_approach(subtask)) {
            lineage->wait_requested.store(false,std::memory_order_release);
            return false; // Native boarding/door work already owns this phase.
        }
        try {
            lineage->pause_children.reserve(std::max<size_t>(lineage->pause_children.size()+1,
                lineage->instances.load(std::memory_order_acquire)));
        } catch(...) { return false; }
    } else if(paused_child && !wait_profiles_match())return {};
    lineage->wait_requested.store(waiting,std::memory_order_release);
    return true;
}

std::optional<bool> sa_cancel_owned_vehicle_entry(ObjectiveActor actor,
        uint64_t epoch) {
    if(!on_registry_thread() || !epoch || actor.handle<0 || !actor.native_ped)return {};
    auto* lineage=find_lineage(actor,epoch);
    if(!lineage || lineage->release_requested.load(std::memory_order_acquire) || !lineage->binding_bound ||
       !lineage->healthy.load(std::memory_order_acquire) ||
       !sa_actor_reference_live(actor,&lineage->actor_cell))return {};
    const auto instances=lineage->instances.load(std::memory_order_acquire);
    if(!instances)return true;

    // A missing/currently different bank entry is never authority to abort a
    // replacement task. Wait for this exact private task and its clones to die.
    uintptr_t task=0;
    if(!current_lineage_task(*lineage,task))return false;
    const auto pose=sa_route_actor_vehicle_snapshot(actor);
    if(!pose || !(pose->actor==actor))return {};
    if(pose->in_vehicle) {
        // Occupancy transfers control back to SA. A queued pre-start request
        // must not suppress the native close-door/drive sequence.
        if(!lineage->abort_accepted.load(std::memory_order_acquire))
            lineage->cancel_requested.store(false,std::memory_order_release);
        lineage->wait_requested.store(false,std::memory_order_release);
        return false;
    }

    uintptr_t subtask=0;
    __try {
        if(!private_shape(task,reinterpret_cast<uintptr_t>(lineage->table.entries.data())) ||
           !task_pool_contains(task))return {};
        subtask=*reinterpret_cast<const uintptr_t*>(task+8);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return {}; }
    if(!subtask) {
        // A clone may be queued even after another clone has started, so the
        // shared first-called bit is not evidence about this instance. A null
        // root child is the only state where cancelling before CreateFirst is safe.
        lineage->cancel_requested.store(true,std::memory_order_release);
        lineage->wait_requested.store(false,std::memory_order_release);
        return false;
    }
    if(is_pause_child(*lineage,task,subtask)) {
        if(lineage->abort_accepted.load(std::memory_order_acquire))return false;
        bool accepted=false;
        __try {
            accepted=invoke_abort(task,actor.native_ped,lineage->original[6]);
        } __except(native_fault(lineage)) { return {}; }
        if(!accepted)return false;
        lineage->wait_requested.store(false,std::memory_order_release);
        lineage->cancel_requested.store(true,std::memory_order_release);
        lineage->abort_accepted.store(true,std::memory_order_release);
        return false;
    }
    if(!valid_vehicle_approach(subtask))return false;
    if(lineage->abort_accepted.load(std::memory_order_acquire))return false;
    bool accepted=false;
    __try {
        accepted=invoke_abort(task,actor.native_ped,lineage->original[6]);
    } __except(native_fault(lineage)) { return {}; }
    if(!accepted)return false;
    lineage->cancel_requested.store(true,std::memory_order_release);
    lineage->abort_accepted.store(true,std::memory_order_release);
    lineage->wait_requested.store(false,std::memory_order_release);
    // The service confirms only when the native task and every wrapped clone
    // retire; this return merely records SA's accepted urgent abort request.
    return false;
}

void sa_release_owned_vehicle_entry(ObjectiveActor actor,uint64_t epoch) {
    if(!on_registry_thread() || !epoch)return;
    auto* lineage=find_lineage(actor,epoch);
    if(!lineage)return;
    lineage->release_requested.store(true,std::memory_order_release);
    lineage->cancel_requested.store(false,std::memory_order_release);
    if(!lineage->instances.load(std::memory_order_acquire))
        (void)sa_release_actor_reference(&lineage->actor_cell);
    collect_released_lineages();
}

bool sa_owned_vehicle_entry_wait_ready() {
    return sa_executor_profile_matches() && entry_profiles_match(0x05CA) &&
        entry_profiles_match(0x05CB) && wait_profiles_match();
}

std::optional<bool> sa_owned_vehicle_entry_active(ObjectiveActor actor,uint64_t epoch) {
    if(!on_registry_thread() || !epoch)return {};
    const auto* lineage=find_lineage(actor,epoch);
    if(!lineage)return false;
    return lineage->actor_cell!=0 && lineage->instances.load(std::memory_order_acquire)!=0 &&
        !lineage->release_requested.load(std::memory_order_acquire);
}

NativeResult sa_create_owned_vehicle_entry(const NativeCall& call) {
    NativeResult result;
    const auto empty_packet=[&] {
        return !call.outputs && !call.predicate && !call.text && !call.text_position &&
            !call.replacement_text && !call.mission_flag_reference &&
            !call.suppress_previous_brief && !call.source_wander_direction &&
            !call.source_wait_in_car && !call.source_entity_command &&
            !call.source_statistic && !call.source_frenzy_headshots &&
            !call.source_environment && !call.source_garage && !call.source_traffic;
    };
    const size_t input_count=call.opcode==0x05CB?3u:(call.opcode==0x05CA?4u:0u);
    if(!input_count || call.inputs.size()!=input_count || !empty_packet()) {
        result.diagnostic="Owned vehicle entry requires an untimed 05CA/05CB packet with no outputs";
        return result;
    }
    if(call.inputs[2]!=-1 || (call.opcode==0x05CA &&
       (call.inputs[3]<-1 || call.inputs[3]>7))) {
        result.diagnostic="Owned vehicle entry requires the reviewed untimed marker and passenger seat";
        return result;
    }
    if(!on_registry_thread()) {
        result.diagnostic="Owned vehicle entry refused: registry thread mismatch (current="+
            std::to_string(GetCurrentThreadId())+", owner="+
            std::to_string(registry_thread.load(std::memory_order_acquire))+")";
        return result;
    }
    if(!sa_executor_profile_matches()) {
        result.diagnostic="Owned vehicle entry refused: SA executor profile mismatch";
        return result;
    }
    const char* profile_mismatch=nullptr;
    if(!entry_profiles_match(call.opcode,&profile_mismatch)) {
        result.diagnostic=std::string("Owned vehicle entry refused: native core profile mismatch at ")+
            (profile_mismatch?profile_mismatch:"unknown entry profile");
        return result;
    }
    const auto actor=sa_live_objective_actor(call.inputs[0]);
    const auto vehicle=sa_route_vehicle_snapshot(call.inputs[1]);
    if(!actor || !vehicle || !vehicle->entity || !actor_assignable(actor->native_ped)) {
        result.diagnostic="Owned vehicle entry requires a live script-assignable ped and vehicle generation";
        return result;
    }

    int32_t passenger_door=0;
    if(call.opcode==0x05CA && !resolve_passenger_door(vehicle->entity,call.inputs[3],passenger_door)) {
        result.diagnostic="Native passenger seat has no valid SA entry door";
        return result;
    }
    const auto memory=invoke_allocate_task();
    if(!memory) {
        result.diagnostic="SA task allocator refused the exact 0x50-byte entry task allocation";
        return result;
    }
    const auto current_actor=sa_live_objective_actor(actor->handle);
    const auto current_vehicle=sa_route_vehicle_snapshot(vehicle->handle);
    if(!sa_executor_profile_matches() || !entry_profiles_match(call.opcode) ||
       !current_actor || !(*current_actor==*actor) || !current_vehicle ||
       current_vehicle->entity!=vehicle->entity ||
       current_vehicle->generation!=vehicle->generation ||
       !actor_assignable(actor->native_ped)) {
        if(!invoke_release_memory(memory))result.native_entered=true;
        result.diagnostic="Actor, vehicle, profile, or script-task assignment changed before native construction";
        return result;
    }

    int32_t current_door=0;
    if(call.opcode==0x05CA && (!resolve_passenger_door(current_vehicle->entity,call.inputs[3],current_door) || current_door!=passenger_door)) {
        if(!invoke_release_memory(memory))result.native_entered=true;
        result.diagnostic="Native passenger door selection changed before task construction";
        return result;
    }
    bool constructor_entered=false;
    if(!invoke_constructor(memory,vehicle->entity,call.opcode,
            call.opcode==0x05CA?passenger_door:-1,&constructor_entered)) {
        result.native_entered=constructor_entered;
        if(!constructor_entered && !invoke_release_memory(memory))result.native_entered=true;
        result.diagnostic="Native CTaskComplexEnterCar constructor entered and faulted";
        return result;
    }
    uintptr_t native_vtable=0,prefix=0;
    std::array<uintptr_t,12> original{};
    if(!read_native_table(memory,call.opcode,native_vtable,original,prefix) ||
       !task_shape_matches(memory,native_vtable,call.opcode,vehicle->entity,
            call.opcode==0x05CA?passenger_door:-1)) {
        result.native_entered=true;
        result.diagnostic="SA constructor produced an unexpected entry-task vtable, actor target, or seat";
        return result;
    }
    auto epoch=sa_track_private_vehicle_entry(memory,*actor,call.opcode);
    if(!epoch) {
        result.native_entered=true;
        (void)sa_discard_private_vehicle_entry(memory,*actor,call.opcode);
        result.diagnostic="Native entry task was constructed but private lifetime ownership could not be installed";
        return result;
    }
    const auto before=sa_script_task_snapshot();
    if(!before) {
        result.task_capture=NativeResult::TaskCapture::unavailable_before;
        (void)sa_discard_private_vehicle_entry(memory,*actor,call.opcode);
        sa_release_owned_vehicle_entry(*actor,epoch);
        result.diagnostic="Native entry task was constructed but the script-task baseline is unavailable";
        return result;
    }

    const auto actor_before_assign=sa_live_objective_actor(actor->handle);
    const auto vehicle_before_assign=sa_route_vehicle_snapshot(vehicle->handle);
    if(!sa_executor_profile_matches() || !entry_profiles_match(call.opcode) ||
       !actor_before_assign || !(*actor_before_assign==*actor) ||
       !vehicle_before_assign || vehicle_before_assign->entity!=vehicle->entity ||
       vehicle_before_assign->generation!=vehicle->generation ||
       !actor_assignable(actor->native_ped)) {
        (void)sa_discard_private_vehicle_entry(memory,*actor,call.opcode);
        sa_release_owned_vehicle_entry(*actor,epoch);
        result.diagnostic="Actor, vehicle, profile, or entry assignment changed after the bank baseline";
        return result;
    }

    SaScript script(nullptr,call.mission_context); // GivePedScriptedTask's native C6 cleanup path
    bool assignment_entered=false;
    const bool assigned=invoke_assign(&script,actor->handle,memory,call.opcode,
        &assignment_entered);
    result.native_entered=assignment_entered;
    result.executed=assigned;
    const auto after=sa_script_task_snapshot();
    if(!after) {
        result.task_capture=NativeResult::TaskCapture::unavailable_after;
        sa_release_owned_vehicle_entry(*actor,epoch);
        result.diagnostic=assigned?
            "Native entry task was assigned but its script-task bank receipt is unavailable":
            "GivePedScriptedTask entered SA and faulted before a readable bank receipt";
        return result;
    }
    const auto final_actor=sa_live_objective_actor(actor->handle);
    const auto final_vehicle=sa_route_vehicle_snapshot(vehicle->handle);
    if(!final_actor || !(*final_actor==*actor) || !final_vehicle ||
       final_vehicle->entity!=vehicle->entity || final_vehicle->generation!=vehicle->generation) {
        result.task_capture=NativeResult::TaskCapture::actor_changed;
        sa_release_owned_vehicle_entry(*actor,epoch);
        result.diagnostic="Entry task entered SA but actor or target generation changed during assignment";
        return result;
    }
    result.accepted_script_task=capture_accepted_script_task(*before,*after,
        uint32_t(actor->native_ped),call.opcode);
    result.task_capture=result.accepted_script_task?NativeResult::TaskCapture::captured:
        NativeResult::TaskCapture::no_unique_record;
    if(result.accepted_script_task) {
        const auto accepted_epoch=sa_owned_vehicle_entry_epoch(*result.accepted_script_task,*actor);
        if(!accepted_epoch || *accepted_epoch!=epoch) {
            result.accepted_script_task.reset();
            result.task_capture=NativeResult::TaskCapture::no_unique_record;
            sa_release_owned_vehicle_entry(*actor,epoch);
        }
    } else {
        sa_release_owned_vehicle_entry(*actor,epoch);
    }
    if(!assigned) {
        // The caller sees a failed command and never adopts this epoch; the task itself stays with SA.
        sa_release_owned_vehicle_entry(*actor,epoch);
        result.diagnostic="GivePedScriptedTask entered SA and faulted; no retry was attempted";
    }
    return result;
}

} // namespace lcs
