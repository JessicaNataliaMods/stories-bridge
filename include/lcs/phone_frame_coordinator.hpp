#pragma once
#include "phone_ring_policy.hpp"
#include <functional>
namespace lcs {
struct PhoneEffectReceipt {bool entered=false,accepted=false;};
struct PhoneFrameRandomReceipt {PhoneEffectReceipt effect;uint32_t sample=0;};
struct PhoneFrameServices {
    std::function<bool(bool visual,bool audio)> preflight;
    std::function<PhoneFrameRandomReceipt()> random;
    std::function<PhoneEffectReceipt(float)> write_up;
    std::function<PhoneEffectReceipt()> audio,refresh;
};
enum class PhoneFrameResult {accepted,deferred,quarantined};
// A side-effecting frame is never handed to the supervisor's retry loop.
// Partial entry quarantines this phone until an explicit lifecycle reset.
class PhoneFrameCoordinator {
    bool visible_=false,quarantined_=false,completed_=false;
    uint32_t last_frame_=0;
public:
    bool visible()const{return visible_;}
    bool quarantined()const{return quarantined_;}
    PhoneFrameResult update(PhoneRingFrame frame,const PhoneFrameServices& services) {
        if(quarantined_)return PhoneFrameResult::quarantined;
        if(completed_ && frame.frame==last_frame_)return PhoneFrameResult::accepted;
        frame.visible=visible_;frame.random.reset();
        auto effect=original_phone_ring_frame(frame);
        if(!effect)return PhoneFrameResult::deferred;
        const bool visual=effect->up_z.has_value() || effect->needs_random;
        if(visual && (!services.preflight || !services.write_up || !services.refresh ||
            (effect->ring && !services.audio) || (effect->needs_random && !services.random)))return PhoneFrameResult::deferred;
        if(visual)try {if(!services.preflight(visual,effect->ring))return PhoneFrameResult::deferred;}
            catch(...){return PhoneFrameResult::deferred;}
        bool entered=false;
        const auto failed=[&](){if(entered)quarantined_=true;return entered?PhoneFrameResult::quarantined:PhoneFrameResult::deferred;};
        try {
        if(effect->needs_random) {
            const auto random=services.random();entered|=random.effect.entered;
            if(!random.effect.accepted)return failed();
            frame.random=random.sample;effect=original_phone_ring_frame(frame);
            if(!effect || !effect->up_z)return failed();
        }
        if(visual) {
            auto receipt=services.write_up(*effect->up_z);entered|=receipt.entered;
            if(!receipt.accepted)return failed();
            if(effect->ring) {
                receipt=services.audio();entered|=receipt.entered;
                if(!receipt.accepted)return failed();
            }
            receipt=services.refresh();entered|=receipt.entered;
            if(!receipt.accepted)return failed();
        }
        } catch(...) {quarantined_=true;return PhoneFrameResult::quarantined;}
        visible_=effect->visible;last_frame_=frame.frame;completed_=true;
        return PhoneFrameResult::accepted;
    }
};
}
