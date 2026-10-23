#include "sa_interface_audio.hpp"
#include <windows.h>
#include <algorithm>
#include <cstring>
namespace lcs {
bool sa_interface_audio_profile_matches() {
    constexpr uint8_t report[]={0x81,0xC1,0xB4,0x00,0x00,0x00,0xE9};   // add ecx,0xB4; jmp AddAudioEvent
    __try {
        if(std::memcmp(reinterpret_cast<const void*>(SaInterfaceAudioProfile::report),report,sizeof(report)))return false;
        for(const auto site:SaInterfaceAudioProfile::calls) {
            const auto* code=reinterpret_cast<const uint8_t*>(site);
            int32_t relative=0;std::memcpy(&relative,code+1,4);
            if(code[0]!=0xE8 || uintptr_t(int64_t(site+5)+relative)!=SaInterfaceAudioProfile::report)return false;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool sa_interface_event_calls(uintptr_t hook) {
    if(!sa_interface_audio_profile_matches())return false;
    for(const auto site:SaInterfaceAudioProfile::calls) {
        auto* code=reinterpret_cast<uint8_t*>(site);
        DWORD old=0;
        if(!VirtualProtect(code,5,PAGE_EXECUTE_READWRITE,&old))return false;
        const int32_t relative=int32_t(int64_t(hook)-int64_t(site+5));
        std::memcpy(code+1,&relative,4);
        FlushInstructionCache(GetCurrentProcess(),code,5);
        DWORD ignored=0;VirtualProtect(code,5,old,&ignored);
    }
    return true;
}
const AudioSample* SaInterfaceSounds::sample(uint64_t key,uint32_t a,uint32_t b,bool mixed) {
    const auto found=cache_.find(key);
    if(found!=cache_.end())return &found->second;
    try {
        AudioSample result;
        if(mixed)result=archive_.frontend_effect(int32_t(a));
        else if(b) {
            // LEFT in the left channel, RIGHT in the right one (the original queues them at pan 0 and 127).
            const auto left=archive_.sample(a),right=archive_.sample(b);
            if(left.channels!=1 || right.channels!=1 || left.frequency!=right.frequency)return nullptr;
            result.channels=2;result.frequency=left.frequency;
            result.pcm.assign(std::max(left.pcm.size(),right.pcm.size())*2,0);
            for(size_t i=0;i+1<left.pcm.size();i+=2)std::memcpy(result.pcm.data()+i*2,left.pcm.data()+i,2);
            for(size_t i=0;i+1<right.pcm.size();i+=2)std::memcpy(result.pcm.data()+i*2+2,right.pcm.data()+i,2);
        } else result=archive_.sample(a);
        if(result.pcm.empty() || !result.frequency)return nullptr;
        return &cache_.emplace(key,std::move(result)).first->second;
    } catch(const std::exception&) {return nullptr;}
}
bool SaInterfaceSounds::event(int32_t sa_event) {
    if(!mixer_ || !mixer_->available())return false;
    const auto sound=interface_sound_for_event(sa_event);
    if(!sound)return false;
    const uint32_t now=GetTickCount();
    if(sa_event>=0 && size_t(sa_event)<last_event_.size()) {
        const auto id=size_t(sa_event);
        if(!seen_[id]){seen_[id]=true;if(log_)log_(std::string("Interface event first seen: SA frontend event ")+std::to_string(sa_event)+" -> "+sound->name);}
        // Count events per second; a caller that fires the same event every frame must not open a voice each time.
        if(now-window_start_[id]>=1000){if(window_count_[id]>20 && log_)log_("Interface event "+std::to_string(sa_event)+" fired "+std::to_string(window_count_[id])+" times in one second");window_start_[id]=now;window_count_[id]=0;}
        if(window_count_[id]<65535)++window_count_[id];
        if(now-last_event_[id]<80 && sa_event!=sa_frontend::highlight && sa_event!=sa_frontend::select)return true;   // drop repeats within 80 ms
        last_event_[id]=now;
    }
    // ProcessFrontEnd drops pickup / mission jingles that arrive within 5 audio frames of the last one.
    constexpr uint32_t gap=100;
    if(sound->limit==InterfaceLimit::pickup) {if(now<next_pickup_)return true;next_pickup_=now+gap;}
    else if(sound->limit==InterfaceLimit::mission) {if(now<next_mission_)return true;next_mission_=now+gap;}
    // Decoding and opening the wave device take milliseconds: leave them to the worker thread.
    const InterfaceSound copy=*sound;
    worker_->post([this,copy,sa_event]{play(copy,sa_event);});
    return true;
}
void SaInterfaceSounds::play(const InterfaceSound& sound_ref,int32_t sa_event) {
    const InterfaceSound* sound=&sound_ref;
    uint32_t a=sound->a;
    if(sound->clip==InterfaceClip::random_mono) {random_.regenerate();a+=random_.table(0)%sound->b;}
    const bool mixed=sound->clip==InterfaceClip::frontend_mix;
    const bool pair=sound->clip==InterfaceClip::pair;
    const auto* base=sample((uint64_t(mixed?1:0)<<63)|(uint64_t(a)<<32)|(pair?sound->b:0),a,pair?sound->b:0,mixed);
    if(!base)return;
    AudioSample voice=*base;
    if(sound->frequency>0)voice.frequency=uint32_t(sound->frequency);
    else if(sa_event>=sa_frontend::bullet_pass_left_rear && sa_event<=sa_frontend::bullet_pass_right_front)
        voice.frequency=uint32_t(int32_t(voice.frequency)+random_.displacement(voice.frequency>>5));
    const uint8_t volume=interface_volume; // the shared mixer applies the menu gain once
    const auto frequency=voice.frequency;
    mixer_->play_one_shot(std::make_shared<const AudioSample>(std::move(voice)),frequency,volume,sound->pan);
}
}
