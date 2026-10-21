#include "lcs/continuous_sound_adapter.hpp"
#include "lcs/continuous_audio_entities.hpp"
#include <iostream>
#include <stdexcept>
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct Result {lcs::StepResult status;int32_t value;};
static Result command(lcs::ContinuousSoundAdapter& service,uint16_t opcode,std::initializer_list<int32_t> args,bool output=true){
    std::vector<uint8_t> code(64);code.push_back(uint8_t(opcode));code.push_back(uint8_t(opcode>>8));
    for(auto value:args){code.push_back(6);for(int i=0;i<4;++i)code.push_back(uint8_t(uint32_t(value)>>(8*i)));}
    if(output)code.push_back(12);
    lcs::Vm vm(code,64,64);lcs::Thread thread;thread.ip=64;thread.locals[0]=-99;
    vm.backend=[&](lcs::Vm& v,lcs::Thread& t,uint16_t op){return service.dispatch(v,t,op);};
    return {vm.step(thread,0),thread.locals[0]};
}
int main(){try{
    lcs::ContinuousSoundAdapter service;int creates=0,releases=0;bool fail_create=false,entered=true,fail_release=false;
    service.available=[]{return true;};
    service.create=[&](const auto& object){
        ++creates;check(object.object.sound==2 && object.object.audio_entity==-5,"Retail narrowing and constructor sentinel reach actual backend");
        return lcs::ContinuousEntityReceipt{entered,!fail_create,73};
    };
    service.release=[&](const auto&){++releases;return lcs::ContinuousEntityReceipt{true,!fail_release,73};};
    check(command(service,402,{0,0,0,0x10002},false).status!=lcs::StepResult::continued && !creates,
          "Malformed output is rejected before backend entry");
    check(command(service,402,{0x7F800000,0,0,0x10002}).status==lcs::StepResult::unsupported && !creates,
          "Nonfinite source coordinates cannot enter the backend");
    const auto first=command(service,402,{0,0,0,0x10002});
    check(first.status==lcs::StepResult::continued && first.value==1 && creates==1,"Actual create receipt publishes original pool reference");
    check(service.objects().find(first.value)->object.audio_entity==73,"Backend entity is distinct from script reference");
    fail_release=true;
    check(command(service,403,{first.value}).status==lcs::StepResult::unsupported && service.quarantined(),"Entered release failure quarantines live object");
    check(command(service,402,{0,0,0,0x10002}).status==lcs::StepResult::unsupported && creates==1,"Uncertain ownership blocks replay");
    check(!service.recover() && service.objects().find(first.value).has_value(),"Failed recovery retains original receipt");
    fail_release=false;check(service.recover() && !service.objects().find(first.value),"Accepted recovery retires pool generation");
    const auto before=releases;
    check(command(service,403,{first.value}).status==lcs::StepResult::continued && releases==before,"Retail stale reference cannot destroy a replacement");
    fail_create=true;entered=false;
    check(command(service,402,{0,0,0,0x10002}).value==-99 && !service.quarantined(),"Rejected pre-entry create leaves output untouched");
    entered=true;
    check(command(service,402,{0,0,0,0x10002}).status==lcs::StepResult::unsupported && service.quarantined(),"Uncertain entered create is retained for backend resolution");
    check(service.recover(),"Backend explicitly resolves uncertain creation by full token");
    fail_create=false;
    const auto next=command(service,402,{0,0,0,0x10002});
    check(next.status==lcs::StepResult::continued && command(service,403,{next.value}).status==lcs::StepResult::continued,
          "Recovered adapter creates and releases through real callbacks");
    service.create=[&](const auto&)->lcs::ContinuousEntityReceipt{++creates;throw std::runtime_error("Injected entered backend exception");};
    check(command(service,402,{0,0,0,2}).status!=lcs::StepResult::continued && service.quarantined(),
          "Backend exception retains its full owner token for recovery");
    check(service.recover(),"Exception path is explicitly recovered without replaying creation");
    service.create=[&](const auto&){++creates;return lcs::ContinuousEntityReceipt{true,true,creates};};
    std::vector<int32_t> references;
    for(size_t i=0;i<lcs::ContinuousSoundObjects::capacity;++i){
        const auto value=command(service,402,{0,0,0,2});
        check(value.status==lcs::StepResult::continued,"Each capacity slot requires an actual accepted entity");references.push_back(value.value);
    }
    const auto capacity_creates=creates;
    check(command(service,402,{0,0,0,2}).status==lcs::StepResult::unsupported && creates==capacity_creates,
          "Exhausted source pool rejects before creating a backend orphan");
    for(auto reference:references)check(command(service,403,{reference}).status==lcs::StepResult::continued,"All actual entity receipts released");
    lcs::ContinuousAudioEntities entities;
    lcs::ContinuousSoundAdapter bound;
    bound.available=[]{return true;};bound.create=[&](const auto& object){return entities.create(object);};
    bound.release=[&](const auto& object){return entities.release(object);};
    const auto a=command(bound,402,{0,0,0,2}),b=command(bound,402,{0,0,0,11});
    check(a.value==1 && b.value==257 && entities.service_order()[0].object.audio_entity==0 &&
          entities.service_order()[1].object.audio_entity==1,"Actual source allocator returns independent first-free audio identities");
    const auto ancient=entities.service_order()[0];
    check(command(bound,403,{a.value}).status==lcs::StepResult::continued,"Release compacts actual source service order");
    const auto c=command(bound,402,{0,0,0,51});const auto order=entities.service_order();
    check(order.size()==2 && order[0].token.reference==b.value && order[1].token.reference==c.value &&
          order[1].object.audio_entity==0,"Reused low audio slot is serviced after existing entry, not sorted by slot");
    check(!entities.release(ancient).accepted && !entities.create(order[1]).accepted,
          "Ancient owner cannot release replacement; duplicate creation cannot allocate another identity");
    bound.music_available=[](){return true;};
    const auto music=command(bound,402,{0,0,0,40});
    check(music.status==lcs::StepResult::continued && bound.objects().find(music.value),
          "A bound real CITY consumer permits a configuration entity and publishes its source handle");
    check(command(bound,403,{music.value},false).status==lcs::StepResult::continued,
          "CITY configuration objects retain the same checked removal contract");
    std::cout<<"PASS: script destination preflight, actual entity receipts, stale references, quarantine and checked recovery; no runtime registration/device\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
