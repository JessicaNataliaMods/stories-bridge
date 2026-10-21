#pragma once

#include <filesystem>

namespace rw { struct Clump; }

// LCS and SA use the same named component contract for standard bikes. These
// adapters retain every source frame/atomic and apply only the shared SA
// material and embedded-collision contract.
void adapt_sa_bike(rw::Clump* bike, const std::filesystem::path& collision);
void validate_sa_bike(const std::filesystem::path& bike);

// Generic LCS boats already use SA's boat_hi/boat_vlo and seat names. The
// ferry remains on its dedicated adapter because its hierarchy is different.
void adapt_sa_generic_boat(rw::Clump* boat, const std::filesystem::path& collision);
void validate_sa_generic_boat(const std::filesystem::path& boat);
