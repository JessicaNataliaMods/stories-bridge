#pragma once

#include <filesystem>

namespace rw { struct Clump; }

// Rebind the original LCS helicopter body and rotors to SA's native CHeli
// descriptor. Source geometry and frame transforms remain attached unchanged.
// When no native suspension dummies exist, four empty SA dummies are derived
// from the root-space chassis_hi envelope. This is an explicit virtual
// suspension adaptation; sparse LCS skid fragments are retained but not
// treated as wheel/contact points. wheel_size comes from the converted IDE.
void adapt_sa_heli(rw::Clump* heli, const std::filesystem::path& collision, float wheel_size);
void validate_sa_heli(const std::filesystem::path& heli);
