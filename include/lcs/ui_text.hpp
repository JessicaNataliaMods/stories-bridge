#pragma once
#include "lcs/ui_font.hpp"
#include <string>
#include <vector>
namespace lcs::ui {
// On-screen texts the LCS HUD draws with its own font and layout. SA decides when a
// text is shown and how it fades; the layout (position, size, colour, shadow, box)
// is the original one.
enum class TextElement {
    zone_name,      // area name, bottom right
    vehicle_name,   // vehicle name, bottom right above the zone
    subtitle,       // mission/cutscene subtitles
    help,           // help message box
    big_message_0,  // mission complete/failed text, screen centre
    big_message_1,  // mission title, bottom right
    big_message_2,  // wasted/busted, bottom right
    big_message_3,  // odd job heading
    big_message_4,  // odd job text
    big_message_5,  // odd job result
};
struct TextRequest {
    TextElement element=TextElement::subtitle;
    std::string text;
    uint8_t alpha=255;        // the fade SA applied to its own print
    bool widescreen_cutscene=false;
    Screen screen;
};
struct TextLayout {
    std::vector<GlyphQuad> quads;
    bool has_box=false;
    TextRect box;
    Rgba box_colour;
};
TextLayout layout_text(const TextRequest&);
}
