#include "../src/sa_threat_occlusion.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <windows.h>

// ---- test doubles for the engine side (see sa_threat_occlusion.cpp) ------------------------------------------------
alignas(16) static uint8_t code_page[16];
static uintptr_t pool[80];                   // handle -> CPed* (0 = no such ped)
static const uint8_t original_call[5]={0xE8,0x5A,0x88,0xF7,0xFF};
struct Seen {int calls=0;bool buildings=false,vehicles=true,peds=true,objects=false,dummies=true,see_through=false,camera=true;};
static Seen seen;
static bool __cdecl fake_los(const void*,const void*,bool buildings,bool vehicles,bool peds,bool objects,bool dummies,bool see_through,bool camera) {
    ++seen.calls;seen.buildings=buildings;seen.vehicles=vehicles;seen.peds=peds;seen.objects=objects;
    seen.dummies=dummies;seen.see_through=see_through;seen.camera=camera;return true;
}
namespace lcs {
uint8_t* threat_test_call_site(){return code_page;}
uintptr_t threat_test_get_ped(int32_t handle){return handle>=0 && handle<80?pool[handle]:0;}
uintptr_t threat_test_los_target(){return reinterpret_cast<uintptr_t>(&fake_los);}
void* sa_threat_occlusion_wrapper_for_tests();
void sa_threat_occlusion_set_forward_for_tests(uintptr_t target);
}
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}

// Calls the redirected instruction exactly like CanPedTargetPoint does: ESI = ped, then the nine cdecl arguments.
static void through_wrapper(uintptr_t ped) {
    void* wrapper=lcs::sa_threat_occlusion_wrapper_for_tests();
    float origin[3]={0,0,0},target[3]={1,1,1};
    const void* o=origin;const void* t=target;
    seen=Seen{};
    __asm {
        push esi
        mov esi,ped
        push 0
        push 1
        push 0
        push 1
        push 0
        push 0
        push 1
        push t
        push o
        call wrapper
        add esp,0x24
        pop esi
    }
}
static bool call_bytes_are(const uint8_t* expected){return std::memcmp(code_page,expected,5)==0;}
static bool redirected(){
    return code_page[0]==0xE8 && !call_bytes_are(original_call);
}

int main(){try{
    std::memcpy(code_page,original_call,5);
    lcs::sa_threat_occlusion_set_forward_for_tests(lcs::threat_test_los_target());
    pool[1]=0x1000;pool[2]=0x2000;pool[3]=0x3000;

    through_wrapper(0x1000);
    check(seen.calls==1 && seen.objects && seen.buildings && !seen.vehicles && !seen.peds && !seen.dummies && seen.see_through && !seen.camera,
          "Unflagged: the wrapper forwards the nine arguments unchanged (SA native behaviour = objects block)");

    check(!lcs::sa_threat_occlusion_installed() && call_bytes_are(original_call),"Nothing is redirected while no actor is flagged");
    check(!lcs::sa_set_char_threats_behind_objects(9,false) && !lcs::sa_threat_occlusion_installed(),"A handle that is not a live ped is refused and installs nothing");
    check(lcs::sa_set_char_threats_behind_objects(1,true) && !lcs::sa_threat_occlusion_installed(),"TRUE for an actor that was never flagged is the native default and installs nothing");

    check(lcs::sa_set_char_threats_behind_objects(1,false) && lcs::sa_threat_occlusion_installed() && redirected(),
          "FALSE redirects the line-of-sight call of the threat scan");
    const auto relative=int32_t(reinterpret_cast<uintptr_t>(lcs::sa_threat_occlusion_wrapper_for_tests())-(reinterpret_cast<uintptr_t>(code_page)+5));
    int32_t patched=0;std::memcpy(&patched,code_page+1,4);
    check(patched==relative,"The rewritten call targets the pass-through wrapper");

    through_wrapper(0x1000);
    check(seen.calls==1 && !seen.objects && seen.buildings && !seen.vehicles && !seen.peds && !seen.dummies && seen.see_through && !seen.camera,
          "The flagged actor's scan sees through objects and every other argument is untouched");
    through_wrapper(0x2000);
    check(seen.calls==1 && seen.objects,"A foreign ped scanning through the same instruction keeps objects blocking");
    through_wrapper(0);
    check(seen.objects,"A null scanner is never matched");

    check(lcs::sa_set_char_threats_behind_objects(2,false),"A second actor can be flagged");
    through_wrapper(0x2000);check(!seen.objects,"Both flagged actors see through objects");
    through_wrapper(0x3000);check(seen.objects,"A third ped is still unaffected");
    check(lcs::sa_set_char_threats_behind_objects(1,true) && lcs::sa_threat_occlusion_installed(),"TRUE withdraws one actor and keeps the redirect for the other");
    through_wrapper(0x1000);check(seen.objects,"The withdrawn actor is back to the native behaviour");
    through_wrapper(0x2000);check(!seen.objects,"The remaining flagged actor still sees through objects");

    // The ped pool reuses addresses: a recycled CPed* with a new handle generation must not inherit the flag.
    pool[2]=0;pool[5]=0x2000;
    through_wrapper(0x2000);
    check(seen.objects,"A recycled ped address does not inherit the old actor's flag");
    check(lcs::sa_set_char_threats_behind_objects(1,true) && !lcs::sa_threat_occlusion_installed() && call_bytes_are(original_call),
          "With no live flagged actor left the original call instruction is back");

    // duration/restoration
    check(lcs::sa_set_char_threats_behind_objects(3,false) && redirected(),"Flag again");
    check(lcs::sa_release_threat_occlusion() && !lcs::sa_threat_occlusion_installed() && call_bytes_are(original_call),"Release withdraws every flag and restores the call");
    through_wrapper(0x3000);check(seen.objects,"Released flags no longer apply");

    // a foreign detour owns the site: never overwritten or removed behind its back
    check(lcs::sa_set_char_threats_behind_objects(3,false),"Flag before the foreign detour");
    const uint8_t foreign[5]={0xE9,1,2,3,4};std::memcpy(code_page,foreign,5);
    check(!lcs::sa_release_threat_occlusion() && call_bytes_are(foreign),"A foreign detour installed meanwhile is left alone and the failure is reported");
    std::memcpy(code_page,original_call,5);
    check(lcs::sa_release_threat_occlusion() && call_bytes_are(original_call),"If the original instruction came back the release completes");

    // wrong pre-image: refuse rather than patch something unknown
    const uint8_t unknown[5]={0x90,0x90,0x90,0x90,0x90};std::memcpy(code_page,unknown,5);
    check(!lcs::sa_set_char_threats_behind_objects(3,false) && call_bytes_are(unknown),"An unexpected instruction is never overwritten");
    std::memcpy(code_page,original_call,5);

    // bounded table
    for(int i=0;i<64;++i){pool[10+i]=0x9000+uintptr_t(i)*16;check(lcs::sa_set_char_threats_behind_objects(10+i,false),"Fill the bounded table");}
    pool[79]=0xAAAA0;
    check(!lcs::sa_set_char_threats_behind_objects(79,false),"The table is bounded and refuses instead of dropping a flag");
    check(lcs::sa_release_threat_occlusion() && call_bytes_are(original_call),"Final release");
    std::cout<<"PASS: per-actor threat occlusion wrapper: only flagged scanning peds lose the objects argument, pool reuse, withdrawal, foreign detour, wrong pre-image and release are handled\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
