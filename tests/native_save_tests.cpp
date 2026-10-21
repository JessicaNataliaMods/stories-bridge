#include "lcs/native.hpp"
#include "ScriptCommands.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void opcode(std::vector<uint8_t>& code,uint16_t op){code.push_back(uint8_t(op));code.push_back(uint8_t(op>>8));}
void integer(std::vector<uint8_t>& code,int32_t value){code.push_back(6);for(int i=0;i<4;++i)code.push_back(uint8_t(uint32_t(value)>>(8*i)));}
lcs::Vm program(std::vector<uint8_t> code){std::vector<uint8_t> bytes(64);bytes.insert(bytes.end(),code.begin(),code.end());return lcs::Vm(std::move(bytes),64,64);}
lcs::StepResult run(lcs::NativeAdapter& adapter,uint16_t op,const std::vector<int32_t>& params){
    std::vector<uint8_t> code;opcode(code,op);for(auto value:params)integer(code,value);
    auto vm=program(std::move(code));vm.backend=[&](lcs::Vm& v,lcs::Thread& t,uint16_t command){return adapter.dispatch(v,t,command);};
    lcs::Thread thread;thread.ip=64;return vm.step(thread,0);
}
lcs::StepResult run_label(lcs::NativeAdapter& adapter,uint16_t op,int32_t actor,const std::string& label,lcs::Thread& thread){
    std::vector<uint8_t> code;opcode(code,op);integer(code,actor);
    for(size_t i=0;i<8;++i)code.push_back(i<label.size()?uint8_t(label[i]):0);
    auto vm=program(std::move(code));vm.backend=[&](lcs::Vm& v,lcs::Thread& t,uint16_t command){return adapter.dispatch(v,t,command);};
    thread={};thread.ip=64;return vm.step(thread,0);
}
void bind_player_facts(lcs::NativeAdapter& adapter,int32_t player_handle,uintptr_t native_ped,int32_t model,
                       const std::array<float,3>& position,std::vector<lcs::NativeCall>& calls){
    adapter.world_ready=true;
    adapter.live_ped_identity=[=](int32_t handle)->std::optional<lcs::ObjectiveActor>{
        return handle==player_handle?std::optional<lcs::ObjectiveActor>(lcs::ObjectiveActor{handle,native_ped}):std::nullopt;
    };
    adapter.execute=[=,&calls](const lcs::NativeCall& call){
        calls.push_back(call);lcs::NativeResult result;result.executed=true;result.condition=true;
        if(call.opcode==0x01F5)result.values[0]=player_handle;
        if(call.opcode==0x0665)result.values[0]=model;
        if(call.opcode==0x00A0)for(size_t i=0;i<3;++i)std::memcpy(&result.values[i],&position[i],sizeof(float));
        return result;
    };
}
void append_float(std::vector<int32_t>& params,float value){int32_t bits=0;std::memcpy(&bits,&value,sizeof(bits));params.push_back(bits);}
void mount_crane(lcs::NativeAdapter& adapter){std::istringstream ipl("inst\n1234 crane 0 0 0 0\nend\n");adapter.load_crane_placements(ipl,1234);}
void mount_ferry(lcs::NativeAdapter& adapter){
    std::istringstream path("8\n0 0 0\n40 20 0\n100 50 0\n150 100 0\n100 150 0\n0 150 0\n-100 100 0\n-150 0 0\n");
    check(adapter.ferry_service.load_path(0,path),"A valid mounted ferry route should load");
}
}

int main(){
    try {
        lcs::ScmImage image;
        // Version 1 had no player-ped policy trailer. Convert a fresh v2
        // empty-policy archive by removing its nine-byte trailer and verify
        // the legacy reader defaults those policies to empty.
        lcs::NativeAdapter empty_source(image);const auto empty_v2=empty_source.save_state(0);
        bool loaded_v1=false;std::string diagnostic;
        for(size_t at=8;at+9<=empty_v2.size()&&!loaded_v1;++at){
            if(!std::all_of(empty_v2.begin()+at,empty_v2.begin()+at+9,[](uint8_t byte){return byte==0;}))continue;
            auto legacy=empty_v2;legacy.erase(legacy.begin()+at,legacy.begin()+at+9);legacy[4]=1;legacy[5]=0;
            lcs::NativeAdapter old_reader(image);std::string why;
            loaded_v1=old_reader.restore_state(legacy,0,why);
        }
        check(loaded_v1,"Native save version 1 should load with empty Player1 policy defaults");

        lcs::NativeAdapter source(image);source.world_ready=true;
        mount_crane(source);mount_ferry(source);
        check(run(source,COMMAND_SET_DEVELOPER_FLAG,{1})==lcs::StepResult::continued,"Developer config command should be accepted");
        check(run(source,COMMAND_SET_TOTAL_NUMBER_OF_KILL_FRENZIES,{17})==lcs::StepResult::continued,"Frenzy total should be recorded");
        check(run(source,COMMAND_SET_TOTAL_EXPORT_CARS,{26})==lcs::StepResult::continued,"Export total should be recorded");
        std::vector<int32_t> crane_args;for(const float value:{0.0f,0.0f,-2.0f,2.0f,-2.0f,2.0f,20.0f,10.0f,3.0f,0.0f})append_float(crane_args,value);
        check(run(source,COMMAND_ACTIVATE_CRUSHER_CRANE,crane_args)==lcs::StepResult::continued,"Campaign crane activation should be captured without replaying MAIN");

        const auto saved=source.save_state(1200);
        lcs::NativeAdapter restored(image);mount_crane(restored);mount_ferry(restored);
        check(restored.restore_state(saved,9000,diagnostic),diagnostic.c_str());
        check(restored.kill_frenzies_total()==17 && restored.export_cars_total()==26,"Normal-save LCS totals should survive the paired load");
        check(restored.crane_service.count()==1 && restored.active_crane_declarations()==1,"Saved crusher activation should restore from the mounted IPL declaration");
        check(restored.save_state(9000)==saved,"Quiescent service state should round-trip canonically across timer epochs");

        const auto before_bad_restore=restored.save_state(9000);
        auto unsupported=saved;unsupported[4]=uint8_t(unsupported[4]+1);
        check(!restored.restore_state(unsupported,9000,diagnostic) && !diagnostic.empty(),"Unknown adapter save versions should fail explicitly");
        auto truncated=saved;truncated.pop_back();
        check(!restored.restore_state(truncated,9000,diagnostic) && !diagnostic.empty(),"Truncated adapter save states should fail explicitly");
        auto trailing=saved;trailing.push_back(0);
        check(!restored.restore_state(trailing,9000,diagnostic) && !diagnostic.empty(),"Trailing adapter save bytes should fail explicitly");
        check(restored.save_state(9000)==before_bad_restore,"Rejected save images must not partially replace adapter state");

        std::vector<uint8_t> query;opcode(query,uint16_t(COMMAND_IS_DEVELOPER));
        auto vm=program(std::move(query));vm.backend=[&](lcs::Vm& v,lcs::Thread& t,uint16_t command){return restored.dispatch(v,t,command);};
        lcs::Thread thread;thread.ip=64;
        check(vm.step(thread,0)==lcs::StepResult::continued && thread.condition,"Developer build configuration should be restored");

        // A committed player outfit alias is persistent adapter state. The
        // normal SA save recreates the ped, so the pointer changes while its
        // player handle, model and three saved coordinates remain verifiable.
        lcs::NativeAdapter outfit_source(image);std::vector<lcs::NativeCall> source_calls;
        const std::array<float,3> player_position{12.5f,-4.0f,7.25f};
        bind_player_facts(outfit_source,42,uintptr_t(0x11110000),297,player_position,source_calls);
        outfit_source.equivalent_entities=true;
        outfit_source.set_pednode_seek=[](int32_t,bool){return true;};
        lcs::Thread outfit_thread;
        check(run_label(outfit_source,COMMAND_UNDRESS_CHAR,42,"plr",outfit_thread)==lcs::StepResult::continued,
              "Player outfit request should pass through the native outfit command path");
        std::vector<uint8_t> dress_code;opcode(dress_code,COMMAND_DRESS_CHAR);integer(dress_code,42);
        auto dress_vm=program(std::move(dress_code));dress_vm.backend=[&](lcs::Vm& v,lcs::Thread& t,uint16_t command){return outfit_source.dispatch(v,t,command);};
        outfit_thread={};outfit_thread.ip=64;
        check(dress_vm.step(outfit_thread,0)==lcs::StepResult::continued,
              "Player outfit alias should commit through the native dress helper");
        check(run(outfit_source,COMMAND_SET_CHAR_RUNNING,{42,1})==lcs::StepResult::continued,
              "Player running policy should be accepted before the paired save");
        check(run(outfit_source,COMMAND_SET_CHAR_USE_PEDNODE_SEEK,{42,1})==lcs::StepResult::continued,
              "Player pednode-seek policy should be accepted before the paired save");
        check(run(outfit_source,COMMAND_SET_CHAR_ONLY_ALLOWED_TO_SIT_IN_FRONT,{42,1})==lcs::StepResult::continued,
              "Player front-seat policy should be accepted before the paired save");
        std::optional<bool> player_has_no_objective=true;
        outfit_source.char_has_no_objective=[&](int32_t actor)->std::optional<bool>{return actor==42?player_has_no_objective:std::nullopt;};
        std::vector<lcs::NativeCall> source_walks;
        outfit_source.execute_walking_objective=[&](const lcs::NativeCall& call){source_walks.push_back(call);lcs::NativeResult result;result.executed=true;result.native_entered=true;return result;};
        std::vector<int32_t> source_walk{42};append_float(source_walk,1.0f);append_float(source_walk,2.0f);
        check(run(outfit_source,COMMAND_SET_CHAR_OBJ_GOTO_COORD_ON_FOOT,source_walk)==lcs::StepResult::continued,
              "Player walking-objective cache should be created by accepted native task dispatch");
        const auto outfit_saved=outfit_source.save_state(1500);
        check(std::any_of(source_calls.begin(),source_calls.end(),[](const lcs::NativeCall& call){return call.opcode==0x01F5&&call.inputs==std::vector<int32_t>{0};}),
              "Player alias save requires the native player-handle resolver");
        check(outfit_source.save_state(1500)==outfit_saved,
              "A player task cache proven complete should be projected away canonically for a normal save");
        player_has_no_objective=false;bool active_player_refused=false;
        try{(void)outfit_source.save_state(1500);}catch(const std::exception& e){active_player_refused=std::string(e.what()).find("walking objective is active")!=std::string::npos;}
        check(active_player_refused,"A real active Player1 walking task must still reject a save that cannot reissue it");
        player_has_no_objective=std::nullopt;bool unreadable_player_refused=false;
        try{(void)outfit_source.save_state(1500);}catch(const std::exception& e){unreadable_player_refused=std::string(e.what()).find("state is unreadable")!=std::string::npos;}
        check(unreadable_player_refused,"An unreadable Player1 objective state must be rejected explicitly");
        player_has_no_objective=true;source_calls.clear();
        check(run(outfit_source,COMMAND_SET_CHAR_RUNNING,{42,0})==lcs::StepResult::continued,
              "Saving must not erase the live player walking-objective cache");
        check(std::any_of(source_calls.begin(),source_calls.end(),[](const lcs::NativeCall& call){return call.opcode==0x05F5&&call.inputs.size()>=5&&call.inputs[0]==42&&call.inputs[4]==4;}),
              "The source player task cache should still drive a running-state update after snapshot projection");
        lcs::NativeAdapter outfit_restored(image);std::vector<lcs::NativeCall> restored_calls;
        bind_player_facts(outfit_restored,42,uintptr_t(0x22220000),297,player_position,restored_calls);
        outfit_restored.equivalent_entities=true;
        std::vector<std::pair<int32_t,bool>> restored_seek_policies;
        outfit_restored.set_pednode_seek=[&](int32_t actor,bool enabled){restored_seek_policies.emplace_back(actor,enabled);return true;};
        check(outfit_restored.restore_state(outfit_saved,9500,diagnostic),diagnostic.c_str());
        check(std::any_of(restored_calls.begin(),restored_calls.end(),[](const lcs::NativeCall& call){return call.opcode==0x01F5&&call.inputs==std::vector<int32_t>{0};}),
              "Player alias restore must re-resolve the handle from player zero");
        const auto read_three_coords=std::count_if(restored_calls.begin(),restored_calls.end(),[](const lcs::NativeCall& call){return call.opcode==0x00A0&&call.inputs==std::vector<int32_t>{42}&&call.outputs==3;});
        check(read_three_coords==1,"Player alias restore must verify all three saved native coordinates");
        check(run_label(outfit_restored,COMMAND_IS_PLAYER_WEARING,0,"plr",outfit_thread)==lcs::StepResult::continued&&outfit_thread.condition,
              "Restored player outfit identity should satisfy the original query after native ped rebasing");
        check(outfit_restored.save_state(9500)==outfit_saved,"Player outfit alias should survive canonical paired-save round-trip");
        check(restored_seek_policies==std::vector<std::pair<int32_t,bool>>{{42,true}},
              "Saved player pednode policy should be reapplied to the new native ped identity");
        restored_calls.clear();
        check(run(outfit_restored,COMMAND_SET_CHAR_OBJ_ENTER_CAR_AS_PASSENGER,{42,43})==lcs::StepResult::continued,
              "Restored seat policy should remain usable after the SA player ped was recreated");
        check(std::any_of(restored_calls.begin(),restored_calls.end(),[](const lcs::NativeCall& call){return call.opcode==0x05CA&&call.inputs==std::vector<int32_t>({42,43,-1,0});}),
              "Front-seat policy should be rebound to the live player identity");
        std::vector<lcs::NativeCall> restored_walks;
        outfit_restored.execute_walking_objective=[&](const lcs::NativeCall& call){restored_walks.push_back(call);lcs::NativeResult result;result.executed=true;result.native_entered=true;return result;};
        std::vector<int32_t> player_walk{42};append_float(player_walk,1.0f);append_float(player_walk,2.0f);
        check(run(outfit_restored,COMMAND_SET_CHAR_OBJ_GOTO_COORD_ON_FOOT,player_walk)==lcs::StepResult::continued&&
              !restored_walks.empty()&&restored_walks.back().inputs.size()>=6&&restored_walks.back().inputs[4]==6,
              "Restored running policy should select run speed for the player's next native walking task");

        // Native LOAD may retain a referenced original special slot. The
        // skin consumer safely uses the other reserved slot for the same outfit.
        lcs::NativeAdapter alternate_player(image);std::vector<lcs::NativeCall> alternate_calls;
        bind_player_facts(alternate_player,42,uintptr_t(0x22330000),298,player_position,alternate_calls);
        alternate_player.equivalent_entities=true;
        alternate_player.set_pednode_seek=[](int32_t,bool){return true;};
        std::optional<std::string> verified_outfit;
        alternate_player.current_outfit=[&](int32_t actor)->std::optional<std::string>{return actor==42?verified_outfit:std::nullopt;};
        check(!alternate_player.restore_state(outfit_saved,9500,diagnostic),"A changed native player slot needs a verified owned outfit");
        verified_outfit="plr2";
        check(!alternate_player.restore_state(outfit_saved,9500,diagnostic),"A different owned outfit must not satisfy a saved worn-outfit alias");
        verified_outfit="plr";
        check(alternate_player.restore_state(outfit_saved,9500,diagnostic),diagnostic.c_str());
        const auto alternate_snapshot=alternate_player.save_state(9500);
        check(alternate_snapshot!=outfit_saved,"Rebased outfit must store its current native model slot");
        check(alternate_player.restore_state(alternate_snapshot,9500,diagnostic),diagnostic.c_str());

        // Dead/stale NPC records are omitted from a normal SA save because
        // SavePedPool only writes Player1. Projection must leave the current
        // adapter untouched so the same still-live actor continues correctly.
        lcs::NativeAdapter npc_source(image);npc_source.world_ready=true;npc_source.equivalent_entities=true;
        bool npc_alive=true;std::vector<lcs::NativeCall> npc_calls,npc_walks;
        npc_source.live_ped_identity=[&](int32_t actor)->std::optional<lcs::ObjectiveActor>{
            if(actor==42)return lcs::ObjectiveActor{42,uintptr_t(0x33000000)};
            if(actor==99&&npc_alive)return lcs::ObjectiveActor{99,uintptr_t(0x44000000)};
            return std::nullopt;
        };
        npc_source.execute=[&](const lcs::NativeCall& call){
            npc_calls.push_back(call);lcs::NativeResult result;result.executed=true;result.condition=true;
            if(call.opcode==0x01F5)result.values[0]=42;
            if(call.opcode==0x056D)result.condition=call.inputs[0]==42||(call.inputs[0]==99&&npc_alive);
            if(call.opcode==0x0665)result.values[0]=177;
            if(call.opcode==0x02CE){const float z=15.0f;std::memcpy(&result.values[0],&z,sizeof(z));}
            return result;
        };
        npc_source.set_pednode_seek=[](int32_t,bool){return true;};
        npc_source.execute_walking_objective=[&](const lcs::NativeCall& call){npc_walks.push_back(call);lcs::NativeResult result;result.executed=true;result.native_entered=true;return result;};
        lcs::Thread npc_thread;
        check(run_label(npc_source,COMMAND_UNDRESS_CHAR,99,"npcskin",npc_thread)==lcs::StepResult::continued&&
              run(npc_source,COMMAND_DRESS_CHAR,{99})==lcs::StepResult::continued,
              "NPC outfit alias should be created through original dispatch producers");
        check(run_label(npc_source,COMMAND_UNDRESS_CHAR,99,"npcskin",npc_thread)==lcs::StepResult::continued,
              "NPC pending outfit alias should remain live across the projection test");
        check(run(npc_source,COMMAND_SET_CHAR_RUNNING,{99,1})==lcs::StepResult::continued&&
              run(npc_source,COMMAND_SET_CHAR_USE_PEDNODE_SEEK,{99,1})==lcs::StepResult::continued&&
              run(npc_source,COMMAND_SET_CHAR_ONLY_ALLOWED_TO_SIT_IN_FRONT,{99,1})==lcs::StepResult::continued,
              "NPC running, pednode and seat caches should be created through original dispatch producers");
        std::vector<int32_t> npc_walk{99};append_float(npc_walk,3.0f);append_float(npc_walk,4.0f);
        check(run(npc_source,COMMAND_SET_CHAR_OBJ_GOTO_COORD_ON_FOOT,npc_walk)==lcs::StepResult::continued&&!npc_walks.empty(),
              "NPC walking-objective cache should be created by accepted native task dispatch");
        npc_alive=false;
        const auto npc_snapshot=npc_source.save_state(3300);
        npc_alive=true;npc_calls.clear();
        check(run(npc_source,COMMAND_SET_CHAR_RUNNING,{99,1})==lcs::StepResult::continued,
              "NPC policy mutation should still work after save projection");
        check(std::any_of(npc_calls.begin(),npc_calls.end(),[](const lcs::NativeCall& call){return call.opcode==0x05F5&&call.inputs.size()>=5&&call.inputs[0]==99&&call.inputs[4]==6;}),
              "Save projection must not erase the source adapter's NPC walking-task cache");
        check(run(npc_source,COMMAND_DRESS_CHAR,{99})==lcs::StepResult::continued,
              "Save projection must not erase the source adapter's pending NPC outfit alias");
        lcs::NativeAdapter npc_loaded(image);npc_loaded.world_ready=true;npc_loaded.equivalent_entities=true;
        npc_loaded.execute=[](const lcs::NativeCall& call){lcs::NativeResult result;result.executed=true;result.condition=true;if(call.opcode==0x056D)result.condition=true;return result;};
        check(npc_loaded.restore_state(npc_snapshot,3300,diagnostic),diagnostic.c_str());
        check(run(npc_loaded,COMMAND_DRESS_CHAR,{99})==lcs::StepResult::unsupported,
              "A non-player NPC outfit alias must not be falsely restored onto a post-load actor");

        // CREATE_CAR returns a real SA handle to SCM and records the chosen
        // original model. A pool-proven absent handle is stale sidecar state;
        // an unavailable/malformed read is not proof of deletion.
        lcs::NativeAdapter vehicle_source(image);vehicle_source.world_ready=true;
        vehicle_source.bind_model(152,547,"kuruma",lcs::ModelKind::vehicle);
        vehicle_source.bind_model(153,547,"kuruma_alias",lcs::ModelKind::vehicle);
        vehicle_source.native_model_class=[](int32_t model)->std::optional<uint8_t>{return model==547?std::optional<uint8_t>(6):std::nullopt;};
        lcs::VehicleLookupStatus vehicle_status=lcs::VehicleLookupStatus::absent;
        lcs::RouteVehicleSnapshot vehicle_snapshot;vehicle_snapshot.handle=81;vehicle_snapshot.entity=uintptr_t(0x55660000);
        vehicle_snapshot.generation=uint8_t(81);vehicle_snapshot.position={10.0f,20.0f,30.0f};
        vehicle_source.exclusive_garages.world.vehicle_lookup=[&](int32_t handle)->lcs::RouteVehicleLookup{
            if(handle!=81)return {lcs::VehicleLookupStatus::absent,std::nullopt};
            if(vehicle_status==lcs::VehicleLookupStatus::present)return {vehicle_status,vehicle_snapshot};
            return {vehicle_status,std::nullopt};
        };
        std::vector<lcs::NativeCall> vehicle_calls;
        vehicle_source.execute=[&](const lcs::NativeCall& call){
            vehicle_calls.push_back(call);lcs::NativeResult result;result.executed=true;result.condition=true;
            if(call.opcode==0x00A5)result.values[0]=81;
            if(call.opcode==0x0441)result.values[0]=547;
            return result;
        };
        std::vector<uint8_t> create_car;opcode(create_car,COMMAND_CREATE_CAR);integer(create_car,153);
        integer(create_car,0);integer(create_car,0);integer(create_car,0);create_car.push_back(12);
        auto create_vm=program(std::move(create_car));create_vm.backend=[&](lcs::Vm& v,lcs::Thread& t,uint16_t command){return vehicle_source.dispatch(v,t,command);};
        lcs::Thread vehicle_thread;vehicle_thread.ip=64;
        check(create_vm.step(vehicle_thread,0)==lcs::StepResult::continued&&vehicle_thread.locals[0]==81,
              "Original CREATE_CAR should return the real created SA vehicle handle to its SCM output");
        const auto query_car_model=[](lcs::NativeAdapter& adapter,int32_t handle){
            std::vector<uint8_t> code;opcode(code,COMMAND_GET_CAR_MODEL);integer(code,handle);code.push_back(12);
            auto vm=program(std::move(code));vm.backend=[&](lcs::Vm& v,lcs::Thread& t,uint16_t command){return adapter.dispatch(v,t,command);};
            lcs::Thread thread;thread.ip=64;const auto result=vm.step(thread,0);return std::pair{result,thread.locals[0]};
        };
        const auto absent_snapshot=vehicle_source.save_state(4000);
        vehicle_status=lcs::VehicleLookupStatus::unavailable;std::string vehicle_error;
        try{(void)vehicle_source.save_state(4000);}catch(const std::exception& e){vehicle_error=e.what();}
        check(vehicle_error.find("81")!=std::string::npos&&vehicle_error.find("unavailable")!=std::string::npos,
              "Unavailable vehicle-pool reads must refuse with the alias handle and lookup status");
        vehicle_status=lcs::VehicleLookupStatus::present;
        check(query_car_model(vehicle_source,81).first==lcs::StepResult::continued&&query_car_model(vehicle_source,81).second==153,
              "An absent snapshot must not erase the live adapter's still-valid original vehicle alias");
        vehicle_snapshot.generation=7;std::string malformed_vehicle_error;
        try{(void)vehicle_source.save_state(4000);}catch(const std::exception& e){malformed_vehicle_error=e.what();}
        check(malformed_vehicle_error.find("81")!=std::string::npos&&malformed_vehicle_error.find("present")!=std::string::npos,
              "A malformed present vehicle snapshot must refuse with its handle and lookup status");
        vehicle_snapshot.generation=uint8_t(81);
        lcs::NativeAdapter absent_vehicle_load(image);absent_vehicle_load.world_ready=true;
        absent_vehicle_load.bind_model(152,547,"kuruma",lcs::ModelKind::vehicle);
        absent_vehicle_load.bind_model(153,547,"kuruma_alias",lcs::ModelKind::vehicle);
        absent_vehicle_load.native_model_class=vehicle_source.native_model_class;
        absent_vehicle_load.execute=[](const lcs::NativeCall& call){lcs::NativeResult result;result.executed=true;result.condition=true;if(call.opcode==0x0441)result.values[0]=547;return result;};
        check(absent_vehicle_load.restore_state(absent_snapshot,4000,diagnostic),diagnostic.c_str());
        const auto absent_model=query_car_model(absent_vehicle_load,81);
        check(absent_model.first==lcs::StepResult::continued&&absent_model.second==152,
              "A pool-proven absent handle must be omitted from the paired snapshot's vehicle aliases");

        const auto present_snapshot=vehicle_source.save_state(4100);
        lcs::NativeAdapter present_vehicle_load(image);present_vehicle_load.world_ready=true;
        present_vehicle_load.bind_model(152,547,"kuruma",lcs::ModelKind::vehicle);
        present_vehicle_load.bind_model(153,547,"kuruma_alias",lcs::ModelKind::vehicle);
        present_vehicle_load.native_model_class=vehicle_source.native_model_class;
        present_vehicle_load.exclusive_garages.world.vehicle_lookup=vehicle_source.exclusive_garages.world.vehicle_lookup;
        present_vehicle_load.execute=[](const lcs::NativeCall& call){lcs::NativeResult result;result.executed=true;result.condition=true;if(call.opcode==0x0441)result.values[0]=547;return result;};
        check(present_vehicle_load.restore_state(present_snapshot,4100,diagnostic),diagnostic.c_str());
        const auto present_model=query_car_model(present_vehicle_load,81);
        check(present_model.first==lcs::StepResult::continued&&present_model.second==153,
              "A present vehicle alias should round-trip with its original model identity");
        check(present_vehicle_load.save_state(4100)==present_snapshot,
              "A present vehicle alias must preserve its saved generation, model, and position across restore");

        vehicle_status=lcs::VehicleLookupStatus::absent;const auto absent_at_4100=vehicle_source.save_state(4100);
        lcs::NativeAdapter absent_after_load(image);absent_after_load.world_ready=true;
        absent_after_load.bind_model(152,547,"kuruma",lcs::ModelKind::vehicle);
        absent_after_load.bind_model(153,547,"kuruma_alias",lcs::ModelKind::vehicle);
        absent_after_load.native_model_class=vehicle_source.native_model_class;
        absent_after_load.exclusive_garages.world.vehicle_lookup=vehicle_source.exclusive_garages.world.vehicle_lookup;
        absent_after_load.execute=[](const lcs::NativeCall& call){lcs::NativeResult result;result.executed=true;result.condition=true;if(call.opcode==0x0441)result.values[0]=547;return result;};
        check(absent_after_load.restore_state(present_snapshot,4100,diagnostic),diagnostic.c_str());
        const auto post_load_absent_model=query_car_model(absent_after_load,81);
        check(post_load_absent_model.first==lcs::StepResult::continued&&post_load_absent_model.second==152,
              "A vehicle proven absent after load must not regain its archived original-model alias");
        check(absent_after_load.save_state(4100)==absent_at_4100,
              "Restore should consume a stale vehicle-alias record without reserializing it");

        std::cout<<"native_save_tests passed\n";return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
