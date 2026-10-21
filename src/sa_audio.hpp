#pragma once
#include <windows.h>
#include <mmsystem.h>
#include "lcs/audio.hpp"
#include "lcs/continuous_sound.hpp"
#include <mutex>
#include <memory>
#include <thread>
#include <atomic>
#include <functional>
namespace lcs {
class Ps2MusicStream;
struct OwnedEffectVoice {size_t slot;uint64_t generation;};
struct OwnedEffectReceipt {bool entered=false,accepted=false;std::optional<OwnedEffectVoice> voice;};
enum class OwnedVoiceProfile { effects, source_audio };
struct ContinuousVoiceStart {
    uint32_t frequency;float time_scale;ContinuousMixContext mix;
    ContinuousVoiceStart(uint32_t frequency,float time_scale,ContinuousMixContext mix):frequency(frequency),time_scale(time_scale),mix(mix){}
};
class MissionSpeech {
    struct PcmStream {std::function<AudioSample()> next;std::array<std::vector<uint8_t>,2> pcm;};
    struct Voice {HWAVEOUT handle=nullptr;WAVEHDR header{};std::array<WAVEHDR,2> loop_headers{};std::vector<uint8_t> bytes,loop_bytes;uint64_t generation=0;uint32_t frequency=0;std::optional<uint32_t> continuous_frequency;std::unique_ptr<PcmStream> pcm_stream;bool continuous_channel=false,looping=false,loop_fault=false,paused=false;};
    // Headers handed to WinMM must keep their addresses through failure and
    // shutdown. Heap storage can be retained if a driver refuses final release.
    std::vector<std::unique_ptr<Voice>> voices_;
    size_t next_effect_=3;
    mutable std::mutex mutex_;
    HANDLE loop_event_=nullptr;
    std::thread loop_worker_;
    std::atomic<bool> loop_stopping_{false};
    bool continuous_user_paused_=false;
    std::vector<uint8_t> continuous_volumes_;
    bool ensure_loop_worker();
    OwnedEffectReceipt play_owned_region(const AudioSample&,uint32_t,uint32_t,AudioMix,std::optional<ContinuousVoiceStart>,bool repeat);
    bool open_unlocked(size_t,const AudioSample&,const AudioMix*,bool streaming);
    OwnedEffectReceipt update_owned_effect_unlocked(const OwnedEffectVoice&,AudioMix,uint32_t,bool);
    bool stop_unlocked(size_t slot);
    bool release_owned_unlocked(size_t slot);
    bool play_unlocked(size_t slot,const AudioSample& sample,const AudioMix* initial_mix=nullptr,uint32_t repeats=1);
public:
    explicit MissionSpeech(OwnedVoiceProfile profile=OwnedVoiceProfile::effects);
    ~MissionSpeech();
    bool shutdown();
    bool play(size_t slot,const AudioSample& sample);
    bool play_effect(const AudioSample& sample);
    void stop(size_t slot);
    bool finished(size_t slot)const;
    bool pause(size_t slot,bool paused);
    bool set_mix(size_t slot,AudioMix mix);
    OwnedEffectReceipt play_owned_effect(const AudioSample& sample,std::optional<AudioMix> initial_mix={},uint32_t repeats=1);
    // Byte offsets, exclusive end; caller must translate its authored format.
    // Two immutable headers keep queuing the same region until an owned stop.
    OwnedEffectReceipt play_owned_loop(const AudioSample&,uint32_t begin,uint32_t end,AudioMix,std::optional<ContinuousVoiceStart> start={});
    // Authored SDT loop records stream indefinitely. Sentinel-1 records play
    // once: the source audio-frame queue must observe completion, release the
    // old receipt and reacquire while its script object still requests sound.
    // Neither this factory nor the worker invents an automatic alarm replay.
    OwnedEffectReceipt play_owned_continuous_sample(const AudioSample&,const AudioRecord&,AudioMix,ContinuousVoiceStart);
    OwnedEffectReceipt update_owned_continuous_frame(const OwnedEffectVoice&,const ContinuousSoundEmission&,const ContinuousSoundMotion&,float camera_x,uint32_t base_frequency,float time_scale,ContinuousMixContext);
    bool service_continuous_user_pause(bool paused);
    OwnedEffectReceipt play_owned_pcm_stream(std::function<AudioSample()> next,AudioMix);
    OwnedEffectReceipt play_owned_music_stream(std::shared_ptr<Ps2MusicStream>,AudioMix);
    // Observed driver sample-frame counter (DWORD, including its native wrap).
    // Never substitute decoder read-ahead, wall-clock time or queued duration.
    std::optional<uint32_t> owned_stream_position(const OwnedEffectVoice&)const;
    bool pump_loop_output();
    OwnedEffectReceipt stop_owned_effect_checked(const OwnedEffectVoice&);
    OwnedEffectReceipt pause_owned_effect(const OwnedEffectVoice&,bool paused);
    std::optional<bool> owned_effect_finished(const OwnedEffectVoice&)const;
    bool stop_owned_effect(const OwnedEffectVoice&);
    OwnedEffectReceipt update_owned_effect(const OwnedEffectVoice&,AudioMix,uint32_t frequency,bool write_frequency);
    OwnedEffectReceipt update_owned_distance(const OwnedEffectVoice&,uint8_t emitting,float maximum,float distance,uint8_t pan,uint32_t frequency,bool write_frequency);
    OwnedEffectReceipt update_owned_motion(const OwnedEffectVoice&,const ContinuousSoundEmission&,const ContinuousSoundMotion&,uint8_t pan,uint32_t base_frequency);
    OwnedEffectReceipt update_owned_spatial(const OwnedEffectVoice&,const ContinuousSoundEmission&,const ContinuousSoundMotion&,float camera_x,uint32_t base_frequency);
};
}
