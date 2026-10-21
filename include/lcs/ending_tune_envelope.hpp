#pragma once
#include <cstdint>
#include <optional>
namespace lcs {
// Retail frontend service16816C..1684B0: audio-frame cadence, not wall time.
class EndingTuneEnvelope {
    uint8_t latency_=3,volume_=0;
    std::optional<uint32_t> serviced_;
public:
    uint8_t volume()const{return volume_;}
    // Service a copy, then commit only after the output accepts the gain.
    // A failed output must not advance the published fade state.
    template<class Output> bool service_accepted(uint32_t audio_frame,Output&& output) {
        auto candidate=*this;
        const auto gain=candidate.service(audio_frame);
        if(gain && !output(*gain))return false;
        *this=candidate;return true;
    }
    std::optional<uint8_t> service(uint32_t audio_frame) {
        if((audio_frame&3) || serviced_==audio_frame)return {};
        serviced_=audio_frame;
        if(latency_){--latency_;return {};}
        if(volume_<100)volume_=uint8_t(volume_+6>100?100:volume_+6);
        return volume_;
    }
};
}
