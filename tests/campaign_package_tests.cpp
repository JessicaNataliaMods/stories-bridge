#include "lcs/scm.hpp"
#include "lcs/audio.hpp"
#include "lcs/text.hpp"
#include "lcs/native.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <stdexcept>
using namespace lcs;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){try {
    check(argc==3,"Package and original converted source required");
    const std::filesystem::path package(argv[1]),original(argv[2]),mounted=package/"metadata/lcs-source";
    const auto a=ScmImage::load(original/"DATA/MAIN.SCM"),b=ScmImage::load(mounted/"DATA/MAIN.SCM.bin");
    check(a.bytes==b.bytes && a.mission_offsets==b.mission_offsets,"Packaged original SCM bytes/mission directory differ");
    if(std::filesystem::exists(mounted/"DATA/WEAPON.DAT.bin")) {
        std::ifstream x(original/"DATA/WEAPON.DAT"),y(mounted/"DATA/WEAPON.DAT.bin");
        const std::string source((std::istreambuf_iterator<char>(x)),{}),packaged((std::istreambuf_iterator<char>(y)),{});
        check(!source.empty() && source==packaged,"Original weapon definitions differ from user-converted source");
        std::istringstream definitions(packaged);NativeAdapter adapter(b);adapter.load_weapon_definitions(definitions);
    }
    for(const auto* language:{"american","french","german","italian","spanish"}) {
        const auto source=TextArchive::load(mounted/"TEXT"/(std::string(language)+".gxt.bin"),true);
        TextArchive::load(package/"text"/(std::string(language)+".gxt"),false).require_campaign(source);
    }
    AudioArchive source_audio(original/"audio"),mounted_audio(mounted/"audio",".bin");size_t speech=0;
    for(const auto& name:AudioArchive::mission_names()) {
        const auto x=source_audio.sample(name),y=mounted_audio.sample(name);
        check(x.frequency==y.frequency && x.channels==y.channels && x.pcm==y.pcm,"Packaged original mission speech differs");++speech;
    }
    for(int i=0;i<14;++i) {
        const auto x=source_audio.announcement(i),y=mounted_audio.announcement(i);
        check(x.frequency==y.frequency && x.channels==y.channels && x.pcm==y.pcm,"Packaged original radio news differs");
    }
    std::cout<<"PASS: unchanged original SCM, "<<b.mission_offsets.size()<<" mission entries, five native language banks, "<<speech<<" original speech labels, 14 news streams; opaque source .bin files; gameExecuted=false\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
