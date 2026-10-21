#include "lcs/script_markers.hpp"
#include "lcs/engine_dispatch.hpp"
#include "ScriptCommands.h"
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int32_t bits(float f){int32_t v;std::memcpy(&v,&f,4);return v;}
void number(std::vector<uint8_t>& b,int32_t v){b.push_back(6);for(int i=0;i<4;++i)b.push_back(uint8_t(uint32_t(v)>>(8*i)));}
int main(){try{
    lcs::ScriptMarkers::ProcessState process;
    lcs::ScriptMarkers bridge(process),other(process);
    bool ready=true,accept=true;size_t ground=0;
    std::vector<lcs::ScriptMarkerCommand> requests;
    bridge.available=[&]{return ready;};
    bridge.ground=[&](float x,float y)->std::optional<float>{check(x==10 && y==20,"Ground input XY");++ground;return 33.f;};
    bridge.submit=[&](const auto& r){requests.push_back(r);return accept;};
    other.available=bridge.available;other.ground=bridge.ground;other.submit=bridge.submit;
    std::vector<uint8_t> code(64);
    const auto point=uint32_t(code.size());
    code.push_back(uint8_t(COMMAND_ADD_POINT_3D_MARKER));code.push_back(uint8_t(COMMAND_ADD_POINT_3D_MARKER>>8));
    code.push_back(12); // first coordinate is also the address identity
    for(auto v:{bits(20),bits(-100),bits(10),-1,257,258})number(code,v);
    const auto arrow=uint32_t(code.size());
    code.push_back(uint8_t(COMMAND_ADD_ARROW_3D_MARKER));code.push_back(uint8_t(COMMAND_ADD_ARROW_3D_MARKER>>8));
    code.push_back(12);
    for(auto v:{bits(20),bits(-100),bits(50),bits(60),bits(70),256,-2,259,bits(2)})number(code,v);
    const auto zero_point=uint32_t(code.size());
    code.push_back(uint8_t(COMMAND_ADD_POINT_3D_MARKER));code.push_back(uint8_t(COMMAND_ADD_POINT_3D_MARKER>>8));
    code.push_back(12);for(auto v:{bits(0),bits(0),bits(1),1,2,3})number(code,v);
    lcs::Vm vm(code,64,64);lcs::Thread t;t.locals[0]=bits(10);
    lcs::ScriptMarkers* selected=&bridge;
    vm.backend=[&](auto& v,auto& thread,uint16_t op){return lcs::try_engine_adapter(*selected,v,thread,op).value_or(lcs::StepResult::unsupported);};
    auto run=[&](uint32_t ip){t.ip=ip;return vm.step(t,0);};
    ready=false;check(run(point)==lcs::StepResult::unsupported && ground==0 && requests.empty(),"Unavailable renderer has no ground or marker effects");
    ready=true;check(run(point)==lcs::StepResult::continued && ground==1 && requests.size()==1,"Point producer submits one whole operation");
    auto r=requests.back();
    check(r.placement.identity==reinterpret_cast<uintptr_t>(&t.locals[0]) && r.placement.identity!=uintptr_t(uint32_t(t.locals[0])),"Identity is source variable address, never coordinate value");
    check(r.copy_before && r.copy_before->source_identity==r.placement.identity && r.copy_before->new_identity==r.placement.identity-1 && r.copy_before->force_position && r.copy_before->persistent_fade,"Copy precedes placement with original ID minus one and both flags");
    check(r.placement.type==5 && r.placement.size==7.f && r.placement.position[2]==33 && r.placement.pulse_period==128 && r.placement.pulse_fraction==0 && r.placement.vertical_size==100 && r.placement.rotate_rate==1 && r.placement.force_position && !r.placement.persistent_fade,"Point raw ABI and ground boundary");
    check(r.placement.colour==std::array<uint8_t,4>{255,1,2,255} && !r.placement.target,"Point colours narrow to bytes");
    selected=&other;check(run(point)==lcs::StepResult::continued && !requests.back().copy_before,"Previous point is shared across adapters/campaigns, not per marker or thread");
    check(run(arrow)==lcs::StepResult::continued && ground==2,"Arrow never applies point ground sentinel");
    r=requests.back();check(!r.copy_before && r.placement.type==2 && r.placement.size==3.2f && r.placement.pulse_fraction==1 && r.placement.vertical_size==0 && r.placement.pulse_period==1 && r.placement.colour==std::array<uint8_t,4>{0,254,3,200},"Arrow raw ABI and colours");
    check(r.placement.position==std::array<float,3>{10,20,-91} && *r.placement.target==std::array<float,3>{50,60,70} && !r.placement.force_position,"Arrow adds height plus seven and retains WORLD target without normalization");
    check(process.previous_point==std::array<float,3>{10,20,33},"Arrow does not replace point history");
    t.locals[0]=bits(12);accept=false;check(run(arrow)==lcs::StepResult::unsupported && t.ip==arrow,"Backend refusal observable and VM restores instruction boundary");
    check(process.previous_point==std::array<float,3>{10,20,33},"Refused operation cannot change history");
    t.locals[0]=bits(std::numeric_limits<float>::infinity());const auto before=requests.size();
    check(run(arrow)==lcs::StepResult::unsupported && requests.size()==before,"Invalid float rejected before submission");
    accept=true;t.locals[0]=0;process.previous_point={};check(run(zero_point)==lcs::StepResult::continued && !requests.back().copy_before,"Initial zero previous point does not invent a copy");
    // Alias identity must follow decoded storage, including local bases/arrays.
    vm.memory.insert(vm.memory.end(),{204,2,230,2,3,4,108,3,4,10,11,107,6,0,0,0,0});
    vm.load_mission(std::vector<uint8_t>(vm.memory.begin()+64,vm.memory.end()));
    const uint32_t refs=uint32_t(code.size());
    t.locals_base=5;t.locals[8]=1;
    t.ip=refs;const auto global=vm.peek_variable_identity(t);
    check(global==reinterpret_cast<uintptr_t>(vm.memory.data()+8) && t.ip==refs,"Global peek preserves operand cursor");
    t.ip=refs+2;check(vm.peek_variable_identity(t)==reinterpret_cast<uintptr_t>(vm.memory.data()+12),"Indexed global resolves current array element");
    t.ip=refs+6;check(vm.peek_variable_identity(t)==reinterpret_cast<uintptr_t>(&t.locals[6]),"Indexed local honours frame base");
    t.ip=refs+9;check(vm.peek_variable_identity(t)==reinterpret_cast<uintptr_t>(&t.locals[104]),"Timer A identity");
    t.ip=refs+10;check(vm.peek_variable_identity(t)==reinterpret_cast<uintptr_t>(&t.locals[105]),"Timer B identity");
    for(auto invalid:{refs+11,refs+12}){t.ip=invalid;bool failed=false;try{vm.peek_variable_identity(t);}catch(const std::exception&){failed=true;}check(failed && t.ip==invalid,"Invalid reference rejected without consuming cursor");}
    std::cout<<"PASS: marker writers: original variable identities, point/arrow ABI, shared history, ground, copy transaction and refusal; isolated writer contract\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
