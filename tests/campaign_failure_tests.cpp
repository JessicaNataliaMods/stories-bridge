#include "lcs/native.hpp"
#include "lcs/ped_flags.hpp"
#include "ScriptCommands.h"
#include <iostream>
#include <set>
#include <stdexcept>
using namespace lcs;
static void check(bool value,const std::string& text){if(!value)throw std::runtime_error(text);}
static void integer(std::vector<uint8_t>& bytes,int32_t value){bytes.push_back(6);for(int i=0;i<4;++i)bytes.push_back(uint8_t(uint32_t(value)>>(i*8)));}
int main(int argc,char** argv){try {
    check(argc==2,"Original SCM required");const auto image=ScmImage::load(argv[1]);NativeAdapter adapter(image);adapter.world_ready=true;
    uint8_t flags=0xB7;size_t changes=0;bool available=true;
    adapter.set_stationary_shooter=[&](int32_t actor,bool enabled){if(!available || actor!=777)return false;++changes;stationary_shooter_flags(flags,enabled);return true;};
    NativeCall received;bool execute_ok=true;int32_t next_handle=20;std::set<std::pair<uint16_t,int32_t>> removed;
    adapter.execute=[&](const NativeCall& call){received=call;NativeResult result;result.executed=execute_ok;if(execute_ok){if(call.outputs)result.values[0]=next_handle++;if(call.opcode==0x0164 || call.opcode==0x03BD)removed.insert({call.opcode,call.inputs[0]});}return result;};
    auto run=[&](uint16_t op,const std::vector<int32_t>& args){std::vector<uint8_t> code(64);code.push_back(uint8_t(op));code.push_back(uint8_t(op>>8));for(auto value:args)integer(code,value);if(op==COMMAND_ADD_SPHERE || op==COMMAND_ADD_BLIP_FOR_COORD)code.push_back(12);Vm vm(std::move(code),64,64);vm.backend=[&](Vm& v,Thread& t,uint16_t command){return adapter.dispatch(v,t,command);};Thread t;t.ip=64;const auto result=vm.step(t,0);if(result==StepResult::unsupported)check(t.ip==64,"Missing backend retains the blocked original instruction");return result;};
    for(int value=0;value<256;++value)for(bool enabled:{false,true}){auto field=uint8_t(value);stationary_shooter_flags(field,enabled);check((field&0xB7)==(uint8_t(value)&0xB7) && bool(field&8)==enabled && bool(field&64)==enabled,"Shooter policy preserves every unrelated native flag, including stay-in-place");}
    for(auto enabled:{0,1,17})check(run(COMMAND_SET_CHAR_STOP_SHOOT_DONT_SEEK_ENTITY,{777,enabled})==StepResult::continued && bool(flags&0x48)==bool(enabled),"Original boolean shooter policy reaches the correct native actor");
    const auto before=changes;available=false;check(run(COMMAND_SET_CHAR_STOP_SHOOT_DONT_SEEK_ENTITY,{777,1})==StepResult::unsupported && changes==before,"Missing native actor backend blocks explicitly");available=true;
    check(run(COMMAND_SET_CHAR_STOP_SHOOT_DONT_SEEK_ENTITY,{888,1})==StepResult::unsupported && changes==before,"Invalid actor cannot change another character");
    // Reproduce the exact original command in the reported INDAMMU stop.
    Vm original(image);Thread shop;shop.ip=106974;
    check(read16(original.memory,shop.ip)==COMMAND_SET_CHAR_STOP_SHOOT_DONT_SEEK_ENTITY,"Reported stop belongs to the untouched original shop command");
    for(int i=0;i<4;++i)original.memory[7736+i]=uint8_t(uint32_t(777)>>(i*8));
    original.backend=[&](Vm& v,Thread& t,uint16_t command){return adapter.dispatch(v,t,command);};
    check(original.step(shop,0)==StepResult::continued && shop.ip==106980 && (flags&0x48)==0x48,"Real original INDAMMU command advances without native unsupported 0573");
    // The user's failure occurs in a concurrent export monitor, not VIC2.
    Thread importer;importer.name="import";importer.ip=139660;
    check(read16(original.memory,importer.ip)==COMMAND_GET_CAR_MODEL,"Reported IMPORT halt is the untouched original vehicle query");
    auto import_args=importer;import_args.ip+=2;original.store(import_args,777);
    importer.locals=import_args.locals;
    const auto output=import_args;
    const auto before_import=adapter.execute;int32_t traffic_model=400;unsigned identity_logs=0;
    adapter.execute=[&](const NativeCall& call){if(call.opcode!=0x0441)return before_import(call);NativeResult r;r.executed=true;r.values[0]=traffic_model;return r;};
    adapter.native_model_class=[](int32_t id)->std::optional<uint8_t>{return id>=400 && id<=611?6:4;};
    adapter.diagnostic_event=[&](const std::string&){++identity_logs;};
    check(original.step(importer,0)==StepResult::continued && importer.active,"Real IMPORT query no longer halts for an SA car without an LCS equivalent");
    auto read_output=importer;read_output.ip=output.ip;const auto alias=original.parameter(read_output);
    check(alias==native_vehicle_model_base+400 && adapter.model(alias)==400,"IMPORT receives a stable identity that cannot match original export model IDs");
    importer.ip=139660;check(original.step(importer,0)==StepResult::continued && identity_logs==1,"Repeated traffic checks do not flood the diagnostic log");
    traffic_model=346;importer.ip=139660;
    check(original.step(importer,0)==StepResult::unsupported && importer.ip==139660,"Invalid native model retains the original instruction rather than inventing a car");
    adapter.execute=before_import;
    const auto previous_executor=adapter.execute;std::vector<NativeCall> frozen;
    adapter.execute=[&](const NativeCall& call){NativeResult result;result.executed=true;if(call.opcode==0x01F5)result.values[0]=100;else if(call.opcode==0x07AF)result.values[0]=50;else if(call.opcode==0x07FD)result.condition=true;else if(call.opcode==0x092B){check(call.inputs[0]==50 && call.inputs[1]<7,"Follower enumeration excludes leader slot seven");result.values[0]=call.inputs[1]==1?777:call.inputs[1]==6?888:-1;}else if(call.opcode==0x04D7)frozen.push_back(call);else return previous_executor(call);return result;};
    for(auto enabled:{0,1}){frozen.clear();check(run(COMMAND_FREEZE_ALL_PLAYER_FOLLOWERS,{0,enabled})==StepResult::continued && frozen.size()==2 && frozen[0].inputs==std::vector<int32_t>({777,enabled}) && frozen[1].inputs==std::vector<int32_t>({888,enabled}),"Interior transition freezes/thaws all actual sparse followers and preserves the player");}
    adapter.execute=previous_executor;
    check(run(COMMAND_ADD_SPHERE,{0,0,0,0})==StepResult::continued,"Fixture sphere is owned after successful creation");
    check(run(COMMAND_ADD_BLIP_FOR_COORD,{0,0,0})==StepResult::continued,"Fixture blip is owned after successful creation");
    check(run(COMMAND_ADD_SPHERE,{0,0,0,0})==StepResult::continued && run(COMMAND_REMOVE_SPHERE,{22})==StepResult::continued,"Normal cleanup releases ownership");removed.clear();
    execute_ok=false;check(!adapter.release_script_markers() && removed.empty(),"Failed marker cleanup keeps ownership for retry");
    execute_ok=true;check(adapter.release_script_markers() && removed==std::set<std::pair<uint16_t,int32_t>>{{0x03BD,20},{0x0164,21}},"Campaign interruption removes only remaining VM-created markers");
    removed.clear();check(adapter.release_script_markers() && removed.empty(),"Repeated cleanup never removes another mod's markers");
    std::cout<<"PASS: original INDAMMU stop 106974, native shooter flags/actor guards, no unsupported native 0573, owned-marker failure cleanup and retry; gameExecuted=false\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
