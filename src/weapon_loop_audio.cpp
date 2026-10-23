#include "lcs/weapon_loop_audio.hpp"
#include "audio_sample_ids.inc"

namespace lcs {

std::optional<WeaponLoopCue> sa_weapon_loop_cue(int32_t type, int32_t event) {
    if (type == SaWeaponLoopProfile::minigun) {
        switch (event) {
        case SaWeaponLoopProfile::weapon_fire:
        case SaWeaponLoopProfile::minigun_ammo:
            return WeaponLoopCue::minigun_ammo;
        case SaWeaponLoopProfile::minigun_no_ammo:
            return WeaponLoopCue::minigun_no_ammo;
        default:
            return std::nullopt;
        }
    }
    if (type == SaWeaponLoopProfile::flamethrower) {
        if (event == SaWeaponLoopProfile::weapon_fire) return WeaponLoopCue::flamethrower_fire;
    }
    return std::nullopt;
}

std::optional<WeaponLoopSampleSpec> weapon_loop_sample_spec(WeaponLoopCue cue) {
    switch (cue) {
    case WeaponLoopCue::minigun_ammo:
        // reLCS SOUND_WEAPON_MINIGUN_ATTACK: base SFX rate, volume 127, 150m.
        return WeaponLoopSampleSpec{SFX_MINIGUN_FIRE_LEFT, 0, 127, 150.0f, true, 0, 0, 3};
    case WeaponLoopCue::minigun_no_ammo:
        // The SA no-ammo event deliberately remains the native fallback.
        return std::nullopt;
    case WeaponLoopCue::minigun_stop:
        // reLCS SOUND_WEAPON_MINIGUN_3 is the one-shot rotor rundown.
        return WeaponLoopSampleSpec{SFX_MINIGUN_STOP, 0, 127, 150.0f, false};
    case WeaponLoopCue::flamethrower_fire:
        // reLCS SOUND_WEAPON_SHOT_FIRED / WEAPONTYPE_FLAMETHROWER. The paired
        // start transient is SOUND_WEAPON_FLAMETHROWER_FIRE.
        return WeaponLoopSampleSpec{SFX_FLAMETHROWER_LEFT, 0, 90, 60.0f, true,
                                    SFX_FLAMETHROWER_START_LEFT, 70, 6,
                                    SFX_FLAMETHROWER_RIGHT, 0.2f};
    }
    return std::nullopt;
}

uint32_t weapon_loop_request_ttl_ms(WeaponLoopCue cue, uint32_t tolerance) {
    const auto spec = weapon_loop_sample_spec(cue);
    if (!spec || !spec->looping || !spec->source_frames) return 0;
    const uint32_t source_ms = (1000u * spec->source_frames) / 30u;
    return tolerance > UINT32_MAX - source_ms ? UINT32_MAX : source_ms + tolerance;
}

bool weapon_loop_request_expired(uint32_t now, uint32_t touched, uint32_t ttl_ms) {
    return ttl_ms != 0 && uint32_t(now - touched) >= ttl_ms;
}

} // namespace lcs
