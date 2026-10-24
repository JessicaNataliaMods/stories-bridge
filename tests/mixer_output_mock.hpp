#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace audio_output_mock {

struct Event {
    bool signaled = false;
    bool closed = false;
};

struct QueuedHeader {
    WAVEHDR* header = nullptr;
    double remaining_ms = 0.0;
};

struct Device {
    Event* event = nullptr;
    std::deque<QueuedHeader> queue;
    std::chrono::steady_clock::time_point last_progress{};
    bool paused = false;
    bool started = false;
};

struct WriteRecord {
    WAVEHDR* header = nullptr;
    LPSTR data = nullptr;
    DWORD length = 0;
    bool accepted = false;
    std::vector<uint8_t> bytes;
};

struct State {
    std::mutex mutex;
    std::condition_variable changed;
    Device* device = nullptr;
    size_t opens = 0;
    size_t prepare_calls = 0;
    size_t write_calls = 0;
    size_t accepted_writes = 0;
    size_t reset_calls = 0;
    size_t unprepare_calls = 0;
    size_t close_calls = 0;
    size_t pause_calls = 0;
    size_t restart_calls = 0;
    size_t event_closes = 0;
    size_t priority_calls = 0;
    int priority_value = THREAD_PRIORITY_NORMAL;
    bool fail_open = false;
    bool fail_pause = false;
    bool fail_restart = false;
    int fail_write_on_call = 0;
    int callback_delay_ms = 0;
    size_t delayed_callbacks_remaining = 0;
    size_t delayed_callbacks = 0;
    size_t completed_headers = 0;
    size_t underruns = 0;
    double underrun_ms = 0.0;
    double minimum_queue_margin_ms = std::numeric_limits<double>::infinity();
    double queue_margin_at_delayed_callback_ms = std::numeric_limits<double>::infinity();
    size_t writes_at_restart = 0;
    size_t queue_depth_at_restart = 0;
    std::vector<WAVEHDR*> prepared_headers;
    std::vector<WAVEHDR*> unprepared_headers;
    std::vector<WriteRecord> writes;
};

inline State state;

inline void reset() {
    std::lock_guard<std::mutex> guard(state.mutex);
    if (state.device) throw std::runtime_error("mock device must be closed before reset");
    state.opens = 0;
    state.prepare_calls = 0;
    state.write_calls = 0;
    state.accepted_writes = 0;
    state.reset_calls = 0;
    state.unprepare_calls = 0;
    state.close_calls = 0;
    state.pause_calls = 0;
    state.restart_calls = 0;
    state.event_closes = 0;
    state.priority_calls = 0;
    state.priority_value = THREAD_PRIORITY_NORMAL;
    state.fail_open = false;
    state.fail_pause = false;
    state.fail_restart = false;
    state.fail_write_on_call = 0;
    state.callback_delay_ms = 0;
    state.delayed_callbacks_remaining = 0;
    state.delayed_callbacks = 0;
    state.completed_headers = 0;
    state.underruns = 0;
    state.underrun_ms = 0.0;
    state.minimum_queue_margin_ms = std::numeric_limits<double>::infinity();
    state.queue_margin_at_delayed_callback_ms = std::numeric_limits<double>::infinity();
    state.writes_at_restart = 0;
    state.queue_depth_at_restart = 0;
    state.prepared_headers.clear();
    state.unprepared_headers.clear();
    state.writes.clear();
}

inline double queued_duration_ms(const Device& device) {
    double duration = 0.0;
    for (const auto& item : device.queue) duration += item.remaining_ms;
    return duration;
}

inline void record_queue_margin(State& current, const Device& device) {
    if (!device.started) return;
    current.minimum_queue_margin_ms = std::min(
        current.minimum_queue_margin_ms, queued_duration_ms(device));
}

inline void advance_device(State& current, Device& device,
                           std::chrono::steady_clock::time_point now) {
    double elapsed = std::chrono::duration<double, std::milli>(now - device.last_progress).count();
    device.last_progress = now;
    if (elapsed <= 0.0 || device.paused || !device.started) return;

    while (elapsed > 0.0 && !device.queue.empty()) {
        auto& front = device.queue.front();
        const double consumed = std::min(elapsed, front.remaining_ms);
        front.remaining_ms -= consumed;
        elapsed -= consumed;
        if (front.remaining_ms > 0.001) break;

        auto* completed = front.header;
        device.queue.pop_front();
        completed->dwFlags &= ~WHDR_INQUEUE;
        completed->dwFlags |= WHDR_DONE;
        ++current.completed_headers;
        if (device.event) device.event->signaled = true;
    }

    if (elapsed > 0.001 && device.queue.empty()) {
        ++current.underruns;
        current.underrun_ms += elapsed;
    }
    record_queue_margin(current, device);
    current.changed.notify_all();
}

inline HANDLE WINAPI create_event(LPSECURITY_ATTRIBUTES, BOOL manual_reset,
                                  BOOL initial_state, LPCWSTR) {
    if (manual_reset || initial_state) return nullptr;
    return reinterpret_cast<HANDLE>(new Event{});
}

inline BOOL WINAPI close_handle(HANDLE handle) {
    auto* event = reinterpret_cast<Event*>(handle);
    if (!event) return FALSE;
    {
        std::lock_guard<std::mutex> guard(state.mutex);
        event->closed = true;
        if (state.device && state.device->event == event) state.device->event = nullptr;
        ++state.event_closes;
        state.changed.notify_all();
    }
    delete event;
    return TRUE;
}

inline BOOL WINAPI set_event(HANDLE handle) {
    auto* event = reinterpret_cast<Event*>(handle);
    if (!event) return FALSE;
    std::lock_guard<std::mutex> guard(state.mutex);
    event->signaled = true;
    state.changed.notify_all();
    return TRUE;
}

inline DWORD WINAPI wait_for_single_object(HANDLE handle, DWORD timeout_ms) {
    auto* event = reinterpret_cast<Event*>(handle);
    if (!event) return WAIT_FAILED;
    std::unique_lock<std::mutex> lock(state.mutex);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    for (;;) {
        const auto now = std::chrono::steady_clock::now();
        if (state.device) advance_device(state, *state.device, now);
        if (event->closed) return WAIT_FAILED;
        if (event->signaled) {
            event->signaled = false;
            const int delay = state.delayed_callbacks_remaining
                ? state.callback_delay_ms : 0;
            if (delay > 0) {
                --state.delayed_callbacks_remaining;
                lock.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
                lock.lock();
                if (state.device)
                    advance_device(state, *state.device, std::chrono::steady_clock::now());
                ++state.delayed_callbacks;
                if (state.device)
                    state.queue_margin_at_delayed_callback_ms = std::min(
                        state.queue_margin_at_delayed_callback_ms,
                        queued_duration_ms(*state.device));
                event->signaled = false; // auto-reset callbacks coalesce during the delay
                state.changed.notify_all();
            }
            return WAIT_OBJECT_0;
        }

        const auto after_update = std::chrono::steady_clock::now();
        if (after_update >= deadline) return WAIT_TIMEOUT;
        std::chrono::duration<double, std::milli> wait_for = deadline - after_update;
        if (state.device && !state.device->paused && !state.device->queue.empty()) {
            const auto to_completion = std::chrono::duration<double, std::milli>(
                state.device->queue.front().remaining_ms);
            if (to_completion < wait_for) wait_for = to_completion;
        }
        state.changed.wait_for(lock, wait_for);
    }
}

inline HANDLE WINAPI current_thread() {
    return reinterpret_cast<HANDLE>(std::uintptr_t(1));
}

inline BOOL WINAPI set_thread_priority(HANDLE thread, int priority) {
    std::lock_guard<std::mutex> guard(state.mutex);
    ++state.priority_calls;
    state.priority_value = priority;
    state.changed.notify_all();
    return thread == current_thread();
}

inline MMRESULT WINAPI open(LPHWAVEOUT output, UINT, LPCWAVEFORMATEX,
                            DWORD_PTR callback, DWORD_PTR, DWORD flags) {
    if (flags != CALLBACK_EVENT) return MMSYSERR_ERROR;
    std::lock_guard<std::mutex> guard(state.mutex);
    ++state.opens;
    if (state.fail_open) return MMSYSERR_ERROR;
    auto* device = new Device{};
    device->event = reinterpret_cast<Event*>(callback);
    device->last_progress = std::chrono::steady_clock::now();
    state.device = device;
    *output = reinterpret_cast<HWAVEOUT>(device);
    state.changed.notify_all();
    return MMSYSERR_NOERROR;
}

inline MMRESULT WINAPI prepare(HWAVEOUT, WAVEHDR* header, UINT) {
    std::lock_guard<std::mutex> guard(state.mutex);
    ++state.prepare_calls;
    header->dwFlags |= WHDR_PREPARED;
    state.prepared_headers.push_back(header);
    state.changed.notify_all();
    return MMSYSERR_NOERROR;
}

inline MMRESULT WINAPI write(HWAVEOUT handle, WAVEHDR* header, UINT) {
    std::lock_guard<std::mutex> guard(state.mutex);
    auto* device = reinterpret_cast<Device*>(handle);
    if (!device || device != state.device) return MMSYSERR_ERROR;
    advance_device(state, *device, std::chrono::steady_clock::now());
    ++state.write_calls;

    WriteRecord record;
    record.header = header;
    record.data = header->lpData;
    record.length = header->dwBufferLength;
    record.accepted = state.fail_write_on_call != int(state.write_calls);
    if (header->lpData && header->dwBufferLength) {
        const auto* data = reinterpret_cast<const uint8_t*>(header->lpData);
        record.bytes.assign(data, data + header->dwBufferLength);
    }
    state.writes.push_back(std::move(record));
    if (!state.writes.back().accepted) {
        state.changed.notify_all();
        return MMSYSERR_ERROR;
    }

    header->dwFlags &= ~WHDR_DONE;
    header->dwFlags |= WHDR_INQUEUE;
    if (!device->paused && !device->started) {
        device->started = true;
        device->last_progress = std::chrono::steady_clock::now();
    }
    const double duration = 1000.0 * double(header->dwBufferLength) / 176400.0;
    if (!device->paused && device->queue.empty()) {
        device->started = true;
        device->last_progress = std::chrono::steady_clock::now();
    }
    device->queue.push_back({header, duration});
    ++state.accepted_writes;
    record_queue_margin(state, *device);
    state.changed.notify_all();
    return MMSYSERR_NOERROR;
}

inline MMRESULT WINAPI pause(HWAVEOUT handle) {
    std::lock_guard<std::mutex> guard(state.mutex);
    auto* device = reinterpret_cast<Device*>(handle);
    ++state.pause_calls;
    if (!device || device != state.device || state.fail_pause) return MMSYSERR_ERROR;
    advance_device(state, *device, std::chrono::steady_clock::now());
    device->paused = true;
    state.changed.notify_all();
    return MMSYSERR_NOERROR;
}

inline MMRESULT WINAPI restart(HWAVEOUT handle) {
    std::lock_guard<std::mutex> guard(state.mutex);
    auto* device = reinterpret_cast<Device*>(handle);
    ++state.restart_calls;
    state.writes_at_restart = state.accepted_writes;
    state.queue_depth_at_restart = device ? device->queue.size() : 0;
    if (!device || device != state.device || state.fail_restart) return MMSYSERR_ERROR;
    device->paused = false;
    device->started = true;
    device->last_progress = std::chrono::steady_clock::now();
    record_queue_margin(state, *device);
    state.changed.notify_all();
    return MMSYSERR_NOERROR;
}

inline MMRESULT WINAPI reset(HWAVEOUT handle) {
    std::lock_guard<std::mutex> guard(state.mutex);
    auto* device = reinterpret_cast<Device*>(handle);
    ++state.reset_calls;
    if (!device || device != state.device) return MMSYSERR_ERROR;
    for (const auto& item : device->queue) {
        item.header->dwFlags &= ~WHDR_INQUEUE;
        item.header->dwFlags |= WHDR_DONE;
    }
    device->queue.clear();
    device->paused = true;
    device->started = false;
    device->last_progress = std::chrono::steady_clock::now();
    state.changed.notify_all();
    return MMSYSERR_NOERROR;
}

inline MMRESULT WINAPI unprepare(HWAVEOUT, WAVEHDR* header, UINT) {
    std::lock_guard<std::mutex> guard(state.mutex);
    ++state.unprepare_calls;
    header->dwFlags &= ~(WHDR_PREPARED | WHDR_INQUEUE | WHDR_DONE);
    state.unprepared_headers.push_back(header);
    state.changed.notify_all();
    return MMSYSERR_NOERROR;
}

inline MMRESULT WINAPI close(HWAVEOUT handle) {
    std::lock_guard<std::mutex> guard(state.mutex);
    auto* device = reinterpret_cast<Device*>(handle);
    ++state.close_calls;
    if (!device || device != state.device || !device->queue.empty()) return MMSYSERR_ERROR;
    state.device = nullptr;
    state.changed.notify_all();
    delete device;
    return MMSYSERR_NOERROR;
}

inline bool wait_until(bool (*predicate)(const State&), std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(state.mutex);
    return state.changed.wait_for(lock, timeout, [&] { return predicate(state); });
}

} // namespace audio_output_mock

#define CreateEventW audio_output_mock::create_event
#define CloseHandle audio_output_mock::close_handle
#define SetEvent audio_output_mock::set_event
#define WaitForSingleObject audio_output_mock::wait_for_single_object
#define GetCurrentThread audio_output_mock::current_thread
#define SetThreadPriority audio_output_mock::set_thread_priority
#define waveOutOpen audio_output_mock::open
#define waveOutPrepareHeader audio_output_mock::prepare
#define waveOutWrite audio_output_mock::write
#define waveOutPause audio_output_mock::pause
#define waveOutRestart audio_output_mock::restart
#define waveOutReset audio_output_mock::reset
#define waveOutUnprepareHeader audio_output_mock::unprepare
#define waveOutClose audio_output_mock::close
