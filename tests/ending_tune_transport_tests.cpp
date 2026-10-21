#include "lcs/ending_tune_transport.hpp"
#include "lcs/ending_tune_phase_profile.hpp"
#include "lcs/ending_tune_native_settings.hpp"
#include <cstring>
#include <limits>
#include <iostream>
#include <stdexcept>
#include <vector>
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(){try {
    using namespace lcs;
    EndingTuneTransport tune;EndingTuneOutput out;bool playing=false;size_t starts=0,stops=0;
    std::vector<uint8_t> gains;
    out.start=[&](uint8_t gain){++starts;playing=true;gains.push_back(gain);return EndingTuneEffect{true,true};};
    out.stop=[&](){++stops;playing=false;return EndingTuneEffect{true,true};};
    out.playing=[&](){return std::optional<bool>(playing);};
    out.gain=[&](uint8_t gain){gains.push_back(gain);return EndingTuneEffect{true,true};};
    check(tune.request_finale(),"Finale request accepted");
    for(uint32_t f=1;f<8;++f)tune.service(f,false,out);
    check(starts==0 && tune.current_mode()==0,"Mode transition retains early frontend request and waits for next cadence");
    tune.service(8,false,out);check(starts==1 && gains==std::vector<uint8_t>{0},"Actual start is silent");
    tune.service(12,false,out);check(gains.size()==1,"Next cadence verifies actual playback before fade");
    for(uint32_t f=16;f<=24;f+=4)tune.service(f,false,out);
    check(gains.size()==1,"Three serviced frames retain initial latency");
    tune.service(28,false,out);tune.service(28,false,out);check(gains.back()==6 && gains.size()==2,"Gain advances once per actual serviced frame");
    for(uint32_t f=32;f<120;f+=4)tune.service(f,false,out);
    check(tune.volume()==100,"Authored maximum is100");
    playing=false;tune.service(120,false,out);
    check(starts==2 && stops==1 && gains.back()==100,"Retail finale EOF restarts at zero position without resetting envelope");
    check(tune.request_finale(),"Repeated request accepted");tune.service(124,false,out);check(starts==2,"Same active frontend tune is not restarted");
    check(tune.request_mode(1,out)==EndingTuneResult::accepted,"STOP requests GAME1");
    tune.service(125,false,out);check(stops==1,"Game transition waits for cadence4");
    tune.service(128,false,out);check(stops==2 && !tune.active(),"Transition stops the owned stream");
    tune.service(132,false,out);check(tune.current_mode()==1,"Only next cadence commits new mode after observed stop");
    check(tune.request_finale(),"Second request");tune.service(136,false,out);tune.service(140,false,out);
    tune.service(141,true,out);check(tune.current_mode()==4,"Original first paused frontend service switches to DISABLED4");
    tune.service(144,true,out);tune.service(148,true,out);tune.service(152,false,out);
    check(!tune.active() && starts==3,"Pause transition clears frontend request rather than inventing resume");
    tune.request_finale();tune.service(156,false,out);check(tune.active(),"Explicit original request can start again");
    check(tune.request_mode(2,out)==EndingTuneResult::accepted && !tune.active() && tune.current_mode()==2,"CUTSCENE2 stops synchronously");
    tune.request_finale();tune.service(160,false,out);tune.service(164,false,out);
    out.gain=[](uint8_t){return EndingTuneEffect{true,false};};
    for(uint32_t f=168;f<=184;f+=4)tune.service(f,false,out);
    check(tune.quarantined() && tune.volume()==0,"Entered gain failure quarantines without publishing volume");
    const auto effects=starts+stops;tune.service(188,false,out);
    check(!tune.request_finale() && starts+stops==effects,"Quarantined transport never replays effects");
    check(original_ending_tune_phase_profile([](uintptr_t,std::initializer_list<uint8_t>){return true;}),"Reviewed phase accepts matching profile");
    check(!original_ending_tune_phase_profile([](uintptr_t a,std::initializer_list<uint8_t>){return a!=0x53E986;}),"Foreign this ABI refuses phase");
    float master=.5f,fade=.25f;uint8_t paused[2]={0,1};
    auto match=[](uintptr_t,std::initializer_list<uint8_t>){return true;};
    auto read=[&](uintptr_t a,void* p,size_t n){
        if(a==0xB5FCC8)std::memcpy(p,&master,n);else if(a==0xB5FCD0)std::memcpy(p,&fade,n);
        else if(a==0xB7CB48)std::memcpy(p,paused,n);else return false;return true;
    };
    const auto settings=original_ending_tune_native_settings(match,read);
    check(settings && settings->paused && settings->music_scale==.125f,"Live native master and fader multiply without shared writes");
    paused[0]=1;paused[1]=0;check(original_ending_tune_native_settings(match,read)->paused,"Native code pause is included in actual audio pause policy");
    master=0;check(original_ending_tune_native_settings(match,read)->music_scale==0,"Genuine mute remains available");
    master=std::numeric_limits<float>::quiet_NaN();check(!original_ending_tune_native_settings(match,read),"Nonfinite native scale is unavailable, not invented");
    master=1;paused[1]=2;check(!original_ending_tune_native_settings(match,read),"Malformed native pause flag refuses");
    std::cout<<"PASS: original finale mode/cadence/verified fade/EOF/pause/fault ownership; no audio/game executed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
