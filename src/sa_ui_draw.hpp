#pragma once
#include "lcs/ui_font.hpp"
#include <filesystem>
#include <string>
#include <vector>
namespace lcs {
// Native 2D layer for the LCS front-end: loads the original LCS fonts/hud TXDs
// through RenderWare and submits textured quads. Everything fails closed: when a
// read, lookup or draw faults the layer reports not ready and callers keep the SA HUD.
bool sa_ui_load(const std::filesystem::path& directory,std::string& diagnostic);
bool sa_ui_ready();
// Fonts: page indexes follow ui::FontStyle (0=font2, 1=font1, 2=font3).
bool sa_ui_draw_glyphs(const std::vector<ui::GlyphQuad>&);
// Untextured rectangle in screen pixels (top-left origin).
bool sa_ui_draw_rect(float left,float top,float right,float bottom,ui::Rgba colour);
// Sprite from hud.txd drawn over [left,right]x[top,bottom] sampling the (u0,v0)-(u1,v1) sub-rectangle.
bool sa_ui_draw_sprite(const char* name,float left,float top,float right,float bottom,ui::Rgba colour,float u0=0.f,float v0=0.f,float u1=1.f,float v1=1.f);
// Weapon icon for an SA weapon type: the LCS icon texture stored in the weapon model's own
// TXD (weapons/<model>.txd next to the ASI). False when the type has no icon or it cannot be read.
bool sa_ui_draw_weapon_icon(int sa_weapon_type,float left,float top,float right,float bottom,ui::Rgba colour);
// Writes the LCS radar icons (hud.txd) into SA's CRadar::RadarBlipSprites table, keeping SA's
// radar logic. Allocation is shared with the adapter in lcs/radar_sprites.hpp;
// SA's built-in player, north and waypoint slots retain their identities.
bool sa_ui_apply_radar_sprites();
bool sa_ui_screen(float& width,float& height);
// Draws the LCS sniper scope / camera viewfinder when SA is in that mode; false leaves SA's crosshair.
bool sa_ui_draw_scope();
}
