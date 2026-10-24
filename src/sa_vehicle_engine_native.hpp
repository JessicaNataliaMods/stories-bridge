#pragma once

#include <cstdint>

namespace lcs {

// CAESound identity captured from a CAEVehicleAudioEntity engine slot. The
// generation is local to the observed entity/slot and advances when its
// pointer or native identity changes.
struct NativeVehicleEngineSoundIdentity {
    uintptr_t sound = 0;
    uintptr_t audio_entity = 0;
    uintptr_t vehicle = 0;
    uint32_t generation = 0;
    uint16_t slot = 0;
    uint16_t vehicle_model = 0;
    uint16_t bank_slot = 0;
    uint16_t sound_id = 0;
    uint16_t flags = 0;
    uint16_t active = 0;
};

inline constexpr float native_vehicle_engine_mute_db = -100.0f;

// Slot 0 (CAR_REV) is also used by the player wheelspin state.
// Native SA car/bike engine slots. Slots 7..11 include nitro, damage and
// moving-part sounds and stay native even when an LCS engine loop is active.
constexpr bool native_vehicle_engine_slot_eligible(uint16_t slot,
                                                   bool player) noexcept {
    return player ? slot <= 6 : slot <= 1;
}

// Pure identity check for the restore path. The current snapshot must carry
// the generation captured for this slot as well as the same native sound,
// owner, vehicle and engine-slot identity.
constexpr bool native_vehicle_engine_same_generation(
    const NativeVehicleEngineSoundIdentity& saved,
    const NativeVehicleEngineSoundIdentity& current) noexcept {
    return saved.sound != 0 && saved.audio_entity != 0 && saved.vehicle != 0 &&
           saved.sound == current.sound &&
           saved.audio_entity == current.audio_entity &&
           saved.vehicle == current.vehicle &&
           saved.generation == current.generation &&
           saved.slot == current.slot &&
           saved.vehicle_model == current.vehicle_model &&
           saved.bank_slot == current.bank_slot &&
           saved.sound_id == current.sound_id &&
           saved.flags == current.flags && saved.active != 0 &&
           saved.active == current.active;
}

constexpr bool native_vehicle_engine_can_restore(
    const NativeVehicleEngineSoundIdentity& saved,
    const NativeVehicleEngineSoundIdentity& current,
    float current_volume) noexcept {
    return native_vehicle_engine_same_generation(saved, current) &&
           current_volume == native_vehicle_engine_mute_db;
}

// Always advance SA state before reading its live RPM values for LCS.
template<class Before,class Native,class Replacement,class After,class Fallback>
void dispatch_vehicle_engine(Before before,Native native,Replacement replacement,After after,Fallback fallback) {
    bool safe=false;
    try {safe=before();}catch(...){}
    native();
    try {
        const bool owns=safe && replacement();
        if(!after(owns) || !safe)fallback();
    }catch(...){fallback();}
}

namespace sa_vehicle_engine_native {

// These hooks bracket the original SA engine processor. `after` mutes only
// its engine CAESounds when the LCS loop pair has taken ownership.
void reset() noexcept;
bool profile_matches();
bool before(const void* audio_entity, bool player);
bool after(const void* audio_entity, bool player, bool lcs_owns_engine);

} // namespace sa_vehicle_engine_native
} // namespace lcs
