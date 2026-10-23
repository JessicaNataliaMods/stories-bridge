#pragma once
#include "lcs/hud.hpp"
#include <filesystem>
#include <string>
namespace lcs {
bool sa_hud_update(const HudRequest&);
std::optional<int32_t> sa_hud_read(uint32_t);
bool sa_hud_write(uint32_t,int32_t);
bool sa_hud_render_profile();
void sa_hud_render_bound(bool);
bool sa_hud_render_ready();
bool sa_hud_presentation_healthy();
void sa_hud_draw_owned(void(__cdecl* original)());
bool sa_hud_present(const HudRequest&);
// Draws the owned mission counters with the original LCS fonts when the UI layer is ready.
void sa_hud_use_lcs_ui(const std::filesystem::path& directory,void(*log)(const std::string&),bool hud_enabled=true);
// Shared lazy load for frontend text before the first gameplay HUD frame.
bool sa_hud_lcs_ui_active();
bool sa_hud_draw_lcs_breath(int player);
}
