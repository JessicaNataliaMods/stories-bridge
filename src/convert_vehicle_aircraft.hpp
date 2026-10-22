#pragma once

#include <filesystem>

namespace rw { struct Clump; }

// Rebind the original LCS helicopter body and rotors to SA's native CHeli
// descriptor. Source geometry and frame transforms remain attached unchanged.
// wheel_size comes from the converted IDE vehicle record and is required when
// source skid bounds must supply the four native suspension anchor dummies.
void adapt_sa_heli(rw::Clump* heli, const std::filesystem::path& collision, float wheel_size);
void validate_sa_heli(const std::filesystem::path& heli);
