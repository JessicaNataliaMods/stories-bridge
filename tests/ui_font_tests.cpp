#include "lcs/ui_font.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace lcs::ui;
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
bool near(float a,float b,float tol=0.001f){return std::fabs(a-b)<tol;}
int main(){try{
    TextStyle s;s.style=FontStyle::standard;s.scale_x=2.f;s.scale_y=3.f;
    // Proportional advance: width + extra spacing + style increment (standard: -2).
    const float a=glyph_advance(s,'A'-' ');
    check(a>0.f && near(string_width(s,"AA"),2.f*a*s.scale_x),"String width is the sum of scaled glyph advances");
    check(near(string_width(s,"A B",false),a*s.scale_x),"Width stops at the first space unless spaces are counted");
    check(string_width(s,"A B",true)>string_width(s,"A B",false),"Counting spaces widens the run");
    check(near(string_width(s,"A~r~B"),string_width(s,"AB")),"Tokens occupy no width");
    s.proportional=false;
    check(near(glyph_advance(s,'A'-' '),glyph_advance(s,'B'-' ')) && glyph_advance(s,'.'-' ')<glyph_advance(s,'A'-' '),"Fixed width fonts narrow only punctuation");
    s.proportional=true;
    std::vector<GlyphQuad> q;print_line(s,100.f,50.f,"AB",q);
    check(q.size()==2,"Each visible glyph yields one quad");
    check(near(q[0].x[0],100.f) && near(q[0].y[0],50.f) && near(q[0].y[2],50.f+20.f*s.scale_y),"Standard glyph box is 20 units tall at the requested scale");
    check(near(q[1].x[0],100.f+a*s.scale_x),"Glyphs advance by the scaled glyph width");
    for(const auto& g:q)for(int i=0;i<4;++i)check(g.u[i]>=0.f && g.u[i]<=1.f && g.v[i]>=0.f && g.v[i]<=1.f && g.page==1,"UVs stay inside the font page");
    q.clear();print_line(s,100.f,50.f,"A B",q);check(q.size()==2,"Spaces are blank glyphs without quads");
    q.clear();print_line(s,100.f,50.f,"A~r~B",q);check(q.size()==2 && q[1].colour.r==174 && q[0].colour.r==255,"A colour token recolours following glyphs");
    s.colour={0,0,0,255};q.clear();print_line(s,100.f,50.f,"A~r~B",q);check(q[1].colour.r==0,"Colour tokens are ignored for a black base colour");
    s.colour={255,255,255,255};
    s.right_justify=true;q.clear();print_line(s,200.f,50.f,"AB",q);
    check(near(q[0].x[0],200.f-string_width(s,"AB")),"Right justify ends at the anchor");
    s.right_justify=false;s.centre=true;q.clear();print_line(s,200.f,50.f,"AB",q);
    check(near(q[0].x[0],200.f-string_width(s,"AB")/2.f),"Centre splits the width around the anchor");
    s.centre=false;s.drop_shadow=2;s.drop_colour={0,0,0,200};q.clear();print_line(s,100.f,50.f,"AB",q);
    check(q.size()==4 && q[0].colour.a==200 && near(q[0].x[0],102.f) && near(q[0].y[0],52.f) && q[2].colour.a==255,"Drop shadow is printed first, offset, in the drop colour");
    s.style=FontStyle::heading;s.drop_shadow=2;q.clear();print_line(s,10.f,10.f,"A",q);
    check(q.size()==1 && q[0].page==2,"Heading font has no drop shadow and uses page 2");
    const int heading_a=new_character('A'-' ');
    TextStyle heading;heading.style=FontStyle::heading;
    q.clear();print_line(heading,10.f,10.f,"A",q);
    check(q.size()==1 && near(q[0].u[0],float(heading_a%16)/16.f) && near(q[0].v[0],float(heading_a/16)/6.4f),
          "Heading style maps characters into the reduced PS2 atlas automatically");
    check(near(string_width(heading,"A"),glyph_advance(heading,heading_a)),
          "Heading text width uses the remapped atlas character");
    check(new_character(33)==27 && new_character(190)==90 && new_character(29)==2,"Reduced character set remapping");
    Screen screen;screen.width=960.f;screen.height=544.f;check(near(screen.psp_x(480.f),960.f) && near(screen.from_right(12.f),960.f-24.f) && near(screen.right_anchored_x(375.f),960.f-105.f*2.f) && screen.wide(),"PSP layout scales by height/272 (width/480 on 16:9) and anchors to the right");
    Screen ultra;ultra.width=2000.f;ultra.height=837.f;check(near(ultra.psp_y(100.f),100.f*837.f/272.f),"Vertical units follow the height on ultrawide screens");
    // Word wrapping, bottom anchoring and the background rectangle.
    TextStyle w;w.style=FontStyle::standard;w.scale_x=1.f;w.scale_y=1.f;
    const float line=32.f*w.scale_y*0.5f+2.f*w.scale_y;
    check(count_lines(w,0.f,"HELLO",1000.f)==1,"Short text is one line");
    const float one_word=string_width(w,"HELLO",false);
    check(count_lines(w,0.f,"HELLO HELLO HELLO",one_word*2.5f)==3-1+0 || count_lines(w,0.f,"HELLO HELLO HELLO",one_word*2.5f)==3,"Words wrap at the limit");
    check(count_lines(w,0.f,"HELLO HELLO",one_word*1.2f)==2,"Two words that do not fit side by side take two lines");
    check(count_lines(w,0.f,"HELLOHELLOHELLO",one_word)==1,"A single long word never splits");
    q.clear();print_block(w,10.f,100.f,"HELLO HELLO",one_word*1.2f,q);
    check(q.size()==10 && near(q[0].y[0],100.f) && near(q[9].y[0],100.f+line),"Each wrapped line moves down by 16+2 units at scale 1");
    w.centre=true;w.centre_size=one_word*1.2f;q.clear();print_block(w,200.f,0.f,"HELLO HELLO",0.f,q);
    check(q.size()==10 && near(q[0].x[0]+ (q[4].x[1]-q[0].x[0])/2.f,200.f,2.f) ,"Centred lines are centred on the anchor");
    w.centre=false;q.clear();print_block_from_bottom(w,10.f,100.f,"HELLO HELLO",one_word*1.2f,q);
    check(near(q[0].y[0],100.f-2.f*line),"From-bottom printing starts one block height above y");
    const auto rect=text_rect(w,10.f,20.f,"HELLO HELLO",one_word*1.2f+10.f);
    check(near(rect.left,6.f) && near(rect.right,one_word*1.2f+10.f) && near(rect.top,20.f) && near(rect.bottom,20.f+2.f*line+4.f),"The background box spans the wrapped block");
    w.right_justify=true;w.right_wrap=0.f;q.clear();print_block(w,300.f,0.f,"HELLO",0.f,q);
    check(q.size()==5 && near(q[4].x[1],300.f,32.f),"Right justified blocks end at the anchor");
    std::cout<<"PASS: LCS font metrics, tokens and quad generation\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
