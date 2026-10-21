#include "sa_city_selection.hpp"

namespace lcs {

std::optional<uint8_t> city_track_for_retail_area(int32_t source_area) noexcept {
    switch (source_area) {
    case 5: return 17;
    case 17: return 18;
    default: return {};
    }
}

CityAreaMusicSelection observe_retail_area_music_selection(
    NativeCityMusic& music, int32_t source_area) {
    const auto track = city_track_for_retail_area(source_area);
    if (!track) {
        return music.request_native_selection()
            ? CityAreaMusicSelection::sa_native
            : CityAreaMusicSelection::controller_refused;
    }
    if (!music.request(*track))return CityAreaMusicSelection::controller_refused;
    return *track == 17
        ? CityAreaMusicSelection::city_track_17_pending
        : CityAreaMusicSelection::city_track_18_pending;
}

} // namespace lcs
