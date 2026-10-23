#pragma once
#include "sa_audio.hpp"
#include "sa_audio_jobs.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include "lcs/audio.hpp"
#include "lcs/vehicle_audio.hpp"
#include <array>
#include <string>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
namespace lcs {
// SA 1.0 US vehicle audio entry points (all checked against the exe; addresses
// and structures follow the gta-reversed CAEVehicleAudioEntity layout):
//   ProcessVehicle            0x501E10  per-vehicle audio update
//   PlayHornOrSiren           0x4F99D0  thiscall(horn, siren, fastSiren, tVehicleParams*) ret 0x10, call at 0x500366
//   ProcessPlayerVehicleEngine 0x4FBB10 thiscall(tVehicleParams*) ret 4, calls 0x501FD6 / 0x5021C2
//   ProcessDummyVehicleEngine  0x501480 thiscall(tVehicleParams*) ret 4, calls 0x501FE4 / 0x5021D0
//   AddAudioEvent(event,float) 0x4F6420 thiscall ret 8 (door, boot, bonnet and engine start events)
struct SaVehicleAudioProfile {
    static constexpr uintptr_t horn_siren=0x4F99D0,player_engine=0x4FBB10,dummy_engine=0x501480,add_event=0x4F6420;
    static constexpr uintptr_t horn_siren_call=0x500366;
    // All 30 CALLs of AddAudioEvent(event, float). The function itself must stay untouched: this exe
    // (Hoodlum build) reads the bytes of its prologue (adc ecx,[0x4F6423]), so a detour there crashes at load.
    static constexpr std::array<uintptr_t,30> add_event_calls{0x4FB0CD,0x4FBD5B,0x4FCE74,0x4FCED7,0x5F0BC2,0x5F110C,0x656696,0x65689D,
        0x66414B,0x66707A,0x6A0AA2,0x6A0B37,0x6A0CA4,0x6A13C2,0x6A3342,0x6A6C10,0x6A6CBF,0x6A83DA,0x6A983E,0x6A9FB4,0x6AE472,0x6AE75B,
        0x6B1594,0x6B17CA,0x6BEBA1,0x6C53CB,0x6CEFCE,0x6CFFBB,0x6DCF48,0x6F91D0};
    static constexpr std::array<uintptr_t,2> player_engine_calls{0x501FD6,0x5021C2};
    static constexpr std::array<uintptr_t,2> dummy_engine_calls{0x501FE4,0x5021D0};
};
bool sa_vehicle_audio_profile_matches();
// Redirects every CALL of AddAudioEvent to `hook` (thiscall(this, event, value), ret 8). All sites are
// verified first; nothing is patched unless every one targets AddAudioEvent.
bool sa_vehicle_event_calls(uintptr_t hook);

struct VehicleSoundConfig {bool horns=false,sirens=false,doors=false,engines=false;};
// Takes over SA's vehicle sounds for the converted original vehicles with the
// LCS samples of the model each SA slot stands for. Everything not taken over
// (other models, other sounds, a failed voice) keeps running in SA.
//
// The SA hooks only decide what should sound and publish it; a worker thread opens, retunes and closes the
// wave devices (each such call costs milliseconds and used to stall the game thread).
class SaVehicleSounds {
    enum Kind : uint64_t {horn_loop=0,siren_loop=1,engine_idle_loop=2,engine_rev_loop=3};
    // A looping voice wanted by the game thread and kept up to date by the worker.
    struct Slot {
        bool want=false;uint32_t sample=0,assumed_rate=0,frequency=0;uint8_t volume=0,pan=63;uint32_t touched=0;   // game thread writes
        std::optional<OwnedEffectVoice> voice;uint32_t voice_sample=0,voice_frequency=0;uint8_t voice_volume=0,voice_pan=63;uint32_t sent=0;
        bool failed=false,silent=false;                                                                          // worker writes
    };
    struct Entry {
        uint32_t touched=0;uint16_t model=0;
        bool engine_idle=true;uint32_t engine_switch=0,engine_quiet_since=0;
        enum class Engine:uint8_t{undecided,lcs,sa} engine_owner=Engine::undecided;
    };
    struct Asset {AudioSample pcm;uint32_t begin=0,end=0;bool silent=false;};   // silent: retail placeholder sample (e.g. RC idle)
    const AudioArchive& archive_;
    MissionSpeech speech_{OwnedVoiceProfile::source_audio};
    VehicleSoundConfig config_;
    std::function<std::optional<int>(int)> lcs_for_native_;
    std::unordered_map<uintptr_t,Entry> entries_;          // game thread only
    uint32_t refusals_=0;
    std::function<void(const std::string&)> log_;
    unsigned door_logged_=0;
    float range_scale_=1.0f;
    std::mutex slot_mutex_;
    std::unordered_map<uint64_t,Slot> slots_;
    std::mutex asset_mutex_;
    std::unordered_map<uint64_t,Asset> assets_;
    SourceAudioRandom random_;                              // worker only
    std::atomic<float> effects_scale_{1.0f};
    const Asset* asset(uint32_t sample,uint32_t assumed_rate);
    static uint64_t key(uintptr_t entity,Kind kind) {return (uint64_t(entity)<<2)|kind;}
    void publish(uint64_t key,uint32_t sample,uint32_t assumed_rate,uint32_t frequency,uint8_t volume,uint8_t pan);
    void release(uint64_t key);
    bool failed(uint64_t key);
    void tick();                                            // worker thread
    Entry* entry_for(uintptr_t entity,uint16_t model,uint32_t now);
    void release_entry(uintptr_t entity);
    std::unique_ptr<AudioWorker> worker_;                   // last member: starts after everything above exists
public:
    SaVehicleSounds(const AudioArchive& archive,VehicleSoundConfig config);
    ~SaVehicleSounds(){shutdown();}
    const VehicleSoundConfig& config() const {return config_;}
    void set_range_scale(float value){range_scale_=value;}
    void set_logger(std::function<void(const std::string&)> value){log_=std::move(value);}
    void set_resolver(std::function<std::optional<int>(int)> value){lcs_for_native_=std::move(value);}
    bool has_resolver() const {return bool(lcs_for_native_);}
    // SA's SFX menu volume (master * fader), refreshed every audio frame.
    void set_effects_scale(float value){effects_scale_=value;}
    // PlayHornOrSiren. taken_horn / taken_siren say which of the two LCS now plays.
    void horn_siren(const void* audio_entity,bool horn,bool siren,bool fast,const void* params,bool& taken_horn,bool& taken_siren);
    // ProcessPlayer/DummyVehicleEngine. True: the LCS engine owns this vehicle and SA's call is skipped.
    bool engine(const void* audio_entity,const void* params,bool player);
    // AddAudioEvent. True: LCS plays (or deliberately drops) the door / starter sound.
    bool vehicle_event(const void* audio_entity,int32_t event);
    // Releases voices of vehicles SA no longer updates.
    void sweep(uint32_t now);
    void shutdown();
    static constexpr int door_open_first=78,door_close_last=89,start_engine_event=103;
};
}
