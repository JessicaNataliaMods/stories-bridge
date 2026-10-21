#include <windows.h>
#include "lcs/owned_route_task.hpp"
#include "lcs/native_memory.hpp"
#include "lcs/sa_task_memory.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstddef>
#include <memory>
#include <unordered_map>
#include "lcs/sa_executor.hpp"
#include "lcs/route_vehicle_native.hpp"

namespace lcs {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
void native_owned_route_retarget(uintptr_t task,const std::array<float,3>& target,
        float radius,float move_state_radius,bool force_update);
#endif
namespace {
constexpr uintptr_t native_table=0x86FDA8;
struct Lineage;
struct Table {
    Lineage* owner=nullptr;
    uintptr_t prefix=0;
    std::array<uintptr_t,11> entries{};
};
static_assert(offsetof(Table,entries)==2*sizeof(uintptr_t),"Owned vtable header layout");
struct Lineage {
    Table table;
    std::array<uintptr_t,11> original{};
    ObjectiveActor actor;
    OwnedRouteControlCallback control=nullptr;
    void* control_context=nullptr;
    uint64_t epoch=0;
    DWORD thread=0;
    std::atomic<uint32_t> instances{1};
    std::atomic<uint32_t> control_depth{0};
    std::atomic<bool> healthy{true};
};
// Registry is touched ONLY by the construction/inspection game thread.
// Callbacks use their own embedded lineage, never this container. Entries with
// outstanding native objects outlive the controller and cannot be freed/reset.
std::unordered_map<uintptr_t,std::unique_ptr<Lineage>> lineages;
std::atomic<DWORD> registry_thread{0};
uint64_t serial=0;
Lineage* owner(uintptr_t task) {
    const auto table=*reinterpret_cast<const uintptr_t*>(task);
    return *reinterpret_cast<Lineage*const*>(table-2*sizeof(uintptr_t));
}
bool callback_thread(Lineage* lineage) {
    if(GetCurrentThreadId()==lineage->thread)return true;
    lineage->healthy.store(false,std::memory_order_release);return false;
}
int native_fault(Lineage* lineage) {
    lineage->healthy.store(false,std::memory_order_release);
    return EXCEPTION_CONTINUE_SEARCH;
}
uintptr_t __fastcall destroy(uintptr_t task,uintptr_t,uint32_t flags) {
    auto* lineage=owner(task);
    callback_thread(lineage);
    // Reentrant observation must see this instance retired BEFORE the native
    // destructor frees/reuses it. Do not invalidate surviving native clones.
    *reinterpret_cast<uintptr_t*>(task)=native_table;
    uintptr_t result=0;
    __try {
        result=reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t,uint32_t)>(lineage->original[0])(task,flags);
    } __except(native_fault(lineage)) {
        // Native exception semantics are retained; uncertain lineage is never
        // collected or accepted again.
    }
    lineage->instances.fetch_sub(1,std::memory_order_acq_rel);
    return result;
}
uintptr_t __fastcall clone(uintptr_t task,uintptr_t) {
    auto* lineage=owner(task);
    const bool same_thread=callback_thread(lineage);
    uintptr_t copy=0;
    __try {copy=reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t)>(lineage->original[1])(task);}
    __except(native_fault(lineage)) {}
    if(!same_thread || !copy || !lineage->healthy.load(std::memory_order_acquire))return copy;
    if(*reinterpret_cast<const uintptr_t*>(copy)!=native_table ||
       *reinterpret_cast<const uintptr_t*>(copy+4) || *reinterpret_cast<const uintptr_t*>(copy+8)) {
        lineage->healthy.store(false,std::memory_order_release);return copy;
    }
    if(lineage->instances.load(std::memory_order_acquire)==UINT32_MAX) {
        lineage->healthy.store(false,std::memory_order_release);return copy;
    }
    lineage->instances.fetch_add(1,std::memory_order_acq_rel);
    *reinterpret_cast<uintptr_t*>(copy)=reinterpret_cast<uintptr_t>(lineage->table.entries.data());
    return copy;
}
void invoke_control(Lineage* lineage,uintptr_t task,uintptr_t ped) {
    if(!lineage->healthy.load(std::memory_order_acquire) || !lineage->control || ped!=lineage->actor.native_ped)return;
    try {lineage->control(lineage->control_context,lineage->actor,task,ped);}
    catch(...) {lineage->healthy.store(false,std::memory_order_release);lineage->control=nullptr;lineage->control_context=nullptr;}
}
uintptr_t __fastcall control_sub_task(uintptr_t task,uintptr_t,uintptr_t ped) {
    auto* lineage=owner(task);
    const auto epoch=sa_owned_route_task_epoch(task,lineage->actor);
    if(callback_thread(lineage) && epoch && *epoch==lineage->epoch) {
        lineage->control_depth.fetch_add(1,std::memory_order_acq_rel);
        __try {invoke_control(lineage,task,ped);}
        __except(native_fault(lineage)) {lineage->control=nullptr;lineage->control_context=nullptr;}
        lineage->control_depth.fetch_sub(1,std::memory_order_acq_rel);
    }
    uintptr_t result=0;
    __try {result=reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t,uintptr_t)>(lineage->original[10])(task,ped);}
    __except(native_fault(lineage)) {}
    return result;
}
bool pin_module() {
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
    return true; // executable containing callbacks lives for this whole test
#else
    HMODULE module=nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&sa_track_private_route_task),&module)!=FALSE;
#endif
}
bool private_shape(uintptr_t task) {
    __try {
        return task && *reinterpret_cast<const uintptr_t*>(task)==native_table &&
            !*reinterpret_cast<const uintptr_t*>(task+4) && !*reinterpret_cast<const uintptr_t*>(task+8);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool read_table(std::array<uintptr_t,11>& entries,uintptr_t& prefix) {
    __try {
        const auto* table=reinterpret_cast<const uintptr_t*>(native_entity_address(native_table));
        for(size_t n=0;n<entries.size();++n)entries[n]=table[n];
        prefix=table[-1];
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        return entries[0] && entries[1];
#else
        // Preserve every other virtual slot and the word preceding the table. A mod
        // replacing destruction/clone owns that ABI; do not wrap its objects.
        const uint8_t clone_prefix[]={0x6A,0xFF,0x68,0xAB,0x53,0x84,0,0x64,0xA1,0,0,0,0,0x50,0x64,0x89,0x25,0,0,0,0};
        return entries[0]==0x66DB80 && entries[1]==0x66CEA0 && entries[10]==0x668570 &&
            !std::memcmp(reinterpret_cast<const void*>(0x66CEA0),clone_prefix,sizeof(clone_prefix));
#endif
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool task_table(uintptr_t task,uintptr_t& table) {
    __try {if(!task)return false;table=*reinterpret_cast<const uintptr_t*>(task);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool resolve_binding_task(const CapturedScriptTask& binding,ObjectiveActor actor,uintptr_t& task) {
    __try {
        const auto& record=binding.record;
        if(binding.slot>=128 || record.ped!=actor.native_ped || record.opcode!=0x05D3)return false;
        task=record.task;
        if(record.status==0 && record.event && !task) {
            const uint8_t event_type[]={0xB8,0x20,0,0,0,0xC3};
            if(record.event>UINTPTR_MAX-0x10 ||
               std::memcmp(reinterpret_cast<const void*>(native_entity_address(0x4B0A30)),event_type,sizeof(event_type)) ||
               *reinterpret_cast<const uintptr_t*>(record.event)!=0x85B378)return false;
            task=*reinterpret_cast<const uintptr_t*>(record.event+0x10);
        } else if(record.event || !task || (record.status!=1 && record.status!=2 && record.status!=4 && record.status!=5 && record.status!=6))return false;
        return sa_task_slot_live(task);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
Lineage* find_lineage(ObjectiveActor actor,uint64_t epoch) {
    for(auto& [table,lineage]:lineages) {
        (void)table;
        if(lineage->actor==actor && lineage->epoch==epoch)return lineage.get();
    }
    return nullptr;
}
} // namespace
uint64_t sa_track_private_route_task(uintptr_t task,ObjectiveActor actor) {
    const auto thread=GetCurrentThreadId();
    DWORD first=0;
    registry_thread.compare_exchange_strong(first,thread,std::memory_order_acq_rel);
    if(registry_thread.load(std::memory_order_acquire)!=thread || !private_shape(task) || actor.handle<0 || !actor.native_ped || serial==UINT64_MAX)return 0;
    for(auto it=lineages.begin();it!=lineages.end();) {
        if(!it->second->instances.load(std::memory_order_acquire) && it->second->healthy.load(std::memory_order_acquire))it=lineages.erase(it);
        else ++it;
    }
    auto lineage=std::make_unique<Lineage>();
    if(!read_table(lineage->original,lineage->table.prefix) || !pin_module())return 0;
    lineage->actor=actor;lineage->thread=thread;lineage->epoch=++serial;
    lineage->table.owner=lineage.get();lineage->table.entries=lineage->original;
    lineage->table.entries[0]=reinterpret_cast<uintptr_t>(&destroy);
    lineage->table.entries[1]=reinterpret_cast<uintptr_t>(&clone);
    lineage->table.entries[10]=reinterpret_cast<uintptr_t>(&control_sub_task);
    const auto table=reinterpret_cast<uintptr_t>(lineage->table.entries.data());
    const auto epoch=lineage->epoch;
    // Finish all allocations before publishing the new PRIVATE object vptr.
    lineages.emplace(table,std::move(lineage));
    *reinterpret_cast<uintptr_t*>(task)=table;
    return epoch;
}
std::optional<uint64_t> sa_owned_route_task_epoch(uintptr_t task,ObjectiveActor actor) {
    if(GetCurrentThreadId()!=registry_thread.load(std::memory_order_acquire))return {};
    uintptr_t table=0;if(!task_table(task,table))return {};
    const auto found=lineages.find(table);
    if(found==lineages.end())return {};
    const auto& lineage=*found->second;
    if(!(lineage.actor==actor) || !lineage.instances.load(std::memory_order_acquire) || !lineage.healthy.load(std::memory_order_acquire) ||
       lineage.table.entries[0]!=reinterpret_cast<uintptr_t>(&destroy) || lineage.table.entries[1]!=reinterpret_cast<uintptr_t>(&clone) ||
       lineage.table.entries[10]!=reinterpret_cast<uintptr_t>(&control_sub_task))return {};
    return lineage.epoch;
}
std::optional<std::array<float,3>> sa_owned_route_target(uintptr_t task,ObjectiveActor actor,uint64_t epoch) {
    if(GetCurrentThreadId()!=registry_thread.load(std::memory_order_acquire) || !task || !epoch)return {};
    const auto live=sa_owned_route_task_epoch(task,actor);
    if(!live || *live!=epoch)return {};
    auto* lineage=find_lineage(actor,epoch);if(!lineage)return {};
    float coordinates[3]{};
    __try {
        if(*reinterpret_cast<const uintptr_t*>(task)!=reinterpret_cast<uintptr_t>(lineage->table.entries.data()) ||
           !*reinterpret_cast<const uintptr_t*>(task+8) ||
           *reinterpret_cast<const float*>(task+0x1C)!=0.8f)return {};
        std::memcpy(coordinates,reinterpret_cast<const void*>(task+0x10),sizeof(coordinates));
    } __except(EXCEPTION_EXECUTE_HANDLER){return {};}
    for(const float value:coordinates)if(!std::isfinite(value))return {};
    const auto after=sa_owned_route_task_epoch(task,actor);
    if(!after || *after!=epoch)return {};
    return std::array<float,3>{coordinates[0],coordinates[1],coordinates[2]};
}
bool sa_discard_private_route_task(uintptr_t task,ObjectiveActor actor,uint64_t epoch) {
    if(!epoch || sa_owned_route_task_epoch(task,actor)!=epoch)return false;
    __try {
        if(*reinterpret_cast<const uintptr_t*>(task+4) || *reinterpret_cast<const uintptr_t*>(task+8))return false;
        destroy(task,0,1);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
std::optional<uint64_t> sa_owned_route_binding_epoch(const CapturedScriptTask& binding,ObjectiveActor actor) {
    uintptr_t task=0;if(!resolve_binding_task(binding,actor,task))return {};
    return sa_owned_route_task_epoch(task,actor);
}
bool sa_bind_owned_route_control(const CapturedScriptTask& binding,ObjectiveActor actor,uint64_t epoch,
        OwnedRouteControlCallback callback,void* context) {
    if(GetCurrentThreadId()!=registry_thread.load(std::memory_order_acquire) || !epoch || !callback || !context)return false;
    uintptr_t task=0;if(!resolve_binding_task(binding,actor,task))return false;
    const auto live_epoch=sa_owned_route_task_epoch(task,actor);
    if(!live_epoch || *live_epoch!=epoch)return false;
    uintptr_t table=0;if(!task_table(task,table))return false;
    const auto found=lineages.find(table);if(found==lineages.end())return false;
    auto& lineage=*found->second;
    if(!lineage.healthy.load(std::memory_order_acquire) || lineage.control)return false;
    lineage.control=callback;lineage.control_context=context;return true;
}
bool sa_unbind_owned_route_control(const CapturedScriptTask&,ObjectiveActor actor,uint64_t epoch,
        OwnedRouteControlCallback callback,void* context) {
    if(GetCurrentThreadId()!=registry_thread.load(std::memory_order_acquire) || !epoch || !callback)return false;
    auto* lineage=find_lineage(actor,epoch);
    if(!lineage || lineage->control!=callback || lineage->control_context!=context)return false;
    lineage->control=nullptr;lineage->control_context=nullptr;return true;
}
OwnedRouteRetargetResult sa_retarget_owned_route_task(uintptr_t task,ObjectiveActor actor,uint64_t epoch,
        const std::array<float,3>& target,OwnedRouteAuthorization authorized,void* context) {
    if(GetCurrentThreadId()!=registry_thread.load(std::memory_order_acquire) || !task || !epoch || !authorized ||
       !sa_executor_profile_matches())return {};
    for(float value:target)if(!std::isfinite(value))return {};
    const auto live=sa_owned_route_task_epoch(task,actor);
    if(!live || *live!=epoch || !authorized(context))return {};
    auto* lineage=find_lineage(actor,epoch);if(!lineage)return {};
    if(!lineage->control_depth.load(std::memory_order_acquire))return {};
    float move_state_radius=0.0f;
    __try {
        constexpr uint8_t setter[]={0x8A,0x44,0x24,0x10,0x84,0xC0,0x8B,0x54,0x24,0x04,0x75,0x40,0xD9,0x41,0x10,0xD8,0x1A,0xDF,0xE0};
        if(std::memcmp(reinterpret_cast<const void*>(native_entity_address(0x46FE60)),setter,sizeof(setter)))return {};
        if(*reinterpret_cast<const uintptr_t*>(task)!=reinterpret_cast<uintptr_t>(lineage->table.entries.data()) ||
           !*reinterpret_cast<const uintptr_t*>(task+8))return {};
        const float radius=*reinterpret_cast<const float*>(task+0x1C);
        move_state_radius=*reinterpret_cast<const float*>(task+0x20);
        if(radius!=0.8f || !std::isfinite(move_state_radius))return {};
    } __except(EXCEPTION_EXECUTE_HANDLER){return {};}
    if(!authorized(context) || sa_owned_route_task_epoch(task,actor)!=live)return {};
    // The source SetSeek compares XY only, before changing radius, Z or
    // movement. This target belongs to our accepted native task, so its
    // identity/epoch supplies the provenance missing for foreign seek tasks.
    const auto pose=sa_route_actor_vehicle_snapshot(actor);
    if(!pose || !(pose->actor==actor) || !pose->alive)return {};
    __try {
        if(pose->native_state==6 &&
           *reinterpret_cast<const float*>(task+0x10)==target[0] &&
           *reinterpret_cast<const float*>(task+0x14)==target[1]) {
            if(!authorized(context) || sa_owned_route_task_epoch(task,actor)!=live)return {};
            return {true,false};
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){return {};}
    if(!authorized(context) || sa_owned_route_task_epoch(task,actor)!=live)return {};
    bool entered=false;
    __try {
        entered=true;
#ifdef LCS_ISOLATED_ENTITY_BACKEND_TEST
        native_owned_route_retarget(task,target,0.8f,move_state_radius,true);
#else
        using GoToPoint=void(__thiscall*)(uintptr_t,const float*,float,float,bool);
        reinterpret_cast<GoToPoint>(native_entity_address(0x46FE60))(task,target.data(),0.8f,move_state_radius,true);
#endif
        const auto after=sa_owned_route_task_epoch(task,actor);
        if(!after || *after!=epoch || !authorized(context))return {false,true};
        if(*reinterpret_cast<const uintptr_t*>(task)!=reinterpret_cast<uintptr_t>(lineage->table.entries.data()) ||
           std::memcmp(reinterpret_cast<const void*>(task+0x10),target.data(),sizeof(float)*3) ||
           *reinterpret_cast<const float*>(task+0x1C)!=0.8f ||
           !(*reinterpret_cast<const uint8_t*>(task+0x24)&0x04))return {false,true};
        return {true,true};
    } __except(EXCEPTION_EXECUTE_HANDLER){return {false,entered};}
}
} // namespace lcs
