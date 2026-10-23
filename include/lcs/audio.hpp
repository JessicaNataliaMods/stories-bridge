#pragma once
#include "vm.hpp"
#include "continuous_sound.hpp"
#include "source_audio_random.hpp"
#include "weapon_audio.hpp"
#include <unordered_map>
#include <optional>
#include <memory>
#include <mutex>

namespace lcs {
struct AudioSample {uint32_t frequency=0;std::vector<uint8_t> pcm;uint16_t channels=1;};
struct AudioRecord {uint32_t offset,size,frequency;uint32_t loop_start=0;int32_t loop_end=-1;};
struct SpatialListener {std::array<float,3> position{},right{{1,0,0}};};
struct AudioMix {uint8_t volume=127,pan=63;};
AudioMix mission_audio_mix(const std::array<float,3>& source,const SpatialListener& listener);
class AudioArchive {
    std::filesystem::path raw_;
    std::filesystem::path news_;
    std::string suffix_;
    std::vector<AudioRecord> records_;
    // Original PS2 AUDIO folder: 67 SFXn_PS banks of 12-byte SDT records
    // (offset, size, frequency) over SPU2 ADPCM; decoded on demand.
    struct Ps2Record {uint16_t bank;uint32_t offset,size,frequency;};
    struct Ps2Layout {uint32_t first,frames;AudioRecord record;};
    std::vector<std::filesystem::path> banks_;
    std::vector<Ps2Record> ps2_;
    std::shared_ptr<std::mutex> layout_mutex_;
    mutable std::vector<std::optional<AudioRecord>> layouts_;
    bool ps2_mode_=false;
    std::vector<uint8_t> ps2_bytes(uint32_t index) const;
    Ps2Layout ps2_layout(uint32_t index,const std::vector<uint8_t>& bytes,uint32_t assumed_frequency=0) const;
public:
    struct Ps2Audio {};
    explicit AudioArchive(const std::filesystem::path& directory,const std::string& suffix="");
    // Reads the user's original LCS PS2 AUDIO folder (SET0..SET6, NEWS).
    AudioArchive(Ps2Audio,const std::filesystem::path& audio_directory);
    static std::vector<AudioRecord> parse_index(const std::vector<uint8_t>& bytes,uint64_t raw_size);
    static std::vector<std::string> mission_names();
    AudioSample sample(const std::string& name) const;
    AudioSample sample(uint32_t index) const;
    // The 27 headerless engine-bank records carry no source rate: the game code that plays them always
    // supplies an absolute rate, so the caller states the rate its PCM is nominally played at.
    AudioSample sample(uint32_t index,uint32_t assumed_frequency) const;
    AudioRecord record(uint32_t index,uint32_t assumed_frequency) const;
    const AudioRecord& record(uint32_t index) const;
    AudioSample announcement(int32_t index) const;
    AudioSample frontend_effect(int32_t script_sound) const;
    AudioSample phone_ring_sample() const; // retail event76 maps to raw sample236
    // Radio retune: DoPoliceRadioCrackle and SOUND_RADIO_CHANGE samples.
    uint32_t radio_crackle_index() const;
    uint32_t radio_dial_index(uint32_t dial) const; // dial 0..2
    size_t sample_count()const {return ps2_mode_?ps2_.size():records_.size();}
};
// Mission speech is sampled PCM in LCS. SA's same-named opcode takes a numeric
// SA sound ID, so it cannot be used for these original labels.
class AudioAdapter {
    const AudioArchive& archive_;
    struct Slot {std::optional<AudioSample> sample;bool started=false;std::optional<std::array<float,3>> position;bool phone_call=false;};
    std::array<Slot,2> slots_;
    std::optional<int32_t> pending_announcement_;
    bool announcement_started_=false;
    uint8_t one_shot_counter_=0; // AudioManager's per-queued-sample counter
    SourceAudioRandom audio_random_;
    std::unordered_map<uint32_t,AudioSample> world_samples_; // decoded once, reused by every world one-shot
    const AudioSample* world_sample(uint32_t index);
    // AudioLogic::Initialise and ProcessMissionAudio own these source values.
    // PreloadMissionAudio clears the per-slot phone marker; none of the
    // currently implemented script audio operations sets it to true.
    uint8_t global_sfx_volume_=127;
public:
    std::function<bool(size_t,const AudioSample&)> play;
    std::function<bool(size_t)> finished;
    std::function<void(size_t)> stop;
    std::function<std::optional<std::array<float,3>>()> listener;
    std::function<bool(const AudioSample&)> play_effect;
    std::function<bool(const AudioSample&,AudioMix)> play_positioned_effect;
    // World one-shots that must not stall the game thread (gun shots): the device is opened elsewhere. When set it
    // replaces play_positioned_effect for them and is assumed to accept the sound.
    std::function<bool(AudioSample,AudioMix)> post_world_effect;
    // LcsOneShotRangePercent: shrinks the audible range of world one-shots (the original ranges are far larger than SA's).
    float one_shot_range_scale=1.0f;
    std::function<std::optional<SpatialListener>()> spatial_listener;
    std::function<bool(size_t,AudioMix)> set_mix;
    explicit AudioAdapter(const AudioArchive& archive):archive_(archive){}
    static bool handles(uint16_t);
    // SOUND_CAR_JERK one-shot at a vehicle position (1486): SFX_SHAG_SUSPENSION, source
    // frequency displacement and volume, 35 m range and ComputeVolume attenuation.
    // False means the sound could not be produced (missing backend/listener, bad
    // position or a refused device); out of range is a genuine silent success.
    bool play_car_jerk(const std::array<float,3>& position);
    // A world one-shot of ProcessPedOneShots. silent: the original produces no
    // voice (out of range or zero volume), so SA must not play its own sound
    // either. refused: the sound could not be produced (backend, listener or
    // sample missing, or no free voice); the caller keeps the SA native sound.
    enum class WorldSound {played,silent,refused};
    // effects_scale: SA's SFX menu volume (master * fader), applied to the voice mix.
    WorldSound play_weapon_shot(const WeaponShotSpec&,const std::array<float,3>& position,float effects_scale=1.0f);
    WorldSound play_weapon_reload(const WeaponReloadSpec&,const std::array<float,3>& position,float effects_scale=1.0f);
    bool tick_announcements(bool eligible);
    // cAudioManager::ShouldDuckMissionAudio: a started mission speech slot
    // that has not finished. The radio is held at volume 25 meanwhile.
    bool mission_speech_active() const {
        for(size_t n=0;n<slots_.size();++n)if(slots_[n].started && finished && !finished(n))return true;
        return false;
    }
    bool tick_spatial();
    ContinuousMixContext service_source_mix(float native_scale);
    uint32_t continuous_music_random_word()const {return audio_random_.table(0);}
    StepResult dispatch(Vm&,Thread&,uint16_t);
};
}
