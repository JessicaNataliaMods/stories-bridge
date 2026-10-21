#include <windows.h>
#include "sa_threat_occlusion.hpp"
#include "lcs/sa_executor.hpp"
#include <cstring>

#ifdef LCS_THREAT_OCCLUSION_TEST_BACKEND
namespace lcs {
// Test doubles (see tests/threat_occlusion_tests.cpp): a fake code page for the redirected call, a fake ped pool and a
// recording stand-in for CWorld::GetIsLineOfSightClear.
uint8_t* threat_test_call_site();
uintptr_t threat_test_get_ped(int32_t handle);
uintptr_t threat_test_los_target();
}
#endif

namespace lcs {
namespace {
// SA 1.0 US (pinned by tests/threat_occlusion_pin_tests.py):
//   CPedGeometryAnalyser::CanPedTargetPoint 0x5F1B70 keeps its ped argument in ESI for the whole function and ends with
//   `call CWorld::GetIsLineOfSightClear` at 0x5F1C31, pushing (buildings=1, vehicles=0, peds=0, objects=1, dummies=0,
//   seeThrough=1, ignoreSomeObjectsForCamera=0) after the two vectors.
constexpr uintptr_t call_site=0x5F1C31,los_function=0x56A490,get_ped_function=0x54FF90;
constexpr uint8_t expected_call[5]={0xE8,0x5A,0x88,0xF7,0xFF};
constexpr uint8_t scan_call[]={0x8D,0x8D,0xAC,0x00,0x00,0x00,0xE8,0xD3,0xFE,0xFF,0xFF};                    // 0x607EA2
constexpr uint8_t closest_call[]={0xE8,0x75,0xFC,0xFF,0xFF};                                             // 0x607E16
constexpr uint8_t target_call[]={0x50,0x56,0x53,0xE8,0x8B,0x9F,0xFE,0xFF};                                // 0x607CAD
constexpr uint8_t point_call[]={0x51,0x8D,0x54,0x24,0x04,0x52,0x50,0xE8,0xCA,0xFE,0xFF,0xFF};             // 0x5F1C9A
constexpr uint8_t sight[]={0x6A,0x00,0xD8,0x05,0x34,0x8F,0x85,0x00,0x6A,0x01,0xD9,0x40,0x04,0x8B,0x00,0x6A,
    0x00,0xD9,0x5C,0x24,0x14,0x6A,0x01,0x6A,0x00,0xD9,0x5C,0x24,0x20,0x6A,0x00,0x6A,0x01,0x52,0x8D,0x4C,
    0x24,0x24,0x51,0x89,0x44,0x24,0x28,0xE8,0x5A,0x88,0xF7,0xFF};                                       // 0x5F1C06
constexpr size_t max_flagged=64;
struct Entry {int32_t handle=-1;uintptr_t ped=0;};
Entry entries[max_flagged];
size_t entry_count=0;
bool installed=false;
uintptr_t los_target=los_function;       // where the wrapper forwards (a variable only so the test can substitute it)

uintptr_t ped_of(int32_t handle) {
    __try {
#ifdef LCS_THREAT_OCCLUSION_TEST_BACKEND
        return threat_test_get_ped(handle);
#else
        return reinterpret_cast<uintptr_t(__cdecl*)(int32_t)>(get_ped_function)(handle);
#endif
    } __except(EXCEPTION_EXECUTE_HANDLER) {return 0;}
}
uint8_t* code_site() {
#ifdef LCS_THREAT_OCCLUSION_TEST_BACKEND
    return threat_test_call_site();
#else
    return reinterpret_cast<uint8_t*>(call_site);
#endif
}
bool pinned_chain() {
#ifdef LCS_THREAT_OCCLUSION_TEST_BACKEND
    return true;
#else
    __try {
        return std::memcmp(reinterpret_cast<const void*>(0x607EA2),scan_call,sizeof(scan_call))==0 &&
               std::memcmp(reinterpret_cast<const void*>(0x607E16),closest_call,sizeof(closest_call))==0 &&
               std::memcmp(reinterpret_cast<const void*>(0x607CAD),target_call,sizeof(target_call))==0 &&
               std::memcmp(reinterpret_cast<const void*>(0x5F1C9A),point_call,sizeof(point_call))==0 &&
               std::memcmp(reinterpret_cast<const void*>(0x5F1C06),sight,sizeof(sight))==0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
#endif
}
}

// Called by the wrapper with the scanning ped. A flagged entry is trusted only while its handle still resolves to the very
// same CPed (the pool reuses addresses; the handle carries the generation), otherwise it is dropped.
extern "C" bool __cdecl lcs_threat_occlusion_decide(uintptr_t ped) {
    if(!entry_count || !ped)return false;
    for(size_t i=0;i<entry_count;++i) {
        if(entries[i].ped!=ped)continue;
        if(ped_of(entries[i].handle)==ped)return true;
        entries[i]=entries[--entry_count];
        return false;
    }
    return false;
}

// Replaces `call CWorld::GetIsLineOfSightClear` inside CanPedTargetPoint. On entry (cdecl, called from that site): ESI is
// the scanning ped, [esp+4]... are the nine arguments and [esp+0x18] is `objects`. Everything else is forwarded untouched.
extern "C" __declspec(naked) void lcs_threat_occlusion_wrapper() {
    __asm {
        push esi
        call lcs_threat_occlusion_decide
        add esp,4
        test al,al
        jz forward
        mov dword ptr [esp+0x18],0
    forward:
        jmp dword ptr [los_target]
    }
}

namespace {
bool write_call(uint8_t* site,const uint8_t* bytes) {
    __try {
        DWORD previous=0;
        if(!VirtualProtect(site,5,PAGE_EXECUTE_READWRITE,&previous))return false;
        std::memcpy(site,bytes,5);
        FlushInstructionCache(GetCurrentProcess(),site,5);
        DWORD ignored=0;
        return VirtualProtect(site,5,previous,&ignored)!=0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool bytes_equal(const uint8_t* site,const uint8_t* expected) {
    __try {return std::memcmp(site,expected,5)==0;} __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
void wrapper_call_bytes(uint8_t* site,uint8_t out[5]) {
    const auto target=reinterpret_cast<uintptr_t>(&lcs_threat_occlusion_wrapper);
    const auto relative=int32_t(target-(reinterpret_cast<uintptr_t>(site)+5));
    out[0]=0xE8;std::memcpy(out+1,&relative,4);
}
bool install_redirect() {
    if(installed)return true;
    uint8_t* site=code_site();
    uint8_t wanted[5];wrapper_call_bytes(site,wanted);
#ifdef LCS_THREAT_OCCLUSION_TEST_BACKEND
    // the fake page carries the same original instruction so the pre-image check below is exercised
#endif
    if(!bytes_equal(site,expected_call))return false;
    if(!write_call(site,wanted) || !bytes_equal(site,wanted)){
        (void)write_call(site,expected_call);       // best effort: never leave a half-written redirect behind
        return false;
    }
    installed=true;return true;
}
bool remove_redirect() {
    if(!installed)return true;
    uint8_t* site=code_site();
    uint8_t wanted[5];wrapper_call_bytes(site,wanted);
    if(bytes_equal(site,expected_call)){installed=false;return true;}       // somebody else already put it back
    if(!bytes_equal(site,wanted))return false;                              // a foreign detour owns the site now: leave it alone
    if(!write_call(site,expected_call) || !bytes_equal(site,expected_call))return false;
    installed=false;return true;
}
}

bool sa_set_char_threats_behind_objects(int32_t ped_handle,bool objects_block) {
#ifndef LCS_THREAT_OCCLUSION_TEST_BACKEND
    if(!sa_executor_profile_matches() || !pinned_chain())return false;
#endif
    const auto ped=ped_of(ped_handle);
    if(!ped)return false;
    // Drop entries of actors that no longer exist so the table stays bounded.
    for(size_t i=0;i<entry_count;) {
        if(ped_of(entries[i].handle)!=entries[i].ped)entries[i]=entries[--entry_count];else ++i;
    }
    size_t found=entry_count;
    for(size_t i=0;i<entry_count;++i)if(entries[i].ped==ped){found=i;break;}
    if(objects_block) {
        if(found<entry_count)entries[found]=entries[--entry_count];
        if(!entry_count)return remove_redirect();
        return true;
    }
    if(found<entry_count)return true;                  // already see-through
    if(entry_count==max_flagged)return false;
    if(!install_redirect())return false;
    entries[entry_count++]=Entry{ped_handle,ped};
    return true;
}

bool sa_release_threat_occlusion() {
    entry_count=0;
    return remove_redirect();
}

bool sa_threat_occlusion_installed(){return installed;}

#ifdef LCS_THREAT_OCCLUSION_TEST_BACKEND
void* sa_threat_occlusion_wrapper_for_tests(){return reinterpret_cast<void*>(&lcs_threat_occlusion_wrapper);}
void sa_threat_occlusion_set_forward_for_tests(uintptr_t target){los_target=target;}
#endif
}
