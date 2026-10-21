#include "lcs/sa_executor.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <filesystem>
static bool profile=true,resident=false,controlled=true,load_ok=true,play_ok=true,fade_ok=true;
static int status=4;static size_t loads=0,plays=0,fades=0,loop_releases=0;
static std::string last_animation;static uint16_t last_flags=0;static size_t gesture_loads=0,gesture_plays=0;
static bool bundled=false,evict_on_remove=true;
static uintptr_t fixture_load(const char* path,const char* alias,int animations,int32_t* index,bool) {
    if(bundled && std::filesystem::path(path).extension()!=".bin")throw std::runtime_error("Modloader must not auto-register mission IFPs");
    if(std::string(alias)=="LG00") {
        if(!bundled || animations<50)throw std::runtime_error("Complete gesture storage bundle required");
        ++gesture_loads;*index=155;return 901;
    }
    if(std::string(alias)!="LN00" || animations!=7)throw std::runtime_error("RIOT must load complete original block");
    ++loads;if(!load_ok)return 0;*index=100;resident=true;return 900;
}
static uintptr_t fixture_play(int32_t actor,uintptr_t block,const char* animation,uint16_t flags,bool blend) {
    if(block==901 && !blend){++gesture_plays;return 2000+gesture_plays;}
    if(actor!=777 || block!=900 || !blend)throw std::runtime_error("Riot uses original actor, named block and 3.0 blend path");
    ++plays;last_animation=animation;last_flags=flags;return play_ok?1000+plays:0;
}
static bool fixture_fade(int32_t actor,uintptr_t) {if(actor!=777)throw std::runtime_error("Wrong actor cleared");++fades;return fade_ok;}
static int fixture_status(int32_t actor,uintptr_t hierarchy,bool release) {
    if(actor!=777)throw std::runtime_error("Wrong RIOT actor queried");
    if(release && hierarchy)++loop_releases;return status;
}
#define LCS_GAMEPLAY_ANIMATION_LOAD fixture_load
#define LCS_GAMEPLAY_ANIMATION_PLAY fixture_play
#define LCS_GAMEPLAY_ANIMATION_FADE fixture_fade
#define LCS_GAMEPLAY_ANIMATION_RELEASE(index) ((void)(index))
#define LCS_GAMEPLAY_ANIMATION_RESIDENT(block) std::optional<bool>(resident)
#define LCS_GAMEPLAY_ANIMATION_ACQUIRE(index) true
#define LCS_GAMEPLAY_RIOT_STATUS fixture_status
#include "../src/sa_gameplay.cpp"
namespace lcs {
bool sa_executor_profile_matches(){return profile;}
std::optional<bool> sa_char_in_control(int32_t actor){return actor==777?std::optional<bool>(controlled):std::nullopt;}
NativeResult execute_sa(const NativeCall& call) {
    NativeResult r;r.executed=true;
    if(call.opcode==0x04EE)r.condition=resident;
    else if(call.opcode==0x04EF && evict_on_remove)resident=false;
    else if(call.opcode==0x04EF){}
    else throw std::runtime_error("Unexpected native operation");return r;
}
}
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){try {
    check(argc==2,"Animation directory required");
    bundled=std::filesystem::exists(std::filesystem::path(argv[1])/"LG00.ifp.bin");
    lcs::NativeGameplayAnimations animations(argv[1]);
    check(animations.waiting(777)==false,"Native ambient SA behavior is not fabricated as a script wait");
    check(!animations.play(777,29,0,false) && loads==0,"Unrequested original RIOT block cannot silently succeed");
    load_ok=false;check(!animations.named("riot",0),"Native named-block load failure remains explicit");load_ok=true;
    check(animations.named("riot",0)==true && animations.named("riot",1)==true && loads==2,"Original request/residency lifecycle");
    check(animations.play(777,29,0,false) && plays==0,"SET_WAIT_STATE initializes without inventing a timed replacement task");
    check(animations.waiting(777)==true,"Pending script RIOT excludes selection before the first animation cycle");
    check(animations.tick(1000) && plays==1 && (last_flags&16),"First Wait tick uses a real original partial animation");
    auto previous=last_animation;status=5;
    check(animations.tick(1001) && plays==1,"Live association is not restarted every frame");
    check(animations.play(777,29,100,false) && animations.play(777,19,100,false) && plays==1 && fades==0,"Repeated SET and other wait states cannot replace an active riot");
    check(animations.tick(10000) && loop_releases>0 && plays==1,"Elapsed duration removes repeat, lets current animation finish");
    status=4;check(animations.tick(10001) && plays==2 && last_animation!=previous,"Fading or finished animation selects a different original gesture");
    status=2;check(animations.tick(10002) && fades==1,"Attack/flee retires only owned riot pose");
    check(animations.play(777,29,-1,true),"Negative script duration does not suppress original persistent riot");
    status=3;check(animations.tick(10003) && plays==2,"Out-of-control actor pauses without stopping the campaign");
    status=1;check(animations.tick(10004),"Deleted actor retires state without touching reused handles");
    check(animations.play(777,29,0,false),"Riot can be restarted after genuine cleanup");
    status=4;play_ok=false;check(!animations.tick(10005),"Failed animation execution cannot report success");play_ok=true;
    check(animations.tick(10006),"Recoverable backend test can resume actual animation");
    fade_ok=false;check(!animations.clear(777),"Failed native fade remains explicit");fade_ok=true;
    check(animations.named("riot",2)==true && !resident && fades==3,"Remove block clears owned riot before native release");
    const auto previous_loads=loads;
    check(animations.named("riot",0)==true && loads==previous_loads+1,"Evicted private block reloads into its owned registry slot");
    evict_on_remove=false;const auto resident_loads=loads;
    check(animations.named("riot",2)==true && animations.named("riot",0)==true && loads==resident_loads,"Resident private block reacquires reference without loading or registering twice");
    profile=false;check(!animations.play(777,29,0,false),"Unsupported executable profile cannot animate");profile=true;
    if(bundled) {
        check(!animations.play(777,46,0,false),"Unrequested logical group remains unavailable");
        check(animations.request(38,true) && animations.play(777,38,0,false) && gesture_loads==1,"First logical group loads storage once");
        check(!animations.play(777,46,0,false),"Sharing a physical bundle must not request another logical group");
        check(animations.request(46,true) && animations.play(777,46,0,false) && gesture_loads==1,"Second logical group reuses same native block");
        check(animations.request(38,false) && !animations.play(777,38,0,false) && animations.play(777,46,0,false),"Unloading one logical group preserves another group's permission");
        status=5;check(animations.waiting(777)==true,"Live script pose excludes selection");
        status=4;check(animations.waiting(777)==false,"SA-finished pose stops blocking even while its owner registry remains");
        status=0;check(!animations.waiting(777).has_value(),"Failed native association observation stays unknown");
        status=5;
    }
    std::cout<<"PASS: original RIOT resources and native lifecycle, load/play/fade failure, no per-frame restart, finish selection, attack/flee/delete, clear/remove; gameExecuted=false\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
