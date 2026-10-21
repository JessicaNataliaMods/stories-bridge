#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

namespace lcs {
// Reviewed straight-line whole instructions only. This builds bytes; it does
// NOT allocate executable pages, install hooks or authorize altered prologues.
struct TaskHookSite {
    uint32_t address=0;
    uint8_t length=0;
    std::array<uint8_t,11> prefix{};
};
inline constexpr std::array<TaskHookSite,11> task_hook_sites{{
    {0x608330,10,{{0x8B,0xC1,0x33,0xC9,0xC7,0x00,0xFF,0xFF,0xFF,0xFF}}}, // Constructor
    {0x608350,6,{{0x56,0x8B,0xF1,0x8B,0x4E,0x0C}}}, // Flush
    {0x608390,7,{{0x53,0x56,0x8B,0xF1,0x8B,0x4E,0x0C}}}, // Set(event), inline Flush
    {0x6083E0,6,{{0x56,0x8B,0xF1,0x8B,0x4E,0x0C}}}, // Set(task), inline Flush
    {0x608440,6,{{0x56,0x8B,0xF1,0x8B,0x4E,0x0C}}}, // Set(attractor), inline Flush
    {0x6084A0,6,{{0x56,0x8B,0xF1,0x8B,0x4E,0x0C}}}, // Set(group), inline Flush
    {0x608500,7,{{0x8B,0x44,0x24,0x04,0x89,0x41,0x04}}}, // AssociateWithEvent
    {0x608520,11,{{0x8B,0x44,0x24,0x04,0xC7,0x41,0x04,0,0,0,0}}}, // AssociateWithTask
    {0x61A5B0,6,{{0x8B,0x0D,0xA8,0x44,0xB7,0}}}, // native task release, before pool-slot reuse
    {0x60860C,5,{{0x89,0x3E,0x89,0x7E,0xF8}}}, // Process inline Flush; record = ESI - 12
    {0x4B0A50,7,{{0x6A,0xFF,0x68,0x18,0xA0,0x83,0}}} // CEventScriptCommand destructor, before owned task deletion
}};
struct TaskHookPlan {
    uint8_t entry_size=0,trampoline_size=0;
    std::array<uint8_t,11> entry{};
    std::array<uint8_t,16> trampoline{};
};
inline void relative_jump(uint8_t* bytes,uint32_t location,uint32_t destination) {
    bytes[0]=0xE9;const uint32_t delta=destination-location-5u;
    std::memcpy(bytes+1,&delta,4); // explicit 32-bit wrap, including high ASLR addresses
}
inline std::optional<TaskHookPlan> plan_task_hook(const TaskHookSite& site,uint32_t destination,uint32_t trampoline,
        const uint8_t* actual,size_t available) {
    if(!destination || !trampoline || !actual)return {};
    if(available<site.length || std::memcmp(actual,site.prefix.data(),site.length) ||
       trampoline>UINT32_MAX-site.length-5u || destination==site.address || trampoline==site.address)return {};
    // Trampoline must be outside the overwritten entry and separate from the
    // callback. Runtime allocator/executable protection/ownership still needed.
    if((uint64_t(trampoline)<uint64_t(site.address)+site.length && uint64_t(trampoline)+site.length+5u>site.address) ||
       (destination>=trampoline && uint64_t(destination)<uint64_t(trampoline)+site.length+5u) ||
       (destination>=site.address && destination<site.address+site.length))return {};
    TaskHookPlan result;result.entry_size=site.length;result.trampoline_size=site.length+5;
    result.entry.fill(0x90);relative_jump(result.entry.data(),site.address,destination);
    std::memcpy(result.trampoline.data(),actual,site.length);
    relative_jump(result.trampoline.data()+site.length,trampoline+site.length,site.address+site.length);
    return result;
}
inline std::optional<TaskHookPlan> plan_task_hook(size_t index,uint32_t destination,uint32_t trampoline,
        const uint8_t* actual,size_t available) {
    if(index>=task_hook_sites.size())return {};
    return plan_task_hook(task_hook_sites[index],destination,trampoline,actual,available);
}
}
