#pragma once

#include "lcs/audio.hpp"
#include "lcs/ped_audio.hpp"
#include "lcs/source_audio_random.hpp"
#include "sa_audio_jobs.hpp"
#include "sa_audio_mixer.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace lcs {

class SaPedAudio {
public:
    using ListenerProvider = std::function<std::optional<SpatialListener>()>;
    using ModelRoleResolver = std::function<SaPedVoiceRole(int16_t native_model)>;

private:
    const AudioArchive& archive_;
    std::shared_ptr<SaAudioMixer> mixer_;
    ListenerProvider listener_;
    SourceAudioRandom random_;
    std::mutex sample_mutex_;
    ModelRoleResolver model_role_resolver_;
    std::unordered_map<uint32_t, std::shared_ptr<const AudioSample>> samples_;
    std::unordered_set<uint32_t> loading_;
    std::unordered_set<uint32_t> failed_;
    std::atomic<bool> stopping_{false};
    std::unique_ptr<AudioWorker> worker_;

    void request_range(LcsPedVoiceRange range);
    std::shared_ptr<const AudioSample> sample(uint32_t index);
    bool replace_accepted(void* speech_entity, uint16_t audio_event,
                          uint16_t context, uint32_t delay_ms,
                          bool force_audible, bool frontend);

public:
    SaPedAudio(const AudioArchive& archive, std::shared_ptr<SaAudioMixer> mixer,
               ListenerProvider listener);
    ~SaPedAudio();
    SaPedAudio(const SaPedAudio&) = delete;
    SaPedAudio& operator=(const SaPedAudio&) = delete;

    // Set before installing the hook. Map only exact, unambiguous equivalent
    // names; unresolved/ambiguous native models stay generic.
    void set_model_resolver(ModelRoleResolver resolver);

    // Called only from the verified CPed::Say AddSayEvent CALL wrapper and
    // only after native scheduling returned an accepted slot.
    bool replace_if_ready(void* speech_entity, uint16_t audio_event,
                          uint16_t context, uint32_t delay_ms,
                          bool force_audible, bool frontend);
    void shutdown();
};

// The provider is installed in the game module's initialization path; it must
// return nullptr until the campaign-owned audio archive and mixer are ready.
bool sa_ped_audio_profile_matches();
bool sa_ped_audio_install(std::function<SaPedAudio*()> provider);

} // namespace lcs
