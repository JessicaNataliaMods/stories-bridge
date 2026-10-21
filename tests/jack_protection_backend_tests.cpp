#include "../src/sa_jack_protection.hpp"
#include <cstdint>
#include <iostream>

namespace {
using namespace lcs;
int failures=0;
bool release_blocked=false;
void check(bool value,const char* why) {
    if(!value) {std::cerr<<why<<'\n';++failures;}
}
bool live(const ObjectiveActor& actor,const uintptr_t* cell) {
    return cell && *cell==actor.native_ped;
}
bool release(uintptr_t* cell) {
    if(release_blocked)return false;
    *cell=0;return true;
}

void weak_cell_contract() {
    JackProtectionRuntime runtime;
    const ObjectiveActor actor{0x101,0x123400};
    auto* original=runtime.prepare(actor);
    check(original && !*original,"fresh jack-protection weak cell");
    if(!original)return;
    *original=actor.native_ped;
    check(!runtime.confirmed(actor,live),"unpublished reference cannot bypass native stay-in-car behavior");
    check(runtime.publish(actor,original),"publish exact registered actor and cell");
    for(int i=2;i<70;++i) {
        const ObjectiveActor next{(i<<8)|1,uintptr_t(0x223400+i*0x1000)};
        check(runtime.prepare(next)!=nullptr,"prepare across map growth");
    }
    check(runtime.prepare(actor)==original && runtime.confirmed_ped(actor.native_ped,live),
        "map growth preserves registered weak-cell address");
    check(!runtime.confirmed({actor.handle,actor.native_ped+4},live),
        "pointer replacement cannot inherit jack protection");
    check(!runtime.confirmed({actor.handle+1,actor.native_ped},live),
        "generation replacement cannot inherit jack protection");

    const ObjectiveActor replacement{actor.handle,actor.native_ped+0x10000};
    check(runtime.retire_actor(replacement,release) && runtime.confirmed(actor,live),
        "clearing a reused handle cannot retire a different actor generation");
    *original=0;
    check(!runtime.confirmed_ped(actor.native_ped,live),
        "SA ResolveReferences zero disables the native protected-ped branch immediately");
    check(runtime.retire_actor(actor,release),"retire only the exact actor entry");

    auto* retry=runtime.prepare(actor);
    check(retry!=nullptr,"fresh weak cell after exact retirement");
    if(!retry)return;
    *retry=actor.native_ped;
    check(runtime.publish(actor,retry),"publish actor before uncertain release");
    release_blocked=true;
    check(!runtime.retire_actor(actor,release),"uncertain native cleanup reports failure");
    check(!runtime.confirmed_ped(actor.native_ped,live),"quarantine disables the protection lookup first");
    check(runtime.retained_count()==1 && *retry==actor.native_ped,
        "failed release retains the stable native weak cell");
    check(!runtime.prepare({actor.handle,actor.native_ped+8}),
        "quarantined handle cannot be reused while SA may retain its cell");
    check(!runtime.release_all(release),"cleanup keeps the runtime pending when native release is uncertain");
    release_blocked=false;
    check(runtime.release_all(release) && runtime.retained_count()==0,
        "later cleanup retry safely releases the quarantined cell");
}

void native_consumer_and_patch_contract() {
    const JackProtectionPatchBytes replacement{{0xE9,1,2,3,4,0x90,0x90,0x90,0x90,0x90}};
    auto foreign=replacement;foreign[4]^=0x20;
    check(plan_jack_protection_patch(jack_protection_original_bytes,replacement,false,true)==
        JackProtectionPatchAction::install,"install only over the reviewed native TEST bytes");
    check(plan_jack_protection_patch(replacement,replacement,false,true)==
        JackProtectionPatchAction::refuse,"matching unowned patch bytes are foreign");
    check(plan_jack_protection_patch(foreign,replacement,true,false)==
        JackProtectionPatchAction::refuse,"cleanup never overwrites a changed foreign patch");
    check(plan_jack_protection_patch(replacement,replacement,true,false)==
        JackProtectionPatchAction::restore,"restore only the exact owned patch");
    check(plan_jack_protection_patch(jack_protection_original_bytes,replacement,true,false)==
        JackProtectionPatchAction::already_restored,"external restoration is harmless");

    check(jack_protection_original_bytes[0]==0xF7 && jack_protection_original_bytes[1]==0x86 &&
        jack_protection_original_bytes[2]==0x70 && jack_protection_original_bytes[3]==0x04 &&
        jack_protection_original_bytes[6]==0x00 && jack_protection_original_bytes[7]==0x00 &&
        jack_protection_original_bytes[8]==0x80 && jack_protection_original_bytes[9]==0x00,
        "overwritten instruction is TEST DWORD PTR [ESI+470h],800000h");
    check(jack_protection_native_jne[0]==0x75 && jack_protection_native_jne[1]==0x48 &&
        jack_protection_native_test_continue+2+jack_protection_native_jne[1]==
            jack_protection_native_protected_skip,
        "replayed TEST rejoins at the original JNE to 64F5D3");
    uint32_t call_encoded=0;
    for(unsigned i=0;i<4;++i)call_encoded|=uint32_t(jack_protection_native_normal_path_bytes[i+3])<<(8*i);
    const auto call_delta=static_cast<int32_t>(call_encoded);
    check(jack_protection_native_normal_path_bytes[0]==0x56 &&
        jack_protection_native_normal_path_bytes[1]==0x57 &&
        jack_protection_following_call+5+call_delta==jack_protection_following_call_target,
        "normal SA path preserves PUSH ESI/PUSH EDI and the call to 64F110");
    check(!jack_protection_native_stay_flag(0) &&
        !jack_protection_native_stay_flag(0x00000001) &&
        jack_protection_native_stay_flag(jack_protection_native_stay_mask),
        "native stay behavior tests the reviewed 0x800000 bit");

    const int lookup_stack=int(JackProtectionHookStackContract::lookup_saved_bytes)+
        int(JackProtectionHookStackContract::lookup_argument_bytes);
    check(lookup_stack-4-32-4==0,"lookup restores PUSHFD/PUSHAD/argument on both hook paths");
    check(jack_protection_patch_site==0x64F57F &&
        jack_protection_native_test_continue==0x64F589 &&
        jack_protection_native_protected_skip==0x64F5D3,
        "hook addresses match the reviewed retail consumer");
}
}

int main() {
    weak_cell_contract();native_consumer_and_patch_contract();
    if(!failures)std::cout<<"jack protection weak-reference, native consumer and patch contracts passed\n";
    return failures?1:0;
}
