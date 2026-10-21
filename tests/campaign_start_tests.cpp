#include "lcs/runtime.hpp"
#include "lcs/engine_dispatch.hpp"
#include "lcs/native.hpp"
#include "lcs/hud.hpp"
#include "lcs/pause_menu.hpp"
#include "ScriptCommands.h"
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <sstream>
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(int argc,char** argv){try {
    check(argc==3,"Expected original LCS MAIN.SCM and mounted world bindings");
    const auto image=lcs::ScmImage::load(argv[1]);lcs::Runtime runtime(image);
    lcs::NativeAdapter native(image);lcs::HudBridge hud;lcs::PauseMenuGate pause;
    std::vector<lcs::NativeCall> calls;std::vector<uint16_t> commands;
    native.world_ready=true;native.text_ready=true;
    native.recovery_taxi_deferred=true;
    std::ifstream bindings(argv[2]);check(bool(bindings),"Mounted original bindings missing");std::string line;
    while(std::getline(bindings,line)){std::istringstream row(line);int32_t original=0,target=0;std::string name;check(bool(row>>original>>target>>name),"Invalid fixture binding");native.bind_model(original,target,name);}
    for(const auto* name:{"world0.ipl","world1.ipl","world2.ipl"}){
        std::ifstream placements(std::filesystem::path(argv[2]).parent_path()/name);check(bool(placements),"Mounted original IPL missing");
        native.load_phone_placements(placements,*native.model(531));
        placements.clear();placements.seekg(0);native.load_crane_placements(placements,*native.model(3542));
    }
    native.execute=[&](const lcs::NativeCall& call){calls.push_back(call);lcs::NativeResult r;r.executed=true;r.condition=call.opcode==0x0248;for(size_t i=0;i<call.outputs;++i)r.values[i]=100+int32_t(i);return r;};
    native.disable_pause_menu=[&](bool disabled){pause.disabled=disabled;return true;};
    native.camera_ped_zoom=[](std::optional<int32_t>)->std::optional<int32_t>{return 2;};
    native.freeze_ped_zoom=[](bool){return true;};
    size_t native_garages=0,rotating_doors=0;
    native.create_garage=[&](const std::array<float,8>&,int32_t,int32_t id)->std::optional<std::string>{++native_garages;return "LCSG"+std::to_string(id);};
    native.rotate_garage_door=[&](const std::string&){++rotating_doors;return true;};
    size_t native_cameras=0;int32_t collected=99;
    native.leave_garage_camera=[&](const std::string&){++native_cameras;return true;};
    native.collectables=[&](bool total,std::optional<int32_t> setting)->std::optional<int32_t>{check(!total && setting==0,"Original package initializer resets actual collected count");collected=*setting;return collected;};
    // Actual production decoders and scheduler, mock native engine only. There
    // is no FirstMission profile, before_step hook or guessed command fallback.
    runtime.engine=[&](lcs::Vm& vm,lcs::Thread& t,uint16_t opcode){
        commands.push_back(opcode);
        if(const auto result=lcs::try_engine_adapter(hud,vm,t,opcode))return *result;
        return native.dispatch(vm,t,opcode);
    };
    const auto main=runtime.start_main();std::vector<lcs::Execution> events;
    lcs::ScriptId blocked=0;
    for(int i=0;i<8 && !blocked;++i){events=runtime.tick(uint32_t(i)*16,16,10000);for(const auto& e:events)if(e.result==lcs::StepResult::unsupported)blocked=e.id;}
    const auto& root=runtime.script(main);
    check(blocked && blocked!=main,"Original MAIN must reach its real initialization child");const auto& init=runtime.script(blocked);
    check(init.blocked && init.thread.active && init.thread.ip==272720 && init.thread.ip==init.thread.last_ip &&
          init.thread.last_opcode==COMMAND_CREATE_CAR_GENERATOR && init.thread.name=="initcg","Vehicle initialization holds at its first real missing model; no manual mission jump");
    check(root.thread.active && root.awaiting_initial_slice==blocked && runtime.active_count()==2 && runtime.mission_index()==2 && !runtime.on_mission(),"Original MAIN completes initob and inithp before waiting for initcg");
    check(!runtime.vm().before_step && commands.size()>100 && commands[2]==COMMAND_DISABLE_PAUSE_MENU &&
          commands[3]==COMMAND_SET_CLOCK_EVENT_WARNING,"No first-mission exclusions or bypass hook can enter strict campaign startup");
    check(pause.disabled && calls.size()>150 && calls[0].inputs==std::vector<int32_t>{111} &&
          calls[1].inputs==std::vector<int32_t>{171},"Native engine receives original campaign totals and pause state");
    check(native.shortcut_pickup_count()==6 && native.claimed_phone_count()==10 && native_garages==16 && rotating_doors==3 && native_cameras==2,"Original service declarations register 16 common garages, retaining both exclusive services separately");
    check(native.active_crane_declarations()==1 && native.declared_garage_capacity(14)==1 && native.declared_garage_player_camera(14)==true,"Original crane and collector settings are retained without substituting a native collector");
    size_t swaps=0,pickups=0;for(const auto& c:calls){if(c.opcode==0x03B6)++swaps;if(c.opcode==0x0213)++pickups;}
    check(swaps==36 && pickups==100 && collected==0,"All initialization swaps and 100 original package pickups execute before reset; no helper exclusions");
    check(lcs::read32(runtime.vm().memory,1640)==58000 && lcs::read32(runtime.vm().memory,488)==100 &&
          lcs::read32(runtime.vm().memory,1464)==0xffffffff,"Original shared initialization assignments run through their own globals");
    const auto pending_ip=root.thread.ip;runtime.tick(1000,16);
    check(runtime.script(main).thread.ip==pending_ip && runtime.script(blocked).thread.ip==272720 && runtime.script(blocked).blocked,"Time passing cannot manufacture a vehicle binding or advance a blocked command");
    check(image.mission_offsets.size()==118 && image.bytes==lcs::ScmImage::load(argv[1]).bytes,"Source MAIN and mission buffers remain unchanged");
    std::cout<<"PASS: original MAIN completes initob and inithp, 36 swaps, 100 pickups, 16 native garages and original exclusive-service settings; holds at initcg missing vehicle 189; no mission/cutscene exclusions; engine mocked, gameExecuted=false, campaignPlayable=false\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
