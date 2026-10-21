#pragma once
#include <cmath>
#include <array>
#include <cstdint>
#include <optional>
namespace lcs {
inline std::optional<uint8_t> original_phone_pan_update(uint8_t requested,uint8_t active) {
    if(requested<20 || requested>107 || active<20 || active>107)return {};
    const int delta=int(requested)-int(active);
    return uint8_t(int(active)+(delta>10?10:delta< -10?-10:delta));
}
inline std::optional<float> original_phone_relative_x(const std::array<float,3>& position,
    const std::array<float,3>& camera,const std::array<float,3>& right) {
    for(size_t i=0;i<3;++i)if(!std::isfinite(position[i]) || !std::isfinite(camera[i]) || !std::isfinite(right[i]))return {};
    const float dx=position[0]-camera[0],dy=position[1]-camera[1],dz=position[2]-camera[2];
    const float x=(dx*right[0]+dy*right[1])+dz*right[2];
    if(!std::isfinite(x))return {};
    return x;
}
struct PhoneFrequencyUpdate {uint32_t active_frequency,channel_frequency;bool write_channel;};
inline std::optional<PhoneFrequencyUpdate> original_phone_frequency_update(
    uint32_t requested,uint32_t active,bool slow_motion) {
    if(requested>2147483647u || active>2147483647u)return {};
    if(requested==active)return PhoneFrequencyUpdate{active,active,false};
    const int64_t delta=int64_t(requested)-int64_t(active);
    const uint32_t next=uint32_t(int64_t(active)+(delta>6000?6000:delta< -6000?-6000:delta));
    uint32_t channel=next;
    if(slow_motion) {
        const float scaled=float(next)*0.75f;
        channel=uint32_t(scaled);const float fraction=scaled-float(channel);
        if(fraction>0.5f || (fraction==0.5f && (channel&1)))++channel;
    }
    return PhoneFrequencyUpdate{next,channel,true};
}
inline std::optional<uint32_t> original_phone_ring_frequency(uint32_t authored,
    float previous_distance,float current_distance,bool camera_switched) {
    if(!std::isfinite(previous_distance) || !std::isfinite(current_distance) ||
        previous_distance<0 || current_distance<0)return {};
    if(camera_switched || previous_distance==current_distance)return authored;
    // Original constructor237E50..84 stores these exact float bits and byte30.
    constexpr float sound_speed=11.433334350585938f;
    float speed=((current_distance-previous_distance)/30.f)*2.f;
    if(!std::isfinite(speed))return {};
    if(std::fabs(speed)>=sound_speed)return authored;
    if(speed>1.5f)speed=1.5f;
    if(speed< -1.5f)speed=-1.5f;
    const float frequency=(float(authored)*sound_speed)/(sound_speed+speed);
    // The retail conversion is signed cvt.w.s. Values outside its proven
    // positive range are unavailable, rather than a fabricated saturation.
    if(!std::isfinite(frequency) || frequency<0 || frequency>=2147483648.f)return {};
    uint32_t rounded=uint32_t(frequency);
    const float fraction=frequency-float(rounded);
    if(fraction>0.5f || (fraction==0.5f && (rounded&1)))++rounded;
    return rounded;
}
inline std::optional<uint8_t> original_phone_ring_pan(float max_distance,float camera_relative_x) {
    // Retail callers23A200/23A508 pass tSound+28 (maximum distance),
    // unlike Doppler's actual-distance field+1C. For the phone this is80.
    if(!std::isfinite(max_distance) || max_distance<=0 || !std::isfinite(camera_relative_x))return {};
    static constexpr uint8_t table[]={0,3,8,12,16,19,22,24,26,28,30,31,33,34,36,37,
        39,40,41,42,44,45,46,47,48,49,49,50,51,52,53,53,54,55,55,56,56,57,57,58,
        58,58,59,59,59,60,60,61,61,61,61,62,62,62,62,62,63,63,63,63,63,63,63,63};
    const float ratio=std::fabs(camera_relative_x)/(max_distance*(1.f/64.f));
    if(!std::isfinite(ratio))return {};
    uint32_t index=63;
    if(ratio<63.f) {
        index=uint32_t(ratio);const float fraction=ratio-float(index);
        if(fraction>0.5f || (fraction==0.5f && (index&1)))++index;
    }
    const int pan=camera_relative_x>0?63-int(table[index]):63+int(table[index]);
    return uint8_t(camera_relative_x>0?(pan<20?20:pan):(pan>107?107:pan));
}
inline std::optional<uint8_t> original_phone_ring_volume(float distance) {
    if(!std::isfinite(distance) || distance<0)return {};
    if(distance>=80.f)return uint8_t(0);
    if(distance<16.f)return uint8_t(80);
    const float numerator=64.f-(distance-16.f);
    const float ratio=numerator/64.f;
    const float raw=(ratio*ratio)*80.f;
    // Retail ComputeVolume uses cvt.w.s and startup sets FCR31=0 (nearest,
    // ties-to-even). Do not truncate as reconstructed C++ does or alter SA's
    // floating-point mode, which belongs to the game and other mods.
    uint32_t rounded=uint32_t(raw);
    const float fraction=raw-float(rounded);
    if(fraction>0.5f || (fraction==0.5f && (rounded&1)))++rounded;
    return uint8_t(rounded);
}
struct PhoneActiveAudioState {
    float distance;
    uint32_t frequency;
    uint8_t volume,pan;
};
struct PhoneActiveAudioUpdate {
    PhoneActiveAudioState next;
    uint32_t channel_frequency;
    bool write_frequency;
};
struct PhoneStaticAudioStart {float distance;uint32_t frequency;uint8_t volume,pan;};
inline std::optional<PhoneStaticAudioStart> original_phone_static_audio_start(uint32_t authored,
    const std::array<float,3>& source,const std::array<float,3>& camera,
    const std::array<float,3>& right,bool slow_motion) {
    if(!authored || authored>192000)return {};
    const auto relative_x=original_phone_relative_x(source,camera,right);
    if(!relative_x)return {};
    const float dx=source[0]-camera[0],dy=source[1]-camera[1],dz=(source[2]-camera[2])*0.65f;
    const float squared=(dx*dx+dy*dy)+dz*dz;
    if(!std::isfinite(squared))return {};
    const float distance=std::sqrt(squared);
    const auto volume=original_phone_ring_volume(distance),pan=original_phone_ring_pan(80,*relative_x);
    if(!volume || !pan)return {};
    uint32_t frequency=authored;
    if(slow_motion) {
        const float scaled=float(authored)*0.75f;frequency=uint32_t(scaled);
        const float fraction=scaled-float(frequency);
        if(fraction>0.5f || (fraction==0.5f && (frequency&1)))++frequency;
    }
    return PhoneStaticAudioStart{distance,frequency,*volume,*pan};
}
// Proposes only the already-active spatial voice branch. Caller commits next
// after accepted device updates, never after a stale voice or partial failure.
inline std::optional<PhoneActiveAudioUpdate> original_phone_active_audio_update(
    const PhoneActiveAudioState& active,uint32_t authored,float distance,
    float relative_x,bool camera_switched,bool slow_motion) {
    if(active.volume>80)return {};
    const auto volume=original_phone_ring_volume(distance);
    const auto pan=original_phone_ring_pan(80.f,relative_x);
    const auto frequency=original_phone_ring_frequency(authored,active.distance,distance,camera_switched);
    if(!volume || !pan || !frequency || distance>=80.f)return {};
    const auto next_pan=original_phone_pan_update(*pan,active.pan);
    const auto next_frequency=original_phone_frequency_update(*frequency,active.frequency,slow_motion);
    if(!next_pan || !next_frequency)return {};
    const int delta=int(*volume)-int(active.volume);
    const uint8_t next_volume=uint8_t(int(active.volume)+(delta>10?10:delta< -10?-10:delta));
    return PhoneActiveAudioUpdate{{distance,next_frequency->active_frequency,next_volume,*next_pan},
        next_frequency->channel_frequency,next_frequency->write_channel};
}
}
