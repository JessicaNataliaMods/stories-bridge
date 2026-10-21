#include <windows.h>
#include "lcs/sa_executor.hpp"
#include "lcs/seek_policy.hpp"
#include <array>
#include <unordered_map>

namespace lcs {
namespace {
struct SeekTable {uintptr_t table,type,first,previous=0;};
// All four native seek specializations share the prefix before the position
// calculator. These entries were independently read from the user's executable.
std::array<SeekTable,4> seek_tables{{
    {0x859DF8,0x46ACE0,0x46F6A0},{0x85A388,0x493800,0x496600},
    {0x86FBB4,0x661EA0,0x665600},{0x87004C,0x66CB20,0x6733E0}
}};
std::unordered_map<int32_t,bool> seek_policies;
using FirstTask=uintptr_t(__thiscall*)(uintptr_t,uintptr_t);
template<size_t Index> uintptr_t __fastcall first_seek_task(uintptr_t task,void*,uintptr_t ped) {
    const auto& entry=seek_tables[Index];
    const auto handle=reinterpret_cast<int32_t(__cdecl*)(uintptr_t)>(0x54FF60)(ped);
    const auto policy=seek_policies.find(handle);
    if(policy!=seek_policies.end())*reinterpret_cast<float*>(task+0x20)=native_seek_radius(policy->second);
    // Apply the policy BEFORE CreateFirstSubTask can select a node route.
    // A game-loop-only update would miss freshly created combat subtasks.
    // Each specialization retains its own original, even in a foreign chain.
    return reinterpret_cast<FirstTask>(entry.previous?entry.previous:entry.first)(task,ped);
}
const std::array<uintptr_t,4> seek_hooks{{reinterpret_cast<uintptr_t>(&first_seek_task<0>),reinterpret_cast<uintptr_t>(&first_seek_task<1>),reinterpret_cast<uintptr_t>(&first_seek_task<2>),reinterpret_cast<uintptr_t>(&first_seek_task<3>)}};
bool replace_entry(uintptr_t address,uintptr_t expected,uintptr_t value) {
    __try {
        auto slot=reinterpret_cast<uintptr_t*>(address);if(*slot!=expected)return false;
        DWORD protection=0;if(!VirtualProtect(slot,sizeof(*slot),PAGE_READWRITE,&protection))return false;
        *slot=value;DWORD ignored=0;return VirtualProtect(slot,sizeof(*slot),protection,&ignored)!=0;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool install_seek_hooks() {
    __try {
        for(size_t i=0;i<seek_tables.size();++i) {
            const auto& entry=seek_tables[i];
            if(*reinterpret_cast<const uintptr_t*>(entry.table+16)!=entry.type)return false;
            const auto current=*reinterpret_cast<const uintptr_t*>(entry.table+36);
            if(entry.previous && current!=seek_hooks[i])return false;
            MEMORY_BASIC_INFORMATION info{};
            if(!VirtualQuery(reinterpret_cast<const void*>(current),&info,sizeof(info)) || info.State!=MEM_COMMIT ||
               !(info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)) ||
               (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        }
        for(size_t i=0;i<seek_tables.size();++i)if(!seek_tables[i].previous) {
            auto& entry=seek_tables[i];
            const auto previous=*reinterpret_cast<const uintptr_t*>(entry.table+36);
            // Chain any existing first-subtask hook and restore it on release.
            entry.previous=previous;
            if(!replace_entry(entry.table+36,previous,seek_hooks[i])){
                if(*reinterpret_cast<const uintptr_t*>(entry.table+36)!=seek_hooks[i])entry.previous=0;
                return false;
            }
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool apply_current_seek_tasks(int32_t actor,bool enabled) {
    __try {
        const auto ped=reinterpret_cast<uintptr_t(__cdecl*)(int32_t)>(0x54FF90)(actor);if(!ped)return false;
        const auto intelligence=*reinterpret_cast<const uintptr_t*>(ped+0x47C);if(!intelligence)return false;
        // Native task manager: five primary and six secondary roots. All task
        // traversal stays on the game thread, and never stores a task pointer.
        for(int slot=0;slot<11;++slot) {
            auto task=*reinterpret_cast<const uintptr_t*>(intelligence+4+slot*4);int depth=0;
            while(task) {
                if(++depth>32)return false;
                const auto table=*reinterpret_cast<const uintptr_t*>(task);if(!table)return false;
                const auto type=reinterpret_cast<int32_t(__thiscall*)(uintptr_t)>(*reinterpret_cast<const uintptr_t*>(table+16))(task);
                if(type==907)*reinterpret_cast<float*>(task+0x20)=native_seek_radius(enabled);
                const auto child=reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t)>(*reinterpret_cast<const uintptr_t*>(table+8))(task);
                if(child==task)return false;task=child;
            }
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
std::optional<bool> sa_char_entering_as_passenger(int32_t actor) {
    if(!sa_executor_profile_matches())return {};
    __try {
        const auto ped=reinterpret_cast<uintptr_t(__cdecl*)(int32_t)>(0x54FF90)(actor);if(!ped)return {};
        const auto intelligence=*reinterpret_cast<const uintptr_t*>(ped+0x47C);if(!intelligence)return {};
        // Read actual primary/secondary task trees, rather than treating a GROUP
        // script record as proof that an entry task reached the pedestrian.
        // Same verified CTaskManager ABI as apply_current_seek_tasks above.
        for(int slot=0;slot<11;++slot) {
            auto task=*reinterpret_cast<const uintptr_t*>(intelligence+4+slot*4);int depth=0;
            while(task) {
                if(++depth>32)return {};
                const auto table=*reinterpret_cast<const uintptr_t*>(task);if(!table)return {};
                const auto type=reinterpret_cast<int32_t(__thiscall*)(uintptr_t)>(*reinterpret_cast<const uintptr_t*>(table+16))(task);
                if(type==700 || type==712)return true; // passenger / timed passenger
                const auto child=reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t)>(*reinterpret_cast<const uintptr_t*>(table+8))(task);
                if(child==task)return {};task=child;
            }
        }
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER){return {};}
}
bool sa_set_pednode_seek(int32_t actor,bool enabled) {
    if(!sa_executor_profile_matches() || !apply_current_seek_tasks(actor,enabled) || !install_seek_hooks())return false;
    seek_policies[actor]=enabled;return true;
}
void sa_erase_pednode_seek(int32_t actor){seek_policies.erase(actor);}
bool sa_release_pednode_seek() {
    seek_policies.clear();bool restored=true;
    for(size_t i=0;i<seek_tables.size();++i)if(seek_tables[i].previous) {
        auto& entry=seek_tables[i];
        if(replace_entry(entry.table+36,seek_hooks[i],entry.previous))entry.previous=0;
        else restored=false; // Preserve a newer foreign hook and its original chain.
    }
    return restored;
}
}
