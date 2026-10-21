#include "lcs/script_marker_pool.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
bool close(float a,float b){return std::abs(a-b)<.00001f;}
lcs::ScriptMarkerCommand command(uintptr_t id,uint16_t type,std::array<float,3> position){
    lcs::ScriptMarkerCommand c;auto& p=c.placement;
    p.identity=id;p.type=type;p.position=position;p.size=type==2?3.2f:7.f;
    p.pulse_period=type==2?1:128;p.pulse_fraction=type==2?1.f:0.f;
    p.rotate_rate=1;p.vertical_size=type==5?100.f:0.f;
    p.colour={20,40,60,uint8_t(type==5?255:200)};p.force_position=type==5;
    return c;
}
int main(){try{
    lcs::ScriptMarkerEnvironment env;env.now=1000;
    size_t floors=0,draws=0;
    env.floor=[&](const std::array<float,3>& p)->std::optional<lcs::ScriptMarkerEnvironment::Floor>{++floors;check(p[2]==20,"Floor probe original position; backend must apply queryZ+1");return lcs::ScriptMarkerEnvironment::Floor{true,42.f};};
    lcs::ScriptMarkerPool pool;
    auto point=command(100,5,{10,20,20});
    check(pool.submit(point,env) && floors==1 && pool.entries()[0].matrix[11]==41.65f,"New point uses actual floor minus size*.05");
    check(pool.entries()[0].vertical_size==100 && pool.entries()[0].current_size==7,"Point anisotropic scaling retained");
    auto draw=[&](size_t,const lcs::ScriptMarkerInstance& e){check(e.live,"Only live marker drawn");++draws;return true;};
    check(pool.frame(draw) && draws==1 && !pool.entries()[0].used && pool.entries()[0].live,"Ordinary marker waits one frame for refresh");
    check(pool.submit(point,env) && floors==1 && pool.entries()[0].matrix[11]==41.65f,"Refresh retains grounded Z instead of reintroducing script height");
    check(pool.frame(draw),"Grounded refresh frame");
    auto relocated=point;relocated.placement.position[0]+=1;
    check(pool.submit(relocated,env) && floors==2 && pool.entries()[0].matrix[9]==11 &&
          pool.entries()[0].matrix[11]==41.65f,"Moved point repeats actual floor query before drawing");
    check(pool.frame(draw) && pool.frame(draw) && !pool.entries()[0].live,"Unrefreshed marker expires rather than becoming persistent");
    pool.clear();auto arrow=command(200,2,{0,0,0});arrow.placement.target=std::array<float,3>{0,10,999};
    check(pool.submit(arrow,env),"Arrow admitted without ground");
    auto a=pool.entries()[0];
    check(close(a.standard_size,2.24f) && close(a.current_size,2.24f) && a.vertical_size==3.2f,"Retail first-frame arrow vertical/current size asymmetry");
    check(close(a.matrix[0],-1) && close(a.matrix[4],.9396926f) && close(a.matrix[5],-.34202015f),"World target basis and fixed negative20 degree tilt");
    check(pool.frame(draw) && pool.submit(arrow,env) && close(pool.entries()[0].vertical_size,2.24f),"Refresh uses pulsed current size for vertical scale");
    pool.frame(draw);arrow.placement.target=std::array<float,3>{0,0,-999};
    check(pool.submit(arrow,env) && close(pool.entries()[0].matrix[1],1),"Zero XY target falls back to positiveX regardless of targetZ");
    pool.clear();point=command(300,5,{200,0,20});
    check(pool.submit(point,env) && pool.frame(draw),"Far anisotropic point can render beyond150");
    check(pool.submit(point,env) && pool.entries()[0].matrix[1]>0 && pool.frame(draw),"Point rotates on refresh before copy");
    auto moved=command(300,5,{210,0,20});moved.copy_before=lcs::ScriptMarkerCommand::Copy{300,299,true,true};
    check(pool.submit(moved,env),"Copy and main placement commit together");
    check(pool.entries()[0].source.identity==300 && pool.entries()[1].source.identity==299 && pool.entries()[1].matrix[9]==200 && pool.entries()[1].source.persistent_fade,"Copy re-places previous position and current size under ID minus one");
    check(pool.entries()[1].matrix[0]==1 && pool.entries()[1].matrix[1]==0,"Copy creates its placement orientation instead of copying rotated matrix");
    check(pool.frame(draw) && pool.entries()[1].source.colour[3]==243 && pool.entries()[1].used,"Ghost fades12 and remains used without source refresh");
    for(int i=0;i<21;++i)check(pool.frame(draw),"Ghost frame succeeds");
    check(!pool.entries()[1].live,"Ghost expires at alpha<=12 without integer underflow");
    pool.clear();auto duplicate=command(301,2,{100,0,20});duplicate.placement.target=std::array<float,3>{100,10,20};
    auto duplicate_first=duplicate;duplicate_first.placement.identity=300;
    check(pool.submit(duplicate_first,env) && pool.submit(duplicate,env),"Two source identities may share a matrix");
    const auto before_duplicate=draws;check(pool.frame(draw) && draws==before_duplicate+1,"Duplicate matrices draw once even under different identities");
    check(pool.submit(duplicate,env) && pool.frame(draw) && draws==before_duplicate+1,"Source dedup includes preceding retired/unrefreshed matrix");
    pool.clear();auto culled=command(302,2,{151,0,20});const auto before_cull=draws;
    check(pool.submit(culled,env) && pool.frame(draw) && draws==before_cull,"Uniform marker at151 is culled");
    pool.clear();auto far_fade=command(303,5,{200,0,20});far_fade.placement.size=100;far_fade.placement.persistent_fade=true;
    check(pool.submit(far_fade,env) && pool.frame(draw) && pool.entries()[0].source.colour[3]==255 && pool.entries()[0].used,"Culling precedes fade; culled persistent marker retains alpha and used state");
    auto alpha_env=env;alpha_env.player={200,0,0};
    pool.clear();check(pool.submit(point,alpha_env) && pool.entries()[0].source.colour[3]==76,"Near point alpha uses source thirty-percent truncation");
    pool.clear();for(uintptr_t i=1;i<=32;++i)check(pool.submit(command(i,5,{200.f+float(i),0,0}),env),"Fill private32-slot pool");
    auto full=pool.entries();check(pool.submit(command(99,5,{500,0,0}),env) && pool.entries()[31].source.identity==32,"Point full-pool admission is successful no-op, no foreign slots borrowed");
    check(pool.submit(command(99,2,{500,0,0}),env) && pool.entries()[31].source.identity==32,"Arrow cannot evict source cylinders");
    pool.clear();auto old=command(400,1,{300,0,0});check(pool.submit(old,env),"Create source type1 entry");
    for(uintptr_t i=1;i<=31;++i)check(pool.submit(command(i,5,{200.f+float(i),0,0}),env),"Fill remaining slots");
    check(pool.submit(command(999,2,{10,0,0}),env) && pool.entries()[0].source.identity==999,"Closer arrow may evict far source type1 only");
    pool.clear();check(pool.submit(point,env) && pool.frame(draw),"Transaction source seeded");
    auto unavailable=env;unavailable.floor={};auto needs_floor=command(300,5,{1,1,20});needs_floor.copy_before=lcs::ScriptMarkerCommand::Copy{300,299,true,true};
    check(!pool.submit(needs_floor,unavailable) && pool.entries()[0].matrix[9]==200 && !pool.entries()[1].live,"Unavailable required floor data rolls back copy and placement together");
    pool.clear();check(pool.submit(point,env) && !pool.frame({}),"Missing real draw consumer cannot report successful frame");
    check(!pool.frame([](size_t,const auto&){return false;}),"Actual renderer refusal remains observable");
    std::cout<<"PASS: private source marker pool32: placement/refresh/copy/floor/pulse/tilt/expiry/fade/eviction/draw refusal; no native graphics executed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
