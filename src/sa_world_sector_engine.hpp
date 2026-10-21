#pragma once
#include "lcs/native.hpp"
#include <array>
#include <functional>
#include <filesystem>
#include <optional>
#include <vector>

namespace lcs {
struct SaWorldSectorTransform {
    std::array<float,3> position{};
    std::array<float,4> quaternion{{0,0,0,1}};
};
struct SaWorldSectorBuildingRef {
    uintptr_t address=0;
    uint32_t slot=0;
    uint8_t pool_generation=0;
    uint16_t model=0;
    uint32_t flags=0;
    SaWorldSectorTransform transform;
    uint64_t world_epoch=0;
};
struct SaWorldSectorObjectRef {
    int32_t script_handle=-1;
    uintptr_t address=0;
    uint32_t slot=0;
    uint8_t pool_generation=0;
    uint16_t model=0;
    SaWorldSectorTransform transform;
    uint64_t world_epoch=0;
};
struct SaWorldSectorObjectReceipt {NativeResult result;std::optional<SaWorldSectorObjectRef> object;};
// Operations used exclusively by the source story-overlay transaction. Production
// binds these to the same SA pools, script executor and CWorld API as the map.
struct SaWorldSectorEngine {
    std::function<uint64_t()> epoch;
    std::function<std::optional<uint8_t>()> hour;
    std::function<NativeResult(uint16_t)> request_model;
    std::function<std::optional<bool>(uint16_t)> model_ready;
    std::function<std::optional<std::vector<SaWorldSectorBuildingRef>>(uint16_t,const SaWorldSectorTransform&)> find_buildings;
    std::function<bool(const SaWorldSectorBuildingRef&)> building_live;
    std::function<NativeResult(const SaWorldSectorBuildingRef&)> remove_building,restore_building;
    std::function<SaWorldSectorObjectReceipt(uint16_t,const SaWorldSectorTransform&)> prepare_object;
    std::function<bool(const SaWorldSectorObjectRef&)> object_live;
    std::function<NativeResult(const SaWorldSectorObjectRef&,bool)> set_object_active;
    std::function<NativeResult(const SaWorldSectorObjectRef&)> destroy_object;
};
SaWorldSectorEngine make_sa_world_sector_engine(const std::filesystem::path& world_directory);
// Call immediately before native CWorld::ShutDown so retained pool receipts
// from the retiring world can no longer be used against a rebuilt SA world.
void sa_world_sector_world_retiring() noexcept;
}
