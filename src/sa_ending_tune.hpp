#pragma once
#include "sa_audio.hpp"
#include "lcs/ending_tune_transport.hpp"
#include "lcs/cutscene_audio.hpp"
#include <filesystem>
#include <fstream>
#include <memory>
#include <cmath>
namespace lcs {
class NativeEndingTune {
    // Separate output instance: never borrows dialogue, NEWS or phone voices.
    std::unique_ptr<MissionSpeech> music_=std::make_unique<MissionSpeech>();
    AudioSample sample_;
    std::optional<OwnedEffectVoice> voice_;
    EndingTuneTransport transport_;
    float music_scale_=1;
    uint8_t gain(uint8_t authored)const{return uint8_t(float(authored)*music_scale_);}
    EndingTuneOutput output() {
        EndingTuneOutput out;
        out.start=[this](uint8_t gain){
            const auto result=music_->play_owned_effect(sample_,AudioMix{this->gain(gain),63},UINT32_MAX);
            if(result.accepted)voice_=result.voice;
            return EndingTuneEffect{result.entered,result.accepted};
        };
        out.stop=[this](){
            if(!voice_)return EndingTuneEffect{};
            const auto result=music_->stop_owned_effect_checked(*voice_);
            if(result.accepted)voice_.reset();
            return EndingTuneEffect{result.entered,result.accepted};
        };
        out.playing=[this]()->std::optional<bool>{
            if(!voice_)return {};
            const auto finished=music_->owned_effect_finished(*voice_);
            return finished?std::optional<bool>(!*finished):std::nullopt;
        };
        out.gain=[this](uint8_t gain){
            if(!voice_)return EndingTuneEffect{};
            const auto result=music_->update_owned_effect(*voice_,{this->gain(gain),63},0,false);
            return EndingTuneEffect{result.entered,result.accepted};
        };
        return out;
    }
public:
    ~NativeEndingTune(){if(!shutdown())music_.release();} // Driver still owns the original header/PCM on a failed release.
    void load(const std::filesystem::path& path) {
        std::ifstream input(path,std::ios::binary|std::ios::ate);
        if(!input || input.tellg()!=3440640)throw std::runtime_error("Original FINALE.VB resource missing or truncated");
        std::vector<uint8_t> bytes(3440640);input.seekg(0);input.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
        if(!input)throw std::runtime_error("Original FINALE.VB read failed");
        auto decoded=decode_cutscene_vb(bytes);
        if(decoded.frequency!=32000 || decoded.channels!=2 || decoded.pcm.size()!=12042240)
            throw std::runtime_error("Original FINALE PCM format differs");
        sample_=std::move(decoded);
    }
    bool ready()const{return !sample_.pcm.empty() && !transport_.quarantined();}
    bool request(){return ready() && transport_.request_finale();}
    EndingTuneResult mode(int mode){return transport_.request_mode(mode,output());}
    EndingTuneResult service(uint32_t frame,bool paused,float music_scale){
        if(!std::isfinite(music_scale) || music_scale<0 || music_scale>1)return EndingTuneResult::deferred;
        music_scale_=music_scale;return transport_.service(frame,paused,output());
    }
    bool active()const{return transport_.active();}
    bool shutdown() {
        if(!voice_)return true;
        const auto result=music_->stop_owned_effect_checked(*voice_);
        if(result.accepted)voice_.reset();return result.accepted;
    }
};
}
