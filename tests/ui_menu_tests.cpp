#include "lcs/ui_menu.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace lcs::ui;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool near(float a,float b){return std::fabs(a-b)<.01f;}
int main(){try{
    MenuFont f;f.text.style=FontStyle::standard;f.line_height=30.f;f.wrap_x=1000.f;
    auto a=layout_menu_text(f,100.f,80.f,"AB~n~CD");
    check(a.lines==2 && a.quads.size()==4 && near(a.quads[2].y[0]-a.quads[0].y[0],30.f),"Explicit menu newlines keep the native line height");
    auto bottom=layout_menu_text(f,100.f,200.f,"AB~n~CD",true);
    check(near(bottom.quads[0].y[0],140.f),"From-bottom helper text stays anchored to its native bottom");
    f.text.right_justify=true;
    auto right=layout_menu_text(f,500.f,80.f,"AB");
    check(near(right.quads[0].x[0],500.f-menu_text_width(f,"AB")),"The measured LCS width and native right anchor agree");
    f.text.right_justify=false;f.text.centre=true;f.text.centre_size=1000.f;
    auto centred=layout_menu_text(f,500.f,80.f,"AB");
    check(near(centred.quads[0].x[0],500.f-menu_text_width(f,"AB")/2.f),"Centre alignment measures the displayed font");
    f.text.centre=false;f.wrap_x=100.f+menu_text_width(f,"AB")+1.f;
    auto wrap=layout_menu_text(f,100.f,80.f,"AB CD");check(wrap.lines==2,"Menu wrapping uses the LCS glyph metrics");
    f.wrap_x=1000.f;f.background=true;f.outline=1;
    auto outlined=layout_menu_text(f,100.f,80.f,"AB");
    check(outlined.has_box && outlined.quads.size()==18,"Native background/outline preferences survive font conversion");
    f.text.style=FontStyle::heading;f.outline=0;f.background=false;
    auto title=layout_menu_text(f,100.f,80.f,"A");
    check(title.quads.size()==1 && title.quads[0].page==2 && near(title.quads[0].u[0],float(new_character('A'-' ')%16)/16.f),"Menu titles use the remapped LCS heading atlas");
    check(menu_text_width(f,"A~n~AAAA")>menu_text_width(f,"A"),"Multiline width is the longest displayed paragraph");
    std::cout<<"PASS: LCS menu font metrics, native anchors, wrapping, newlines and outline\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
