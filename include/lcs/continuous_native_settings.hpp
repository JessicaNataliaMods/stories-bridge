#pragma once

#include "ending_tune_native_settings.hpp"

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <optional>

namespace lcs {

struct ContinuousNativeSettings {
    bool paused;
    float time_scale;
    bool camera_switched;
    float effects_scale;
};

// Capture current SA state only after the retail providers are recognized.
// Pause and music-profile validation reuse the approved ending-tune reader;
// target hardware gain is effects master * effects fader * non-stream fader.
// Source global SFX volume and DoubleVolume belong to the audio backend.
template<class Match, class Read>
std::optional<ContinuousNativeSettings> original_continuous_native_settings(
    Match match, Read read) {
    const auto ending = original_ending_tune_native_settings(match, read);
    if (!ending ||
        !match(0x4D9540, {0x8b,0x44,0x24,0x04,0x89,0x81,0x14,0x04,0x00,0x00,0xc2,0x04,0x00}) ||
        !match(0x4D9560, {0x8b,0x44,0x24,0x04,0x89,0x81,0x1c,0x04,0x00,0x00,0xc2,0x04,0x00}) ||
        !match(0x4D95A0, {0x8b,0x44,0x24,0x04,0x89,0x81,0x20,0x04,0x00,0x00,0xc2,0x04,0x00}) ||
        !match(0x506E10, {0x0f,0xbe,0x44,0x24,0x04,0x89,0x44,0x24,0x04,0x51,0xb9,0xb8,0xf8,0xb5,0x00,
                          0xdb,0x44,0x24,0x08,0xd8,0x0d,0xd4,0xa7,0x85,0x00,0xd9,0x1c,0x24,0xe8,0x0f,0x27,0xfd,0xff,0xc2,0x04,0x00}) ||
        !match(0x506E50, {0xb9,0xb8,0xf8,0xb5,0x00,0xe9,0x06,0x27,0xfd,0xff}) ||
        !match(0x506E60, {0xb9,0xb8,0xf8,0xb5,0x00,0xe9,0x36,0x27,0xfd,0xff}) ||
        !match(0x561AD0, {0xd9,0x05,0x64,0xcb,0xb7,0x00,0xd8,0x1d,0x24,0x86,0x85,0x00,
                          0xdf,0xe0,0xf6,0xc4,0x05,0x7a,0x03,0xb0,0x01,0xc3,0x32,0xc0,0xc3}) ||
        !match(0x50AE10, {0x8a,0x41,0x2a,0xc3}))
        return {};

    // CAEAudioHardware starts at B5F8B8. The contiguous fields at +410..+420
    // are music master, effects master, music fader, effects fader and the
    // non-stream fader. Read them together so the three gains share a sample.
    float hardware[5]{};
    float time_scale = 0.0f;
    uint32_t one_bits = 0;
    uint8_t camera_switched = 0;
    if (!read(0xB5FCC8, hardware, sizeof(hardware)) ||
        !read(0xB7CB64, &time_scale, sizeof(time_scale)) ||
        !read(0x858624, &one_bits, sizeof(one_bits)) ||
        !read(0xB6F052, &camera_switched, sizeof(camera_switched)) ||
        one_bits != 0x3F800000u || camera_switched > 1 ||
        !std::isfinite(time_scale) || time_scale < 0.0f)
        return {};

    const float effects_master = hardware[1];
    const float effects_fader = hardware[3];
    const float non_stream_fader = hardware[4];
    if (!std::isfinite(effects_master) || effects_master < 0.0f || effects_master > 1.0f ||
        !std::isfinite(effects_fader) || effects_fader < 0.0f || effects_fader > 1.0f ||
        !std::isfinite(non_stream_fader) || non_stream_fader < 0.0f || non_stream_fader > 1.0f)
        return {};

    const float effects_scale = (effects_master * effects_fader) * non_stream_fader;
    if (!std::isfinite(effects_scale) || effects_scale < 0.0f || effects_scale > 1.0f)
        return {};
    return ContinuousNativeSettings{
        ending->paused, time_scale, bool(camera_switched), effects_scale};
}

} // namespace lcs
