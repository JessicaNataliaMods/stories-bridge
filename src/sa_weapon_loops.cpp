#include "sa_weapon_loops.hpp"
#include "lcs/continuous_sound.hpp"
#include "lcs/sa_executor.hpp"
#include "sa_weapon_audio.hpp"
#include <windows.h>
#include <algorithm>
#include <cmath>

namespace lcs {
namespace {
struct WeaponMix {
    uint8_t volume = 0, pan = 63;
    float distance = 0.0f;
    bool valid = false;
    bool in_range = false;
};

WeaponMix weapon_mix(const std::array<float, 3>& source, const SpatialListener& listener,
                     float maximum, uint8_t emitting) {
    float squared = 0.0f, camera_x = 0.0f;
    for (size_t axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(source[axis]) || !std::isfinite(listener.position[axis]) ||
            !std::isfinite(listener.right[axis])) return {};
        const float delta = source[axis] - listener.position[axis];
        squared += delta * delta;
        camera_x += delta * listener.right[axis];
    }
    if (!std::isfinite(squared) || !std::isfinite(camera_x) || squared < 0.0f) return {};
    const float distance = std::sqrt(squared);
    if (!(distance < maximum)) return {0, 63, distance, true, false};
    const auto volume = continuous_sound_volume(emitting, maximum, distance);
    const auto pan = continuous_sound_pan(distance, camera_x);
    if (!volume || !pan) return {};
    return {*volume, *pan, distance, true, true};
}

constexpr uint8_t minigun_attack_slot = 0;
constexpr uint8_t flamethrower_left_slot = 2;
constexpr uint8_t flamethrower_right_slot = 3;
}

uint64_t SaWeaponLoops::loop_id(const void* entity, uint8_t kind) {
    // SA is 32-bit; reserving the low two bits gives this client stable,
    // disjoint IDs within the mixer's low-63-bit loop namespace.
    const auto address = uint32_t(reinterpret_cast<uintptr_t>(entity));
    return 0x2000000000000000ull | (uint64_t(address) << 2) | kind;
}

const SaWeaponLoops::Asset* SaWeaponLoops::asset(uint32_t sample, bool looping) {
    const uint64_t key = (uint64_t(sample) << 1) | uint64_t(looping);
    if (const auto found = assets_.find(key); found != assets_.end()) return &found->second;
    try {
        auto pcm = archive_.sample(sample);
        const auto& record = archive_.record(sample);
        if (pcm.channels != 1 || pcm.pcm.empty() || pcm.pcm.size() % 2 || !pcm.frequency ||
            pcm.pcm.size() > UINT32_MAX || record.size != pcm.pcm.size()) return nullptr;
        const auto span = continuous_pcm_loop_span(record.size, record.loop_start, record.loop_end);
        // A source loop request may carry sentinel -1: SetChannelLoopPoints
        // then selects the whole sample (the checked helper handles both forms).
        if (looping && !span) return nullptr;
        Asset decoded;
        decoded.pcm = std::make_shared<const AudioSample>(std::move(pcm));
        // Source one-shots reset loop offsets even if the authored sample also
        // carries PS2 loop flags; only continuous voices consume that metadata.
        decoded.begin = looping ? span->begin : 0;
        decoded.end = looping ? span->end : uint32_t(record.size);
        if (decoded.begin >= decoded.end) return nullptr;
        return &assets_.emplace(key, std::move(decoded)).first->second;
    } catch (const std::exception&) {
        return nullptr;
    }
}

void SaWeaponLoops::release(uint64_t id) {
    if (mixer_) mixer_->release_loop(id);
    requests_.erase(id);
}

bool SaWeaponLoops::publish(WeaponLoopCue cue, const void* entity, uint32_t now,
                            const std::array<float, 3>& position, const SpatialListener& listener) {
    const auto spec = weapon_loop_sample_spec(cue);
    if (!spec || !spec->looping) return false;
    return publish_sample(cue, entity,
                          cue == WeaponLoopCue::minigun_ammo ? minigun_attack_slot : flamethrower_left_slot,
                          spec->sample, spec->frequency, spec->volume, spec->max_distance,
                          now, position, listener);
}

bool SaWeaponLoops::publish_sample(WeaponLoopCue cue, const void* entity, uint8_t slot, uint32_t sample,
                                  uint32_t frequency, uint8_t volume, float max_distance, uint32_t now,
                                  const std::array<float, 3>& position, const SpatialListener& listener,
                                  std::optional<uint8_t> forced_pan) {
    const uint64_t id = loop_id(entity, slot);
    const auto mixed = weapon_mix(position, listener, max_distance, volume);
    if (!mixed.valid) { release(id); return false; }
    if (!mixed.in_range) {
        release(id);
        return true; // Recognized LCS event; retail is silent beyond this radius.
    }
    const auto* decoded = asset(sample, true);
    if (!decoded) { release(id); return false; }
    if (!mixer_->publish_loop(id, decoded->pcm, decoded->begin, decoded->end,
                             frequency ? frequency : decoded->pcm->frequency,
                             mixed.volume, forced_pan.value_or(mixed.pan))) {
        release(id);
        return false;
    }
    auto& request = requests_[id];
    request.touched = now;
    request.ttl_ms = weapon_loop_request_ttl_ms(cue);
    request.cue = cue;
    request.position = position;
    return true;
}

bool SaWeaponLoops::play_stop(const WeaponLoopSampleSpec& spec,
                              const std::array<float, 3>& position, const SpatialListener& listener) {
    const auto mixed = weapon_mix(position, listener, spec.max_distance, spec.volume);
    if (!mixed.valid) return false;
    if (!mixed.in_range) return true;
    const auto* decoded = asset(spec.sample, false);
    if (!decoded) return false;
    return mixer_->play_one_shot(decoded->pcm,
                                spec.frequency ? spec.frequency : decoded->pcm->frequency,
                                mixed.volume, mixed.pan);
}

bool SaWeaponLoops::event(int32_t type, const void* entity, int32_t event_code) {
    const auto cue = sa_weapon_loop_cue(type, event_code);
    if (!cue) return false;
    if (!entity) return false;

    if (*cue == WeaponLoopCue::minigun_no_ammo) {
        // This SA event is not replaced: stop the LCS firing request, then let
        // the original no-ammo sound path run in the hook's fallback.
        release(loop_id(entity, minigun_attack_slot));
        return false;
    }
    if (!mixer_ || !mixer_->available()) {
        if (*cue == WeaponLoopCue::minigun_ammo) release(loop_id(entity, minigun_attack_slot));
        else {
            release(loop_id(entity, flamethrower_left_slot));
            release(loop_id(entity, flamethrower_right_slot));
        }
        return false;
    }

    const auto position = sa_physical_position(entity);
    const auto listener = listener_ ? listener_() : std::nullopt;
    if (!position || !listener) {
        if (*cue == WeaponLoopCue::minigun_ammo) release(loop_id(entity, minigun_attack_slot));
        else {
            release(loop_id(entity, flamethrower_left_slot));
            release(loop_id(entity, flamethrower_right_slot));
        }
        return false;
    }
    const auto spec = weapon_loop_sample_spec(*cue);
    if (!spec) return false;
    if (!spec->looping) return play_stop(*spec, *position, *listener);

    const uint32_t now = GetTickCount();
    const uint64_t id = loop_id(entity, *cue == WeaponLoopCue::minigun_ammo ? minigun_attack_slot : flamethrower_left_slot);
    const bool new_flame = *cue == WeaponLoopCue::flamethrower_fire && requests_.find(id) == requests_.end();
    bool accepted = false;
    if (*cue == WeaponLoopCue::flamethrower_fire && spec->paired_sample) {
        const auto* left_asset = asset(spec->sample, true);
        if (!left_asset) {
            release(loop_id(entity, flamethrower_left_slot));
            release(loop_id(entity, flamethrower_right_slot));
            return false;
        }
        // reLCS computes frequency from the left sample before incrementing
        // the sample index for the paired right-channel voice.
        const uint32_t frequency = spec->frequency ? spec->frequency : left_asset->pcm->frequency;
        const auto mixed = weapon_mix(*position, *listener, spec->max_distance, spec->volume);
        if (!mixed.valid) {
            release(loop_id(entity, flamethrower_left_slot));
            release(loop_id(entity, flamethrower_right_slot));
            return false;
        }
        if (!mixed.in_range) {
            release(loop_id(entity, flamethrower_left_slot));
            release(loop_id(entity, flamethrower_right_slot));
            return true;
        }
        const bool stereo_pair = spec->paired_stereo_distance > 0.0f &&
                                 mixed.distance < spec->max_distance * spec->paired_stereo_distance;
        if (stereo_pair) {
            accepted = publish_sample(*cue, entity, flamethrower_left_slot, spec->sample,
                                      frequency, spec->volume, spec->max_distance,
                                      now, *position, *listener, uint8_t(0)) &&
                       publish_sample(*cue, entity, flamethrower_right_slot, spec->paired_sample,
                                      frequency, spec->volume, spec->max_distance,
                                      now, *position, *listener, uint8_t(127));
        } else {
            release(loop_id(entity, flamethrower_right_slot));
            accepted = publish_sample(*cue, entity, flamethrower_left_slot, spec->sample,
                                      frequency, spec->volume, spec->max_distance,
                                      now, *position, *listener);
        }
        if (!accepted) {
            release(loop_id(entity, flamethrower_left_slot));
            release(loop_id(entity, flamethrower_right_slot));
            return false;
        }
    } else {
        accepted = publish(*cue, entity, now, *position, *listener);
        if (!accepted) return false;
    }
    if (new_flame && spec->start_sample) {
        const auto* start = asset(spec->start_sample, false);
        if (start) {
            const auto mixed = weapon_mix(*position, *listener, spec->max_distance, spec->start_volume);
            if (mixed.valid && mixed.in_range) {
                mixer_->play_one_shot(start->pcm, start->pcm->frequency, mixed.volume, mixed.pan);
            }
        }
    }
    return true;
}

void SaWeaponLoops::service(uint32_t now, bool paused, float effects_scale) {
    if (!mixer_) return;
    if (paused || !std::isfinite(effects_scale) || effects_scale <= 0.0f) {
        // SA's shared effects service owns the mixer scale. A paused/menu or
        // muted frame releases this family's IDs only; it never stops the
        // shared stream used by the other audio families.
        for (const auto& request : requests_) mixer_->release_loop(request.first);
        requests_.clear();
        return;
    }
    for (auto it = requests_.begin(); it != requests_.end();) {
        if (weapon_loop_request_expired(now, it->second.touched, it->second.ttl_ms)) {
            const auto cue = it->second.cue;
            const auto position = it->second.position;
            mixer_->release_loop(it->first);
            it = requests_.erase(it);
            if (cue == WeaponLoopCue::minigun_ammo) {
                // LCS queues SOUND_WEAPON_MINIGUN_3 after the 3-frame firing
                // request expires; use the last safe world point as its tail.
                if (mixer_->available() && listener_) {
                    if (const auto listener = listener_()) {
                        const auto spec = weapon_loop_sample_spec(WeaponLoopCue::minigun_stop);
                        if (spec) play_stop(*spec, position, *listener);
                    }
                }
            }
        } else {
            ++it;
        }
    }
}

void SaWeaponLoops::shutdown() {
    if (mixer_) for (const auto& request : requests_) mixer_->release_loop(request.first);
    requests_.clear();
}

} // namespace lcs
