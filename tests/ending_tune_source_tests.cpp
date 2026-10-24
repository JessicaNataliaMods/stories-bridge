#include "audio_output_mock.hpp"
#include "sa_ending_tune.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static int16_t read_sample(const std::vector<uint8_t>& pcm,size_t frame,size_t channel){
    int16_t value=0;std::memcpy(&value,pcm.data()+(frame*2+channel)*2,sizeof(value));return value;
}
static bool all_zero(const std::vector<uint8_t>& pcm){return std::all_of(pcm.begin(),pcm.end(),[](uint8_t value){return value==0;});}
static void wake_output_worker(HANDLE completion){
    check(SetEvent(reinterpret_cast<HANDLE>(audio_mock::last_callback))!=0,"mock WinMM callback event wakes the output worker");
    check(WaitForSingleObject(completion,2000)==WAIT_OBJECT_0,"output worker refills a completed PCM block");
}
int main(int argc,char** argv){try {
    if(argc!=2)throw std::runtime_error("Expected original FINALE.VB path");
    using namespace lcs;
    std::ifstream source_file(argv[1],std::ios::binary|std::ios::ate);
    check(bool(source_file) && source_file.tellg()==3440640,"Original VB is readable for PCM comparison");
    std::vector<uint8_t> source_bytes(3440640);source_file.seekg(0);
    source_file.read(reinterpret_cast<char*>(source_bytes.data()),std::streamsize(source_bytes.size()));
    check(bool(source_file),"Original VB PCM comparison input is complete");
    const auto source=decode_cutscene_vb(source_bytes);
    {
        NativeEndingTune tune;check(!tune.ready() && !tune.request(),"Missing stream cannot accept original request");
        tune.load(argv[1]);check(tune.ready() && tune.request(),"Genuine original VB decodes before accepting request");
        for(uint32_t f=1;f<=8;++f)tune.service(f,false,.5f);
        check(audio_mock::opens==1 && audio_mock::writes==3 && audio_mock::pcm_writes.size()==3,
              "Finale starts on its owned output with three prefetched PCM blocks");
        check(audio_mock::rates==0 && audio_mock::volumes==0,
              "Finale mixing does not use shared WinMM hardware rate or volume controls");
        for(size_t i=0;i<3;++i){
            check(audio_mock::headers[i]->dwBufferLength==6400 && audio_mock::headers[i]->dwLoops==0 &&
                !(audio_mock::headers[i]->dwFlags&(WHDR_BEGINLOOP|WHDR_ENDLOOP)),
                "Each stable output header owns one 50 ms stereo PCM block without driver looping");
            check(audio_mock::pcm_writes[i].size()==6400 && all_zero(audio_mock::pcm_writes[i]),
                  "Initial software mute is present in submitted PCM");
            for(size_t j=0;j<i;++j)check(audio_mock::headers[i]!=audio_mock::headers[j],
                                        "Prefilled PCM blocks use distinct stable headers");
        }
        HANDLE completion=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        check(completion!=nullptr,"Create a completion barrier for the mocked output worker");
        audio_mock::completion_event=completion;
        for(uint32_t f=12;f<=28;f+=4)tune.service(f,false,.5f);
        audio_mock::headers[0]->dwFlags|=WHDR_DONE;
        wake_output_worker(completion);
        check(audio_mock::pcm_writes.size()==4 && audio_mock::pcm_writes[3].size()==6400,
              "A completed header is refilled through the production output worker");
        bool nonzero=false;
        for(size_t frame=0;frame<1600;++frame){
            for(size_t channel=0;channel<2;++channel){
                const auto expected=int16_t(std::clamp(std::lround(double(read_sample(source.pcm,4800+frame,channel))*3.0/127.0),-32768l,32767l));
                check(read_sample(audio_mock::pcm_writes[3],frame,channel)==expected,
                      "Live music master gain is mixed into the refilled stereo PCM samples");
                nonzero|=expected!=0;
            }
        }
        check(nonzero,"The gain verification block contains audible source samples");
        tune.service(32,false,0);
        audio_mock::headers[1]->dwFlags|=WHDR_DONE;
        wake_output_worker(completion);
        check(audio_mock::pcm_writes.size()==5 && all_zero(audio_mock::pcm_writes[4]),
              "Live music mute is applied to refilled PCM without touching native globals");
        audio_mock::completion_event=nullptr;CloseHandle(completion);
        check(tune.mode(2)==EndingTuneResult::accepted && !tune.active(),"Cutscene switch retires only owned finale output");
    }
    WAVEHDR* retained=nullptr;
    {
        NativeEndingTune tune;tune.load(argv[1]);tune.request();tune.service(4,false,1);tune.service(8,false,1);
        const auto first_header=audio_mock::headers.size()-3;
        retained=audio_mock::headers[first_header];audio_mock::fail_reset=true;
        check(!tune.shutdown(),"Driver reset failure is reported");
    }
    check(retained->lpData && retained->dwBufferLength==6400,"Failed destruction retains driver-owned header and PCM storage");
    audio_mock::fail_reset=false;
    std::cout<<"PASS: original FINALE decode/software PCM gain and mute/stop/failure lifetime; output mocked, gameExecuted=false\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
