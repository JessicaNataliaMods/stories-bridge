#pragma once

#include "sa_city_music.hpp"
#include <cstdint>
#include <optional>

namespace lcs {

enum class CityAreaMusicSelection : uint8_t {
    sa_native,
    city_track_17_pending,
    city_track_18_pending,
    controller_refused
};

// Retail MusicManager's area switch reads the source area at 0x3D8430:
// index 5 writes desired track17 and index17 writes desired track18.
// Every other area uses a non-CITY retail track and remains with SA audio.
std::optional<uint8_t> city_track_for_retail_area(int32_t source_area) noexcept;

// Call only after the source area's native setter succeeds. CITY selections
// become real pending requests on NativeCityMusic; the next source music
// service performs the owned transition. Other areas queue retirement of any
// owned CITY stream and return control to SA's native music path.
CityAreaMusicSelection observe_retail_area_music_selection(
    NativeCityMusic& music, int32_t source_area);

} // namespace lcs
