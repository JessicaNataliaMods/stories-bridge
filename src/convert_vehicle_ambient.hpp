#pragma once

#include <filesystem>

namespace rw { struct Clump; }

// Convert the LCS train hierarchy to SA's native CTrain descriptor, preserving
// the original body tiers and mapping the two door pivots and their distant
// atomics to SA's names and LOD tier. The original train COLL input is required
// for the common SA COL3 plugin.
void adapt_sa_train(rw::Clump* train, const std::filesystem::path& collision);
void validate_sa_train(const std::filesystem::path& train);
