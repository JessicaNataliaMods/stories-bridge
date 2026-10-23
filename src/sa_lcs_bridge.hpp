#pragma once

#include <cstdint>
#include "lcs/native.hpp"

namespace lcs {

// CPhysical overrides these methods to own repeat-sector membership links.
// Dispatch through Add() (slot 2), not Add(CRect&) (slot 1), and Remove (3).
bool sa_world_entity_add(uintptr_t entity);
bool sa_world_entity_remove(uintptr_t entity);

struct SaLcsBridgeModelIds {
    uint16_t lift = 0;
    uint16_t weight = 0;
    uint16_t road = 0;
};

struct SaLcsBridgeEntityRef {
    uintptr_t address = 0;
    uint32_t pool_slot = 0;
    float position[3]{};
};

struct SaLcsBridgeOwnedEntities {
    SaLcsBridgeEntityRef lift;
    SaLcsBridgeEntityRef road;
    SaLcsBridgeEntityRef weight;
};

enum class SaLcsBridgeResolveStatus : uint8_t {
    ready,
    // Retail CBridge::Update waits for a missing lift/counterweight; its road
    // segment is optional and is updated only when present.
    retry_streaming,
    // Bad pool layout, wrong entity type/placement, or duplicate ownership.
    unexpected_mismatch
};

struct SaLcsBridgeStateRequest {
    bool valid = false;
    int32_t next_state = 0;
    bool block_path_links = true;
};

struct SaLcsBridgeCycleSample {
    int32_t state = 0;
    uint32_t operational_since = 0;
    float lift_height = 25.0f;
};

// Bind the model IDs resolved from the mounted campaign model-bindings.tsv.
// Repeating the same configuration is harmless; changing it after setup fails.
bool sa_lcs_bridge_configure(SaLcsBridgeModelIds ids);

// The resolver used by production and by focused offline pool-layout tests.
// pool_root is the address of the global pointer to a SA CPool<CBuilding>.
bool sa_lcs_bridge_resolve_owned_pool(uintptr_t pool_root, SaLcsBridgeModelIds ids,
                                     SaLcsBridgeOwnedEntities& result);
SaLcsBridgeResolveStatus sa_lcs_bridge_resolve_owned_pool_status(
    uintptr_t pool_root, SaLcsBridgeModelIds ids, SaLcsBridgeOwnedEntities& result);

// POD state seams shared by production and the focused offline tests.
SaLcsBridgeStateRequest sa_lcs_bridge_state_request(int32_t current_state, int32_t requested_state);
SaLcsBridgeCycleSample sa_lcs_bridge_cycle_sample(int32_t current_state,
                                                  uint32_t operational_since,
                                                  uint32_t now);

bool sa_lcs_bridge_profile_matches();

// Backend for the LCS lift bridge placed in the converted Liberty City world.
// These functions only touch the three bridge buildings owned by that map.
NativeResult sa_lcs_bridge_set_state(int32_t state);
bool sa_lcs_bridge_tick();
bool sa_lcs_bridge_lights_flashing();
void sa_lcs_bridge_cleanup();

}
