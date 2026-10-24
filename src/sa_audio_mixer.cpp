#include "sa_audio_mixer.hpp"
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <thread>

namespace lcs {

SaAudioMixerCore::SaAudioMixerCore() {
    voices_.reserve(max_loops+max_one_shots);
    finished_.reserve(max_loops+max_one_shots);
}

bool SaAudioMixerCore::valid(const Request& request) {
    if (!request.sample || !request.sample->frequency || request.sample->frequency > 192000 ||
        (request.sample->channels != 1 && request.sample->channels != 2) ||
        request.sample->pcm.empty() || request.sample->pcm.size() % (size_t(request.sample->channels) * 2) ||
        request.sample->pcm.size() > std::numeric_limits<uint32_t>::max() ||
        request.frequency > 192000 || request.volume > 127 || request.pan > 127) return false;
    const auto bytes_per_frame = size_t(request.sample->channels) * 2;
    return request.begin < request.end && request.end <= request.sample->pcm.size() &&
        request.begin % bytes_per_frame == 0 && request.end % bytes_per_frame == 0 &&
        (request.loop || (request.begin == 0 && request.end == request.sample->pcm.size()));
}

bool SaAudioMixerCore::publish_loop(uint64_t id, std::shared_ptr<const AudioSample> pcm,
                                    uint32_t begin, uint32_t end, uint32_t frequency,
                                    uint8_t volume, uint8_t pan) {
    if (id & (uint64_t(1) << 63)) return false;
    Request request{std::move(pcm), begin, end, frequency, volume, pan, true};
    if (!valid(request)) return false;
    try {
        std::lock_guard<std::mutex> guard(request_mutex_);
        const auto found = loops_.find(id);
        if (found == loops_.end() && loops_.size() >= max_loops) return false;
        loops_[id] = std::move(request);
        return true;
    } catch (...) { return false; }
}

void SaAudioMixerCore::release_loop(uint64_t id) {
    std::lock_guard<std::mutex> guard(request_mutex_);
    if (loops_.erase(id)) {
        try { released_loops_.insert(id); } catch (...) {}
    }
}

bool SaAudioMixerCore::play_one_shot(std::shared_ptr<const AudioSample> pcm, uint32_t frequency,
                                     uint8_t volume, uint8_t pan) {
    if (!pcm) return false;
    Request request{std::move(pcm), 0, 0, frequency, volume, pan, false};
    request.end = uint32_t(request.sample->pcm.size());
    if (!valid(request)) return false;

    auto count = active_one_shots_.load(std::memory_order_relaxed);
    do {
        if (count >= max_one_shots) return false;
    } while (!active_one_shots_.compare_exchange_weak(count, count + 1,
        std::memory_order_acq_rel, std::memory_order_relaxed));

    try {
        std::lock_guard<std::mutex> guard(request_mutex_);
        one_shots_.push_back(std::move(request));
    } catch (...) {
        active_one_shots_.fetch_sub(1, std::memory_order_acq_rel);
        return false;
    }
    return true;
}

void SaAudioMixerCore::set_effects_scale(float value) {
    effects_scale_.store(std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f,
                         std::memory_order_relaxed);
}
int16_t SaAudioMixerCore::read_frame(const AudioSample& sample, size_t frame, size_t channel) {
    int16_t value = 0;
    const auto offset = (frame * sample.channels + channel) * sizeof(value);
    if (offset + sizeof(value) <= sample.pcm.size())
        std::memcpy(&value, sample.pcm.data() + offset, sizeof(value));
    return value;
}

int16_t SaAudioMixerCore::limit_sample(double sample) {
    if (!std::isfinite(sample)) return 0;
    const double sign = sample < 0.0 ? -1.0 : 1.0;
    const double magnitude = std::fabs(sample);
    constexpr double knee = 28000.0;
    constexpr double span = 32767.0 - knee;
    const double limited = magnitude <= knee
        ? magnitude
        : knee + span * (1.0 - std::exp(-(magnitude - knee) / span));
    return int16_t(std::clamp(std::lround(sign * limited), -32768l, 32767l));
}

void SaAudioMixerCore::sync_requests() {
    std::deque<Request> shots;
    {
        std::lock_guard<std::mutex> guard(request_mutex_);
        for (const auto& [id, request] : loops_) {
            auto found = voices_.find(id);
            const bool released = released_loops_.erase(id) != 0;
            const bool replace = released || found == voices_.end() || !found->second.request.loop ||
                found->second.request.sample.get() != request.sample.get() ||
                found->second.request.begin != request.begin || found->second.request.end != request.end;
            if (replace) {
                Voice voice;
                voice.request = request;
                voice.position = double(request.begin) / (size_t(request.sample->channels) * 2);
                voice.rendered_frequency = float(request.frequency ? request.frequency : request.sample->frequency);
                voices_[id] = std::move(voice);
            } else {
                found->second.request = request;
            }
        }
        for (auto it = voices_.begin(); it != voices_.end();) {
            if (it->second.request.loop && !loops_.count(it->first)) it = voices_.erase(it);
            else ++it;
        }
        released_loops_.clear();
        shots.swap(one_shots_);
    }

    for (auto& request : shots) {
        if (next_one_shot_id_ == UINT64_MAX) next_one_shot_id_ = uint64_t(1) << 63;
        Voice voice;
        voice.request = std::move(request);
        voice.position = 0.0;
        voice.rendered_frequency = float(voice.request.frequency ? voice.request.frequency : voice.request.sample->frequency);
        voices_[next_one_shot_id_++] = std::move(voice);
    }
}

void SaAudioMixerCore::render(size_t frames, std::vector<int16_t>& output) {
    sync_requests();
    if (frames > (std::numeric_limits<size_t>::max() / 2)) { output.clear(); return; }
    output.assign(frames * 2, 0);
    if (!frames) return;

    const float scale = effects_scale_.load(std::memory_order_relaxed);
    finished_.clear();
    for (size_t out_frame = 0; out_frame < frames; ++out_frame) {
        double left = 0.0, right = 0.0;
        for (auto& [id, voice] : voices_) {
            if (voice.finished) continue;
            const auto& request = voice.request;
            const auto& sample = *request.sample;
            const size_t bytes_per_frame = size_t(sample.channels) * 2;
            const size_t begin = request.begin / bytes_per_frame;
            const size_t end = request.end / bytes_per_frame;
            const size_t length = end - begin;
            if (!length || voice.position >= double(end)) {
                if (request.loop && length) voice.position = double(begin) + std::fmod(voice.position - double(begin), double(length));
                else { voice.finished=true;finished_.push_back(id);continue; }
            }

            const float target = float(request.frequency ? request.frequency : sample.frequency);
            const float mix = float(out_frame + 1) / float(frames);
            const float frequency = voice.rendered_frequency + (target - voice.rendered_frequency) * mix;
            const double step = double(frequency) / double(output_rate);
            const auto first = size_t(voice.position);
            const auto second = first + 1 < end ? first + 1 : (request.loop ? begin : first);
            const double fraction = voice.position - double(first);
            const auto interpolate = [&](size_t channel) {
                const double a = read_frame(sample, first, channel);
                const double b = read_frame(sample, second, channel);
                return a + (b - a) * fraction;
            };

            const double volume = double(request.volume) / 127.0 * scale;
            const double left_pan = request.pan <= 63 ? 1.0 : double(127 - request.pan) / 64.0;
            const double right_pan = request.pan >= 63 ? 1.0 : double(request.pan) / 63.0;
            if (sample.channels == 1) {
                const auto value = interpolate(0) * volume;
                left += value * left_pan;
                right += value * right_pan;
            } else {
                left += interpolate(0) * volume * left_pan;
                right += interpolate(1) * volume * right_pan;
            }

            voice.position += step;
            if (request.loop && voice.position >= double(end))
                voice.position = double(begin) + std::fmod(voice.position - double(begin), double(length));
            else if (!request.loop && voice.position >= double(end)) {voice.finished=true;finished_.push_back(id);}
        }
        output[out_frame * 2] = limit_sample(left);
        output[out_frame * 2 + 1] = limit_sample(right);
    }

    for (const auto id : finished_) {
        const auto found = voices_.find(id);
        if (found == voices_.end()) continue;
        if (!found->second.request.loop) active_one_shots_.fetch_sub(1, std::memory_order_acq_rel);
        voices_.erase(found);
    }

    for (auto& [id, voice] : voices_) {
        (void)id;
        voice.rendered_frequency = float(voice.request.frequency ? voice.request.frequency : voice.request.sample->frequency);
    }
}

struct SaAudioMixer::Runtime {
    struct Buffer {
        WAVEHDR header{};
        std::vector<int16_t> pcm;
        uint64_t submission_order = 0;
    };
    struct DeviceState {
        HWAVEOUT handle = nullptr;
        HANDLE event = nullptr;
        std::array<Buffer, SaAudioMixer::output_queue_buffer_count> buffers;
    };

    SaAudioMixerCore& core;
    std::atomic<bool> ready{false};
    std::atomic<bool> stop{false};
    std::shared_ptr<DeviceState> device;
    std::thread thread;
    uint64_t next_submission_order = 0;

    explicit Runtime(SaAudioMixerCore& mixer) : core(mixer) {
        try { thread = std::thread([this] { run(); }); }
        catch (...) { ready = false; }
    }

    ~Runtime() { shutdown(); }

    void shutdown() {
        ready = false;
        stop = true;
        // `device` belongs to the worker until it exits. The wait timeout
        // bounds shutdown latency without racing its open/cleanup path.
        if (thread.joinable()) thread.join();
    }

    static void retain(std::shared_ptr<DeviceState> state) {
        static std::mutex mutex;
        static std::vector<std::shared_ptr<DeviceState>> retained;
        std::lock_guard<std::mutex> guard(mutex);
        retained.push_back(std::move(state));
    }

    bool open() {
        device = std::make_shared<DeviceState>();
        device->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!device->event) return false;

        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = 2;
        format.wBitsPerSample = 16;
        format.nSamplesPerSec = SaAudioMixerCore::output_rate;
        format.nBlockAlign = 4;
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
        if (waveOutOpen(&device->handle, WAVE_MAPPER, &format,
                        reinterpret_cast<DWORD_PTR>(device->event), 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
            device->handle = nullptr;
            CloseHandle(device->event);
            device->event = nullptr;
            return false;
        }

        // Hold playback until every output block has been prepared and queued.
        // Otherwise the first short block can start before the safety margin
        // exists and a startup scheduling delay can immediately underrun.
        if (waveOutPause(device->handle) != MMSYSERR_NOERROR) return false;

        for (auto& buffer : device->buffers) {
            buffer.pcm.resize(SaAudioMixerCore::output_frames * 2);
            buffer.header.lpData = reinterpret_cast<LPSTR>(buffer.pcm.data());
            buffer.header.dwBufferLength = DWORD(buffer.pcm.size() * sizeof(int16_t));
            if (waveOutPrepareHeader(device->handle, &buffer.header, sizeof(buffer.header)) != MMSYSERR_NOERROR)
                return false;
        }
        return true;
    }

    bool submit(Buffer& buffer, std::vector<int16_t>& mixed) {
        core.render(SaAudioMixerCore::output_frames, mixed);
        std::copy(mixed.begin(), mixed.end(), buffer.pcm.begin());
        buffer.header.dwFlags &= ~WHDR_DONE;
        buffer.submission_order = next_submission_order;
        if (waveOutWrite(device->handle, &buffer.header, sizeof(buffer.header)) != MMSYSERR_NOERROR)
            return false;
        ++next_submission_order;
        return true;
    }

    void cleanup() {
        if (!device) return;
        bool safe_to_free = true;
        if (device->handle) {
            if (waveOutReset(device->handle) != MMSYSERR_NOERROR) safe_to_free = false;
            if (safe_to_free) {
                for (auto& buffer : device->buffers) {
                    if ((buffer.header.dwFlags & WHDR_PREPARED) &&
                        waveOutUnprepareHeader(device->handle, &buffer.header, sizeof(buffer.header)) != MMSYSERR_NOERROR)
                        safe_to_free = false;
                }
            }
            if (safe_to_free && waveOutClose(device->handle) != MMSYSERR_NOERROR) safe_to_free = false;
            if (safe_to_free) device->handle = nullptr;
        }
        if (safe_to_free) {
            if (device->event) CloseHandle(device->event);
            device->event = nullptr;
            device.reset();
        } else {
            retain(std::move(device)); // keep any still-published WAVEHDR storage alive
        }
    }

    void run() {
        // Scheduling priority is best-effort; a refusal must not mute all effects.
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        if (!open()) {
            ready = false;
            cleanup();
            return;
        }
        std::vector<int16_t> mixed;
        for (auto& buffer : device->buffers) {
            if (!submit(buffer, mixed)) {
                cleanup();
                return;
            }
        }
        // All blocks are now queued under pause; restart publishes one full
        // bounded margin to the driver at once.
        if (waveOutRestart(device->handle) != MMSYSERR_NOERROR) {
            cleanup();
            return;
        }
        ready = true;

        while (!stop) {
            const auto waited = WaitForSingleObject(device->event, 10);
            if (stop) break;
            if (waited == WAIT_TIMEOUT) continue;
            if (waited != WAIT_OBJECT_0) break;

            std::array<Buffer*, SaAudioMixer::output_queue_buffer_count> completed{};
            size_t completed_count = 0;
            for (auto& buffer : device->buffers) {
                if (!(buffer.header.dwFlags & WHDR_DONE)) continue;
                completed[completed_count++] = &buffer;
            }
            // Event callbacks may coalesce while this worker is delayed. Sort
            // completed blocks by their original submission order so a wrap
            // around the fixed header array cannot reorder the rendered PCM.
            std::sort(completed.begin(), completed.begin() + completed_count,
                [](const Buffer* left, const Buffer* right) {
                    return left->submission_order < right->submission_order;
                });
            for (size_t index = 0; index < completed_count; ++index) {
                if (!submit(*completed[index], mixed)) {
                    ready = false;
                    stop = true;
                    break;
                }
            }
        }
        ready = false;
        cleanup();
    }
};

SaAudioMixer::SaAudioMixer() : runtime_(std::make_unique<Runtime>(core_)) {}
SaAudioMixer::~SaAudioMixer() { shutdown(); }

bool SaAudioMixer::publish_loop(uint64_t id, std::shared_ptr<const AudioSample> pcm,
                                uint32_t begin, uint32_t end, uint32_t frequency,
                                uint8_t volume, uint8_t pan) {
    return available() && core_.publish_loop(id, std::move(pcm), begin, end, frequency, volume, pan);
}

void SaAudioMixer::release_loop(uint64_t id) { core_.release_loop(id); }

bool SaAudioMixer::play_one_shot(std::shared_ptr<const AudioSample> pcm, uint32_t frequency,
                                 uint8_t volume, uint8_t pan) {
    return available() && core_.play_one_shot(std::move(pcm), frequency, volume, pan);
}

void SaAudioMixer::set_effects_scale(float value) { core_.set_effects_scale(value); }


bool SaAudioMixer::available() const { return runtime_ && runtime_->ready.load(std::memory_order_acquire); }

void SaAudioMixer::shutdown() {
    if (!runtime_) return;
    runtime_->shutdown();
    runtime_.reset();
}

}
