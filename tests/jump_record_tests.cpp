#include "lcs/native.hpp"
#include "ScriptCommands.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <set>
#include <stdexcept>
using namespace lcs;
static void check(bool condition,const std::string& message){if(!condition)throw std::runtime_error(message);}
static int32_t bits(float value){int32_t result;std::memcpy(&result,&value,4);return result;}
static void integer(std::vector<uint8_t>& bytes,int32_t value){bytes.push_back(6);for(int i=0;i<4;++i)bytes.push_back(uint8_t(uint32_t(value)>>(i*8)));}
int main(int argc,char** argv){try {
    check(argc==2,"Original LCS script required");
    const auto image=ScmImage::load(argv[1]);NativeAdapter native(image);native.world_ready=true;native.text_ready=true;
    std::vector<NativeCall> history;
    native.execute=[&](const NativeCall& call){history.push_back(call);NativeResult result;result.executed=true;return result;};
    const uint16_t commands[]={COMMAND_REGISTER_JUMP_DISTANCE,COMMAND_REGISTER_JUMP_HEIGHT,COMMAND_REGISTER_JUMP_FLIPS,COMMAND_REGISTER_JUMP_SPINS,COMMAND_REGISTER_JUMP_STUNT};
    auto run=[&](uint16_t opcode,int32_t value){std::vector<uint8_t> bytes(64);bytes.push_back(uint8_t(opcode));bytes.push_back(uint8_t(opcode>>8));integer(bytes,value);Vm vm(std::move(bytes),64,64);vm.backend=[&](Vm& v,Thread& t,uint16_t op){return native.dispatch(v,t,op);};Thread t;t.ip=64;return vm.step(t,0);};
    for(int i=0;i<5;++i){const auto value=i<2?bits(23.75f):7;check(run(commands[i],value)==StepResult::continued,"Every original jump-record command executes");check(history.back().opcode==(i<2?0x0628:0x0627) && history.back().inputs==std::vector<int32_t>({139+i,value}),"Real native jump stats use the correct identity and float/int representation");}
    for(int i=0;i<5;++i)check(run(commands[i],i<2?bits(10.5f):3)==StepResult::continued,"Lower records are valid updates");
    check(native.jump_records().distance==23.75f && native.jump_records().height==23.75f && native.jump_records().flips==7 && native.jump_records().spins==7 && native.jump_records().best_stunt==7,"Full-precision original records never fall after a smaller jump");
    const auto before=history.size();check(run(commands[0],0x7FC00000)==StepResult::unsupported && history.size()==before,"Non-finite distance cannot enter native stats");
    native.execute=[](const NativeCall&){return NativeResult{};};check(run(commands[0],bits(99.5f))==StepResult::unsupported && native.jump_records().distance==23.75f,"A failed native record neither pretends to succeed nor changes original state");
    // Reproduce the actual user's stopped thread at ScriptSpace 204601, using
    // the untouched original instructions. Only engine responses are simulated.
    for(bool metres:{false,true}) {
    NativeAdapter reproduced(image);reproduced.world_ready=reproduced.text_ready=true;
    reproduced.player_made_safe=[]()->std::optional<bool>{return false;};
    std::set<int32_t> recorded;std::vector<uint16_t> original_commands;size_t rewards=0,feedback=0;
    reproduced.display_message=[&](const MissionMessage&){++feedback;return true;};
    reproduced.execute=[&](const NativeCall& call){NativeResult result;result.executed=true;result.condition=call.opcode==0x0424 && metres;if(call.opcode==0x0627 || call.opcode==0x0628)recorded.insert(call.inputs[0]);if(call.opcode==0x0109){check(call.inputs[1]>0,"Original stunt issues a positive native reward");++rewards;}if(call.text && call.text->find("HJ_")==0)++feedback;if(call.opcode==0x042D)result.values[0]=int32_t(float(call.inputs[0])*3.28084f);return result;};
    Vm vm(image);vm.backend=[&](Vm& v,Thread& t,uint16_t op){original_commands.push_back(op);return reproduced.dispatch(v,t,op);};
    // Height and distance retain full precision plus their original truncated
    // integer parts, which this sequence uses to calculate fractional feedback.
    Thread t;t.ip=204601;t.locals[22]=bits(73.25f);t.locals[21]=bits(8.5f);t.locals[10]=73;t.locals[13]=2;t.locals[17]=3;t.locals[31]=1;t.locals[35]=500;
    check(read16(vm.memory,t.ip)==COMMAND_REGISTER_JUMP_DISTANCE,"Real reported stop matches original bytecode, without changing SCM");
    for(size_t i=0;i<100 && recorded.size()<4;++i){const auto result=vm.step(t,0);check(result!=StepResult::unsupported && result!=StepResult::failed,"Original jump sequence blocked at "+std::to_string(t.last_ip)+" "+t.diagnostic);}
    check(recorded.count(139) && recorded.count(140) && recorded.count(141) && recorded.count(142),"Original background script gets past all four physical jump records");
    check(reproduced.jump_records().distance==73.25f && reproduced.jump_records().height==8.5f,"Real original local variables reach full-precision jump statistics");
    StepResult result=StepResult::continued;
    for(size_t i=0;i<1000 && result==StepResult::continued;++i){result=vm.step(t,0);check(result!=StepResult::unsupported && result!=StepResult::failed,"Original stunt feedback blocked at "+std::to_string(t.last_ip)+" "+t.diagnostic);}
    check(result==StepResult::yielded && recorded.count(143) && rewards==1 && feedback>0,"Original measurement branch must finish stunt/reward/feedback and yield: metres="+std::to_string(metres)+" result="+std::to_string(int(result))+" ip="+std::to_string(t.last_ip)+" best="+std::to_string(recorded.count(143))+" rewards="+std::to_string(rewards)+" feedback="+std::to_string(feedback));
    }
    check(run(commands[4],9)==StepResult::unsupported,"Backend failure remains explicit; regression is fixed without bypassing missing commands");
    std::cout<<"PASS: all five original jump records, real SA record IDs/types, maximum semantics, rejected backend/non-finite failures, untouched original sequence at user stop 204601 through reward/feedback/yield in metres and feet; gameExecuted=false\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
