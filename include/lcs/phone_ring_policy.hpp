#pragma once
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <optional>
namespace lcs {
struct PhoneRingFrame {
    uint32_t now=0,previous=0,frame=0;
    uint16_t entity_seed=0;
    float camera_x=0,camera_y=0,phone_x=0,phone_y=0,player_z=0;
    bool visible=false;
    std::optional<uint32_t> random;
};
struct PhoneRingEffect {
    bool visible=false,ring=false,needs_random=false;
    std::optional<float> up_z;
};
// Reconstructed original state9 update. Native booth ownership/identity and
// retail update proof are separate requirements before runtime registration.
inline std::optional<PhoneRingEffect> original_phone_ring_frame(const PhoneRingFrame& in) {
    for(float coordinate:{in.camera_x,in.camera_y,in.phone_x,in.phone_y,in.player_z})if(!std::isfinite(coordinate))return {};
    const float dx=in.camera_x-in.phone_x,dy=in.camera_y-in.phone_y;
    const float distance=dx*dx+dy*dy;if(!std::isfinite(distance))return {};
    PhoneRingEffect effect;effect.visible=in.visible;
    if(in.visible) {
        const bool scratch=(in.now/1880)%2==1,previous=(in.previous/1880)%2==1;
        effect.ring=scratch && !previous && in.player_z>-5.f; // retail-only height gate
        if(scratch) {
            if(!in.random)effect.needs_random=true;
            else effect.up_z=float(*in.random%1024)*(1.f/16000.f)+1.f; // retail mul.s 0x3883126F
        } else effect.up_z=1.f;
        // Update effects precede deactivation at the outer distance boundary.
        if(distance>100.f*100.f)effect.visible=false;
    } else if((in.frame+in.entity_seed)%16==0 && distance<80.f*80.f) { // retail 0x45C80000, not reconstructed60m
        effect.visible=true; // activation applies ringing on the NEXT frame
    }
    return effect;
}
}
