#include "lcs/ui_hud.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
namespace lcs::ui {
namespace {
float clamp(float v,float lo,float hi){return std::min(std::max(v,lo),hi);}
// DrawHealthBar / DrawArmourBar: the filled part, the dark remainder and the outline
// share one 64x16 strip each. Rows are 10 units tall at y (health 40, armour 28).
void bar(const PlayerHudInputs& in,float value,float maximum,float y,const char* inside,const char* dark,std::vector<SpriteCommand>& out){
    const auto& s=in.screen;
    const bool wide=s.wide();
    value=clamp(value,0.f,maximum);
    float width=(value/maximum)*40.f+16.f+4.f;
    float u=width/64.f;
    if(int(value)==0){width=0.f;u=0.f;}
    else if(value==maximum){width=64.f;u=1.f;}
    width*=wide?0.68f:0.85f;
    const float x0=wide?375.f:360.f;
    const float total=wide?43.52f:54.4f;
    const Rgba white{255,255,255,in.alpha};
    const float top=s.psp_y(y),bottom=s.psp_y(y+10.f);
    // Filled part: u in [0,u].
    out.push_back({inside,s.right_anchored_x(x0),top,s.right_anchored_x(x0+width),bottom,0.f,0.f,u,1.f,white});
    // Remainder: u in [u,1].
    out.push_back({dark,s.right_anchored_x(x0+width),top,s.right_anchored_x(x0+total),bottom,u,0.f,1.f,1.f,white});
    // Outline over the whole strip, sampled from 0.01 as in the original.
    out.push_back({"bar_outline",s.right_anchored_x(x0),top,s.right_anchored_x(x0+total),bottom,0.01f,0.f,1.f,1.f,white});
}
}
void layout_numbers(const Screen& s,const std::string& text,float x,float y,bool second_set,uint8_t alpha,std::vector<SpriteCommand>& out){
    Rgba colour{255,255,255,alpha};
    const float width=s.wide()?9.f:11.f,height=10.f;
    for(const char ch:text){
        int c=uint8_t(ch);
        if(c>='0' && c<=':'){
            c-=second_set?'%':'0';
        }else if(c==22){
            colour=second_set?Rgba{62,141,188,alpha}:Rgba{192,155,54,alpha};
        }else{
            c=second_set?21:10;
        }
        const int row=c/8,col=c-row*8;
        const float width2=c==22?width+3.f:width;
        const float u=float(col)*0.125f,v=float(row)*0.265625f;
        out.push_back({"hudnumbers",s.right_anchored_x(x),s.psp_y(y),s.right_anchored_x(x+width2),s.psp_y(y)+s.psp_y(height),
                       u,v,u+0.125f,v+0.265625f,colour});
        x+=width-2.f;
        if(c==10)x-=4.f;
        // The original snaps the advance to PS2 pixels (x*640/480, rounded up).
        x=std::ceil(x*(640.f/480.f))*(480.f/640.f);
    }
}
std::vector<SpriteCommand> layout_player_info(const PlayerHudInputs& in){
    std::vector<SpriteCommand> out;
    const auto& s=in.screen;
    // Below ten health the bar blinks on the native frame counter.
    const bool low=in.health<10.f;
    if(!low || (in.frame&8u))
        bar(in,in.health,in.max_health,40.f,"bar_inside2","bar_inside2dark",out);
    if(in.armour>1.f)
        bar(in,in.armour,in.max_armour,28.f,"bar_inside1","bar_inside1dark",out);
    char buffer[32];
    std::snprintf(buffer,sizeof(buffer),"$%08d",in.money);
    layout_numbers(s,buffer,s.wide()?386.f:373.f,53.f,true,in.alpha,out);
    std::snprintf(buffer,sizeof(buffer),"%02d:%02d",in.hour,in.minute);
    layout_numbers(s,buffer,s.wide()?386.f:373.f,17.f,false,in.alpha,out);
    return out;
}

std::vector<SpriteCommand> layout_breath_bar(const Screen& s,float fraction,uint8_t alpha){
    const float width=s.wide()?43.52f:54.4f;
    const float life=s.wide()?375.f:360.f;
    // The first quarter of bar_outline is transparent. Match the visible life
    // frame, not its padded quad, and crop that padding from the oxygen frame.
    constexpr float life_u0=0.01f,visible_u0=0.25f;
    const float visible_left=s.right_anchored_x(life)+s.psp_x(width*(visible_u0-life_u0)/(1.f-life_u0));
    const float right=visible_left-s.psp_x(1.f);
    const float left=right-s.psp_x(width*(1.f-visible_u0)/(1.f-life_u0));
    const float top=s.psp_y(40.f),bottom=s.psp_y(50.f);
    const float dx=s.psp_x(width/64.f/(1.f-life_u0)),dy=s.psp_y(10.f/16.f);
    const float fill_left=left+dx,fill_right=right-dx;
    fraction=std::isfinite(fraction)?clamp(fraction,0.f,1.f):0.f;
    return {{"",fill_left,top+dy,fill_right,bottom-dy,0,0,1,1,{32,32,32,alpha}},
            {"",fill_left,top+dy,fill_left+(fill_right-fill_left)*fraction,bottom-dy,0,0,1,1,{255,255,255,alpha}},
            {"bar_outline",left,top,right,bottom,visible_u0,0,1,1,{255,255,255,alpha}}};
}
// Weapons with a magazine show "total-clip"; SA types below 16 are melee, 40 is the detonator.
CombatLayout layout_weapon_and_wanted(const PlayerHudInputs& in){
    CombatLayout out;
    const auto& s=in.screen;
    const bool wide=s.wide();
    const Rgba white{255,255,255,in.alpha};
    // DrawWeaponIcon: a 54x44 (48.6x44 wide) box right of the clock, from y=16.
    const float right=wide?468.6f:470.f,left=right-(wide?48.6f:54.f);
    SpriteCommand icon;
    icon.left=s.right_anchored_x(left);icon.right=s.right_anchored_x(right);
    icon.top=s.psp_y(16.f);icon.bottom=s.psp_y(60.f);icon.colour=white;
    if(in.weapon_type==0)icon.texture="fist";
    else icon.weapon=in.weapon_type;
    out.sprites.push_back(icon);
    // DrawAmmo.
    const int total=in.ammo_total,clip=in.ammo_clip;
    if(in.weapon_type>=16 && in.weapon_type!=40 && std::min(9999,total-clip)!=9999){
        TextStyle style;
        style.style=FontStyle::standard;
        style.scale_x=s.psp_x(wide?0.18f:0.2f);style.scale_y=s.psp_y(0.44f);
        style.colour=white;style.proportional=true;
        style.drop_shadow=std::max(1,int(2.f*s.height/448.f+0.5f));
        style.drop_colour=Rgba{0,0,0,in.alpha};
        const float y=s.psp_y(42.f);
        const auto at=[&](float canvas_x){return s.from_right(480.f-canvas_x);};
        if(in.clip_size<=1 || in.clip_size>=1000){
            style.centre=true;
            out.texts.push_back({std::to_string(total),style,at(wide?438.f:435.f),y});
        }else{
            const bool flame=in.weapon_type==37;
            const int shown_clip=flame?clip/10:clip;
            const int shown_ammo=std::min(flame?(total-clip)/10:total-clip,9999);
            auto right_style=style;right_style.right_justify=true;
            out.texts.push_back({std::to_string(shown_ammo),right_style,at(wide?438.f:435.f),y});
            out.texts.push_back({"-",style,at(wide?439.f:436.f),y});
            out.texts.push_back({std::to_string(shown_clip),style,at(wide?441.f:439.f),y});
        }
    }
    // DrawWanted: six star slots from x=441 leftwards; a new level flashes for two seconds,
    // and the level kept for parole flashes in the second colour set.
    float star_x=441.f;
    for(int i=0;i<6;++i){
        const bool recent=in.now_ms<=in.wanted_changed_ms+2000u;
        const std::string star(1,char(22));
        if(in.wanted>i && (!recent || (in.frame&4u)))layout_numbers(s,star,star_x,63.f,false,in.alpha,out.sprites);
        else if(in.parole>i && (in.frame&4u))layout_numbers(s,star,star_x,63.f,true,in.alpha,out.sprites);
        star_x-=wide?11.f:13.5f;
    }
    return out;
}

RadarLayout layout_radar(const Screen& s){
    RadarLayout r;
    // Radar.h FIX_RADAR uses 82x82; 94x76 is the old distorted layout.
    // Uniform pixel scaling keeps the map, mask and disc circular at any aspect.
    r.left=s.ps2(40.f);r.width=s.ps2(82.f);r.height=s.ps2(82.f);
    r.top=s.ps2_from_bottom(40.f+82.f);
    const float grow=s.ps2(6.f);
    r.disc_left=r.left-grow;r.disc_right=r.left+r.width+grow;
    r.disc_top=r.top-grow;r.disc_bottom=r.top+r.height+grow;
    r.shadow_offset=s.ps2(2.f);
    return r;
}
}
