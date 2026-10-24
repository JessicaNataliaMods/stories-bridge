#include "audio_output_mock.hpp"
#include <iostream>
#include <stdexcept>
#include <cstring>
static void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
static int16_t at(const std::vector<uint8_t>& p,size_t n){int16_t v;std::memcpy(&v,p.data()+n*2,2);return v;}
int main(){try{
    lcs::AudioSample sample;sample.frequency=22050;sample.channels=1;
    for(int16_t frame:{int16_t(12000),int16_t(8000),int16_t(4000),int16_t(0)}){auto* p=reinterpret_cast<uint8_t*>(&frame);sample.pcm.insert(sample.pcm.end(),p,p+2);}
    lcs::MissionSpeech speech;
    auto left=speech.play_owned_effect(sample,lcs::AudioMix{63,0});check(left.accepted,"left voice accepted");
    const auto& pcm=audio_mock::pcm_writes.back();
    check(std::abs(int(at(pcm,0))-5953)<=1 && at(pcm,1)==0,"Per-voice gain/pan must be present in PCM before the driver receives it");
    auto loud=speech.play_owned_effect(sample,lcs::AudioMix{127,63});check(loud.accepted,"second voice accepted");
    check(at(audio_mock::pcm_writes.back(),0)==12000 && at(audio_mock::pcm_writes.back(),1)==12000,"Other voice keeps its own PCM gain");
    check(audio_mock::volumes==0 && audio_mock::rates==0,"No driver-wide volume or rate controls");
    check(speech.stop_owned_effect(*left.voice) && speech.stop_owned_effect(*loud.voice),"Owned release");
    std::cout<<"PASS: independent per-voice PCM volume/pan; zero driver gain/rate writes; no audio device opened\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
