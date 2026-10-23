#pragma once

#include "lcs/audio.hpp"
#include "lcs/weapon_loop_audio.hpp"
#include "sa_audio_mixer.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>

namespace lcs {

class SaWeaponLoops {
    struct Asset {
        std::shared_ptr<const AudioSample> pcm;
        uint32_t begin = 0, end = 0;
    };
    struct Request {
        uint32_t touched = 0;
        uint32_t ttl_ms = 0;
        WeaponLoopCue cue = WeaponLoopCue::minigun_ammo;
        std::array<float, 3> position{};
    };

    const AudioArchive& archive_;
    std::shared_ptr<SaAudioMixer> mixer_;
    std::function<std::optional<SpatialListener>()> listener_;
    std::unordered_map<uint64_t, Asset> assets_;
    std::unordered_map<uint64_t, Request> requests_;

    const Asset* asset(uint32_t sample, bool looping);
    static uint64_t loop_id(const void* entity, uint8_t kind);
    void release(uint64_t id);
    bool publish(WeaponLoopCue cue, const void* entity, uint32_t now,
                 const std::array<float, 3>& position, const SpatialListener& listener);
    bool publish_sample(WeaponLoopCue cue, const void* entity, uint8_t slot, uint32_t sample,
                        uint32_t frequency, uint8_t volume, float max_distance, uint32_t now,
                        const std::array<float, 3>& position, const SpatialListener& listener,
                        std::optional<uint8_t> forced_pan = std::nullopt);
    bool play_stop(const WeaponLoopSampleSpec& spec,
                   const std::array<float, 3>& position, const SpatialListener& listener);

public:
    SaWeaponLoops(const AudioArchive& archive, std::shared_ptr<SaAudioMixer> mixer,
                  std::function<std::optional<SpatialListener>()> listener)
        : archive_(archive), mixer_(std::move(mixer)), listener_(std::move(listener)) {}
    ~SaWeaponLoops() { shutdown(); }

    // Called by the existing CAEWeaponAudioEntity::WeaponFire hook before its
    // one-shot handler. False preserves the retail SA sound on any refusal.
    bool event(int32_t sa_weapon_type, const void* entity, int32_t event);
    // Called from the audio service each frame. Pause/mute releases this
    // family's loops; effects_scale itself is owned by the shared mixer.
    void service(uint32_t now, bool paused, float effects_scale);
    void shutdown();
};

} // namespace lcs
