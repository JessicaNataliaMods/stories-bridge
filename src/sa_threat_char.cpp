#include <windows.h>
#include "lcs/native_memory.hpp"
#include "lcs/sa_executor.hpp"
#include "sa_threat_char.hpp"
#include <cstdint>
#include <cstring>

namespace lcs {

#ifdef LCS_ISOLATED_THREAT_CHAR_BACKEND_TEST
uintptr_t native_threat_char_current_event(uintptr_t history);
bool native_threat_char_source_entity(uintptr_t event,uintptr_t& source);
#else
static uintptr_t native_threat_char_current_event(uintptr_t history) {
    // CEventHandlerHistory::GetCurrentEvent() const, confirmed at 0x4B8CA0.
    return reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t)>(0x4B8CA0)(history);
}

namespace {
bool native_module_page(uintptr_t address,size_t bytes,bool executable) {
    if(!address || !bytes || address+bytes<address)return false;
    MEMORY_BASIC_INFORMATION info{};
    if(!VirtualQuery(reinterpret_cast<const void*>(address),&info,sizeof(info)) ||
       info.State!=MEM_COMMIT || info.AllocationBase!=reinterpret_cast<void*>(0x400000) ||
       (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
    const auto start=reinterpret_cast<uintptr_t>(info.BaseAddress);
    const auto end=start+info.RegionSize;
    if(end<start || address<start || address+bytes>end)return false;
    const DWORD access=info.Protect&0xFF;
    if(executable)
        return access==PAGE_EXECUTE || access==PAGE_EXECUTE_READ ||
            access==PAGE_EXECUTE_READWRITE || access==PAGE_EXECUTE_WRITECOPY;
    return access==PAGE_READONLY || access==PAGE_READWRITE || access==PAGE_WRITECOPY ||
        access==PAGE_EXECUTE_READ || access==PAGE_EXECUTE_READWRITE || access==PAGE_EXECUTE_WRITECOPY;
}

bool native_threat_char_source_entity(uintptr_t event,uintptr_t& source) {
    if(!event)return false;
    const auto vtable=*reinterpret_cast<const uintptr_t*>(event);
    constexpr size_t source_entity_slot=10; // CEvent::GetSourceEntity() in the retail vtable.
    const auto slot=vtable+source_entity_slot*sizeof(uintptr_t);
    if(!native_module_page(slot,sizeof(uintptr_t),false))return false;
    const auto method=*reinterpret_cast<const uintptr_t*>(slot);
    if(method==0x82263A || !native_module_page(method,1,true))return false;
    source=reinterpret_cast<uintptr_t(__thiscall*)(uintptr_t)>(method)(event);
    return true;
}
}
#endif

namespace {
constexpr int32_t no_threat_char=0;
constexpr int32_t invalid_threat_char=-1;
constexpr size_t ped_stride=0x7C4;

template<size_t N> bool profile(uintptr_t address,const uint8_t (&bytes)[N]) {
    return std::memcmp(reinterpret_cast<const void*>(native_entity_address(address)),bytes,N)==0;
}

bool backend_profile() {
    if(!sa_executor_profile_matches())return false;
    const uint8_t ped_pool[]={0x8B,0x4C,0x24,0x04,0x8B,0x15,0x90,0x44,0xB7,0x00,0x53,0x56,0x8B,0x72,0x04,0x8B,0xC1,0xC1,0xF8,0x08,0x8A,0x1C,0x30,0x5E,0x3A,0xD9,0x5B,0x75,0x0B,0x8B,0x0A,0x69,0xC0,0xC4,0x07,0x00};
    const uint8_t history_getter[]={0x8B,0x41,0x08,0x85,0xC0,0x75,0x03,0x8B,0x41,0x04,0xC3};
    const uint8_t intelligence_ctor[]={0x6A,0xFF,0x68,0x76,0xEA,0x83,0x00,0x64,0xA1,0,0,0,0,0x50,0x64,0x89,0x25,0,0,0,0,0x51,0x53,0x55,0x8B,0x6C,0x24,0x1C,0x56,0x8B,0xF1,0x57,0x55,0x8D,0x4E,0x04,0x89,0x74,0x24,0x14,0x89,0x2E,0xE8,0x31,0xA5,0x07,0x00};
    const uint8_t event_source_default[]={0x33,0xC0,0xC3};
    const uint8_t event_source_slot[]={0xF0,0xE1,0x43,0x00};
    return profile(0x54FF90,ped_pool) && profile(0x4B8CA0,history_getter) &&
        profile(0x607140,intelligence_ctor) && profile(0x85AB60,event_source_slot) &&
        profile(0x43E1F0,event_source_default);
}

struct PedPool {
    uintptr_t objects=0;
    uintptr_t generations=0;
    int32_t count=0;
};

bool read_ped_pool(PedPool& pool) {
    const auto pointer=*reinterpret_cast<const uintptr_t*>(native_entity_address(0xB74490));
    if(!pointer)return false;
    pool.objects=*reinterpret_cast<const uintptr_t*>(pointer);
    pool.generations=*reinterpret_cast<const uintptr_t*>(pointer+4);
    pool.count=*reinterpret_cast<const int32_t*>(pointer+8);
    return pool.objects && pool.generations && pool.count>=0 && pool.count<=0x7FFFFF;
}

bool same_pool(const PedPool& left,const PedPool& right) {
    return left.objects==right.objects && left.generations==right.generations && left.count==right.count;
}

uintptr_t ped_at(const PedPool& pool,int32_t handle) {
    if(!valid_entity_slot(handle,pool.count,reinterpret_cast<const uint8_t*>(pool.generations)))return 0;
    const auto slot=uint32_t(handle)>>8;
    const auto offset=uintptr_t(slot)*ped_stride;
    if(offset/ped_stride!=slot || pool.objects+offset<pool.objects)return 0;
    const auto ped=pool.objects+offset;
    return (*reinterpret_cast<const uint8_t*>(ped+0x36)&7)==3?ped:0;
}

int32_t live_ped_handle(const PedPool& pool,uintptr_t pointer) {
    if(!pointer)return no_threat_char;
    if(pointer<pool.objects)return invalid_threat_char;
    const auto delta=pointer-pool.objects;
    if(delta%ped_stride)return invalid_threat_char;
    const auto slot=delta/ped_stride;
    if(slot>=uint32_t(pool.count))return invalid_threat_char;
    const auto generation=*reinterpret_cast<const uint8_t*>(pool.generations+slot);
    if(generation&0x80)return invalid_threat_char;
    const auto handle=static_cast<int32_t>((uint32_t(slot)<<8)|generation);
    // Retail converts the event source through the ped pool. A dead-state
    // check would change that behavior: a dead but still allocated CPed keeps
    // its pool handle until the pool releases the slot.
    if(ped_at(pool,handle)!=pointer)return invalid_threat_char;
    return handle;
}

bool target_identity_still_matches(const PedPool& before,int32_t handle,uintptr_t ped,
        uintptr_t intelligence,uintptr_t event) {
    PedPool after;
    if(!read_ped_pool(after) || !same_pool(before,after) || ped_at(after,handle)!=ped)return false;
    if(*reinterpret_cast<const uintptr_t*>(ped+0x47C)!=intelligence || !intelligence ||
       *reinterpret_cast<const uintptr_t*>(intelligence+0x34)!=ped)return false;
    const auto history=intelligence+0x38;
    return native_threat_char_current_event(history)==event;
}
}

std::optional<int32_t> sa_get_char_threat_char(int32_t char_handle) {
    __try {
        if(!backend_profile())return {};
        PedPool pool;
        if(!read_ped_pool(pool))return {};
        const auto ped=ped_at(pool,char_handle);
        if(!ped)return no_threat_char;
        const auto intelligence=*reinterpret_cast<const uintptr_t*>(ped+0x47C);
        if(!intelligence)return no_threat_char;
        // CEventHandler stores its owning CPed before the history member.
        // A mismatched owner indicates stale/corrupt intelligence, not an
        // ordinary absent threat event.
        if(*reinterpret_cast<const uintptr_t*>(intelligence+0x34)!=ped)return {};

        // CPedIntelligence::m_eventHandler is at +0x34 (verified from the SA
        // constructor); CEventHandler::m_History follows m_Ped at +4.
        const auto history=intelligence+0x38;
        const auto event=native_threat_char_current_event(history);
        if(!event)return no_threat_char;
        uintptr_t source=0;
        if(!native_threat_char_source_entity(event,source))return {};
        const auto source_handle_before=source?live_ped_handle(pool,source):no_threat_char;
        if(!target_identity_still_matches(pool,char_handle,ped,intelligence,event))return {};
        // Resolve against the refreshed pool only after the native getter has
        // returned. This prevents publishing a handle for a recycled source
        // slot or for an event/target identity that changed during the call.
        PedPool after;
        if(!read_ped_pool(after) || !same_pool(pool,after))return {};
        if(!source)return no_threat_char;
        const auto source_handle_after=live_ped_handle(after,source);
        if(source_handle_after!=source_handle_before)return {};
        return source_handle_after;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return {};
    }
}

}
