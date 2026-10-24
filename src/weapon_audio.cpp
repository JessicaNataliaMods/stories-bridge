#include "lcs/weapon_audio.hpp"
#include "audio_sample_ids.inc"

namespace lcs {
// Source: reLCS AudioLogic.cpp, cAudioManager::ProcessPedOneShots,
// SOUND_WEAPON_SHOT_FIRED and SOUND_WEAPON_RELOAD. Volumes: Colt45 90, Uzi 70,
// Shotgun 100, M4 90, M16 (M60) 127, Sniper 110, Rocket launcher 80.
// User-requested comfort adjustment: M60 is 90 instead of its source 127 (~-3 dB).
// Quirk kept out on purpose: the original also flags the rocket launcher as
// stereo, but SFX_ROCKET_LEFT is followed by SFX_ROCKET_RELOAD (there is no
// right sample), so the original would layer the reload clack on every launch
// at close range. The launch is played mono.
namespace {
constexpr uint8_t no_table=0xFF;
constexpr WeaponShotSpec shots[]={
    {lcs_weapon::Colt45,"Colt45",SFX_COLT45_LEFT,true,0,0,1,10,90},
    {lcs_weapon::Python,"Python",SFX_PYTHON_LEFT,true,0,0,no_table,0,127},
    {lcs_weapon::RocketLauncher,"RocketLauncher",SFX_ROCKET_LEFT,false,0,0,0,20,80},
    {lcs_weapon::M60,"M60",SFX_M60_LEFT,true,0,0,no_table,0,90},
    {lcs_weapon::Mp5,"MP5",SFX_MP5_LEFT,true,0,0,3,15,70},
    {lcs_weapon::Tec9,"Tec9",SFX_TEC_LEFT,true,17000,500,3,15,70},
    {lcs_weapon::SilencedIngram,"SilencedIngram",SFX_TEC_LEFT,true,34000,1000,3,15,70},
    {lcs_weapon::Ruger,"Ruger",SFX_RUGER_LEFT,true,0,0,3,15,90},
    {lcs_weapon::M4,"M4",SFX_RUGER_LEFT,true,43150,1000,3,15,90},
    {lcs_weapon::Uzi,"Uzi",SFX_UZI_LEFT,true,0,0,3,15,70},
    {lcs_weapon::SniperRifle,"SniperRifle",SFX_SNIPER_LEFT,true,25472,796,4,10,110},
    {lcs_weapon::Spas12,"Spas12",SFX_SPAS12_LEFT,true,0,0,2,10,100},
    {lcs_weapon::Shotgun,"Shotgun",SFX_SHOTGUN_LEFT,true,0,0,2,10,100},
    {lcs_weapon::StubbyShotgun,"StubbyShotgun",SFX_SHOTGUN_LEFT,true,0,0,2,10,100},
};
constexpr WeaponReloadSpec reloads[]={
    {lcs_weapon::Colt45,"Colt45",SFX_PISTOL_RELOAD,0},
    {lcs_weapon::Python,"Python",SFX_PISTOL_RELOAD,0},
    {lcs_weapon::Tec9,"Tec9",SFX_AK47_RELOAD,39243},
    {lcs_weapon::Uzi,"Uzi",SFX_AK47_RELOAD,39243},
    {lcs_weapon::SilencedIngram,"SilencedIngram",SFX_AK47_RELOAD,39243},
    {lcs_weapon::Mp5,"MP5",SFX_AK47_RELOAD,39243},
    {lcs_weapon::M4,"M4",SFX_AK47_RELOAD,39243},
    {lcs_weapon::M60,"M60",SFX_AK47_RELOAD,39243},
    {lcs_weapon::Shotgun,"Shotgun",SFX_AK47_RELOAD,30290},
    {lcs_weapon::Spas12,"Spas12",SFX_AK47_RELOAD,30290},
    {lcs_weapon::StubbyShotgun,"StubbyShotgun",SFX_AK47_RELOAD,30290},
    {lcs_weapon::Ruger,"Ruger",SFX_AK47_RELOAD,30290},
    {lcs_weapon::RocketLauncher,"RocketLauncher",SFX_ROCKET_RELOAD,0},
    {lcs_weapon::SniperRifle,"SniperRifle",SFX_RIFLE_RELOAD,0},
};
static_assert(SFX_COLT45_RIGHT==SFX_COLT45_LEFT+1 && SFX_PYTHON_RIGHT==SFX_PYTHON_LEFT+1 && SFX_M60_RIGHT==SFX_M60_LEFT+1 &&
              SFX_MP5_RIGHT==SFX_MP5_LEFT+1 && SFX_TEC_RIGHT==SFX_TEC_LEFT+1 && SFX_RUGER_RIGHT==SFX_RUGER_LEFT+1 &&
              SFX_UZI_RIGHT==SFX_UZI_LEFT+1 && SFX_SNIPER_RIGHT==SFX_SNIPER_LEFT+1 && SFX_SPAS12_RIGHT==SFX_SPAS12_LEFT+1 &&
              SFX_SHOTGUN_RIGHT==SFX_SHOTGUN_LEFT+1,"Stereo weapon samples are adjacent LEFT/RIGHT entries");
}
// The SA type each original weapon was converted onto and that type's single
// sound owner (docs/weapons.md). Shared SA types keep the weapon whose model is
// mounted there: the sniper (not the laser scope) and the rocket launcher (not
// the projectile). Flamethrower and minigun are continuous sounds, not shots,
// and stay with SA until they get their own looped service.
std::optional<int> weapon_owner_for_sa_type(int sa_type) {
    switch(sa_type) {
    case 22:return lcs_weapon::Colt45;
    case 23:return lcs_weapon::SilencedIngram;
    case 24:return lcs_weapon::Python;
    case 25:return lcs_weapon::Shotgun;
    case 26:return lcs_weapon::StubbyShotgun;
    case 27:return lcs_weapon::Spas12;
    case 28:return lcs_weapon::Uzi;
    case 29:return lcs_weapon::Mp5;
    case 30:return lcs_weapon::Ruger;
    case 31:return lcs_weapon::M4;
    case 32:return lcs_weapon::Tec9;
    case 33:return lcs_weapon::M60;
    case 34:return lcs_weapon::SniperRifle;
    case 35:return lcs_weapon::RocketLauncher;
    default:return std::nullopt;
    }
}
std::optional<WeaponShotSpec> weapon_shot_for_lcs_weapon(int lcs_weapon) {
    for(const auto& spec:shots)if(spec.lcs_weapon==lcs_weapon)return spec;
    return std::nullopt;
}
std::optional<WeaponReloadSpec> weapon_reload_for_lcs_weapon(int lcs_weapon) {
    for(const auto& spec:reloads)if(spec.lcs_weapon==lcs_weapon)return spec;
    return std::nullopt;
}
}
