#pragma once
#include "lcs/ui_font.hpp"
#include <string>
#include <vector>
namespace lcs::ui {
// Textured rectangle from hud.txd; (u0,v0)-(u1,v1) is the sub-rectangle sampled.
struct SpriteCommand {
    std::string texture;   // hud.txd name; empty when `weapon` selects the weapon icon
    float left=0,top=0,right=0,bottom=0;
    float u0=0,v0=0,u1=1,v1=1;
    Rgba colour;
    int weapon=-1;         // SA weapon type whose LCS icon is sampled, or -1
};
// Inputs for the player-information block of the original CHud::Draw. The caller
// reads them from the running game; this module decides what is drawn where.
struct PlayerHudInputs {
    float health=100.f,max_health=100.f;
    float armour=0.f,max_armour=100.f;
    int32_t money=0;
    int hour=0,minute=0;
    uint32_t frame=0;      // native frame counter (FRAMECOUNTER)
    // Weapon in hand (SA weapon type, clip and total ammunition) and the weapon
    // table's clip size; wanted level, the level kept for parole and the times used
    // by the star flash.
    int weapon_type=0,ammo_clip=0,ammo_total=0,clip_size=0;
    int wanted=0,parole=0;
    uint32_t now_ms=0,wanted_changed_ms=0;
    Screen screen;
    uint8_t alpha=255;
};
// Health and armour bars, the money counter and the clock (Hud.cpp DrawHealthBar,
// DrawArmourBar, DrawCash, DrawTime), PSP layout over the PS2 textures.
std::vector<SpriteCommand> layout_player_info(const PlayerHudInputs&);
std::vector<SpriteCommand> layout_breath_bar(const Screen&,float fraction,uint8_t alpha=255);
struct TextCommand {
    std::string text;
    TextStyle style;
    float x=0,y=0;
};
struct CombatLayout {
    std::vector<SpriteCommand> sprites;
    std::vector<TextCommand> texts;
};
// Hud.cpp DrawWeaponIcon/DrawAmmo and the wanted stars (DrawWanted).
CombatLayout layout_weapon_and_wanted(const PlayerHudInputs&);
// LCS radar geometry (Radar.h FIX_RADAR/Hud.cpp, PS2 canvas): the map is an 82x82 box 40 units from the left
// and 40 from the bottom; the disc frame grows 6 units around it and carries a black copy 2 units lower.
struct RadarLayout {
    float left=0,top=0,width=0,height=0;                       // map box in pixels
    float disc_left=0,disc_top=0,disc_right=0,disc_bottom=0;   // disc frame
    float shadow_offset=0;                                     // vertical offset of the black copy
    // Radar space (-1..1, y up) to screen pixels (Radar.cpp TransformRadarPointToScreenSpace).
    void to_screen(float rx,float ry,float& sx,float& sy)const{sx=(rx+1.f)*0.5f*width+left;sy=(1.f-ry)*0.5f*height+top;}
};
RadarLayout layout_radar(const Screen&);
// DrawTimeAndCashNumbers: draws str with the hudnumbers atlas starting at x (PSP
// canvas units from the left of a 480-wide screen) and y (from the top).
void layout_numbers(const Screen&,const std::string& text,float x,float y,bool second_set,uint8_t alpha,std::vector<SpriteCommand>& out);
}
