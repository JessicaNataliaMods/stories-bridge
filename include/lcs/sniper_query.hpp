#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace lcs {

// The native SA script equivalent is a NOP. These are the two verified
// producer call sites that expose the current weapon-update sniper segment.
const uintptr_t* sa_sniper_query_call_sites(size_t* count) noexcept;
uintptr_t sa_sniper_query_callback_for_site(uintptr_t address) noexcept;
bool sa_sniper_query_bind_callsite(uintptr_t address, uintptr_t previous_target) noexcept;
bool sa_sniper_query_profile_matches() noexcept;
bool sa_sniper_query_calls_healthy() noexcept;

// The first hook runs at the start of CWeapon::UpdateWeapons; the second
// captures the exact sniper line endpoints before the chained line test.
void sa_sniper_query_begin_frame() noexcept;
void sa_sniper_query_capture_segment(const float* start, const float* end) noexcept;

// Bounds are {minX,minY,minZ,maxX,maxY,maxZ}. nullopt means the producer or
// captured segment is not trustworthy; false is reserved for a valid frame
// with no sniper bullet or a valid non-intersecting result.
std::optional<bool> sa_sniper_bullet_in_area(const std::array<float,6>& bounds) noexcept;

} // namespace lcs
