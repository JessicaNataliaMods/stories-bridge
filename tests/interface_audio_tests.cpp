#include "lcs/audio.hpp"
#include "lcs/interface_audio.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
void check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
int peak(const lcs::AudioSample& sample) {
    int value=0;
    for(size_t i=0;i+1<sample.pcm.size();i+=2){int16_t v;std::memcpy(&v,sample.pcm.data()+i,2);value=std::max(value,std::abs(int(v)));}
    return value;
}
}
// argv[1]: the original PS2 AUDIO folder. Every SA frontend event with an LCS equivalent plays real audio.
int main(int argc,char** argv){try {
    check(argc==2,"Expected the original PS2 AUDIO folder");
    const lcs::AudioArchive archive(lcs::AudioArchive::Ps2Audio{},argv[1]);
    size_t mapped=0;std::set<std::string> names;
    for(int event=0;event<lcs::sa_frontend::count;++event) {
        const auto sound=lcs::interface_sound_for_event(event);
        if(!sound)continue;
        ++mapped;names.insert(sound->name);
        const auto decode=[&](uint32_t index,const char* what){
            lcs::AudioSample s;
            try {s=archive.sample(index);} catch(const std::exception& e) {throw std::runtime_error(std::string(what)+" "+std::to_string(index)+": "+e.what());}
            check(s.channels==1 && s.pcm.size()>100 && peak(s)>500,std::string("Silent interface sample for ")+sound->name+" #"+std::to_string(index));
            return s;
        };
        switch(sound->clip) {
        case lcs::InterfaceClip::mono:decode(sound->a,"mono");break;
        case lcs::InterfaceClip::pair: {
            const auto l=decode(sound->a,"left"),r=decode(sound->b,"right");
            check(l.frequency==r.frequency,std::string("Pair rates differ: ")+sound->name);break;}
        case lcs::InterfaceClip::random_mono:for(uint32_t i=0;i<sound->b;++i)decode(sound->a+i,"random");break;
        case lcs::InterfaceClip::frontend_mix: {
            const auto mix=archive.frontend_effect(int32_t(sound->a));
            check(mix.channels==2 && peak(mix)>500,std::string("Silent frontend mix: ")+sound->name);break;}
        }
        std::cout<<"  SA frontend event "<<event<<" -> "<<sound->name<<'\n';
    }
    check(mapped>=28,"The interface table covers menu, pickups, HUD and jingles");
    // Events that must stay with SA: loading tune, scanner, amplifier, frenzy, retune.
    for(const int event:{21,22,23,24,31,34,35,36,39,46,47,48,49,50,51})check(!lcs::interface_sound_for_event(event),"Event stays with SA: "+std::to_string(event));
    std::cout<<"PASS: "<<mapped<<" SA frontend events play LCS interface samples ("<<names.size()<<" distinct sounds); gameExecuted=false\n";
    return 0;
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
