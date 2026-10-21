#include "lcs/cutscene_audio.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc,char** argv) {
    try {
        if(argc!=3)throw std::runtime_error("Usage: lcs-cutscene-audio ORIGINAL.VB OUTPUT.wav");
        std::ifstream input(argv[1],std::ios::binary|std::ios::ate);
        if(!input)throw std::runtime_error("PS2 cutscene audio is missing");
        const auto size=input.tellg();
        if(size<=0 || size>32*1024*1024)throw std::runtime_error("Invalid cutscene stream size");
        std::vector<uint8_t> bytes(static_cast<size_t>(size));input.seekg(0);
        if(!input.read(reinterpret_cast<char*>(bytes.data()),size))throw std::runtime_error("Incomplete cutscene stream read");
        const auto sample=lcs::decode_cutscene_vb(bytes);lcs::write_pcm_wave(argv[2],sample);
        std::cout<<"frequency="<<sample.frequency<<" channels="<<sample.channels<<" pcm_bytes="<<sample.pcm.size()
                 <<" duration_seconds="<<double(sample.pcm.size())/(sample.frequency*sample.channels*2)
                 <<" decoded=true audio_played=false game_executed=false\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"ERROR: "<<e.what()<<'\n';return 1;}
}
