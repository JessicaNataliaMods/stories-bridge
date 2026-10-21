#include "lcs/runtime.hpp"
#include "lcs/engine_dispatch.hpp"
#include "lcs/equivalents.hpp"
#include "lcs/audio.hpp"
#include "lcs/hud.hpp"
#include "lcs/crouch_duration.hpp"
#include "ScriptCommands.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iostream>
#include <set>
#include <map>
#include <cstring>
#include <cmath>
using namespace lcs;
static void check(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
static int32_t bits(float f){int32_t b;std::memcpy(&b,&f,4);return b;}
static float real(int32_t b){float f;std::memcpy(&f,&b,4);return f;}
int main(int argc,char** argv){try {
    check(argc>=6 && argc<=9,"Expected MAIN, converted data, world bindings, equivalents, mounted scene index, optional converted peds, mission count and paid-shop scenario");
    const size_t expected_missions=argc>=8?size_t(std::stoi(argv[7])):3;
    const bool empty_shop=argc==9 && std::string(argv[8])=="empty-shop";
    const bool paid_shop=argc==9 && (std::string(argv[8])=="paid-shop" || empty_shop);
    check(argc<9 || paid_shop,"Unknown campaign regression scenario");
    check(expected_missions==3 || expected_missions==4,"Supported regression mission count");
    const auto image=ScmImage::load(argv[1]);Runtime runtime(image);NativeAdapter native(image);HudBridge hud;
    std::ifstream file(argv[3]);std::string line;
    std::set<int32_t> carriers;
    if(argc>=7){std::ifstream anchors(std::filesystem::path(argv[3]).parent_path()/"campaign-anchor-bindings.tsv");while(std::getline(anchors,line)){std::istringstream row(line);int id;row>>id;carriers.insert(id);}}
    while(std::getline(file,line)){std::istringstream row(line);int a,b;std::string name;check(bool(row>>a>>b>>name),"Invalid world binding");native.bind_model(a,b,name,carriers.count(a)?ModelKind::world_collision:ModelKind::object);}
    if(argc>=7){
        // Exercise the same mounted alias pass as the ASI constructor, including
        // collision carriers. Previously this pass was absent from the fixture.
        for(const auto* index:{"model-bindings.tsv","campaign-anchor-bindings.tsv"}){
            std::ifstream aliases(std::filesystem::path(argv[3]).parent_path()/index);
            while(std::getline(aliases,line)){std::istringstream row(line);int a,b;std::string name;check(bool(row>>a>>b>>name),"Invalid mounted alias");native.bind_named_model(b,name,carriers.count(a)?ModelKind::world_collision:ModelKind::object);}
        }
    }
    std::ifstream equivalents(argv[4]);const auto entities=read_equivalents(equivalents);check(entities.size()==231,"Complete donor profile");bind_equivalents(native,entities);
    if(argc>=7) {
        std::ifstream canonical(argv[6]);std::string source_id,name,native_name,asset;int32_t id=0;size_t count=0;
        while(canonical>>source_id>>id>>name>>native_name>>asset){native.bind_named_model(id,name,ModelKind::ped);++count;}
        check(count==138,"Complete canonical converted mission actors");
        for(int32_t slot=12;slot<=21;++slot)native.bind_model(108+slot,124,"special"+std::to_string(slot),ModelKind::ped);
        native.named_special_characters=true;
    }
    native.world_ready=native.text_ready=native.reuse_native_player=native.flat_native_world=native.recovery_taxi_deferred=true;
    std::ifstream zones(std::filesystem::path(argv[2])/"DATA"/"GTA3.ZON");native.load_original_zones(zones);
    std::ifstream weapons(std::filesystem::path(argv[2])/"DATA/WEAPON.DAT");native.load_weapon_definitions(weapons);
    native.native_weapon_slot=[](int32_t weapon)->std::optional<int32_t>{
        if(weapon<=1)return 0;if(weapon<=9)return 1;if(weapon==22 || weapon==24)return 2;
        if(weapon>=25 && weapon<=27)return 3;if(weapon==28 || weapon==29 || weapon==32)return 4;
        if(weapon==30 || weapon==31)return 5;if(weapon==34)return 6;if(weapon==35 || weapon==37 || weapon==38)return 7;
        if(weapon==40)return 12;if(weapon==43)return 9;return 8;
    };
    native.set_threat_range_multiplier=[](uint16_t multiplier){return multiplier>=1;};
    for(const auto* name:{"world0.ipl","world1.ipl","world2.ipl"}){
        std::ifstream placements(std::filesystem::path(argv[3]).parent_path()/name);native.load_phone_placements(placements,*native.model(531));placements.clear();placements.seekg(0);native.load_crane_placements(placements,*native.model(3542));
    }
    std::ifstream scenes(argv[5]);check(bool(std::getline(scenes,line)),"Mounted scenes missing");
    while(std::getline(scenes,line)){std::istringstream row(line);std::string name,alias,hash;check(bool(row>>name>>alias>>hash),"Invalid scene row");native.bind_cutscene(name,alias);}
    native.mission_script=[&](const Thread& t){return runtime.is_mission_script(t);};
    native.char_in_control=[](int32_t)->std::optional<bool>{return true;};native.player_made_safe=[]()->std::optional<bool>{return false;};
    native.native_model_class=[](int32_t)->std::optional<uint8_t>{return 6;};
    native.scripted_pad_state=[](int32_t)->std::optional<int32_t>{return 0;};
    native.random_char_in_area=[](const std::array<float,4>&,const std::array<int32_t,3>&,int32_t,bool)->std::optional<int32_t>{return -1;};
    native.stored_player_weapon=[](int32_t)->std::optional<int32_t>{return 0;};
    native.camera_ped_zoom=[](std::optional<int32_t> value)->std::optional<int32_t>{return value?value:2;};native.freeze_ped_zoom=[](bool){return true;};
    native.disable_pause_menu=[](bool){return true;};native.switch_angled_roads=[](const std::array<float,7>&,bool,bool){return true;};
    std::vector<std::pair<std::array<float,8>,int32_t>> garage_sites;
    size_t garages=0;native.create_garage=[&](const std::array<float,8>& bounds,int32_t type,int32_t id)->std::optional<std::string>{++garages;garage_sites.push_back({bounds,type});return "LCSG"+std::to_string(id);};
    native.rotate_garage_door=native.leave_garage_camera=[](const std::string&){return true;};
    native.set_garage_capacity=[](const std::string&,uint8_t count){return count<=4;};
    native.clear_garage_message=[](){return true;};
    const auto gesture_path=argc>=7 ? std::filesystem::path(argv[4]).parent_path()/"animations/index.tsv" : std::filesystem::path(argv[4]).parent_path()/"gameplay-animations-v3/index.tsv";
    std::set<int32_t> gestures;std::ifstream gesture_index(gesture_path);
    while(std::getline(gesture_index,line)){std::istringstream row(line);int32_t state=0;check(bool(row>>state),"Original gesture declaration");gestures.insert(state);}
    check(gestures.size()==71,"Original nonstandard and standard mission gestures prepared together");
    native.request_wait_animation=[&](int32_t state,bool){return gestures.count(state)!=0;};
    native.play_wait_animation=[&](int32_t,int32_t state,int32_t,bool){return state==29 || gestures.count(state)!=0;};
    native.named_animation=[](const std::string& name,uint16_t)->std::optional<bool>{return name=="riot"?std::optional<bool>(true):std::nullopt;};
    native.clear_wait_animation=[](int32_t){return true;};
    native.set_char_crouch=[](int32_t actor,bool crouch,int32_t duration){std::cout<<"crouch actor="<<actor<<" active="<<crouch<<" duration="<<duration<<'\n';return !crouch || bool(native_crouch_duration(duration));};
    native.set_char_can_fight=[](int32_t,bool){return true;};
    native.set_char_personality=[](int32_t,const std::string&){return true;};
    native.display_message=[](const lcs::MissionMessage&){return true;};
    std::array<int32_t,2> collectables{};native.collectables=[&](bool total,std::optional<int32_t> value)->std::optional<int32_t>{auto& v=collectables[total];if(value)v=*value;return v;};
    uint32_t now=0,loaded_at=UINT32_MAX,started_at=UINT32_MAX,duration=0,playable_at=UINT32_MAX;
    std::vector<std::string> movie_order;size_t movies_started=0,passed=0,cleanup=0;int32_t next_handle=300;
    uint32_t first_completed_at=UINT32_MAX;std::string active_script;bool next_mission_loaded=false,next_mission_seen=false;
    bool third_mission_seen=false;size_t seek_updates=0,player_punches=0;
    std::set<int32_t> third_peds,dead_peds;std::map<int32_t,int32_t> ped_health;
    std::set<int32_t> fourth_peds;std::map<int32_t,int32_t> ped_types;
    std::map<std::pair<int32_t,int32_t>,std::pair<int32_t,int32_t>> inventories;
    size_t player_shots=0;bool fourth_mission_seen=false;
    uint32_t last_punch=0;
    bool in_car=false,companion_in_car=false,left_car=false;
    std::set<int32_t> peds{100},cars,mission_pickups;std::map<int32_t,int32_t> vehicle_models;
    native.live_ped_identity=[&](int32_t actor)->std::optional<ObjectiveActor>{
        const auto found=peds.find(actor);if(found==peds.end())return {};
        return ObjectiveActor{actor,reinterpret_cast<uintptr_t>(&*found)};
    };
    std::map<int32_t,bool> heed_threats;native.set_char_heed_threats=[&](int32_t actor,bool value){if(!peds.count(actor))return false;heed_threats[actor]=value;return true;};
    std::set<int32_t> scared_crouch;native.set_char_crouch_when_threatened=[&](int32_t actor){if(!peds.count(actor))return false;scared_crouch.insert(actor);return true;};
    int32_t player_skin=297;std::string actual_outfit="plr3",pending_outfit;
    std::vector<std::string> committed_outfits;
    native.begin_outfit=[&](int32_t actor,const std::string& label){
        check(actor==100,"Outfit transition must preserve the player actor");
        check(label=="plr" || label=="plr3","Opening missions must request original PS2 outfit labels");
        pending_outfit=label;return true;
    };
    native.finish_outfit=[&](int32_t actor){
        check(actor==100 && !pending_outfit.empty(),"Dress must match the pending actor/outfit");
        actual_outfit=pending_outfit;pending_outfit.clear();player_skin=player_skin==297?298:297;
        committed_outfits.push_back(actual_outfit);return true;
    };
    native.current_outfit=[&](int32_t actor)->std::optional<std::string>{return actor==100?std::optional<std::string>(actual_outfit):std::nullopt;};
    size_t shop_shooters=0;
    native.set_stationary_shooter=[&](int32_t actor,bool enabled){check(peds.count(actor)!=0,"Shop stationary shooter must reference a real fixture actor");if(enabled && active_script=="indammu" && runtime.on_mission())++shop_shooters;return true;};
    native.set_pednode_seek=[&](int32_t actor,bool){check(peds.count(actor)!=0,"Seek policy references an actual fixture actor");++seek_updates;return true;};
    std::array<int32_t,3> player_position{};std::array<uint32_t,3> speech_end{};
    std::map<int32_t,std::array<int32_t,3>> entity_positions;
    std::map<int32_t,std::array<int32_t,3>> pickup_positions;
    std::map<int32_t,int32_t> occupants;
    std::map<int32_t,int32_t> follow_targets;
    std::map<int32_t,int32_t> group_members;
    std::map<int32_t,std::pair<int32_t,uint32_t>> pending_boarding;
    std::optional<std::array<int32_t,3>> destination;
    int32_t destination_blip=-1,second_car=-1,second_dealer=-1,player_transport=-1;
    int32_t destination_entity=-1;
    int32_t fourth_transport=-1;
    std::map<int32_t,std::array<int32_t,2>> vehicle_colours;std::map<int32_t,uint32_t> paint_started;size_t resprays=0;
    std::optional<std::array<int32_t,3>> shopping_target;float player_speed=0;
    int32_t player_money=0;
    size_t weapon_purchases=0;int32_t weapon_purchase_cost=0;
    bool weapon_purchase_balance_checked=false;float native_weapon_budget=0;
    size_t weapon_budget_updates=0;
    uint32_t shop_buy_requested_at=UINT32_MAX;bool shop_purchase_denied=false;int32_t denied_cash=-1;
    native.scripted_pad_state=[&](int32_t selector)->std::optional<int32_t>{
        if(active_script=="indammu" && runtime.mission_index()==50){static std::set<int32_t> queried;if(queried.insert(selector).second)std::cout<<"shop input selector="<<selector<<'\n';
            const bool bought=inventories.count({100,3})!=0;
            const bool finished=bought && (empty_shop?shop_purchase_denied:(!paid_shop || weapon_purchase_cost>0));
            if(shopping_target && now%1000<16 && ((!finished && selector==42) || (finished && selector==43))){
                if(selector==42)shop_buy_requested_at=now;return 1;
            }
        }
        return 0;
    };
    native.can_see_dead_char=[&](int32_t actor,int32_t type)->std::optional<bool>{
        const auto from=actor==100?player_position:entity_positions.at(actor);
        for(auto dead:dead_peds)if(ped_types[dead]==type){const auto to=entity_positions.at(dead);
            const auto dx=real(to[0])-real(from[0]),dy=real(to[1])-real(from[1]);if(dx*dx+dy*dy<900)return true;}
        return false;
    };
    bool companion_query_fault_injected=false;size_t companion_deferrals=0,companion_recoveries=0,passenger_status_queries=0;
    std::optional<ScriptId> ambient_fault_worker;size_t ambient_fault_reports=0;
    native.char_entering_as_passenger=[&](int32_t actor)->std::optional<bool>{
        if(actor==second_dealer && !companion_query_fault_injected){companion_query_fault_injected=true;return {};}
        return pending_boarding.count(actor)!=0;
    };
    native.diagnostic_event=[&](const std::string& event){
        if(event.find("Companion boarding deferred:")==0)++companion_deferrals;
        if(event.find("Companion boarding recovered:")==0)++companion_recoveries;
    };
    bool player_control=true;size_t dealer_entries=0,dealer_exits=0;
    int32_t first_car=-1;bool first_walk=false,first_clothes=false;float first_speed=0;
    std::set<std::array<int32_t,3>> first_vehicle_markers;size_t first_vehicle_checks_outside=0;
    bool first_exit_mode=false;std::optional<std::array<int32_t,3>> first_exit_target;
    AudioArchive archive(std::filesystem::path(argv[2])/"audio");AudioAdapter audio(archive);
    audio.play=[&](size_t slot,const AudioSample& sample){speech_end[slot]=now+uint32_t(sample.pcm.size()*1000ull/(sample.frequency*sample.channels*2ull));return true;};
    audio.finished=[&](size_t slot){return now>=speech_end[slot];};audio.stop=[&](size_t slot){speech_end[slot]=0;};
    audio.listener=[&](){std::array<float,3> p{};for(size_t i=0;i<3;++i)p[i]=real(player_position[i]);return std::optional<std::array<float,3>>(p);};
    size_t spatial_updates=0;
    audio.spatial_listener=[&]()->std::optional<SpatialListener>{SpatialListener listener;listener.position=*audio.listener();return listener;};
    audio.set_mix=[&](size_t slot,AudioMix mix){check(slot<2 && mix.volume<=127 && mix.pan<=127,"Positioned dialogue uses a real speech slot and native listener");++spatial_updates;return true;};
    audio.play_effect=[](const AudioSample& sample){return sample.frequency>0 && !sample.pcm.empty();};
    native.execute=[&](const NativeCall& call){NativeResult r;r.executed=true;
        if(call.opcode==0x03B6){std::cout<<"building-swap script="<<active_script;for(auto value:call.inputs)std::cout<<' '<<value;std::cout<<'\n';}
        if(active_script=="vic2" || active_script=="vic1" || active_script=="vic3" || active_script=="vic4" || (active_script=="indammu" && runtime.mission_index()==50)){
            static std::set<std::string> packets;
            std::ostringstream packet;packet<<"script="<<active_script<<" native="<<std::hex<<call.opcode<<std::dec;
            if(call.text)packet<<" text="<<*call.text;
            for(auto value:call.inputs)packet<<" "<<value;
            if(packets.insert(packet.str()).second)std::cout<<packet.str()<<'\n';
        }
        switch(call.opcode){
        case 0x05DE:case 0x05D0:
            if(call.source_wander_direction || call.source_wait_in_car) {
                check(call.inputs.size()==1 && call.outputs==0 && !call.predicate && peds.count(call.inputs[0]),"Original source task references a live fixture actor");
                check((call.opcode==0x05DE)==bool(call.source_wander_direction),"Wander direction sideband was discarded");
                check((call.opcode==0x05D0)==call.source_wait_in_car,"Source wait was sent to the native no-op");
                follow_targets.erase(call.inputs[0]);pending_boarding.erase(call.inputs[0]);
                // An in-car wait retains its actual seat. On-foot waiting is
                // stationary; neither case creates a vehicle or teleports.
            }
            break;
        case 0x02E4:movie_order.push_back(*call.text);duration=call.text=="L0015"?56667:74133;loaded_at=now;break;
        case 0x06B9:r.condition=loaded_at!=UINT32_MAX && now-loaded_at>=100;break;
        case 0x02E7:started_at=now;++movies_started;break;
        case 0x02E8:r.values[0]=started_at==UINT32_MAX?0:int32_t((now-started_at)/1000*1000);break;
        case 0x02E9:r.condition=started_at!=UINT32_MAX && now-started_at>=duration;break;
        case 0x02EA:loaded_at=started_at=UINT32_MAX;break;
        case 0x01F5:r.values[0]=100;break;
        case 0x0665:r.values[0]=call.inputs[0]==100?player_skin:124;break;
        case 0x00A1:{auto pos=std::array<int32_t,3>{{call.inputs[1],call.inputs[2],call.inputs[3]}};
            if(real(pos[2])<=-100.f){float closest=1.e20f;auto ground=player_position[2];
                for(const auto& entity:entity_positions){if(real(entity.second[2])<=-100.f)continue;const auto dx=real(entity.second[0])-real(pos[0]),dy=real(entity.second[1])-real(pos[1]);
                    if(dx*dx+dy*dy<closest){closest=dx*dx+dy*dy;ground=entity.second[2];}}
                pos[2]=ground;
            }(call.inputs[0]==100?player_position:entity_positions[call.inputs[0]])=pos;break;}
        case 0x00A0:{const auto actor=call.inputs[0];const auto pos=actor==100?player_position:occupants.count(actor)?entity_positions[occupants[actor]]:entity_positions[actor];for(int i=0;i<3;++i)r.values[i]=pos[i];break;}
        case 0x00AA:for(int i=0;i<3;++i)r.values[i]=entity_positions[call.inputs[0]][i];break;
        case 0x00AB:for(int i=0;i<3;++i)entity_positions[call.inputs[0]][i]=call.inputs[i+1];break;
        case 0x00A5:r.values[0]=next_handle++;cars.insert(r.values[0]);vehicle_models[r.values[0]]=call.inputs[0];for(int i=0;i<3;++i)entity_positions[r.values[0]][i]=call.inputs[i+1];if(active_script=="vic2")second_car=r.values[0];if(active_script=="vic1")first_car=r.values[0];break;
        case 0x009A:case 0x01C8:case 0x0129:r.values[0]=next_handle++;peds.insert(r.values[0]);if(call.opcode!=0x009A)companion_in_car=true;
            else {for(int i=0;i<3;++i)entity_positions[r.values[0]][i]=call.inputs[i+2];ped_types[r.values[0]]=call.inputs[0];if(active_script=="vic2" && call.inputs[1]==*native.model(80))second_dealer=r.values[0];if(active_script=="vic3"){third_peds.insert(r.values[0]);ped_health[r.values[0]]=100;}if(active_script=="vic4"){fourth_peds.insert(r.values[0]);ped_health[r.values[0]]=100;}}break;
        case 0x009B:peds.erase(call.inputs[0]);break;case 0x00A6:cars.erase(call.inputs[0]);break;
        case 0x056D:r.condition=peds.count(call.inputs[0])!=0;break;case 0x056E:r.condition=cars.count(call.inputs[0])!=0;break;
        case 0x0118:r.condition=peds.count(call.inputs[0])==0 || dead_peds.count(call.inputs[0])!=0;break;
        case 0x0223:ped_health[call.inputs[0]]=call.inputs[1];break;
        case 0x0256:case 0x03EE:case 0x0248:r.condition=true;break;
        case 0x00D9:case 0x03C0:r.values[0]=occupants.count(call.inputs[0])?occupants[call.inputs[0]]:-1;break;
        case 0x0441:r.values[0]=vehicle_models.at(call.inputs[0]);break;
        case 0x0186:r.values[0]=next_handle++;if(active_script=="vic4"){destination_blip=r.values[0];destination_entity=call.inputs[0];destination=entity_positions.at(destination_entity);}break;
        case 0x0187:r.values[0]=next_handle++;if(active_script=="vic4"){destination_blip=r.values[0];destination_entity=call.inputs[0];destination=entity_positions.at(destination_entity);}break;
        case 0x018A:r.values[0]=next_handle++;if(active_script=="vic2" || active_script=="vic1" || active_script=="vic3" || active_script=="vic4"){destination_blip=r.values[0];destination_entity=-1;destination=std::array<int32_t,3>{call.inputs[0],call.inputs[1],call.inputs[2]};if(active_script=="vic1")first_walk=real(call.inputs[2])>-100.0f;}break;
        case 0x0164:if(call.inputs[0]==destination_blip){destination.reset();destination_blip=-1;}break;
        case 0x0213:case 0x032B:r.values[0]=next_handle++;if(call.mission_context && (runtime.mission_index()==9 || runtime.mission_index()==50)){mission_pickups.insert(r.values[0]);pickup_positions[r.values[0]]={{call.inputs[2],call.inputs[3],call.inputs[4]}};}break;
        case 0x02A8:case 0x04CE:r.values[0]=next_handle++;if(active_script=="vic4"){destination_blip=r.values[0];destination_entity=-1;destination=std::array<int32_t,3>{{call.inputs[0],call.inputs[1],call.inputs[2]}};}break;
        case 0x03F3:{const auto colours=vehicle_colours[call.inputs[0]];r.values[0]=colours[0];r.values[1]=colours[1];break;}
        case 0x014B:case 0x02A7:case 0x0570:case 0x03BC:case 0x00AE:r.values[0]=next_handle++;break;
        case 0x01B2:{const auto slot=native.native_weapon_slot(call.inputs[1]);check(bool(slot),"Fixture native weapon slot exists");auto& item=inventories[{call.inputs[0],*slot+1}];
            if(active_script=="indammu" && call.inputs[0]==100)++weapon_purchases;
            if(item.first==call.inputs[1])item.second+=call.inputs[2];else item={call.inputs[1],call.inputs[2]};break;}
        case 0x041A:{const auto slot=native.native_weapon_slot(call.inputs[1]);const auto item=slot?inventories.find({call.inputs[0],*slot+1}):inventories.end();
            r.values[0]=item!=inventories.end() && item->second.first==call.inputs[1]?item->second.second:0;break;}
        case 0x0109:player_money+=call.inputs[1];if(active_script=="indammu" && call.inputs[1]<0)weapon_purchase_cost-=call.inputs[1];break;
        case 0x010A:r.condition=player_money>call.inputs[1];if(active_script=="indammu" && call.inputs[1]==239){
            weapon_purchase_balance_checked=true;
            if(empty_shop && !r.condition && weapon_budget_updates>0 && shop_buy_requested_at!=UINT32_MAX && now-shop_buy_requested_at<1000){
                shop_purchase_denied=true;denied_cash=player_money;
            }
        }break;
        case 0x0653:check(call.inputs==std::vector<int32_t>{13},"Shop reads the actual native float weapon expenditure");r.values[0]=bits(native_weapon_budget);break;
        case 0x062A:check(call.inputs[0]==13 && active_script=="indammu","Original shop writes the actual weapon expenditure");native_weapon_budget=real(call.inputs[1]);++weapon_budget_updates;break;
        case 0x04B8:{const auto item=inventories.find({call.inputs[0],call.inputs[1]});r.values[0]=item==inventories.end()?0:item->second.first;r.values[1]=item==inventories.end()?0:item->second.second;r.values[2]=-1;break;}
        case 0x04A5:{const auto pos=entity_positions.at(call.inputs[0]);for(size_t i=0;i<3;++i)r.values[i]=pos[i];break;}
        case 0x03DC:r.values[0]=next_handle++;if(active_script=="vic4"){destination_blip=r.values[0];destination_entity=-1;destination=pickup_positions.at(call.inputs[0]);}break;
        case 0x01BD:r.values[0]=now;break;case 0x00BF:r.values[0]=12;break;
        case 0x0172:case 0x0174:r.values[0]=bits(90);break;case 0x02CE:r.values[0]=bits(30);break;
        case 0x0226:r.values[0]=100;break;case 0x0227:r.values[0]=1000;break;
        case 0x0327:r.values[0]=-1;break;
        case 0x0470:case 0x00E2:case 0x01FA:r.values[0]=0;break;
        case 0x051E:r.values[0]=13;break;
        case 0x00BC:if(call.text=="VIC1_10")playable_at=now;if(call.text=="VIC1_7")first_clothes=first_walk=true;if(call.text=="VIC1_8"){first_clothes=false;first_walk=first_exit_mode=true;}break;
        case 0x01B4:player_control=call.inputs[1]!=0;break;
        case 0x07AF:r.values[0]=50;break;
        case 0x07FD:r.condition=call.inputs[0]==50;break;
        case 0x092B:r.values[0]=-1;{int32_t slot=0;for(const auto& member:group_members)if(member.second==call.inputs[0] && slot++==call.inputs[1])r.values[0]=member.first;}break;
        case 0x0631:group_members[call.inputs[1]]=call.inputs[0];follow_targets[call.inputs[1]]=100;if(active_script=="vic2")second_dealer=call.inputs[1];break;
        case 0x06EE:r.condition=group_members.count(call.inputs[0]) && group_members[call.inputs[0]]==call.inputs[1];break;
        case 0x06C9:group_members.erase(call.inputs[0]);follow_targets.erase(call.inputs[0]);break;
        case 0x046C:r.values[0]=occupants.count(100) && occupants[100]==call.inputs[0]?100:-1;break;
        case 0x01EA:r.values[0]=3;break;
        case 0x0431:r.condition=call.inputs[1]>=0 && call.inputs[1]<3;break;
        case 0x062E:++passenger_status_queries;r.values[0]=7;r.executed=false;break;
        case 0x0362:occupants.erase(call.inputs[0]);for(int i=0;i<3;++i)(call.inputs[0]==100?player_position:entity_positions[call.inputs[0]])[i]=call.inputs[i+1];if(call.inputs[0]==second_dealer)++dealer_exits;break;
        case 0x05D9:follow_targets[call.inputs[0]]=call.inputs[1];break;
        case 0x0687:pending_boarding.erase(call.inputs[0]);follow_targets.erase(call.inputs[0]);break;
        case 0x05BA:follow_targets.erase(call.inputs[0]);break;
        case 0x05CA:if(call.inputs[0]==second_dealer){check(!pending_boarding.count(call.inputs[0]),"Do not restart a pending native passenger animation");pending_boarding[call.inputs[0]]={call.inputs[1],now};}else {companion_in_car=true;occupants[call.inputs[0]]=call.inputs[1];}break;
        case 0x05CD:case 0x0633:occupants.erase(call.inputs[0]);if(active_script=="vic2" && call.inputs[0]==second_dealer)++dealer_exits;if(call.inputs[0]==100){in_car=false;left_car=true;}else companion_in_car=false;break;
        case 0x0214:if(pickup_positions.count(call.inputs[0]) && !occupants.count(100)){const auto pos=pickup_positions.at(call.inputs[0]);r.condition=true;for(int i=0;i<3;++i)r.condition=r.condition && std::abs(real(player_position[i])-real(pos[i]))<1.5f;}break;
        case 0x00DB:case 0x00DD:case 0x00DF:
            {
                const auto found=occupants.find(call.inputs[0]);
                r.condition=found!=occupants.end() && (call.opcode==0x00DF || (call.opcode==0x00DB?found->second==call.inputs[1]:vehicle_models[found->second]==call.inputs[1]));break;
            }
        case 0x00EC:case 0x00ED:case 0x00EE:case 0x00EF:case 0x00F0:case 0x00F1:
        case 0x00FE:case 0x00FF:case 0x0100:case 0x0101:case 0x0102:case 0x0103:
            // Contacts launched with LAUNCH_MISSION also have mission_context.
            // They must wait for a separate player visit after VIC1 has ended.
            r.condition=active_script=="vicgov" ?
                first_completed_at!=UINT32_MAX && now-first_completed_at>=5000 :
                call.mission_context && runtime.mission_index()==9 && playable_at!=UINT32_MAX && now-playable_at>=15000;
            if(active_script=="vic2" || active_script=="vic1" || active_script=="vic3" || active_script=="vic4"){
                r.condition=true;const int dimensions=call.opcode>=0x00FE?3:2;
                for(int i=0;i<dimensions;++i)r.condition=r.condition && std::abs(real(player_position[i])-real(call.inputs[i+1]))<=real(call.inputs[i+1+dimensions]);
                if(call.opcode==0x00ED || call.opcode==0x00FF || call.opcode==0x00F0 || call.opcode==0x0102)r.condition=r.condition && !occupants.count(100);
                if(call.opcode==0x00EE || call.opcode==0x0100 || call.opcode==0x00F1 || call.opcode==0x0103)r.condition=r.condition && occupants.count(100);
                if(call.opcode==0x00EF || call.opcode==0x00F0 || call.opcode==0x00F1 || call.opcode==0x0101 || call.opcode==0x0102 || call.opcode==0x0103)r.condition=r.condition && player_speed<0.01f;
                if(active_script=="vic1" && call.opcode==0x0100){if(r.condition)first_vehicle_markers.insert({{call.inputs[1],call.inputs[2],call.inputs[3]}});else ++first_vehicle_checks_outside;}
                if(active_script=="vic1" && first_exit_mode && call.opcode==0x00FF){first_exit_target=std::array<int32_t,3>{{call.inputs[1],call.inputs[2],call.inputs[3]}};if(r.condition)first_walk=first_exit_mode=false;}
            }
            if(active_script=="indammu"){
                r.condition=true;const int dimensions=call.opcode>=0x00FE?3:2;
                for(int i=0;i<dimensions;++i)r.condition=r.condition && std::abs(real(player_position[i])-real(call.inputs[i+1]))<=real(call.inputs[i+1+dimensions]);
                if(call.opcode==0x0102){r.condition=r.condition && !occupants.count(100) && player_speed<0.01f;
                    if(runtime.mission_index()==50 && !inventories.count({100,3}))shopping_target=std::array<int32_t,3>{{call.inputs[1],call.inputs[2],call.inputs[3]}};}
            }break;
        case 0x00F2:case 0x00F3:case 0x00F4:
            if(active_script=="vic2"){
                r.condition=true;const auto pos=entity_positions[call.inputs[1]];
                for(int i=0;i<2;++i)r.condition=r.condition && std::abs(real(player_position[i])-real(pos[i]))<=real(call.inputs[i+2]);
                if(call.opcode==0x00F3)r.condition=r.condition && !occupants.count(100);
                if(call.opcode==0x00F4)r.condition=r.condition && occupants.count(100);
            }break;
        case 0x01AD:case 0x01AE:case 0x01AF:{const int dimensions=call.opcode==0x01AD?2:3;const auto pos=entity_positions.at(call.inputs[0]);r.condition=true;for(int i=0;i<dimensions;++i)r.condition=r.condition && std::abs(real(pos[i])-real(call.inputs[i+1]))<=real(call.inputs[i+1+dimensions]);if(r.condition && call.opcode==0x01AF)r.condition=first_speed<0.01f;break;}
        case 0x00B0:case 0x00B1:{const int dimensions=call.opcode==0x00B0?2:3;const auto pos=entity_positions.at(call.inputs[0]);r.condition=true;for(int i=0;i<dimensions;++i)r.condition=r.condition && real(pos[i])>=std::min(real(call.inputs[i+1]),real(call.inputs[i+1+dimensions])) && real(pos[i])<=std::max(real(call.inputs[i+1]),real(call.inputs[i+1+dimensions]));break;}
        case 0x00A4:{const auto pos=call.inputs[0]==100?player_position:entity_positions.at(call.inputs[0]);r.condition=true;
            for(int i=0;i<3;++i)r.condition=r.condition && real(pos[i])>=std::min(real(call.inputs[i+1]),real(call.inputs[i+4])) && real(pos[i])<=std::max(real(call.inputs[i+1]),real(call.inputs[i+4]));
            if(active_script=="indammu" && runtime.mission_index()==50 && !inventories.count({100,3}) && !shopping_target){
                bool close=true;for(int i=0;i<2;++i)close=close && real(pos[i])>=std::min(real(call.inputs[i+1]),real(call.inputs[i+4]))-3.f && real(pos[i])<=std::max(real(call.inputs[i+1]),real(call.inputs[i+4]))+3.f;
                if(close){std::array<int32_t,3> inside;for(int i=0;i<3;++i)inside[i]=bits((real(call.inputs[i+1])+real(call.inputs[i+4]))*.5f);shopping_target=inside;}
            }break;}
        case 0x01C1:r.condition=first_speed<0.01f;break;
        case 0x02E3:r.values[0]=bits(first_speed);break;
        case 0x03B0:case 0x03B1:r.condition=call.mission_context;break;
        case 0x0318:check(call.text=="VIC1" || (call.text=="VIC2" && passed==1) || (call.text=="VIC3" && passed==2) || (call.text=="VIC4" && passed==3),"Original mission passed sequence");++passed;break;
        case 0x00D8:++cleanup;break;
        default:break; // commands with no result only record native side effects
        }
        return r;
    };
    native.execute=guard_native_executor(native.execute);
    std::set<std::pair<uint32_t,uint16_t>> seen;
    runtime.engine=[&](Vm& vm,Thread& t,uint16_t command){
        active_script=t.name;
        if(t.name=="vic2")next_mission_seen=true;
        if(t.name=="vic3")third_mission_seen=true;
        if(t.name=="vic4")fourth_mission_seen=true;
        if(seen.emplace(t.last_ip,command).second)std::cout<<"ip="<<t.last_ip<<" script="<<t.name<<" "<<opcode_name(command)<<'\n';
        if(const auto result=try_engine_adapter(audio,vm,t,command))return *result;
        if(const auto result=try_engine_adapter(hud,vm,t,command))return *result;
        if(const auto result=try_engine_adapter(hud,vm,t,command))return *result;
        return native.dispatch(vm,t,command);
    };
    const auto root=runtime.start_main();std::optional<size_t> last_mission;bool reported_lease=false;
    for(now=0;now<(expected_missions==4?3600000u:1200000u);now+=16){
        if(runtime.mission_index()==9 && playable_at!=UINT32_MAX && player_control && first_car>=0) {
            std::array<int32_t,3> target=player_position;
            if(first_clothes && !pickup_positions.empty()){target=pickup_positions.rbegin()->second;occupants.erase(100);}
            else if(first_exit_mode){if(first_exit_target)target=*first_exit_target;occupants.erase(100);}
            else if(first_walk && destination){target=*destination;occupants.erase(100);}
            else if(now-playable_at>=4000){occupants[100]=first_car;if(destination)target=*destination;else{target=player_position;target[1]=bits(real(target[1])-0.30f);}}
            const auto dx=real(target[0])-real(player_position[0]),dy=real(target[1])-real(player_position[1]),distance=std::sqrt(dx*dx+dy*dy);
            const auto step=std::min(distance,occupants.count(100)?0.30f:0.04f);first_speed=step/0.016f;
            if(distance>0){player_position[0]=bits(real(player_position[0])+dx/distance*step);player_position[1]=bits(real(player_position[1])+dy/distance*step);}
            player_position[2]=real(target[2])<=-100.0f?bits(15.0f):target[2];
            if(occupants.count(100)){entity_positions[first_car]=player_position;for(const auto& entry:occupants)if(entry.second==first_car)entity_positions[entry.first]=player_position;}
        }
        // A separate simulated player visit while VIC1 is active reproduces
        // the shop worker that stopped the user's campaign at 106974. No
        // opcode, mission flag or shop global is rewritten to trigger it.
        if(first_completed_at==UINT32_MAX && playable_at!=UINT32_MAX && now-playable_at>=6000 && now-playable_at<12000)
            player_position={{bits(1065.47f),bits(-397.85f),bits(13.93f)}};
        // Explicit simulated player inputs for Slacker: take the available
        // Primo, visit the pickup marker and drive to the dropoff marker.
        // Conditions below use actual fixture positions and occupants; no
        // mission global or success flag is changed by the fixture.
        if(first_completed_at!=UINT32_MAX && runtime.mission_index()==48 && player_control && destination && second_car>=0){
            if(player_transport<0)for(const auto car:cars)if(vehicle_models[car]==547){player_transport=car;break;}
            check(player_transport>=0,"Player chooses an available native Primo instead of stealing the dealer's motorcycle");
            occupants[100]=player_transport;
            auto dx=real((*destination)[0])-real(player_position[0]),dy=real((*destination)[1])-real(player_position[1]);
            bool ready=true;for(const auto& member:group_members)if(!occupants.count(member.first))ready=false;
            const auto distance=std::sqrt(dx*dx+dy*dy),step=ready?std::min(distance,0.30f):0.0f;
            if(distance>0){player_position[0]=bits(real(player_position[0])+dx/distance*step);player_position[1]=bits(real(player_position[1])+dy/distance*step);}
            // A -100 blip Z requests ground height. The pickup is on the
            // dealer's original 48.6382m surface; Chinatown is at 14.97m.
            // Keep the two actual fixture occupants on the same pickup ground.
            player_position[2]=real((*destination)[2])<=-100.0f?
                (!occupants.count(second_dealer)?entity_positions[second_dealer][2]:bits(15.0f)):(*destination)[2];entity_positions[player_transport]=player_position;
            for(const auto& entry:occupants)if(entry.second==player_transport)entity_positions[entry.first]=player_position;
        }
        if(runtime.mission_index()==49 && player_control){
            occupants.erase(100);std::optional<std::array<int32_t,3>> target=destination;int32_t enemy=-1;float best=1.0e20f;
            if(!destination)for(auto actor:third_peds)if(peds.count(actor) && !dead_peds.count(actor)){
                const auto pos=entity_positions[actor];const auto dx=real(pos[0])-real(player_position[0]),dy=real(pos[1])-real(player_position[1]),distance=dx*dx+dy*dy;
                if(distance<best){best=distance;enemy=actor;target=pos;}
            }
            if(target){const auto dx=real((*target)[0])-real(player_position[0]),dy=real((*target)[1])-real(player_position[1]),distance=std::sqrt(dx*dx+dy*dy),step=std::min(distance,0.04f);
                if(distance>0){player_position[0]=bits(real(player_position[0])+dx/distance*step);player_position[1]=bits(real(player_position[1])+dy/distance*step);}
                player_position[2]=real((*target)[2])<=-100.0f?bits(15.0f):(*target)[2];
                // Explicit simulated attack input after reaching an actual NPC.
                // Only physical health/death changes, never SCM objectives.
                if(enemy>=0 && distance<1.7f && now-last_punch>=1000){last_punch=now;++player_punches;ped_health[enemy]-=25;if(ped_health[enemy]<=0)dead_peds.insert(enemy);}
            }
        }
        if(runtime.mission_index()==50 && player_control && expected_missions==4){
            if(inventories.count({100,3}) && (empty_shop?shop_purchase_denied:(!paid_shop || weapon_purchase_cost>0)))shopping_target.reset();
            if(fourth_transport>=0 && cars.count(fourth_transport))occupants[100]=fourth_transport;else occupants.erase(100);
            auto target=shopping_target?shopping_target:destination;int32_t enemy=-1;float best=1.e20f;
            if(!target)for(auto actor:fourth_peds)if(peds.count(actor) && !dead_peds.count(actor)){
                const auto pos=entity_positions.at(actor);const auto dx=real(pos[0])-real(player_position[0]),dy=real(pos[1])-real(player_position[1]);
                if(dx*dx+dy*dy<best){best=dx*dx+dy*dy;enemy=actor;target=pos;}
            }
            if(destination_entity>=0 && fourth_peds.count(destination_entity) && !dead_peds.count(destination_entity))enemy=destination_entity;
            if(target){const auto dx=real((*target)[0])-real(player_position[0]),dy=real((*target)[1])-real(player_position[1]),distance=std::sqrt(dx*dx+dy*dy),step=std::min(distance,occupants.count(100)?.30f:.04f);
                if(distance>0){player_position[0]=bits(real(player_position[0])+dx/distance*step);player_position[1]=bits(real(player_position[1])+dy/distance*step);}
                player_speed=step/.016f;
                if(real((*target)[2])>-100.f)player_position[2]=(*target)[2];
                const auto gun=inventories.find({100,3});
                if(enemy>=0 && gun!=inventories.end() && gun->second.second>0 && distance<18.f && now-last_punch>=500){last_punch=now;++player_shots;--gun->second.second;ped_health[enemy]-=25;if(ped_health[enemy]<=0)dead_peds.insert(enemy);}
                if(destination_entity>=0 && cars.count(destination_entity) && distance<2.f){fourth_transport=destination_entity;occupants[100]=destination_entity;}
                if(fourth_transport>=0 && occupants.count(100))entity_positions[fourth_transport]=player_position;
            }
            if(fourth_transport>=0 && occupants.count(100) && !paint_started.count(fourth_transport)){
                for(const auto& site:garage_sites)if(site.second==5){const auto& b=site.first;const auto x=real(player_position[0]),y=real(player_position[1]),z=real(player_position[2]);
                    if(x>=std::min({b[0],b[3],b[5]}) && x<=std::max({b[0],b[3],b[5]}) && y>=std::min({b[1],b[4],b[6]}) && y<=std::max({b[1],b[4],b[6]}) && z>=b[2] && z<=b[7])paint_started[fourth_transport]=now;
                }
            }
            if(fourth_transport>=0 && paint_started.count(fourth_transport) && now-paint_started.at(fourth_transport)>=7500 && vehicle_colours[fourth_transport]==std::array<int32_t,2>{{0,0}}){
                vehicle_colours[fourth_transport]={{1,2}};++resprays;
            }
        }
        for(const auto& follow:follow_targets){
            if(occupants.count(follow.first))continue;
            auto& pos=entity_positions[follow.first];const auto target=follow.second==100?player_position:entity_positions[follow.second];
            const auto dx=real(target[0])-real(pos[0]),dy=real(target[1])-real(pos[1]),distance=std::sqrt(dx*dx+dy*dy);
            if(distance>1.0f){const auto step=std::min(distance-1.0f,0.04f);pos[0]=bits(real(pos[0])+dx/distance*step);pos[1]=bits(real(pos[1])+dy/distance*step);}
        }
        for(auto entry=pending_boarding.begin();entry!=pending_boarding.end();) {
            const auto actor=entry->first,car=entry->second.first;const auto pos=entity_positions[actor],target=entity_positions[car];
            const auto dx=real(target[0])-real(pos[0]),dy=real(target[1])-real(pos[1]);
            if(dx*dx+dy*dy<9.0f && now-entry->second.second>=1500){
                check(occupants.count(100) && occupants[100]==car,"Player stays in the driver seat while dealer boards");
                occupants[actor]=car;if(actor==second_dealer)++dealer_entries;entry=pending_boarding.erase(entry);
            }else ++entry;
        }
        check(audio.tick_announcements(occupants.count(100) && native.music_mode()==1 && !native.has_active_cutscene()),"Original queued news plays independently of mission dialogue");
        if(passed>=2 && !ambient_fault_worker) {
            check(read16(runtime.vm().memory,192540)==COMMAND_SET_CHAR_FRIGHTENED_IN_JACKED_CAR,"User's exact AMBTAXI instruction remains in original MAIN.SCM");
            ambient_fault_worker=runtime.start_script(192540);
        }
        for(const auto& event:runtime.tick(now,16,10000))if(event.result==StepResult::unsupported || event.result==StepResult::failed) {
            check(ambient_fault_worker && event.id==*ambient_fault_worker && event.ip==192540 &&
                  runtime.failure(event.id) && runtime.failure(event.id)->scope==FailureScope::thread,
                  "Original campaign blocked outside the injected ambient failure at "+std::to_string(event.ip)+" "+event.diagnostic);
            ++ambient_fault_reports;
        }
        std::string ped_error;check(native.tick_scripted_peds(ped_error),ped_error);
        check(audio.tick_spatial(),"Positioned original speech mix remains available during campaign progression");
        if(runtime.mission_index()!=last_mission){last_mission=runtime.mission_index();std::cout<<"mission index="<<*last_mission<<" now="<<now<<" passed="<<passed<<" lease="<<runtime.mission_buffer_in_use()<<" onMission="<<runtime.on_mission()<<'\n';}
        if(passed && !reported_lease){reported_lease=true;std::cout<<"post-VIC1 now="<<now<<" lease="<<runtime.mission_buffer_in_use()<<" onMission="<<runtime.on_mission()<<" original2240="<<read32(runtime.vm().memory,2240)<<'\n';}
        if(passed && cleanup && !runtime.mission_buffer_in_use() && !runtime.on_mission() && first_completed_at==UINT32_MAX){first_completed_at=now;in_car=false;left_car=true;}
        // Original mission indices follow the SCM directory, not story order:
        // VIC1 is block 9 and the original vicgov contact selects VIC2 in 48.
        if(first_completed_at!=UINT32_MAX && runtime.mission_index()==48 && runtime.mission_buffer_in_use() && next_mission_seen){next_mission_loaded=true;}
        if(passed==expected_missions && !runtime.mission_buffer_in_use() && !runtime.on_mission())break;
    }
    check(movie_order.size()>=2 && movie_order[0]=="L0015" && movie_order[1]=="L0013" && movies_started>=2,"Original MAIN intro must precede the mission office scene without skip");
    if(passed!=expected_missions){std::cout<<"fixture player="<<real(player_position[0])<<','<<real(player_position[1])<<','<<real(player_position[2])<<" control="<<player_control<<" dealer="<<second_dealer<<" dealerCar="<<(occupants.count(second_dealer)?occupants[second_dealer]:-1)<<" dealerGroup="<<(group_members.count(second_dealer)?group_members[second_dealer]:-1)<<" entries="<<dealer_entries<<" exits="<<dealer_exits;
        if(second_dealer>=0){auto pos=entity_positions[second_dealer];std::cout<<" dealerPos="<<real(pos[0])<<','<<real(pos[1])<<','<<real(pos[2]);}
        if(destination)std::cout<<" target="<<real((*destination)[0])<<','<<real((*destination)[1])<<','<<real((*destination)[2]);std::cout<<'\n';}
    check(passed==expected_missions && cleanup>=expected_missions && first_completed_at!=UINT32_MAX,"Original mission sequence must pass and release its mission buffer: passed="+std::to_string(passed)+" cleanup="+std::to_string(cleanup)+" now="+std::to_string(now)+" onMission="+std::to_string(runtime.on_mission()));
    if(expected_missions==4){
        check(fourth_mission_seen && player_shots>0 && spatial_updates>0,"VIC4 starts naturally and requires physical gun combat and positioned dialogue");
        // The unmodified opening tutorial branch checks 239 but emits no
        // ADD_SCORE debit for its first pistol. Do not manufacture a payment.
        const bool purchase_count_ok=empty_shop?weapon_purchases==weapon_budget_updates+1 && weapon_budget_updates>1:weapon_purchases==(paid_shop?2:1);
        const bool cost_ok=empty_shop?weapon_purchase_cost==240*int32_t(weapon_budget_updates):weapon_purchase_cost==(paid_shop?240:0);
        check(purchase_count_ok && cost_ok && weapon_purchase_balance_checked && fourth_transport>=0 && resprays==1,
              "VIC4 buys a pistol through original Ammu-Nation input/balance check, steals an actual vehicle and physically visits an original respray site: purchases="+
              std::to_string(weapon_purchases)+" cost="+std::to_string(weapon_purchase_cost)+" car="+std::to_string(fourth_transport)+" resprays="+std::to_string(resprays));
        check(native_weapon_budget==float(weapon_purchase_cost) && (empty_shop || weapon_budget_updates==(paid_shop?1:0)),
              "Paid INDAMMU purchase records each debit once and then continues the original Snuff mission");
        if(empty_shop)check(shop_purchase_denied && denied_cash>=0 && denied_cash<240,
            "Repeated purchases exhaust the original reward money; an unaffordable purchase is denied without another debit, grant or statistic update");
    }
    check(third_mission_seen && seek_updates>0 && player_punches>0,"Original VIC3 must start naturally, apply navigation and run physical combat before success");
    check(next_mission_loaded,"Original contact must naturally select and load VIC2 after VIC1 completion");
    check(dealer_entries>=1 && dealer_exits>=1,"Slacker's dealer must physically enter and leave the chosen vehicle before original success");
    check(companion_query_fault_injected && companion_deferrals>=1 && companion_recoveries>=1 && passenger_status_queries==0,"Real original three-mission progression survives injected companion helper failure with no status-query dependency");
    check(ambient_fault_worker && ambient_fault_reports==1 && runtime.script(*ambient_fault_worker).blocked &&
          runtime.script(*ambient_fault_worker).thread.active && runtime.script(*ambient_fault_worker).thread.ip==192540,
          "Exact AMBTAXI missing instruction stays quarantined once while original VIC3 starts, completes and releases its mission buffer");
    check(runtime.script(root).thread.active && runtime.active_count()>1,"Full campaign workers remain active after VIC1; no one-mission session cutoff");
    check(garages==16 && native.claimed_phone_count()==10,"Full original world initialization ran");
    check(shop_shooters>0,"Original INDAMMU setup must execute during VIC1 without stopping the concurrent campaign");
    check(first_vehicle_markers.size()>=2 && first_vehicle_checks_outside>0,"VIC1 requires simulated physical arrival at both safehouse/final vehicle markers and waits when outside");
    check(!committed_outfits.empty() && actual_outfit=="plr" && std::find(committed_outfits.begin(),committed_outfits.end(),"plr")!=committed_outfits.end(),"Original safehouse outfit must commit visually and permit VIC1/VIC2/VIC3 progression");
    check(image.bytes==ScmImage::load(argv[1]).bytes,"Original MAIN and every mission remain unmodified");
    std::cout<<"PASS: MAIN -> full initialization -> INTRO -> HOMSWET -> VIC1 success -> VIC2 helper failure/recovery, boarding with player driving, dropoff and success -> original VIC3 navigation, physical combat and success";
    if(expected_missions==4)std::cout<<" -> VIC4 Ammu-Nation "<<(paid_shop?"paid ammunition purchase":"tutorial purchase")<<"/balance check (debit="<<weapon_purchase_cost<<" nativeWeaponBudget="<<native_weapon_budget<<"), armed combat, phone pickup/dialogue, car and respray, original success/cleanup; shots="<<player_shots<<" spatialUpdates="<<spatial_updates<<" resprays="<<resprays;
    std::cout<<"; mock player/engine only; gameExecuted=false; campaignPlayable=false\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
