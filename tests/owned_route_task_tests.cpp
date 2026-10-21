#include <windows.h>
#include "lcs/owned_route_task.hpp"
#include "lcs/owned_route_host.hpp"
#include "lcs/route_vehicle_native.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <cstring>
#include <limits>

namespace {
constexpr uintptr_t native_table=0x86FDA8;
std::array<uintptr_t,12> table{};
struct Pool {uintptr_t first=0,flags=0;int32_t count=1;} pool;
uintptr_t pool_pointer=reinterpret_cast<uintptr_t>(&pool);
uint8_t pool_flag=0;
std::array<uint8_t,6> event_profile{{0xB8,0x20,0,0,0,0xC3}};
std::array<uint8_t,19> target_profile{{0x8A,0x44,0x24,0x10,0x84,0xC0,0x8B,0x54,0x24,0x04,0x75,0x40,0xD9,0x41,0x10,0xD8,0x1A,0xDF,0xE0}};
lcs::ScriptTaskSnapshot bank{};
struct Task {uintptr_t vptr=native_table,parent=0,subtask=0;std::array<uint32_t,7> payload{};};
size_t destructions=0,clones=0;
size_t controls=0,retargets=0;
int32_t actor_state=0;
uintptr_t __fastcall native_control(uintptr_t self,uintptr_t,uintptr_t) {
    ++controls;return reinterpret_cast<Task*>(self)->subtask;
}
struct ControlProbe {uint64_t epoch=0;bool authorized=true;size_t calls=0;lcs::OwnedRouteRetargetResult effect;std::array<float,3> target{20,30,40};};
bool authorized(void* context){return static_cast<ControlProbe*>(context)->authorized;}
void control_route(void* context,lcs::ObjectiveActor actor,uintptr_t task,uintptr_t ped) {
    auto& probe=*static_cast<ControlProbe*>(context);++probe.calls;
    if(ped!=actor.native_ped)throw std::runtime_error("Control actor mismatch");
    probe.effect=lcs::sa_retarget_owned_route_task(task,actor,probe.epoch,probe.target,authorized,&probe);
}
uintptr_t __fastcall native_destroy(uintptr_t self,uintptr_t,uint32_t flags) {
    ++destructions;reinterpret_cast<Task*>(self)->vptr=native_table;
    if(flags&1)delete reinterpret_cast<Task*>(self);
    return self;
}
uintptr_t __fastcall native_clone(uintptr_t self,uintptr_t) {
    ++clones;auto* task=new Task(*reinterpret_cast<const Task*>(self));
    task->vptr=native_table;task->parent=task->subtask=0;return reinterpret_cast<uintptr_t>(task);
}
uintptr_t duplicate(uintptr_t self) {
    const auto* vtable=reinterpret_cast<const uintptr_t*>(*reinterpret_cast<const uintptr_t*>(self));
    return reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t)>(vtable[1])(self);
}
void destroy(uintptr_t self,uint32_t flags=0) {
    const auto* vtable=reinterpret_cast<const uintptr_t*>(*reinterpret_cast<const uintptr_t*>(self));
    reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t,uint32_t)>(vtable[0])(self,flags);
}
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
}
namespace lcs {
uintptr_t native_task_limit_adjuster_module(){return 0;}
uintptr_t native_entity_address(uintptr_t retail) {
    if(retail==native_table)return reinterpret_cast<uintptr_t>(table.data()+1);
    static const uint8_t allocator[]={0x8B,0x0D,0xA8,0x44,0xB7,0,0xE9,0x55,0xFF,0xFF,0xFF};
    if(retail==0x61A5A0)return reinterpret_cast<uintptr_t>(allocator);
    static const uint8_t stride[]={0xC1,0xE0,0x07};
    if(retail==0x61A55A)return reinterpret_cast<uintptr_t>(stride);
    if(retail==0xB744A8)return reinterpret_cast<uintptr_t>(&pool_pointer);
    if(retail==0x4B0A30)return reinterpret_cast<uintptr_t>(event_profile.data());
    if(retail==0x46FE60)return reinterpret_cast<uintptr_t>(target_profile.data());
    throw std::runtime_error("Unexpected native data access");
}
std::optional<ScriptTaskSnapshot> sa_script_task_snapshot(){return bank;}
bool sa_executor_profile_matches(){return true;}
std::optional<RouteActorVehicleSnapshot> sa_route_actor_vehicle_snapshot(const ObjectiveActor& actor) {
    RouteActorVehicleSnapshot pose;pose.actor=actor;pose.alive=true;pose.native_state=actor_state;return pose;
}
void native_owned_route_retarget(uintptr_t task,const std::array<float,3>& target,float radius,float move_radius,bool force) {
    ++retargets;if(!force || radius!=0.8f || move_radius!=0.25f)throw std::runtime_error("Native route radius/force mismatch");
    std::memcpy(reinterpret_cast<void*>(task+0x10),target.data(),12);
    *reinterpret_cast<float*>(task+0x1C)=radius;*reinterpret_cast<float*>(task+0x20)=move_radius;
    *reinterpret_cast<uint8_t*>(task+0x24)|=4;
}
}
int main(){try {
    using namespace lcs;
    table[0]=0x123456;table[1]=reinterpret_cast<uintptr_t>(&native_destroy);table[2]=reinterpret_cast<uintptr_t>(&native_clone);
    for(size_t n=3;n<table.size();++n)table[n]=0x1000+n;
    table[11]=reinterpret_cast<uintptr_t>(&native_control);
    const auto preserved=table;
    const ObjectiveActor actor{27,0x123400};
    Task root;root.payload[0]=6;const auto address=reinterpret_cast<uintptr_t>(&root);
    root.parent=1;check(!sa_track_private_route_task(address,actor),"Published/parented task cannot become owned");root.parent=0;
    const auto epoch=sa_track_private_route_task(address,actor);
    check(epoch && sa_owned_route_task_epoch(address,actor)==epoch,"Private constructor yields real lifetime epoch");
    const auto* wrapped=reinterpret_cast<const uintptr_t*>(root.vptr);
    check(wrapped[-1]==table[0],"Word preceding native table preserved");
    for(size_t n=2;n<10;++n)check(wrapped[n]==table[n+1],"Unchanged virtual slots forwarded intact");
    check(wrapped[10]!=table[11],"Only the owned task's control slot receives the route boundary");
    check(table==preserved && root.payload[0]==6,"No global vtable/code or target/movement mutation");
    const auto copy=duplicate(address),second=duplicate(copy);
    check(clones==2 && sa_owned_route_task_epoch(copy,actor)==epoch && sa_owned_route_task_epoch(second,actor)==epoch,"Native event/task clones carry original assignment lineage");
    check(!sa_owned_route_task_epoch(copy,{28,actor.native_ped}),"Another actor cannot claim a native clone");
    bool actor_alive=true;
    OwnedRouteReaders readers;
    readers.actor=[&](uint32_t ped)->std::optional<ObjectiveActor>{return actor_alive && ped==actor.native_ped?std::optional<ObjectiveActor>(actor):std::nullopt;};
    OwnedRouteTaskHost host(readers);
    std::array<uint32_t,6> event{};event[0]=0x85B378;event[4]=uint32_t(address);
    bank[5].opcode=0x05D3;bank[5].ped=uint32_t(actor.native_ped);bank[5].status=0;
    bank[5].task=0;bank[5].event=uint32_t(reinterpret_cast<uintptr_t>(event.data()));
    pool.first=address;pool.flags=reinterpret_cast<uintptr_t>(&pool_flag);
    const auto initial=host.accept({5,bank[5]});
    check(initial && initial->epoch==epoch,"Actual event record accepts only the tracked private-task lineage");
    destroy(address);
    check(!sa_owned_route_task_epoch(address,actor) && sa_owned_route_task_epoch(copy,actor)==epoch,"Event-original destruction retires only that instance, not the transferred clone");
    bank[5].event=0;bank[5].task=uint32_t(copy);bank[5].status=1;pool.first=copy;
    check(initial && host.refresh(*initial) && host.refresh(*initial)->binding.record.task==copy,"Real native clone lineage proves event-to-task association without global mutation hooks");
    {
        ControlProbe probe;probe.epoch=epoch;
        const auto captured=host.refresh(*initial)->binding;
        auto* live=reinterpret_cast<Task*>(copy);live->subtask=0x1234;
        const std::array<float,3> accepted_target{11.5f,22.5f,33.5f};
        std::memcpy(reinterpret_cast<void*>(copy+0x10),accepted_target.data(),sizeof(accepted_target));
        *reinterpret_cast<float*>(copy+0x1C)=0.8f;*reinterpret_cast<float*>(copy+0x20)=0.25f;
        check(sa_owned_route_target(copy,actor,epoch)==accepted_target,
            "Accepted private GoToPoint exposes its finite live target outside ControlSubTask");
        check(!sa_owned_route_target(copy,{28,actor.native_ped},epoch) &&
            !sa_owned_route_target(copy,actor,epoch+1),"Target read rejects a stale actor or task epoch");
        const auto private_vptr=live->vptr;live->vptr=native_table;
        check(!sa_owned_route_target(copy,actor,epoch),"Target read rejects a foreign vtable");live->vptr=private_vptr;
        const auto accepted_x=*reinterpret_cast<float*>(copy+0x10);
        *reinterpret_cast<float*>(copy+0x10)=std::numeric_limits<float>::quiet_NaN();
        check(!sa_owned_route_target(copy,actor,epoch),"Target read rejects a non-finite coordinate");
        *reinterpret_cast<float*>(copy+0x10)=accepted_x;
        live->subtask=0;
        check(!sa_owned_route_target(copy,actor,epoch),"Target read rejects an owned task without its live child");
        live->subtask=0x1234;
        check(!sa_retarget_owned_route_task(copy,actor,epoch,{1,2,3},authorized,&probe).entered,"Retarget is forbidden outside the actual native control callback");
        check(!sa_bind_owned_route_control(captured,actor,epoch+1,control_route,&probe),"A stale epoch cannot bind a controller");
        check(sa_bind_owned_route_control(captured,actor,epoch,control_route,&probe),"Real accepted clone receipt binds its source controller");
        const auto* methods=reinterpret_cast<const uintptr_t*>(live->vptr);
        const auto control=reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t,uintptr_t)>(methods[10]);
        check(control(copy,actor.native_ped)==live->subtask && probe.calls==1 && controls==1 && retargets==1 && probe.effect.executed && probe.effect.entered,"Owned control retargets before forwarding the original child processor");
        check(sa_owned_route_task_epoch(copy,actor)==epoch && *reinterpret_cast<float*>(copy+0x10)==20 && *reinterpret_cast<float*>(copy+0x20)==0.25f,"Same-task target update preserves accepted epoch and movement radius");
        probe.authorized=false;control(copy,actor.native_ped);
        check(probe.calls==2 && controls==2 && retargets==1 && !probe.effect.entered,"Lost source authorization cannot enter the native setter");
        probe.authorized=true;actor_state=6;probe.target={20,30,999};
        const auto before_same_xy=*live;
        control(copy,actor.native_ped);
        check(probe.effect.executed && !probe.effect.entered && retargets==1 &&
            std::memcmp(live,&before_same_xy,sizeof(Task))==0,
            "Source SEEK_POS identical XY preserves Z, radius, flags and task bytes without calling native setter");
        probe.target[0]=21;control(copy,actor.native_ped);
        check(probe.effect.executed && probe.effect.entered && retargets==2 &&
            *reinterpret_cast<float*>(copy+0x10)==21 && *reinterpret_cast<float*>(copy+0x18)==999,
            "Different XY while seeking still executes the real native retarget");
        actor_state=0;probe.target[2]=1000;control(copy,actor.native_ped);
        check(probe.effect.executed && probe.effect.entered && retargets==3,
            "Identical XY in another source state does not invent a SetSeek early return");
        check(sa_unbind_owned_route_control(captured,actor,epoch,control_route,&probe),"Controller can detach without destroying the native task");
        control(copy,actor.native_ped);check(probe.calls==5 && controls==6,"Unbound control delegates without calling the retired context");
        live->subtask=0;
    }
    auto faulty_readers=readers;faulty_readers.actor=[](uint32_t)->std::optional<ObjectiveActor>{throw std::runtime_error("Injected reader fault");};
    OwnedRouteTaskHost faulty_host(faulty_readers);
    check(!faulty_host.refresh(*initial),"Advisory reader exception cannot escape and replay an already entered native effect");
    pool_flag=0x80;check(!host.refresh(*initial),"Free task pool slot cannot authorize a stale receipt");pool_flag=0;
    actor_alive=false;check(!host.refresh(*initial),"Retired weak actor cell blocks even a surviving native task clone");actor_alive=true;
    destroy(copy,1);destroy(second,1);
    check(destructions==3,"Every real destructor forwarded exactly once");
    // Reconstruct at precisely the same address after all former instances die.
    root=Task{};check(!sa_owned_route_task_epoch(address,actor),"Byte-identical native pool reuse cannot resurrect ownership");
    const auto fresh=sa_track_private_route_task(address,actor);
    check(fresh && fresh!=epoch,"New private construction has a fresh monotonic epoch");
    bank[5].task=uint32_t(address);pool.first=address;
    check(!host.refresh(*initial),"Reused task bank slot and identical actor cannot refresh an earlier construction epoch");
    const auto peer=duplicate(address);
    std::thread foreign([&]{destroy(peer,1);});foreign.join();
    check(!sa_owned_route_task_epoch(address,actor),"Foreign mutation quarantines surviving lineage without touching registry on foreign thread");
    destroy(address);
    std::cout<<"PASS: owned native task destruction/clone ABI, event transfer, reuse and foreign-thread quarantine; engine calls mocked\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
