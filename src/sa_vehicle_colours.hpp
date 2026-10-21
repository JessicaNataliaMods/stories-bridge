#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace lcs {

// Relocate SA's palette before CGameInit/LoadVehicleColours so save and
// native startup paths can safely hold extended byte indices.
void sa_prepare_vehicle_colours(const std::filesystem::path& metadata_path);

// Install the campaign's exact vehicle palette and per-model colour choices
// after vehicle model infos load. Returns LCS palette index -> runtime SA
// palette index. Throws std::runtime_error on any mismatch.
std::vector<int32_t> sa_load_vehicle_colours(const std::filesystem::path& metadata_path);

}
