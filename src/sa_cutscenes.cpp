#include "sa_cutscenes.hpp"
#include "lcs/cutscene_audio.hpp"
#include "lcs/sa_executor.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
namespace lcs {
namespace {
std::filesystem::path source_stream(const std::filesystem::path& directory,std::string name) {
    for(auto& c:name)if(c>='a' && c<='z')c=char(c-32);
    return directory/(name+".VB");
}
bool read(uintptr_t address,void* out,size_t size) {
    SIZE_T actual=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),out,size,&actual) && actual==size;
}
std::array<std::string,3> row(const std::string& line) {
    std::istringstream input(line);std::array<std::string,3> values;
    if(!std::getline(input,values[0],'\t') || !std::getline(input,values[1],'\t') || !std::getline(input,values[2]) || values[2].find('\t')!=std::string::npos)throw std::runtime_error("Invalid mounted cutscene index row");
    if(!values[2].empty() && values[2].back()=='\r')values[2].pop_back();return values;
}
}
NativeCutscenes::NativeCutscenes(const std::filesystem::path&,const std::filesystem::path& directory,const std::filesystem::path& audio_directory,NativeAdapter& adapter):directory_(directory),audio_directory_(audio_directory) {
    std::ifstream index(directory_/"index.tsv");std::string line;
    if(!index || !std::getline(index,line))throw std::runtime_error("Missing mounted cutscene index");
    const auto header=row(line);
    if(header[0]!="LCSCUT1")throw std::runtime_error("Invalid mounted cutscene index header");
    while(std::getline(index,line)) {
        const auto entry=row(line);
        if(entry[0].empty() || entry[0].size()>8 || !std::all_of(entry[0].begin(),entry[0].end(),[](char c){return (c>='a' && c<='z') || (c>='0' && c<='9') || c=='_';}) || entry[1].empty())throw std::runtime_error("Invalid mounted cutscene scene/alias");
        if(!scenes_.emplace(entry[0],Scene{entry[1],entry[2]}).second)throw std::runtime_error("Duplicate original cutscene index entry");
        // Decoding every stream here would delay startup; the selected stream
        // is structurally decoded when its voice is prepared. Index hash fields
        // are package metadata and do not pin user-modified archives or audio.
        if(!std::filesystem::is_regular_file(source_stream(audio_directory_,entry[0])))
            throw std::runtime_error("Mounted cutscene audio is missing: "+source_stream(audio_directory_,entry[0]).string());
    }
    if(scenes_.empty())throw std::runtime_error("Mounted cutscene index is empty");
    for(const auto& entry:scenes_)adapter.bind_cutscene(entry.first,entry.second.alias);
    adapter.prepare_cutscene_audio=[this](const std::string& name){
        const auto found=scenes_.find(name);if(found==scenes_.end())return false;
        clear();auto decoded=read_ps2_stream(source_stream(audio_directory_,name));
        if(decoded.pcm.empty() || !decoded.frequency || (decoded.channels!=1 && decoded.channels!=2))return false;
        sample_=std::move(decoded);return true;
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
