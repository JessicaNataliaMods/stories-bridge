#include "lcs/first_mission.hpp"
#include "lcs/engine_dispatch.hpp"
#include "lcs/audio.hpp"
#include "ScriptCommands.h"
#include <iostream>
#include <cstring>
#include <map>
#include <fstream>
#include <sstream>
#include <set>
using namespace lcs;
static int32_t bits(float x){int32_t b;std::memcpy(&b,&x,4);return b;}
// A deterministic native-world fixture, never linked into the ASI. User inputs,
// travel and audio time are simulated; every original branch still runs in Vm.
static void scenario(const ScmImage& image,const std::filesystem::path& data,const std::filesystem::path& bindings,bool fail,bool movie=false,bool arrival=false,bool skip=false,bool destroy_mission_car=false) {
    Runtime runtime(image);NativeAdapter adapter(image);FirstMission profile(image,runtime,adapter);
    std::ifstream file(bindings);std::string line;
    while(std::getline(file,line)){std::istringstream row(line);int a,b;std::string name;row>>a>>b>>name;adapter.bind_model(a,b,name);}
    adapter.world_ready=true;adapter.text_ready=true;adapter.reuse_native_player=true;
    profile.opening_movie=movie;profile.arrival_movie=arrival;
    if(movie)adapter.bind_cutscene("homswet","L0001");
    if(arrival)adapter.bind_cutscene("intro","L0002");
    std::vector<std::string> movie_order;
    uint32_t movie_duration=74133;
    uint32_t cutscene_load=UINT32_MAX,cutscene_start=UINT32_MAX;unsigned movies_loaded=0,movies_started=0,movies_cleared=0;
    adapter.stored_player_weapon=[](int32_t){return std::optional<int32_t>(55);};
    int32_t zoom=1;
    adapter.camera_ped_zoom=[&](std::optional<int32_t> value){if(value)zoom=*value;return std::optional<int32_t>(zoom);};
    adapter.freeze_ped_zoom=[](bool){return true;};
    uint32_t now=0,playable_at=UINT32_MAX;
    unsigned next_handle=300,vehicles=0,markers=0,locations=0;
    bool companion_in_car=false,companion_entry_task=false,player_in_car=false,player_left_car=false;
    bool died=false,car_destroyed=false,wrecked_before_boarding=false,mission_car_dead_reported=false;
    bool mission_failed_text_seen=false,mission_cleanup_command_seen=false,mission_cleanup_native_seen=false,critical_retry_seen=false;
    int32_t mission_car_handle=-1;
    std::set<int32_t> peds{100},cars;
    adapter.live_ped_identity=[&](int32_t actor)->std::optional<ObjectiveActor>{
        const auto found=peds.find(actor);if(found==peds.end())return {};
        return ObjectiveActor{actor,reinterpret_cast<uintptr_t>(&*found)};
    };
    std::array<uint32_t,2> sound_end{};
    AudioArchive archive(data/"audio");AudioAdapter audio(archive);
    audio.play=[&](size_t slot,const AudioSample& sample){sound_end[slot]=now+uint32_t(sample.pcm.size()*1000ull/(sample.frequency*2ull));return true;};
    audio.finished=[&](size_t slot){return now>=sound_end[slot];};audio.stop=[&](size_t slot){sound_end[slot]=0;};
    const auto executor=[&](const NativeCall& call){
        NativeResult r;r.executed=true;
        if(call.opcode==0x0360 || call.opcode==0x0361 || call.opcode==0x03B0 || call.opcode==0x03B1) {
            if(call.text!="LCSVIC1" || !call.inputs.empty())throw std::runtime_error("Garage fixture received invalid native operand types");
        }
        switch(call.opcode) {
        case 0x02E4:
            if(call.text!="L0001" && call.text!="L0002")throw std::runtime_error("Opening movie did not use its mounted native alias");
            movie_order.push_back(*call.text);movie_duration=call.text=="L0002"?56667:74133;
            cutscene_load=now;++movies_loaded;break;
        case 0x06B9:r.condition=cutscene_load!=UINT32_MAX && now-cutscene_load>=100;break;
        case 0x02E7:cutscene_start=now;++movies_started;break;
        case 0x02E8:r.values[0]=cutscene_start==UINT32_MAX?0:int32_t(((now-cutscene_start)/1000)*1000);break; // native getter truncates seconds before converting back to ms
        case 0x02E9:r.condition=cutscene_start!=UINT32_MAX && now-cutscene_start>=(skip?500:movie_duration);break;
        case 0x02EA:cutscene_load=UINT32_MAX;cutscene_start=UINT32_MAX;++movies_cleared;break;
        case 0x00BA:if(call.text && *call.text=="M_FAIL")mission_failed_text_seen=true;break;
        case 0x0701:r.condition=false;break;
        case 0x056A:r.condition=skip;break;
        case 0x01F5:r.values[0]=100;break;
        case 0x051E:r.values[0]=13;break;
        case 0x00A5:r.values[0]=next_handle++;mission_car_handle=r.values[0];cars.insert(r.values[0]);++vehicles;break;
        case 0x009A:case 0x01C8:case 0x0129:r.values[0]=next_handle++;peds.insert(r.values[0]);if(call.opcode!=0x009A)companion_in_car=true;break;
        case 0x009B:peds.erase(call.inputs[0]);break;
        case 0x00A6:cars.erase(call.inputs[0]);break;
        case 0x0186:
            if(player_left_car){player_left_car=false;playable_at=now;}
            r.values[0]=next_handle++;++markers;break;
        case 0x0187:case 0x018A:case 0x03BC:case 0x0213:r.values[0]=next_handle++;++markers;break;
        case 0x0227:r.values[0]=destroy_mission_car && car_destroyed && call.inputs[0]==mission_car_handle?0:1000;break;
        case 0x0226:r.values[0]=100;break;
        case 0x01BD:r.values[0]=int32_t(now);break;
        case 0x02CE:r.values[0]=bits(30);break;
        case 0x00E2:r.values[0]=0;break;
        case 0x00A0:case 0x00AA:r.values={bits(1100),bits(-500),bits(30)};break;
        case 0x00D9:r.values[0]=cars.empty()?-1:*cars.begin();break;
        case 0x00BF:r.values[0]=12;r.values[1]=0;break;
        case 0x0174:case 0x0172:r.values[0]=bits(90);break;
        case 0x00BC:
            if(call.text && *call.text=="VIC1_10")playable_at=now;
            break;
        // This native schedules the original passenger-entry task. Boarding is
        // asynchronous, so a destroyed vehicle must not teleport the actor in.
        case 0x05CA:companion_entry_task=true;break;
        case 0x0633:
            if(call.inputs[0]==100){player_in_car=false;player_left_car=true;}else companion_in_car=false;
            break;
        case 0x00D8:if(destroy_mission_car && mission_failed_text_seen)mission_cleanup_native_seen=true;break;
        case 0x0118:r.condition=peds.count(call.inputs[0])==0;break;
        case 0x0119:
            r.condition=!cars.count(call.inputs[0]) || (destroy_mission_car && car_destroyed && call.inputs[0]==mission_car_handle);
            if(r.condition && call.inputs[0]==mission_car_handle)mission_car_dead_reported=true;
            break;
        case 0x056E:r.condition=cars.count(call.inputs[0])!=0;break;
        case 0x056D:r.condition=peds.count(call.inputs[0])!=0;break;
        case 0x0256:case 0x03EE:r.condition=!died;break;
        case 0x0117:r.condition=died;break;
        case 0x016B:case 0x02BF:case 0x01F4:r.condition=false;break;
        case 0x0248:r.condition=true;break;
        case 0x0214:r.condition=playable_at!=UINT32_MAX && now-playable_at>=20000;break;
        case 0x01AD:case 0x01AE:case 0x01AF:case 0x01C1:case 0x00B0:case 0x00B1:case 0x03B0:case 0x03B1:r.condition=true;break;
        case 0x00DB:case 0x00DF:case 0x00DD:
            player_in_car=!player_left_car && !car_destroyed && playable_at!=UINT32_MAX && now-playable_at>=4000;
            if(companion_entry_task && !car_destroyed && player_in_car)companion_in_car=true;
            r.condition=call.inputs[0]==100 ? player_in_car:companion_in_car;break;
        case 0x00EC:case 0x00ED:case 0x00EE:case 0x00FE:case 0x00FF:case 0x0100:
            // Simulated player drives/walks to the ORIGINAL supplied destination.
            r.condition=playable_at!=UINT32_MAX && now-playable_at>=15000;
            if(r.condition)++locations;
            break;
        default:
            if(call.predicate || call.outputs)throw std::runtime_error("Unmodeled native return in fixture: "+std::to_string(call.opcode));
        }
        return r;
    };
    adapter.execute=executor;profile.execute=executor;profile.log=[](const std::string& s){std::cout<<s<<'\n';};
    std::set<std::pair<uint32_t,uint16_t>> visited;
    runtime.engine=[&](Vm& vm,Thread& t,uint16_t op){
        if(visited.emplace(t.last_ip,op).second)std::cout<<"command ip="<<t.last_ip<<" "<<opcode_name(op)<<'\n';
        if(op==COMMAND_RESTART_CRITICAL_MISSION)critical_retry_seen=true;
        if(const auto result=lcs::try_engine_adapter(audio,vm,t,op))return *result;
        if(op==COMMAND_MISSION_HAS_FINISHED && destroy_mission_car && mission_failed_text_seen) {
            // Exercise the real Runtime/NativeAdapter cleanup and the original
            // bytecode after it. FirstMission's isolated-session hook normally
            // ends the script here and clears its mission flag for test reuse.
            mission_cleanup_command_seen=true;
            return adapter.dispatch(vm,t,op);
        }
        if(const auto result=lcs::try_engine_adapter(profile,vm,t,op))return *result;
        return adapter.dispatch(vm,t,op);
    };
    adapter.mission_script=[&](const Thread& t){return runtime.is_mission_script(t);};
    profile.start();
    for(now=0;now<1200000;now+=16) {
        if(fail && !died && playable_at!=UINT32_MAX && now-playable_at>=8000) {
            if(!runtime.notify_player_restart())throw std::runtime_error("Original death recovery did not accept player death");
            died=true;
        }
        if(destroy_mission_car && !car_destroyed && playable_at!=UINT32_MAX && now>=playable_at) {
            car_destroyed=true;
            wrecked_before_boarding=companion_entry_task && !companion_in_car && !player_in_car;
        }
        for(const auto& event:runtime.tick(now,16,1000))
            if(event.result==StepResult::unsupported || event.result==StepResult::failed)
                throw std::runtime_error("Original VIC1 blocked at "+std::to_string(event.ip)+": "+event.diagnostic);
        profile.tick(now);
        if(!runtime.script(profile.root()).thread.active)break;
    }
    const bool expected_failure=fail || destroy_mission_car;
    const auto& mission_thread=runtime.script(profile.root()).thread;
    if(vehicles!=1 || playable_at==UINT32_MAX || runtime.on_mission() ||
       (!destroy_mission_car && !profile.finished) ||
       (expected_failure && profile.passed) || (!expected_failure && (markers<3 || !locations))) {
        throw std::runtime_error("Original mission did not reach expected end: passed="+std::to_string(profile.passed)+
            " profileFinished="+std::to_string(profile.finished)+" onMission="+std::to_string(runtime.on_mission())+
            " active="+std::to_string(mission_thread.active)+" ip="+std::to_string(mission_thread.last_ip)+
            " opcode="+opcode_name(mission_thread.last_opcode)+" deadPredicate="+std::to_string(mission_car_dead_reported)+
            " mFail="+std::to_string(mission_failed_text_seen)+" cleanup="+std::to_string(mission_cleanup_native_seen)+
            " ms="+std::to_string(now));
    }
    if(destroy_mission_car && (mission_car_handle<0 || !mission_car_dead_reported || !wrecked_before_boarding ||
       !mission_failed_text_seen || !mission_cleanup_command_seen || !mission_cleanup_native_seen || !critical_retry_seen || mission_thread.active))
        throw std::runtime_error("Original VIC1 destroyed-car flow did not observe a pre-boarding wreck, M_FAIL, native cleanup, critical retry, and natural script return: entryTask="+
            std::to_string(companion_entry_task)+" companionInCar="+std::to_string(companion_in_car)+" playerInCar="+std::to_string(player_in_car)+" deadPredicate="+
            std::to_string(mission_car_dead_reported)+" wreckedBeforeBoarding="+std::to_string(wrecked_before_boarding)+
            " M_FAIL="+std::to_string(mission_failed_text_seen)+" cleanupCommand="+std::to_string(mission_cleanup_command_seen)+
            " cleanupNative="+std::to_string(mission_cleanup_native_seen)+" criticalRetry="+std::to_string(critical_retry_seen)+" active="+std::to_string(mission_thread.active)+
            " ip="+std::to_string(mission_thread.last_ip)+" opcode="+opcode_name(mission_thread.last_opcode));
    const auto expected_movies=arrival?2u:movie?1u:0u;
    if(movies_loaded!=expected_movies || movies_started!=expected_movies || movies_cleared!=expected_movies)throw std::runtime_error("Original opening movie lifecycle did not execute");
    if(arrival && movie_order!=std::vector<std::string>{"L0002","L0001"})throw std::runtime_error("Arrival did not precede office cutscene");
    std::cout<<"PASS original VIC1 "<<(destroy_mission_car?"pre-boarding destroyed-car failure":fail?"failure":"success")<<" route; native world/input/audio fixture only; game not executed\n";
    if(fail) {
        profile.tick(now);profile.tick(now+6000);
        if(!profile.finished)throw std::runtime_error("Mission retried while the native player was still dead");
        died=false;profile.tick(now+6001);
        if(profile.finished || runtime.active_count()!=1)throw std::runtime_error("First-mission retry did not reinitialize after player recovery");
        if(arrival && (runtime.mission_index()!=9 || runtime.script(profile.root()).thread.ip!=image.main_size))throw std::runtime_error("Critical retry replayed the arrival instead of VIC1");
        std::cout<<"PASS first-mission retry waits for native recovery and reruns original initialization\n";
    }
}
int main(int argc,char** argv) {
    try {
        if(argc!=4)throw std::runtime_error("Usage: first-mission-tests MAIN.SCM DATA_ROOT model-bindings.tsv");
        const auto image=ScmImage::load(argv[1]);
        scenario(image,argv[2],argv[3],false);scenario(image,argv[2],argv[3],true);scenario(image,argv[2],argv[3],false,true);
        scenario(image,argv[2],argv[3],false,true,true);scenario(image,argv[2],argv[3],false,true,true,true);
        scenario(image,argv[2],argv[3],true,true,true);scenario(image,argv[2],argv[3],false,false,false,false,true);
        auto bad=image;bad.bytes.back()^=1;Runtime runtime(bad);NativeAdapter adapter(bad);
        try{FirstMission rejected(bad,runtime,adapter);throw std::runtime_error("Bad source profile accepted");}
        catch(const std::runtime_error& e){if(std::string(e.what())=="Bad source profile accepted")throw;}
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
