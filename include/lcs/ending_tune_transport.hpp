#pragma once
#include "ending_tune_envelope.hpp"
#include <functional>
namespace lcs {
struct EndingTuneEffect {bool entered=false,accepted=false;};
enum class EndingTuneResult {accepted,deferred,quarantined};
struct EndingTuneOutput {
    std::function<EndingTuneEffect(uint8_t)> start,gain;
    std::function<EndingTuneEffect()> stop;
    std::function<std::optional<bool>()> playing;
};
// Retail music manager subset for FINALE63: requests, cadence4 mode changes,
// early frontend request, verified start, fade and EOF restart. The source
// audio service supplies one frame per invocation, including paused frames.
class EndingTuneTransport {
    int current_=1,upcoming_=1,target_=1;
    bool transition_=false,early_=false,requested_=false,voice_=false,verify_=false;
    bool quarantined_=false,was_paused_=false;
    std::optional<uint32_t> serviced_;
    EndingTuneEnvelope envelope_;
    EndingTuneResult effect(const EndingTuneEffect& receipt) {
        if(receipt.accepted)return EndingTuneResult::accepted;
        if(receipt.entered)quarantined_=true;
        return quarantined_?EndingTuneResult::quarantined:EndingTuneResult::deferred;
    }
public:
    bool active()const{return voice_;}
    bool quarantined()const{return quarantined_;}
    int current_mode()const{return current_;}
    uint8_t volume()const{return envelope_.volume();}
    bool request_finale() {
        if(quarantined_)return false;
        upcoming_=0;requested_=true;if(current_!=0)early_=true;return true;
    }
    EndingTuneResult request_mode(int mode,const EndingTuneOutput& out) {
        if(quarantined_)return EndingTuneResult::quarantined;
        // DISABLE3 requests DISABLED4; passing4 to the original switch does
        // nothing. CUTSCENE2 stops synchronously, unlike frontend/game modes.
        if(mode==4)return EndingTuneResult::accepted;
        if(mode<0 || mode>3)return EndingTuneResult::deferred;
        if(mode==2) {
            if(voice_) {
                const auto result=effect(out.stop());if(result!=EndingTuneResult::accepted)return result;
                voice_=false;
            }
            current_=upcoming_=target_=2;transition_=early_=requested_=verify_=false;
        } else upcoming_=mode==3?4:mode;
        return EndingTuneResult::accepted;
    }
    EndingTuneResult service(uint32_t frame,bool paused,const EndingTuneOutput& out) {
        if(quarantined_)return EndingTuneResult::quarantined;
        if(serviced_==frame)return EndingTuneResult::accepted;
        serviced_=frame;
        const bool entered_pause=paused && !was_paused_;was_paused_=paused;
        try {
            if(!transition_)target_=upcoming_;
            if(target_!=current_) {
                transition_=true;
                if(frame&3)return EndingTuneResult::accepted;
                if(voice_) {
                    const auto result=effect(out.stop());if(result!=EndingTuneResult::accepted)return result;
                    voice_=false;return result;
                }
                current_=target_;transition_=false;verify_=false;envelope_={};
                if(early_)early_=false;else requested_=false;
                return EndingTuneResult::accepted;
            }
            if(entered_pause && current_==0){current_=4;return EndingTuneResult::accepted;}
            if(current_!=0 || (frame&3))return EndingTuneResult::accepted;
            if(!requested_)return EndingTuneResult::accepted;
            if(!voice_) {
                const auto result=effect(out.start(0));if(result!=EndingTuneResult::accepted)return result;
                voice_=true;verify_=true;envelope_={};return result;
            }
            const auto playing=out.playing();if(!playing)return EndingTuneResult::deferred;
            if(!*playing) {
                auto result=effect(out.stop());if(result!=EndingTuneResult::accepted)return result;
                voice_=false;
                result=effect(out.start(envelope_.volume()));if(result==EndingTuneResult::accepted)voice_=true;
                return result;
            }
            if(verify_){verify_=false;return EndingTuneResult::accepted;}
            EndingTuneResult result=EndingTuneResult::accepted;
            envelope_.service_accepted(frame,[&](uint8_t gain){result=effect(out.gain(gain));return result==EndingTuneResult::accepted;});
            return result;
        } catch(...) {quarantined_=true;return EndingTuneResult::quarantined;}
    }
};
}
