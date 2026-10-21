#include "lcs/route_table.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace lcs;
static void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
int main(){try{
    OriginalRouteTable routes;
    for(const auto& n:routes.nodes())check(n.route==-1 && n.position==std::array<float,3>{},"All 200 source slots initialize empty with zero position");
    check(routes.add(0x10001,{{1,2,3}}) && routes.first(1)==0,"Route identifiers narrow to the source signed halfword");
    check(routes.add(0xfffe,{{4,5,6}}) && routes.at(1)->route==-2,"Signed negative route identity survives narrowing");
    check(routes.add(-1,{{7,8,9}}) && routes.add(2,{{10,11,12}}) && routes.at(2)->route==2,"Source -1 remains the free sentinel and can be reused");
    check(!routes.at(-1) && !routes.at(200) && !routes.at(INT32_MAX),"No negative or end sentinel can access outside the source table");
    const auto original=routes.nodes();
    check(!routes.add(3,{{0,std::numeric_limits<float>::infinity(),0}}) && routes.nodes()[3].route==original[3].route,"Nonfinite route coordinates reject before modifying the table");
    routes.reset();
    for(int i=0;i<200;++i)check(routes.add(i<2?1:(i<5?2:3),{{float(i),0,0}}),"Every original slot is usable");
    check(!routes.add(4,{{0,0,0}}),"Full table refuses the original out-of-bounds write");
    check(routes.remove(1),"Removing a short route with an available source block succeeds");
    check(routes.at(0)->position[0]==2 && routes.at(1)->position[0]==3 && routes.at(2)->position[0]==2 && routes.at(4)->position[0]==4,
        "Retail copies only the removed block and preserves following duplicates; no silent full-tail compaction");
    check(routes.at(198)->route==-1 && routes.at(199)->route==-1 && routes.at(199)->position[0]==199,"Removal clears only tail identities, retaining position storage");
    check(routes.add(9,{{900,0,0}}) && routes.first(9)==198,"Insertion reuses the first actual free slot");
    check(routes.remove(777),"Absent route removal is a legitimate original idle branch");
    routes.reset();for(int i=0;i<200;++i)routes.add(i==199?9:1,{{float(i),0,0}});
    check(!routes.remove(9) && routes.at(199)->route==9 && routes.at(199)->position[0]==199,"Tail deletion that retail would read beyond capacity fails atomically");
    check(!routes.remove(1) && routes.at(0)->route==1,"Large retail block overflow also leaves the table intact");
    routes.reset();check(routes.at(0)->route==-1 && routes.at(199)->route==-1,"New original script initialization releases every route slot");
    for(int i=0;i<3;++i)routes.add(5,{{float(i*10),0,float(i)}});
    using Cursor=OriginalRouteCursor;
    auto stop=Cursor::begin(routes,5,1),bounce=Cursor::begin(routes,5,2),loop=Cursor::begin(routes,5,3);
    check(stop && bounce && loop && stop->target()[0]==10,"Follow starts at start+1; the first point is its source anchor");
    check(!Cursor::begin(routes,6,1) && !Cursor::begin(routes,-1,1) && !Cursor::begin(routes,5,4),"Unknown route, free sentinel and unknown mode cannot start movement");
    check(stop->arrive(routes,{{9,0,0}})==Cursor::Result::waiting && stop->passed()==0,"Exact one-metre boundary does not count as arrival");
    check(stop->arrive(routes,{{10,0,999}})==Cursor::Result::advanced && stop->target()[0]==20,"Original arrival is XY, advances once and copies next XYZ");
    check(stop->arrive(routes,{{20,0,0}})==Cursor::Result::ended && stop->arrive(routes,{{0,0,0}})==Cursor::Result::ended,"Stop mode reaches a terminal route state without inventing a script completion latch");
    bounce->arrive(routes,{{10,0,0}});bounce->arrive(routes,{{20,0,0}});
    check(bounce->direction()==-1 && bounce->target()[0]==10,"Reverse mode turns at the endpoint without repeating it");
    bounce->arrive(routes,{{10,0,0}});bounce->arrive(routes,{{0,0,0}});
    check(bounce->direction()==1 && bounce->target()[0]==10,"Reverse mode returns through the anchor and turns forward");
    loop->arrive(routes,{{10,0,0}});loop->arrive(routes,{{20,0,0}});
    check(loop->passed()==-1 && loop->target()[0]==0,"Loop mode resets passed=-1 and selects the original anchor");
    auto mutation=Cursor::begin(routes,5,2);check(routes.remove(5),"Fixture removes a live route from the global table");
    check(mutation->target()[0]==10 && mutation->arrive(routes,{{10,0,0}})==Cursor::Result::unavailable && mutation->passed()==0,
        "A table mutation preserves the copied current target; an invalid next target refuses atomically instead of finishing the mission");
    check(mutation->arrive(routes,{{std::numeric_limits<float>::quiet_NaN(),0,0}})==Cursor::Result::unavailable,"Invalid actor position cannot count as route arrival");
    std::cout<<"PASS: retail 200-node route storage, halfword IDs, block deletion and bounded failures; no runtime command support granted\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
