#pragma once
#include "lcs/hud.hpp"
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
}
