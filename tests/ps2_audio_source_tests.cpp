#include "lcs/audio.hpp"
#include "lcs/cutscene_audio.hpp"
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#pragma comment(lib,"bcrypt")

namespace {
void check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
std::string sha256(const std::vector<uint8_t>& bytes) {
    std::array<uint8_t,32> digest{};
    if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,const_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),digest.data(),ULONG(digest.size()))<0)
        throw std::runtime_error("SHA-256 failed");
    std::ostringstream out;out<<std::uppercase<<std::hex<<std::setfill('0');for(auto c:digest)out<<std::setw(2)<<int(c);return out.str();
}
}
// Original PS2 AUDIO folder against the converted PC-layout index (argv[2]).
// The converted PCM carried predictor history across samples and wrapped on
// overflow; the runtime reader decodes each voice from key-on and saturates,
// so PCM is compared by length and metadata, not bytes. News streams use the
// prepared-wave decoder and must match exactly; with a cutscene index
// (argv[3]) every decoded stream must hash to its recorded canonical wave.
int main(int argc,char** argv){try {
    check(argc==3 || argc==4,"Expected PS2 AUDIO folder, converted audio folder and optional cutscene index");
    const std::filesystem::path audio(argv[1]);
    const lcs::AudioArchive ps2(lcs::AudioArchive::Ps2Audio{},audio);
    const lcs::AudioArchive converted(argv[2]);
    check(ps2.sample_count()==converted.sample_count(),"Sample counts differ");
    size_t equal=0,engine=0;
    for(uint32_t index=0;index<ps2.sample_count();++index) {
        const auto& expected=converted.record(index);
        try {
            const auto& actual=ps2.record(index);
            check(actual.frequency==expected.frequency && actual.loop_start==expected.loop_start && actual.loop_end==expected.loop_end,
                  "Record metadata differs at "+std::to_string(index));
            // SFX_LIFT_RUNNING: the converted copy kept frames past the loop end.
            check(actual.size==expected.size || (actual.loop_end>=0 && actual.size<expected.size),"Record size differs at "+std::to_string(index));
            ++equal;
        } catch(const std::runtime_error& error) {
            if(std::string(error.what()).find("headerless engine bank")==std::string::npos)throw;
            ++engine;
        }
    }
    check(engine==27,"Only the 27 headerless engine records may lack a source frequency");
    for(const uint32_t index:{0u,4u,183u,290u,586u,4036u,5607u}) {
        const auto sample=ps2.sample(index);
        check(sample.pcm.size()==ps2.record(index).size && sample.frequency==ps2.record(index).frequency,"Decoded sample length differs at "+std::to_string(index));
    }
    size_t labels=0;
    for(const auto& name:lcs::AudioArchive::mission_names()) {
        const auto a=ps2.sample(name),b=converted.sample(name);
        check(a.frequency==b.frequency && !a.pcm.empty() && a.pcm.size()<=b.pcm.size(),"Mission audio label differs: "+name);++labels;
    }
    for(int32_t news=0;news<14;++news) {
        const auto a=ps2.announcement(news),b=converted.announcement(news);
        check(a.pcm==b.pcm && a.frequency==b.frequency && a.channels==b.channels,"News stream differs from its prepared wave");
    }
    size_t scenes=0;
    if(argc==4) {
        std::ifstream index(argv[3]);std::string line;
        check(bool(std::getline(index,line)) && line.rfind("LCSCUT1\t",0)==0,"Invalid cutscene index");
        while(std::getline(index,line)) {
            if(!line.empty() && line.back()=='\r')line.pop_back();
            const auto first=line.find('\t'),second=line.find('\t',first+1);
            check(first!=std::string::npos && second!=std::string::npos,"Invalid cutscene index row");
            auto name=line.substr(0,first);for(auto& c:name)if(c>='a' && c<='z')c=char(c-32);
            const auto decoded=lcs::read_ps2_stream(audio/"CUTSCENE"/(name+".VB"));
            check(sha256(lcs::pcm_wave_bytes(decoded))==line.substr(second+1),"Cutscene stream differs from its recorded wave: "+name);
            ++scenes;
        }
    }
    std::cout<<"PASS: records="<<equal<<" engine_records_refused="<<engine<<" mission_labels="<<labels<<" news=14 cutscenes="<<scenes<<"; gameExecuted=false\n";
    return 0;
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
