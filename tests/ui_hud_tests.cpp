#include "lcs/ui_hud.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace lcs::ui;
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
bool near(float a,float b){return std::fabs(a-b)<0.01f;}
size_t count(const std::vector<SpriteCommand>& v,const std::string& name){size_t n=0;for(const auto& s:v)n+=s.texture==name;return n;}
int main(){try{
    PlayerHudInputs in;in.screen.width=960.f;in.screen.height=544.f;in.money=1112;in.hour=18;in.minute=2;in.frame=8;
    const float unit=in.screen.unit();
    check(near(unit,2.f),"16:9 unit equals width/480");
    auto out=layout_player_info(in);
    check(count(out,"bar_inside2")==1 && count(out,"bar_inside2dark")==1 && count(out,"bar_outline")==1,"Full health draws a fill, a remainder and an outline");
    check(count(out,"bar_inside1")==0,"No armour bar without armour");
    SpriteCommand fill;for(const auto& s:out)if(s.texture=="bar_inside2")fill=s;
    check(near(fill.u0,0.f) && near(fill.u1,1.f) && near(fill.top,40.f*unit) && near(fill.bottom,50.f*unit),"Full health fills the whole strip at y 40..50");
    check(near(fill.right-fill.left,43.52f*unit),"Wide layout bar is 43.52 units wide");
    check(near(fill.right,in.screen.right_anchored_x(375.f+43.52f)),"The bar is anchored to the right edge");
    in.health=50.f;out=layout_player_info(in);
    for(const auto& s:out)if(s.texture=="bar_inside2")fill=s;
    check(near(fill.u1,0.625f) && near(fill.right-fill.left,40.f*0.68f*unit),"Half health samples 40/64 of the strip and scales by 0.68");
    SpriteCommand dark;for(const auto& s:out)if(s.texture=="bar_inside2dark")dark=s;
    check(near(dark.left,fill.right) && near(dark.u0,0.625f) && near(dark.u1,1.f),"The dark remainder starts where the fill ends");
    in.armour=60.f;out=layout_player_info(in);
    SpriteCommand armour;for(const auto& s:out)if(s.texture=="bar_inside1")armour=s;
    check(count(out,"bar_inside1dark")==1 && near(armour.top,28.f*unit),"Armour bar sits at y 28 when armour is above 1");
    in.health=5.f;in.frame=0;out=layout_player_info(in);
    check(count(out,"bar_inside2")==0,"Health below ten blinks off on frames without bit 8");
    in.frame=8;out=layout_player_info(in);check(count(out,"bar_inside2")==1,"... and on with bit 8");
    in.health=100.f;in.armour=0.f;out=layout_player_info(in);
    check(count(out,"hudnumbers")==9+5,"Money ($00001112) and clock (18:02) are one atlas quad per character");
    const SpriteCommand* first_digit=nullptr;
    for(const auto& s:out)if(s.texture=="hudnumbers"){first_digit=&s;break;}
    check(first_digit && near(first_digit->u0,(21%8)*0.125f) && near(first_digit->v0,(21/8)*0.265625f),"The dollar sign uses the second-set separator glyph");
    std::vector<SpriteCommand> digits;layout_numbers(in.screen,"01",373.f,53.f,true,255,digits);
    check(digits.size()==2 && near(digits[0].u0,(11%8)*0.125f) && near(digits[0].v0,(11/8)*0.265625f),"Second-set digits start at atlas index 11");
    check(digits[1].left>digits[0].left && near(digits[0].right-digits[0].left,9.f*unit),"Wide layout digit cell is 9 units");
    Screen narrow;narrow.width=640.f;narrow.height=480.f;std::vector<SpriteCommand> nd;layout_numbers(narrow,"1",373.f,53.f,true,255,nd);
    check(near(nd[0].right-nd[0].left,11.f*narrow.unit()),"4:3 layout digit cell is 11 units");
    std::vector<SpriteCommand> clock;layout_numbers(in.screen,"1:2",386.f,17.f,false,255,clock);
    check(clock.size()==3 && near(clock[1].u0,(10%8)*0.125f) && near(clock[1].v0,(10/8)*0.265625f),"The colon is atlas glyph 10 in the first set");
    PlayerHudInputs ultra;ultra.screen.width=2000.f;ultra.screen.height=837.f;
    out=layout_player_info(ultra);
    for(const auto& s:out)check(s.right<=ultra.screen.width+0.01f && s.left>0.f && s.bottom<ultra.screen.height,"Everything stays on screen on ultrawide");
    // Weapon icon, ammunition and wanted stars.
    PlayerHudInputs w;w.screen.width=960.f;w.screen.height=544.f;
    auto combat=layout_weapon_and_wanted(w);
    check(combat.sprites.size()==1 && combat.sprites[0].texture=="fist" && combat.sprites[0].weapon==-1 && combat.texts.empty(),"Bare fists use the hud.txd fist sprite and no ammunition");
    check(near(combat.sprites[0].top,16.f*unit) && near(combat.sprites[0].bottom,60.f*unit) && near(combat.sprites[0].right,w.screen.right_anchored_x(468.6f)),"The icon box spans y 16..60 and ends at 468.6 (wide)");
    w.weapon_type=31;w.ammo_clip=12;w.ammo_total=112;w.clip_size=30;
    combat=layout_weapon_and_wanted(w);
    check(combat.sprites[0].weapon==31 && combat.sprites[0].texture.empty(),"A weapon with a model samples its own icon");
    check(combat.texts.size()==3 && combat.texts[0].text=="100" && combat.texts[1].text=="-" && combat.texts[2].text=="12" && combat.texts[0].style.right_justify && !combat.texts[2].style.right_justify,"Magazine weapons print ammunition, a dash and the clip");
    check(near(combat.texts[0].y,42.f*unit) && combat.texts[0].style.drop_shadow>=1 && combat.texts[0].style.colour.a==255,"Ammunition sits at y 42 with a drop shadow");
    w.weapon_type=16;w.ammo_clip=0;w.ammo_total=5;w.clip_size=1;combat=layout_weapon_and_wanted(w);
    check(combat.texts.size()==1 && combat.texts[0].text=="5" && combat.texts[0].style.centre,"Single-shot weapons print the total centred");
    w.weapon_type=37;w.ammo_clip=500;w.ammo_total=3500;w.clip_size=500;combat=layout_weapon_and_wanted(w);
    check(combat.texts.size()==3 && combat.texts[0].text=="300" && combat.texts[2].text=="50","The flamethrower shows tenths of its fuel");
    w.weapon_type=5;combat=layout_weapon_and_wanted(w);check(combat.texts.empty() && combat.sprites[0].weapon==5,"Melee weapons show no ammunition");
    w.weapon_type=40;w.ammo_total=1;w.ammo_clip=0;w.clip_size=1;combat=layout_weapon_and_wanted(w);check(combat.texts.empty(),"The detonator shows no ammunition");
    w.weapon_type=31;w.ammo_total=20000;w.ammo_clip=10;w.clip_size=30;combat=layout_weapon_and_wanted(w);check(combat.texts.empty(),"Unlimited ammunition is hidden");
    w.weapon_type=0;w.ammo_total=0;w.ammo_clip=0;
    w.wanted=3;w.now_ms=10000;w.wanted_changed_ms=1000;w.frame=0;combat=layout_weapon_and_wanted(w);
    size_t stars=0;for(const auto& sp:combat.sprites)stars+=sp.texture=="hudnumbers";
    check(stars==3,"Three stars for wanted level three once the flash is over");
    for(const auto& sp:combat.sprites)if(sp.texture=="hudnumbers")check(sp.colour.r==192 && sp.colour.g==155 && sp.colour.b==54,"Wanted stars are gold");
    w.now_ms=1500;combat=layout_weapon_and_wanted(w);stars=0;for(const auto& sp:combat.sprites)stars+=sp.texture=="hudnumbers";
    check(stars==0,"A new wanted level flashes: hidden on frames without bit 4");
    w.frame=4;combat=layout_weapon_and_wanted(w);stars=0;for(const auto& sp:combat.sprites)stars+=sp.texture=="hudnumbers";
    check(stars==3,"... and shown with bit 4");
    w.wanted=0;w.parole=2;w.now_ms=10000;w.frame=4;combat=layout_weapon_and_wanted(w);stars=0;
    for(const auto& sp:combat.sprites)if(sp.texture=="hudnumbers"){++stars;check(sp.colour.b==188,"Parole stars use the second colour set");}
    check(stars==2,"Parole shows its remembered level");
    w.frame=0;combat=layout_weapon_and_wanted(w);stars=0;for(const auto& sp:combat.sprites)stars+=sp.texture=="hudnumbers";check(stars==0,"Parole stars blink");
    w.wanted=6;w.parole=0;w.frame=0;w.now_ms=10000;combat=layout_weapon_and_wanted(w);
    float previous=1e9f;stars=0;for(const auto& sp:combat.sprites)if(sp.texture=="hudnumbers"){++stars;check(sp.left<previous,"Stars run leftwards from x=441");previous=sp.left;}
    check(stars==6,"Six stars at the maximum level");
    // Radar geometry.
    Screen rs;rs.width=1920.f;rs.height=1080.f;
    const auto radar=layout_radar(rs);
    const float u2=rs.height/448.f;
    check(near(radar.left,40.f*u2) && near(radar.width,82.f*u2) && near(radar.height,82.f*u2) && near(radar.top+radar.height,rs.height-40.f*u2),"The corrected radar box is 82x82, 40 units from the left and bottom");
    check(near(radar.disc_right-radar.disc_left,radar.width+12.f*u2) && near(radar.disc_bottom-radar.disc_top,radar.height+12.f*u2) && near(radar.shadow_offset,2.f*u2),"The disc frame grows 6 units on each side and its shadow sits 2 units lower");
    float sx=0,sy=0;radar.to_screen(0.f,0.f,sx,sy);
    check(near(sx,radar.left+radar.width/2.f) && near(sy,radar.top+radar.height/2.f),"Radar centre maps to the middle of the box");
    radar.to_screen(1.f,1.f,sx,sy);check(near(sx,radar.left+radar.width) && near(sy,radar.top),"Radar space (1,1) is the top right corner");
    radar.to_screen(-1.f,-1.f,sx,sy);check(near(sx,radar.left) && near(sy,radar.top+radar.height),"Radar space (-1,-1) is the bottom left corner");
    for(float aspect:{4.f/3.f,16.f/9.f,3440.f/1440.f}){
        Screen screen;screen.height=1440.f;screen.width=screen.height*aspect;
        const auto r=layout_radar(screen);
        float cx,cy,ex,ey,nx,ny;r.to_screen(0,0,cx,cy);r.to_screen(1,0,ex,ey);r.to_screen(0,1,nx,ny);
        check(near(ex-cx,cy-ny),"Radar world axes have equal pixel scale at 4:3, 16:9 and ultrawide");
        check(near(r.disc_right-r.disc_left,r.disc_bottom-r.disc_top),"Radar disc and map remain circular across aspects");
        check(near(screen.psp_x(20),screen.psp_y(20)),"Other PSP HUD elements retain uniform XY scale");
    }
    std::cout<<"PASS: LCS player HUD and circular radar across 4:3, 16:9 and ultrawide\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
