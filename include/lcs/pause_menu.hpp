#pragma once
#include <array>
#include <cstdint>
namespace lcs {
struct PauseMenuGate {
    bool disabled=false;
    // Permit the native frontend's own loading/closing operations. Only a new
    // manual activation from gameplay is blocked by original LCS script state.
    bool delegate(bool active,bool closing,bool activate_next,bool save_requested=false)const {
        return !disabled || active || closing || activate_next || save_requested;
    }
};
inline constexpr uintptr_t native_pause_tail=0x57B477;
inline constexpr std::array<uint8_t,8> native_pause_tail_bytes={0x8B,0xCE,0x5E,0xE9,0xF4,0xB6,0xFF,0xFF};
bool sa_disable_pause_menu(bool);
bool sa_release_pause_menu();
}
