#pragma once

#include "sa_world_sector_engine.hpp"
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace lcs {

struct SaWorldSectorModelBinding {
    std::string asset;
    uint16_t model = 0;
};

// Transactional consumer for the 229 conditional LCS map layers. `set` only
// stages an original state; request/complete/commit are the source 1590-1592
// three-phase protocol. The engine owns all SA pool/SCM ABI details.
class SaWorldSectors {
    struct Layer;
    struct Plan {
        uint64_t world_epoch = 0;
        uint64_t state_revision = 0;
        uint8_t hour = 0;
        std::vector<bool> desired;
        std::vector<uint16_t> models;
    };

    std::filesystem::path directory_;
    // Own callback copies so a retired campaign can keep its world-pool
    // receipts alive after the active adapter has moved on.
    SaWorldSectorEngine engine_;
    std::vector<Layer> layers_;
    std::vector<bool> applied_;
    std::vector<std::optional<SaWorldSectorBuildingRef>> originals_;
    std::vector<std::optional<SaWorldSectorObjectRef>> overlays_;
    std::map<int32_t,int32_t> committed_states_, staged_states_;
    std::optional<Plan> plan_;
    std::set<uint16_t> requested_models_;
    uint64_t world_epoch_ = 0;
    uint64_t state_revision_ = 0;
    bool faulted_ = false;
    bool uncertain_ownership_ = false;
    bool uncertain_building_membership_ = false;
    bool unresolved_overlay_ownership_ = false;
    bool asynchronous_native_entry_ = false;
    std::string fault_diagnostic_;

    bool synchronize_epoch(std::string& diagnostic);
    bool rebuild_plan(uint8_t hour, bool& native_entered, std::string& diagnostic);
    std::optional<bool> check_plan_models() const;
    NativeResult rollback(const std::vector<std::pair<size_t,bool>>& building_ops,
                          const std::vector<std::pair<size_t,bool>>& object_ops,
                          bool entered, const std::string& cause);

public:
    SaWorldSectors(const std::filesystem::path& directory,
                   const std::vector<SaWorldSectorModelBinding>& world_bindings,
                   SaWorldSectorEngine& engine);
    ~SaWorldSectors();

    SaWorldSectors(const SaWorldSectors&) = delete;
    SaWorldSectors& operator=(const SaWorldSectors&) = delete;

    NativeResult set(int32_t group, int32_t state);
    NativeResult request();
    std::optional<bool> complete();
    NativeResult commit();
    NativeResult cleanup();

    bool faulted() const noexcept { return faulted_; }
    const std::string& diagnostic() const noexcept { return fault_diagnostic_; }
};

} // namespace lcs
