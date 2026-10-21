#pragma once
#include "sa_audio.hpp"
#include "lcs/cutscene_audio.hpp"
#include <fstream>
namespace lcs {
// Only the two verified retail CITY aliases used by continuous selectors40..45.
// Current/pending identities are backed by owned output transitions. This is
// not a replacement for unported radio/ambience selection in MusicManager.
class NativeCityMusic {
    std::unique_ptr<MissionSpeech> output_=std::make_unique<MissionSpeech>();
    std::vector<uint8_t> encoded_;
    ContinuousMusicTrack strip_{0,0,0},club_{0,0,0};
    std::optional<OwnedEffectVoice> voice_;
    std::optional<uint8_t> playing_,next_;
    bool native_selection_pending_=false;
    bool fault_=false;
    ContinuousMusicTrack& state(uint8_t track){return track==17?strip_:club_;}
    bool observe(){
        if(!voice_)return !playing_;
        const auto ended=output_->owned_effect_finished(*voice_);
        if(!ended || *ended){fault_=true;return false;}
        return true;
    }
    bool retire(){
        if(!voice_)return !playing_;
        const auto position=output_->owned_stream_position(*voice_);
        if(!position){fault_=true;return false;}
        auto& track=state(*playing_);
        // The verified non-radio consumer always starts at encoded0. Save
        // observed playback frames; decoder read-ahead never enters this state.
        // Retail167D34 stores the observed position directly. Its manager
        // length field is not necessarily the encoded file duration.
        track.position=*position/32u;track.service_age=0;
        const auto receipt=output_->stop_owned_effect_checked(*voice_);
        if(!receipt.accepted){fault_=true;return false;}
        voice_.reset();playing_.reset();return true;
    }
public:
    ~NativeCityMusic(){if(!shutdown())output_.release();}
    void load(const std::filesystem::path& path,const ContinuousMusicInitialization& source){
        const auto initial=initialize_continuous_music(source);
        if(!initial)throw std::runtime_error("Original music initialization inputs missing or invalid");
        load(path,(*initial)[17],(*initial)[18]);
    }
    void load(const std::filesystem::path& path,ContinuousMusicTrack initial_strip,ContinuousMusicTrack initial_club){
        if(voice_ || next_ || native_selection_pending_)
            throw std::runtime_error("Cannot replace an owned CITY resource");
        std::ifstream file(path,std::ios::binary|std::ios::ate);
        if(!file || file.tellg()!=21954560)throw std::runtime_error("Original CITY.VB resource missing or truncated");
        std::vector<uint8_t> bytes(21954560);file.seekg(0);
        if(!file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size())))throw std::runtime_error("Original CITY.VB read failed");
        // Retail1678AC..1678BC initially writes100 for tracks>=10, unlike
        // the IOP's actual encoded duration600320. Preserve supplied manager
        // state rather than replacing that field with a measured file length.
        if(!initial_strip.length || !initial_club.length)
            throw std::runtime_error("Observed source CITY manager length is zero");
        encoded_=std::move(bytes);
        strip_=initial_strip;club_=initial_club;
    }
    bool ready()const{return !fault_ && !encoded_.empty();}
    bool service_output(bool paused,AudioMix mix){
        if(!voice_)return !playing_;
        if(!observe())return false;
        const auto pause=output_->pause_owned_effect(*voice_,paused);
        if(!pause.accepted){fault_|=pause.entered;return false;}
        const auto volume=output_->update_owned_effect(*voice_,mix,32000,false);
        if(!volume.accepted){fault_|=volume.entered;return false;}
        return true;
    }
    std::optional<ContinuousMusicConfiguration> configure(uint8_t selector,uint32_t actual_random_word){
        if(!ready() || !observe())return {};
        return configure_continuous_music(selector,actual_random_word,
            playing_.value_or(67),int8_t(next_.value_or(67)),strip_,club_);
    }
    // Called by the source selection producer, never by a configuration-only
    // script object. A pending request remains real controller state until its
    // owned transition succeeds or is explicitly cancelled.
    bool request(uint8_t track){
        if(!ready() || (track!=17 && track!=18) || !observe())return false;
        native_selection_pending_=false;next_=track;return true;
    }
    // A successful source-area change outside the two retail CITY areas lets
    // the SA music path own selection again. Retire any owned CITY stream on
    // the next real MusicManager service; do not stop it from the script call.
    bool request_native_selection(){
        if(!ready())return !voice_ && !playing_;
        if(!observe())return false;
        next_.reset();native_selection_pending_=true;return true;
    }
    bool service_selection(AudioMix mix,bool& source_flag_3d8ba1){
        if(!ready() || !observe())return false;
        if(native_selection_pending_){
            if((voice_ || playing_) && !retire())return false;
            native_selection_pending_=false;return true;
        }
        if(!next_)return true;
        if(playing_==next_){next_.reset();return true;}
        if(!retire())return false;
        auto& track=state(*next_);
        auto stream=Ps2MusicStream::open_source_city_track(encoded_,*next_,track.position,track.service_age);
        //169FF0 clears this observed source byte before its actual start result
        // is passed to16AE80. No guessed flag meaning or native SA write.
        source_flag_3d8ba1=false;
        const auto receipt=output_->play_owned_music_stream(std::move(stream),mix);
        if(!receipt.accepted){if(receipt.entered)fault_=true;return false;}
        voice_=receipt.voice;playing_=next_;next_.reset();return true;
    }
    // Invoke only at a proved source MusicManager service, not a game timer.
    void advance_service_ages(){++strip_.service_age;++club_.service_age;}
    const ContinuousMusicTrack& track_state(uint8_t track)const{
        if(track!=17 && track!=18)throw std::runtime_error("Unknown CITY track");
        return track==17?strip_:club_;
    }
    std::optional<uint8_t> playing_track()const{return playing_;}
    std::optional<uint8_t> pending_track()const{return next_;}
    bool native_selection_pending()const{return native_selection_pending_;}
    bool shutdown(){
        next_.reset();native_selection_pending_=false;
        if(!voice_)return output_->shutdown();
        const auto receipt=output_->stop_owned_effect_checked(*voice_);
        if(!receipt.accepted)return false;
        voice_.reset();playing_.reset();return true;
    }
};
}
