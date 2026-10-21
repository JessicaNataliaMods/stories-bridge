#pragma once
#include "lcs/native.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace lcs {
// Physical consumers of SA garages. Configure each exact converted IPL door
// placement once; repeated calls add bindings and are idempotent. A garage
// callback selects a binding only when that placement is unique inside Retail's
// 20-unit door search window. The caller explicitly identifies building vs
// object pool; each entity is resolved by exact model and placement.
enum class SaGarageDoorResolve {ready,streamed_out,unavailable};
enum class SaGarageDoorEntityPool {building,object};

// Read-only evidence for a placement already registered with the door backend.
// Pool counts cover only exact model/placement matches and include entities at
// any point in the native door's vertical travel.
struct SaGarageDoorRuntimeStatus {
    uint16_t model=0;
    std::array<float,3> placement{};
    uint32_t buildings=0;
    uint32_t dummies=0;
    uint32_t objects=0;
    bool building_pool_available=false;
    bool dummy_pool_available=false;
    bool object_pool_available=false;
    bool model_info_available=false;
    int16_t object_info_index=-1;
    uint16_t model_flags=0;
    bool collision_model_available=false;
    bool collision_ready=false;
    bool garage_door_model=false;
    // -128 means no matching CObject; -2 means multiple. -1 is SA's native
    // unbound value for a single matching CObject.
    int8_t native_garage_index=-128;
    // Read-only snapshot from one uniquely matched CObject. Availability bits
    // distinguish a real zero/null from a field that could not be sampled.
    bool object_sample_available=false;
    bool object_z_available=false;
    float object_z=0.0f;
    bool has_dummy_known=false;
    bool has_dummy=false;
    bool dummy_z_available=false;
    float dummy_z=0.0f;
    bool control_code_known=false;
    bool control_code_linked=false;
    bool native_garage_state_available=false;
    uint8_t native_garage_state=0;
    float native_garage_progress=0.0f;
    uint8_t native_garage_flags=0;
};

bool sa_garage_door_configure(uint16_t model,const std::array<float,3>& placement,
    SaGarageDoorEntityPool pool=SaGarageDoorEntityPool::building);
// Reports only exact placements previously passed to sa_garage_door_configure.
// It never mutates entities or streams models.
std::vector<SaGarageDoorRuntimeStatus> sa_garage_door_runtime_statuses();
SaGarageDoorResolve sa_garage_door_resolve(uintptr_t pool_root,uint16_t model,
    const std::array<float,3>& placement,uintptr_t& entity,
    SaGarageDoorEntityPool pool=SaGarageDoorEntityPool::building);
// Retail CGarages::FindDoorHeightForMI over the live converted collision model.
// nullopt: unavailable or streamed out (a benign pause, never a guessed height).
std::optional<float> sa_garage_door_height(const std::array<float,8>& garage_bounds);
// Retail CGarage::UpdateDoorsHeight for the non-rotated door: placement Z + position.
NativeResult sa_garage_door_move(const std::array<float,8>& garage_bounds,float position);
// Restores the approved IPL pose of a door this module moved.
bool sa_garage_door_cleanup();

// SA-native persistence of the import list: CGarages::CarTypesCollected[4] at
// 0x96BFEC. SA's own CGarages::Init/Save/Load are its only users, so the saved
// game slot carries the bits. nullopt/false when the profile does not match.
std::optional<uint32_t> sa_garage_collected_read(uint32_t index);
bool sa_garage_collected_write(uint32_t index,uint32_t mask);

#ifdef LCS_GARAGE_DOOR_TEST_BACKEND
void sa_garage_door_test_collected(uintptr_t address);
void sa_garage_door_test_model_info(uintptr_t address);
void sa_garage_door_test_pool_root(uintptr_t address);
void sa_garage_door_test_object_pool_root(uintptr_t address);
void sa_garage_door_test_dummy_pool_root(uintptr_t address);
void sa_garage_door_test_native_garages(uintptr_t address);
#endif
}
