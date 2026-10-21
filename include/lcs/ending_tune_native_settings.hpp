#pragma once
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <optional>
namespace lcs {
struct EndingTuneNativeSettings {bool paused;float music_scale;};
template<class Match,class Read> std::optional<EndingTuneNativeSettings> original_ending_tune_native_settings(Match match,Read read) {
    // Reviewed CAudioHardware master/fader stores and their music wrappers.
    // Read live values; do not take ownership of SA's shared volume controls.
    if(!match(0x4D9530,{0x8b,0x44,0x24,0x04,0x89,0x81,0x10,0x04,0x00,0x00,0xc2,0x04,0x00}) ||
       !match(0x4D9550,{0x8b,0x44,0x24,0x04,0x89,0x81,0x18,0x04,0x00,0x00,0xc2,0x04,0x00}) ||
       !match(0x506DEA,{0xb9,0xb8,0xf8,0xb5,0x00}) ||
       !match(0x506E40,{0xb9,0xb8,0xf8,0xb5,0x00,0xe9,0x06,0x27,0xfd,0xff}) ||
       !match(0x50776A,{0xa0,0x49,0xcb,0xb7,0x00,0x84,0xc0,0x75,0x19,0xa0,0x48,0xcb,0xb7,0x00,0x84,0xc0,0x75,0x10}))return {};
    float master=0,fade=0;uint8_t pause_flags[2]{};
    if(!read(0xB5FCC8,&master,sizeof(master)) || !read(0xB5FCD0,&fade,sizeof(fade)) ||
       !read(0xB7CB48,pause_flags,sizeof(pause_flags)) || pause_flags[0]>1 || pause_flags[1]>1 || !std::isfinite(master) || !std::isfinite(fade) ||
       master<0 || master>1 || fade<0 || fade>1)return {};
    return EndingTuneNativeSettings{bool(pause_flags[0] || pause_flags[1]),master*fade};
}
}
