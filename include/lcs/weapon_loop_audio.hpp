#pragma once

#include <cstdint>
#include <optional>

namespace lcs {

// SA 1.0 US audio event and weapon identities from the local plugin-sdk
// reference. LCS's converted flamethrower/minigun occupy these same SA slots.
struct SaWeaponLoopProfile {
    static constexpr int32_t flamethrower = 37;
    static constexpr int32_t minigun = 38;
    static constexpr int32_t weapon_fire = 145;
    static constexpr int32_t minigun_ammo = 150;
    static constexpr int32_t minigun_no_ammo = 151;
};

enum class WeaponLoopCue : uint8_t {
    minigun_ammo,
    minigun_no_ammo,
    minigun_stop,
    flamethrower_fire
};

struct WeaponLoopSampleSpec {
    uint32_t sample = 0;
    uint32_t frequency = 0; // zero uses the authored sample frequency
    uint8_t volume = 0;
    float max_distance = 0.0f;
    bool looping = false;
    uint32_t start_sample = 0; // only the LCS flamethrower has a start transient
    uint8_t start_volume = 0;
    uint8_t source_frames = 0;
    uint32_t paired_sample = 0; // LCS flame voice has authored left/right samples
    float paired_stereo_distance = 0.0f; // fraction of max_distance where both voices are used
};

std::optional<WeaponLoopCue> sa_weapon_loop_cue(int32_t sa_weapon_type, int32_t event);
std::optional<WeaponLoopSampleSpec> weapon_loop_sample_spec(WeaponLoopCue cue);
uint32_t weapon_loop_request_ttl_ms(WeaponLoopCue cue, uint32_t scheduler_tolerance_ms = 50);
bool weapon_loop_request_expired(uint32_t now, uint32_t touched, uint32_t ttl_ms);

} // namespace lcs
