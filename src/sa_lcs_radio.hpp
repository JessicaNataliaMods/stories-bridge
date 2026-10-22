#pragma once
#include "sa_audio.hpp"
#include "lcs/lcs_radio.hpp"
#include "lcs/cutscene_audio.hpp"
#include "lcs/continuous_sound.hpp"
#include <windows.h>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <initializer_list>
#include <memory>
namespace lcs {
// Original LCS radio in place of SA's radio audio. SA keeps its own station
// selection, retune, vehicle on/off and HUD logic (CAERadioTrackManager at
// 0x8CB6F8); while an SA station slot is active the matching LCS station
// streams from AUDIO/MUSIC through an owned output, and SA's music master
// (CAEAudioHardware+0x410) is held at zero so its own stream is silent. The
// LCS stream uses the menu radio volume (0xBA6798, 0..64) times SA's music
// fader (+0x418), so menu, cutscene and mission fades still apply. User tracks,
// the emergency scanner and radio off stay native and get the master back.
struct SaRadioState {bool on,paused,retuning;int32_t station,pending;float menu,fader;};
inline bool sa_radio_code_matches(uintptr_t address,std::initializer_list<uint8_t> bytes) {
    __try {return std::memcmp(reinterpret_cast<const void*>(address),bytes.begin(),bytes.size())==0;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline bool sa_radio_profile_matches() {
    // IsRadioOn, the active-station read (in place or moved by an exe patch),
    // the menu radio volume read and the master/fader setters.
    return sa_radio_code_matches(0x4E8350,{0x83,0x79,0x68,0x07,0x75,0x16,0x80,0x39,0x00}) &&
           (sa_radio_code_matches(0x4E83F0,{0x8A,0x81,0xE9,0x00,0x00,0x00}) ||
            (sa_radio_code_matches(0x4E83F0,{0x90,0xE9}) && sa_radio_code_matches(0x405FF5,{0x8A,0x81,0xE9,0x00,0x00,0x00}))) &&
           sa_radio_code_matches(0x53C79F,{0x8A,0x0D,0x98,0x67,0xBA,0x00}) &&
           sa_radio_code_matches(0x4D9530,{0x8B,0x44,0x24,0x04,0x89,0x81,0x10,0x04,0x00,0x00}) &&
           sa_radio_code_matches(0x4D9550,{0x8B,0x44,0x24,0x04,0x89,0x81,0x18,0x04,0x00,0x00});
}
inline bool sa_radio_read(SaRadioState& state) {
    __try {
        state.on=reinterpret_cast<bool(__thiscall*)(uintptr_t)>(0x4E8350)(0x8CB6F8);
        const auto raw=*reinterpret_cast<const uint8_t*>(0x8CB6F8+0xE9);
        state.station=raw==0xFF?13:int32_t(raw);
        // Pending station steps up/down (+0x6C/+0x70, also read by IsRadioOn):
        // SA is retuning until both are consumed.
        const auto up=*reinterpret_cast<const int32_t*>(0x8CB6F8+0x6C),down=*reinterpret_cast<const int32_t*>(0x8CB6F8+0x70);
        state.retuning=state.on && (up!=0 || down!=0);state.pending=up*64+down;
        state.paused=*reinterpret_cast<const uint8_t*>(0xB7CB48)!=0 || *reinterpret_cast<const uint8_t*>(0xB7CB49)!=0;
        state.menu=float(*reinterpret_cast<const int8_t*>(0xBA6798))/64.0f;
        state.fader=*reinterpret_cast<const float*>(0xB5F8B8+0x418);
        return std::isfinite(state.fader) && state.fader>=0 && state.fader<=1 && state.menu>=0 && state.menu<=1;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
inline bool sa_radio_write_master(float value) {
    __try {*reinterpret_cast<float*>(0xB5F8B8+0x410)=value;return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
class SaLcsRadio {
    std::unique_ptr<MissionSpeech> output_=std::make_unique<MissionSpeech>();
    // Retune: the station stream stops, the police-radio crackle loops at a
    // random volume 15..34 and up to two dial clicks follow each press, 800 ms
    // apart (cMusicManager::ServiceGameMode, DoPoliceRadioCrackle,
    // SOUND_RADIO_CHANGE at FRONTEND_VOLUME 127).
    AudioSample crackle_;uint32_t crackle_begin_=0,crackle_end_=0;
    std::array<AudioSample,3> dials_;uint32_t dial_=0;
    std::optional<OwnedEffectVoice> crackle_voice_;
    uint32_t static_counter_=5,static_timer_=0;int32_t last_pending_=0;uint32_t random_=0x12345678;
    // ServiceTrack fade-in after a station starts: 10 frames, then +6 per frame to 100.
    uint32_t volume_latency_=0;float current_volume_=0;
    uint32_t random(){random_=random_*1103515245u+12345u;return random_>>16;}
    std::filesystem::path directory_;
    std::array<LcsRadioTrack,lcs_radio_station_count> tracks_{};
    std::array<uint64_t,lcs_radio_station_count> sizes_{};
    std::optional<OwnedEffectVoice> voice_;
    std::optional<uint32_t> playing_;
    uint32_t start_ms_=0;
    bool master_owned_=false,fault_=false;
    std::filesystem::path station_path(uint32_t station)const{return directory_/(std::string(lcs_radio_files[station])+".VB");}
    bool retire(uint32_t now) {
        if(!voice_)return true;
        const auto played=output_->owned_stream_position(*voice_);
        auto& track=tracks_[*playing_];
        if(played)track.position_ms=uint32_t((uint64_t(start_ms_)+*played/32u)%track.length_ms);
        track.last_check_ms=now;
        const auto receipt=output_->stop_owned_effect_checked(*voice_);
        if(!receipt.accepted){fault_=true;return false;}
        voice_.reset();playing_.reset();return true;
    }
    bool start(uint32_t station,uint32_t now,AudioMix mix) {
        auto& track=tracks_[station];
        const auto position=lcs_radio_start_position(track,now);
        const auto offset=lcs_radio_block_offset(sizes_[station],position);
        auto stream=std::make_shared<Ps2FileMusicStream>(station_path(station),offset);
        const auto receipt=output_->play_owned_pcm_stream([stream](){return stream->next_block();},mix);
        if(!receipt.accepted){if(receipt.entered)fault_=true;return false;}
        voice_=receipt.voice;playing_=station;start_ms_=lcs_radio_length_ms(offset);
        volume_latency_=10;current_volume_=0;return true;
    }
    bool stop_crackle() {
        if(!crackle_voice_)return true;
        const auto receipt=output_->stop_owned_effect_checked(*crackle_voice_);
        if(!receipt.accepted){fault_=true;return false;}
        crackle_voice_.reset();return true;
    }
    bool service_retune(const SaRadioState& state,uint32_t now,float effects) {
        if(!retire(now))return false;
        if(state.pending!=last_pending_){static_counter_=0;last_pending_=state.pending;}
        const AudioMix crackle{uint8_t(std::lround(float(random()%20+15)*effects)),63};
        if(!crackle_voice_) {
            const auto receipt=output_->play_owned_loop(crackle_,crackle_begin_,crackle_end_,crackle);
            if(!receipt.accepted){if(receipt.entered)fault_=true;return !receipt.entered;}
            crackle_voice_=receipt.voice;
        } else {
            const auto update=output_->update_owned_effect(*crackle_voice_,crackle,crackle_.frequency,false);
            if(!update.accepted){fault_|=update.entered;return false;}
        }
        if(static_counter_<2 && now>static_timer_+800) {
            dial_=(dial_+(random()%2?1:2))%3;
            output_->play_owned_effect(dials_[dial_],AudioMix{uint8_t(std::lround(127.0f*effects)),63});
            ++static_counter_;static_timer_=now;
        }
        return true;
    }
    bool release_master(const SaRadioState& state) {
        if(!master_owned_)return true;
        if(!sa_radio_write_master(state.menu))return false;
        master_owned_=false;return true;
    }
public:
    SaLcsRadio(const std::filesystem::path& music_directory,uint32_t now,const AudioArchive& archive):directory_(music_directory) {
        crackle_=archive.sample(archive.radio_crackle_index());
        const auto& record=archive.record(archive.radio_crackle_index());
        const auto span=continuous_pcm_loop_span(record.size,record.loop_start,record.loop_end);
        crackle_begin_=span && record.loop_end!=-1 ? span->begin : 0;
        crackle_end_=span && record.loop_end!=-1 ? span->end : uint32_t(crackle_.pcm.size());
        for(uint32_t dial=0;dial<3;++dial)dials_[dial]=archive.sample(archive.radio_dial_index(dial));
        for(uint32_t station=0;station<lcs_radio_station_count;++station) {
            std::error_code error;const auto size=std::filesystem::file_size(station_path(station),error);
            if(error || size<0x4000 || size%0x4000)throw std::runtime_error("Original LCS radio stream missing or invalid: "+station_path(station).string());
            sizes_[station]=size;tracks_[station].length_ms=lcs_radio_length_ms(size);
            tracks_[station].position_ms=lcs_radio_new_game_positions[station]%tracks_[station].length_ms;
            tracks_[station].last_check_ms=now;
        }
    }
    ~SaLcsRadio(){if(!shutdown())output_.release();}
    bool faulted()const{return fault_;}
    // SetRadioChannelByScript: the next tuning of this station starts at seek.
    void script_position(int32_t station,int32_t seek_ms,uint32_t now) {
        if(station<0 || station>=int32_t(lcs_radio_station_count) || seek_ms<0)return;
        auto& track=tracks_[size_t(station)];
        track.position_ms=uint32_t(seek_ms)%track.length_ms;track.last_check_ms=now;
    }
    bool service(uint32_t now,bool duck,float effects) {
        if(fault_)return false;
        SaRadioState state{};
        if(!sa_radio_read(state)){fault_=true;return false;}
        if(state.retuning) {
            // SA's own retune static plays on the music stream; keep it silent.
            if(!sa_radio_write_master(0.0f)){fault_=true;return false;}
            master_owned_=true;
            return service_retune(state,now,std::clamp(effects,0.0f,1.0f));
        }
        last_pending_=0;
        if(!stop_crackle())return false;
        const auto station=state.on?lcs_station_for_sa(state.station):std::nullopt;
        if(!station) {
            if(!retire(now))return false;
            return release_master(state);
        }
        // cMusicManager::ServiceTrack: fade in to 100, held at 25 while
        // mission speech plays.
        if(volume_latency_)--volume_latency_;
        else current_volume_=std::min(100.0f,current_volume_+6.0f);
        auto volume=current_volume_*state.menu*state.fader;
        if(duck)volume=std::min(volume,25.0f);
        const AudioMix mix{uint8_t(std::lround(std::clamp(volume,0.0f,127.0f))),63};
        if(playing_!=station) {
            if(!retire(now) || !start(*station,now,mix))return false;
        }
        if(!sa_radio_write_master(0.0f)){fault_=true;return false;}
        master_owned_=true;
        const auto pause=output_->pause_owned_effect(*voice_,state.paused);
        if(!pause.accepted){fault_|=pause.entered;return false;}
        const auto update=output_->update_owned_effect(*voice_,mix,32000,false);
        if(!update.accepted){fault_|=update.entered;return false;}
        return true;
    }
    bool shutdown() {
        SaRadioState state{};const bool read=sa_radio_read(state);
        if(!stop_crackle())return false;
        if(voice_) {
            const auto receipt=output_->stop_owned_effect_checked(*voice_);
            if(!receipt.accepted)return false;
            voice_.reset();playing_.reset();
        }
        return !master_owned_ || (read && release_master(state));
    }
};
}
