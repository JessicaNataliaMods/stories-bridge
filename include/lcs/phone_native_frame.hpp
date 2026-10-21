#pragma once
#include "sa_executor.hpp"
#include "phone_frame_coordinator.hpp"
#include "phone_audio_policy.hpp"
namespace lcs {
// Production provider remains unregistered until adapter lifecycle and
// integrated native-fixture tests are complete.
inline PhoneFrameResult sa_update_phone_frame(int32_t model,const std::array<float,3>& position,
    PhoneFrameCoordinator& coordinator,const AudioSample& authored,
    const std::function<PhoneEffectReceipt(const AudioSample&,AudioMix)>& play) {
    if(coordinator.quarantined())return PhoneFrameResult::quarantined;
    const auto clock=sa_frame_clock();const auto camera=sa_camera_placeable_position();
    const auto player=sa_focused_player_position();const auto booth=sa_phone_booth_snapshot(model,position);
    if(!clock || !camera || !player || !booth || !booth->entity)return PhoneFrameResult::deferred;
    PhoneRingFrame frame;frame.now=clock->now;frame.previous=clock->previous;frame.frame=clock->frame;
    frame.entity_seed=booth->seed;frame.camera_x=(*camera)[0];frame.camera_y=(*camera)[1];
    frame.phone_x=position[0];frame.phone_y=position[1];frame.player_z=(*player)[2];
    std::optional<NativePhoneRenderSnapshot> visual;std::optional<PhoneStaticAudioStart> audio;
    AudioSample prepared;PhoneFrameServices services;
    services.preflight=[&](bool,bool ring) {
        visual=sa_phone_render_snapshot(*booth);
        if(!visual || !visual->booth.up_z)return false;
        if((frame.now/1880)%2==1 && !sa_phone_random_ready())return false;
        if(ring) {
            const auto listener=sa_phone_camera_listener();const auto slow=sa_phone_slow_motion();
            if(!play || !listener || listener->position!=*camera || !slow || authored.pcm.empty() ||
                authored.channels<1 || authored.channels>2 || authored.pcm.size()%(authored.channels*2))return false;
            audio=original_phone_static_audio_start(authored.frequency,position,listener->position,listener->right,*slow);
            if(!audio)return false;
            prepared=authored;prepared.frequency=audio->frequency;
        }
        return true;
    };
    services.random=[](){const auto result=sa_phone_random_sample();
        return PhoneFrameRandomReceipt{{result.native_entered,result.executed},uint32_t(result.values[0])};};
    services.write_up=[&](float up){const auto result=sa_phone_write_up_z(*visual,up);
        if(result.executed)visual->booth.up_z=up;
        return PhoneEffectReceipt{result.native_entered,result.executed};};
    services.audio=[&](){
        const auto fresh=sa_phone_render_snapshot(visual->booth);
        if(!fresh || fresh->frame!=visual->frame || fresh->root!=visual->root ||
            fresh->engine!=visual->engine || fresh->booth.up_z!=visual->booth.up_z)return PhoneEffectReceipt{};
        // Original zero attenuation is a real queue cull, not dummy playback.
        if(!audio->volume)return PhoneEffectReceipt{false,true};
        return play(prepared,{audio->volume,audio->pan});
    };
    services.refresh=[&](){const auto result=sa_phone_refresh_visual(*visual);
        return PhoneEffectReceipt{result.native_entered,result.executed};};
    return coordinator.update(frame,services);
}
}
