#include "lcs/script_coronas.hpp"
#include "lcs/engine_dispatch.hpp"
#include "ScriptCommands.h"
#include <iostream>
#include <cstring>
#include <stdexcept>
void check(bool c,const char* m){if(!c)throw std::runtime_error(m);}
int32_t bits(float v){int32_t b;std::memcpy(&b,&v,4);return b;}
void number(std::vector<uint8_t>& b,int32_t v){b.push_back(6);for(int i=0;i<4;++i)b.push_back(uint8_t(uint32_t(v)>>(i*8)));}
struct Result{lcs::StepResult status;int32_t id;};
Result command(lcs::ScriptCoronas& bridge,uint16_t op,const std::vector<int32_t>& inputs,bool output=false){
    std::vector<uint8_t> code(64);code.push_back(uint8_t(op));code.push_back(uint8_t(op>>8));
    for(auto v:inputs)number(code,v);if(output)code.push_back(12);
    lcs::Vm vm(code,64,64);lcs::Thread t;t.ip=64;t.locals[0]=-12345;
    vm.backend=[&](lcs::Vm& v,lcs::Thread& th,uint16_t command){return lcs::try_engine_adapter(bridge,v,th,command).value_or(lcs::StepResult::unsupported);};
    const auto result=vm.step(t,0);return {result,t.locals[0]};
}
int main(){try{
    lcs::ScriptCoronas bridge;bool available=true;int ground=0;
    bridge.available=[&]{return available;};
    bridge.ground=[&](float x,float y)->std::optional<float>{check(x==10 && y==20,"Ground query receives original XY");++ground;return 33;};
    std::vector<std::pair<uintptr_t,lcs::ScriptCorona>> draws;
    bridge.render=[&](uintptr_t id,const lcs::ScriptCorona& c){draws.emplace_back(id,c);return true;};
    const std::vector<int32_t> args{bits(10),bits(20),bits(-100),bits(2.5),258,256,-1,257,258};
    available=false;check(command(bridge,COMMAND_CREATE_SCRIPT_CORONA,args,true).status==lcs::StepResult::unsupported && ground==0,"Unavailable renderer refuses before ground and source output");
    available=true;const auto first=command(bridge,COMMAND_CREATE_SCRIPT_CORONA,args,true);
    check(first.status==lcs::StepResult::continued && ground==1 && draws.empty(),"Creation yields source handle, draw occurs in frame phase");
    check(bridge.tick() && draws.size()==1,"Persistent emitter submits in frame phase");
    const auto identity=draws[0].first;const auto c=draws[0].second;
    check(c.position[2]==33 && c.size==2.5f && c.type==2 && c.flare==0 && c.r==255 && c.g==1 && c.b==2,"Retail ground boundary, size, byte type/flare/RGB retained");
    check(bridge.tick() && draws.back().first==identity,"Emitter identity remains stable across frames");
    const auto second=command(bridge,COMMAND_CREATE_SCRIPT_CORONA,args,true);
    check(second.id==first.id+1 && bridge.tick() && draws[draws.size()-2].second.id==uint32_t(second.id),"IDs increment and head insertion preserves retail draw order");
    check(command(bridge,COMMAND_REMOVE_SCRIPT_CORONA,{first.id}).status==lcs::StepResult::continued,"Remove source emitter");
    draws.clear();check(bridge.tick() && draws.size()==1 && draws[0].second.id==uint32_t(second.id),"Removed emitter is no longer refreshed; unrelated emitter survives");
    command(bridge,COMMAND_REMOVE_SCRIPT_CORONA,{first.id});command(bridge,COMMAND_REMOVE_SCRIPT_CORONA,{-900});
    check(bridge.tick(),"Repeated/unknown removal follows original list search without invented failure");
    const auto third=command(bridge,COMMAND_CREATE_SCRIPT_CORONA,args,true);draws.clear();check(bridge.tick(),"Replacement frame");
    check(draws[0].first!=identity && third.id==second.id+1,"Retired renderer identity cannot be recycled while native fade is pending");
    bridge.render=[](uintptr_t,const lcs::ScriptCorona&){return false;};check(!bridge.tick(),"Backend refusal is observable, not successful rendering");
    bridge.clear();check(bridge.tick(),"Campaign shutdown stops refresh of all owned emitters");
    lcs::ScriptCoronas restarted;restarted.available=[](){return true;};restarted.render=[](uintptr_t,const lcs::ScriptCorona&){return true;};restarted.ground=bridge.ground;
    const auto after=command(restarted,COMMAND_CREATE_SCRIPT_CORONA,args,true);check(after.id==third.id+1,"Script Init does not reset original process-global ID counter");
    std::cout<<"PASS: original script corona lifetime, output, ground, byte narrowing, stable IDs and frame refusal; no game/rendering executed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
