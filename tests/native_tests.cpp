#include "lcs/native.hpp"
#include "ScriptCommands.h"
#include <iostream>
#include <stdexcept>
#include <cstring>
#include "lcs/pause_menu.hpp"
void check(bool value,const char* name) { if(!value) throw std::runtime_error(name); }
void opcode(std::vector<uint8_t>& code,uint16_t op) { code.push_back(uint8_t(op));code.push_back(uint8_t(op>>8)); }
void integer(std::vector<uint8_t>& code,int32_t value) { code.push_back(6);for(int i=0;i<4;++i) code.push_back(uint8_t(uint32_t(value)>>(8*i))); }
lcs::Vm program(std::vector<uint8_t> code) { std::vector<uint8_t> bytes(64); bytes.insert(bytes.end(),code.begin(),code.end()); return lcs::Vm(std::move(bytes),64,64); }
int main() {
    try {
        lcs::ScmImage image; image.objects={"","door"}; lcs::NativeAdapter adapter(image);
        {
            lcs::NativeAdapter colours(image);colours.world_ready=true;
            colours.bind_vehicle_colours({127,3,137});
            std::vector<uint8_t> bytes;opcode(bytes,COMMAND_CHANGE_CAR_COLOUR);
            integer(bytes,42);integer(bytes,0);integer(bytes,2);
            auto vm=program(bytes);lcs::NativeCall accepted;size_t entered=0;
            colours.execute=[&](const lcs::NativeCall& call){accepted=call;++entered;lcs::NativeResult out;out.executed=true;return out;};
            vm.backend=[&](lcs::Vm& v,lcs::Thread& t,uint16_t op){return colours.dispatch(v,t,op);};
            lcs::Thread thread;thread.ip=64;
            check(vm.step(thread,0)==lcs::StepResult::continued && entered==1 &&
                accepted.opcode==0x0229 && accepted.inputs==std::vector<int32_t>({42,127,137}),
                "Script vehicle colour indices reach the real SA command through the exact RGB map");
            bytes.clear();opcode(bytes,COMMAND_CHANGE_CAR_COLOUR);integer(bytes,42);integer(bytes,3);integer(bytes,0);
            auto invalid=program(bytes);invalid.backend=vm.backend;thread={};thread.ip=64;
            check(invalid.step(thread,0)==lcs::StepResult::unsupported && entered==1 && thread.ip==64,
                "Unmounted source colour refuses before native vehicle mutation");
        }
        adapter.bind_model(3750,10000,"DOOR");
        check(adapter.model(-1)==10000 && adapter.model(3750)==10000,"negative SCM object and positive model mapping");
        check(!adapter.model(1) && !adapter.model(INT32_MIN),"no accidental SA model identity");
        int calls=0; lcs::NativeCall last;
        adapter.execute=[&](const lcs::NativeCall& call) { ++calls; last=call; lcs::NativeResult result;result.executed=true;result.condition=true;result.values[0]=12345;return result; };
        auto attach=[&](lcs::Vm& vm) { vm.backend=[&](lcs::Vm& v,lcs::Thread& t,uint16_t op){return adapter.dispatch(v,t,op);}; };
        std::vector<uint8_t> code; opcode(code,COMMAND_SET_TOTAL_NUMBER_OF_MISSIONS);integer(code,70);
        auto stat=program(code);attach(stat);lcs::Thread t;t.ip=64;
        check(stat.step(t,0)==lcs::StepResult::continued && last.opcode==0x042C && last.inputs==std::vector<int32_t>{70},"LCS 0431 translates to SA 042C");
        code.clear();opcode(code,COMMAND_CREATE_OBJECT_NO_OFFSET);integer(code,-1);integer(code,0x3f800000);integer(code,0x40000000);integer(code,0x40400000);code.push_back(12);
        auto object=program(code);attach(object);t={};t.ip=64;
        check(object.step(t,0)==lcs::StepResult::unsupported && calls==1 && t.ip==64,"world gate prevents native side effects");
        check(t.diagnostic.find("not mounted")!=std::string::npos,"world diagnostic preserved");
        // Local audit withheld 1586: a field initializer is not proof that
        // SA combat consumes the original LCS script-attack timer semantics.
        {
            lcs::NativeAdapter timer_native(image);timer_native.world_ready=true;
            std::vector<uint8_t> bytes;opcode(bytes,COMMAND_SET_CHAR_ATTACK_TIMER);
            integer(bytes,27);integer(bytes,int32_t(0x87654321u));
            auto timer_vm=program(bytes);size_t timer_calls=0;
            timer_native.execute=[&](const lcs::NativeCall&){++timer_calls;lcs::NativeResult out;out.executed=true;return out;};
            timer_vm.backend=[&](lcs::Vm& v,lcs::Thread& th,uint16_t op){return timer_native.dispatch(v,th,op);};
            lcs::Thread timer_thread;timer_thread.ip=64;
            check(timer_vm.step(timer_thread,0)==lcs::StepResult::unsupported && timer_thread.ip==64 && timer_calls==0,
                  "Unproved 1586 cannot advance source execution or write an unrelated SA timer");
            uint32_t observed_ms=0;int32_t observed_ped=-1;bool timer_accepted=true;
            timer_native.set_char_attack_timer=[&](int32_t ped,uint32_t ms){observed_ped=ped;observed_ms=ms;return timer_accepted;};
            timer_thread={};timer_thread.ip=64;timer_thread.condition=true;
            check(timer_vm.step(timer_thread,0)==lcs::StepResult::continued && timer_thread.ip==76 && timer_thread.condition &&
                  observed_ped==27 && observed_ms==0x87654321u && timer_calls==0,
                  "1586 preserves full unsigned millisecond bits and reaches only its real consumer");
            timer_accepted=false;timer_thread={};timer_thread.ip=64;
            check(timer_vm.step(timer_thread,0)==lcs::StepResult::unsupported && timer_thread.ip==64,
                  "A refused native combat consumer preserves the instruction for diagnosis");

        }
        adapter.world_ready=true;t.diagnostic.clear();
        check(object.step(t,0)==lcs::StepResult::continued && t.locals[0]==12345 && calls==4,"resident native entity handle returned to LCS local");


        check(last.opcode==0x029B && last.inputs[0]==10000 && last.inputs[1]==0x3f800000,"object model and float bits marshalled");
        auto encoded=last.encode();
        check(encoded[0]==0x9B && encoded[1]==2 && encoded[2]==1 && encoded[22]==3 && encoded[23]==0 && encoded[24]==0,"SA packet uses SA numeric/local format");
        lcs::NativeCall templated;templated.opcode=0x0384;templated.text="MISSION";templated.replacement_text="CAR_NAME";templated.inputs={5000,1};
        const auto message_bytes=templated.encode();
        check(message_bytes.size()==30 && message_bytes[2]==9 && message_bytes[11]==9 && message_bytes[20]==1 && message_bytes[25]==1 && std::string(message_bytes.begin()+3,message_bytes.begin()+10)=="MISSION" && std::string(message_bytes.begin()+12,message_bytes.begin()+20)=="CAR_NAME","Native replacement message contains two distinct fixed GXT labels before numeric time/style operands");
        auto bad_message=templated;bad_message.text.reset();bool bad_template=false;try{bad_message.encode();}catch(...){bad_template=true;}
        check(bad_template,"An orphan replacement key cannot corrupt the native parameter stream");
        std::vector<uint8_t> message_code;opcode(message_code,COMMAND_PRINT_STRING_IN_STRING_NOW);message_code.insert(message_code.end(),{'M','I','S','S','I','O','N',0,'C','A','R','_','N','A','M','E'});integer(message_code,5000);integer(message_code,1);
        lcs::NativeAdapter message_adapter(image);message_adapter.text_ready=true;message_adapter.execute=[&](const lcs::NativeCall& call){last=call;lcs::NativeResult r;r.executed=true;return r;};
        auto message=program(message_code);message.backend=[&](lcs::Vm& v,lcs::Thread& th,uint16_t op){return message_adapter.dispatch(v,th,op);};t={};t.ip=64;
        check(message.step(t,0)==lcs::StepResult::continued && last.opcode==0x0384 && last.text=="MISSION" && last.replacement_text=="CAR_NAME" && last.inputs==std::vector<int32_t>{5000,1},"Original mission template/replacement keys reach the actual native message together");
        code.back()=1; auto invalid=program(code);attach(invalid);t={};t.ip=64;
        check(invalid.step(t,0)==lcs::StepResult::failed && calls==4,"invalid output rejected before streaming or native entity creation");
        lcs::NativeAdapter physical(image);physical.world_ready=true;
        physical.bind_model(1000,14000,"NullSource",lcs::ModelKind::world_collision);
        physical.bind_model(1001,14001,"SourceSwapTarget",lcs::ModelKind::world_collision);
        size_t physical_calls=0;bool resident=true;
        physical.execute=[&](const lcs::NativeCall& call){++physical_calls;last=call;lcs::NativeResult result;result.executed=true;result.condition=resident;return result;};
        auto physical_command=[&](uint16_t op,std::initializer_list<int32_t> args){std::vector<uint8_t> bytes;opcode(bytes,op);for(auto value:args)integer(bytes,value);auto vm=program(bytes);vm.backend=[&](lcs::Vm& v,lcs::Thread& th,uint16_t command){return physical.dispatch(v,th,command);};lcs::Thread thread;thread.ip=64;return vm.step(thread,0);};
        check(physical_command(COMMAND_SWAP_NEAREST_BUILDING_MODEL,{0,0,0,0,1000,1001})==lcs::StepResult::continued && physical_calls==5 && last.opcode==0x03B6 && last.inputs[4]==14000 && last.inputs[5]==14001,"Original physical swap requests both source carriers and executes the actual native replacement");
        resident=false;check(physical_command(COMMAND_SWAP_NEAREST_BUILDING_MODEL,{0,0,0,0,1000,1001})==lcs::StepResult::yielded,"Unloaded physical swap cannot be marked complete");
        const auto before_phantom=physical_calls;
        check(physical_command(COMMAND_CREATE_OBJECT_NO_OFFSET,{1000,0,0,0})==lcs::StepResult::unsupported && physical_calls==before_phantom,"Collision carrier cannot masquerade as a visible mission prop");
        code.clear();opcode(code,COMMAND_HAS_MODEL_LOADED|0x8000);integer(code,-1);
        auto predicate=program(code);attach(predicate);t={};t.ip=64;t.condition=true;t.and_or=1;
        check(predicate.step(t,0)==lcs::StepResult::continued && !t.condition && !t.and_or && last.opcode==0x0248,"predicate preserves LCS NOT and logical group");
        code.clear();opcode(code,COMMAND_REQUEST_MODEL);integer(code,9999);
        auto unmapped=program(code);attach(unmapped);t={};t.ip=64;const auto before=calls;
        check(unmapped.step(t,0)==lcs::StepResult::unsupported && calls==before && t.ip==64,"unmapped model blocks instead of loading wrong SA asset");
        adapter.execute=[](const lcs::NativeCall&){return lcs::NativeResult{};};
        t={};t.ip=64;
        check(stat.step(t,0)==lcs::StepResult::unsupported && t.ip==64,"executor failure keeps LCS command retryable");
        lcs::NativeCall too_many;too_many.outputs=33;bool rejected=false;try {too_many.encode();}catch(const std::exception&){rejected=true;}
        check(rejected,"native local bounds checked");
        std::vector<lcs::NativeCall> history;
        adapter.execute=[&](const lcs::NativeCall& call){history.push_back(call);lcs::NativeResult r;r.executed=true;r.condition=true;r.values[0]=2345;return r;};
        code.clear();opcode(code,COMMAND_LOAD_MISSION_TEXT);code.insert(code.end(),{'V','I','C','1',0,0,0,0});
        auto text=program(code);attach(text);t={};t.ip=64;
        check(text.step(t,0)==lcs::StepResult::unsupported && history.empty(),"unmounted mission text cannot reach the native loader");
        adapter.text_ready=true;
        check(text.step(t,0)==lcs::StepResult::continued && history.back().text=="VIC1" && history.back().opcode==0x054C,"original mission bank label is native short text");
        lcs::NativeCall display;display.opcode=0x033E;display.inputs={0x3F800000,0x40000000};display.text="VIC1_1";display.text_position=2;
        encoded=display.encode();
        check(encoded.size()==21 && encoded[2]==1 && encoded[7]==1 && encoded[12]==9 && encoded[13]=='V' && encoded[20]==0,"text can follow numeric operands without changing float bits");
        display.text=std::string(9,'x');rejected=false;try{display.encode();}catch(...){rejected=true;}
        check(rejected,"oversized native GXT label is rejected");
        lcs::NativeCall declaration;declaration.opcode=0x0180;declaration.mission_flag_reference=true;
        check(declaration.encode()==std::vector<uint8_t>({0x80,1,2,24,0}),"native mission flag uses reserved SA global, not an integer literal");
        declaration.opcode=0x0004;rejected=false;try{declaration.encode();}catch(...){rejected=true;}
        check(rejected,"reserved mission flag reference cannot be used by another command");
        code.clear();opcode(code,COMMAND_CREATE_PLAYER);integer(code,0);integer(code,0);integer(code,0);integer(code,0);code.push_back(12);
        auto reuse=program(code);attach(reuse);t={};t.ip=64;history.clear();adapter.reuse_native_player=true;
        check(reuse.step(t,0)==lcs::StepResult::continued && t.locals[0]==0 && history.size()==2 && history[0].opcode==0x01F5 && history[1].opcode==0x00A1 && history[1].inputs[0]==2345,"original startup reuses native player instead of creating a second ped");
        code.back()=1;auto bad_reuse=program(code);attach(bad_reuse);t={};t.ip=64;history.clear();
        check(bad_reuse.step(t,0)==lcs::StepResult::failed && history.empty(),"player output is validated before moving the native ped");
        code.clear();opcode(code,COMMAND_SET_CURRENT_PLAYER_WEAPON);integer(code,0);integer(code,17);
        auto weapon=program(code);attach(weapon);t={};t.ip=64;history.clear();
        check(weapon.step(t,0)==lcs::StepResult::continued && history.size()==2 && history[1].opcode==0x01B9 && history[1].inputs==std::vector<int32_t>({2345,22}),"LCS Colt45 and player index are explicitly translated");
        code.back()=0; // Replace the entire weapon value with an exclusive type.
        code[code.size()-4]=2;weapon=program(code);attach(weapon);t={};t.ip=64;history.clear();
        check(weapon.step(t,0)==lcs::StepResult::unsupported && history.empty(),"exclusive LCS weapon cannot accidentally select a native SA numeric ID");
        adapter.bind_model(109,113,"vinc_01",lcs::ModelKind::ped);
        code.clear();opcode(code,COMMAND_CREATE_CHAR);integer(code,21);integer(code,109);integer(code,0);integer(code,0);integer(code,0);code.push_back(12);
        auto special=program(code);attach(special);t={};t.ip=64;history.clear();
        check(special.step(t,0)==lcs::StepResult::continued && history.back().inputs[0]==23,"LCS special ped type is not mistaken for SA bum type");
        code.clear();opcode(code,COMMAND_CREATE_CAR);integer(code,3750);integer(code,0);integer(code,0);integer(code,0);code.push_back(12);
        auto wrong_class=program(code);attach(wrong_class);t={};t.ip=64;history.clear();
        check(wrong_class.step(t,0)==lcs::StepResult::unsupported && history.empty(),"building model cannot be passed to the native vehicle factory");
        code.clear();opcode(code,COMMAND_LOAD_CUTSCENE);code.insert(code.end(),{'h','o','m','s','w','e','t',0});
        auto scene=program(code);attach(scene);t={};t.ip=64;history.clear();
        check(scene.step(t,0)==lcs::StepResult::unsupported && history.empty(),"unmounted cutscene cannot enter native loader");
        adapter.bind_cutscene("homswet","L0001");
        unsigned voice_prepared=0,voice_armed=0,voice_cleared=0;
        adapter.prepare_cutscene_audio=[&](const std::string& original){check(original=="homswet","voice callback keeps original identity instead of native alias");++voice_prepared;return true;};
        adapter.arm_cutscene_audio=[&](){++voice_armed;};adapter.clear_cutscene_audio=[&](){++voice_cleared;};
        adapter.execute=[&](const lcs::NativeCall& call){history.push_back(call);return lcs::NativeResult{};};
        check(scene.step(t,0)==lcs::StepResult::unsupported && !adapter.has_active_cutscene() && voice_cleared==1 && !voice_armed,"failed native load releases prepared voice without starting it");
        bool loaded=false;
        adapter.execute=[&](const lcs::NativeCall& call){history.push_back(call);lcs::NativeResult r;r.executed=true;r.condition=loaded;r.values[0]=678;return r;};
        check(scene.step(t,0)==lcs::StepResult::continued && adapter.has_active_cutscene() && history.back().opcode==0x02E4 && history.back().text=="L0001" && voice_prepared==2,"original scene uses unique mounted native alias");
        code.clear();opcode(code,COMMAND_START_CUTSCENE);
        auto start_scene=program(code);attach(start_scene);t={};t.ip=64;history.clear();
        check(start_scene.step(t,0)==lcs::StepResult::yielded && t.ip==64 && history.size()==1 && history[0].opcode==0x06B9 && !voice_armed,"asynchronous loading retries START without arming voice prematurely");
        loaded=true;history.clear();
        check(start_scene.step(t,0)==lcs::StepResult::continued && history.size()==2 && history[1].opcode==0x02E7 && voice_armed==1,"scene and voice start only after actual backend readiness");
        code.clear();opcode(code,COMMAND_GET_CUTSCENE_TIME);code.push_back(12);
        auto scene_time=program(code);attach(scene_time);t={};t.ip=64;
        check(scene_time.step(t,0)==lcs::StepResult::continued && t.locals[0]==678 && history.back().opcode==0x02E8,"cutscene time comes from native backend");
        code.clear();opcode(code,COMMAND_HAS_CUTSCENE_FINISHED);
        auto scene_done=program(code);attach(scene_done);t={};t.ip=64;loaded=false;
        check(scene_done.step(t,0)==lcs::StepResult::continued && !t.condition,"unfinished native scene is not simulated as complete");
        code.clear();opcode(code,COMMAND_CLEAR_CUTSCENE);
        auto clear_scene=program(code);attach(clear_scene);t={};t.ip=64;
        check(clear_scene.step(t,0)==lcs::StepResult::continued && !adapter.has_active_cutscene() && history.back().opcode==0x02EA && voice_cleared==2,"clear frees scene and voice after native success");
        code.clear();opcode(code,COMMAND_LOAD_CUTSCENE);code.insert(code.end(),{'s','a','l','1',0,0,0,0});
        auto other_scene=program(code);attach(other_scene);t={};t.ip=64;history.clear();
        check(other_scene.step(t,0)==lcs::StepResult::unsupported && history.empty(),"unregistered later story scene cannot use an unrelated SA scene");
        // LCS special01 is 109, SA special01 is 290. A generic ped binding is
        // insufficient to send an original label to SA's special directory.
        code.clear();opcode(code,COMMAND_LOAD_SPECIAL_CHARACTER);integer(code,1);code.insert(code.end(),{'v','i','n','c','_','0','1',0});
        auto special_load=program(code);attach(special_load);t={};t.ip=64;history.clear();
        check(special_load.step(t,0)==lcs::StepResult::unsupported && history.empty(),"generic ped slot cannot impersonate native special character slot");
        lcs::NativeAdapter specials(image);specials.world_ready=true;specials.text_ready=true;
        specials.bind_model(109,290,"vinc_01",lcs::ModelKind::ped);specials.execute=adapter.execute;
        special_load.backend=[&](lcs::Vm& v,lcs::Thread& s,uint16_t op){return specials.dispatch(v,s,op);};
        check(special_load.step(t,0)==lcs::StepResult::continued && history.back().opcode==0x023C && history.back().inputs==std::vector<int32_t>({1}) && history.back().text=="VINC_01" && history.back().text_position==1,"original Vincenzo uses verified native special slot and named resource");
        code.clear();opcode(code,COMMAND_LOAD_SPECIAL_CHARACTER);integer(code,1);code.insert(code.end(),{'s','a','l','_','0','1',0,0});
        auto missing_actor=program(code);missing_actor.backend=special_load.backend;t={};t.ip=64;history.clear();
        check(missing_actor.step(t,0)==lcs::StepResult::unsupported && history.empty(),"unmounted named actor cannot reach RequestSpecialModel");
        adapter.execute=[&](const lcs::NativeCall& call){history.push_back(call);lcs::NativeResult r;r.executed=true;r.values[0]=0x41200000;return r;};
        code.clear();opcode(code,COMMAND_SET_CHAR_OBJ_GOTO_COORD_ON_FOOT);integer(code,42);integer(code,0x3F800000);integer(code,0x40000000);
        auto walk=program(code);attach(walk);t={};t.ip=64;history.clear();
        check(walk.step(t,0)==lcs::StepResult::continued && history.size()==2 && history[0].opcode==0x02CE && history[0].inputs==std::vector<int32_t>({0x3F800000,0x40000000,0x447A0000}) && history[1].opcode==0x05D3 && history[1].inputs==std::vector<int32_t>({42,0x3F800000,0x40000000,0x41200000,4,-2}),"LCS 2D walking resolves native ground and constructs an untimed 3D walking task");
        adapter.execute=[&](const lcs::NativeCall& call){history.push_back(call);lcs::NativeResult r;r.executed=true;r.values[0]=0x7FC00000;return r;};
        t={};t.ip=64;history.clear();
        check(walk.step(t,0)==lcs::StepResult::unsupported && history.size()==1 && history[0].opcode==0x02CE,"invalid ground height cannot enter a native movement task");
        adapter.execute=[&](const lcs::NativeCall& call){history.push_back(call);lcs::NativeResult r;r.executed=true;return r;};
        code.clear();opcode(code,COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_PASSENGER);integer(code,42);integer(code,43);
        auto passenger=program(code);attach(passenger);t={};t.ip=64;history.clear();
        check(passenger.step(t,0)==lcs::StepResult::continued && history.size()==1 && history[0].opcode==0x05CA && history[0].inputs==std::vector<int32_t>({42,43,-1,-1}),"LCS passenger objective uses native entry task and any passenger seat");
        code.clear();opcode(code,COMMAND_SET_CHAR_OBJ_NO_OBJ);integer(code,42);
        auto clear_task=program(code);attach(clear_task);t={};t.ip=64;history.clear();
        check(clear_task.step(t,0)==lcs::StepResult::continued && history.size()==1 && history[0].opcode==0x0687 && history[0].inputs==std::vector<int32_t>({42}),"LCS empty objective clears native character tasks");
        adapter.execute=[&](const lcs::NativeCall& call){history.push_back(call);lcs::NativeResult r;r.executed=true;r.condition=true;r.values[0]=123;return r;};
        uintptr_t live_ped=0x12345000;
        bool readable_identity=true;
        adapter.live_ped_identity=[&](int32_t ped)->std::optional<lcs::ObjectiveActor>{
            if(!readable_identity)return {};
            return lcs::ObjectiveActor{ped,live_ped};
        };
        code.clear();opcode(code,COMMAND_SET_CHAR_ONLY_ALLOWED_TO_SIT_BEHIND);integer(code,42);integer(code,1);
        auto rear=program(code);attach(rear);t={};t.ip=64;history.clear();
        check(rear.step(t,0)==lcs::StepResult::continued && history.back().opcode==0x056D,"rear-seat policy validates actual ped handle");
        code.clear();opcode(code,COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_PASSENGER);integer(code,42);integer(code,43);
        auto rear_entry=program(code);attach(rear_entry);t={};t.ip=64;history.clear();
        check(rear_entry.step(t,0)==lcs::StepResult::continued && history.back().inputs==std::vector<int32_t>({42,43,-1,1}),"Vincenzo rear-seat policy selects native rear passenger seat");
        // Original 1485 front-only seat policy is distinct from 1479 rear.
        code.clear();opcode(code,COMMAND_SET_CHAR_ONLY_ALLOWED_TO_SIT_BEHIND);integer(code,42);integer(code,0);
        auto unset_rear=program(code);attach(unset_rear);t={};t.ip=64;history.clear();
        check(unset_rear.step(t,0)==lcs::StepResult::continued && history.back().opcode==0x056D,"Removing rear restriction validates actual ped");
        code.clear();opcode(code,COMMAND_SET_CHAR_ONLY_ALLOWED_TO_SIT_IN_FRONT);integer(code,42);integer(code,1);
        auto front=program(code);attach(front);t={};t.ip=64;history.clear();
        check(front.step(t,0)==lcs::StepResult::continued && history.size()==1 && history.back().opcode==0x056D,"1485 sets front-only policy using live ped validation");
        code.clear();opcode(code,COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_PASSENGER);integer(code,42);integer(code,43);
        auto front_entry=program(code);attach(front_entry);t={};t.ip=64;history.clear();
        check(front_entry.step(t,0)==lcs::StepResult::continued && history.back().opcode==0x05CA &&
            history.back().inputs==std::vector<int32_t>({42,43,-1,0}),"Front-only passenger enters seat 0, never native any-seat sentinel");
        adapter.world_ready=false;t={};t.ip=64;history.clear();
        check(front.step(t,0)==lcs::StepResult::unsupported && history.empty(),"1485 must not mutate without a mounted native world");
        adapter.world_ready=true;
        code.clear();opcode(code,COMMAND_SET_CHAR_ONLY_ALLOWED_TO_SIT_BEHIND);integer(code,42);integer(code,1);
        auto enforce_rear=program(code);attach(enforce_rear);t={};t.ip=64;
        check(enforce_rear.step(t,0)==lcs::StepResult::continued,"Independent rear restriction can be applied");
        t={};t.ip=64;history.clear();
        check(front_entry.step(t,0)==lcs::StepResult::unsupported && history.empty(),"Contradictory seat restrictions cannot assign a wrong passenger seat");
        code.clear();opcode(code,COMMAND_SET_CHAR_ONLY_ALLOWED_TO_SIT_BEHIND);integer(code,42);integer(code,0);
        auto clear_rear=program(code);attach(clear_rear);t={};t.ip=64;
        check(clear_rear.step(t,0)==lcs::StepResult::continued,"Rear restriction can be cleared independently");
        code.clear();opcode(code,COMMAND_SET_CHAR_ONLY_ALLOWED_TO_SIT_IN_FRONT);integer(code,42);integer(code,0);
        auto clear_front=program(code);attach(clear_front);t={};t.ip=64;
        check(clear_front.step(t,0)==lcs::StepResult::continued,"Front-only flag can be cleared");
        t={};t.ip=64;history.clear();
        check(front_entry.step(t,0)==lcs::StepResult::continued && history.back().inputs==std::vector<int32_t>({42,43,-1,-1}),"Cleared 1485 restores native auto seat choice");
        // Reassignment of a live native pool slot must invalidate a previous
        // ped's front-only bit; temporary identity-query failure must refuse
        // task assignment, not silently fall back to any passenger seat.
        t={};t.ip=64;check(front.step(t,0)==lcs::StepResult::continued,"Old ped sets front-only before pool reuse");
        readable_identity=false;t={};t.ip=64;history.clear();
        check(front_entry.step(t,0)==lcs::StepResult::unsupported && history.empty(),"Unverifiable owner cannot use a remembered seat restriction");
        t={};t.ip=64;history.clear();
        check(front.step(t,0)==lcs::StepResult::unsupported && history.size()==1 && history[0].opcode==0x056D,
              "Unverifiable owner cannot overwrite an existing source seat restriction");
        readable_identity=true;live_ped=0x12346000;t={};t.ip=64;history.clear();
        check(front_entry.step(t,0)==lcs::StepResult::continued && history.back().inputs==std::vector<int32_t>({42,43,-1,-1}),"Recycled handle cannot inherit old occupant's front-only restriction");
        t={};t.ip=64;check(front.step(t,0)==lcs::StepResult::continued,"New ped can establish its own seat policy");
        t={};t.ip=64;history.clear();
        check(front_entry.step(t,0)==lcs::StepResult::continued && history.back().inputs==std::vector<int32_t>({42,43,-1,0}),"Freshly owned restriction applies to current ped only");
        // A real source DELETE_CHAR clears both bits without depending on the
        // next actor reusing the same pool index.
        code.clear();opcode(code,COMMAND_DELETE_CHAR);integer(code,42);
        auto delete_ped=program(code);attach(delete_ped);t={};t.ip=64;
        check(delete_ped.step(t,0)==lcs::StepResult::continued,"Source ped deletion reaches native SA");
        t={};t.ip=64;history.clear();
        check(front_entry.step(t,0)==lcs::StepResult::continued && history.back().inputs==std::vector<int32_t>({42,43,-1,-1}),"Deletion releases both seat policy handles");
        code.clear();opcode(code,COMMAND_GET_PAD_BUTTON_STATE);integer(code,44);code.push_back(12);
        auto pad=program(code);attach(pad);t={};t.ip=64;history.clear();
        check(pad.step(t,0)==lcs::StepResult::continued && history.back().inputs==std::vector<int32_t>({0,16}) && t.locals[0]==123,"original dialogue skip button reads native pad result");
        code.clear();opcode(code,COMMAND_GET_PLAYER_STORED_WEAPON);integer(code,0);code.push_back(12);
        adapter.stored_player_weapon=[](int32_t){return std::optional<int32_t>(55);};
        auto saved=program(code);attach(saved);t={};t.ip=64;history.clear();
        check(saved.step(t,0)==lcs::StepResult::continued && t.locals[0]==45,"native unidentified saved weapon maps to LCS sentinel");
        code.clear();opcode(code,COMMAND_SET_CURRENT_PLAYER_WEAPON);integer(code,0);integer(code,45);
        auto unidentified=program(code);attach(unidentified);t={};t.ip=64;history.clear();
        check(unidentified.step(t,0)==lcs::StepResult::continued && history.empty(),"original unidentified sentinel cannot equip an unrelated native weapon");
        adapter.bind_garage(0,"LCSVIC1");adapter.bind_garage(9,"LCGR0009");
        for(const auto pair:std::vector<std::pair<uint16_t,uint16_t>>{{COMMAND_OPEN_GARAGE,0x0360},{COMMAND_CLOSE_GARAGE,0x0361},{COMMAND_IS_GARAGE_OPEN,0x03B0},{COMMAND_IS_GARAGE_CLOSED,0x03B1}}) {
            adapter.execute=[&](const lcs::NativeCall& call){history.push_back(call);lcs::NativeResult r;r.executed=true;r.condition=true;return r;};
            code.clear();opcode(code,pair.first);integer(code,9);
            auto garage=program(code);attach(garage);t={};t.ip=64;history.clear();
            check(garage.step(t,0)==lcs::StepResult::continued && history.size()==1 && history[0].opcode==pair.second &&
                  history[0].inputs.empty() && history[0].text=="LCGR0009" && history[0].encode().size()==11,
                  "garage handles translate to native name operands for mutations and predicates");
        }
        code.clear();opcode(code,COMMAND_SET_TARGET_CAR_FOR_MISSION_GARAGE);integer(code,0);integer(code,1234);
        auto target_garage=program(code);attach(target_garage);t={};t.ip=64;history.clear();
        check(target_garage.step(t,0)==lcs::StepResult::continued && history[0].text=="LCSVIC1" && history[0].inputs==std::vector<int32_t>{1234},
              "garage name replacement preserves target vehicle handle");
        code.clear();opcode(code,COMMAND_OPEN_GARAGE);integer(code,5);
        auto unknown_garage=program(code);attach(unknown_garage);t={};t.ip=64;history.clear();
        check(unknown_garage.step(t,0)==lcs::StepResult::unsupported && history.empty(),"unbound garage cannot mutate an unrelated native garage");
        code.clear();opcode(code,COMMAND_HAS_CHAR_BEEN_DAMAGED_BY_CHAR|0x8000);integer(code,42);integer(code,43);
        auto attacker=program(code);attach(attacker);t={};t.ip=64;t.condition=true;t.and_or=1;history.clear();
        check(attacker.step(t,0)==lcs::StepResult::continued && history[0].opcode==0x051A &&
              history[0].inputs==std::vector<int32_t>({42,43}) && !t.condition && !t.and_or,
              "shared damage query preserves both native handles and original negation/AND");
        code.clear();opcode(code,COMMAND_FREEZE_CHAR_POSITION);integer(code,42);integer(code,1);
        auto frozen=program(code);attach(frozen);t={};t.ip=64;history.clear();adapter.world_ready=false;
        check(frozen.step(t,0)==lcs::StepResult::unsupported && history.empty(),"shared entity adapters require the real mounted native world");
        adapter.world_ready=true;
        check(frozen.step(t,0)==lcs::StepResult::continued && history[0].opcode==0x04D7 &&
              history[0].inputs==std::vector<int32_t>({42,1}),"shared freeze command forwards the native ped and boolean");
        adapter.execute=[&](const lcs::NativeCall& call){history.push_back(call);lcs::NativeResult r;r.executed=true;
            r.values[0]=0x3f800000;r.values[1]=int32_t(0xc0200000);r.values[2]=0x42c80000;return r;};
        code.clear();opcode(code,COMMAND_GET_OFFSET_FROM_CHAR_IN_WORLD_COORDS);
        integer(code,42);integer(code,0x3f800000);integer(code,0x40000000);integer(code,0x40400000);
        code.insert(code.end(),{14,12,15});
        auto offsets=program(code);attach(offsets);t={};t.ip=64;history.clear();
        check(offsets.step(t,0)==lcs::StepResult::continued && history[0].opcode==0x04C4 && history[0].outputs==3 &&
              t.locals[2]==0x3f800000 && t.locals[0]==int32_t(0xc0200000) && t.locals[3]==0x42c80000,
              "shared coordinate queries preserve float bits and original noncontiguous output destinations");
        code.back()=1;auto bad_offsets=program(code);attach(bad_offsets);t={};t.ip=64;history.clear();
        check(bad_offsets.step(t,0)==lcs::StepResult::failed && history.empty(),"shared coordinate outputs are validated before native side effects");
        code.clear();opcode(code,COMMAND_GENERATE_RANDOM_FLOAT_IN_RANGE);integer(code,int32_t(0xc0200000));integer(code,0x42c80000);code.push_back(12);
        auto random=program(code);attach(random);t={};t.ip=64;history.clear();adapter.world_ready=false;
        check(random.step(t,0)==lcs::StepResult::continued && history[0].opcode==0x0208 && t.locals[0]==0x3f800000,
              "non-world random queries work during original campaign initialization");
        adapter.world_ready=true;bool mission_owner=true;
        adapter.mission_script=[&](const lcs::Thread&){return mission_owner;};
        code.clear();opcode(code,COMMAND_FREEZE_CHAR_POSITION);integer(code,42);integer(code,1);
        auto ownership=program(code);attach(ownership);t={};t.ip=64;history.clear();
        check(ownership.step(t,0)==lcs::StepResult::continued && history[0].mission_context,
              "native entity operations follow actual original mission ownership");
        code.clear();opcode(code,COMMAND_MISSION_HAS_FINISHED);
        auto cleanup=program(code);attach(cleanup);t={};t.ip=64;history.clear();
        check(cleanup.step(t,0)==lcs::StepResult::continued && history.size()==1 && history[0].opcode==0x00D8 && history[0].mission_context,
              "global campaign cleanup executes native cleanup without ending campaign threads");
        mission_owner=false;t={};t.ip=64;history.clear();
        check(cleanup.step(t,0)==lcs::StepResult::continued && history.empty(),"ordinary MAIN worker cannot clean up another mission's entities");
        adapter.mission_script={};t={};t.ip=64;
        check(cleanup.step(t,0)==lcs::StepResult::unsupported && history.empty(),"cleanup without original script ownership cannot delete unrelated native entities");
        code.clear();opcode(code,COMMAND_GET_CAR_COLOURS);integer(code,42);code.insert(code.end(),{12,15});
        auto colours=program(code);attach(colours);t={};t.ip=64;t.condition=false;history.clear();
        adapter.execute=[&](const lcs::NativeCall& call){history.push_back(call);lcs::NativeResult r;r.executed=true;r.condition=true;r.values[0]=3;r.values[1]=7;return r;};
        check(colours.step(t,0)==lcs::StepResult::continued && t.locals[0]==3 && t.locals[3]==7 && !t.condition && !history.back().predicate,
              "actual car colour getter stores two original destinations without the misleading debug predicate");
        code.clear();opcode(code,COMMAND_SLIDE_OBJECT);integer(code,42);for(int i=0;i<6;++i)integer(code,0x3f800000);integer(code,1);
        auto slide=program(code);attach(slide);t={};t.ip=64;history.clear();
        adapter.execute=[&](const lcs::NativeCall& call){history.push_back(call);lcs::NativeResult r;r.executed=true;return r;};
        check(slide.step(t,0)==lcs::StepResult::continued && !t.condition && history[0].opcode==0x034E && history[0].inputs.size()==8,
              "object objective waits for actual native movement result instead of simulated completion");
        adapter.mission_script=[&](const lcs::Thread&){return true;};
        code.clear();opcode(code,COMMAND_GIVE_WEAPON_TO_CHAR);integer(code,42);integer(code,17);integer(code,100);
        auto armed=program(code);attach(armed);t={};t.ip=64;history.clear();
        check(armed.step(t,0)==lcs::StepResult::continued && history.size()==2 && history[0].opcode==0x01B2 && history[0].inputs==std::vector<int32_t>({42,22,100}) && history[1].opcode==0x01B9 && history[1].mission_context,"original combat equip translates weapon, ammo and current selection with real mission ownership");
        code.clear();opcode(code,COMMAND_GIVE_WEAPON_TO_CHAR);integer(code,42);integer(code,2);integer(code,100);
        auto exclusive=program(code);attach(exclusive);t={};t.ip=64;history.clear();
        check(exclusive.step(t,0)==lcs::StepResult::unsupported && history.empty(),"unimplemented exclusive gun stops rather than substituting an unrelated weapon");
        code.clear();opcode(code,COMMAND_SET_CHAR_OBJ_KILL_CHAR_ON_FOOT);integer(code,42);integer(code,43);
        auto attack=program(code);attach(attack);t={};t.ip=64;history.clear();
        check(attack.step(t,0)==lcs::StepResult::continued && history.size()==1 && history[0].opcode==0x05E2 && history[0].inputs==std::vector<int32_t>({42,43}) && !t.condition,"original attack starts a real native task without completing its objective");
        code.clear();opcode(code,COMMAND_SET_CHAR_OBJ_KILL_PLAYER_ON_FOOT);integer(code,42);integer(code,0);
        auto player_target=program(code);attach(player_target);t={};t.ip=64;history.clear();
        adapter.execute=[&](const lcs::NativeCall& call){history.push_back(call);lcs::NativeResult r;r.executed=true;r.values[0]=2345;return r;};
        check(player_target.step(t,0)==lcs::StepResult::continued && history.size()==2 && history[0].inputs==std::vector<int32_t>({0}) && history[1].inputs==std::vector<int32_t>({42,2345}),"combat player target resolves the second operand instead of the attacker");
        code.clear();opcode(code,COMMAND_SET_CHAR_OBJ_RUN_TO_COORD);integer(code,42);integer(code,0x3f800000);integer(code,0x40000000);
        auto run=program(code);attach(run);t={};t.ip=64;history.clear();
        adapter.execute=[&](const lcs::NativeCall& call){history.push_back(call);lcs::NativeResult r;r.executed=true;r.values[0]=0x41200000;return r;};
        check(run.step(t,0)==lcs::StepResult::continued && history.size()==2 && history[1].inputs==std::vector<int32_t>({42,0x3f800000,0x40000000,0x41200000,6,-2}),"shared running objective uses real ground and untimed native running state");
        code.clear();opcode(code,COMMAND_IS_CHAR_STILL_ALIVE|0x8000);integer(code,42);
        auto alive=program(code);attach(alive);t={};t.ip=64;history.clear();
        check(alive.step(t,0)==lcs::StepResult::continued && !t.condition && history.back().opcode==0x0118,"life query composes actual native death with original NOT exactly once");
        lcs::PauseMenuGate gate;
        for(int mask=0;mask<16;++mask){gate.disabled=(mask&8)!=0;const bool actual=gate.delegate((mask&4)!=0,(mask&2)!=0,(mask&1)!=0);check(actual==(mask!=8),"pause gate blocks only new manual activation during script disable");}
        code.clear();opcode(code,COMMAND_DISABLE_PAUSE_MENU);integer(code,1);auto pause=program(code);attach(pause);t={};t.ip=64;history.clear();
        check(pause.step(t,0)==lcs::StepResult::unsupported && history.empty(),"missing actual frontend activation backend cannot become a NOP");
        adapter.disable_pause_menu=[&](bool disabled){gate.disabled=disabled;return true;};
        check(pause.step(t,0)==lcs::StepResult::continued && gate.disabled && !gate.delegate(false,false,false),"original pause command controls persistent frontend gate");
        {
            lcs::NativeAdapter cops(image);cops.world_ready=true;
            const std::array<int32_t,12> donors{280,285,286,287,281,282,283,284,288,62,66,67};
            for(size_t i=0;i<12;++i)cops.bind_model(i<4?int32_t(i+1):int32_t(97+i-4),donors[i],"cop"+std::to_string(i),lcs::ModelKind::ped);
            int cop_calls=0;int32_t exclusion=-1;
            cops.random_cop_in_area=[&](const auto& area,const auto& kinds,const auto& mapped,int32_t excluded,bool mission,const auto& commit) {
                ++cop_calls;check(area==std::array<float,4>{1,2,3,4} && kinds==std::array<int32_t,5>{1,2,3,4,5} && mapped==donors && !mission,"Cop adapter preserves nine operands and canonical bindings");
                exclusion=excluded;lcs::NativeResult r;r.executed=true;
                if(cop_calls==1){check(commit(27),"Shared random ped commit accepted before promotion");r.values[0]=27;}
                else r.values[0]=-1;
                return r;
            };
            std::vector<uint8_t> cop_code;opcode(cop_code,COMMAND_GET_RANDOM_COP_IN_AREA);
            for(int32_t bits:{0x3f800000,0x40000000,0x40400000,0x40800000})integer(cop_code,bits);
            for(int32_t flag:{1,2,3,4,5})integer(cop_code,flag);cop_code.push_back(12);
            auto cop_vm=program(cop_code);cop_vm.backend=[&](auto& v,auto& th,uint16_t op){return cops.dispatch(v,th,op);};
            lcs::Thread cop_thread;cop_thread.ip=64;
            check(cop_vm.step(cop_thread,0)==lcs::StepResult::continued && cop_thread.locals[0]==27 && exclusion==-1,"Cop output stores accepted handle");
            cop_thread={};cop_thread.ip=64;
            check(cop_vm.step(cop_thread,0)==lcs::StepResult::continued && cop_thread.locals[0]==-1 && exclusion==27,"Next query shares original exclusion and preserves genuine no-match sentinel");
            cop_code.back()=6;auto malformed=program(cop_code);
            malformed.backend=cop_vm.backend;cop_thread={};cop_thread.ip=64;
            check(malformed.step(cop_thread,0)!=lcs::StepResult::continued && cop_calls==2,"Malformed output refuses before cop effects");
            cops.random_cop_in_area=[&](const auto&,const auto&,const auto&,int32_t excluded,bool,const auto& commit) {
                check(excluded==27 && commit(42),"Failure fixture commits a new shared source identity");
                lcs::NativeResult r;r.native_entered=true;r.diagnostic="partial promotion";return r;
            };
            cop_thread={};cop_thread.ip=64;
            check(cop_vm.step(cop_thread,0)==lcs::StepResult::failed && cop_thread.locals[0]==0,"Entered cop failure cannot be downgraded to unsupported/replayed or store a fake handle");
            cops.random_cop_in_area=[&](const auto&,const auto&,const auto&,int32_t excluded,bool,const auto&) {
                check(excluded==42,"Partial acceptance preserves source selection order rather than rolling back LastRandomPedId");
                lcs::NativeResult r;r.executed=true;r.values[0]=-1;return r;
            };
            cop_thread={};cop_thread.ip=64;check(cop_vm.step(cop_thread,0)==lcs::StepResult::continued,"Independent next query retains genuine no-match behavior");
        }
        {
            // Source 1512 has zero SCM operands and must dispatch as a typed
            // backend action, not native SA's empty 0409 or exploding 1038.
            lcs::NativeAdapter rc(image);rc.world_ready=true;
            size_t rc_calls=0;
            rc.execute=[&](const lcs::NativeCall& packet){
                ++rc_calls;
                check(packet.source_entity_command==lcs::EntityCommand::cancel_remote_mode &&
                      packet.inputs.empty() && packet.outputs==0 && !packet.predicate && !packet.opcode,
                      "1512 uses typed remote cancellation with no invented operand or SA opcode");
                lcs::NativeResult output;output.executed=true;output.native_entered=true;return output;
            };
            auto make_rc=[&](std::vector<uint8_t> bytes){
                auto machine=program(bytes);
                machine.backend=[&](lcs::Vm& v,lcs::Thread& thread,uint16_t op){return rc.dispatch(v,thread,op);};
                return machine;
            };
            std::vector<uint8_t> bytes;opcode(bytes,COMMAND_CANCEL_REMOTE_MODE);
            auto mission=make_rc(bytes);lcs::Thread step;step.ip=64;
            check(mission.step(step,0)==lcs::StepResult::continued && rc_calls==1,
                  "Source 1512 is now wired to the live entity family");
            rc.world_ready=false;step={};step.ip=64;
            check(mission.step(step,0)==lcs::StepResult::unsupported && rc_calls==1,
                  "Remote cancel without mounted SA world never dispatches");
            rc.world_ready=true;
            bytes.push_back(6);bytes.insert(bytes.end(),{1,0,0,0});
            auto excess=make_rc(bytes);step={};step.ip=64;
            check(excess.step(step,0)==lcs::StepResult::continued && step.ip==66 && rc_calls==2,
                  "1512 never consumes the next opcode's operands");
            rc.execute=lcs::guard_native_executor([](const lcs::NativeCall&) {lcs::NativeResult r;r.native_entered=true;
                r.diagnostic="Remote transition partially entered";return r;});
            step={};step.ip=64;
            check(mission.step(step,0)==lcs::StepResult::failed,
                  "Entered remote failure is fatal and cannot replay ownership transitions");
        }
        {
            // Source 0990 and native SA 03D9 compare live front-end/save
            // state, not a fabricated "save complete" flag. 0989 remains
            // unsupported until its LCS state serialization is implemented.
            lcs::NativeAdapter save(image);save.world_ready=true;
            size_t calls=0;bool done=false;
            save.execute=[&](const lcs::NativeCall& packet){
                ++calls;
                check(packet.source_entity_command==lcs::EntityCommand::save_game_finished &&
                      packet.predicate && packet.inputs.empty() && packet.outputs==0 && !packet.opcode,
                      "0990 dispatches the native save-menu predicate without fabricated operands");
                lcs::NativeResult r;r.executed=true;r.condition=done;return r;
            };
            std::vector<uint8_t> code;opcode(code,COMMAND_HAS_SAVE_GAME_FINISHED);
            auto query=program(code);
            query.backend=[&](lcs::Vm& v,lcs::Thread& t,uint16_t op){return save.dispatch(v,t,op);};
            lcs::Thread t;t.ip=64;
            check(query.step(t,0)==lcs::StepResult::continued && !t.condition && calls==1,
                  "A live save menu cannot be reported as completed");
            done=true;t={};t.ip=64;
            check(query.step(t,0)==lcs::StepResult::continued && t.condition && calls==2,
                  "Both native front-end flags clear permit real save completion query");
            save.world_ready=false;t={};t.ip=64;
            check(query.step(t,0)==lcs::StepResult::unsupported && calls==2,
                  "Unbound native menu cannot invent save completion");
            save.world_ready=true;save.execute=lcs::guard_native_executor([](const lcs::NativeCall&){
                lcs::NativeResult r;r.native_entered=true;r.diagnostic="Native snapshot interrupted";return r;
            });
            t={};t.ip=64;
            check(query.step(t,0)==lcs::StepResult::failed,
                  "Entered backend fault is not converted into a false predicate");
        }
        std::cout<<"Native translation and pre-call safety tests passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
