#include "../src/sa_move_anim_override.hpp"
#include <cstdint>
#include <iostream>
namespace {
using namespace lcs;
int failures=0;
void check(bool value,const char* why) {if(!value) {std::cerr<<why<<'\n';++failures;}}
bool release_blocked=false;
bool live(const ObjectiveActor& actor,const uintptr_t* cell) {return cell && *cell==actor.native_ped;}
bool release(uintptr_t* cell) {if(release_blocked)return false;*cell=0;return true;}
void weak_cell_contract() {
    check(!MoveAnimOverrideRuntime::applies_to_native_type(0) &&
          !MoveAnimOverrideRuntime::applies_to_native_type(1) &&
          MoveAnimOverrideRuntime::applies_to_native_type(4),
          "Original empty player updater preserves native SA player locomotion; NPCs can use override");
    MoveAnimOverrideRuntime runtime;
    const ObjectiveActor actor{0x101,0x123400};
    auto* original=runtime.prepare(actor);
    check(original && !*original,"fresh weak cell");
    if(!original)return;
    *original=actor.native_ped;
    check(!runtime.confirmed(actor,live),"unpublished reference must not drive consumer");
    check(runtime.publish(actor,original),"publish exact registered actor/cell");
    for(int i=2;i<70;++i) {
        const ObjectiveActor next{(i<<8)|1,uintptr_t(0x123400+i*0x1000)};
        auto* cell=runtime.prepare(next);
        check(cell!=nullptr,"prepare map growth");
    }
    check(runtime.prepare(actor)==original && runtime.confirmed_ped(actor.native_ped,live),"map rehash preserves registered cell address");
    check(!runtime.confirmed({actor.handle,actor.native_ped+4},live),"pointer replacement cannot inherit override");
    check(!runtime.confirmed({actor.handle+1,actor.native_ped},live),"generation replacement cannot inherit override");
    *original=0;
    check(!runtime.confirmed_ped(actor.native_ped,live),"native ResolveReferences zero disables consumer immediately");
    check(runtime.retire_handle(actor.handle,release),"zero cell retires without native cleanup");
    original=runtime.prepare(actor);*original=actor.native_ped;
    check(runtime.publish(actor,original),"republish live identity");
    release_blocked=true;
    check(!runtime.retire_handle(actor.handle,release),"uncertain native cleanup reports failure");
    check(!runtime.confirmed_ped(actor.native_ped,live),"quarantine disables override before uncertain release");
    check(runtime.retained_count()==1 && *original==actor.native_ped,"uncertain native weak cell remains allocated");
    check(!runtime.prepare({actor.handle,actor.native_ped+8}),"quarantine prevents handle reuse");
    check(!runtime.release_all(release),"abort retains cells when native cleanup is uncertain");
    release_blocked=false;
    check(runtime.release_all(release) && runtime.retained_count()==0,"cleanup retry safely releases quarantine");
}
void patch_and_stack_contract() {
    const MoveAnimPatchBytes replacement{{0xE9,1,2,3,4,0x90}};
    MoveAnimPatchBytes foreign=replacement;foreign[2]^=0x20;
    check(plan_move_anim_patch(move_anim_original_bytes,replacement,false,true)==MoveAnimPatchAction::install,"install only original bytes");
    check(plan_move_anim_patch(replacement,replacement,false,true)==MoveAnimPatchAction::refuse,"matching unowned hook refused");
    check(plan_move_anim_patch(foreign,replacement,true,false)==MoveAnimPatchAction::refuse,"teardown never overwrites foreign hook");
    check(plan_move_anim_patch(replacement,replacement,true,false)==MoveAnimPatchAction::restore,"restore exact owned hook");
    check(plan_move_anim_patch(move_anim_original_bytes,replacement,true,false)==MoveAnimPatchAction::already_restored,"external restoration is harmless");
    int32_t call_delta=0;
    for(unsigned i=0;i<4;++i)call_delta|=int32_t(move_anim_original_bytes[i+2])<<(8*i);
    check(move_anim_original_bytes[0]==0x56 && move_anim_original_bytes[1]==0xE8 &&
        move_anim_native_continue+call_delta==move_anim_native_group_query,"overwritten PUSH ESI/CALL targets real native group query");
    const int lookup_stack=int(MoveAnimHookStackContract::lookup_saved_bytes)+int(MoveAnimHookStackContract::lookup_argument_bytes);
    check(lookup_stack-4-32-4==0,"lookup restores PUSHFD/PUSHAD/argument on both branches");
    check(MoveAnimHookStackContract::native_pending_argument_bytes==4 &&
        MoveAnimHookStackContract::own_group_pending_argument_bytes==0 &&
        move_anim_own_group_continue==0x5E4C04 && move_anim_native_continue==0x5E4BBF,
        "only native replay leaves PUSH ESI for original ADD ESP,4");
}
}
int main() {
    weak_cell_contract();patch_and_stack_contract();
    if(!failures)std::cout<<"move override weak-reference, patch ownership and hook stack contracts passed\n";
    return failures?1:0;
}
