#include "lcs/native.hpp"
#include "ScriptCommands.h"
#include <iostream>
#include <stdexcept>
static void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
static void integer(std::vector<uint8_t>& bytes,int32_t value){bytes.push_back(6);for(int i=0;i<4;++i)bytes.push_back(uint8_t(uint32_t(value)>>(i*8)));}
int main(){try {
    std::array<lcs::NativeMessage,8> queue{};
    for(size_t i=0;i<8;++i){queue[i].text=uint32_t(100+i);queue[i].duration=uint32_t(1000+i);queue[i].numbers[0]=int32_t(i);}
    lcs::NativeMessage next;next.text=200;next.duration=8000;next.created_at=12345;next.numbers={{1,-2,3,-4,5,-6}};
    check(lcs::insert_mission_message(queue.data(),next,true)==1,"Soon waits for the active message");
    check(queue[0].text==100 && queue[0].duration==1000 && queue[0].numbers[0]==0,"Soon preserves the active message and its timing/substitutions");
    check(queue[1].text==200 && queue[1].numbers[5]==-6 && queue[1].created_at==12345,"Soon retains all original numeric substitutions and timing");
    check(queue[2].text==101 && queue[7].text==106,"Soon pushes pending messages back and drops only the final full-queue entry");
    next.text=201;lcs::insert_mission_message(queue.data(),next,true);
    check(queue[1].text==201 && queue[2].text==200 && queue[3].text==101,"Repeated Soon messages preserve original insertion order");
    lcs::insert_mission_message(queue.data(),next,false);
    check(queue[0].text==201 && queue[1].text==201 && queue[2].text==200,"Now replaces only the active message");
    const auto full=queue;next.text=300;
    check(lcs::insert_mission_message(queue.data(),next,false,true)==8,"Queued messages are discarded when the original queue is full");
    for(size_t i=0;i<8;++i)check(queue[i].text==full[i].text && queue[i].numbers==full[i].numbers,"Full-queue append preserves every pending objective");
    queue[6]={};check(lcs::insert_mission_message(queue.data(),next,false,true)==6 && queue[6].text==300 && queue[0].text==201,"Queued message fills the first unused slot without changing priority");
    next.text=201;
    queue={};check(lcs::insert_mission_message(queue.data(),next,true)==0 && queue[0].text==201,"Soon shows immediately when the queue is empty");
    const uint16_t soon[]={COMMAND_PRINT_SOON,COMMAND_PRINT_WITH_NUMBER_SOON,COMMAND_PRINT_WITH_2_NUMBERS_SOON,COMMAND_PRINT_WITH_3_NUMBERS_SOON,COMMAND_PRINT_WITH_4_NUMBERS_SOON,COMMAND_PRINT_WITH_5_NUMBERS_SOON,COMMAND_PRINT_WITH_6_NUMBERS_SOON};
    const uint16_t now[]={COMMAND_PRINT_WITH_3_NUMBERS_NOW,COMMAND_PRINT_WITH_4_NUMBERS_NOW,COMMAND_PRINT_WITH_5_NUMBERS_NOW,COMMAND_PRINT_WITH_6_NUMBERS_NOW};
    lcs::ScmImage image;lcs::NativeAdapter adapter(image);adapter.text_ready=true;
    lcs::MissionMessage received;size_t displays=0;
    adapter.display_message=[&](const lcs::MissionMessage& message){received=message;++displays;return true;};
    auto run=[&](uint16_t op,int count){std::vector<uint8_t> bytes(64);bytes.push_back(uint8_t(op));bytes.push_back(uint8_t(op>>8));for(char c:std::string("vic2_1\0\0",8))bytes.push_back(uint8_t(c));for(int i=0;i<count;++i)integer(bytes,i%2?-i-10:i+10);integer(bytes,-1);integer(bytes,0x10002);lcs::Vm vm(std::move(bytes),64,64);vm.backend=[&](lcs::Vm& v,lcs::Thread& t,uint16_t code){return adapter.dispatch(v,t,code);};lcs::Thread t;t.ip=64;return vm.step(t,0);};
    for(int n=0;n<=6;++n){check(run(soon[n],n)==lcs::StepResult::continued,"Original Soon family decodes");check(received.soon && received.key=="VIC2_1" && received.duration==UINT32_MAX && received.flags==2,"Original message key/duration/16-bit flags retain their semantics");for(int i=0;i<6;++i)check(received.numbers[i]==(i<n?(i%2?-i-10:i+10):-1),"Duration/style cannot be mistaken for a numeric substitution");}
    for(int n=3;n<=6;++n){check(run(now[n-3],n)==lcs::StepResult::continued && !received.soon,"Original 3-6 number Now family decodes");}
    check(run(COMMAND_PRINT_WITH_NUMBER_NOW_NO_BRIEF,1)==lcs::StepResult::continued && !received.soon && !received.previous_brief && received.numbers[0]==10,"No-brief numeric Now retains rendering/substitution while suppressing previous briefs");
    check(run(COMMAND_PRINT_WITH_2_NUMBERS_NOW_NO_BRIEF,2)==lcs::StepResult::continued && !received.previous_brief && received.numbers[1]==-11,"Two-number no-brief Now keeps both original substitutions");
    check(run(soon[0],0)==lcs::StepResult::continued && received.previous_brief,"No-brief message policy is per request and cannot leak to the next message");
    adapter.text_ready=false;const auto before=displays;
    check(run(soon[0],0)==lcs::StepResult::unsupported && displays==before,"Unmounted GXT prevents native message writes");
    adapter.text_ready=true;adapter.display_message=[](const lcs::MissionMessage&){return false;};
    check(run(soon[0],0)==lcs::StepResult::unsupported,"A renderer failure blocks the original script");
    adapter.world_ready=true;lcs::NativeCall lane;
    adapter.execute=[&](const lcs::NativeCall& call){lane=call;lcs::NativeResult result;result.executed=true;return result;};
    const uint16_t queued[]={COMMAND_PRINT_WITH_2_NUMBERS,COMMAND_PRINT_WITH_3_NUMBERS,COMMAND_PRINT_WITH_4_NUMBERS,COMMAND_PRINT_WITH_5_NUMBERS,COMMAND_PRINT_WITH_6_NUMBERS};
    const uint16_t native_queued[]={0x02FC,0x02FF,0x0302,0x0305,0x0308};
    adapter.display_message=[&](const lcs::MissionMessage& message){received=message;++displays;return true;};
    for(int n=2;n<=6;++n){check(run(queued[n-2],n)==lcs::StepResult::continued,"Original queued numeric messages execute");if(n==2 || n==5){check(received.queued && !received.soon && received.key=="VIC2_1" && received.duration==UINT32_MAX && received.flags==2,"Unsupported native 2/5-number operations use the original append policy");for(int i=0;i<6;++i)check(received.numbers[size_t(i)]==(i<n?(i%2?-i-10:i+10):-1),"Queued renderer retains all original substitutions");}else{check(lane.opcode==native_queued[n-2] && lane.text=="VIC2_1" && lane.text_position==0 && lane.inputs.size()==size_t(n+2),"Native queued message keeps its text and every numeric operand");for(int i=0;i<n;++i)check(lane.inputs[size_t(i)]==(i%2?-i-10:i+10),"Native queued numeric substitutions retain order");check(lane.inputs[size_t(n)]==-1 && lane.inputs[size_t(n+1)]==0x10002,"Native queued duration and style follow all substitutions");}}
    for(int flag:{0,1,17}){std::vector<uint8_t> bytes(64);const auto op=COMMAND_SET_CAR_CHANGE_LANE;bytes.push_back(uint8_t(op));bytes.push_back(uint8_t(op>>8));integer(bytes,777);integer(bytes,flag);lcs::Vm vm(std::move(bytes),64,64);vm.backend=[&](lcs::Vm& v,lcs::Thread& t,uint16_t code){return adapter.dispatch(v,t,code);};lcs::Thread t;t.ip=64;check(vm.step(t,0)==lcs::StepResult::continued && lane.opcode==0x0466 && lane.inputs==std::vector<int32_t>({777,flag?0:1}),"LCS lane-change permission reverses native stay-fast-lane and preserves the vehicle handle");}
    for(int wheel:{0,1,2,3,4})for(bool burst:{false,true}) {
        adapter.execute=[&](const lcs::NativeCall& call){lane=call;lcs::NativeResult result;result.executed=true;result.condition=burst;return result;};
        std::vector<uint8_t> bytes(64);const auto op=COMMAND_IS_CAR_TYRE_BURST|0x8000;bytes.push_back(uint8_t(op));bytes.push_back(uint8_t(op>>8));integer(bytes,777);integer(bytes,wheel);
        lcs::Vm vm(std::move(bytes),64,64);vm.backend=[&](lcs::Vm& v,lcs::Thread& t,uint16_t code){return adapter.dispatch(v,t,code);};lcs::Thread t;t.ip=64;
        check(vm.step(t,0)==lcs::StepResult::continued && t.condition==!burst && lane.opcode==0x0496 && lane.predicate && lane.inputs==std::vector<int32_t>({777,wheel}),"Native wheel query preserves original selectors/vehicle and uses the real predicate with LCS NOT semantics");
    }
    {
        lcs::BigMissionMessage received_big;size_t displays=0;bool accepted=true,entered=false;
        adapter.world_ready=adapter.text_ready=true;
        adapter.display_big_message=[&](const lcs::BigMissionMessage& r){received_big=r;++displays;lcs::NativeResult result;result.executed=accepted;result.native_entered=entered;return result;};
        auto make_big=[](int32_t style){
            std::vector<uint8_t> bytes(64);const auto op=COMMAND_PRINT_WITH_3_NUMBERS_BIG;
            bytes.push_back(uint8_t(op));bytes.push_back(uint8_t(op>>8));
            for(char c:std::string("vic2_1\0\0",8))bytes.push_back(uint8_t(c));
            for(int32_t n:{INT32_MIN,20000001,INT32_MAX,-1,style})integer(bytes,n);
            return lcs::Vm(std::move(bytes),64,64);
        };
        for(int32_t style:{1,2,3,4,5,6,7,65537}) {
            auto vm=make_big(style);vm.backend=[&](lcs::Vm& v,lcs::Thread& t,uint16_t op){return adapter.dispatch(v,t,op);};
            lcs::Thread t;t.ip=64;
            check(vm.step(t,0)==lcs::StepResult::continued && t.ip==vm.memory.size() && received_big.key=="VIC2_1" &&
                received_big.duration==UINT32_MAX && received_big.style==uint16_t(uint16_t(style)-1) &&
                received_big.numbers==std::array<int32_t,6>{{INT32_MIN,20000001,INT32_MAX,-1,-1,-1}},
                "Big message decodes three independent integers before full duration and narrowed source style");
        }
        auto vm=make_big(1);vm.backend=[&](lcs::Vm& v,lcs::Thread& t,uint16_t op){return adapter.dispatch(v,t,op);};
        for(size_t cut=1;cut<=33;++cut) {
            auto truncated=make_big(1);truncated.memory.resize(truncated.memory.size()-cut);truncated.backend=vm.backend;
            const auto before=displays;lcs::Thread t;t.ip=64;
            check(truncated.step(t,0)==lcs::StepResult::failed && t.ip==64 && displays==before,"Any truncated big label/operand refuses before a display mutation");
        }
        for(int32_t style:{0,8,-1,65536}) {
            auto invalid=make_big(style);invalid.backend=vm.backend;const auto before=displays;lcs::Thread t;t.ip=64;
            check(invalid.step(t,0)==lcs::StepResult::unsupported && t.ip==64 && displays==before,"Source style outside native storage cannot be forced to an invented valid style");
        }
        lcs::Thread rejected;rejected.ip=64;accepted=false;
        check(vm.step(rejected,0)==lcs::StepResult::unsupported && rejected.ip==64,"Pre-entry big-message rejection keeps the original instruction");
        entered=true;lcs::Thread partial;partial.ip=64;
        check(vm.step(partial,0)==lcs::StepResult::failed && partial.ip==64 && partial.diagnostic.find("replay rejected after entry")!=std::string::npos,"Entered big-message failure requires repair instead of unsafe replay or a skipped instruction");
        accepted=true;entered=false;adapter.text_ready=false;const auto before=displays;lcs::Thread unavailable;unavailable.ip=64;
        check(vm.step(unavailable,0)==lcs::StepResult::unsupported && displays==before,"Unmounted campaign text cannot display a numbered big message");
    }
    std::cout<<"PASS: original message queue priority, full-queue eviction, 17 numeric/text signatures, guarded three-number big messages, blocked/entered renderer failures, lane permission inversion and tyre selectors; gameExecuted=false\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
