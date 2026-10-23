#pragma once

#include "lcs/audio.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lcs {


// Device-independent PCM mixer. Producers may publish/update/release loops from
// the game thread; render() is called only by the output worker (or a test).
// Loop IDs use the lower 63 bits. Samples remain alive through shared ownership.
class SaAudioMixerCore {
public:
    static constexpr uint32_t output_rate = 44100;
    static constexpr size_t output_frames = 441; // 10 ms at 44.1 kHz
    static constexpr size_t max_loops = 64;
    static constexpr size_t max_one_shots = 128;

private:
    struct Request {
        std::shared_ptr<const AudioSample> sample;
        uint32_t begin = 0, end = 0, frequency = 0;
        uint8_t volume = 0, pan = 63;
        bool loop = false;
    };
    struct Voice {
        Request request;
        double position = 0;
        float rendered_frequency = 0;
        bool finished = false;
    };

    mutable std::mutex request_mutex_;
    std::unordered_map<uint64_t, Request> loops_;
    std::unordered_set<uint64_t> released_loops_;
    std::deque<Request> one_shots_;
    std::atomic<size_t> active_one_shots_{0};
    std::atomic<float> effects_scale_{1.0f};
    std::unordered_map<uint64_t, Voice> voices_; // render thread only
    std::vector<uint64_t> finished_; // reused on the render thread to avoid per-block allocation
    uint64_t next_one_shot_id_ = uint64_t(1) << 63;

    static bool valid(const Request& request);
    static int16_t read_frame(const AudioSample& sample, size_t frame, size_t channel);
    static int16_t limit_sample(double sample);
    void sync_requests();

public:
    SaAudioMixerCore();
    // PCM is signed 16-bit mono/stereo. Loop bounds are aligned byte offsets
    // with an exclusive end; frequency 0 uses AudioSample::frequency.
    // Updating an ID preserves its playback phase when PCM and bounds match.
    bool publish_loop(uint64_t id, std::shared_ptr<const AudioSample> pcm,
                      uint32_t begin, uint32_t end, uint32_t frequency,
                      uint8_t volume, uint8_t pan);
    void release_loop(uint64_t id);
    // One-shot uses the full PCM range. Volume and pan use the SA 0..127 scale.
    bool play_one_shot(std::shared_ptr<const AudioSample> pcm, uint32_t frequency,
                       uint8_t volume, uint8_t pan);
    void set_effects_scale(float value);

    // Writes interleaved stereo PCM16. Calls to render() must be serialized.
    void render(size_t frames, std::vector<int16_t>& output);
};

// One WinMM output stream for all voices owned by this adapter. Device calls
// run on its worker thread; the public methods only update bounded PCM state.
class SaAudioMixer {
    struct Runtime;
    SaAudioMixerCore core_;
    std::unique_ptr<Runtime> runtime_;

public:
    SaAudioMixer();
    ~SaAudioMixer();
    SaAudioMixer(const SaAudioMixer&) = delete;
    SaAudioMixer& operator=(const SaAudioMixer&) = delete;

    // Same ID/phase and PCM argument contract as SaAudioMixerCore.
    bool publish_loop(uint64_t id, std::shared_ptr<const AudioSample> pcm,
                      uint32_t begin, uint32_t end, uint32_t frequency,
                      uint8_t volume, uint8_t pan);
    void release_loop(uint64_t id);
    bool play_one_shot(std::shared_ptr<const AudioSample> pcm, uint32_t frequency,
                       uint8_t volume, uint8_t pan);
    void set_effects_scale(float value);
    bool available() const;
    void shutdown();
};

}
