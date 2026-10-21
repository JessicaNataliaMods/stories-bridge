#include "lcs/animation.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sstream>
int main(int argc,char** argv) {
    try {
        if(argc!=3 && argc!=5)throw std::runtime_error("Usage: lcs-ifp-adapt ORIGINAL.ifp OUTPUT.ifp [ORIGINAL_FRAME NATIVE_FRAME | --bone-tags MAP.tsv | --block-name NATIVE_NAME]");
        std::ifstream input(argv[1],std::ios::binary|std::ios::ate);
        if(!input)throw std::runtime_error("Cannot read original animation");
        const auto size=input.tellg();if(size<8 || size>64*1024*1024)throw std::runtime_error("Invalid original animation size");
        std::vector<uint8_t> bytes(static_cast<size_t>(size));input.seekg(0);
        if(!input.read(reinterpret_cast<char*>(bytes.data()),size))throw std::runtime_error("Truncated original animation");
        std::map<std::string,std::string> names;lcs::AnimationBoneMap bones;std::string block_name;
        if(argc==5 && std::string(argv[3])=="--bone-tags") {
            std::ifstream mapping(argv[4]);if(!mapping)throw std::runtime_error("Cannot read verified animation/model bone mapping");
            std::string line;
            while(std::getline(mapping,line)) {
                if(!line.empty() && line.back()=='\r')line.pop_back();
                std::istringstream row(line);std::string animation,frame,number;
                if(!std::getline(row,animation,'\t') || !std::getline(row,frame,'\t') || !std::getline(row,number) || animation.empty() || frame.empty())throw std::runtime_error("Invalid animation bone mapping row");
                size_t used=0;const auto tag=std::stoi(number,&used);
                if(used!=number.size() || !bones.emplace(std::make_pair(animation,frame),tag).second)throw std::runtime_error("Invalid/duplicate animation bone mapping");
            }
        } else if(argc==5 && std::string(argv[3])=="--block-name")block_name=argv[4];
        else if(argc==5)names[argv[3]]=argv[4];
        const auto adapted=lcs::adapt_anpk(bytes,names,bones,block_name);
        if(adapted.tagged_sequences!=bones.size())throw std::runtime_error("Bone mapping references an absent animation sequence");
        if(std::filesystem::exists(argv[2]))throw std::runtime_error("Refusing to overwrite prepared animation");
        std::ofstream output(argv[2],std::ios::binary);output.write(reinterpret_cast<const char*>(adapted.bytes.data()),std::streamsize(adapted.bytes.size()));
        if(!output)throw std::runtime_error("Cannot write complete adapted animation");
        std::cout<<"reordered_sequences="<<adapted.reordered_sequences<<" renamed_sequences="<<adapted.renamed_sequences<<" tagged_sequences="<<adapted.tagged_sequences
                 <<" keyframe_records_preserved=true animation_played=false game_executed=false\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"ERROR: "<<e.what()<<'\n';return 1;}
}
