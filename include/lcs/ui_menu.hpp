#pragma once
#include "ui_font.hpp"
#include <string_view>
namespace lcs::ui {
// The native menu still supplies position, scale, alignment, colour and wrap.
// Its line height is kept in pixels; the LCS font supplies glyphs and advances.
struct MenuFont {
    TextStyle text;
    float line_height=18.f,wrap_x=640.f;
    bool background=false;
    Rgba background_colour{0,0,0,255};
    int outline=0;
};
struct MenuText {
    std::vector<GlyphQuad> quads;
    bool has_box=false;
    TextRect box;
    Rgba box_colour;
    int lines=0;
};
float menu_text_width(const MenuFont&,std::string_view,bool spaces=true);
MenuText layout_menu_text(const MenuFont&,float x,float y,std::string_view,bool from_bottom=false);
}
