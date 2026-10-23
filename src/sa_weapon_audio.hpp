#pragma once
#include <array>
#include <cstdint>
#include <optional>
namespace lcs {
// SA 1.0 US audio entry points for gun sounds (all checked against the exe):
//   CAEWeaponAudioEntity::WeaponFire   0x504F80  thiscall(type, ped, event) ret 0xC
//   CAEWeaponAudioEntity::WeaponReload 0x503690  thiscall(type, ped, event) ret 0xC
// Both are reached from CAEPedWeaponAudioEntity::AddAudioEvent (0x4E6A3D /
// 0x4E6A5B, the active weapon of a ped, so NPCs and the player alike) and from
// the vehicle-weapon dispatcher at 0x4DFAA0 (0x4DFAC6 / 0x4DFAD6).
struct SaWeaponAudioProfile {
    static constexpr uintptr_t fire=0x504F80,reload=0x503690;
    static constexpr std::array<uintptr_t,2> fire_calls{0x4E6A3D,0x4DFAC6};
    static constexpr std::array<uintptr_t,2> reload_calls{0x4E6A5B,0x4DFAD6};
};
// The functions start with the expected bytes and every call site targets them.
bool sa_weapon_audio_profile_matches();
// World position of a CPhysical (ped or vehicle): matrix position, or the
// placement position when the entity has no matrix yet.
std::optional<std::array<float,3>> sa_physical_position(const void* entity);
}
