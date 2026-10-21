#include "lcs/animation.hpp"
#include <iostream>
#include <algorithm>
int main(int argc,char** argv) {
    try {
        if(argc!=2 && !(argc==3 && std::string(argv[2])=="--bindings"))throw std::runtime_error("Usage: lcs-ifp-inspect ORIGINAL.ifp [--bindings]");
        const auto archive=lcs::AnimationArchive::load_anpk(argv[1]);
        size_t total=0;
        std::cout<<"block="<<archive.name<<" animations="<<archive.animations.size()<<'\n';
        for(const auto& animation:archive.animations) {
            float duration=0;size_t frames=0;
            std::cout<<"animation="<<animation.name<<" sequences="<<animation.sequences.size()<<" bone_tags=";
            for(const auto& sequence:animation.sequences) {
                std::cout<<sequence.bone_tag<<',';frames+=sequence.keyframes;duration=std::max(duration,sequence.duration);
            }
            total+=frames;std::cout<<" keyframes="<<frames<<" duration_seconds="<<duration<<'\n';
            if(argc==3)for(const auto& sequence:animation.sequences)
                std::cout<<"binding animation="<<animation.name<<" name="<<sequence.name<<" bone_tag="<<sequence.bone_tag<<" keyframes="<<sequence.keyframes<<'\n';
        }
        std::cout<<"total_keyframes="<<total<<" bounded_anpk_validation_passed=true native_animation_played=false game_executed=false\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"ERROR: "<<e.what()<<'\n';return 1;}
}
