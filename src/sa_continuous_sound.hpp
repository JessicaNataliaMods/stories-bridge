#pragma once

#include "lcs/audio.hpp"
#include "lcs/continuous_audio_entities.hpp"
#include "sa_audio.hpp"

#include <array>
#include <cmath>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace lcs {

// Values sampled by the real audio-frame producer. In particular, time_spent,
// speed_of_sound, time_scale and the mix state have no synthetic defaults.
struct SaContinuousSoundFrame {
    SpatialListener listener;
    uint8_t time_spent;
    float speed_of_sound;
    float time_scale;
    bool camera_switched;
    bool user_paused;
    ContinuousMixContext mix;
};

// Runtime owner for the verified PCM continuous-sound selectors. This owns a
// separate MissionSpeech instance so it cannot reclaim dialogue, telephone,
// announcements or music voices.
class SaContinuousSound {
    using Snapshot = ContinuousSoundObjects::Snapshot;
    struct Asset {
        AudioSample pcm;
        AudioRecord record{};
        uint32_t base_frequency = 0;
    };
    struct Owner {
        bool used = false;
        bool quarantined = false;
        Snapshot source{};
        ContinuousSoundEmission emission{};
        ContinuousSoundKind kind = ContinuousSoundKind::none;
        std::shared_ptr<const Asset> asset;
        std::optional<OwnedEffectVoice> voice;
        float previous_distance = 0.0f;
        bool has_previous_distance = false;
    };
    struct SpatialPoint { float distance; float camera_x; };

    const AudioArchive& archive_;
    ContinuousAudioEntities entities_;
    MissionSpeech speech_{OwnedVoiceProfile::source_audio};
    std::array<Owner, 250> owners_{};
    std::unordered_map<uint32_t, std::shared_ptr<const Asset>> assets_;
    mutable std::mutex mutex_;
    bool global_quarantine_ = false;
    bool audio_paused_ = false;
    bool pause_service_pending_ = false;

    static bool same_owner(ContinuousSoundObjects::Token a,
                           ContinuousSoundObjects::Token b) noexcept {
        return a.reference == b.reference && a.epoch == b.epoch;
    }
    static std::optional<SpatialPoint> spatial_point(
        const std::array<float, 3>& source, const SpatialListener& listener) noexcept {
        float squared = 0.0f;
        float camera_x = 0.0f;
        for (size_t i = 0; i < 3; ++i) {
            if (!std::isfinite(source[i]) || !std::isfinite(listener.position[i]) ||
                !std::isfinite(listener.right[i])) return {};
            const float delta = source[i] - listener.position[i];
            squared += delta * delta;
            camera_x += delta * listener.right[i];
        }
        if (!std::isfinite(squared) || !std::isfinite(camera_x)) return {};
        const float distance = std::sqrt(squared);
        if (!std::isfinite(distance)) return {};
        return SpatialPoint{distance, camera_x};
    }
    static bool valid_frame(const SaContinuousSoundFrame& frame) noexcept {
        if (!std::isfinite(frame.speed_of_sound) || frame.speed_of_sound <= 0.0f ||
            !frame.time_spent || !std::isfinite(frame.time_scale) || frame.time_scale < 0 ||
            frame.mix.global_sfx_volume > 127 || !std::isfinite(frame.mix.native_scale) ||
            frame.mix.native_scale < 0 || frame.mix.native_scale > 1)
            return false;
        for (size_t i = 0; i < 3; ++i)
            if (!std::isfinite(frame.listener.position[i]) ||
                !std::isfinite(frame.listener.right[i])) return false;
        return true;
    }
    std::shared_ptr<const Asset> asset_for(const ContinuousSoundEmission& emission) {
        const auto found = assets_.find(emission.sample);
        if (found != assets_.end()) return found->second;
        auto asset = std::make_shared<Asset>();
        asset->record = archive_.record(emission.sample);
        asset->pcm = archive_.sample(emission.sample);
        asset->base_frequency = emission.fixed_frequency
            ? emission.fixed_frequency : archive_.record(emission.frequency_sample).frequency;
        if (!asset->base_frequency || asset->base_frequency > 192000 ||
            asset->record.frequency != asset->pcm.frequency || asset->pcm.channels != 1 ||
            asset->pcm.pcm.size() != asset->record.size ||
            !continuous_pcm_loop_span(asset->record.size, asset->record.loop_start,
                                      asset->record.loop_end))
            return {};
        auto owned = std::shared_ptr<const Asset>(std::move(asset));
        assets_.emplace(emission.sample, owned);
        return owned;
    }
    void clear_voice_receipts() noexcept {
        for (auto& owner : owners_) owner.voice.reset();
    }
    bool retire_all_for_recovery() {
        if (!speech_.shutdown()) {
            global_quarantine_ = true;
            return false;
        }
        clear_voice_receipts();
        global_quarantine_ = false;
        return true;
    }
    bool retire_owner(Owner& owner) {
        if (!owner.voice) return true;
        const auto receipt = speech_.stop_owned_effect_checked(*owner.voice);
        if (!receipt.accepted) {
            owner.quarantined = true;
            return false;
        }
        owner.voice.reset();
        return true;
    }
    bool start_owner(Owner& owner, const SpatialPoint& point,
                     const SaContinuousSoundFrame& frame) {
        const auto volume = continuous_sound_volume(owner.emission.volume,
                                                     owner.emission.distance,
                                                     point.distance);
        const auto pan = continuous_sound_pan(point.distance, point.camera_x);
        if (!volume || !pan) return false;
        const AudioMix mix{*volume, *pan};
        OwnedEffectReceipt receipt;
        try {
            receipt = speech_.play_owned_continuous_sample(
                owner.asset->pcm, owner.asset->record, mix,
                ContinuousVoiceStart{owner.asset->base_frequency, frame.time_scale,
                                     frame.mix});
        } catch (...) {
            // The call may have entered WinMM without returning a receipt.
            owner.quarantined = true;
            retire_all_for_recovery();
            return false;
        }
        if (receipt.accepted && receipt.voice) {
            owner.voice = receipt.voice;
            owner.previous_distance = point.distance;
            owner.has_previous_distance = true;
            return true;
        }
        if (receipt.entered) {
            // A failed start can have no voice token. Retire the dedicated
            // MissionSpeech instance before allowing any retry or entity free.
            owner.quarantined = true;
            retire_all_for_recovery();
            return false;
        }
        return false; // No driver entry: capacity/transient refusal can retry.
    }
    bool service_owner(Owner& owner, const SaContinuousSoundFrame& frame) {
        if (owner.quarantined) return false;
        if(owner.kind==ContinuousSoundKind::music_configuration)
            return configure_music && configure_music(uint8_t(owner.source.object.sound));
        // Retail 1B2A98 never requests a voice for these selectors (configuration-only,
        // CITY music setters or the default branch); the entity exists and is serviced silently.
        if (owner.kind != ContinuousSoundKind::voice) return true;
        const auto point = spatial_point(owner.source.object.position, frame.listener);
        if (!point) return false;
        if (point->distance >= owner.emission.distance) {
            if (!owner.voice) return true; // Retail emitter's strict distance gate.
            // SA/project adaptation: the LCS shared request queue (releasing fade, priority
            // ranking against other queue samples) is deliberately not rebuilt. Leaving the
            // audible range ends this owner's own voice with a checked stop; re-entering the
            // range requests a fresh voice. A refused stop quarantines only this owner.
            owner.has_previous_distance = false;
            return retire_owner(owner);
        }
        if (owner.voice) {
            const auto finished = speech_.owned_effect_finished(*owner.voice);
            if (!finished) {
                owner.quarantined = true;
                retire_owner(owner);
                return false;
            }
            if (*finished) {
                if (!retire_owner(owner)) return false;
                // Selector 0 is an authored finite one-shot. The source queue
                // requests it again once the previous channel has retired.
                return start_owner(owner, *point, frame);
            }
            const float previous = owner.has_previous_distance
                ? owner.previous_distance : point->distance;
            const ContinuousSoundMotion motion{
                previous, point->distance, frame.speed_of_sound, frame.time_spent,
                frame.camera_switched};
            const auto updated = speech_.update_owned_continuous_frame(
                *owner.voice, owner.emission, motion, point->camera_x,
                owner.asset->base_frequency, frame.time_scale, frame.mix);
            if (!updated.accepted) {
                if (updated.entered) {
                    owner.quarantined = true;
                    retire_owner(owner);
                }
                return false;
            }
            owner.previous_distance = point->distance;
            owner.has_previous_distance = true;
            return true;
        }
        return start_owner(owner, *point, frame);
    }

public:
    std::function<bool()> music_available;
    std::function<bool(uint8_t)> configure_music;
    explicit SaContinuousSound(const AudioArchive& archive) : archive_(archive) {}
    SaContinuousSound(const SaContinuousSound&) = delete;
    SaContinuousSound& operator=(const SaContinuousSound&) = delete;

    bool available() const {
        std::lock_guard<std::mutex> guard(mutex_);
        return !global_quarantine_;
    }
    bool quarantined() const {
        std::lock_guard<std::mutex> guard(mutex_);
        if (global_quarantine_) return true;
        for (const auto& owner : owners_) if (owner.used && owner.quarantined) return true;
        return false;
    }
    const ContinuousAudioEntities& entities() const noexcept { return entities_; }

    ContinuousEntityReceipt create(const Snapshot& source) {
        std::lock_guard<std::mutex> guard(mutex_);
        if (global_quarantine_) return {};
        // Every selector owns an audio entity (retail CreateLoopingScriptObject); only the
        // five voice selectors request output, so only they need a verified PCM asset.
        const auto kind = continuous_sound_kind(source.object.sound);
        // Selectors 40..45 configure the MusicManager CITY track position (retail 16A488/16A530).
        // A missing CITY consumer refuses before any entity or handle exists.
        // A bound consumer applies each selector at the actual audio service.
        if (kind == ContinuousSoundKind::music_configuration &&
            (!configure_music || !music_available || !music_available())) return {};
        std::optional<ContinuousSoundEmission> emission;
        std::shared_ptr<const Asset> asset;
        if (kind == ContinuousSoundKind::voice) {
            emission = continuous_sound_emission(source.object.sound);
            if (!emission) return {};
            try { asset = asset_for(*emission); }
            catch (...) { return {}; }
            if (!asset) return {};
        }
        const auto receipt = entities_.create(source);
        if (!receipt.accepted || receipt.entity < 0) return receipt;
        const auto index = static_cast<size_t>(receipt.entity);
        if (index >= owners_.size() || owners_[index].used) {
            auto attached = source;
            attached.object.audio_entity = receipt.entity;
            entities_.release(attached);
            return {};
        }
        auto attached = source;
        attached.object.audio_entity = receipt.entity;
        auto& owner = owners_[index];
        owner = Owner{};
        owner.used = true;
        owner.source = attached;
        owner.kind = kind;
        if (emission) owner.emission = *emission;
        owner.asset = std::move(asset);
        return receipt;
    }

    ContinuousEntityReceipt release(const Snapshot& source) {
        std::lock_guard<std::mutex> guard(mutex_);
        if (source.object.audio_entity == -3) return entities_.release(source);
        const auto id = source.object.audio_entity;
        if (id < 0 || static_cast<size_t>(id) >= owners_.size()) return {};
        auto& owner = owners_[static_cast<size_t>(id)];
        if (!owner.used || !same_owner(owner.source.token, source.token)) return {};
        if (global_quarantine_ && !retire_all_for_recovery()) return {true, false, id};
        if (owner.voice && !retire_owner(owner)) return {true, false, id};
        auto receipt = entities_.release(source);
        if (!receipt.accepted) return receipt;
        owner = Owner{};
        return receipt;
    }

    bool service_frame(const SaContinuousSoundFrame& frame) {
        std::lock_guard<std::mutex> guard(mutex_);
        if (global_quarantine_ || !valid_frame(frame)) return false;
        if (frame.user_paused || audio_paused_ || pause_service_pending_) {
            pause_service_pending_ = true;
            if (!speech_.service_continuous_user_pause(frame.user_paused)) return false;
            clear_voice_receipts();
            audio_paused_ = frame.user_paused;
            pause_service_pending_ = false;
            if (frame.user_paused) return true;
        }
        bool accepted = true;
        for (const auto& source : entities_.service_order()) {
            const auto id = source.object.audio_entity;
            if (id < 0 || static_cast<size_t>(id) >= owners_.size()) {
                if (id != -3) accepted = false;
                continue;
            }
            auto& owner = owners_[static_cast<size_t>(id)];
            if (!owner.used || !same_owner(owner.source.token, source.token)) {
                accepted = false;
                continue;
            }
            if (!service_owner(owner, frame)) accepted = false;
            if (global_quarantine_) return false;
        }
        return accepted;
    }

    // Retries only checked retirement; it never replays a quarantined owner.
    bool recover() {
        std::lock_guard<std::mutex> guard(mutex_);
        if (global_quarantine_ && !retire_all_for_recovery()) return false;
        bool accepted = true;
        for (auto& owner : owners_) {
            if (!owner.used || !owner.quarantined || !owner.voice) continue;
            if (retire_owner(owner)) owner.quarantined = true;
            else accepted = false;
        }
        return accepted;
    }

    bool shutdown() {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!speech_.shutdown()) {
            global_quarantine_ = true;
            return false;
        }
        clear_voice_receipts();
        global_quarantine_ = false;
        audio_paused_ = false;
        pause_service_pending_ = false;
        return true;
    }
};

} // namespace lcs
