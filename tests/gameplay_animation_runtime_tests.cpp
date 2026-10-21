#include "lcs/sa_executor.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <filesystem>
#include <map>
static bool profile=true,resident=false,controlled=true,load_ok=true,play_ok=true,fade_ok=true;
static int status=4;static size_t loads=0,plays=0,fades=0,loop_releases=0;
static std::string last_animation;static uint16_t last_flags=0;static size_t gesture_loads=0,gesture_plays=0;
static bool bundled=false,evict_on_remove=true;
struct FixtureAnimBlock {uintptr_t block;int32_t index,start,count;bool resident;int references;};
static std::map<std::string,FixtureAnimBlock> fixture_blocks;
static int32_t fixture_next_gesture_range=1700;
static uintptr_t fixture_load(const char* path,const char* alias,int animations,int32_t* index,bool owned) {
    if(bundled && std::filesystem::path(path).extension()!=".bin")throw std::runtime_error("Modloader must not auto-register mission IFPs");
    if(std::string(alias)=="LG00") {
        if(!bundled || animations<50)throw std::runtime_error("Complete gesture storage bundle required");
        auto found=fixture_blocks.find(alias);
        if(found!=fixture_blocks.end()) {
            if(!owned || found->second.count!=animations || found->second.block!=901 || found->second.index!=155)return 0;
            if(!found->second.resident){++gesture_loads;found->second.resident=true;}
            ++found->second.references;*index=found->second.index;return found->second.block;
        }
        if(owned)return 0;
        ++gesture_loads;*index=155;fixture_blocks.emplace(alias,FixtureAnimBlock{901,155,fixture_next_gesture_range,animations,true,1});return 901;
    }
    if(std::string(alias)!="LN00" || animations!=7)throw std::runtime_error("RIOT must load complete original block");
    auto found=fixture_blocks.find(alias);
    if(found!=fixture_blocks.end()) {
        if(!owned || found->second.count!=animations || found->second.block!=900 || found->second.index!=100)return 0;
        ++loads;found->second.resident=true;++found->second.references;resident=true;*index=found->second.index;return found->second.block;
    }
    ++loads;if(!load_ok)return 0;*index=100;resident=true;fixture_blocks.emplace(alias,FixtureAnimBlock{900,100,1600,animations,true,1});return 900;
}
static bool fixture_probe(const char* alias,bool* readable,bool* exists,bool* loaded,uintptr_t* block,int32_t* index,int32_t* start,int32_t* count) {
    if(!readable||!exists||!loaded||!block||!index||!start||!count)return false;
    *readable=true;auto found=fixture_blocks.find(alias);
    if(found==fixture_blocks.end()){*exists=false;*loaded=false;*block=0;*index=-1;*start=-1;*count=0;return true;}
    *exists=true;*loaded=found->second.resident;*block=found->second.block;*index=found->second.index;
    *start=found->second.start;*count=found->second.count;return true;
}
static std::optional<bool> fixture_resident(uintptr_t block) {
    for(const auto& entry:fixture_blocks)if(entry.second.block==block)return entry.second.resident;
    return {};
}
static bool fixture_acquire(int32_t index) {
    for(auto& entry:fixture_blocks)if(entry.second.index==index){++entry.second.references;return true;}
    return true;
}
static void fixture_release(int32_t index) {
    for(auto& entry:fixture_blocks)if(entry.second.index==index && entry.second.references>0)--entry.second.references;
}
static uintptr_t fixture_play(int32_t actor,uintptr_t block,const char* animation,uint16_t flags,bool blend) {
    if(block==901 && !blend){last_animation=animation;++gesture_plays;return 2000+gesture_plays;}
    if(actor!=777 || block!=900 || !blend)throw std::runtime_error("Riot uses original actor, named block and 3.0 blend path");
    ++plays;last_animation=animation;last_flags=flags;return play_ok?1000+plays:0;
}
static bool fixture_fade(int32_t actor,uintptr_t) {if(actor!=777)throw std::runtime_error("Wrong actor cleared");++fades;return fade_ok;}
static int fixture_status(int32_t actor,uintptr_t hierarchy,bool release) {
    if(actor!=777)throw std::runtime_error("Wrong RIOT actor queried");
    if(release && hierarchy)++loop_releases;return status;
}
#define LCS_GAMEPLAY_ANIMATION_LOAD fixture_load
#define LCS_GAMEPLAY_ANIMATION_PROBE fixture_probe
#define LCS_GAMEPLAY_ANIMATION_PLAY fixture_play
#define LCS_GAMEPLAY_ANIMATION_FADE fixture_fade
#define LCS_GAMEPLAY_ANIMATION_RELEASE(index) fixture_release(index)
#define LCS_GAMEPLAY_ANIMATION_RESIDENT(block) fixture_resident(block)
#define LCS_GAMEPLAY_ANIMATION_ACQUIRE(index) fixture_acquire(index)
#define LCS_GAMEPLAY_RIOT_STATUS fixture_status
#include "../src/sa_gameplay.cpp"
namespace lcs {
bool sa_executor_profile_matches(){return profile;}
std::optional<bool> sa_char_in_control(int32_t actor){return actor==777?std::optional<bool>(controlled):std::nullopt;}
NativeResult execute_sa(const NativeCall& call) {
    NativeResult r;r.executed=true;
    if(call.opcode==0x04EE)r.condition=resident;
    else if(call.opcode==0x04EF && evict_on_remove) {
        resident=false;auto found=fixture_blocks.find("LN00");if(found!=fixture_blocks.end())found->second.resident=false;
    }
    else if(call.opcode==0x04EF){}
    else throw std::runtime_error("Unexpected native operation");return r;
}
}
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){try {
    check(argc==2,"Animation directory required");
    bundled=std::filesystem::exists(std::filesystem::path(argv[1])/"LG00.ifp.bin");
    {
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
        fixture_blocks["LG00"]={999,154,1650,60,true,0};
        check(!animations.request(72,true),"Unreceipted foreign LG00 registration is refused");
        fixture_blocks.erase("LG00");
        check(!animations.play(777,46,0,false),"Unrequested logical group remains unavailable");
        check(animations.request(38,true) && animations.play(777,38,0,false) && gesture_loads==1,"First logical group loads storage once");
        check(!animations.play(777,46,0,false),"Sharing a physical bundle must not request another logical group");
        check(animations.request(46,true) && animations.play(777,46,0,false) && gesture_loads==1,"Second logical group reuses same native block");
        check(animations.request(38,false) && !animations.play(777,38,0,false) && animations.play(777,46,0,false),"Unloading one logical group preserves another group's permission");
        status=5;check(animations.waiting(777)==true,"Live script pose excludes selection");
        status=4;check(animations.waiting(777)==false,"SA-finished pose stops blocking even while its owner registry remains");
        status=0;check(!animations.waiting(777).has_value(),"Failed native association observation stays unknown");
        status=5;
        check(animations.request(72,true) && animations.request(16,true),"VIC2 and taxi logical groups share the registered bundle");
    }
    }
    if(bundled) {
        const auto resident_gesture_loads=gesture_loads;
        {
            lcs::NativeGameplayAnimations after_owner_destroy(argv[1]);
            check(after_owner_destroy.request(72,true) && after_owner_destroy.request(16,true),"VIC2 and taxi reacquire after Campaign owner destruction");
            check(gesture_loads==resident_gesture_loads,"Reacquiring resident LG00 does not register/load it twice");
            check(after_owner_destroy.play(777,72,0,false) && last_animation=="VIC2_point_angry","Recreated mission consumer plays the actual original VIC2 gesture");
            check(after_owner_destroy.play(777,16,100,false) && last_animation=="IDLE_taxi","Recreated ambient consumer plays the actual original taxi gesture");
        }
        fixture_blocks.at("LG00").resident=false;
        {
            lcs::NativeGameplayAnimations after_unload(argv[1]);
            check(after_unload.request(72,true) && after_unload.request(16,true),"VIC2 and taxi reload an owned unloaded range");
            check(gesture_loads==resident_gesture_loads+1,"Owned unloaded LG00 reloads once in its original range");
        }
        fixture_blocks.erase("LG00");fixture_next_gesture_range=1800;
        {
            lcs::NativeGameplayAnimations after_registry_reset(argv[1]);
            check(after_registry_reset.request(72,true) && after_registry_reset.request(16,true),"Missing native registration can be registered again from the owned package");
            check(fixture_blocks.at("LG00").start==1800 && gesture_loads==resident_gesture_loads+2,"Registry reset records the new native range");
        }
    }
    std::cout<<"PASS: original RIOT resources and native lifecycle, load/play/fade failure, no per-frame restart, finish selection, attack/flee/delete, clear/remove; gameExecuted=false\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
