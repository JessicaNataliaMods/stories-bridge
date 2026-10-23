#pragma once
#include <string>
#include <optional>
#include "lcs/radar_sprites.hpp"
namespace lcs {
// LCS radar over SA's own radar code. SA keeps the map tiles, mask, zoom and blip logic; the
// ASI moves the radar to the LCS box (CRadar::TransformRadarPointToScreenSpace, whose original
// is kept in a trampoline for the menu map), replaces the four SA disc draws with the LCS disc
// and swaps SA's blip textures for the LCS ones (see sa_ui_apply_radar_sprites).
bool sa_radar_install(std::string& diagnostic);
// Called every frame before SA draws its radar. Inactive: SA's geometry and disc are untouched.
void sa_radar_frame(bool active,float screen_width,float screen_height);
// Generation-checked coordinate/contact blips, used only to restore original
// script sprite identities after LOAD. Never creates a blip or edits a save.
std::optional<RadarSpriteBlip> sa_radar_sprite_blip(int32_t handle);
bool sa_radar_set_sprite_blip(int32_t handle,int32_t sprite);
}
// Read by the naked disc stubs in asi.cpp and drawn from them.
extern "C" volatile uint8_t lcs_radar_skip;
extern "C" void __cdecl lcs_radar_disc_draw();
