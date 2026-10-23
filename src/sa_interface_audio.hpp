#pragma once
#include "sa_audio.hpp"
#include "sa_audio_jobs.hpp"
#include "sa_audio_mixer.hpp"
#include <atomic>
#include <memory>
#include "lcs/audio.hpp"
#include "lcs/interface_audio.hpp"
#include <array>
#include <functional>
#include <string>
#include <cstdint>
#include <unordered_map>
namespace lcs {
// SA 1.0 US: CAudioEngine::ReportFrontendAudioEvent (0x506EA0, thiscall(event, volumeBoost, speed) ret 0xC)
// forwards every menu, pickup, HUD and mission jingle event to CAEFrontendAudioEntity::AddAudioEvent. The
// 90 CALLs below (checked by decoding the exe) are redirected; the function itself is left untouched.
struct SaInterfaceAudioProfile {
    static constexpr uintptr_t report=0x506EA0;
    static constexpr std::array<uintptr_t,90> calls{
0x43DB40,0x43DE3A,0x43DF4B,0x44AD28,0x44AE4A,0x44B4E6,0x44B552,0x44B614,0x44CBA2,0x4552DF,0x45539F,0x456575,0x4565BF,0x456637,0x4566BD,0x45675A,0x456C6E,0x457E98,0x457F02,0x45803E,0x458079,0x458231,0x4586F8,0x49C5B0,0x4D7234,0x4E6C40,0x4E7014,0x4E7027,0x4E76D8,0x4E76F9,0x4E9CF0,0x4EA5E5,0x4EB463,0x4EB62B,0x4EB7CD,0x4EB7F1,0x4EB847,0x4EB859,0x4EB8DC,0x4EB8EF,0x4EB92F,0x4EB954,0x4EBB3B,0x4ED79D,0x4ED7BA,0x4ED7D7,0x4ED947,0x4ED964,0x4ED981,0x4ED99E,0x4EE587,0x4EE5A4,0x4EE5C1,0x4EE5DE,0x4EE5FB,0x4EE618,0x52B716,0x5737B8,0x576C18,0x576C67,0x576F33,0x577579,0x577595,0x5788CB,0x578BD1,0x57B4D3,0x57B574,0x57B6CE,0x57B731,0x57B745,0x57DB6A,0x57DC73,0x57DCE5,0x57E666,0x57F294,0x57FFCD,0x580349,0x5805FB,0x5808A0,0x58091D,0x5809FE,0x580ACD,0x58B81F,0x60F2AB,0x723B84,0x723B99,0x723BE1,0x723BF6,0x7391E2,0x73A1A5};
};
bool sa_interface_audio_profile_matches();
bool sa_interface_event_calls(uintptr_t hook);

// Plays the LCS sound of the equivalent frontend event on a private voice pool; events without an LCS
// equivalent (or when no voice is free) stay with SA.
class SaInterfaceSounds {
    const AudioArchive& archive_;
    std::shared_ptr<SaAudioMixer> mixer_;
    SourceAudioRandom random_;
    std::unordered_map<uint64_t,AudioSample> cache_;
    uint32_t next_pickup_=0,next_mission_=0;
    std::array<uint32_t,64> last_event_{},window_start_{};
    std::array<uint16_t,64> window_count_{};
    std::array<bool,64> seen_{};
    std::function<void(const std::string&)> log_;
    const AudioSample* sample(uint64_t key,uint32_t a,uint32_t b,bool mixed);
    void play(const InterfaceSound& sound,int32_t sa_event);   // worker thread: decodes and opens the wave device
    std::unique_ptr<AudioWorker> worker_;                      // last member
public:
    SaInterfaceSounds(const AudioArchive& archive,std::shared_ptr<SaAudioMixer> mixer):archive_(archive),mixer_(std::move(mixer)),worker_(std::make_unique<AudioWorker>(nullptr,std::chrono::milliseconds(1000))){}
    ~SaInterfaceSounds(){worker_.reset();}
    void set_effects_scale(float value){if(mixer_)mixer_->set_effects_scale(value);}
    void set_logger(std::function<void(const std::string&)> value){log_=std::move(value);}
    // True: LCS played (or dropped it on purpose); SA must skip the event.
    bool event(int32_t sa_event);
};
}
