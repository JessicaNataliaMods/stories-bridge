#pragma once
#include <array>
#include <cstdint>
#include <optional>
namespace lcs {
// Original LCS eWeaponType (reLCS WeaponType.h / WEAPON.DAT order). Only the
// firearms that own a one-shot sound in cAudioManager::ProcessPedOneShots.
namespace lcs_weapon {
enum : int {
    Rocket=16,Colt45=17,Python=18,Shotgun=19,Spas12=20,StubbyShotgun=21,Tec9=22,Uzi=23,SilencedIngram=24,
    Mp5=25,M4=26,Ruger=27,SniperRifle=28,LaserScope=29,RocketLauncher=30,Flamethrower=31,M60=32,Minigun=33,
    HeliCannon=35,Count=37
};
}
// Original eWeaponType -> SA eWeaponType, identical to
// equivalent_weapon_counterparts in native.cpp and WEAPON_TYPES in
// tools/weapon_slots.py (tests/weapon_slot_contracts.py compares all three).
inline constexpr std::array<int,lcs_weapon::Count> lcs_to_sa_weapon_type{
    0,1,11,2,3,4,5,10,13,12,8,9,16,39,17,18,35,22,24,25,27,26,32,28,23,29,31,30,34,34,35,37,33,38,40,38,43};
// One SOUND_WEAPON_SHOT_FIRED entry of ProcessPedOneShots. `left_sample` is an
// eSfxSample index; the right channel is the next sample (stereo=true). The
// emitted volume is `table(volume_table)%volume_modulus+volume_base`, or the
// constant volume_base when volume_modulus is 0.
struct WeaponShotSpec {
    int lcs_weapon;
    const char* name;
    uint32_t left_sample;
    bool stereo;
    int32_t fixed_frequency;     // 0: the sample's own frequency
    uint32_t fixed_jitter;       // RandomDisplacement amplitude when fixed_frequency!=0; otherwise frequency>>5
    uint8_t volume_table,volume_modulus,volume_base;
};
// One SOUND_WEAPON_RELOAD entry: a single mono sample, volume 75, 30 m range.
struct WeaponReloadSpec {
    int lcs_weapon;
    const char* name;
    uint32_t sample;
    int32_t fixed_frequency;     // 0: the sample's own frequency
};
inline constexpr float weapon_shot_max_distance=120.0f;   // PED_ONE_SHOT_WEAPON_BULLET_ECHO_MAX_DIST
inline constexpr float weapon_reload_max_distance=30.0f;  // PED_ONE_SHOT_WEAPON_RELOAD_MAX_DIST
inline constexpr uint8_t weapon_reload_volume=75;         // PED_ONE_SHOT_WEAPON_RELOAD_VOLUME
inline constexpr float weapon_stereo_distance_fraction=0.2f;
// The original weapon whose sound the given SA weapon type plays: the one the
// bridge converted onto that SA type (docs/weapons.md). nullopt for types
// whose sound stays with SA (melee, thrown, flamethrower, minigun, spray...).
std::optional<int> weapon_owner_for_sa_type(int sa_type);
std::optional<WeaponShotSpec> weapon_shot_for_lcs_weapon(int lcs_weapon);
std::optional<WeaponReloadSpec> weapon_reload_for_lcs_weapon(int lcs_weapon);
inline std::optional<WeaponShotSpec> weapon_shot_for_sa_type(int sa_type){
    const auto owner=weapon_owner_for_sa_type(sa_type);
    return owner?weapon_shot_for_lcs_weapon(*owner):std::nullopt;
}
inline std::optional<WeaponReloadSpec> weapon_reload_for_sa_type(int sa_type){
    const auto owner=weapon_owner_for_sa_type(sa_type);
    return owner?weapon_reload_for_lcs_weapon(*owner):std::nullopt;
}
}
