#include "sa_cutscenes.hpp"
#include "lcs/cutscene_audio.hpp"
#include "lcs/sa_executor.hpp"
#include <bcrypt.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
namespace lcs {
namespace {
std::string sha256(const std::filesystem::path& path) {
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    const auto cleanup=[&](){if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);};
    try {
        if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("Cannot open cutscene resource hash provider");
        DWORD length=0,written=0;
        if(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&length),sizeof(length),&written,0)<0 || !length || length>1048576)throw std::runtime_error("Invalid hash provider size");
        std::vector<uint8_t> object(length);
        if(BCryptCreateHash(algorithm,&hash,object.data(),length,nullptr,0,0)<0)throw std::runtime_error("Cannot create resource hash");
        std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Missing mounted cutscene resource: "+path.string());
        std::array<uint8_t,65536> buffer{};
        while(in){in.read(reinterpret_cast<char*>(buffer.data()),buffer.size());if(in.gcount() && BCryptHashData(hash,buffer.data(),ULONG(in.gcount()),0)<0)throw std::runtime_error("Resource hashing failed");}
        if(!in.eof())throw std::runtime_error("Mounted resource read failed");
        std::array<uint8_t,32> digest{};if(BCryptFinishHash(hash,digest.data(),digest.size(),0)<0)throw std::runtime_error("Resource hash completion failed");
        cleanup();hash=nullptr;algorithm=nullptr;
        std::ostringstream out;out<<std::uppercase<<std::hex<<std::setfill('0');for(auto c:digest)out<<std::setw(2)<<int(c);return out.str();
    }catch(...){cleanup();throw;}
}
bool read(uintptr_t address,void* out,size_t size) {
    SIZE_T actual=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),out,size,&actual) && actual==size;
}
bool valid_hash(const std::string& hash){return hash.size()==64 && std::all_of(hash.begin(),hash.end(),[](char c){return (c>='0' && c<='9') || (c>='A' && c<='F');});}
std::array<std::string,3> row(const std::string& line) {
    std::istringstream input(line);std::array<std::string,3> values;
    if(!std::getline(input,values[0],'\t') || !std::getline(input,values[1],'\t') || !std::getline(input,values[2]) || values[2].find('\t')!=std::string::npos)throw std::runtime_error("Invalid mounted cutscene index row");
    if(!values[2].empty() && values[2].back()=='\r')values[2].pop_back();return values;
}
}
NativeCutscenes::NativeCutscenes(const std::filesystem::path& game,const std::filesystem::path& directory,NativeAdapter& adapter):directory_(directory) {
    std::ifstream index(directory_/"index.tsv");std::string line;
    if(!index || !std::getline(index,line))throw std::runtime_error("Missing mounted cutscene index");
    const auto header=row(line);
    if(header[0]!="LCSCUT1" || !valid_hash(header[1]) || !valid_hash(header[2]) || sha256(game/"anim"/"cuts.img")!=header[1] || sha256(game/"models"/"cutscene.img")!=header[2])throw std::runtime_error("Mounted cutscene archive hash mismatch");
    while(std::getline(index,line)) {
        const auto entry=row(line);
        if(entry[0].empty() || entry[0].size()>8 || !std::all_of(entry[0].begin(),entry[0].end(),[](char c){return (c>='a' && c<='z') || (c>='0' && c<='9') || c=='_';}) || !valid_hash(entry[2]))throw std::runtime_error("Invalid original cutscene identity/hash");
        if(!scenes_.emplace(entry[0],Scene{entry[1],entry[2]}).second)throw std::runtime_error("Duplicate original cutscene index entry");
        const auto audio=directory_/"audio"/(entry[0]+".wav");
        if(sha256(audio)!=entry[2])throw std::runtime_error("Original cutscene audio hash mismatch");
    }
    if(scenes_.empty())throw std::runtime_error("Mounted cutscene index is empty");
    for(const auto& entry:scenes_)adapter.bind_cutscene(entry.first,entry.second.alias);
    adapter.prepare_cutscene_audio=[this](const std::string& name){
        const auto found=scenes_.find(name);if(found==scenes_.end())return false;
        clear();const auto path=directory_/"audio"/(name+".wav");
        if(sha256(path)!=found->second.hash)return false;
        sample_=read_pcm_wave(path);return true;
    };
    adapter.arm_cutscene_audio=[this](){if(sample_.pcm.empty())throw std::runtime_error("Cutscene voice is not prepared");armed_=true;};
    adapter.clear_cutscene_audio=[this](){clear();};
    pause_observer_=std::thread([this](){
        bool paused=false;
        while(!exiting_.load()) {
            std::array<uint8_t,2> flags{};
            if(read(0xB7CB48,flags.data(),flags.size())) {
                const bool next=flags[0] || flags[1];
                // Repeat after opening a fresh voice while already paused.
                if(next || next!=paused)voice_.pause(0,next);paused=next;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
    });
}
NativeCutscenes::~NativeCutscenes(){exiting_=true;if(pause_observer_.joinable())pause_observer_.join();clear();}
void NativeCutscenes::clear(){voice_.stop(0);armed_=false;started_=false;sample_={};}
void NativeCutscenes::service() {
    if(!armed_)return;
    uint8_t running=0,skipped=0;float seconds=0;
    if(!read(0xB5F851,&running,1) || !read(0xB5F854,&skipped,1) || !read(0xBC3F08,&seconds,4))throw std::runtime_error("Cannot read verified native cutscene clock");
    if(skipped){voice_.stop(0);armed_=false;return;}
    if(!started_ && running) {
        // START only arms the voice. SA actually starts its animation/spline on
        // a later frame. Avoid starting dialogue during asynchronous loading.
        if(!std::isfinite(seconds) || seconds<0 || seconds>.5f || !voice_.play(0,sample_))throw std::runtime_error("Cannot start original stereo cutscene voice in sync with native animation");
        started_=true;
    }
    if(started_) {
        const auto result=execute_sa({0x02E9,{},{},0,0,true});
        if(!result.executed)throw std::runtime_error("Cannot query native cutscene completion");
        if(result.condition){voice_.stop(0);armed_=false;}
    }
}
}
