#include "lcs/route_consumer.hpp"
#include <iostream>
#include <stdexcept>
using namespace lcs;
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static const ObjectiveActor actor{27,0xABC0};
static EpochScriptTask receipt(size_t slot,uint64_t epoch,uint32_t task=0x1234){return {{slot,{0x05D3,0,task,0xABC0,1}},epoch};}
struct Backend:RouteConsumerBackend {
    std::optional<RouteObservation> state=RouteObservation{actor,receipt(0,2),{10,0,0},true,false,true,true,true,false};
    RouteReplacement effect{true,true,receipt(0,3,0x1235)};
    int reads=0,writes=0;std::array<float,3> target{};bool running=false,cleanup=false;
    std::optional<RouteObservation> observe(ObjectiveActor,const EpochScriptTask&)override{++reads;return state;}
    RouteReplacement replace(ObjectiveActor live,const EpochScriptTask& expected,const std::array<float,3>& point,bool run,bool clean)override {
        check(live==actor && state && expected.epoch==state->binding.epoch,"Native replacement receives the current actor and refreshed ownership token");
        ++writes;target=point;running=run;cleanup=clean;return effect;
    }
};
int main(){try{
    OriginalRouteTable table;for(float x:{0.f,10.f,20.f})check(table.add(4,{x,0,0}),"Build original route fixture");
    auto make=[&](int mode=1){auto cursor=OriginalRouteCursor::begin(table,4,mode);check(bool(cursor),"Original route start");auto consumer=OriginalRouteConsumer::accepted(*cursor,actor,receipt(0,2),true);check(bool(consumer),"Accepted route command + actual native receipt");return *consumer;};
    {Backend b;auto c=make();b.state->position={0,0,0};check(c.tick(table,b)==OriginalRouteConsumer::Result::waiting && !b.writes && c.cursor().passed()==0,"Far position does not advance a route or recreate motion");}
    for(int suspended=0;suspended<5;++suspended){Backend b;auto c=make();
        if(suspended==0)b.state->temporary=true;if(suspended==1)b.state->controllable=false;if(suspended==2)b.state->on_foot=false;if(suspended==3)b.state->motion_active=false;
        if(suspended==4)b.state->binding.binding.record={0x05D3,0x1234,0,0xABC0,0};
        check(c.tick(table,b)==OriginalRouteConsumer::Result::suspended && !b.writes && c.cursor().passed()==0,"Temporary/lost control/vehicle/inactive/pending event cannot count as arrival even when target position matches");}
    for(int missing=0;missing<4;++missing){Backend b;auto c=make();
        if(missing==0)b.state.reset();if(missing==1)b.state->actor.handle=28;if(missing==2)b.state->binding.epoch=99;if(missing==3)b.state->current_route=false;
        check(c.tick(table,b)==OriginalRouteConsumer::Result::unavailable && !c.monitoring() && !b.writes,"Lost observation/generation/ownership/current objective retires monitoring without forcing another task");
        auto reads=b.reads;c.tick(table,b);check(b.reads==reads && !b.writes,"Retired job never queries or replays itself");}
    {Backend b;auto c=make();b.state->running=true;
     check(c.tick(table,b)==OriginalRouteConsumer::Result::advanced && b.target==std::array<float,3>{20,0,0} && b.running && b.cleanup && c.cursor().passed()==1,"Owned arrival issues next live table target with source running preference and cleanup, then commits");
     b.state->binding=*b.effect.accepted;b.state->position={20,0,0};
     check(c.tick(table,b)==OriginalRouteConsumer::Result::sequence_ended && b.writes==1 && !c.monitoring(),"Stop mode holds at the final point (retail point -1 is an out-of-bounds seek): no write, no completion, no replay");}
    {Backend b;auto c=make();b.effect.accepted=receipt(1,2,0x4444);
     check(c.tick(table,b)==OriginalRouteConsumer::Result::advanced,"Epochs are per slot: same number in a different newly accepted record is not stale ownership");}
    {Backend b;auto c=make();b.effect={false,false,{}};
     check(c.tick(table,b)==OriginalRouteConsumer::Result::refused && c.cursor().passed()==0 && c.monitoring(),"Pre-entry refusal leaves original cursor unchanged");}
    {Backend b;auto c=make();b.effect={false,true,{}};
     check(c.tick(table,b)==OriginalRouteConsumer::Result::unavailable && c.cursor().passed()==0 && !c.monitoring(),"Entered native fault retires monitoring and never replays side effects");c.tick(table,b);check(b.writes==1,"No second emission after entered fault");}
    {Backend b;auto c=make();b.effect.entered=false;check(c.tick(table,b)==OriginalRouteConsumer::Result::unavailable && c.cursor().passed()==0 && !c.monitoring(),"Contradictory successful movement without native entry is not an accepted motion effect");}
    for(int invalid=0;invalid<3;++invalid){Backend b;auto c=make();
        if(invalid==0)b.effect.accepted.reset();if(invalid==1)b.effect.accepted=receipt(0,2);if(invalid==2)b.effect.accepted->binding.record.ped=0xFFFF;
        check(c.tick(table,b)==OriginalRouteConsumer::Result::unavailable && c.cursor().passed()==1 && !c.monitoring(),"Accepted effect with missing/stale/foreign advisory receipt commits once but does not replay or invent ownership");}
    {Backend b;auto c=make();b.state->position[0]=std::numeric_limits<float>::infinity();
     check(c.tick(table,b)==OriginalRouteConsumer::Result::unavailable && !b.writes && !c.cursor().passed(),"Invalid observed coordinate never advances cursor or writes task");}
    {Backend b;auto c=make(2);b.state->position={10,0,100};
     check(c.tick(table,b)==OriginalRouteConsumer::Result::advanced,"Original XY arrival ignores height without extending native radius");}
    std::cout<<"PASS: live route consumer, exact actor/epoch/control gates, per-slot identity, native effect receipts, atomic refusals and no completion/progression inference; backend model only\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
