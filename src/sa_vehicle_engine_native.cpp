#include "sa_vehicle_engine_native.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>

#if defined(_WIN32) && defined(_M_IX86)
#include <windows.h>
#endif

namespace lcs::sa_vehicle_engine_native {
namespace {

constexpr uintptr_t player_engine = 0x4FBB10;
constexpr uintptr_t dummy_engine = 0x501480;
constexpr uintptr_t cancel_engine_sound = 0x4F55C0;
constexpr uintptr_t update_engine_sound = 0x4F56D0;
// m_EngineSounds starts at +0xE4. Its CAESound* member is at +4, so the
// verified native pointer expression is audio_entity + index*8 + 0xE8.
constexpr uintptr_t engine_sound_array = 0xE4;
constexpr uintptr_t engine_sound_stride = 8;
constexpr uintptr_t engine_sound_pointer = 4;
constexpr uintptr_t sound_bank_slot = 0x00;
constexpr uintptr_t sound_id = 0x02;
constexpr uintptr_t sound_audio_entity = 0x04;
constexpr uintptr_t sound_volume = 0x14;
constexpr uintptr_t sound_flags = 0x56;
constexpr uintptr_t sound_active = 0x58;
constexpr uint16_t sound_slot_count = 12;

struct VehicleIdentity {
    uintptr_t vehicle = 0;
    uint16_t model = 0;
};

struct SoundSnapshot {
    NativeVehicleEngineSoundIdentity identity{};
    float volume = 0.0f;
    bool present = false;
};

struct SlotState {
    NativeVehicleEngineSoundIdentity identity{};
    float original_volume = 0.0f;
    uint32_t generation = 0;
    bool identity_valid = false;
    bool muted = false;
};

struct EntityState {
    VehicleIdentity vehicle{};
    std::array<SlotState, sound_slot_count> slots{};
};

// ProcessPlayer/DummyVehicleEngine and the corresponding audio entity hooks
// run on the game thread. Each entry exists only while one of its engine
// CAESounds is muted for an LCS-owned loop.
std::unordered_map<uintptr_t, EntityState> entity_states;

bool same_native_identity(const NativeVehicleEngineSoundIdentity& a,
                          const NativeVehicleEngineSoundIdentity& b) noexcept {
    return a.sound != 0 && a.sound == b.sound &&
           a.audio_entity == b.audio_entity && a.vehicle == b.vehicle &&
           a.slot == b.slot && a.vehicle_model == b.vehicle_model &&
           a.bank_slot == b.bank_slot && a.sound_id == b.sound_id &&
           a.flags == b.flags && a.active != 0 && a.active == b.active;
}

void advance_generation(SlotState& state) noexcept {
    ++state.generation;
    if (!state.generation) ++state.generation;
}

bool read_vehicle_identity(const void* audio_entity, VehicleIdentity& out) noexcept {
#if defined(_WIN32) && defined(_M_IX86)
    if (!audio_entity) return false;
    uintptr_t vehicle = 0;
    uint16_t model = 0;
    __try {
        vehicle = *reinterpret_cast<const uintptr_t*>(
            reinterpret_cast<uintptr_t>(audio_entity) + 4); // CAEAudioEntity::m_Entity
        if (vehicle) {
            if (vehicle > std::numeric_limits<uintptr_t>::max() - 0x24u) return false;
            model = *reinterpret_cast<const uint16_t*>(vehicle + 0x22);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!vehicle) return false;
    out.vehicle = vehicle;
    out.model = model;
    return true;
#else
    (void)audio_entity;
    (void)out;
    return false;
#endif
}

bool read_engine_slot(const void* audio_entity, const VehicleIdentity& vehicle,
                      uint16_t slot, SoundSnapshot& out) noexcept {
#if defined(_WIN32) && defined(_M_IX86)
    if (!audio_entity || slot >= sound_slot_count) return false;
    const uintptr_t entity = reinterpret_cast<uintptr_t>(audio_entity);
    if (entity > std::numeric_limits<uintptr_t>::max() -
                     (engine_sound_array + engine_sound_stride * (sound_slot_count - 1) + engine_sound_pointer + 4))
        return false;
    uintptr_t sound = 0;
    uint16_t bank = 0;
    uint16_t id = 0;
    uintptr_t owner = 0;
    float volume = 0.0f;
    uint16_t flags = 0;
    uint16_t active = 0;
    __try {
        sound = *reinterpret_cast<const uintptr_t*>(
            entity + engine_sound_array + engine_sound_stride * slot + engine_sound_pointer);
        if (sound) {
            if ((sound & 3u) || sound > std::numeric_limits<uintptr_t>::max() - 0x5Au)
                return false;
            bank = *reinterpret_cast<const uint16_t*>(sound + sound_bank_slot);
            id = *reinterpret_cast<const uint16_t*>(sound + sound_id);
            owner = *reinterpret_cast<const uintptr_t*>(sound + sound_audio_entity);
            volume = *reinterpret_cast<const float*>(sound + sound_volume);
            flags = *reinterpret_cast<const uint16_t*>(sound + sound_flags);
            active = *reinterpret_cast<const uint16_t*>(sound + sound_active);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    out = {};
    if (!sound) return true;
    out.identity.sound = sound;
    out.identity.audio_entity = owner;
    out.identity.vehicle = vehicle.vehicle;
    out.identity.slot = slot;
    out.identity.vehicle_model = vehicle.model;
    out.identity.bank_slot = bank;
    out.identity.sound_id = id;
    out.identity.flags = flags;
    out.identity.active = active;
    out.volume = volume;
    out.present = owner == entity && active != 0 && std::isfinite(volume);
    return true;
#else
    (void)audio_entity;
    (void)vehicle;
    (void)slot;
    (void)out;
    return false;
#endif
}

bool write_engine_volume_if_owned(
    const void* audio_entity, const VehicleIdentity& vehicle,
    const NativeVehicleEngineSoundIdentity& expected, float expected_volume,
    float replacement_volume, bool require_mute_sentinel) noexcept {
#if defined(_WIN32) && defined(_M_IX86)
    if (!audio_entity || expected.slot >= sound_slot_count) return false;
    if (reinterpret_cast<uintptr_t>(audio_entity) >
        std::numeric_limits<uintptr_t>::max() - engine_sound_array -
            engine_sound_stride * (sound_slot_count - 1) - engine_sound_pointer - 4)
        return false;
    SoundSnapshot current;
    if (!read_engine_slot(audio_entity, vehicle, expected.slot, current) || !current.present)
        return false;
    current.identity.generation = expected.generation;
    if (!same_native_identity(expected, current.identity)) return false;
    if (require_mute_sentinel) {
        if (!native_vehicle_engine_can_restore(expected, current.identity, current.volume))
            return false;
    } else if (current.volume != expected_volume) {
        return false;
    }

    __try {
        // Recheck slot ownership and identity immediately before the only
        // write. This never edits a horn/siren field or a non-engine slot.
        const uintptr_t entity = reinterpret_cast<uintptr_t>(audio_entity);
        const uintptr_t sound = *reinterpret_cast<const uintptr_t*>(
            entity + engine_sound_array + engine_sound_stride * expected.slot + engine_sound_pointer);
        if (sound != expected.sound || (sound & 3u) ||
            sound > std::numeric_limits<uintptr_t>::max() - 0x5Au)
            return false;
        if (*reinterpret_cast<const uintptr_t*>(entity + 4) != expected.vehicle ||
            *reinterpret_cast<const uint16_t*>(expected.vehicle + 0x22) != expected.vehicle_model ||
            *reinterpret_cast<const uintptr_t*>(sound + sound_audio_entity) != entity ||
            *reinterpret_cast<const uint16_t*>(sound + sound_bank_slot) != expected.bank_slot ||
            *reinterpret_cast<const uint16_t*>(sound + sound_id) != expected.sound_id ||
            *reinterpret_cast<const uint16_t*>(sound + sound_flags) != expected.flags ||
            *reinterpret_cast<const uint16_t*>(sound + sound_active) != expected.active ||
            *reinterpret_cast<const float*>(sound + sound_volume) != current.volume)
            return false;
        *reinterpret_cast<float*>(sound + sound_volume) = replacement_volume;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
#else
    (void)audio_entity;
    (void)vehicle;
    (void)expected;
    (void)expected_volume;
    (void)replacement_volume;
    (void)require_mute_sentinel;
    return false;
#endif
}

template <typename Function>
bool for_engine_slots(bool player, Function&& function) {
    for (uint16_t slot = 0; slot < sound_slot_count; ++slot) {
        if (native_vehicle_engine_slot_eligible(slot, player) && !function(slot)) return false;
    }
    return true;
}

bool restore_previous_mutes(const void* audio_entity, const VehicleIdentity& vehicle,
                            EntityState& state) {
    bool ok = true;
    for (uint16_t index = 0; index < sound_slot_count; ++index) {
        auto& slot = state.slots[index];
        if (!slot.muted) continue;

        SoundSnapshot current;
        if (!read_engine_slot(audio_entity, vehicle, index, current)) {
            ok = false;
            continue;
        }
        if (!current.present) {
            slot.muted = false;
            slot.identity_valid = false;
            advance_generation(slot);
            continue;
        }
        current.identity.generation = slot.generation;
        if (!native_vehicle_engine_same_generation(slot.identity, current.identity)) {
            // The slot or voice was recycled. Never apply an older volume to
            // a sound that no longer has the captured native identity.
            slot.muted = false;
            slot.identity_valid = false;
            advance_generation(slot);
            continue;
        }
        if (current.volume != native_vehicle_engine_mute_db) {
            // Another native writer changed the value; relinquish ownership.
            slot.muted = false;
            continue;
        }
        if (!write_engine_volume_if_owned(audio_entity, vehicle, slot.identity,
                                          native_vehicle_engine_mute_db,
                                          slot.original_volume, true)) {
            ok = false;
            continue;
        }
        slot.muted = false;
    }
    return ok;
}

void rollback_partial_mutes(const void* audio_entity, const VehicleIdentity& vehicle,
                            EntityState& state) {
    (void)restore_previous_mutes(audio_entity, vehicle, state);
}

bool any_muted(const EntityState& state) noexcept {
    for (const auto& slot : state.slots) if (slot.muted) return true;
    return false;
}

} // namespace

void reset() noexcept {entity_states.clear();}

bool profile_matches() {
#if defined(_WIN32) && defined(_M_IX86)
    constexpr uint8_t player_signature[]{
        0x51, 0x56, 0x8B, 0xF1, 0x33, 0xC9, 0x66, 0x8B, 0x8E, 0xE0, 0x00, 0x00};
    constexpr uint8_t dummy_signature[]{
        0x56, 0x8B, 0xF1, 0x66, 0x83, 0xBE, 0xE0, 0x00, 0x00, 0x00, 0xFF, 0x75};
    constexpr uint8_t engine_array_load[]{
        0x8B, 0x8C, 0xC6, 0xE8, 0x00, 0x00, 0x00};
    constexpr uint8_t engine_volume_store[]{
        0xD9, 0x59, 0x14, 0x89, 0x51, 0x1C};
    __try {
        return std::memcmp(reinterpret_cast<const void*>(player_engine),
                           player_signature, sizeof(player_signature)) == 0 &&
               std::memcmp(reinterpret_cast<const void*>(dummy_engine),
                           dummy_signature, sizeof(dummy_signature)) == 0 &&
               std::memcmp(reinterpret_cast<const void*>(cancel_engine_sound + 0x0D),
                           engine_array_load, sizeof(engine_array_load)) == 0 &&
               std::memcmp(reinterpret_cast<const void*>(update_engine_sound + 0x1E),
                           engine_volume_store, sizeof(engine_volume_store)) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    return false;
#endif
}

bool before(const void* audio_entity, bool player) {
    (void)player; // restore the exact slots captured by the previous frame
    if (!audio_entity || !profile_matches()) return false;
    const auto found = entity_states.find(reinterpret_cast<uintptr_t>(audio_entity));
    if (found == entity_states.end()) return true;

    VehicleIdentity current_vehicle;
    if (!read_vehicle_identity(audio_entity, current_vehicle)) return false;
    auto& state = found->second;
    if (state.vehicle.vehicle != current_vehicle.vehicle ||
        state.vehicle.model != current_vehicle.model) {
        // This audio entity was reused for a different vehicle. Its cached
        // engine sound volumes no longer belong to the current owner.
        entity_states.erase(found);
        return false;
    }
    const bool restored = restore_previous_mutes(audio_entity, current_vehicle, state);
    if (!restored) return false;
    return true;
}

bool after(const void* audio_entity, bool player, bool lcs_owns_engine) {
    if (!audio_entity || !profile_matches()) return false;
    const auto key = reinterpret_cast<uintptr_t>(audio_entity);
    VehicleIdentity vehicle;
    if (!read_vehicle_identity(audio_entity, vehicle)) return false;

    auto found = entity_states.find(key);
    if (found != entity_states.end() &&
        (found->second.vehicle.vehicle != vehicle.vehicle ||
         found->second.vehicle.model != vehicle.model)) {
        // Do not transfer a saved mute across audio entity reuse.
        const bool had_muted_sound = any_muted(found->second);
        entity_states.erase(found);
        if (had_muted_sound) return false;
        found = entity_states.end();
    }

    EntityState* state = nullptr;
    if (found != entity_states.end()) {
        state = &found->second;
        if (!restore_previous_mutes(audio_entity, vehicle, *state)) return false;
    }
    if (!lcs_owns_engine) {
        if (found != entity_states.end()) entity_states.erase(found);
        return true;
    }

    if (!state) {
        state = &entity_states[key];
        state->vehicle = vehicle;
    }

    bool ok = for_engine_slots(player, [&](uint16_t index) {
        auto& slot = state->slots[index];
        SoundSnapshot current;
        if (!read_engine_slot(audio_entity, vehicle, index, current)) return false;
        if (!current.present) {
            if (slot.identity_valid) {
                slot.identity_valid = false;
                advance_generation(slot);
            }
            slot.muted = false;
            return true;
        }

        if (!slot.identity_valid || !same_native_identity(slot.identity, current.identity)) {
            advance_generation(slot);
            slot.identity = current.identity;
            slot.identity_valid = true;
        }
        current.identity.generation = slot.generation;
        slot.identity = current.identity;
        slot.original_volume = current.volume;
        slot.muted = true;
        if (!write_engine_volume_if_owned(audio_entity, vehicle, slot.identity,
                                          current.volume, native_vehicle_engine_mute_db,
                                          false)) {
            return false;
        }
        return true;
    });

    if (!ok) {
        rollback_partial_mutes(audio_entity, vehicle, *state);
        if (!any_muted(*state)) entity_states.erase(key);
        return false;
    }
    if (!any_muted(*state)) entity_states.erase(key);
    return true;
}

} // namespace lcs::sa_vehicle_engine_native
