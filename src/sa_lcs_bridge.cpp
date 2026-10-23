#include <windows.h>
#include "lcs/sa_executor.hpp"
#include "sa_lcs_bridge.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>

namespace lcs {
namespace {

// Converted v63 IPL placement records: source indices 301/302/305 are
// liftweight/liftsec/subbridge_lift at these coordinates. Active bindings map
// them to SA IDs 11349/11348/11350; ASI supplies those IDs from the mounted
// model-bindings.tsv rather than relying on constants here.
constexpr float bridge_weight_x = -283.63720703125f;
constexpr float bridge_weight_y = -630.54266357421875f;
constexpr float bridge_weight_z = 128.07135009765625f;
constexpr float bridge_lift_x = -283.159027099609375f;
constexpr float bridge_lift_y = -629.8465576171875f;
constexpr float bridge_lift_z = 74.7115631103515625f;
constexpr float bridge_road_x = -279.43817138671875f;
constexpr float bridge_road_y = -631.564208984375f;
constexpr float bridge_road_z = 43.008823394775390625f;

constexpr int state_locked = 0;
constexpr int state_lift_up = 1;
constexpr int state_moving_down = 2;
constexpr int state_down = 3;
constexpr int state_about_to_move_up = 4;
constexpr int state_moving_up = 5;
constexpr int state_always_unlocked = 6;

constexpr float bridge_lift_height = 25.0f;
constexpr uint32_t bridge_cycle_ms = 0x20000;

using BridgeEntity = SaLcsBridgeEntityRef;

using BridgeEntities = SaLcsBridgeOwnedEntities;

struct BridgeState {
    int32_t current = state_locked;
    int32_t previous = state_locked;
    uint32_t operational_since = 0;
    float default_lift_z = 0.0f;
    float default_road_z = 0.0f;
    float default_weight_z = 0.0f;
    float previous_lift = -1.0f;
    bool defaults_known = false;
    bool path_state_sent = false;
    bool configured = false;
    SaLcsBridgeModelIds model_ids{};
};

BridgeState bridge;

constexpr uint8_t set_posn_profile[5] = {0xE9,0xAB,0x62,0x14,0x01};
constexpr uint8_t update_rw_matrix_profile[5] = {0xE9,0x5B,0x2C,0x12,0x01};
constexpr uint8_t set_path_links_profile[16] = {
    0x53,0x8A,0x5C,0x24,0x18,0x55,0x56,0x57,
    0x8D,0x91,0x04,0x13,0x00,0x00,0xBD,0x40
};
constexpr uint8_t remove_entity_profile[16] = {
    0x83,0xEC,0x30,0x53,0x55,0x56,0x8B,0xF1,
    0x8B,0x06,0x57,0x8D,0x4C,0x24,0x30,0x51
};
constexpr uint8_t add_entity_profile[16] = {
    0x83,0xEC,0x10,0x56,0x8B,0xF1,0x8B,0x06,
    0x8D,0x4C,0x24,0x04,0x51,0x8B,0xCE,0xFF
};
constexpr uint8_t update_rw_frame_profile[16] = {
    0x8B,0x41,0x18,0x85,0xC0,0x74,0x0A,0x8B,
    0x40,0x04,0x50,0xE8,0x00,0xDE,0x2B,0x00
};

bool bytes_match(uintptr_t address, const uint8_t* expected, size_t size) {
    return std::memcmp(reinterpret_cast<const void*>(address), expected, size) == 0;
}

bool executable_target(uintptr_t address) {
    MEMORY_BASIC_INFORMATION info{};
    if (!address || !VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) ||
        info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD)) return false;
    const DWORD protection = info.Protect & 0xff;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

bool hooked_entry_matches(uintptr_t address, const uint8_t* expected) {
    if (!bytes_match(address, expected, 5) || expected[0] != 0xE9) return false;
    int32_t displacement = 0;
    std::memcpy(&displacement, reinterpret_cast<const void*>(address + 1), sizeof(displacement));
    const uintptr_t target = address + 5 + static_cast<intptr_t>(displacement);
    return target != address && executable_target(target);
}

bool engine_profile_matches() {
    // SetPosn and UpdateRwMatrix are existing compatible detours in the active
    // installation. Pin their exact entry patch and require its live target to
    // remain executable; never replace either hook.
    return hooked_entry_matches(0x420B80, set_posn_profile) &&
        hooked_entry_matches(0x446F90, update_rw_matrix_profile) &&
        bytes_match(0x44D960, set_path_links_profile, sizeof(set_path_links_profile)) &&
        bytes_match(0x534AE0, remove_entity_profile, sizeof(remove_entity_profile)) &&
        bytes_match(0x533020, add_entity_profile, sizeof(add_entity_profile)) &&
        bytes_match(0x532B00, update_rw_frame_profile, sizeof(update_rw_frame_profile));
}

bool model_ids_valid(SaLcsBridgeModelIds ids) {
    return ids.lift > 0 && ids.weight > 0 && ids.road > 0 &&
        ids.lift < 20000 && ids.weight < 20000 && ids.road < 20000 &&
        ids.lift != ids.weight && ids.lift != ids.road && ids.weight != ids.road;
}

bool close_to(float value, float expected) {
    return std::isfinite(value) && std::fabs(value - expected) <= 1.0f;
}

bool read_position(uintptr_t entity, float* position) {
    const uintptr_t matrix = *reinterpret_cast<const uintptr_t*>(entity + 0x14);
    const uintptr_t coordinates = matrix ? matrix + 0x30 : entity + 4;
    for (int axis = 0; axis < 3; ++axis) {
        position[axis] = *reinterpret_cast<const float*>(coordinates + 4 * axis);
        if (!std::isfinite(position[axis])) return false;
    }
    return true;
}

SaLcsBridgeResolveStatus scan_pool(uintptr_t pool_root, SaLcsBridgeModelIds ids,
                                   BridgeEntities& result) {
    const uintptr_t pool = *reinterpret_cast<const uintptr_t*>(pool_root);
    if (!pool) return SaLcsBridgeResolveStatus::retry_streaming;
    const uintptr_t entries = *reinterpret_cast<const uintptr_t*>(pool);
    const uintptr_t flags = *reinterpret_cast<const uintptr_t*>(pool + 4);
    const int32_t count = *reinterpret_cast<const int32_t*>(pool + 8);
    if (!entries || !flags || count < 0 || count > 200000)
        return SaLcsBridgeResolveStatus::unexpected_mismatch;

    int lift_count = 0, road_count = 0, weight_count = 0;

    for (int32_t slot = 0; slot < count; ++slot) {
        if (*reinterpret_cast<const uint8_t*>(flags + slot) & 0x80) continue;
        const uintptr_t entity = entries + uintptr_t(slot) * 0x38;
        const auto model = *reinterpret_cast<const uint16_t*>(entity + 0x22);
        if (model != ids.lift && model != ids.road && model != ids.weight) continue;
        // SA eEntityType: building=1, vehicle=2, ped=3, object=4. These
        // bindings are world buildings; an ID in the wrong native pool class
        // is a data/ownership mismatch, never a streaming miss.
        if ((*reinterpret_cast<const uint8_t*>(entity + 0x36) & 7) != 1)
            return SaLcsBridgeResolveStatus::unexpected_mismatch;

        BridgeEntity* target = nullptr;
        int* matches = nullptr;
        float expected_x = 0.0f, expected_y = 0.0f;
        if (model == ids.lift) {
            target = &result.lift; matches = &lift_count;
            expected_x = bridge_lift_x; expected_y = bridge_lift_y;
        } else if (model == ids.road) {
            target = &result.road; matches = &road_count;
            expected_x = bridge_road_x; expected_y = bridge_road_y;
        } else {
            target = &result.weight; matches = &weight_count;
            expected_x = bridge_weight_x; expected_y = bridge_weight_y;
        }

        BridgeEntity candidate{};
        candidate.address = entity;
        candidate.pool_slot = static_cast<uint32_t>(slot);
        if (!read_position(entity, candidate.position))
            return SaLcsBridgeResolveStatus::unexpected_mismatch;
        // Model IDs are project-owned, but also bind to the converted IPL
        // placement so another mod using one of these IDs elsewhere is untouched.
        if (!close_to(candidate.position[0], expected_x) ||
            !close_to(candidate.position[1], expected_y))
            return SaLcsBridgeResolveStatus::unexpected_mismatch;
        if (++*matches != 1) return SaLcsBridgeResolveStatus::unexpected_mismatch;
        *target = candidate;
    }
    if (lift_count > 1 || road_count > 1 || weight_count > 1)
        return SaLcsBridgeResolveStatus::unexpected_mismatch;
    // Source CBridge::Update waits for its lift and counterweight to stream in;
    // the road segment is optional and is moved only when present.
    if (!lift_count || !weight_count) return SaLcsBridgeResolveStatus::retry_streaming;
    return SaLcsBridgeResolveStatus::ready;
}

SaLcsBridgeResolveStatus resolve_bridge(BridgeEntities& result) {
    if (!bridge.configured) return SaLcsBridgeResolveStatus::unexpected_mismatch;
    return sa_lcs_bridge_resolve_owned_pool_status(0xB74498, bridge.model_ids, result);
}

bool set_path_links(bool blocked);

bool set_state_engine(int32_t requested) {
    __try {
        const auto transition = sa_lcs_bridge_state_request(bridge.current, requested);
        if (!transition.valid) return false;
        bridge.current = transition.next_state;
        return set_path_links(transition.block_path_links);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool set_path_links(bool blocked) {
    // CPathFind::SetLinksBridgeLights, SA 1.0 US. This applies the original
    // LCS bridge rectangle to any native SA car links loaded by its path service.
    reinterpret_cast<void(__thiscall*)(uintptr_t, float, float, float, float, bool)>(0x44D960)(
        0x96F050, -330.0f, -230.0f, -700.0f, -588.0f, blocked);
    bridge.path_state_sent = true;
    return true;
}

void move_entity(uintptr_t entity, float x, float y, float z) {
    // Rebuild SA's physical world-sector membership around the new transform.
    // CEntity::Remove/Add handle normal and LOD sectors from the entity flags;
    // UpdateRwMatrix/UpdateRwFrame keep RenderWare and collision on that transform.
    reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x534AE0)(entity);
    reinterpret_cast<void(__thiscall*)(uintptr_t, float, float, float)>(0x420B80)(entity, x, y, z);
    reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x446F90)(entity);
    reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x532B00)(entity);
    reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x533020)(entity);
}

bool apply_lift(float height, const BridgeEntities& entities) {
    if (height == bridge.previous_lift) return true;

    // All three addresses were resolved from active, correctly typed building
    // pool slots immediately before entering this function. Never retain them.
    move_entity(entities.lift.address, entities.lift.position[0], entities.lift.position[1],
                bridge.default_lift_z + height);
    if (entities.road.address) {
        move_entity(entities.road.address, entities.road.position[0], entities.road.position[1],
                    bridge.default_road_z + height);
    }
    move_entity(entities.weight.address, entities.weight.position[0], entities.weight.position[1],
                bridge.default_weight_z - height);
    bridge.previous_lift = height;
    return true;
}

bool initialize_defaults() {
    // Fixed base heights come from the same converted IPL records as the
    // x/y placement guard; recapturing a building after a DLL reload must not
    // turn its already moved position into a new baseline.
    if (!bridge.defaults_known) {
        bridge.default_lift_z = bridge_lift_z;
        bridge.default_road_z = bridge_road_z;
        bridge.default_weight_z = bridge_weight_z;
        bridge.defaults_known = true;
        bridge.previous_lift = -1.0f;
    }
    if (!bridge.path_state_sent && !set_path_links(
            bridge.current != state_down && bridge.current != state_always_unlocked)) return false;
    return true;
}

} // namespace

namespace {
bool dispatch_world_membership(uintptr_t entity,unsigned slot) {
    __try {
        if(!entity)return false;
        const auto table=*reinterpret_cast<const uintptr_t*>(entity);
        if(!table)return false;
        const auto target=*reinterpret_cast<const uintptr_t*>(table+slot*sizeof(uintptr_t));
        MEMORY_BASIC_INFORMATION page{};
        if(!target || !VirtualQuery(reinterpret_cast<const void*>(target),&page,sizeof(page)) ||
           page.State!=MEM_COMMIT || (page.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
           !(page.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
        reinterpret_cast<void(__thiscall*)(uintptr_t)>(target)(entity);
        return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}
bool sa_world_entity_add(uintptr_t entity){return dispatch_world_membership(entity,2);}
bool sa_world_entity_remove(uintptr_t entity){return dispatch_world_membership(entity,3);}

bool sa_lcs_bridge_configure(SaLcsBridgeModelIds ids) {
    if (!model_ids_valid(ids)) return false;
    if (bridge.configured) {
        return bridge.model_ids.lift == ids.lift && bridge.model_ids.weight == ids.weight &&
            bridge.model_ids.road == ids.road;
    }
    bridge.model_ids = ids;
    bridge.configured = true;
    return true;
}

bool sa_lcs_bridge_resolve_owned_pool(uintptr_t pool_root, SaLcsBridgeModelIds ids,
                                     SaLcsBridgeOwnedEntities& result) {
    return sa_lcs_bridge_resolve_owned_pool_status(pool_root, ids, result) ==
        SaLcsBridgeResolveStatus::ready;
}

SaLcsBridgeResolveStatus sa_lcs_bridge_resolve_owned_pool_status(
    uintptr_t pool_root, SaLcsBridgeModelIds ids, SaLcsBridgeOwnedEntities& result) {
    result = {};
    if (!pool_root || !model_ids_valid(ids)) return SaLcsBridgeResolveStatus::unexpected_mismatch;
    __try {
        const auto status = scan_pool(pool_root, ids, result);
        if (status != SaLcsBridgeResolveStatus::ready) result = {};
        return status;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        result = {};
        return SaLcsBridgeResolveStatus::unexpected_mismatch;
    }
}

SaLcsBridgeStateRequest sa_lcs_bridge_state_request(int32_t current_state, int32_t requested_state) {
    SaLcsBridgeStateRequest result{};
    switch (requested_state) {
    case 0:
        result.valid = true;
        result.next_state = state_locked;
        result.block_path_links = true;
        break;
    case 1:
        result.valid = true;
        result.next_state = state_always_unlocked;
        result.block_path_links = false;
        break;
    case 2:
        result.valid = true;
        result.next_state = (current_state == state_down || current_state == state_always_unlocked)
            ? state_about_to_move_up : state_moving_down;
        result.block_path_links = true;
        break;
    default:
        break;
    }
    return result;
}

SaLcsBridgeCycleSample sa_lcs_bridge_cycle_sample(int32_t current_state,
                                                  uint32_t operational_since,
                                                  uint32_t now) {
    SaLcsBridgeCycleSample sample{};
    sample.state = current_state;
    sample.operational_since = operational_since;
    if (current_state == state_locked) {
        sample.state = state_locked;
        sample.operational_since = 0;
        sample.lift_height = bridge_lift_height;
        return sample;
    }
    if (current_state == state_always_unlocked) {
        sample.operational_since = now - 20001u;
        sample.lift_height = 0.0f;
        return sample;
    }
    if (sample.operational_since == 0) sample.operational_since = now;
    const uint32_t elapsed = (now - sample.operational_since) % bridge_cycle_ms;
    if (elapsed < 20000u) {
        sample.state = state_moving_down;
        sample.lift_height = bridge_lift_height - (static_cast<float>(elapsed) / 20000.0f) * bridge_lift_height;
    } else if (elapsed < 80000u) {
        sample.state = state_down;
        sample.lift_height = 0.0f;
    } else if (elapsed < 90000u) {
        sample.state = state_about_to_move_up;
        sample.lift_height = 0.0f;
    } else if (elapsed < 110000u) {
        sample.state = state_moving_up;
        sample.lift_height = (static_cast<float>(elapsed - 90000u) / 20000.0f) * bridge_lift_height;
    } else {
        sample.state = state_lift_up;
        sample.lift_height = bridge_lift_height;
    }
    return sample;
}

bool sa_lcs_bridge_profile_matches() {
    if (!sa_executor_profile_matches()) return false;
    __try {
        return engine_profile_matches();
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

NativeResult sa_lcs_bridge_set_state(int32_t requested) {
    NativeResult result{};
    if (requested < 0 || requested > 2) {
        result.diagnostic = "LCS bridge state must be 0, 1, or 2";
        return result;
    }
    if (!bridge.configured || !sa_lcs_bridge_profile_matches()) {
        result.diagnostic = "LCS bridge model bindings or native engine profile unavailable";
        return result;
    }
    // This handler now owns state mutation and may call native path/entity APIs.
    // Once entered, failure must never replay the same command in the SA VM.
    result.native_entered = true;
    if (!set_state_engine(requested)) {
        result.diagnostic = "SA bridge path-state update faulted after native entry";
        return result;
    }
    result.executed = true;
    return result;
}

bool sa_lcs_bridge_tick() {
    if (!bridge.configured || !sa_lcs_bridge_profile_matches()) return false;
    __try {
        BridgeEntities entities{};
        const auto resolve_status = resolve_bridge(entities);
        if (resolve_status == SaLcsBridgeResolveStatus::retry_streaming) {
            // CBridge::Update waits while its lift/counterweight are streamed out.
            return true;
        }
        if (resolve_status != SaLcsBridgeResolveStatus::ready) return false;
        if (!initialize_defaults()) return false;

        bridge.previous = bridge.current;
        const uint32_t now = *reinterpret_cast<const uint32_t*>(0xB7CB84);
        const auto sample = sa_lcs_bridge_cycle_sample(bridge.current, bridge.operational_since, now);
        bridge.current = sample.state;
        bridge.operational_since = sample.operational_since;

        if (sample.lift_height != bridge.previous_lift && !apply_lift(sample.lift_height, entities)) return false;
        if (bridge.current == state_about_to_move_up && bridge.previous == state_down)
            return set_path_links(true);
        if (bridge.current == state_down && bridge.previous == state_moving_down)
            return set_path_links(false);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool sa_lcs_bridge_lights_flashing() {
    // CBridge::ShouldLightsBeFlashing: all source states except fully down.
    return bridge.configured && bridge.current != state_down;
}

void sa_lcs_bridge_cleanup() {
    if (!bridge.configured || !sa_lcs_bridge_profile_matches()) return;
    __try {
        if (bridge.defaults_known) {
            BridgeEntities entities{};
            if (resolve_bridge(entities) == SaLcsBridgeResolveStatus::ready) {
                bridge.previous_lift = -1.0f;
                apply_lift(bridge_lift_height, entities);
                bridge.previous_lift = bridge_lift_height;
            }
        }
        bridge.current = state_locked;
        bridge.previous = state_locked;
        bridge.operational_since = 0;
        bridge.previous_lift = -1.0f;
        set_path_links(true);
        bridge.path_state_sent = false;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        bridge.current = state_locked;
        bridge.previous = state_locked;
        bridge.operational_since = 0;
        bridge.previous_lift = -1.0f;
        bridge.path_state_sent = false;
    }
}

} // namespace lcs
