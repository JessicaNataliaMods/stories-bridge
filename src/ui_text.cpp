#include "lcs/ui_text.hpp"
#include <algorithm>
namespace lcs::ui {
namespace {
// Hud.cpp colour constants.
constexpr Rgba bigmessage{255,150,225,255},wastedbusted{0,207,133,255},oddjob{0,207,133,255},oddjob2{97,194,247,255},
    missiontitle{220,172,2,255},white{255,255,255,255};
Rgba with_alpha(Rgba c,uint8_t a){c.a=a;return c;}
int shadow(const Screen& s){return std::max(1,int(2.f*s.height/448.f+0.5f));}
TextStyle base(FontStyle font,float scale_x,float scale_y,Rgba colour,uint8_t alpha){
    TextStyle t;t.style=font;t.scale_x=scale_x;t.scale_y=scale_y;t.colour=with_alpha(colour,alpha);t.proportional=true;
    return t;
}
}
TextLayout layout_text(const TextRequest& r){
    TextLayout out;
    const Screen& s=r.screen;
    const float W=s.width,H=s.height;
    TextStyle t;float x=0,y=0,wrap=W;bool from_bottom=false;
    switch(r.element){
    case TextElement::zone_name:
    case TextElement::vehicle_name:{
        // DrawZoneName / DrawVehicleName: bank font, right aligned, printed up from the bottom.
        const bool wide=s.wide();
        t=base(FontStyle::bank,s.psp_x(wide?0.42768f:0.4752f),s.psp_y(0.88f),white,r.alpha);
        t.right_justify=true;t.drop_shadow=shadow(s);t.drop_colour=Rgba{0,0,0,r.alpha};
        x=s.from_right(24.f);
        y=s.psp_from_bottom(r.element==TextElement::zone_name?16.f:35.6f);
        from_bottom=true;
        break;
    }
    case TextElement::subtitle:{
        t=base(FontStyle::standard,s.ps2(0.58f),s.ps2(r.widescreen_cutscene?1.2f:1.22f),Rgba{225,225,225,255},255);
        t.colour.a=r.alpha;t.centre=true;
        if(r.widescreen_cutscene){
            t.centre_size=W-s.ps2(60.f);x=W/2.f;y=s.ps2_from_bottom(80.f);
        }else{
            const float bulge=s.ps2(140.f)+s.ps2(8.f);
            const float width=W-s.ps2(20.f)-s.ps2(8.f)-bulge;
            t.centre_size=width;x=width/2.f+bulge;y=s.ps2_from_bottom(105.f+2.f);
        }
        break;
    }
    case TextElement::help:{
        // Pager-style box at the top left: grey text on a black box that fades with it.
        t=base(FontStyle::standard,s.ps2(0.52f),s.ps2(1.1f),Rgba{175,175,175,255},r.alpha);
        wrap=s.ps2(200.f+34.f-4.f);x=s.ps2(34.f);y=s.ps2(28.f);
        out.has_box=true;out.box=text_rect(t,x,y,r.text,wrap);
        out.box_colour=Rgba{0,0,0,uint8_t(float(r.alpha)*0.9f)};
        break;
    }
    case TextElement::big_message_0:
        t=base(FontStyle::heading,s.ps2(1.8f),s.ps2(1.8f),bigmessage,r.alpha);
        t.centre=true;t.centre_size=s.ps2(590.f);t.drop_shadow=shadow(s);t.drop_colour=Rgba{0,0,0,r.alpha};
        x=W/2.f;y=H/2.f-s.ps2(18.f);
        break;
    case TextElement::big_message_1:
        t=base(FontStyle::bank,s.ps2(1.7f),s.ps2(1.8f),missiontitle,r.alpha);
        t.right_justify=true;t.drop_shadow=shadow(s);t.drop_colour=Rgba{0,0,0,r.alpha};
        x=s.ps2_from_right(20.f);y=s.ps2_from_bottom(140.f);
        break;
    case TextElement::big_message_2:
        t=base(FontStyle::heading,s.ps2(2.0f),s.ps2(2.0f),wastedbusted,r.alpha);
        t.right_justify=true;t.drop_shadow=shadow(s);t.drop_colour=Rgba{0,0,0,r.alpha};
        x=s.ps2_from_right(20.f);y=s.ps2_from_bottom(90.f);
        break;
    case TextElement::big_message_3:
    case TextElement::big_message_4:
        t=base(FontStyle::standard,s.ps2(1.2f),s.ps2(1.5f),oddjob,r.alpha);
        t.centre=true;t.centre_size=s.ps2(r.element==TextElement::big_message_3?600.f:580.f);
        t.drop_shadow=shadow(s);t.drop_colour=Rgba{0,0,0,r.alpha};
        x=W/2.f;y=r.element==TextElement::big_message_3?s.ps2(140.f)-s.ps2(16.f):s.ps2(140.f);
        break;
    case TextElement::big_message_5:
        t=base(FontStyle::standard,s.ps2(1.0f),s.ps2(1.2f),oddjob2,r.alpha);
        t.centre=true;t.centre_size=s.ps2(560.f);t.drop_shadow=shadow(s);t.drop_colour=Rgba{0,0,0,r.alpha};
        x=W/2.f;y=s.ps2(217.f);
        break;
    }
    if(from_bottom)print_block_from_bottom(t,x,y,r.text,wrap,out.quads);
    else print_block(t,x,y,r.text,wrap,out.quads);
    return out;
}
}
