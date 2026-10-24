#include "audio_output_mock.hpp"
#include "sa_continuous_sound.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

lcs::SaContinuousSoundFrame frame(bool paused = false, float listener_x = 0.0f) {
    return {{ {listener_x, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f} },
             30, 343.0f / 30.0f, 1.0f, false, paused, {false, 127}};
}

lcs::ContinuousSoundObjects::Snapshot create_source(
    lcs::SaContinuousSound& runtime, lcs::ContinuousSoundObjects& handles,
    uint16_t sound, std::array<float, 3> position) {
    const auto allocated = handles.allocate(sound, position);
    check(bool(allocated), "external script pool allocates source handle");
    const auto receipt = runtime.create(*allocated);
    check(receipt.entered && receipt.accepted,
          "verified PCM selector creates an actual continuous audio entity");
    if (receipt.entity >= 0) {
        check(handles.attach_entity(allocated->token, receipt.entity),
              "audio entity receipt attaches to the source handle");
        return *handles.find(allocated->token.reference);
    }
    return *allocated;
}

void release_source(lcs::SaContinuousSound& runtime,
                    lcs::ContinuousSoundObjects& handles,
                    const lcs::ContinuousSoundObjects::Snapshot& source) {
    const auto released = runtime.release(source);
    check(released.accepted, "checked output retirement permits entity release");
    check(handles.erase_released(source.token), "script handle retires after audio entity");
}
}

int main(int argc, char** argv) try {
    check(argc == 2, "pass the converted LCS audio resource directory");
    lcs::AudioArchive archive(argv[1]);
    check(archive.sample_count() > 5607, "original SDT has all selected PCM and frequency records");
    check(archive.record(4).loop_end == -1,
          "selector 0 remains the authored finite sample rather than an invented PCM loop");

    lcs::SaContinuousSound runtime(archive);
    lcs::ContinuousSoundObjects handles;
    const auto opens_before = audio_mock::opens;

    // Retail 1B2A98: selectors 6, 36..39, 46 (configuration only) and every default selector (e.g. 1, 12,
    // 47, 52, 67) create an audio entity but never request a voice. The entity is real: it is serviced,
    // released and its handle retires. Selectors 40..45 need the MusicManager CITY consumer, which is not
    // integrated: they are refused before any entity or handle exists.
    for (uint16_t music = 40; music <= 45; ++music) {
        const auto refused_music = handles.allocate(music, {0.0f, 0.0f, 0.0f});
        check(bool(refused_music) && !runtime.create(*refused_music).accepted && !runtime.create(*refused_music).entered &&
                  runtime.entities().service_order().empty() && audio_mock::opens == opens_before,
              "music selectors 40..45 are refused before an audio entity exists");
        check(handles.erase_released(refused_music->token), "the refused music selector leaves no script handle");
    }
    for (const uint16_t selector : {uint16_t(6), uint16_t(36), uint16_t(46),
                                    uint16_t(1), uint16_t(12), uint16_t(47), uint16_t(52), uint16_t(67), uint16_t(255)}) {
        const auto silent = create_source(runtime, handles, selector, {0.0f, 0.0f, 0.0f});
        check(silent.object.audio_entity >= 0 && audio_mock::opens == opens_before,
              "silent selector owns an audio entity without opening output");
        check(runtime.service_frame(frame()) && audio_mock::opens == opens_before,
              "silent selector is serviced without a voice and without failing the frame");
        release_source(runtime, handles, silent);
    }
    const auto stale_silent = create_source(runtime, handles, 67, {0.0f, 0.0f, 0.0f});
    release_source(runtime, handles, stale_silent);
    check(!runtime.release(stale_silent).accepted,
          "a retired silent entity cannot be released twice");

    std::array<lcs::ContinuousSoundObjects::Snapshot, 5> sources{
        create_source(runtime, handles, 0, {30.0f, 0.0f, 0.0f}),
        create_source(runtime, handles, 2, {30.0f, 5.0f, 0.0f}),
        create_source(runtime, handles, 4, {30.0f, -5.0f, 0.0f}),
        create_source(runtime, handles, 11, {20.0f, 0.0f, 0.0f}),
        create_source(runtime, handles, 51, {10.0f, 0.0f, 0.0f})};
    check(audio_mock::opens == opens_before,
          "script creation only allocates/cache-preflights and does not bypass the audio frame service");
    const auto finite_record = archive.record(4);
    const auto finite_frames = uint64_t(finite_record.size / 2);
    const auto block_frames = std::max<uint32_t>(1, finite_record.frequency / 20);
    const auto finite_emission = *lcs::continuous_sound_emission(0);
    const auto playback_frequency = lcs::continuous_channel_frequency(archive.record(finite_emission.frequency_sample).frequency);
    const auto output_frames = (finite_frames * finite_record.frequency + playback_frequency - 1) / playback_frequency;
    const auto finite_blocks = size_t((output_frames + block_frames - 1) / block_frames);
    const auto finite_initial_headers = std::min<size_t>(3, finite_blocks);
    const auto finite_output_begin = audio_mock::headers.size();
    for (uint32_t id = 1; id <= sources.size(); ++id) {
        check(runtime.service_frame(frame()), "source frame services validated PCM requests in insertion order");
    }
    check(audio_mock::opens == opens_before + sources.size() && audio_mock::writes >= 9,
          "selectors 0, 2, 4, 11 and 51 reach owned PCM output in entity insertion order");

    check(finite_initial_headers > 0 && finite_output_begin + finite_initial_headers <= audio_mock::headers.size(),
          "owned PCM path prepared every initial one-shot output header");
    std::vector<WAVEHDR*> finite_headers(audio_mock::headers.begin() + finite_output_begin,
                                         audio_mock::headers.begin() + finite_output_begin + finite_initial_headers);
    auto blocks_to_render = finite_blocks - finite_initial_headers;
    if (blocks_to_render) {
        HANDLE completion = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        check(completion != nullptr, "create a completion barrier for the mocked output worker");
        audio_mock::completion_event = completion;
        while (blocks_to_render) {
            const auto writes_before_pump = audio_mock::pcm_writes.size();
            finite_headers.front()->dwFlags |= WHDR_DONE;
            check(SetEvent(reinterpret_cast<HANDLE>(audio_mock::last_callback)) != 0,
                  "mock WinMM callback event wakes the output worker for a finite sample");
            check(WaitForSingleObject(completion, 2000) == WAIT_OBJECT_0,
                  "output worker refills a completed finite-sample block");
            check(runtime.service_frame(frame()),
                  "audio-frame service waits for the output worker to finish its refill");
            check(audio_mock::pcm_writes.size() == writes_before_pump + 1,
                  "finite output advances one completed 50 ms block until source end");
            --blocks_to_render;
        }
        audio_mock::completion_event = nullptr;
        CloseHandle(completion);
    }
    // The rendered source is now exhausted; model playback completion for every
    // prepared final buffer before the controller observes the one-shot receipt.
    for (auto* header : finite_headers) header->dwFlags |= WHDR_DONE;
    const auto opens_before_reacquire = audio_mock::opens;
    check(runtime.service_frame(frame()), "completed finite sample is retired before reacquisition");
    check(audio_mock::opens == opens_before_reacquire + 1,
          "selector 0 reacquires after actual channel completion, without driver loop flags");

    const auto opens_before_pause = audio_mock::opens;
    check(runtime.service_frame(frame(true)), "user pause retires continuous channels");
    check(audio_mock::opens == opens_before_pause,
          "pause does not resubmit or reopen a continuous PCM voice");
    check(runtime.service_frame(frame(true)) && audio_mock::opens == opens_before_pause,
          "sustained user pause keeps source voices retired");
    check(runtime.service_frame(frame()), "unpause obtains fresh voice generations");
    check(audio_mock::opens == opens_before_pause + sources.size(),
          "resume reacquires rather than restarting a stale channel receipt");

    audio_mock::fail_reset = true;
    const auto refused = runtime.release(sources[1]);
    check(refused.entered && !refused.accepted && runtime.quarantined(),
          "failed entered channel retirement retains the entity and full voice receipt");
    audio_mock::fail_reset = false;
    check(runtime.release(sources[1]).accepted,
          "a later checked retirement releases the same source epoch");
    check(handles.erase_released(sources[1].token),
          "script slot is freed only after physical channel retirement succeeds");

    const auto start_failure = create_source(runtime, handles, 2, {25.0f, 0.0f, 0.0f});
    audio_mock::fail_write = true;
    audio_mock::fail_reset = true;
    const auto opens_before_failure = audio_mock::opens;
    check(!runtime.service_frame(frame()) && runtime.quarantined(),
          "entered initial PCM write failure quarantines the owner and dedicated speech instance");
    audio_mock::fail_write = false;
    check(!runtime.service_frame(frame()) && audio_mock::opens == opens_before_failure + 1,
          "ambiguous failed start blocks every reopen until checked shutdown succeeds");
    audio_mock::fail_reset = false;
    check(runtime.recover(), "checked shutdown resolves an ambiguous start with no voice receipt");
    check(!runtime.service_frame(frame()) && audio_mock::opens == opens_before_failure + 5,
          "recovery clears global quarantine but does not replay the failed owner");
    release_source(runtime, handles, start_failure);

    release_source(runtime, handles, sources[0]);
    release_source(runtime, handles, sources[2]);
    release_source(runtime, handles, sources[3]);
    release_source(runtime, handles, sources[4]);
    check(runtime.shutdown(), "dedicated continuous output shuts down with checked channel retirement");
    const auto gated = create_source(runtime, handles, 11, {40.0f, 0.0f, 0.0f});
    const auto gated_opens = audio_mock::opens;
    check(runtime.service_frame(frame()) && audio_mock::opens == gated_opens,
          "distance equal to the source cutoff never opens output");
    auto invalid = frame();
    invalid.time_spent = 0;
    check(!runtime.service_frame(invalid) && audio_mock::opens == gated_opens,
          "missing source time quantum refuses before output");
    invalid = frame();
    invalid.mix.native_scale = std::numeric_limits<float>::quiet_NaN();
    check(!runtime.service_frame(invalid) && audio_mock::opens == gated_opens,
          "invalid native gain refuses before output");
    check(runtime.service_frame(frame(false, 1.0f)) && audio_mock::opens == gated_opens + 1,
          "moving inside the original cutoff starts the retained entity");
    // SA/project adaptation: leaving the cutoff ends only this owner's voice with a checked stop.
    const auto neighbour = create_source(runtime, handles, 51, {1.0f, 0.0f, 0.0f});
    check(runtime.service_frame(frame(false, 1.0f)), "neighbour source starts inside its range");
    const auto before_exit = audio_mock::opens;
    check(runtime.service_frame(frame()) && !runtime.quarantined() && audio_mock::opens == before_exit,
          "leaving the cutoff retires the owner's voice without quarantine and without a new open");
    check(runtime.service_frame(frame(false, 1.0f)) && audio_mock::opens == before_exit + 1,
          "re-entering the range requests a fresh voice for the retired owner only");
    audio_mock::fail_reset = true;
    check(!runtime.service_frame(frame()) && runtime.quarantined(),
          "a refused checked stop at the cutoff quarantines instead of reporting success");
    audio_mock::fail_reset = false;
    check(runtime.recover(), "the retained voice receipt is retired by a later checked recovery");
    release_source(runtime, handles, neighbour);
    release_source(runtime, handles, gated);
    check(runtime.shutdown(), "cutoff handling leaves releasable source identities");
    std::vector<lcs::ContinuousSoundObjects::Snapshot> capacity_sources;
    for (size_t i = 0; i < 41; ++i)
        capacity_sources.push_back(create_source(runtime, handles, 0, {1.0f, 0.0f, 0.0f}));
    const auto capacity_opens = audio_mock::opens;
    check(!runtime.service_frame(frame()) && !runtime.quarantined() &&
              audio_mock::opens == capacity_opens + 40,
          "source transport admits40 owned PCM channels; the41st retains its identity without eviction");
    for (const auto& source : capacity_sources) release_source(runtime, handles, source);
    check(runtime.shutdown(), "all40 channel receipts retire before source transport shutdown");
    {
        // 402/403 through the script VM: handle publication, spatial voice, silent selectors,
        // generation-checked removal that keeps every other voice, and failure handling.
        lcs::SaContinuousSound rt(archive);
        lcs::ContinuousSoundAdapter script;
        script.available = [&] { return rt.available(); };
        size_t create_calls=0;
        script.create = [&](const auto& source) { ++create_calls; return rt.create(source); };
        script.release = [&](const auto& source) { return rt.release(source); };
        const auto bits = [](float v) { int32_t b; std::memcpy(&b, &v, 4); return b; };
        struct Result { lcs::StepResult status; int32_t value; };
        const auto run = [&](uint16_t opcode, std::initializer_list<int32_t> args, bool output) {
            std::vector<uint8_t> code(64);
            code.push_back(uint8_t(opcode)); code.push_back(uint8_t(opcode >> 8));
            for (auto value : args) { code.push_back(6); for (int i = 0; i < 4; ++i) code.push_back(uint8_t(uint32_t(value) >> (8 * i))); }
            if (output) code.push_back(12);
            lcs::Vm vm(code, 64, 64); lcs::Thread thread; thread.ip = 64; thread.locals[0] = -99;
            vm.backend = [&](lcs::Vm& v, lcs::Thread& t, uint16_t op) { return script.dispatch(v, t, op); };
            return Result{vm.step(thread, 0), thread.locals[0]};
        };
        const auto opens_start = audio_mock::opens;
        const auto a = run(402, {bits(10.f), 0, 0, 51}, true);
        const auto b = run(402, {bits(20.f), 0, 0, 11}, true);
        const auto silent67 = run(402, {0, 0, 0, 67}, true);
        const auto before_music=create_calls;
        for(int selector=40;selector<=45;++selector){
            const auto music = run(402, {0, 0, 0, selector}, true);
            check(music.status == lcs::StepResult::unsupported && music.value == -99 && create_calls==before_music,
                  "402 refuses each music selector before entering the backend or writing output");
        }
        check(a.status == lcs::StepResult::continued && a.value == 1 && b.value == 257 &&
                  silent67.value == 513 && audio_mock::opens == opens_start,
              "402 publishes retail pool handles for voice and default (67) selectors without opening output");
        check(rt.service_frame(frame()) && audio_mock::opens == opens_start + 2,
              "the audio frame consumer starts exactly the two spatial voices");
        check(run(403, {a.value}, false).status == lcs::StepResult::continued, "403 accepts the live handle");
        const auto resets_after_a = audio_mock::resets;
        check(resets_after_a > 0 && rt.service_frame(frame()) && audio_mock::opens == opens_start + 2,
              "403 stops only that instance's voice; the other voice keeps playing without reopening");
        check(run(403, {a.value}, false).status == lcs::StepResult::continued && audio_mock::resets == resets_after_a,
              "a stale handle is a retail no-op and cannot stop another voice");
        const auto replacement = run(402, {bits(10.f), 0, 0, 51}, true);
        check(replacement.value != a.value && rt.service_frame(frame()) && audio_mock::opens == opens_start + 3,
              "a replacement object gets a new handle and its own voice");
        check(run(403, {a.value}, false).status == lcs::StepResult::continued && rt.service_frame(frame()) &&
                  audio_mock::opens == opens_start + 3,
              "the retired handle cannot remove the replacement that reuses its audio entity");
        const auto before_silent = audio_mock::resets;
        check(run(403, {silent67.value}, false).status == lcs::StepResult::continued &&
                  audio_mock::resets == before_silent,
              "removing silent objects never touches any voice");
        for (int32_t invalid : {-1, 0x7FFFFFFF, 0x00FFFF01})
            check(run(403, {invalid}, false).status == lcs::StepResult::continued, "unresolvable references are retail no-ops");
        audio_mock::fail_reset = true;
        check(run(403, {b.value}, false).status == lcs::StepResult::unsupported && script.quarantined(),
              "a refused checked stop keeps the object alive and blocks further 402/403 until recovery");
        check(run(402, {0, 0, 0, 67}, true).status == lcs::StepResult::unsupported,
              "no new object is created while ownership is uncertain");
        audio_mock::fail_reset = false;
        check(script.recover() && !script.quarantined(), "later checked recovery releases the same object");
        script.music_available=[](){return true;};
        rt.music_available=[](){return true;};
        std::vector<uint8_t> configurations;
        rt.configure_music=[&](uint8_t selector){configurations.push_back(selector);return true;};
        std::vector<int32_t> music_handles;
        for(int selector=40;selector<=45;++selector){
            const auto music=run(402,{0,0,0,selector},true);
            check(music.status==lcs::StepResult::continued,"A ready CITY consumer accepts every music selector");
            music_handles.push_back(music.value);
        }
        const auto music_opens=audio_mock::opens;
        check(rt.service_frame(frame()) && configurations==std::vector<uint8_t>({40,41,42,43,44,45}) && audio_mock::opens==music_opens,
              "The production continuous frame applies configuration in entity order without allocating spatial voices");
        for(auto handle:music_handles)check(run(403,{handle},false).status==lcs::StepResult::continued,"CITY source objects release through the real consumer");
        check(run(403, {replacement.value}, false).status == lcs::StepResult::continued && rt.shutdown(),
              "remaining objects release and the dedicated output shuts down");
    }
    std::cout << "PASS: verified PCM selectors, real owned voices, finite one-shot reacquisition, pause generations, epoch release and entered-failure quarantine; no game or audio device\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
