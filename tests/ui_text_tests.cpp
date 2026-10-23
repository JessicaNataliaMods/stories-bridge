#include "lcs/ui_text.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace lcs::ui;
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
bool near(float a,float b,float tol=0.01f){return std::fabs(a-b)<tol;}
float min_x(const TextLayout& l){float m=1e9f;for(const auto& q:l.quads)m=std::min(m,q.x[0]);return m;}
float max_x(const TextLayout& l){float m=-1e9f;for(const auto& q:l.quads)m=std::max(m,q.x[1]);return m;}
float min_y(const TextLayout& l){float m=1e9f;for(const auto& q:l.quads)m=std::min(m,q.y[0]);return m;}
float max_y(const TextLayout& l){float m=-1e9f;for(const auto& q:l.quads)m=std::max(m,q.y[2]);return m;}
int main(){try{
    TextRequest r;r.screen.width=1920.f;r.screen.height=1080.f;r.text="DOWNTOWN";
    // Zone name: right aligned 24 PSP units from the right, bank font page 0, white with a black shadow.
    r.element=TextElement::zone_name;
    auto zone=layout_text(r);
    check(!zone.quads.empty() && zone.quads.front().page==0,"Zone names use the bank font page");
    check(zone.quads.front().colour.a==255 && zone.quads.front().colour.r==0 && zone.quads.back().colour.r==255,"The shadow comes first, then the white text");
    float right_edge=0;for(const auto& q:zone.quads)if(q.colour.r==255)right_edge=std::max(right_edge,q.x[1]);
    check(near(right_edge,r.screen.from_right(24.f),r.screen.unit()*20.f),"Zone text ends at 24 PSP units from the right");
    check(max_y(zone)<=r.screen.height && min_y(zone)>0,"Zone text stays on screen");
    // Vehicle name sits higher than the zone name.
    r.element=TextElement::vehicle_name;auto vehicle=layout_text(r);
    check(max_y(vehicle)<max_y(zone),"The vehicle name is printed above the zone name");
    // Fade: the SA alpha reaches both text and shadow.
    r.alpha=100;auto faded=layout_text(r);
    check(faded.quads.front().colour.a==100 && faded.quads.back().colour.a==100,"SA's fade alpha is applied to text and shadow");
    r.alpha=255;
    // Subtitles: centred in the strip right of the radar.
    r.element=TextElement::subtitle;r.text="HELLO THERE";auto sub=layout_text(r);
    check(!sub.quads.empty() && sub.quads.front().page==1 && sub.quads.front().colour.r==225,"Subtitles are light grey in the standard font");
    const float bulge=r.screen.ps2(148.f),strip=r.screen.width-r.screen.ps2(28.f)-bulge;
    check(near((min_x(sub)+max_x(sub))/2.f,strip/2.f+bulge,r.screen.unit()*2.f),"Subtitles are centred in the strip right of the radar");
    r.widescreen_cutscene=true;auto wide=layout_text(r);
    check(near((min_x(wide)+max_x(wide))/2.f,r.screen.width/2.f,r.screen.unit()*2.f),"Cutscene subtitles are centred on the screen");
    r.widescreen_cutscene=false;
    // Help message: box behind grey wrapped text at the top left, fading with the text.
    r.element=TextElement::help;r.text="PRESS A BUTTON TO DO THE THING AND THEN ANOTHER ONE";r.alpha=200;auto help=layout_text(r);
    check(help.has_box && help.box_colour.a==180 && help.box_colour.r==0,"Help text has a black box at 90 percent of its fade");
    check(near(help.box.left,r.screen.ps2(34.f)-4.f) && near(help.box.right,r.screen.ps2(230.f)),"The help box spans x 34..230 (PS2 canvas)");
    check(help.box.bottom-help.box.top>r.screen.ps2(20.f) && max_x(help)<=r.screen.ps2(230.f)+r.screen.unit(),"Long help texts wrap inside the box");
    check(help.quads.front().colour.r==175,"Help text is grey");
    r.alpha=255;
    // Big messages.
    r.text="MISSION COMPLETE";
    r.element=TextElement::big_message_0;auto big=layout_text(r);
    check(big.quads.front().page==2 && near((min_x(big)+max_x(big))/2.f,r.screen.width/2.f,r.screen.unit()*3.f),"Big message 0 is centred in the heading font");
    check(max_y(big)>r.screen.height/2.f-r.screen.ps2(18.f) && min_y(big)<r.screen.height/2.f,"Big message 0 sits around the screen centre");
    r.element=TextElement::big_message_2;auto wasted=layout_text(r);
    check(near(max_x(wasted),r.screen.ps2_from_right(20.f),r.screen.unit()*40.f),"Wasted/busted is right aligned 20 units from the edge");
    r.element=TextElement::big_message_1;auto title=layout_text(r);
    check(title.quads.front().page==0 && title.quads.back().colour.g==172,"The mission title uses the bank font in its gold colour");
    for(auto e:{TextElement::big_message_3,TextElement::big_message_4,TextElement::big_message_5}){
        r.element=e;auto odd=layout_text(r);
        check(!odd.quads.empty() && odd.quads.front().page==1 && near((min_x(odd)+max_x(odd))/2.f,r.screen.width/2.f,r.screen.unit()*25.f),"Odd job texts are centred in the standard font");
    }
    r.element=TextElement::big_message_3;auto o3=layout_text(r);r.element=TextElement::big_message_4;auto o4=layout_text(r);
    check(min_y(o3)<min_y(o4),"The odd job heading is 16 units above the text");
    std::cout<<"PASS: LCS on-screen text layouts (zone, vehicle, subtitles, help, big messages)\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
