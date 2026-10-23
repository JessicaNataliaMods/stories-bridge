#include "lcs/audio.hpp"
#include "lcs/weapon_audio.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
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
// argv[1]: the original PS2 AUDIO folder (read, never played).
// Proves which SA weapon type plays which LCS sample: the SA type -> original
// weapon owner table agrees with the converter's weapon table, every SA type
// has at most one owner, and each owner's samples decode to real audio.
int main(int argc,char** argv){try {
    check(argc==2,"Expected the original PS2 AUDIO folder");
    // 1. Ownership: the owner of an SA type is converted onto that exact type,
    //    and no other original weapon with its own sound shares the type.
    //    Shared SA types (documented in docs/weapons.md): the laser scope uses
    //    the sniper type, the projectile rocket the launcher type, the heli
    //    cannon the minigun type. The latter two have no fire row here.
    const std::set<int> documented_sharers{lcs::lcs_weapon::LaserScope,lcs::lcs_weapon::Rocket,lcs::lcs_weapon::HeliCannon};
    size_t routed=0;
    for(int sa_type=0;sa_type<64;++sa_type) {
        const auto owner=lcs::weapon_owner_for_sa_type(sa_type);
        if(!owner)continue;
        ++routed;
        check(*owner>=0 && *owner<lcs::lcs_weapon::Count && lcs::lcs_to_sa_weapon_type[size_t(*owner)]==sa_type,
              "Owner is not converted onto SA type "+std::to_string(sa_type));
        for(int other=0;other<lcs::lcs_weapon::Count;++other)
            if(other!=*owner && lcs::lcs_to_sa_weapon_type[size_t(other)]==sa_type)
                check(documented_sharers.count(other)==1,"Undocumented second owner of SA type "+std::to_string(sa_type)+": original "+std::to_string(other));
        const auto shot=lcs::weapon_shot_for_sa_type(sa_type);
        check(shot && shot->lcs_weapon==*owner,"Missing or foreign fire row for SA type "+std::to_string(sa_type));
        const auto reload=lcs::weapon_reload_for_sa_type(sa_type);
        check(reload && reload->lcs_weapon==*owner,"Missing or foreign reload row for SA type "+std::to_string(sa_type));
    }
    check(routed==14,"Fourteen SA gun types route to LCS sounds");
    // Continuous weapons stay with SA: flamethrower (37) and minigun (38) have no owner.
    check(!lcs::weapon_owner_for_sa_type(37) && !lcs::weapon_owner_for_sa_type(38),"Looped weapons are not one-shots");
    // 2. Every original firearm that fires one-shots is reachable from some SA type.
    std::set<int> reachable;
    for(int sa_type=0;sa_type<64;++sa_type)if(const auto owner=lcs::weapon_owner_for_sa_type(sa_type))reachable.insert(*owner);
    for(const int lcs_weapon:{lcs::lcs_weapon::Colt45,lcs::lcs_weapon::Python,lcs::lcs_weapon::Shotgun,lcs::lcs_weapon::Spas12,
        lcs::lcs_weapon::StubbyShotgun,lcs::lcs_weapon::Tec9,lcs::lcs_weapon::Uzi,lcs::lcs_weapon::SilencedIngram,lcs::lcs_weapon::Mp5,
        lcs::lcs_weapon::M4,lcs::lcs_weapon::Ruger,lcs::lcs_weapon::SniperRifle,lcs::lcs_weapon::RocketLauncher,lcs::lcs_weapon::M60})
        check(reachable.count(lcs_weapon)==1,"Original firearm without SA type: "+std::to_string(lcs_weapon));
    // 3. The samples exist and are real audio.
    const lcs::AudioArchive archive(lcs::AudioArchive::Ps2Audio{},argv[1]);
    std::cout<<"SA type -> LCS weapon: fire sample (stereo, rate, ms, peak) / reload sample\n";
    for(int sa_type=0;sa_type<64;++sa_type) {
        const auto owner=lcs::weapon_owner_for_sa_type(sa_type);
        if(!owner)continue;
        const auto shot=*lcs::weapon_shot_for_sa_type(sa_type);
        const auto reload=*lcs::weapon_reload_for_sa_type(sa_type);
        const auto left=archive.sample(shot.left_sample);
        check(left.channels==1 && left.frequency>0 && left.pcm.size()>400 && peak(left)>1500,std::string("Fire sample is silent or empty: ")+shot.name);
        check(archive.record(shot.left_sample).loop_end<0,std::string("Fire sample loops: ")+shot.name);
        if(shot.stereo) {
            const auto right=archive.sample(shot.left_sample+1);
            check(right.channels==1 && right.frequency==left.frequency && right.pcm.size()>400 && peak(right)>1500,std::string("Right sample mismatched: ")+shot.name);
            const auto a=left.pcm.size(),b=right.pcm.size();
            check(std::max(a,b)<=std::min(a,b)*2,std::string("Left/right lengths differ too much: ")+shot.name);
        }
        const auto reloaded=archive.sample(reload.sample);
        // Retail SFX_ROCKET_RELOAD is a silent 728-byte placeholder: the rocket launcher reload makes no sound in the original either.
        const bool silent_placeholder=reload.lcs_weapon==lcs::lcs_weapon::RocketLauncher;
        check(reloaded.channels==1 && reloaded.pcm.size()>100 && (silent_placeholder?peak(reloaded)==0:(reloaded.pcm.size()>400 && peak(reloaded)>1500)),
              std::string("Reload sample is silent or empty: ")+shot.name+" size="+std::to_string(reloaded.pcm.size())+" peak="+std::to_string(peak(reloaded))+" sfx#"+std::to_string(reload.sample));
        // A fixed playback rate must stay near the sample's own rate (the
        // original picks it per weapon around the recorded one).
        if(shot.fixed_frequency)check(shot.fixed_frequency>int32_t(left.frequency)/4 && shot.fixed_frequency<int32_t(left.frequency)*4,
                                      std::string("Fixed rate far from the sample rate: ")+shot.name);
        std::cout<<"  SA "<<sa_type<<" -> "<<shot.name<<": sfx#"<<shot.left_sample<<(shot.stereo?" st ":" mono ")<<left.frequency<<"Hz "
                 <<left.pcm.size()*500/left.frequency<<"ms peak"<<peak(left)<<" fixed="<<shot.fixed_frequency
                 <<" / reload sfx#"<<reload.sample<<' '<<reloaded.frequency<<"Hz "<<reloaded.pcm.size()*500/reloaded.frequency<<"ms rate="<<reload.fixed_frequency<<'\n';
    }
    lcs::AudioAdapter adapter(archive);
    adapter.spatial_listener=[](){return std::optional<lcs::SpatialListener>(lcs::SpatialListener{{0,0,0},{1,0,0}});};
    adapter.play_positioned_effect=[](const lcs::AudioSample&,lcs::AudioMix){return true;};
    adapter.post_world_effect=[](lcs::AudioSample,lcs::AudioMix){return false;};
    const auto spec=lcs::weapon_shot_for_sa_type(22);
    check(spec && adapter.play_weapon_shot(*spec,{0,0,0})==lcs::AudioAdapter::WorldSound::refused,
          "A full or failed shared mixer must retain the native shot");
    const auto reload_spec=lcs::weapon_reload_for_sa_type(22);
    check(reload_spec && adapter.play_weapon_reload(*reload_spec,{0,0,0})==lcs::AudioAdapter::WorldSound::refused,
          "A full or failed shared mixer must retain the native reload");
    std::cout<<"PASS: 14 SA gun types own exactly one LCS weapon sound each; samples decode; gameExecuted=false\n";
    return 0;
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
