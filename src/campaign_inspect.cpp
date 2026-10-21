#include "lcs/runtime.hpp"
#include "lcs/engine_dispatch.hpp"
#include "lcs/native.hpp"
#include "lcs/pause_menu.hpp"
#include "lcs/hud.hpp"
#include "lcs/equivalents.hpp"
#include "lcs/audio.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <set>
#include <filesystem>
#include <memory>
#include "ScriptCommands.h"

// Records calls to the actual adapter without invoking SA. Return values belong
// to this explicit simulation and cannot establish mission playability.
struct SimSpec {std::vector<bool> strings;size_t outputs;bool predicate;};
static std::optional<SimSpec> signature(uint16_t op) {
    switch(op) {
#include "diagnostic_specs.inc"
    default:return {};
    }
}
int main(int argc,char** argv) {
    try {
        if(argc<3 || argc>8)throw std::runtime_error("Usage: lcs-campaign-inspect MAIN.SCM model-bindings.tsv [mission-index|main] [--simulate-missing | --equivalents FILE [--cutscene-index FILE]]");
        const bool simulate_missing=argc==5 && std::string(argv[4])=="--simulate-missing";
        const bool equivalents=(argc==6 || argc==8) && std::string(argv[4])=="--equivalents";
        if(argc==5 && !simulate_missing)throw std::runtime_error("Unknown simulation option");
        if((argc==6 || argc>=7) && !equivalents)throw std::runtime_error("Unknown equivalents option");
        auto image=lcs::ScmImage::load(argv[1]);
        lcs::NativeAdapter adapter(image);
        std::ifstream bindings(argv[2]);
        if(!bindings)throw std::runtime_error("Cannot read model bindings");
        std::string line;
        while(std::getline(bindings,line)) {
            std::istringstream row(line);int lcs_id=0,sa_id=0;std::string name;
            if(!(row>>lcs_id>>sa_id>>name))throw std::runtime_error("Invalid binding");
            adapter.bind_model(lcs_id,sa_id,name);
        }
        if(equivalents) {
            std::ifstream profile(argv[5]);if(!profile)throw std::runtime_error("Cannot read equivalents profile");
            lcs::bind_equivalents(adapter,lcs::read_equivalents(profile));
        }
        if(argc==8) {
            if(std::string(argv[6])!="--cutscene-index")throw std::runtime_error("Unknown cutscene index option");
            std::ifstream index(argv[7]);std::string header;
            if(!index || !std::getline(index,header) || header.rfind("LCSCUT1\t",0)!=0)throw std::runtime_error("Invalid mounted cutscene index");
            while(std::getline(index,line)) {
                std::istringstream row(line);std::string original,alias,hash,extra;
                if(!(row>>original>>alias>>hash) || row>>extra || hash.size()!=64)throw std::runtime_error("Invalid mounted scene alias");
                adapter.bind_cutscene(original,alias);
            }
        }
        adapter.world_ready=true;
        adapter.flat_native_world=equivalents;
        if(equivalents) {
            const auto zone_path=std::filesystem::path(argv[5]).parent_path()/"lcs-converted"/"DATA"/"GTA3.ZON";
            std::ifstream zones(zone_path);if(!zones)throw std::runtime_error("Cannot read original campaign zones");
            adapter.load_original_zones(zones);
        }
        adapter.text_ready=true;
        adapter.reuse_native_player=true;
        adapter.recovery_taxi_deferred=true;
        for(const auto* name:{"world0.ipl","world1.ipl","world2.ipl"}) {
            std::ifstream placements(std::filesystem::path(argv[2]).parent_path()/name);
            if(!placements)throw std::runtime_error("Cannot read mounted world phone placements");
            // Resolve the original model by its verified binding, not SA identity.
            const auto phone=adapter.model(531);
            if(!phone)throw std::runtime_error("Missing original phonebooth1 model binding");
            adapter.load_phone_placements(placements,*phone);
            const auto crusher=adapter.model(3542);
            if(!crusher)throw std::runtime_error("Missing original crusher crane binding");
            placements.clear();placements.seekg(0);adapter.load_crane_placements(placements,*crusher);
        }
        lcs::PauseMenuGate pause_gate;
        lcs::HudBridge hud;
        int32_t simulated_zoom=2;
        adapter.camera_ped_zoom=[&](std::optional<int32_t> setting)->std::optional<int32_t>{if(setting)simulated_zoom=*setting;return simulated_zoom;};
        adapter.freeze_ped_zoom=[](bool){return true;};
        adapter.switch_angled_roads=[](const std::array<float,7>&,bool,bool){return true;};
        adapter.char_in_control=[](int32_t)->std::optional<bool>{return true;};
        adapter.player_made_safe=[]()->std::optional<bool>{return false;};
        adapter.random_char_in_area=[](const std::array<float,4>&,const std::array<int32_t,3>&,int32_t,bool)->std::optional<int32_t>{return -1;};
        adapter.stored_player_weapon=[](int32_t)->std::optional<int32_t>{return 0;};
        adapter.create_garage=[](const std::array<float,8>&,int32_t,int32_t id)->std::optional<std::string>{return "LCSG"+std::to_string(id);};
        adapter.rotate_garage_door=[](const std::string&){return true;};
        adapter.leave_garage_camera=[](const std::string&){return true;};
        adapter.set_garage_capacity=[](const std::string&,uint8_t value){return value<=4;};
        adapter.clear_garage_message=[](){return true;};
        adapter.scripted_pad_state=[](int32_t)->std::optional<int32_t>{return 0;};
        adapter.set_char_crouch=[](int32_t,bool,int32_t value){return value>=0 && value<=65535;};
        adapter.set_char_can_fight=[](int32_t,bool){return true;};
        adapter.set_char_personality=[](int32_t,const std::string&){return true;};
        adapter.display_message=[](const lcs::MissionMessage& message){std::cout<<"simulated ordered message key="<<message.key<<" soon="<<message.soon<<'\n';return true;};
        std::set<int32_t> available_gestures;
        if(equivalents) {
            std::ifstream gesture_index(std::filesystem::path(argv[5]).parent_path()/"gameplay-animations-v3"/"index.tsv");
            while(std::getline(gesture_index,line)){std::istringstream row(line);int32_t state;if(!(row>>state))throw std::runtime_error("Invalid original gesture index");available_gestures.insert(state);}
        }
        adapter.request_wait_animation=[&](int32_t state,bool){return available_gestures.count(state)!=0;};
        adapter.play_wait_animation=[&](int32_t,int32_t state,int32_t,bool){return available_gestures.count(state)!=0;};
        adapter.clear_wait_animation=[](int32_t){return true;};
        std::array<int32_t,2> simulated_collectables{};
        adapter.collectables=[&](bool total,std::optional<int32_t> setting)->std::optional<int32_t>{auto& value=simulated_collectables[total?1:0];if(setting)value=*setting;return value;};
        adapter.disable_pause_menu=[&](bool disabled){pause_gate.disabled=disabled;std::cout<<"recorded_frontend_pause_disabled="<<disabled<<" native_game_executed=false\n";return true;};
        size_t calls=0;
        uint32_t simulation_time=0;
        std::unique_ptr<lcs::AudioArchive> audio_archive;
        std::unique_ptr<lcs::AudioAdapter> audio;
        std::array<uint32_t,3> simulated_speech_end{};
        if(equivalents) {
            audio_archive=std::make_unique<lcs::AudioArchive>(std::filesystem::path(argv[5]).parent_path()/"lcs-converted"/"audio");
            audio=std::make_unique<lcs::AudioAdapter>(*audio_archive);
            audio->play=[&](size_t slot,const lcs::AudioSample& sample){simulated_speech_end.at(slot)=simulation_time+uint32_t(sample.pcm.size()*1000ull/(sample.frequency*sample.channels*2ull));return true;};
            audio->finished=[&](size_t slot){return simulation_time>=simulated_speech_end.at(slot);};
            audio->stop=[&](size_t slot){simulated_speech_end.at(slot)=0;};
        }
        bool show_call=true;
        std::array<int32_t,3> simulated_player_position{};
        std::set<std::pair<uint32_t,uint16_t>> visited;
        adapter.execute=[&](const lcs::NativeCall& call) {
            if(show_call) {
                std::cout<<" native=0x"<<std::hex<<call.opcode<<std::dec<<" inputs=";
                for(auto value:call.inputs)std::cout<<value<<',';
                std::cout<<" outputs="<<call.outputs;
                if(call.text)std::cout<<" text="<<*call.text;
                std::cout<<'\n';
            }
            lcs::NativeResult result;result.executed=true;result.condition=false;
            // Only engine readiness is true in this idle-player diagnostic.
            // Objective/region/pickup/cheat queries cannot manufacture progress.
            if(call.opcode==0x0248 || call.opcode==0x0256 || call.opcode==0x056D || call.opcode==0x06B9 || call.opcode==0x02E9)result.condition=true;
            for(size_t i=0;i<call.outputs;++i)result.values[i]=int32_t(100+i);
            if(call.opcode==0x01BD || call.opcode==0x02E8)result.values[0]=int32_t(simulation_time);
            if(call.opcode==0x0470 || call.opcode==0x01FA || call.opcode==0x00E2)result.values[0]=0;
            if(call.opcode==0x0327)result.values[0]=-1; // No ambient vehicle in this idle fixture.
            if(call.opcode==0x07AF)result.values[0]=50;
            if(call.opcode==0x07FD)result.condition=call.inputs[0]==50;
            if(call.opcode==0x00A1)for(int i=0;i<3;++i)simulated_player_position[i]=call.inputs[i+1];
            if(call.opcode==0x00A0)for(int i=0;i<3;++i)result.values[i]=simulated_player_position[i];
            ++calls;return result;
        };
        lcs::Runtime runtime(image);
        adapter.mission_script=[&](const lcs::Thread& t){return runtime.is_mission_script(t);};
        std::set<uint32_t> flow_sites;
        runtime.vm().before_step=[&](lcs::Vm& vm,lcs::Thread& t) {
            if(!flow_sites.insert(t.ip).second)return false;
            const auto op=lcs::read16(vm.memory,t.ip)&0x7fff;
            if(op==COMMAND_SET_VAR_INT || op==COMMAND_DECLARE_MISSION_FLAG) {
                auto cursor=t;cursor.ip+=2;
                std::cout<<"global ip="<<t.ip<<" ref="<<vm.global_reference(cursor);
                if(op==COMMAND_SET_VAR_INT)std::cout<<" value="<<vm.parameter(cursor);
                std::cout<<'\n';
            }
            if(op==COMMAND_GOSUB || op==COMMAND_GOSUB_FILE || op==COMMAND_CALL || op==COMMAND_RETURN) {
                auto cursor=t;cursor.ip+=2;
                if(op==COMMAND_CALL)cursor.ip+=3;
                std::cout<<"flow ip="<<t.ip<<" depth="<<t.stack_size<<" command="<<lcs::opcode_name(op);
                if(op!=COMMAND_RETURN)std::cout<<" target="<<vm.parameter(cursor);
                std::cout<<'\n';
            }
            return false;
        };
        runtime.engine=[&](lcs::Vm& vm,lcs::Thread& t,uint16_t op) {
            show_call=visited.emplace(t.last_ip,op).second;
            if(show_call)std::cout<<"engine ip="<<t.last_ip<<" name="<<t.name<<" command="<<lcs::opcode_name(op)<<'\n';
            const auto before=t.ip;
            std::optional<lcs::StepResult> owner;
            if(audio)owner=lcs::try_engine_adapter(*audio,vm,t,op);
            if(!owner)owner=lcs::try_engine_adapter(hud,vm,t,op);
            const auto result_owned=owner.has_value();
            auto result=owner?*owner:adapter.dispatch(vm,t,op);
            if(result==lcs::StepResult::unsupported && simulate_missing && !result_owned) {
                if(const auto spec=signature(op)) {
                    t.ip=before;
                    if(show_call)std::cout<<"SIMULATED_UNIMPLEMENTED command="<<lcs::opcode_name(op)<<" inputs=";
                    for(bool string:spec->strings) {
                        if(string) {const auto v=vm.text_label(t);if(show_call)std::cout<<v<<',';}
                        else {const auto v=vm.parameter(t);if(show_call)std::cout<<v<<',';}
                    }
                    for(size_t i=0;i<spec->outputs;++i)vm.store(t,100);
                    // This is an inventory path, not a production fallback.
                    // The original debug source omits some predicate annotations.
                    vm.compare(t,op!=COMMAND_IS_CHAR_DEAD && op!=COMMAND_IS_CAR_DEAD);
                    if(show_call)std::cout<<" outputs="<<spec->outputs<<" native_implementation=false\n";
                    t.diagnostic.clear();return lcs::StepResult::continued;
                }
            }
            if(result==lcs::StepResult::unsupported) {
                std::cout<<"blocked_bytes=";
                for(size_t i=t.last_ip;i<vm.memory.size() && i<t.last_ip+48;++i)
                    std::cout<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(vm.memory[i])<<' ';
                std::cout<<std::dec<<"\nreason="<<t.diagnostic<<'\n';
            }
            return result;
        };
        const auto root=argc>=4 && std::string(argv[3])!="main" ? runtime.load_mission(std::stoul(argv[3])) : runtime.start_main();
        std::cout<<"simulation=true\nnative_game_executed=false\nplayability_verified=false\n";
        for(uint32_t now=0;now<60000;now+=16) {
            simulation_time=now;
            if(audio && !audio->tick_announcements(false))throw std::runtime_error("News backend missing in offline diagnostic");
            bool stop=false;
            for(const auto& event:runtime.tick(now,16,10000)) {
                if(event.result==lcs::StepResult::unsupported || event.result==lcs::StepResult::failed) {
                    std::cout<<"stop thread="<<event.id<<" command="<<lcs::opcode_name(event.opcode)<<" diagnostic="<<event.diagnostic<<'\n';
                    stop=true;
                }
            }
            if(stop || !runtime.script(root).thread.active)break;
        }
        std::cout<<"recorded_native_calls="<<calls<<"\nactive_original_scripts="<<runtime.active_count()<<"\ncurrent_original_mission=";
        if(runtime.mission_index())std::cout<<*runtime.mission_index();else std::cout<<"none";
        std::cout<<'\n';return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
