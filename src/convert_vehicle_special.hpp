#pragma once

#include <filesystem>

namespace rw { struct Clump; }

// Adapt the original LCS Dodo mesh for SA's native plane model descriptor.
// CPlane inherits CAutomobile and uses wheel_rf_dummy as its main wheel frame,
// so the established wheel extraction and chassis conversion remain valid.
void adapt_sa_dodo_plane(rw::Clump* dodo, const std::filesystem::path& wheel,
                         float wheel_scale, const std::filesystem::path& collision);
void validate_sa_dodo_plane(const std::filesystem::path& dodo);
