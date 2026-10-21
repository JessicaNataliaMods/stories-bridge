#pragma once
#include <cstdint>
#include <optional>
#include <array>
#include <limits>
#include <cmath>
#include <stdexcept>

namespace lcs {
struct ContinuousMusicTrack {uint32_t position,service_age,length;};
// Retail libc34FCE0 uses the Schrage form of16807*state mod2147483647.
// The campaign must supply its actual shared state. A private default-seeded
// generator would silently diverge from all other source rand() consumers.
class ContinuousSourceRandom {
    uint32_t state_;
public:
    explicit ContinuousSourceRandom(uint32_t actual_state):state_(actual_state){
        if(!state_ || state_>=2147483647u)throw std::runtime_error("Missing valid shared source random state");
    }
    uint32_t state()const{return state_;}
    uint32_t next(){
        const auto difference=int64_t(state_%127773u)*16807-int64_t(state_/127773u)*2836;
        state_=uint32_t(difference>0?difference:difference+2147483647ll);return state_;
    }
    std::array<uint32_t,5> audio_table(){
        std::array<uint32_t,5> table;
        for(auto& value:table)value=next(); // Original23A8A0, exactly five shared draws.
        return table;
    }
};
struct ContinuousMusicInitialization {
    // Original16783C discards one rand() result, then draws once for each of
    // 66 slots. Inputs must be captured from the shared source RNG producer;
    // no default seed, host clock or replacement generator is supplied here.
    std::array<uint32_t,67> random_draws;
    std::array<uint32_t,5> audio_random_table;
    std::array<uint32_t,10> radio_lengths;
};
inline std::optional<ContinuousMusicInitialization> capture_continuous_music_initialization(
    ContinuousSourceRandom& shared,const std::array<uint32_t,5>& actual_audio_table,
    const std::array<uint32_t,10>& actual_radio_lengths){
    for(auto length:actual_radio_lengths)if(!length)return {};
    ContinuousMusicInitialization source;
    source.audio_random_table=actual_audio_table;source.radio_lengths=actual_radio_lengths;
    for(auto& draw:source.random_draws)draw=shared.next();
    return source;
}
inline std::optional<std::array<ContinuousMusicTrack,66>> initialize_continuous_music(
    const ContinuousMusicInitialization& source){
    for(auto draw:source.random_draws)if(draw>0x7FFFFFFFu)return {};
    for(auto length:source.radio_lengths)if(!length)return {};
    std::array<ContinuousMusicTrack,66> tracks{};
    for(size_t i=0;i<tracks.size();++i){
        auto& track=tracks[i];
        track.length=i<10?source.radio_lengths[i]:100u;
        if(i<25){
            //167878 signed64 multiplication,167880/84 high-word scale,
            // then16788C shift and1678F8 low32 product before unsigned DIV.
            const auto scaled=uint32_t((uint64_t(source.random_draws[i+1])*65535u)>>31)<<10;
            const uint32_t product=source.audio_random_table[i%5]*scaled;
            track.position=product%track.length;
        }
        //167910 clears the other positions;167930 clears every service age.
    }
    return tracks;
}
enum class ContinuousMusicConfiguration {unsupported,playing_gated,next_gated,configured};
// Observed current/next tracks and the real AudioManager random word are
// mandatory inputs. Retail16A488/16A530 clear the service age, not a clock.
inline ContinuousMusicConfiguration configure_continuous_music(uint8_t selector,uint32_t random_word,
    uint8_t playing,int8_t next,ContinuousMusicTrack& strip,ContinuousMusicTrack& club){
    if(selector<40 || selector>45)return ContinuousMusicConfiguration::unsupported;
    const auto target=uint8_t(selector<43?18:17);
    if(playing==target)return ContinuousMusicConfiguration::playing_gated;
    if(next==target)return ContinuousMusicConfiguration::next_gated;
    constexpr uint32_t starts[]={8640,286720,509120,0,320200,672000};
    auto& track=target==18?club:strip;
    track.position=starts[selector-40]+(random_word&127u);track.service_age=0;
    return ContinuousMusicConfiguration::configured;
}
// Retail MusicManager169FF0, not the reconstruction's wall-clock formula.
// +20 is a service-frame age (169FB0 increments it once per service). Arithmetic
// wraps as32-bit EE words. Non-radio tracks still validate length/divide, then
// the final MOVZ returns zero for track>=10, including configured17/18.
inline std::optional<uint32_t> continuous_music_start_position(uint8_t track,uint32_t position,uint32_t service_age,uint32_t length){
    if(!length)return {};
    const uint32_t advanced=position+service_age*33u+500u;
    const auto remainder=advanced%length;
    return track<10?remainder:0u;
}
struct ContinuousPcmLoop {uint32_t begin,end;}; // exclusive byte end
// Mounted PCM16 SDT stores the last sample's byte offset inclusively. Proven
// for continuous samples18/183/290 against retail VAG loop-start/end flags,
// including all28 samples of the terminal block, after the omitted lead block.
// Sentinel-1 selects the whole sample; never infer a nonzero start for it.
inline std::optional<ContinuousPcmLoop> continuous_pcm_loop_span(uint32_t size,uint32_t start,int32_t end){
    if(!size || size%2 || start%2 || start>=size)return {};
    if(end==-1){if(start)return {};return ContinuousPcmLoop{0,size};}
    if(end<0 || uint32_t(end)%2 || uint32_t(end)>=size || uint32_t(end)<start)return {};
    return ContinuousPcmLoop{start,uint32_t(end)+2};
}
// Retail 1B2A98 emission descriptors, not the reconstructed AudioLogic enum.
// Preflight only: this does not register 402/403 or claim output acceptance.
// Other branches can configure banks/music without requesting a voice, and
// must be implemented separately before the complete family is bound.
struct ContinuousSoundEmission {
    uint32_t sample;
    uint32_t frequency_sample;
    uint32_t fixed_frequency; // zero means fetch the authored SDT frequency
    float distance;
    uint8_t volume;
    uint8_t priority;
    uint8_t frames;
    float speed;
};
inline std::optional<ContinuousSoundEmission> continuous_sound_emission(int32_t script_id) {
    // Handler stores low 16 bits; the actual audio consumer reads low 8 bits.
    switch (uint8_t(uint16_t(script_id))) {
    case 0: return ContinuousSoundEmission{4,5607,0,80,90,2,3,2};
    case 2: case 4: return ContinuousSoundEmission{183,183,0,80,127,3,3,2};
    case 11: return ContinuousSoundEmission{290,290,0,40,60,4,3,2};
    case 51: return ContinuousSoundEmission{18,18,20812,80,30,4,9,2};
    default: return std::nullopt;
    }
}
// Retail 1B2A98 jump table (SLUS_214.23 table 3B28A0, 52 entries; selector = low byte, sltiu 0x34):
//  voice:               0, 2, 4, 11, 51 build a queue sample and request a voice (descriptors above).
//  configuration_only:  6 and 36..39 and 46 fill the queue sample fields (sample 5608..5612/5619, banks
//                       41..45/52, 80 m or 6 m range) and jump to the epilogue: no voice is ever requested.
//  music_configuration: 40..45 select CITY track 17/18 and call the MusicManager position setters
//                       (16A488/16A530), then the same epilogue: no voice.
//  none:                every other selector (1,3,5,7..10,12..35,47..50 and everything >=52, e.g. 67)
//                       takes the default branch straight to the epilogue.
// Entity creation, handle publication and release are identical for every kind except
// music_configuration, which requires the mounted CITY consumer before publishing a handle.
enum class ContinuousSoundKind {voice, configuration_only, music_configuration, none};
inline ContinuousSoundKind continuous_sound_kind(int32_t script_id) {
    const auto selector=uint8_t(uint16_t(script_id));
    switch(selector) {
    case 0: case 2: case 4: case 11: case 51: return ContinuousSoundKind::voice;
    case 6: case 36: case 37: case 38: case 39: case 46: return ContinuousSoundKind::configuration_only;
    case 40: case 41: case 42: case 43: case 44: case 45: return ContinuousSoundKind::music_configuration;
    default: return ContinuousSoundKind::none;
    }
}
// Retail emitter's strict distance gate followed by238A10 ComputeVolume.
// Preserve the float operation order, integer truncation and low-byte narrowing.
inline std::optional<uint8_t> continuous_sound_volume(uint8_t emitting,float maximum,float distance){
    if(!std::isfinite(maximum) || !std::isfinite(distance) || distance<0)return {};
    if(maximum<=0 || distance>=maximum)return uint8_t(0);
    const float minimum=maximum*0.2f;
    if(distance<minimum)return emitting;
    const float excess=distance-minimum;
    const float span=maximum-minimum;
    const float remaining=span-excess;
    const float ratio=remaining/span;
    const float square=ratio*ratio;
    const float scaled=square*float(emitting);
    const auto narrowed=uint32_t(scaled)&255u;
    return uint8_t(narrowed>127?127:narrowed);
}
struct ContinuousSoundMotion {
    float previous_distance,current_distance,speed_of_sound;
    uint8_t time_spent;
    bool camera_switched;
};
inline constexpr std::array<uint8_t,64> continuous_pan_table{{
    0,3,8,12,16,19,22,24,26,28,30,31,33,34,36,37,39,40,41,42,44,45,46,47,
    48,49,49,50,51,52,53,53,54,55,55,56,56,57,57,58,58,58,59,59,59,60,60,
    61,61,61,61,62,62,62,62,62,63,63,63,63,63,63,63,63}};
// Camera-space X, not a guessed screen/right vector. Retail238C90/table392700.
inline std::optional<uint8_t> continuous_sound_pan(float distance,float camera_x){
    if(!std::isfinite(distance) || !std::isfinite(camera_x) || distance<0)return {};
    size_t index=63;
    if(distance>0){
        const float divisor=distance*(1.f/64.f);
        const float ratio=camera_x/divisor;
        const float magnitude=std::fabs(ratio);
        if(std::isfinite(magnitude) && magnitude<63)index=size_t(magnitude);
    }
    // EE DIV by zero saturates, CVT.W then index clamp selects63. Preserve
    // the retail degenerate result rather than inventing centered panning.
    const auto curve=continuous_pan_table[index];
    if(camera_x>0){const auto pan=63-int(curve);return uint8_t(pan<20?20:pan);}
    const auto pan=63+int(curve);return uint8_t(pan>107?107:pan);
}
// Retail238D30: observed consecutive audio-frame distances, not wall-clock
// guesses. Time/sound-speed/camera status must come from the audio controller.
inline std::optional<uint32_t> continuous_sound_frequency(uint32_t frequency,float multiplier,const ContinuousSoundMotion& frame){
    if(!frequency || frequency>192000 || !std::isfinite(multiplier) ||
       !std::isfinite(frame.previous_distance) || !std::isfinite(frame.current_distance) ||
       frame.previous_distance<0 || frame.current_distance<0)return {};
    if(frame.camera_switched || multiplier==0)return frequency;
    const float delta=frame.current_distance-frame.previous_distance;
    if(delta==0)return frequency;
    if(!frame.time_spent || !std::isfinite(frame.speed_of_sound) || frame.speed_of_sound<=0)return {};
    const float per_time=delta/float(frame.time_spent);
    float speed=per_time*multiplier;
    if(std::fabs(speed)>=frame.speed_of_sound)return frequency;
    if(speed>=0){if(speed>1.5f)speed=1.5f;}else if(speed< -1.5f)speed=-1.5f;
    const float denominator=frame.speed_of_sound+speed;
    const float numerator=float(frequency)*frame.speed_of_sound;
    const float result=numerator/denominator;
    if(!std::isfinite(result) || result<1 || result>192000)return {};
    return uint32_t(result);
}

struct ContinuousFrequencyStep {
    uint32_t cached_frequency,output_frequency;
    bool write;
};
struct ContinuousMixContext {
    bool double_volume;uint8_t global_sfx_volume;
    // Target hardware/user gain is applied after the original integer mixer.
    // Runtime supplies the live SA effects scaling; standalone source tests
    // keep unity gain, independently of the original logical-volume cache.
    float native_scale=1.f;
};
struct ContinuousVolumeStep {uint8_t cached_volume,output_volume;};
inline std::optional<ContinuousVolumeStep> continuous_volume_step(uint8_t cached,uint8_t desired,ContinuousMixContext context,bool initial=false){
    if(cached>127 || desired>127 || context.global_sfx_volume>127 ||
       !std::isfinite(context.native_scale) || context.native_scale<0 || context.native_scale>1)return {};
    const int delta=int(desired)-cached;
    const auto next=initial?desired:uint8_t(int(cached)+(delta>10?10:delta< -10?-10:delta));
    const auto emitting=context.double_volume?2u*(next<63?next:63):unsigned(next);
    const auto source_volume=emitting*context.global_sfx_volume/127u;
    return ContinuousVolumeStep{next,uint8_t(float(source_volume)*context.native_scale)};
}
// Retail SampleManager114C38 clamps only the hardware request to44000 Hz.
// The active queue's logical cache must remain unbounded by this driver cap.
inline uint32_t continuous_channel_frequency(uint32_t frequency){return frequency>44000?44000:frequency;}
// Existing-channel frame policy23A06C..23A0E0. Cache is unscaled; a change
// of time scale alone does not enter SetChannelFrequency. Initial-channel
// setup is a separate contract and must supply the actual cached frequency.
inline std::optional<ContinuousFrequencyStep> continuous_frequency_step(
    uint32_t cached,uint32_t desired,float time_scale){
    if(!cached || cached>192000 || !desired || desired>192000 ||
       !std::isfinite(time_scale))return {};
    if(cached==desired)return ContinuousFrequencyStep{cached,cached,false};
    const auto next=desired>cached ? (desired-cached>6000?cached+6000:desired)
                                   : (cached-desired>6000?cached-6000:desired);
    const auto output=time_scale<1.f?uint32_t(float(next)*0.75f):next;
    return ContinuousFrequencyStep{next,continuous_channel_frequency(output),true};
}

// Portable storage for the actual PS2 AudioScriptObj pool. Runtime binding is
// deliberately separate: allocation alone is not acceptance by an audio driver.
class ContinuousSoundObjects {
    friend struct CampaignServicesSave;
public:
    static constexpr size_t capacity=192; // retail 22E768
    struct Token {int32_t reference;uint64_t epoch;};
    struct Object {
        uint16_t sound;
        std::array<float,3> position;
        int32_t audio_entity=-5; // retail constructor 13CAC4
    };
    struct Snapshot {Token token;Object object;};
private:
    struct Slot {uint8_t flag=0x80;uint64_t epoch=0;Object object{};};
    std::array<Slot,capacity> slots_{};
    int32_t cursor_=-1;
    const Slot* resolve(int32_t reference)const {
        if(reference<0)return nullptr;
        const auto index=uint32_t(reference)>>8;
        if(index>=capacity)return nullptr;
        const auto& slot=slots_[index];
        if((slot.flag&0x80) || slot.flag!=uint8_t(reference))return nullptr;
        return &slot;
    }
public:
    std::optional<Snapshot> allocate(int32_t sound,std::array<float,3> position) {
        // New() starts after the last cursor. Its exhausted scan exits on the
        // second end-of-pool wrap with cursor0, even if it began mid-pool.
        for(size_t probe=0;probe<capacity;++probe) {
            cursor_=(cursor_+1)%int32_t(capacity);
            auto& slot=slots_[size_t(cursor_)];
            if(!(slot.flag&0x80) || slot.epoch==std::numeric_limits<uint64_t>::max())continue;
            slot.flag=uint8_t((slot.flag+1)&0x7F);++slot.epoch;
            slot.object={uint16_t(sound),position,-5};
            return Snapshot{{(cursor_<<8)|slot.flag,slot.epoch},slot.object};
        }
        cursor_=0;
        return {};
    }
    std::optional<Snapshot> find(int32_t reference)const {
        const auto* slot=resolve(reference);if(!slot)return {};
        return Snapshot{{reference,slot->epoch},slot->object};
    }
    bool attach_entity(Token token,int32_t entity) {
        const auto* slot=resolve(token.reference);
        if(!slot || slot->epoch!=token.epoch)return false;
        slots_[uint32_t(token.reference)>>8].object.audio_entity=entity;return true;
    }
    // Called only after the owning audio service accepts release. No driver
    // operation is performed here; a failed release must leave this object live.
    bool erase_released(Token token) {
        const auto* current=resolve(token.reference);
        if(!current || current->epoch!=token.epoch)return false;
        const auto index=int32_t(uint32_t(token.reference)>>8);
        auto& slot=slots_[size_t(index)];slot.flag|=0x80;slot.object={};
        if(index<cursor_)cursor_=index; // retail Delete(), next New() starts after it
        return true;
    }
};
}
