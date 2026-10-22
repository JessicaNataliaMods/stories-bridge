#pragma once

#include <filesystem>

namespace rw { struct Clump; }

// Adapt the original LCS Dodo mesh for SA's native plane model descriptor.
// CPlane inherits CAutomobile and uses wheel_rf_dummy as its main wheel frame,
// so the established wheel extraction and chassis conversion remain valid.
void adapt_sa_dodo_plane(rw::Clump* dodo, const std::filesystem::path& wheel,
                         float wheel_scale, const std::filesystem::path& collision);
void validate_sa_dodo_plane(const std::filesystem::path& dodo);

// Adapt the original LCS Airtrain or Dead Dodo to SA's native CPlane. Their
// front wheel is singular and their rear atomic contains two wheels; preserve
// both source wheel meshes as static geometry and derive four empty native SA
// suspension dummies from the original pivots and rear wheel vertex clusters.
// LCS planes use a fixed wheel size of 1.0 in the source loader.
void adapt_sa_ambient_plane(rw::Clump* plane, const std::filesystem::path& collision,
                            float wheel_size);
void validate_sa_ambient_plane(const std::filesystem::path& plane);
