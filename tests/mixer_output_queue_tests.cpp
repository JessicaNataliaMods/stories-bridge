#include "mixer_output_mock.hpp"
#include "../src/sa_audio_mixer.cpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>

using namespace std::chrono_literals;

static void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

static bool wait_for_startup_cleanup(const audio_output_mock::State& current) {
    return current.close_calls == 1 && current.event_closes == 1;
}

static bool wait_for_delayed_callbacks(const audio_output_mock::State& current) {
    return current.delayed_callbacks == 4 && current.writes.size() >= 32;
}

static int16_t read_sample(const audio_output_mock::WriteRecord& write, size_t frame,
                           size_t channel) {
    int16_t sample = 0;
    const size_t offset = (frame * 2 + channel) * sizeof(sample);
    if (offset + sizeof(sample) <= write.bytes.size())
        std::memcpy(&sample, write.bytes.data() + offset, sizeof(sample));
    return sample;
}

static void test_prefill_delayed_callbacks_and_pcm_phase() {
    audio_output_mock::reset();
    lcs::SaAudioMixer mixer;
    const auto startup_deadline = std::chrono::steady_clock::now() + 1s;
    while (!mixer.available() && std::chrono::steady_clock::now() < startup_deadline)
        std::this_thread::sleep_for(1ms);
    check(mixer.available(), "Runtime opens only after the paused output queue is ready");

    {
        std::lock_guard<std::mutex> guard(audio_output_mock::state.mutex);
        const auto& current = audio_output_mock::state;
        check(current.priority_calls == 1 && current.priority_value == THREAD_PRIORITY_HIGHEST,
              "Actual mixer worker requests elevated Windows scheduling priority");
        check(current.pause_calls == 1 && current.restart_calls == 1,
              "Startup holds the device paused until one complete queue is submitted");
        check(current.writes_at_restart == lcs::SaAudioMixer::output_queue_buffer_count &&
              current.queue_depth_at_restart == lcs::SaAudioMixer::output_queue_buffer_count,
              "Driver restart publishes all twelve stable blocks together");
        check(lcs::SaAudioMixer::output_queue_margin_ms == 120,
              "The bounded startup margin is 120 ms at 44.1 kHz");
        check(current.prepared_headers.size() == lcs::SaAudioMixer::output_queue_buffer_count,
              "Each fixed output header is prepared once");
    }

    auto sample = std::make_shared<lcs::AudioSample>();
    sample->frequency = lcs::SaAudioMixerCore::output_rate;
    sample->channels = 1;
    constexpr size_t source_frames = 200000;
    sample->pcm.resize(source_frames * sizeof(int16_t));
    for (size_t frame = 0; frame < source_frames; ++frame) {
        const int16_t value = int16_t(1000 + frame);
        std::memcpy(sample->pcm.data() + frame * sizeof(value), &value, sizeof(value));
    }
    check(mixer.publish_loop(7, sample, 0, uint32_t(sample->pcm.size()),
                             lcs::SaAudioMixerCore::output_rate, 127, 63),
          "A real mixer loop is accepted after Runtime startup");

    // Four callback deliveries are each held for 60 ms. These 50-80 ms stalls
    // let multiple headers complete, including headers on both sides of the
    // fixed-array wrap, while the mock device continues consuming queued PCM.
    {
        std::lock_guard<std::mutex> guard(audio_output_mock::state.mutex);
        audio_output_mock::state.callback_delay_ms = 60;
        audio_output_mock::state.delayed_callbacks_remaining = 4;
    }
    check(audio_output_mock::wait_until(wait_for_delayed_callbacks, 2s),
          "Runtime services repeated delayed WinMM completion events");

    std::vector<audio_output_mock::WriteRecord> writes;
    std::vector<WAVEHDR*> prepared;
    double remaining_margin = 0.0;
    double minimum_margin = 0.0;
    size_t underruns = 0;
    {
        std::lock_guard<std::mutex> guard(audio_output_mock::state.mutex);
        const auto& current = audio_output_mock::state;
        writes = current.writes;
        prepared = current.prepared_headers;
        remaining_margin = current.queue_margin_at_delayed_callback_ms;
        minimum_margin = current.minimum_queue_margin_ms;
        underruns = current.underruns;
    }

    check(writes.size() >= 32, "Repeated callback batches refill more than one header cycle");
    check(underruns == 0 && remaining_margin >= 20.0 && minimum_margin >= 20.0,
          "A 60 ms callback stall remains inside the queued safety margin without silence gaps");

    std::unordered_map<WAVEHDR*, LPSTR> header_data;
    for (auto* header : prepared) {
        check(header_data.emplace(header, header->lpData).second,
              "The runtime owns distinct stable WAVEHDR slots");
    }
    check(header_data.size() == lcs::SaAudioMixer::output_queue_buffer_count,
          "The driver queue stays bounded to twelve headers");

    bool began_audio = false;
    size_t audio_blocks = 0;
    int32_t expected_first_sample = 1000;
    for (const auto& write : writes) {
        if (!write.accepted) continue;
        const auto header = header_data.find(write.header);
        check(header != header_data.end() && header->second == write.data,
              "Every refill reuses its original prepared header and PCM storage");
        check(write.length == lcs::SaAudioMixerCore::output_frames * 4,
              "Every output write contains one complete stereo PCM block");

        const int16_t first = read_sample(write, 0, 0);
        const int16_t first_right = read_sample(write, 0, 1);
        if (!began_audio && first < 1000) continue;
        began_audio = true;
        check(first == expected_first_sample && first_right == first,
              "Coalesced callbacks preserve the mixer’s exact playback phase and stereo frame");
        check(read_sample(write, lcs::SaAudioMixerCore::output_frames - 1, 0) ==
              expected_first_sample + int32_t(lcs::SaAudioMixerCore::output_frames - 1),
              "Each refilled PCM block contains consecutive source frames");
        expected_first_sample += int32_t(lcs::SaAudioMixerCore::output_frames);
        ++audio_blocks;
    }
    check(audio_blocks >= 20,
          "Repeated delayed completions carry ordered PCM through multiple header-array wraps");

    mixer.shutdown();
    check(!mixer.available(), "Shutdown retires the output runtime");
    {
        std::lock_guard<std::mutex> guard(audio_output_mock::state.mutex);
        const auto& current = audio_output_mock::state;
        check(current.reset_calls == 1 && current.unprepare_calls == prepared.size() &&
              current.close_calls == 1 && current.event_closes == 1 && current.device == nullptr,
              "Shutdown drains the queued headers before releasing the mock device and event");
    }
}

static void test_write_failure_keeps_startup_unavailable() {
    audio_output_mock::reset();
    {
        std::lock_guard<std::mutex> guard(audio_output_mock::state.mutex);
        audio_output_mock::state.fail_write_on_call = 3;
    }
    lcs::SaAudioMixer mixer;
    check(audio_output_mock::wait_until(wait_for_startup_cleanup, 1s),
          "A partial startup write failure reaches Runtime cleanup");
    check(!mixer.available(), "A partial output queue never publishes Runtime readiness");
    {
        std::lock_guard<std::mutex> guard(audio_output_mock::state.mutex);
        const auto& current = audio_output_mock::state;
        check(current.write_calls == 3 && current.accepted_writes == 2 &&
              current.restart_calls == 0,
              "The device is never restarted after a failed prefill write");
        check(current.reset_calls == 1 && current.unprepare_calls ==
              lcs::SaAudioMixer::output_queue_buffer_count && current.close_calls == 1,
              "Failure resets and releases every prepared header");
    }
    mixer.shutdown();
}

static void test_restart_failure_releases_paused_prefill() {
    audio_output_mock::reset();
    {
        std::lock_guard<std::mutex> guard(audio_output_mock::state.mutex);
        audio_output_mock::state.fail_restart = true;
    }
    lcs::SaAudioMixer mixer;
    check(audio_output_mock::wait_until(wait_for_startup_cleanup, 1s),
          "A driver restart refusal reaches Runtime cleanup");
    check(!mixer.available(), "The output runtime stays unavailable when atomic start fails");
    {
        std::lock_guard<std::mutex> guard(audio_output_mock::state.mutex);
        const auto& current = audio_output_mock::state;
        check(current.writes_at_restart == lcs::SaAudioMixer::output_queue_buffer_count &&
              current.queue_depth_at_restart == lcs::SaAudioMixer::output_queue_buffer_count,
              "Restart is attempted only after the full queue is held by the paused device");
        check(current.reset_calls == 1 && current.unprepare_calls ==
              lcs::SaAudioMixer::output_queue_buffer_count && current.close_calls == 1,
              "A refused atomic start releases the complete paused prefill");
    }
    mixer.shutdown();
}

int main() {
    try {
        test_prefill_delayed_callbacks_and_pcm_phase();
        test_write_failure_keeps_startup_unavailable();
        test_restart_failure_releases_paused_prefill();
        std::cout << "mixer WinMM runtime queue tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
