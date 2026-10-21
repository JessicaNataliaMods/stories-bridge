#include "../src/sa_world_preview.hpp"
#include "../src/sa_map_culling.hpp"
#include <stdexcept>
int main() {
    const auto check=[](bool condition){if(!condition)throw std::runtime_error("LOD range/island regression");};
    lcs::WorldRenderRule ordinary{true,true,110,10002,31};
    check(!lcs::preview_lod_visible(ordinary,1,90,1,true));
    check(lcs::preview_lod_visible(ordinary,1,90,1,false));
    check(lcs::preview_lod_visible(ordinary,1,110,1,true));
    check(!lcs::preview_lod_visible(ordinary,1,150,2,true));
    check(lcs::preview_lod_visible(ordinary,1,330,4,true));
    lcs::WorldRenderRule industrial{true,true,100,-1,(1u<<2)|(1u<<3)};
    check(!lcs::preview_lod_visible(industrial,1,2000,1,false));
    check(lcs::preview_lod_visible(industrial,2,1200,1,false));
    check(lcs::preview_lod_visible(industrial,3,1200,1,false));
    check(!lcs::preview_lod_visible(industrial,0,1200,1,false));
    std::vector<lcs::IslandZone> zones{{0,{-2000,-2000,-500,2000,2000,500}},
                                    {1,{700,-1200,-50,1900,500,400}},
                                    {2,{-300,-1700,-50,650,400,400}}};
    const float portland[]{880,-542,20},staunton[]{0,-600,30},outside[]{3000,0,0};
    check(lcs::preview_level(zones,portland)==1);
    check(lcs::preview_level(zones,staunton)==2);
    check(lcs::preview_level(zones,outside)==0);
    lcs::WorldRenderRule track{true,false,0,-1,31,true};
    auto flags=lcs::preview_physical_flags((1u<<8)|(1u<<27)|128,track);
    check((flags&1) && !(flags&(1u<<8)) && (flags&128) && (flags&(1u<<27)));
    check(lcs::preview_physical_flags(flags,track)==flags);
    unsigned promoted_dummy_flags=(1u<<8)|(1u<<27);
    int removes=0,adds=0;
    lcs::preview_apply_physical_membership(promoted_dummy_flags,track,true,true,true,true,
        [&](){++removes;},[&](){++adds;});
    check(promoted_dummy_flags==((1u<<8)|(1u<<27)) && removes==0 && adds==0);
    unsigned big_dummy_flags=(1u<<8)|(1u<<27);
    lcs::preview_apply_physical_membership(big_dummy_flags,track,true,true,false,true,
        [&](){++removes;},[&](){++adds;});
    check((big_dummy_flags&1u) && !(big_dummy_flags&(1u<<8)) && removes==1 && adds==1);
    unsigned ordinary_physical_flags=1u<<27;
    lcs::preview_apply_physical_membership(ordinary_physical_flags,track,false,false,false,false,
        [&](){++removes;},[&](){++adds;});
    check((ordinary_physical_flags&1u) && !(ordinary_physical_flags&(1u<<8)) && removes==1 && adds==1);
    unsigned building_flags=(1u<<8)|(1u<<27);
    lcs::preview_apply_physical_membership(building_flags,track,true,false,true,true,
        [&](){++removes;},[&](){++adds;});
    check((building_flags&1u) && !(building_flags&(1u<<8)) && removes==2 && adds==2);
    // A BIG building already detached by the story overlay is demoted in its
    // flags only; the overlay's later restore performs the single Add.
    unsigned detached_flags=(1u<<8)|(1u<<27);
    lcs::preview_apply_physical_membership(detached_flags,track,true,false,false,false,
        [&](){++removes;},[&](){++adds;});
    check((detached_flags&1u) && !(detached_flags&(1u<<8)) && removes==2 && adds==2);
    ordinary.physical=false;
    check(lcs::preview_physical_flags(256,ordinary)==256);
    // Simulate the road pass forcing back/front culling before drawing. Both
    // orientations must reach the draw callback, with the caller's state intact
    // afterwards. Nesting and failures must not corrupt the next stock draw.
    for(unsigned initial:{1u,2u,3u}) {
        unsigned state=initial,draws=0;
        auto get=[&](unsigned& out){out=state;return true;};
        auto set=[&](unsigned value){state=value;return true;};
        auto result=lcs::draw_map_double_sided(get,set,[&](){
            ++draws;check(state==1);
            return lcs::draw_map_double_sided(get,set,[&](){++draws;check(state==1);return 10828;});
        });
        check(result==10828 && draws==2 && state==initial);
        check(lcs::draw_map_double_sided(get,set,[&](){check(state==1);state=3;return 4;})==4);
        check(state==initial);
        try {lcs::draw_map_double_sided(get,set,[&]()->int{check(state==1);throw std::runtime_error("draw failure");});}
        catch(const std::runtime_error&){}
        check(state==initial);
        auto failed_get=[](unsigned&){return false;};
        check(lcs::draw_map_double_sided(failed_get,set,[&](){check(state==initial);return 1;})==1);
        auto failed_set=[](unsigned){return false;};
        check(lcs::draw_map_double_sided(get,failed_set,[&](){check(state==initial);return 2;})==2);
    }
}
