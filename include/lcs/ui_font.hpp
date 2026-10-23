#pragma once
#include <array>
#include <cstdint>
#include <string_view>
#include <vector>
namespace lcs::ui {
// Pure LCS front-end font logic (reStories CFont): glyph metrics, text tokens and
// quad generation. It owns no textures and calls no engine function, so the
// native layer only has to submit the produced quads.
struct Rgba {
    uint8_t r=255,g=255,b=255,a=255;
    bool operator==(const Rgba& o)const{return r==o.r && g==o.g && b==o.b && a==o.a;}
};
// Page order follows the original texture slots: Sprite[0]=font2 (bank),
// Sprite[1]=font1 (standard), Sprite[2]=font3 (heading).
enum class FontStyle:int{bank=0,standard=1,heading=2};
struct GlyphQuad {
    // Corner order matches the original AddToBuffer: top-left, top-right,
    // bottom-left, bottom-right (screen y grows downward).
    std::array<float,4> x{},y{},u{},v{};
    Rgba colour;
    int page=0;
};
struct TextStyle {
    FontStyle style=FontStyle::bank;
    float scale_x=1.f,scale_y=1.f;
    Rgba colour;
    bool proportional=true;
    bool right_justify=false,centre=false;
    float centre_size=0.f;       // line width used by centre alignment
    int drop_shadow=0;           // pixels (already screen scaled by the caller)
    Rgba drop_colour{0,0,0,255};
    float slant=0.f,slant_ref_x=0.f,slant_ref_y=0.f;
    bool half_texture=false;      // FontStyle::heading also enables the reduced glyph atlas automatically.
    float right_wrap=0.f;        // right-justified lines wrap at xstart-right_wrap
};
inline constexpr int font_style_count=3;
// c is a glyph index (character - ' ').
float glyph_advance(const TextStyle&,int c,bool force_proportional=false);
// Width of the printable run, stopping at the first space unless spaces=true.
float string_width(const TextStyle&,std::string_view text,bool spaces=true);
// Glyph index remapping used by fonts that only carry the reduced character set.
int new_character(int c);
// Emits the quads for one line, including drop shadow. x is the left edge unless
// right_justify (right edge) or centre (centre of the line) is set.
void print_line(const TextStyle&,float x,float y,std::string_view text,std::vector<GlyphQuad>& out);
// Screen metrics for the PSP-derived layout used by the original HUD code. The
// PSP canvas is 480x272; one unit is height/272 pixels on both axes, which is
// width/480 on a 16:9 screen and keeps proportions on wider or narrower ones.
// Elements are anchored to the edge they sit on in the original.
struct Screen {
    float width=640.f,height=448.f;
    float unit()const{return height/272.f;}
    float psp_x(float v)const{return v*unit();}
    float psp_y(float v)const{return v*unit();}
    float from_right(float v)const{return width-psp_x(v);}
    // A coordinate measured from the left of the 480-wide canvas, anchored to the right edge.
    float right_anchored_x(float x)const{return width-(480.f-x)*unit();}
    bool wide()const{return width/height>=1.5f;}
    float psp_from_bottom(float v)const{return height-psp_y(v);}
    // PS2-canvas helpers (640x448 design, aspect corrected: one unit is height/448).
    float ps2(float v)const{return v*height/448.f;}
    float ps2_from_right(float v)const{return width-ps2(v);}
    float ps2_from_bottom(float v)const{return height-ps2(v);}
};
// Word-wrapped text as CFont::PrintString lays it out. wrap_x is the right limit of left-aligned
// text; centred text wraps at centre_size and right-justified text at xstart-right_wrap.
int count_lines(const TextStyle&,float xstart,std::string_view text,float wrap_x);
void print_block(const TextStyle&,float x,float y,std::string_view text,float wrap_x,std::vector<GlyphQuad>& out);
// PrintStringFromBottom: y is the bottom of the block.
void print_block_from_bottom(const TextStyle&,float x,float y,std::string_view text,float wrap_x,std::vector<GlyphQuad>& out);
struct TextRect {float left=0,top=0,right=0,bottom=0;};
// CFont::GetTextRect, the box drawn behind text when a background is enabled.
TextRect text_rect(const TextStyle&,float xstart,float ystart,std::string_view text,float wrap_x);
}
