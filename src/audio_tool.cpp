#include "lcs/audio.hpp"
#include <iostream>
#include <stdexcept>
int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::runtime_error("Usage: lcs-audio-inspect converted/audio");
        lcs::AudioArchive archive(argv[1]);size_t count=0;uint64_t bytes=0;
        for(const auto& name:lcs::AudioArchive::mission_names()) {
            const auto sample=archive.sample(name);++count;bytes+=sample.pcm.size();
            if(name.rfind("VIC1_",0)==0)std::cout<<"label="<<name<<" frequency="<<sample.frequency<<" duration_ms="<<1000ull*sample.pcm.size()/2/sample.frequency<<'\n';
        }
        std::cout<<"sample_index_records="<<archive.sample_count()<<"\nmission_audio_labels="<<count<<"\nvalidated_pcm_bytes="<<bytes
                 <<"\nplayback_executed=false\ngame_executed=false\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
