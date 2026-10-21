#include "lcs/wander_path.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
using namespace lcs;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Queries {
    std::optional<bool> los=true;
    WanderHeight water{true,{}},ground{true,0.f};
    bool provider_failure=false,bridge=false;
    size_t los_calls=0,water_calls=0;
    std::vector<std::pair<WanderPoint,float>> rays;
    std::vector<float> ground_sequence;
    size_t ground_index=0;
    std::optional<bool> building_los(const WanderPoint&,const WanderPoint&){++los_calls;return los;}
    WanderHeight water_level(const WanderPoint&){++water_calls;return water;}
    WanderHeight building_vertical(const WanderPoint& from,float end){
        rays.push_back({from,end});
        if(provider_failure)return {};
        if(water.height && from[2]==*water.height && end>=from[2])return {true,bridge?std::optional<float>(0.f):std::nullopt};
        if(ground_index<ground_sequence.size())return {true,ground_sequence[ground_index++]};
        return ground;
    }
};
int main(){try{
    auto run=[](Queries& q,WanderPoint to,float tolerance=1.f){return source_wander_path(WanderPoint{0,0,.5f},to,tolerance,q);};
    Queries q;
    check(run(q,{0,0,2.f})==false && !q.los_calls,"Vertical tolerance rejects before engine queries");
    q.los=false;check(run(q,{10,0,.5f})==false && q.rays.empty(),"Real obstructed LOS rejects before ground queries");
    q.los.reset();check(!run(q,{10,0,.5f}).has_value(),"Engine LOS failure is not an invented false condition");
    q=Queries{};check(run(q,{0,0,.5f})==true && q.rays.empty() && !q.water_calls,"Coincident points take retail zero-step return");
    q=Queries{};check(run(q,{.99f,0,.5f})==true && q.rays.empty(),"Floor of submeter distance does not force four steps");
    q=Queries{};check(run(q,{1.99f,0,.5f})==true && q.rays.size()==1 && !q.water_calls,"One step checks origin ground only");
    check(q.rays[0].second==-4.5f,"Origin ground ray ends five meters below the ped");
    for(float distance:{2.f,3.99f,4.f,1000.f}) {
        q=Queries{};const auto steps=distance>=4?4u:unsigned(distance);
        check(run(q,{distance,0,.5f})==true && q.rays.size()==steps && q.water_calls==steps-1,"Retail minimum clamps path sampling to at most four steps");
        for(size_t i=1;i<q.rays.size();++i)check(q.rays[i].first[0]==float(i) && q.rays[i].second==-1.5f,"Sampling advances one normalized meter, not fractions of destination distance");
    }
    q=Queries{};q.ground.height.reset();check(run(q,{10,0,.5f})==false,"Missing origin ground blocks path");
    q=Queries{};q.ground_sequence={0.f,-.6f};check(run(q,{10,0,.5f})==false && q.rays.size()==2,"A ground drop greater than one meter from next ray origin blocks progression");
    q=Queries{};q.ground_sequence={0.f,-.5f};check(run(q,{2,0,.5f})==true,"One-meter boundary is inclusive in the original ground-height test");
    q=Queries{};q.ground_sequence={0.f,.25f,.5f};check(run(q,{3,0,.5f})==true && q.rays[2].first[2]==.75f && q.rays[2].second==-1.25f,"Each ground probe updates the next origin from the observed surface plus half a meter");
    q=Queries{};q.water={true,-2.f};check(run(q,{10,0,.5f})==false && q.rays.size()==1 && q.rays[0].second==.5f,"Water requires real building intersection upwards to maximum endpoint height");
    q=Queries{};q.water={true,-2.f};q.bridge=true;check(run(q,{10,0,.5f})==true && q.rays.size()==7,"Bridge above water passes all upward and ground probes");
    q=Queries{};q.water.valid=false;check(!run(q,{10,0,.5f}).has_value(),"Water provider failure is separate from dry land");
    q=Queries{};q.provider_failure=true;check(!run(q,{10,0,.5f}).has_value(),"Native ray failure is separate from absent collision");
    q=Queries{};q.ground.height=std::numeric_limits<float>::quiet_NaN();check(!run(q,{10,0,.5f}).has_value(),"Invalid native collision result cannot poison subsequent ray positions");
    q=Queries{};check(!run(q,{std::numeric_limits<float>::infinity(),0,.5f}).has_value() && !q.los_calls,"Non-finite SCM coordinates refuse before native query");
    q=Queries{};check(run(q,{0,0,.5f},-1.f)==false && !q.los_calls,"Finite negative tolerance preserves original rejection");
    std::cout<<"PASS: retail wander sampling, vertical tolerance, LOS, water/bridge rays, ground probes and provider failure separation; engine queries mocked\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
