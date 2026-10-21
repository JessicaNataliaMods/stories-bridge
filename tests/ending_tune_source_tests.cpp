#include "audio_output_mock.hpp"
#include "sa_ending_tune.hpp"
#include <iostream>
#include <stdexcept>
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){try {
    if(argc!=2)throw std::runtime_error("Expected original FINALE.VB path");
    using namespace lcs;
    {
        NativeEndingTune tune;check(!tune.ready() && !tune.request(),"Missing stream cannot accept original request");
        tune.load(argv[1]);check(tune.ready() && tune.request(),"Genuine original VB decodes before accepting request");
        for(uint32_t f=1;f<=8;++f)tune.service(f,false,.5f);
        check(audio_mock::opens==1 && audio_mock::last_volume==0,"Finale starts on its owned output with actual silent gain");
        const auto header=audio_mock::headers.back();
        check(header->dwBufferLength==12042240 && header->dwLoops==UINT32_MAX &&
            (header->dwFlags&(WHDR_BEGINLOOP|WHDR_ENDLOOP))==(WHDR_BEGINLOOP|WHDR_ENDLOOP),"Authentic stereo PCM is submitted with loop boundaries");
        for(uint32_t f=12;f<=28;f+=4)tune.service(f,false,.5f);
        const auto half=uint32_t(65535.0*3/127.0);
        check(audio_mock::last_volume==(half|(half<<16)),"Authored6 is scaled by live native music master to3 on both channels");
        tune.service(32,false,0);check(audio_mock::last_volume==0,"Live music mute is applied without touching native globals");
        check(tune.mode(2)==EndingTuneResult::accepted && !tune.active(),"Cutscene switch retires only owned finale output");
    }
    WAVEHDR* retained=nullptr;
    {
        NativeEndingTune tune;tune.load(argv[1]);tune.request();tune.service(4,false,1);tune.service(8,false,1);
        retained=audio_mock::headers.back();audio_mock::fail_reset=true;
        check(!tune.shutdown(),"Driver reset failure is reported");
    }
    check(retained->lpData && retained->dwBufferLength==12042240,"Failed destruction retains original driver-owned header and PCM storage");
    audio_mock::fail_reset=false;
    std::cout<<"PASS: original FINALE decode/owned loop/live master/stop/failure lifetime; output mocked, gameExecuted=false\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
