#include "lcs/task_observer_host.hpp"
#include "lcs/route_native_backend.hpp"
#include "lcs/route_scripts.hpp"
#include "sa_route_service.hpp"
#include "lcs/cop_native_translation.hpp"
#include "lcs/route_vehicle_native.hpp"
#include "lcs/task_hook_win32.hpp"
#include "lcs/sa_task_memory.hpp"
// Keep the observer-backed route specialization compiled alongside the owned host.
namespace lcs {template class NativeRouteConsumerBackend<TaskObserverHost>;}
#include <windows.h>
#include "lcs/runtime.hpp"
#include "lcs/engine_dispatch.hpp"
#include "lcs/update_supervisor.hpp"
#include "lcs/sa_executor.hpp"
#include "lcs/sa_fx.hpp"
#include "lcs/phone_native_frame.hpp"
#include "lcs/phone_phase_profile.hpp"
#include "lcs/ending_tune_phase_profile.hpp"
#include "lcs/ending_tune_native_settings.hpp"
#include "lcs/text.hpp"
#include "lcs/credits.hpp"
#include "lcs/audio.hpp"
#include "lcs/first_mission.hpp"
#include "lcs/equivalents.hpp"
#include "sa_audio.hpp"
#include "sa_continuous_sound.hpp"
#include "lcs/continuous_native_settings.hpp"
#include "sa_ending_tune.hpp"
#include "sa_cutscenes.hpp"
#include "sa_hud.hpp"
#include "sa_script_coronas.hpp"
#include "sa_script_marker_render.hpp"
#include "sa_remote_door_policy.hpp"
#include "lcs/sniper_query.hpp"
#include "lcs/collectives.hpp"
#include "sa_gameplay.hpp"
#include "sa_world_preview.hpp"
#include "sa_lcs_bridge.hpp"
#include "sa_world_sectors.hpp"
#include "sa_allowed_collision.hpp"
#include "sa_taxi_hail.hpp"
#include "sa_security_camera.hpp"
#include "sa_char_follow_path.hpp"
#include "sa_attack_timer.hpp"
#include "sa_threat_char.hpp"
#include "sa_player_friend.hpp"
#include "sa_reward_vehicle.hpp"
#include "sa_vehicle_colours.hpp"
#include "sa_no_objective.hpp"
#include "sa_move_anim_override.hpp"
#include "sa_jack_protection.hpp"
#include "sa_crusher.hpp"
#include "sa_garage_door.hpp"
#include <limits>
#include "lcs/campaign_checkpoint.hpp"
#include "lcs/sa_campaign_save.hpp"
#include "campaign_services_save.hpp"
#include "sa_crane.hpp"
#include "sa_ferry.hpp"
#include "sa_threat_occlusion.hpp"
#include "sa_attack_policy.hpp"
#include "sa_city_selection.hpp"
#include "sa_player_skins.hpp"
#include "sa_gameplay_skins.hpp"
#include "lcs/pause_menu.hpp"
#include <array>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <memory>
#include <set>

namespace {
HMODULE module = nullptr;
using ScriptProcess = void (__cdecl*)();
ScriptProcess original_process = nullptr, original_startup_process = nullptr, original_phone_phase = nullptr;
ScriptProcess original_audio_main=nullptr,original_audio_menu=nullptr;
ScriptProcess original_mission_hud=nullptr;
ScriptProcess original_credits_hud=nullptr;
ScriptProcess original_marker_phase=nullptr,original_marker_shutdown=nullptr;
ScriptProcess original_world_shutdown=nullptr,original_world_restart=nullptr;
uint32_t original_audio_frame=0;
bool credits_inputs(bool* skip,bool* accelerated) {
    if(!lcs::sa_executor_profile_matches())return false;
    __try {
        const uint8_t get_pad[]={0x8B,0x44,0x24,0x04,0x69,0xC0,0x34,0x01,0,0,0x05,0x58,0x34,0xB7,0,0xC3};
        if(std::memcmp(reinterpret_cast<const void*>(0x53FB70),get_pad,sizeof(get_pad)))return false;
        const auto pad=reinterpret_cast<uintptr_t(__cdecl*)(int32_t)>(0x53FB70)(0);if(!pad)return false;
        // Retail LCS maps its two credits controls through raw button selectors
        // 12 (Start) and 16 (Cross), not the action-based GET_PAD_BUTTON_STATE.
        // SA SDK: Start +0x18, Cross +0x20, OldState +0x30.
        *skip=*reinterpret_cast<const int16_t*>(pad+0x18)!=0 && *reinterpret_cast<const int16_t*>(pad+0x48)==0;
        *accelerated=*reinterpret_cast<const int16_t*>(pad+0x20)!=0;
        return true;
    }__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
std::optional<lcs::ContinuousNativeSettings> continuous_native_settings() {
    return lcs::original_continuous_native_settings(
        [](uintptr_t address,std::initializer_list<uint8_t> bytes){return std::memcmp(reinterpret_cast<const void*>(address),bytes.begin(),bytes.size())==0;},
        [](uintptr_t address,void* out,size_t size){SIZE_T actual=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),out,size,&actual) && actual==size;});
}
std::optional<lcs::EndingTuneNativeSettings> ending_tune_settings() {
    return lcs::original_ending_tune_native_settings(
        [](uintptr_t address,std::initializer_list<uint8_t> bytes){return std::memcmp(reinterpret_cast<const void*>(address),bytes.begin(),bytes.size())==0;},
        [](uintptr_t address,void* out,size_t size){SIZE_T actual=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),out,size,&actual) && actual==size;});
}
bool first_frame = true, f9_down = false;
uint64_t frames = 0;
struct Binding { int32_t lcs_id,sa_id;std::string name,sa_name; };
std::vector<Binding> bindings;
std::vector<lcs::EquivalentEntity> equivalent_bindings;
bool inspected_world=false,world_failure=false;
bool campaign_load_requires_restore=false;
std::optional<lcs::CampaignCheckpoint> pending_campaign_load, pending_campaign_save;
std::string campaign_load_diagnostic;
std::filesystem::path pending_native_save;
std::unique_ptr<lcs::SaWorldPreview> world_preview;
// Original ADD_POINT keeps its previous location process-wide across scripts.
lcs::ScriptMarkers::ProcessState marker_process;
std::unique_ptr<lcs::SaPlayerSkins> player_skins;
std::unique_ptr<lcs::SaGameplaySkins> gameplay_skins;
uint32_t last_game_time=0;
std::filesystem::path module_directory();
std::filesystem::path resource_directory();
std::string source_suffix();
std::filesystem::path source_file(const std::filesystem::path&,const std::filesystem::path&);
bool write_game_word(uintptr_t address,uint32_t value);
bool read_game_word(uintptr_t address,uint32_t* value);
void log(const std::string& text);
void validate_first_mission_world();
std::wstring setting(const wchar_t* name);
std::vector<std::unique_ptr<lcs::SaWorldSectors>> retired_world_sectors;
std::vector<std::unique_ptr<lcs::SaTaxiHail>> retired_taxi_hails;
std::vector<std::unique_ptr<lcs::NativeCityMusic>> retired_city_music;
bool attack_timer_cleanup_pending=false;
bool player_friend_cleanup_pending=false;
bool car_reward_cleanup_pending=false;
bool move_anim_cleanup_pending=false;
bool jack_protection_cleanup_pending=false;
bool crusher_cleanup_pending=false;
bool garage_door_cleanup_pending=false;
bool crane_cleanup_pending=false;
bool ferry_cleanup_pending=false;
bool threat_occlusion_cleanup_pending=false;
// Script-task receipt observer (event->task association, record replacement and task release). Created lazily on the game
// thread, installed once from a frame callback while that thread is the only one running script/task code we control, and
// intentionally never freed: native entries may still reach its trampolines.
DWORD task_observer_thread=0;
bool task_observer_window=false,task_observer_attempted=false;
bool task_observer_on_thread(){return task_observer_thread && GetCurrentThreadId()==task_observer_thread;}
bool task_observer_excluded(){return task_observer_window && task_observer_on_thread();}
bool task_observer_profile(){return lcs::sa_executor_profile_matches();}
bool task_observer_task_live(uint32_t task){return lcs::sa_task_slot_live(task);}
lcs::TaskHookWin32Memory task_hook_memory(&task_observer_excluded);
lcs::TaskObserverHost* task_observer=nullptr;
bool own_task_original_prefix(uintptr_t address,const uint8_t* expected,size_t length) {
    return address<=UINT32_MAX && task_observer &&
        task_observer->owns_original_prefix(uint32_t(address),expected,length);
}
bool retail_task_release_entry() {
    const uint8_t retail[]={0x8B,0x0D,0xA8,0x44,0xB7,0x00};
    __try {return std::memcmp(reinterpret_cast<const void*>(0x61A5B0),retail,sizeof(retail))==0;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool allowed_collision_cleanup_pending=false;
bool initial_car_reward_hooks_attempted=false;
bool security_cleanup_pending=false;
void service_retired_world_sectors() {
    for(auto it=retired_city_music.begin();it!=retired_city_music.end();){
        if((*it)->shutdown())it=retired_city_music.erase(it);else ++it;
    }
    if(security_cleanup_pending && lcs::sa_security_camera_cleanup())security_cleanup_pending=false;
    if(attack_timer_cleanup_pending && lcs::sa_release_char_attack_timer())attack_timer_cleanup_pending=false;
    if(player_friend_cleanup_pending && lcs::sa_release_player_friends())player_friend_cleanup_pending=false;
    if(car_reward_cleanup_pending && lcs::sa_release_car_rewards())car_reward_cleanup_pending=false;
    if(move_anim_cleanup_pending && lcs::sa_release_move_anim_overrides())move_anim_cleanup_pending=false;
    if(jack_protection_cleanup_pending && lcs::sa_release_jack_protections())jack_protection_cleanup_pending=false;
    if(crusher_cleanup_pending && lcs::sa_crusher_cleanup())crusher_cleanup_pending=false;
    if(garage_door_cleanup_pending && lcs::sa_garage_door_cleanup())garage_door_cleanup_pending=false;
    if(crane_cleanup_pending && lcs::sa_crane_cleanup())crane_cleanup_pending=false;
    if(ferry_cleanup_pending && lcs::sa_ferry_cleanup())ferry_cleanup_pending=false;
    if(threat_occlusion_cleanup_pending && lcs::sa_release_threat_occlusion())threat_occlusion_cleanup_pending=false;
    if(allowed_collision_cleanup_pending && lcs::sa_release_allowed_collision())allowed_collision_cleanup_pending=false;
    for(auto it=retired_taxi_hails.begin();it!=retired_taxi_hails.end();){
        if((*it)->clear_all())it=retired_taxi_hails.erase(it);else ++it;
    }
    for(auto it=retired_world_sectors.begin();it!=retired_world_sectors.end();){
        if((*it)->cleanup().executed)it=retired_world_sectors.erase(it);else ++it;
    }
}
void prepare_initial_car_reward_hooks() {
    if(initial_car_reward_hooks_attempted || car_reward_cleanup_pending)return;
    const auto mode=setting(L"Mode");
    if(mode!=L"Campaign" && mode!=L"FirstMission")return;
    initial_car_reward_hooks_attempted=true;
    if(!lcs::sa_install_car_reward_hook())
        log("Reward storage hooks refused before native garage processing");
}
struct Campaign {
    lcs::ScmImage image;
    lcs::NativeAdapter adapter;
    lcs::Runtime runtime;
    lcs::OriginalRouteIntents route_intents;
    struct RouteActorReference {
        lcs::ObjectiveActor actor;
        uintptr_t* cell=new uintptr_t(0);
        DWORD thread=GetCurrentThreadId();
        explicit RouteActorReference(lcs::ObjectiveActor value):actor(value){}
        ~RouteActorReference() {
            // Native may still hold this address after a fault/profile change.
            // Quarantine that tiny allocation rather than leave a dangling ref.
            if(GetCurrentThreadId()==thread && lcs::sa_release_actor_reference(cell))delete cell;
        }
        bool live() const {
            return GetCurrentThreadId()==thread && lcs::sa_actor_reference_live(actor,cell);
        }
    };
    std::unordered_map<int32_t,std::unique_ptr<RouteActorReference>> route_actor_refs;
    lcs::RouteScripts route_scripts;
    std::unique_ptr<lcs::SaRouteService> route_service;
    lcs::AudioArchive audio_archive;
    lcs::AudioSample phone_ring;
    lcs::MissionSpeech speech;
    lcs::NativeEndingTune finale;
    lcs::SaCredits credits;
    std::array<lcs::TextArchive,5> credit_text;
    lcs::AudioAdapter audio;
    // 402/403: script objects (192-slot pool) over the dedicated owned-PCM continuous runtime.
    lcs::SaContinuousSound continuous_runtime;
    lcs::ContinuousSoundAdapter continuous_sounds;
    std::unique_ptr<lcs::NativeCityMusic> city_music;
    std::filesystem::path city_music_path;
    int32_t source_music_area=0;
    bool ensure_city_music(){
        if(!retired_city_music.empty())return false;
        if(city_music)return city_music->ready();
        try{
            auto loaded=std::make_unique<lcs::NativeCityMusic>();
            // Retail non-radio tracks17/18 start at encoded zero regardless
            // of their randomized unused saved position. Use this actual
            // decoder reset origin; retain the retail manager length100.
            loaded->load(city_music_path,{0,0,100},{0,0,100});
            city_music=std::move(loaded);return true;
        }catch(const std::exception& e){log(std::string("CITY resource unavailable: ")+e.what());return false;}
    }
    bool service_city_music(bool paused,float gain){
        // A failed owned transition keeps its voice receipt. Retry checked
        // shutdown on every audio service before replacing the controller.
        if(city_music && !city_music->ready()){
            if(!city_music->shutdown())return false;
            city_music.reset();
        }
        if(!city_music && !lcs::city_track_for_retail_area(source_music_area))return true;
        if(!ensure_city_music())return false;
        paused=paused || adapter.music_mode()==2 || adapter.has_active_cutscene() || finale.active();
        const lcs::AudioMix mix{uint8_t(std::clamp(gain,0.f,1.f)*127.f),63};
        if(!city_music->service_output(paused,mix))return false;
        if(paused)return true;
        city_music->advance_service_ages();
        if(lcs::observe_retail_area_music_selection(*city_music,source_music_area)==lcs::CityAreaMusicSelection::controller_refused)return false;
        bool start_flag=true;
        return city_music->service_selection(mix,start_flag);
    }
    void release_city_music(){
        if(city_music && !city_music->shutdown())retired_city_music.push_back(std::move(city_music));
        else city_music.reset();
    }
    lcs::HudBridge hud;
    lcs::ScriptCoronas coronas;
    lcs::ScriptMarkerPool marker_pool;
    lcs::ScriptMarkers markers{marker_process};
    lcs::Collectives collectives;
    bool corona_pending=false;
    std::unique_ptr<lcs::NativeCutscenes> cutscenes;
    std::unique_ptr<lcs::FirstMission> first_mission;
    std::unique_ptr<lcs::NativeGameplayAnimations> gameplay_animations;
    std::unique_ptr<lcs::SaWorldSectors> world_sectors;
    std::unique_ptr<lcs::SaTaxiHail> taxi_hails;
    std::set<std::pair<uint32_t,uint16_t>> visited;
    bool halted=false;
    bool original_save_menu_requested=false;
    bool restoring_world=false;
    lcs::CampaignServicesSave::PendingWorld pending_saved_world;
    uint32_t last_door_status_time=0;
    std::string last_door_status;
    uint32_t last_scoped_notice=0;
    std::set<lcs::ScriptId> notified_faults;
    lcs::UpdateSupervisor frame_updates;
    bool hud_write_pending=false;
    std::string failure_notice="~r~LCS INTERROMPIDO~w~~n~Erro de execucao. Objetivos inativos. Consulte LcsSaBridge.log.";
    uint32_t last_failure_notice=0;
    bool skip_down=false,skip_pressed=false;
    const lcs::TextArchive* credits_language() const {
        // CMenuManager::m_nPrefsLanguage, SDK offset 0x84, same order as GXT files.
        uint8_t language=255;SIZE_T actual=0;
        if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(0xBA67CC),&language,1,&actual) || actual!=1 || language>=credit_text.size())return nullptr;
        return &credit_text[language];
    }
    Campaign(const std::filesystem::path& directory,bool first=false,bool restoring=false):image(lcs::ScmImage::load(source_file(directory,L"DATA/MAIN.SCM"))),adapter(image),runtime(image),audio_archive(directory/L"audio",source_suffix()),audio(audio_archive),continuous_runtime(audio_archive) {
        if(setting(L"VehicleColours")==L"1")
            adapter.bind_vehicle_colours(lcs::sa_load_vehicle_colours(resource_directory()/L"data/lcs/vehicle-colours.dat"));
        continuous_sounds.available=[this](){return !halted && continuous_runtime.available();};
        continuous_sounds.create=[this](const lcs::ContinuousSoundObjects::Snapshot& source){return continuous_runtime.create(source);};
        continuous_sounds.release=[this](const lcs::ContinuousSoundObjects::Snapshot& source){return continuous_runtime.release(source);};
        city_music_path=source_file(directory,L"AUDIO/MUSIC/CITY.VB");
        continuous_sounds.music_available=[this](){return ensure_city_music();};
        continuous_runtime.music_available=[this](){return ensure_city_music();};
        continuous_runtime.configure_music=[this](uint8_t selector){
            if(!city_music)return false;
            const auto configured=city_music->configure(selector,audio.continuous_music_random_word());
            return configured && *configured!=lcs::ContinuousMusicConfiguration::unsupported;
        };
        adapter.source_music_area_changed=[this](int32_t area){source_music_area=area;};
        adapter.get_char_threat_char=lcs::sa_get_char_threat_char;
        adapter.char_has_no_objective=lcs::sa_char_has_no_objective;
        adapter.set_char_jack_protection=[](int32_t actor,bool active){
            if(jack_protection_cleanup_pending)return lcs::NativeResult{};
            return lcs::sa_set_char_jack_protection(actor,active);
        };
        adapter.override_char_move_anim=[](int32_t ped){
            if(move_anim_cleanup_pending)return lcs::NativeResult{};
            return lcs::sa_override_char_move_anim(ped);
        };
        adapter.clear_char_move_anim_override=[](int32_t ped){
            if(move_anim_cleanup_pending)return lcs::NativeResult{};
            return lcs::sa_clear_char_move_anim_override(ped);
        };
        // Install restore hooks before saved garage cars can be recreated,
        // including a run in which no SET_CAR_IS_REWARD has occurred yet.
        if(car_reward_cleanup_pending || !lcs::sa_install_car_reward_hook())
            log("Reward vehicle storage unavailable; source setter remains gated");
        adapter.set_car_reward=[](int32_t car,bool enabled){
            if(car_reward_cleanup_pending)return lcs::NativeResult{};
            return lcs::sa_set_car_reward(car,enabled);
        };
        adapter.set_char_player_friend=[](int32_t ped,bool enabled){
            if(player_friend_cleanup_pending)return lcs::NativeResult{};
            const auto actor=lcs::sa_live_objective_actor(ped);
            return actor?lcs::sa_set_char_player_friend(*actor,enabled):lcs::NativeResult{};
        };
        adapter.player_friend_actor_created=[](int32_t handle){
            if(const auto actor=lcs::sa_live_objective_actor(handle))lcs::sa_erase_player_friend(*actor);
        };
        adapter.set_char_shoot_timer=[](int32_t ped,uint32_t milliseconds){
            if(attack_timer_cleanup_pending)return false;
            const auto actor=lcs::sa_live_objective_actor(ped);
            return actor && lcs::sa_set_char_shoot_timer(*actor,milliseconds);
        };
        adapter.set_char_attack_timer=[](int32_t ped,uint32_t milliseconds){
            if(attack_timer_cleanup_pending)return false;
            const auto actor=lcs::sa_live_objective_actor(ped);
            return actor && lcs::sa_set_char_attack_timer(*actor,milliseconds);
        };
        // All five supported language files must contain the LCS banks, so menu
        // language changes cannot leave the script looking up unrelated SA text.
        size_t language_index=0;
        for(const wchar_t* language:{L"american",L"french",L"german",L"italian",L"spanish"}) {
            const auto original_text=lcs::TextArchive::load(source_file(directory,std::filesystem::path(L"TEXT")/(std::wstring(language)+L".gxt")),true);
            const auto path=resource_directory()/L"text"/(std::wstring(language)+L".gxt");
            auto text=lcs::TextArchive::load(path,false);text.require_campaign(original_text);
            credit_text[language_index++]=std::move(text);
        }
        const auto primary_path=std::filesystem::path(setting(L"WorldDirectory"))/L"campaign-model-bindings.tsv";
        if(std::filesystem::exists(primary_path)) {
            std::set<int32_t> physical_carriers;
            std::ifstream carrier_file(std::filesystem::path(setting(L"WorldDirectory"))/L"campaign-anchor-bindings.tsv");
            int32_t carrier_original=0,carrier_target=0;std::string carrier_name,carrier_native;
            while(carrier_file>>carrier_original>>carrier_target>>carrier_name>>carrier_native)physical_carriers.insert(carrier_original);
            std::ifstream primary(primary_path);int32_t original=0,target=0;std::string name,runtime_name;size_t count=0;
            while(primary>>original>>target>>name>>runtime_name) {
                const auto registered=std::find_if(bindings.begin(),bindings.end(),[&](const Binding& b){return b.lcs_id==original && b.sa_id==target && b.name==name && b.sa_name==runtime_name;});
                if(registered==bindings.end())throw std::runtime_error("Primary campaign world model is not mounted");
                adapter.bind_model(original,target,name,physical_carriers.count(original)?lcs::ModelKind::world_collision:lcs::ModelKind::object);++count;
            }
            if(!primary.eof() || !count)throw std::runtime_error("Invalid primary campaign world model index");
            // Split artist atomics still render as their existing IPL objects.
            // Their synthetic aliases must not overwrite the original SCM ID.
            for(const auto& b:bindings)adapter.bind_named_model(b.sa_id,b.name,physical_carriers.count(b.lcs_id)?lcs::ModelKind::world_collision:lcs::ModelKind::object);
        } else for(const auto& b:bindings)adapter.bind_model(b.lcs_id,b.sa_id,b.name);
        if(!equivalent_bindings.empty())lcs::bind_equivalents(adapter,equivalent_bindings);
        if(setting(L"GameplaySkins")==L"1") {
            std::ifstream peds(std::filesystem::path(setting(L"WorldDirectory"))/L"peds"/L"canonical-model-bindings.tsv");
            std::string source_id,name,native_name,asset;int32_t native_id=0;size_t count=0;
            if(!peds)throw std::runtime_error("Canonical mission character bindings missing");
            while(peds>>source_id>>native_id>>name>>native_name>>asset) {
                if(native_id<=0 || native_id>=290 || lcs::sa_model_class(native_id)!=7)throw std::runtime_error("Invalid mission character native slot");
                adapter.bind_named_model(native_id,name,lcs::ModelKind::ped);++count;
            }
            if(!peds.eof() || count!=138)throw std::runtime_error("Incomplete canonical mission character bindings");
            for(int32_t slot=12;slot<=21;++slot)adapter.bind_model(108+slot,124,"special"+std::to_string(slot),lcs::ModelKind::ped);
            adapter.named_special_characters=true;
            log("Original gameplay mission characters bound by name; LOAD_SPECIAL_CHARACTER preserves logical slot identity");
        }
        lcs::SaLcsBridgeModelIds bridge_ids;
        for(const auto& b:bindings){
            if(b.lcs_id==302 && b.name=="bridge_liftsec")bridge_ids.lift=uint16_t(b.sa_id);
            if(b.lcs_id==301 && b.name=="bridge_liftweight")bridge_ids.weight=uint16_t(b.sa_id);
            if(b.lcs_id==305 && b.name=="subbridge_lift")bridge_ids.road=uint16_t(b.sa_id);
        }
        if(!lcs::sa_lcs_bridge_configure(bridge_ids))throw std::runtime_error("Converted LCS bridge bindings unavailable");
        adapter.set_bridge_state=lcs::sa_lcs_bridge_set_state;
        adapter.char_follow_path=[this](int32_t ped,const std::array<float,3>& at,float radius,int32_t mode,bool mission){
            return lcs::sa_char_follow_path(ped,at,radius,mode,mission,adapter.execute);
        };
        adapter.switch_security_camera=[](bool enabled){
            if(security_cleanup_pending){lcs::NativeResult refused;refused.diagnostic="Previous CCTV owner awaits native cleanup";return refused;}
            return lcs::sa_security_camera_set(enabled);
        };
        std::vector<lcs::SaWorldSectorModelBinding> sector_bindings;
        for(const auto& b:bindings)if(b.sa_id>=10000 && b.sa_id<20000)sector_bindings.push_back({b.name,uint16_t(b.sa_id)});
        auto sector_engine=lcs::make_sa_world_sector_engine(std::filesystem::path(setting(L"WorldDirectory")));
        world_sectors=std::make_unique<lcs::SaWorldSectors>(std::filesystem::path(setting(L"WorldDirectory")),sector_bindings,sector_engine);
        adapter.world_swap_state=[this](int32_t group,int32_t state){return world_sectors->set(group,state);};
        adapter.request_world_swaps=[this](){return world_sectors->request();};
        adapter.world_streaming_complete=[this](){return world_sectors->complete();};
        adapter.commit_world_sector=[this](){return world_sectors->commit();};
        // Preserve native SA ambient voices, as for the other adapted NPC objectives.
        // The LCS generic TAXI_WAIT resolver permits NO_SAMPLE. Do not invent a voiced clip
        // for a native SA ped lacking an authored LCS voice-bank identity.
        taxi_hails=std::make_unique<lcs::SaTaxiHail>([](const lcs::ObjectiveActor&){return lcs::TaxiWaitSfx{lcs::TaxiWaitSfx::Kind::no_sample,-1};});
        adapter.hail_taxi=[this](int32_t ped,bool mission){return taxi_hails->start(ped,mission,adapter.execute);};
        adapter.world_ready=true;adapter.text_ready=true;adapter.reuse_native_player=true;
        adapter.flat_native_world=true;
        std::ifstream original_zones(source_file(directory,L"DATA/GTA3.ZON"));
        if(!original_zones)throw std::runtime_error("Original campaign zones missing");
        adapter.load_original_zones(original_zones);
        adapter.recovery_taxi_deferred=true;
        if(!first)log("Story campaign priority: optional post-death taxi deferred; original pickup/dropoff declarations retained");
        log("Companion transport v24: actual occupancy/task tree; no 062E status dependency; advisory faults defer per actor and preserve original campaign");
        log("Campaign fault supervisor v25: per-thread quarantine; mission/initialization dependencies retained; no script-name allowlist and no global abort on script failures");
        log("Ammu-Nation compatibility v30: signed weapon expenditure updates the live native float statistic; paid/denied shop branches validated offline");
        log("Shared commands v31: live signed analogue axes, debug-camera source, next-vehicle extra selectors and verified live targettable objects");
        if(!first) {
            const auto phone=std::find_if(bindings.begin(),bindings.end(),[](const Binding& b){return b.name=="phonebooth1";});
            if(phone==bindings.end())throw std::runtime_error("Mounted LCS phone booth binding missing");
            phone_ring=audio_archive.phone_ring_sample();
            adapter.phone_phase_available=[](){return original_phone_phase!=nullptr;};
            adapter.update_phone_frame=[this,model=phone->sa_id](const std::array<float,3>& position,lcs::PhoneFrameCoordinator& state) {
                return lcs::sa_update_phone_frame(model,position,state,phone_ring,[this](const lcs::AudioSample& sample,lcs::AudioMix mix){
                    const auto receipt=speech.play_owned_effect(sample,mix);
                    return lcs::PhoneEffectReceipt{receipt.entered,receipt.accepted};
                });
            };
            const auto crusher_body=std::find_if(bindings.begin(),bindings.end(),[](const Binding& b){return b.name=="crushercrushB";});
            const auto crusher_lid=std::find_if(bindings.begin(),bindings.end(),[](const Binding& b){return b.name=="crushertop";});
            if(crusher_body!=bindings.end() && crusher_lid!=bindings.end())
                lcs::sa_crusher_configure(uint16_t(crusher_body->sa_id),uint16_t(crusher_lid->sa_id));
            // ObjectData makes the IPL door a CDummyObject; SA's population
            // conversion produces the CObject that registers itself with the
            // native garage controller. Collector callbacks use the same exact
            // placements, never an arbitrary nearby building of the same model.
            std::ifstream door_file(std::filesystem::path(setting(L"WorldDirectory"))/L"door-bindings.tsv");
            if(!door_file)throw std::runtime_error("Mounted garage door bindings missing");
            size_t door_count=0;std::string door_line;
            while(std::getline(door_file,door_line)) {
                std::istringstream row(door_line);int32_t source=0,native=0;size_t instance=0;
                std::string original,alias,ipl,pool,extra;std::array<float,3> at{};
                if(!(row>>source>>native>>original>>alias>>ipl>>instance>>at[0]>>at[1]>>at[2]>>pool) ||
                   row>>extra || pool!="object" || (ipl!="world0.ipl" && ipl!="world1.ipl" && ipl!="world2.ipl"))
                    throw std::runtime_error("Invalid mounted garage door binding");
                const auto model=std::find_if(bindings.begin(),bindings.end(),[&](const Binding& b){
                    return b.lcs_id==source && b.sa_id==native && b.name==original && b.sa_name==alias;
                });
                if(model==bindings.end() || !lcs::sa_garage_door_configure(uint16_t(native),at,lcs::SaGarageDoorEntityPool::object))
                    throw std::runtime_error("Mounted garage door identity/configuration mismatch");
                ++door_count;
            }
            if(!door_count)throw std::runtime_error("Mounted garage door inventory is empty");
            log("Original garage door placements registered="+std::to_string(door_count)+"; native SA dynamic-object/garage consumer");
            for(const auto* name:{L"world0.ipl",L"world1.ipl",L"world2.ipl"}) {
                std::ifstream placements(std::filesystem::path(setting(L"WorldDirectory"))/name);
                if(!placements)throw std::runtime_error("Mounted LCS phone placements missing");
                adapter.load_phone_placements(placements,phone->sa_id);
                const auto crusher=std::find_if(bindings.begin(),bindings.end(),[](const Binding& b){return b.name=="crusher_crane";});
                if(crusher==bindings.end())throw std::runtime_error("Mounted original crusher crane binding missing");
                placements.clear();placements.seekg(0);adapter.load_crane_placements(placements,crusher->sa_id);
            }
            {
                const auto crusher_crane=std::find_if(bindings.begin(),bindings.end(),[](const Binding& b){return b.name=="crusher_crane";});
                const auto origin=adapter.crane_origin(0);
                if(crusher_crane==bindings.end() || !origin || adapter.crane_origin(1) || !lcs::sa_crane_configure(uint16_t(crusher_crane->sa_id),*origin))
                    throw std::runtime_error("Mounted crusher crane placement is missing or ambiguous");
            }
            {
                // Ferry track and physical hull. The track is optional package data; the hull is the packaged
                // CBoat model named by metadata/ferry-boat.json. Either one missing leaves the ferry commands
                // refused (never answered from a virtual ferry).
                std::ifstream track(source_file(directory,L"DATA/PATHS/FERRY1.DAT"));
                if(track && !adapter.ferry_service.load_path(0,track))throw std::runtime_error("Original ferry track is unreadable");
                if(!track)log("Original ferry track missing; ferry commands stay unsupported");
                int32_t ferry_model=-1;
                std::ifstream manifest(std::filesystem::path(setting(L"ResourceDirectory"))/L"metadata"/L"ferry-boat.json");
                std::string text((std::istreambuf_iterator<char>(manifest)),std::istreambuf_iterator<char>());
                const auto key=text.find("\"nativeModel\"");
                if(key!=std::string::npos) {
                    char* end=nullptr;const auto colon=text.find(':',key);
                    const long value=colon==std::string::npos?-1:std::strtol(text.c_str()+colon+1,&end,10);
                    if(end && end!=text.c_str()+colon+1 && value>0 && value<20000)ferry_model=int32_t(value);
                }
                // Docked collision (retail ms_colModelFerryDocked) travels next to the hull collision; absent or malformed
                // data leaves only the collision switch refused.
                std::vector<uint8_t> docked_col;
                {
                    std::ifstream docked_file(std::filesystem::path(setting(L"ResourceDirectory"))/L"models"/L"lcs"/L"ferry-docked.col",std::ios::binary);
                    docked_col.assign((std::istreambuf_iterator<char>(docked_file)),std::istreambuf_iterator<char>());
                }
                if(track && ferry_model>0 && !lcs::sa_ferry_configure(ferry_model,lcs::execute_sa,std::move(docked_col)))
                    throw std::runtime_error("Ferry hull configuration refused");
                if(ferry_model<=0)log("Ferry hull manifest missing; physical ferry unavailable");
                auto& ferry=adapter.ferry_service;
                ferry.world.physical_ready=[]()->std::optional<bool>{return ferry_cleanup_pending?std::optional<bool>(false):lcs::sa_ferry_ready();};
                ferry.world.producer_available=[]()->std::optional<bool>{return !ferry_cleanup_pending && lcs::sa_ferry_configured();};
                ferry.world.apply_pose=[](int32_t id,const lcs::FerryPose& pose,const std::array<float,3>& speed){return lcs::sa_ferry_pose(id,pose,speed);};
                ferry.world.apply_door=[](int32_t id,bool front,float ratio){return lcs::sa_ferry_set_door(id,front,ratio);};
                ferry.world.set_collision=[](int32_t id,int32_t type){return lcs::sa_ferry_set_collision(id,type);};
                ferry.world.offsets=[]{return lcs::sa_ferry_offsets();};
                ferry.world.surface_z=lcs::sa_ferry_surface_z;
                // The door end the retail OpenDoor picks depends on which end the player is nearer to.
                ferry.world.player_xy=[]()->std::optional<std::array<float,2>>{
                    const auto player=lcs::sa_focused_player_position();
                    if(!player)return std::nullopt;
                    return std::array<float,2>{(*player)[0],(*player)[1]};
                };
            }
        }
        adapter.execute=lcs::guard_native_executor(lcs::execute_sa);
        adapter.start_credits=[this](int32_t group){
            lcs::NativeResult result;uint32_t now=0;const auto* text=credits_language();
            if(!original_credits_hud || !lcs::sa_hud_render_ready() || !credits.available() || !text || !read_game_word(0xB7CB84,&now)) {
                result.diagnostic="Original credits text/render phase unavailable";return result;
            }
            lcs::SaCredits pending;
            if(!pending.start(group,now,*text)) {result.diagnostic="Original credits bank incomplete or unavailable";return result;}
            // Start also loads the source mission text bank. Reuse the real
            // LOAD_MISSION_TEXT backend so subsequent script labels see it.
            lcs::NativeCall load;load.opcode=0x054C;load.text=pending.bank();
            result=lcs::execute_sa(load);if(!result.executed)return result;
            result.native_entered=true;
            credits=std::move(pending);
            return result;
        };
        adapter.stop_credits=[this](){lcs::NativeResult result;credits.stop();result.executed=true;return result;};
        adapter.original_credits_finished=[this](){return credits.finished();};
        if(!first) {
            finale.load(source_file(directory,L"AUDIO/MUSIC/FINALE.VB"));
            adapter.change_music_mode=[this](int32_t mode){
                lcs::NativeResult result;
                if(!original_audio_main || !original_audio_menu || !finale.ready()) {result.diagnostic="Finale audio phase/resource unavailable";return result;}
                result.native_entered=finale.active() && mode==2;
                result.executed=finale.mode(mode)==lcs::EndingTuneResult::accepted;
                if(!result.executed)result.diagnostic="Owned finale mode transition failed";return result;
            };
            adapter.ending_tune=[this](bool play){
                lcs::NativeResult result;
                if(!original_audio_main || !original_audio_menu || !finale.ready() || !ending_tune_settings()) {
                    result.diagnostic="Finale audio phase/resource/settings unavailable";return result;
                }
                result=lcs::execute_sa({0x041E,{13}});if(!result.executed)return result;
                result.native_entered=true;
                // The original frontend service retires an active announcement.
                if(!audio.tick_announcements(false)){result.executed=false;result.diagnostic="Cannot retire original radio announcement";return result;}
                if(!play && adapter.music_mode()==2 && adapter.clear_cutscene_audio)adapter.clear_cutscene_audio();
                result.executed=play?finale.request():finale.mode(1)==lcs::EndingTuneResult::accepted;
                if(!result.executed)result.diagnostic="Owned finale request failed";return result;
            };
        }
        std::ifstream original_weapons(source_file(directory,"DATA/WEAPON.DAT"));
        if(!original_weapons)throw std::runtime_error("Original DATA/WEAPON.DAT.bin is required for mission inventory identity");
        adapter.load_weapon_definitions(original_weapons);
        adapter.native_weapon_slot=lcs::sa_native_weapon_slot;
        adapter.can_see_dead_char=lcs::sa_can_see_dead_char;
        adapter.set_threat_range_multiplier=lcs::sa_set_threat_range_multiplier;
        if(player_skins) {
            adapter.begin_outfit=[](int32_t actor,const std::string& label){return player_skins->begin(actor,label);};
            adapter.finish_outfit=[](int32_t actor){return player_skins->finish(actor);};
            adapter.current_outfit=[](int32_t actor){return player_skins->wearing(actor);};
        }
        hud.update=lcs::sa_hud_update;hud.read_cell=lcs::sa_hud_read;hud.write_cell=lcs::sa_hud_write;
        runtime.frame_time_step=lcs::sa_frame_time_step;
        adapter.mission_script=[this](const lcs::Thread& t){return runtime.is_mission_script(t);};
        adapter.stored_player_weapon=lcs::sa_saved_player_weapon;
        adapter.camera_ped_zoom=lcs::sa_camera_ped_zoom;adapter.freeze_ped_zoom=lcs::sa_freeze_ped_zoom;
        adapter.disable_pause_menu=lcs::sa_disable_pause_menu;
        adapter.create_garage=lcs::sa_create_garage;
        adapter.rotate_garage_door=lcs::sa_rotate_garage_door;
        adapter.leave_garage_camera=lcs::sa_leave_garage_camera;
        adapter.set_garage_capacity=lcs::sa_set_garage_capacity;
        adapter.clear_garage_message=lcs::sa_clear_garage_message;
        {
            std::ifstream ide(source_file(directory,L"DATA/DEFAULT.IDE")),handling(source_file(directory,L"DATA/HANDLING.CFG"));
            if(ide && handling) {
                auto& garages=adapter.exclusive_garages;
                garages.catalog.load(ide,handling);
                garages.world.vehicle_slice_checked=lcs::sa_garage_vehicle_slice;
                garages.world.vehicle=lcs::sa_route_vehicle_snapshot;
                garages.world.vehicle_lookup=lcs::sa_route_vehicle_lookup;
                garages.world.ped_identity=lcs::sa_garage_ped_identity;
                garages.world.original_model=[this](int32_t handle){return adapter.original_vehicle_model(handle);};
                garages.world.execute=[this](const lcs::NativeCall& call){return adapter.execute(call);};
                garages.world.player=[]()->std::optional<lcs::ExclusiveGaragePlayer>{
                    const auto native=lcs::execute_sa({0x01F5,{0},{},0,1});
                    if(!native.executed || native.values[0]<0)return {};
                    const auto actor=lcs::sa_live_objective_actor(native.values[0]);
                    const auto pose=actor?lcs::sa_route_actor_vehicle_snapshot(*actor):std::nullopt;
                    if(!actor || !pose || !(pose->actor==*actor) || !pose->alive)return {};
                    lcs::ExclusiveGaragePlayer facts;
                    facts.ped=actor->handle;facts.ped_entity=actor->native_ped;facts.position=pose->position;
                    facts.entering_car=pose->source_entering_car;
                    if(pose->in_vehicle) {
                        if(pose->current_vehicle_handle<0 || !pose->current_vehicle_entity)return {};
                        facts.vehicle=pose->current_vehicle_handle;
                    }
                    return facts;
                };
                garages.world.player_on_mission=[this]{return runtime.on_mission();};
                garages.world.crusher_reward_message=[this](int32_t reward){
                    if(!adapter.text_ready || !adapter.display_message)return false;
                    lcs::MissionMessage message;message.key="CRUSHED";message.duration=5000;message.flags=1;message.numbers[0]=reward;
                    return adapter.display_message(message);
                };
                garages.world.crusher_ready=[]()->std::optional<bool>{return crusher_cleanup_pending?std::nullopt:lcs::sa_crusher_ready();};
                garages.world.crusher_door=[](int32_t id,const std::array<float,8>& bounds,float angle){
                    return crusher_cleanup_pending?lcs::NativeResult{}:lcs::sa_crusher_door(id,bounds,angle);
                };
                garages.world.collector_door_height=[](int32_t,const std::array<float,8>& bounds)->std::optional<float>{
                    return garage_door_cleanup_pending?std::nullopt:lcs::sa_garage_door_height(bounds);
                };
                garages.world.collector_door=[](int32_t,const std::array<float,8>& bounds,float position){
                    return garage_door_cleanup_pending?lcs::NativeResult{}:lcs::sa_garage_door_move(bounds,position);
                };
                garages.world.collected_read=lcs::sa_garage_collected_read;
                garages.world.collected_write=lcs::sa_garage_collected_write;
                garages.world.collector_vehicle_touches=lcs::sa_crusher_collector_vehicle_touches;
                garages.world.collector_player_outside=lcs::sa_crusher_collector_player_outside;
                garages.world.collector_vehicle_deliverable=lcs::sa_crusher_collector_vehicle_deliverable;
                garages.world.entirely_inside=lcs::sa_crusher_contains;
                garages.world.crusher_hold=lcs::sa_crusher_hold;
                garages.world.crusher_release=lcs::sa_crusher_release_vehicle;
                {
                    // Retail CCranes over the exact mounted crusher crane; the focused player stands in for the camera.
                    auto& crane=adapter.crane_service;
                    crane.catalog=&garages.catalog;
                    crane.world.camera=[player=garages.world.player]()->std::optional<std::array<float,3>>{
                        const auto facts=player();if(!facts)return {};return facts->position;
                    };
                    crane.world.vehicle_slice_checked=lcs::sa_garage_vehicle_slice;
                    crane.world.vehicle_lookup=lcs::sa_route_vehicle_lookup;
                    crane.world.original_model=garages.world.original_model;
                    crane.world.player=garages.world.player;
                    crane.world.player_entry_target=lcs::sa_crane_player_entry_target;
                    crane.world.crane_ready=[](size_t index)->std::optional<bool>{return crane_cleanup_pending?std::nullopt:lcs::sa_crane_ready(index);};
                    crane.world.crane_rotate=[](size_t index,float angle){return crane_cleanup_pending?lcs::NativeResult{}:lcs::sa_crane_rotate(index,angle);};
                    crane.world.vehicle_top=lcs::sa_crane_vehicle_top;
                    crane.world.vehicle_forward_angle=lcs::sa_crane_vehicle_forward_angle;
                    crane.world.hold=[](const lcs::RouteVehicleSnapshot& car){return crane_cleanup_pending?lcs::NativeResult{}:lcs::sa_crane_hold(car);};
                    crane.world.carry=lcs::sa_crane_carry;
                    crane.world.release=lcs::sa_crane_release;
                    crane.world.message=[this](const std::string& key){
                        if(!adapter.text_ready || !adapter.display_message)return false;
                        lcs::MissionMessage message;message.key=key;message.duration=4000;message.flags=1;
                        return adapter.display_message(message);
                    };
                    crane.world.time_step=[]{const auto step=lcs::sa_frame_time_step();return step?*step:std::numeric_limits<float>::quiet_NaN();};
                }
            } else log("Original crusher vehicle catalogue unavailable; physical service remains unbound");
        }
        adapter.collectables=lcs::sa_collectables;
        adapter.switch_angled_roads=lcs::sa_switch_angled_roads;
        adapter.live_ped_identity=lcs::sa_live_objective_actor;
        const auto route_actor=[this](int32_t handle)->std::optional<lcs::ObjectiveActor> {
            const auto it=route_actor_refs.find(handle);
            if(it==route_actor_refs.end() || !it->second->live())return {};
            return it->second->actor;
        };
        lcs::OwnedRouteReaders route_readers;
        route_readers.actor=[this](uint32_t ped)->std::optional<lcs::ObjectiveActor> {
            std::optional<lcs::ObjectiveActor> found;
            for(const auto& [handle,ref]:route_actor_refs) {
                (void)handle;
                if(ref->actor.native_ped!=ped || !ref->live())continue;
                if(found)return {};found=ref->actor;
            }
            return found;
        };
        const auto route_state=[route_actor](lcs::ObjectiveActor actor)->std::optional<int32_t> {
            const auto live=route_actor(actor.handle);if(!live || !(*live==actor))return {};
            const auto facts=lcs::sa_route_actor_vehicle_snapshot(actor);
            return facts?lcs::original_cop_state(facts->native_state):std::nullopt;
        };
        const auto route_vehicle=lcs::guard_native_executor(lcs::sa_execute_vehicle_task_with_capture);
        route_service=std::make_unique<lcs::SaRouteService>(route_intents,route_scripts.table(),
            std::move(route_readers),route_actor,route_state,
            [this](int32_t actor){adapter.clear_walking_for_source_route(actor);},route_vehicle);
        adapter.execute_vehicle_objective=route_vehicle;
        adapter.set_enter_car_range_multiplier=[this](uint16_t value){
            return lcs::sa_owned_vehicle_entry_wait_ready() && route_service->set_enter_car_range_multiplier(value);
        };
        adapter.char_objective_passed=[this](int32_t actor){return route_service->objective_passed(actor);};
        route_service->set_walking_receipts(
            [](const lcs::CapturedScriptTask& captured)->std::optional<lcs::EpochScriptTask>{return task_observer?task_observer->accept(captured):std::nullopt;},
            [](const lcs::EpochScriptTask& token)->std::optional<lcs::EpochScriptTask>{return task_observer?task_observer->refresh(token):std::nullopt;},
            [](const lcs::EpochScriptTask& token)->std::optional<lcs::ScriptTaskRelease>{return task_observer?task_observer->released(token):std::nullopt;},
            lcs::sa_execute_walk_with_capture);
        adapter.set_allowed_collision=lcs::sa_set_allowed_collision;
        // Standalone entries share the owned native wait/resume policy.
        adapter.execute_walking_objective=lcs::sa_execute_walk_with_capture;
        adapter.execute_source_objective=[this](uint16_t original,const lcs::NativeCall& call,bool& projected,
                const std::function<lcs::NativeResult()>& once) {
            return route_service->execute_source_objective(original,call,projected,once);
        };
        route_scripts.set_follow_route_handler([this](int32_t actor,int32_t route,int32_t mode,lcs::Thread& thread) {
            if(!adapter.world_ready){thread.diagnostic="Original route world is not mounted";return lcs::StepResult::unsupported;}
            const auto result=route_service->follow_route(actor,route,mode,runtime.is_mission_script(thread));
            if(result==lcs::StepResult::unsupported || result==lcs::StepResult::failed)
                thread.diagnostic=route_service->diagnostic().empty()?
                    "Original route actor, source state or native consumer unavailable":route_service->diagnostic();
            return result;
        });
        const auto register_route_actor=[this](const lcs::ObjectiveActor& actor){
            for(auto it=route_actor_refs.begin();it!=route_actor_refs.end();) {
                if(it->first==actor.handle || it->second->actor.native_ped==actor.native_ped)
                    it=route_actor_refs.erase(it);
                else ++it;
            }
            auto reference=std::make_unique<RouteActorReference>(actor);
            if(!lcs::sa_register_actor_reference(actor,reference->cell))
                throw std::runtime_error("Native source actor lifetime unavailable");
            route_actor_refs.emplace(actor.handle,std::move(reference));
        };
        adapter.source_actor_created=[this,register_route_actor](const lcs::ObjectiveActor& actor){
            register_route_actor(actor);
            // Original CREATE_CHAR unconditionally assigns MISSION_CHAR,
            // independently of the script's mission-cleanup flag.
            if(!route_service->constructed(actor,uint8_t(2)))throw std::runtime_error("Source constructor intent revision unavailable");
        };
        adapter.source_actor_acquired=[this,register_route_actor](const lcs::ObjectiveActor& actor){
            const auto found=route_actor_refs.find(actor.handle);
            if(found!=route_actor_refs.end() && found->second->actor==actor && found->second->live())return;
            register_route_actor(actor);
            if(!route_service->acquired(actor,uint8_t(2)))throw std::runtime_error("Acquired source actor intent unavailable");
        };
        adapter.source_actor_deleted=[this](const lcs::ObjectiveActor& actor){
            lcs::sa_erase_player_friend(actor);
            lcs::sa_clear_char_attack_timer(actor);
            route_service->retired(actor);
            const auto it=route_actor_refs.find(actor.handle);
            if(it!=route_actor_refs.end() && it->second->actor==actor)route_actor_refs.erase(it);
        };
        adapter.source_actor_goal_unknown=[this](int32_t actor){
            route_service->untracked(actor);route_actor_refs.erase(actor);
        };
        adapter.source_actor_running=[this](const lcs::ObjectiveActor& actor,bool enabled){
            const auto it=route_actor_refs.find(actor.handle);
            if(it==route_actor_refs.end() || !(it->second->actor==actor) || !it->second->live())
                throw std::runtime_error("Native source actor lifetime retired");
            if(!route_service->running(actor,enabled))
                throw std::runtime_error("Source running intent changed before acceptance");
        };
        adapter.char_in_control=lcs::sa_char_in_control;adapter.player_made_safe=lcs::sa_player_made_safe;
        adapter.char_entering_as_passenger=lcs::sa_char_entering_as_passenger;
        adapter.task_sequence_state=lcs::sa_task_sequence_state;
        adapter.abort_unissued_task_sequence=lcs::sa_abort_unissued_task_sequence;
        adapter.ped_group=[](const lcs::ObjectiveActor& actor)->std::optional<lcs::NativeAdapter::PedGroupFacts>{
            const auto group=lcs::sa_ped_group_snapshot(actor);
            if(!group)return std::nullopt;
            return lcs::NativeAdapter::PedGroupFacts{group->member,group->leader,group->leader_ped,group->group,group->free_group};
        };
        adapter.native_model_class=lcs::sa_model_class;
        adapter.pickup_model_in_area=lcs::sa_pickup_model_in_area;
        adapter.diagnostic_event=[](const std::string& message){log(message);};
        frame_updates.diagnostic_event=adapter.diagnostic_event;
        adapter.set_char_crouch=lcs::sa_set_char_crouch;
        adapter.set_char_can_fight=lcs::sa_set_char_can_fight;
        adapter.set_stationary_shooter=lcs::sa_set_stationary_shooter;
        adapter.set_char_heed_threats=lcs::sa_set_char_heed_threats;
        adapter.set_gang_damage=lcs::sa_set_gang_damage;
        adapter.set_char_crouch_when_threatened=lcs::sa_set_char_crouch_when_threatened;
        adapter.set_char_threats_behind_objects=lcs::sa_set_char_threats_behind_objects;
        adapter.attack_policy_actor=lcs::sa_attack_policy_actor;
        adapter.attack_policy_world=lcs::sa_attack_policy_world;
        adapter.attack_policy_task_accept=[](const lcs::CapturedScriptTask& captured)->std::optional<lcs::EpochScriptTask>{
            return task_observer?task_observer->accept(captured):std::nullopt;
        };
        adapter.attack_policy_task_active=[](const lcs::ObjectiveActor& actor,const lcs::EpochScriptTask& token)->std::optional<bool>{
            if(!task_observer || !task_observer->active())return std::nullopt;
            const auto current=task_observer->refresh(token);
            if(!current)return false;
            if(current->binding.record.status==0)return std::nullopt; // accepted event not associated yet
            return lcs::sa_active_walking_task(actor,current->binding);
        };
        adapter.set_pednode_seek=lcs::sa_set_pednode_seek;
        adapter.erase_pednode_seek=lcs::sa_erase_pednode_seek;
        adapter.set_char_personality=lcs::sa_set_char_personality;
        adapter.display_message=lcs::sa_display_message;
        adapter.display_big_message=lcs::sa_display_big_message;
        adapter.scripted_pad_state=lcs::sa_scripted_pad_state;
        adapter.random_char_in_area=lcs::sa_random_char_in_area;
        adapter.sniper_bullet_in_area=lcs::sa_sniper_bullet_in_area;
        adapter.random_cop_in_area=lcs::sa_random_cop_in_area;
        adapter.random_char_unchecked=lcs::sa_random_char_unchecked;
        adapter.char_stuck=lcs::sa_char_stuck;
        adapter.random_char_in_zone=lcs::sa_random_char_in_zone;
        adapter.apply_car_tilt=lcs::sa_apply_car_tilt;
        adapter.car_jerk_sound=[this](const std::array<float,3>& position){return audio.play_car_jerk(position);};
        adapter.fx.callbacks.emit=lcs::sa_fx_emit;
        adapter.fx.callbacks.fire_start=lcs::sa_fx_fire_start;
        adapter.fx.callbacks.fire_stop=lcs::sa_fx_fire_stop;
        adapter.fx.callbacks.release=lcs::sa_fx_release_all;
        adapter.fx.callbacks.camera=[]()->std::optional<lcs::FxVec3>{return lcs::sa_camera_placeable_position();};
        adapter.fx.diagnostic=[](const std::string& message){log(message);};
        if(!first) {
            gameplay_animations=std::make_unique<lcs::NativeGameplayAnimations>(std::filesystem::path(setting(L"WorldDirectory"))/L"animations");
            adapter.request_wait_animation=[this](int32_t state,bool load){
                const bool accepted=gameplay_animations->request(state,load);
                if(!accepted)log("Original gesture block request refused: state="+std::to_string(state)+" load="+std::to_string(load)+" "+lcs::gameplay_animation_load_diagnostic());
                return accepted;
            };
            adapter.named_animation=[this](const std::string& name,uint16_t action){return gameplay_animations->named(name,action);};
            adapter.play_wait_animation=[this](int32_t actor,int32_t state,int32_t duration,bool repeat){return gameplay_animations->play(actor,state,duration,repeat);};
            adapter.clear_wait_animation=[this](int32_t actor){return gameplay_animations->clear(actor);};
            adapter.wait_animation_active=[this](int32_t actor)->std::optional<bool>{return gameplay_animations->waiting(actor);};
            log("Original mission gestures validated: 71 states, 25 original blocks; native loading on demand");
        }
        runtime.vm().message_skip=[this](){return skip_pressed;};
        const auto cutscene_directory=setting(L"CutsceneDirectory");
        if(!cutscene_directory.empty()) {
            cutscenes=std::make_unique<lcs::NativeCutscenes>(resource_directory(),std::filesystem::path(cutscene_directory),adapter);
            log("Original cutscene archives/audio verified and native aliases registered");
        }
        if(first) {
            validate_first_mission_world();
            first_mission=std::make_unique<lcs::FirstMission>(image,runtime,adapter);
            first_mission->execute=adapter.execute;first_mission->log=log;
            first_mission->opening_movie=cutscenes && adapter.has_cutscene("homswet");
            first_mission->arrival_movie=cutscenes && adapter.has_cutscene("intro");
        }
        audio.play=[this](size_t slot,const lcs::AudioSample& sample){return speech.play(slot,sample);};
        audio.finished=[this](size_t slot){return speech.finished(slot);};audio.stop=[this](size_t slot){speech.stop(slot);};
        audio.play_effect=[this](const lcs::AudioSample& sample){return speech.play_effect(sample);};
        audio.play_positioned_effect=[this](const lcs::AudioSample& sample,lcs::AudioMix mix){return speech.play_owned_effect(sample,mix).accepted;};
        audio.spatial_listener=lcs::sa_audio_listener;
        audio.set_mix=[this](size_t slot,lcs::AudioMix mix){return speech.set_mix(slot,mix);};
        audio.listener=[]()->std::optional<std::array<float,3>>{
            const auto camera=lcs::execute_sa({0x068D,{},{},0,3});
            if(!camera.executed)return {};
            std::array<float,3> position{};for(size_t i=0;i<3;++i)std::memcpy(&position[i],&camera.values[i],4);
            return position;
        };
        coronas.available=[this](){return adapter.world_ready && lcs::sa_script_coronas_available();};
        coronas.render=lcs::sa_register_script_corona;
        coronas.ground=lcs::sa_script_corona_ground;
        const auto marker_directory=resource_directory()/L"markers";
        markers.available=[this,marker_directory](){return adapter.world_ready && lcs::sa_script_marker_resources_load(marker_directory);};
        markers.ground=lcs::sa_script_marker_ground;
        markers.submit=[this](const lcs::ScriptMarkerCommand& request){return lcs::sa_script_marker_submit(marker_pool,request,runtime.vm().time_step);};
        collectives.available=[this](){return adapter.world_ready && lcs::sa_collectives_available();};
        collectives.candidates=lcs::sa_collective_candidates;
        collectives.live=lcs::sa_live_objective_actor;
        collectives.flee=[this](const lcs::ObjectiveActor& actor,const lcs::ObjectiveActor& target){
            // Same accepted 05DB packet as the individual source ALWAYS action.
            return adapter.execute_collective_flee(actor,target);
        };
        collectives.flee_till_safe=[this](const lcs::ObjectiveActor& actor,const lcs::ObjectiveActor& target){
            return adapter.execute_collective_flee_till_safe(actor,target);
        };
        collectives.flee_point_till_safe=[this](const lcs::ObjectiveActor& actor){
            return adapter.execute_flee_point_till_safe(actor);
        };
        runtime.engine=[this](lcs::Vm& vm,lcs::Thread& t,uint16_t op){
            if(first_mission && visited.emplace(t.last_ip,op).second)
                log("VIC1 ip="+std::to_string(t.last_ip)+" command="+lcs::opcode_name(op));
            if(const auto sound=lcs::try_engine_adapter(audio,vm,t,op))return *sound;
            if(const auto looping=lcs::try_engine_adapter(continuous_sounds,vm,t,op))return *looping;
            if(const auto display=lcs::try_engine_adapter(hud,vm,t,op))return *display;
            if(const auto corona=lcs::try_engine_adapter(coronas,vm,t,op))return *corona;
            if(const auto marker=lcs::try_engine_adapter(markers,vm,t,op))return *marker;
            if(const auto collective=lcs::try_engine_adapter(collectives,vm,t,op))return *collective;
            if(const auto route=lcs::try_engine_adapter(route_scripts,vm,t,op))return *route;
            if(first_mission) {
                if(const auto result=lcs::try_engine_adapter(*first_mission,vm,t,op))return *result;
            }
            const auto result=adapter.dispatch(vm,t,op);
            // Admission belongs to the original script that opened the native
            // menu. A save helper may itself reside in the mission allocation.
            if(op==989 && result==lcs::StepResult::continued){
                uint32_t requested=0;
                if(read_game_word(0xBA67A7,&requested) && (requested&255))original_save_menu_requested=true;
            }
            if(op==990 && result==lcs::StepResult::continued){
                uint32_t menu=0,requested=0;
                if(read_game_word(0xBA67A4,&menu) && read_game_word(0xBA67A7,&requested) && !(menu&255) && !(requested&255))original_save_menu_requested=false;
            }
            return result;
        };
        lcs::NativeCall flag;flag.opcode=0x0180;flag.mission_flag_reference=true;
        if(!lcs::execute_sa(flag).executed)throw std::runtime_error("Cannot declare reserved native mission flag");
        if(!first_mission && lcs::FirstMission::supports_verified_image(image)) {
            runtime.vm().before_step=[this](lcs::Vm& vm,lcs::Thread& thread) {
                if(runtime.mission_index()!=9 || !runtime.is_mission_script(thread))return false;
                const bool redirected=lcs::FirstMission::redirect_destroyed_car_failure(vm,thread);
                if(redirected)log("Destroyed mission car: original failure messages, cleanup and critical restart selected");
                return redirected;
            };
        }
        if(!restoring){if(first_mission)first_mission->start();else runtime.start_main();}
        // Publish the borrowed service only after every throwing setup/start
        // operation succeeded. A failed constructor never leaves a callback
        // into its unwound route_service in the process-global observer.
        if(task_observer)task_observer->set_release_probe(route_service->walking_release_probe());
    }
    void release_world_sectors() {
        if(!world_sectors)return;
        if(!world_sectors->cleanup().executed){
            log("World-sector cleanup retained for safe recovery: "+world_sectors->diagnostic());
            retired_world_sectors.push_back(std::move(world_sectors));
        }else world_sectors.reset();
        adapter.world_swap_state={};adapter.request_world_swaps={};adapter.world_streaming_complete={};adapter.commit_world_sector={};
    }
    lcs::CampaignCheckpoint capture_save(uint32_t now) {
        if(halted)throw std::runtime_error("Cannot save a halted campaign");
        if(first_mission)throw std::runtime_error("Cannot save the isolated FirstMission test mode");
        if(adapter.has_active_cutscene())throw std::runtime_error("Cannot save during an active cutscene");
        if(runtime.on_mission() && !original_save_menu_requested)
            throw std::runtime_error("A story mission did not authorize the save menu");
        // mission_buffer_in_use() is a code-lifetime lease, not an on-mission
        // flag. VM snapshots already preserve its bytecode, locals and frames.
        if(route_intents.has_active_route())throw std::runtime_error("Cannot save while a script-controlled route is active");
        if(restoring_world || !hud.pull(runtime.vm()))throw std::runtime_error("Campaign save is waiting for restored world/HUD");
        const auto player=lcs::execute_sa({0x01F5,{0},{},0,1});
        const auto outfit=player.executed && player_skins?player_skins->wearing(player.values[0]):std::nullopt;
        if(!outfit)throw std::runtime_error("Cannot save without a committed Toni outfit");
        lcs::CampaignCheckpoint state;state.time=now;state.outfit=*outfit;
        state.runtime=runtime.save_state();state.adapter=adapter.save_state(now);
        state.services=lcs::CampaignServicesSave::save(hud,collectives,route_scripts,coronas,continuous_sounds,adapter.fx,*world_sectors,now);
        // Music area is source identity, not SA interior zero.
        lcs::CampaignSaveWriter extra;extra.u32(uint32_t(source_music_area));extra.pod(marker_process.previous_point);extra.bytes(state.services);state.services=std::move(extra.data);
        return state;
    }
    void restore_save(const lcs::CampaignCheckpoint& state,uint32_t now) {
        runtime.restore_state(state.runtime,now);
        std::string diagnostic;
        if(!adapter.restore_state(state.adapter,now,diagnostic))throw std::runtime_error("LCS services: "+diagnostic);
        lcs::CampaignSaveReader extra{state.services};const auto music_area=int32_t(extra.u32());const auto previous=extra.pod<std::array<float,3>>();const auto services=extra.bytes();extra.end();
        for(float x:previous)if(!std::isfinite(x))throw std::runtime_error("Invalid saved marker position");
        pending_saved_world=lcs::CampaignServicesSave::restore(services,hud,collectives,route_scripts,coronas,continuous_sounds,adapter.fx,*world_sectors,runtime.vm(),now);
        source_music_area=music_area;marker_process.previous_point=previous;restoring_world=true;
    }
    bool finish_restore() {
        if(!restoring_world)return true;
        const auto requested=world_sectors->request();
        if(!requested.executed)throw std::runtime_error("Cannot request saved world layers: "+requested.diagnostic);
        const auto ready=world_sectors->complete();if(!ready)throw std::runtime_error("Cannot verify saved world layers: "+world_sectors->diagnostic());
        if(!*ready)return false;
        const auto committed=world_sectors->commit();if(!committed.executed)throw std::runtime_error("Cannot commit saved world layers: "+committed.diagnostic);
        if(!committed.condition)return false;
        // Restore the physically committed world first. The original VM must
        // still drive commit of any source transition that was pending at SAVE.
        lcs::CampaignServicesSave::apply_pending(pending_saved_world,*world_sectors);
        pending_saved_world={};
        restoring_world=false;log("Campaign LOAD restored LCS globals/threads/services and Toni without replaying MAIN");return true;
    }
    void release_taxi_hails() {
        if(taxi_hails && !taxi_hails->clear_all())retired_taxi_hails.push_back(std::move(taxi_hails));
        else taxi_hails.reset();
        adapter.hail_taxi={};
    }
    ~Campaign() {
        // Task releases during cleanup must not call a retiring service.
        if(task_observer && task_observer_on_thread()){task_observer->set_release_probe({});task_observer->reset();}
        crusher_cleanup_pending=!lcs::sa_crusher_cleanup();
        garage_door_cleanup_pending=!lcs::sa_garage_door_cleanup();
        crane_cleanup_pending=!(adapter.crane_service.release_all() && lcs::sa_crane_cleanup());
        ferry_cleanup_pending=!lcs::sa_ferry_cleanup();
        threat_occlusion_cleanup_pending=!lcs::sa_release_threat_occlusion();
        move_anim_cleanup_pending=!lcs::sa_release_move_anim_overrides();
        jack_protection_cleanup_pending=!lcs::sa_release_jack_protections();
        allowed_collision_cleanup_pending=!lcs::sa_release_allowed_collision();
        car_reward_cleanup_pending=!lcs::sa_release_car_rewards();
        player_friend_cleanup_pending=!lcs::sa_release_player_friends();
        release_city_music();
        security_cleanup_pending=!lcs::sa_security_camera_cleanup();
        attack_timer_cleanup_pending=!lcs::sa_release_char_attack_timer();
        release_taxi_hails();
        release_world_sectors();
        lcs::sa_lcs_bridge_cleanup();
        adapter.fx.reset();
        if(!continuous_runtime.shutdown())log("Continuous script sound output release refused; owned PCM retained");
        if(!lcs::sa_release_gang_damage())log("Later damage hook preserved; original gang permissions released");
        if(!lcs::sa_release_threat_ranges())log("Cannot restore native threat scanner radii; state retained for retry");
        marker_pool.clear();coronas.clear();hud.clear();lcs::sa_release_pause_menu();
        lcs::sa_release_pednode_seek();
        lcs::sa_release_garage_capacity();
    }
    void abort() {
        if(task_observer && task_observer_on_thread()){task_observer->set_release_probe({});task_observer->reset();}
        crusher_cleanup_pending=!lcs::sa_crusher_cleanup();
        garage_door_cleanup_pending=!lcs::sa_garage_door_cleanup();
        crane_cleanup_pending=!(adapter.crane_service.release_all() && lcs::sa_crane_cleanup());
        ferry_cleanup_pending=!lcs::sa_ferry_cleanup();
        threat_occlusion_cleanup_pending=!lcs::sa_release_threat_occlusion();
        move_anim_cleanup_pending=!lcs::sa_release_move_anim_overrides();
        jack_protection_cleanup_pending=!lcs::sa_release_jack_protections();
        allowed_collision_cleanup_pending=!lcs::sa_release_allowed_collision();
        car_reward_cleanup_pending=!lcs::sa_release_car_rewards();
        player_friend_cleanup_pending=!lcs::sa_release_player_friends();
        release_city_music();
        security_cleanup_pending=!lcs::sa_security_camera_cleanup();
        attack_timer_cleanup_pending=!lcs::sa_release_char_attack_timer();
        release_taxi_hails();
        release_world_sectors();
        adapter.fx.reset();
        if(!continuous_runtime.shutdown())log("Continuous script sound output release refused; owned PCM retained");
        if(!lcs::sa_release_gang_damage())log("Later damage hook preserved; original gang permissions released");
        halted=true;credits.stop();speech.stop(0);speech.stop(1);speech.stop(2);
        marker_pool.clear();
        if(!finale.shutdown())log("Finale driver release refused; owned PCM retained");
        if(route_service)route_service->clear();
        route_intents.clear();route_actor_refs.clear();adapter.stop_scripted_ped_updates();
        if(!lcs::sa_release_pednode_seek())log("Native seek task hook changed; later hook preserved");
        if(!adapter.release_script_markers())log("Could not remove an owned LCS marker after campaign interruption");
        gameplay_animations.reset();
        coronas.clear();
        if(!hud.clear())log("Could not clear an owned native mission HUD entry");
        if(!lcs::sa_release_pause_menu())log("Native frontend hook changed; pause blocking was released without overwriting another hook");
        if(!lcs::sa_release_garage_capacity())log("Native garage capacity hook changed; later hook was preserved");
        if(cutscenes)cutscenes->clear();
        lcs::sa_freeze_ped_zoom(false);
        if(adapter.has_active_cutscene())lcs::execute_sa({0x02EA,{}});
        for(auto call:std::initializer_list<lcs::NativeCall>{
            {0x01B4,{0,1}},{0x01F7,{0,0}},{0x03BF,{0,0}},
            {0x02EB,{}},{0x02A3,{0}},{0x03B7,{1}},{0x03AF,{1}},{0x016A,{250,1}}})lcs::execute_sa(call);
        write_game_word(0xA49960+24,0);
        lcs::sa_show_campaign_error(failure_notice);
    }
    std::vector<lcs::Execution> tick(uint32_t now,uint32_t elapsed) {
        if(last_door_status.empty() || now-last_door_status_time>=5000) {
            last_door_status_time=now;
            const auto camera=lcs::execute_sa({0x068D,{},{},0,3});
            if(camera.executed) {
                std::array<float,3> at{};
                for(size_t i=0;i<3;++i)std::memcpy(&at[i],&camera.values[i],4);
                std::ostringstream state;
                for(const auto& door:lcs::sa_garage_door_runtime_statuses()) {
                    const auto dx=door.placement[0]-at[0],dy=door.placement[1]-at[1];
                    if(dx*dx+dy*dy>10000.0f)continue;
                    state<<" model="<<door.model<<" B/D/O="<<door.buildings<<'/'<<door.dummies<<'/'<<door.objects
                        <<" pools="<<door.building_pool_available<<door.dummy_pool_available<<door.object_pool_available
                        <<" info="<<door.model_info_available<<':'<<door.object_info_index
                        <<" door="<<door.garage_door_model<<" COL="<<door.collision_model_available<<'/'<<door.collision_ready
                        <<" garage="<<int(door.native_garage_index);
                    if(door.object_sample_available) {
                        if(door.object_z_available)state<<" objectZ="<<door.object_z;
                        if(door.has_dummy_known)state<<" dummy="<<door.has_dummy;
                        if(door.dummy_z_available)state<<" dummyZ="<<door.dummy_z;
                        if(door.control_code_known)state<<" control="<<door.control_code_linked;
                        if(door.native_garage_state_available)
                            state<<" state="<<unsigned(door.native_garage_state)
                                <<" progress="<<door.native_garage_progress
                                <<" flags="<<unsigned(door.native_garage_flags);
                    }
                }
                const auto text=state.str();
                if(!text.empty() && text!=last_door_status)log("Nearby owned garage doors:"+text);
                last_door_status=text.empty()?"none nearby":text;
            }
        }
        if(halted){if(now-last_failure_notice>=2000){last_failure_notice=now;lcs::sa_show_campaign_error(failure_notice);}return {};}
        // Observe actual hook/context loss even if a mod bypasses the renderer
        // or no marker is currently visible. Never advance through a renderer
        // quarantine or retry an entered native draw as a VM command.
        if(!frame_updates.run("script-marker-health",lcs::UpdateScope::dependency,now,
            "Original private marker renderer needs repair",lcs::sa_script_marker_health))return {};
        if(!frame_updates.run("remote-door-policy",lcs::UpdateScope::dependency,now,
            "Original remote vehicle door policy needs repair",lcs::sa_remote_door_policy_runtime_healthy))return {};
        if(corona_pending){
            corona_pending=!frame_updates.run("script-coronas",lcs::UpdateScope::dependency,now,
                "Original script corona renderer refused ownership",[&](){return coronas.tick();});
            if(corona_pending)return {};
        }
        if(gameplay_animations)frame_updates.run("gesture-expiry",lcs::UpdateScope::auxiliary,now,"Cannot release an expired mission wait gesture",[&](){return gameplay_animations->tick(now);});
        skip_pressed=false;
        frame_updates.run("threat-scanner-ranges",lcs::UpdateScope::auxiliary,now,"Cannot update native threat scanner radii",lcs::sa_tick_threat_ranges);
        frame_updates.run("dialogue-input",lcs::UpdateScope::auxiliary,now,"Cannot read native dialogue input",[&](){
            const auto skip=lcs::execute_sa({0x00E2,{0,16},{},0,1});if(!skip.executed)return false;
            const bool down=skip.values[0]!=0;skip_pressed=down && !skip_down;skip_down=down;return true;
        });
        // HUD is a runtime input/output dependency. Hold the VM while its
        // actual binding is unavailable, retaining all code/markers/scene state.
        if(hud_write_pending) {
            if(!frame_updates.run("hud-write",lcs::UpdateScope::dependency,now,"Cannot synchronize original mission HUD variables",[&](){return hud.push(runtime.vm());}))return {};
            hud_write_pending=false;
        }
        if(runtime.on_mission()) {
            bool restarting=false;
            if(!frame_updates.run("player-restart",lcs::UpdateScope::dependency,now,"Cannot read native player restart state",[&](){return lcs::sa_player_is_restarting(restarting);}))return {};
            if(restarting && runtime.notify_player_restart()) {
                lcs::NativeCall clear;clear.opcode=0x03EB;lcs::execute_sa(clear);
            }
        }
        if(!frame_updates.run("hud-read",lcs::UpdateScope::dependency,now,"Native mission HUD renderer or clock binding unavailable",[&](){return lcs::sa_hud_presentation_healthy() && hud.pull(runtime.vm());}))return {};
        if(!first_mission) {
            frame_updates.run("radio-news",lcs::UpdateScope::auxiliary,now,"Original radio announcement playback failed",[&](){
                const auto player=lcs::execute_sa({0x01F5,{0},{},0,1});if(!player.executed || player.values[0]<0)return false;
                const auto car=lcs::execute_sa({0x00DF,{player.values[0]},{},0,0,true});
                return car.executed && audio.tick_announcements(car.condition && adapter.music_mode()==1 && !adapter.has_active_cutscene());
            });
        }
        if(!frame_updates.run("mission-flag",lcs::UpdateScope::dependency,now,"Cannot synchronize native mission flag",[&](){return write_game_word(0xA49960+24,runtime.on_mission()?1u:0u);}))return {};
        if(!frame_updates.run("source-routes",lcs::UpdateScope::dependency,now,
            "Original route objective consumer is unavailable",[&](){
                if(route_service->tick_all())return true;
                if(!route_service->diagnostic().empty())throw std::runtime_error(route_service->diagnostic());
                return false;
            }))return {};
        if(!frame_updates.run("island-collision-residency",lcs::UpdateScope::dependency,now,
            "Native island collision residency consumer failed",lcs::sa_allowed_collision_healthy))return {};
        const auto events=runtime.tick(now,elapsed,1000);
        corona_pending=!frame_updates.run("script-coronas",lcs::UpdateScope::dependency,now,"Original script corona renderer refused ownership",[&](){return coronas.tick();});
        frame_updates.run("attack-with-cops",lcs::UpdateScope::auxiliary,now,"Attack-with-cops reaction consumer unavailable",[&](){
            return adapter.tick_attack_with_cops();
        });
        frame_updates.run("ferry",lcs::UpdateScope::auxiliary,now,"Ferry consumer unavailable",[&](){
            const auto step=lcs::sa_frame_time_step();if(!step)return false;
            const bool ok=adapter.ferry_service.tick(now,uint32_t(*step*20.0f+0.5f));
            if(!ok && !adapter.ferry_service.diagnostic().empty())log(adapter.ferry_service.diagnostic());
            return ok;
        });
        frame_updates.run("physical-crane",lcs::UpdateScope::auxiliary,now,"Physical crane consumer unavailable",[&](){
            const bool ok=adapter.crane_service.tick(now);
            if(!ok && !adapter.crane_service.diagnostic().empty())log(adapter.crane_service.diagnostic());
            return ok;
        });
        frame_updates.run("physical-crusher",lcs::UpdateScope::auxiliary,now,"Physical crusher consumer unavailable",[&](){
            const auto step=lcs::sa_frame_time_step();if(!step)return false;
            const bool ok=adapter.exclusive_garages.tick(now,*step);
            if(!ok && !adapter.exclusive_garages.diagnostic().empty())log(adapter.exclusive_garages.diagnostic());
            return ok;
        });
        frame_updates.run("spatial-mission-speech",lcs::UpdateScope::auxiliary,now,"Cannot update original positioned mission speech",[&](){return audio.tick_spatial();});
        for(const auto& event:events)if(event.result==lcs::StepResult::unsupported || event.result==lcs::StepResult::failed) {
            const auto fault=runtime.failure(event.id);
            if(!fault) {
                // Original script cleanup may already have retired the failed
                // worker and reused its slot. Diagnostics use the event snapshot.
                log("Script fault retired during original cleanup: thread="+std::to_string(event.id)+" name="+event.name+
                    " ip="+std::to_string(event.ip)+" command="+lcs::opcode_name(event.opcode)+" diagnostic="+event.diagnostic);
                continue;
            }
            log("Script quarantined; scheduler continues: thread="+std::to_string(event.id)+" name="+event.name+
                " scope="+lcs::failure_scope_name(fault->scope)+" resumable="+(fault->resumable?"1":"0")+
                " missionCodeLeased="+(fault->mission_code_leased?"1":"0")+
                " dependentThreads="+std::to_string(fault->dependents.size())+
                " onMission="+(runtime.on_mission()?"1":"0")+
                " ip="+std::to_string(event.ip)+" command="+lcs::opcode_name(event.opcode)+" diagnostic="+event.diagnostic);
        }
        // Independent services never clear mission markers/HUD/cutscenes. A
        // mission or initializer dependency stays suspended and visibly reported
        // at the retained command; unrelated scripts still get their slices.
        const auto faults=runtime.failures();
        if(!faults.empty()) {
            const auto priority=std::find_if(faults.begin(),faults.end(),[](const lcs::ScriptFailure& f){return f.scope!=lcs::FailureScope::thread;});
            const auto service=std::find_if(faults.begin(),faults.end(),[this](const lcs::ScriptFailure& f){return !notified_faults.count(f.id);});
            const auto notice=priority==faults.end()?service:priority;
            if(notice!=faults.end() && (!notified_faults.count(notice->id) || now-last_scoped_notice>=10000)) {
                const auto& worker=runtime.script(notice->id);
                const auto prefix=notice->scope==lcs::FailureScope::thread?"~y~LCS: servico ":
                    notice->scope==lcs::FailureScope::mission?"~r~LCS: missao aguardando script ":"~r~LCS: inicio aguardando script ";
                lcs::sa_show_campaign_error(std::string(prefix)+worker.thread.name+" suspenso.~w~~n~Estado preservado. Consulte LcsSaBridge.log.");
                notified_faults.insert(notice->id);last_scoped_notice=now;
            }
        }
        if(taxi_hails)frame_updates.run("taxi-hail-cycle",lcs::UpdateScope::auxiliary,now,"Cannot maintain native taxi-hail cycle",[&](){return taxi_hails->tick();});
        std::string ped_diagnostic;
        frame_updates.run("scripted-ped-maintenance",lcs::UpdateScope::auxiliary,now,"Cannot maintain original scripted pedestrian policy",[&](){
            if(!adapter.tick_scripted_peds(ped_diagnostic))throw std::runtime_error(ped_diagnostic);
            return true;
        });
        frame_updates.run("mission-flag",lcs::UpdateScope::dependency,now,"Cannot synchronize native mission flag",[&](){return write_game_word(0xA49960+24,runtime.on_mission()?1u:0u);});
        if(first_mission && !halted)first_mission->tick(now);
        hud_write_pending=!frame_updates.run("hud-write",lcs::UpdateScope::dependency,now,"Cannot synchronize original mission HUD variables",[&](){return hud.push(runtime.vm());});
        return events;
    }
};
std::unique_ptr<Campaign> campaign;
// CGame::Process -> CTheScripts::Process. CLEO replaces the outer 0x53E981
// game-loop CALL, so diagnostics there can be lost to its DMA hook.
constexpr uintptr_t process_call = 0x53BFC7;
// The initial pass precedes Camera::Process and must already create the player.
constexpr uintptr_t startup_process_call = 0x53BCC9;
uint32_t last_bootstrap_stage = UINT32_MAX;

std::filesystem::path module_directory() {
    std::array<wchar_t,32768> path{};
    const auto n=GetModuleFileNameW(module,path.data(),DWORD(path.size()));
    if (!n || n==path.size()) throw std::runtime_error("Cannot resolve ASI directory");
    return std::filesystem::path(path.data()).parent_path();
}
std::filesystem::path resource_directory() {
    const auto configured=setting(L"ResourceDirectory");
    return configured.empty() ? module_directory().parent_path() : std::filesystem::path(configured);
}
std::string source_suffix() {
    const auto suffix=setting(L"SourceFileSuffix");
    if(suffix.empty())return "";
    if(suffix!=L".bin")throw std::runtime_error("SourceFileSuffix must be empty or .bin");
    return ".bin";
}
std::filesystem::path source_file(const std::filesystem::path& directory,const std::filesystem::path& relative) {
    auto path=directory/relative;path+=source_suffix();return path;
}
void log(const std::string& text) {
    std::ofstream f(module_directory()/L"LcsSaBridge.log",std::ios::app);
    SYSTEMTIME time{}; GetLocalTime(&time);
    f<<time.wYear<<'-'<<time.wMonth<<'-'<<time.wDay<<' '<<time.wHour<<':'<<time.wMinute<<':'<<time.wSecond<<' '<<text<<'\n';
}
void preflight() {
    try {
        const auto ini=module_directory()/L"LcsSaBridge.ini";
        std::array<wchar_t,32> mode{};
        GetPrivateProfileStringW(L"LCS",L"Mode",L"",mode.data(),DWORD(mode.size()),ini.c_str());
        if(std::wstring(mode.data())==L"WorldPreview") {
            log("Mode=WorldPreview; original mission data is not required for the native map bootstrap.");
            return;
        }
        std::array<wchar_t,32768> data{};
        GetPrivateProfileStringW(L"LCS",L"DataDirectory",L"",data.data(),DWORD(data.size()),ini.c_str());
        if (!data[0]) throw std::runtime_error("Set [LCS] DataDirectory in LcsSaBridge.ini");
        const auto image=lcs::ScmImage::load(source_file(std::filesystem::path(data.data()),L"DATA/MAIN.SCM"));
        lcs::Runtime runtime(image); const auto id=runtime.start_main();
        runtime.tick(0,0);
        const auto& script=runtime.script(id); const auto& thread=script.thread;
        std::ostringstream out;
        out<<"LCS preflight: main="<<image.main_size<<" missions="<<image.mission_offsets.size()
           <<" objects="<<image.objects.size()<<" ip="<<thread.last_ip<<" opcode=0x"<<std::hex<<thread.last_opcode
           <<std::dec<<" status="<<int(script.result)<<" diagnostic="<<thread.diagnostic;
        log(out.str());
        log("Mode=preflight; SA world, native peds and renderer remain active; LCS world is not mounted yet.");
    } catch (const std::exception& e) { log(std::string("Preflight error: ")+e.what()); }
}
bool read_game_word(uintptr_t address,uint32_t* value) {
    __try { *value=*reinterpret_cast<const uint32_t*>(address);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool write_game_word(uintptr_t address,uint32_t value) {
    __try {*reinterpret_cast<uint32_t*>(address)=value;return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
uint32_t model_key(const std::string& name) {
    uint32_t key=0xFFFFFFFF;
    for(unsigned char c:name) {
        if(c>='a' && c<='z')c-=32;
        key^=c;for(int i=0;i<8;++i)key=(key>>1)^((key&1)?0xEDB88320u:0u);
    }
    return key;
}
void validate_first_mission_world() {
    struct Donor {int32_t id;const char* name;uint8_t kind;};
    for(const auto& donor:std::initializer_list<Donor>{{124,"vmaff1",7},{125,"vmaff2",7},{547,"primo",6}}) {
        uint32_t info=0,key=0;
        if(!read_game_word(0xA9B0C8+4*donor.id,&info) || !info ||
           !read_game_word(info+4,&key) || key!=model_key(donor.name) ||
           lcs::sa_model_class(donor.id)!=donor.kind)
            throw std::runtime_error(std::string("First-mission native model/class mismatch: ")+donor.name);
    }
    uint32_t count=0,name0=0,name1=0,type=0,door=0,object_flags=0;
    // CGarages (primary Garage.h/Garages.h): slot zero, 0xD8 stride,
    // name at 0x44 and type at 0x4C. Original VIC1's garage global is zero.
    const bool readable=read_game_word(0x96C024,&count) &&
       read_game_word(0x96C048+0x44,&name0) && read_game_word(0x96C048+0x48,&name1) &&
       read_game_word(0x96C048+0x4C,&type) &&
       read_game_word(0xA9B0C8+4*10780,&door) && door && read_game_word(door+0x10,&object_flags);
    const auto door_class=lcs::sa_model_class(10780);
    std::array<char,9> garage_name{};
    std::memcpy(garage_name.data(),&name0,4);std::memcpy(garage_name.data()+4,&name1,4);
    std::ostringstream diagnostic;
    diagnostic<<"FirstMission garage: readable="<<readable<<" count="<<count
       <<" name="<<garage_name.data()<<" type="<<(type&255)
       <<" doorClass="<<(door_class ? int(*door_class):-1)
       <<" objectInfo="<<int16_t(object_flags&0xffff)<<" modelFlags=0x"<<std::hex<<(object_flags>>16);
    log(diagnostic.str());
    if(!readable || count!=1 || name0!=0x5653434C || name1!=0x00314349 ||
       (type&255)!=19 || int16_t(object_flags&0xffff)<0 ||
       ((object_flags>>16)&0x7800)!=0x3800 || door_class!=1)
        throw std::runtime_error("First-mission native garage/physical door profile mismatch; "+diagnostic.str());
    log("FirstMission native resources verified: ped classes, Primo, LCSVIC1 garage and dynamic garage door");
}
std::wstring setting(const wchar_t* name) {
    std::array<wchar_t,32768> value{};const auto ini=module_directory()/L"LcsSaBridge.ini";
    GetPrivateProfileStringW(L"LCS",name,L"",value.data(),DWORD(value.size()),ini.c_str());return value.data();
}
void retire_campaign_world() {
    // Retire owners while the OLD pools still exist. The bootstrap ready word
    // alone is insufficient: LOAD/New Game can clear and set it again between
    // two Process callbacks, retaining stale handles and a halted VM otherwise.
    campaign.reset();
    world_preview.reset();player_skins.reset();gameplay_skins.reset();
    bindings.clear();equivalent_bindings.clear();marker_process={};
    inspected_world=false;world_failure=false;last_game_time=0;
    last_bootstrap_stage=UINT32_MAX;
}
void observe_world() {
    service_retired_world_sectors(); // Independent of Campaign/halt; retain uncertain ownership until safe.
    uint32_t marker=0;
    if(!read_game_word(0xA49960+16,&marker) || marker!=0x4C435357) {
        // A new game or a return to the menu invalidates all old script handles.
        if(inspected_world || campaign || player_skins)retire_campaign_world();
        return;
    }
    if(campaign_load_requires_restore && !pending_campaign_load) {
        if(!world_failure)log("Campaign LOAD refused: "+campaign_load_diagnostic);
        world_failure=true;lcs::sa_show_campaign_error("Save sem estado LCS valido");return;
    }
    // A campaign failure must not stop servicing the independently mounted skins
    // and their double-sided draw callbacks as new pedestrians stream in.
    if(!inspected_world) {
        inspected_world=true;
        try {
            if(!lcs::sa_executor_profile_matches())throw std::runtime_error("Native command dispatcher profile mismatch");
            const auto directory=setting(L"WorldDirectory");
            if(directory.empty())throw std::runtime_error("WorldDirectory is not configured");
            std::ifstream file(std::filesystem::path(directory)/L"model-bindings.tsv");
            if(!file)throw std::runtime_error("Cannot open native model bindings");
            bindings.clear();std::string line;
            std::vector<std::string> binding_lines;
            while(std::getline(file,line))binding_lines.push_back(line);
            if(setting(L"Mode")==L"Campaign") {
                std::ifstream anchors(std::filesystem::path(directory)/L"campaign-anchor-bindings.tsv");
                while(std::getline(anchors,line))binding_lines.push_back(line);
            }
            for(const auto& binding_line:binding_lines) {
                std::istringstream row(binding_line);std::string lcs_id,sa_id;Binding b{};
                if(!std::getline(row,lcs_id,'\t') || !std::getline(row,sa_id,'\t') ||
                   !std::getline(row,b.name,'\t') || !std::getline(row,b.sa_name))throw std::runtime_error("Invalid binding row");
                if(!b.sa_name.empty() && b.sa_name.back()=='\r')b.sa_name.pop_back();
                b.lcs_id=std::stoi(lcs_id);b.sa_id=std::stoi(sa_id);
                if(b.sa_id<10000 || b.sa_id>=20000 || b.name.empty() || b.sa_name.empty())throw std::runtime_error("Invalid native model index");
                uint32_t pointer=0,key=0,collision=0;
                if(!read_game_word(0xA9B0C8+4*b.sa_id,&pointer) || !pointer ||
                   !read_game_word(pointer+4,&key) || key!=model_key(b.sa_name) ||
                   !read_game_word(pointer+0x14,&collision) || !collision)throw std::runtime_error("Native model/collision binding mismatch: "+b.sa_name);
                bindings.push_back(b);
            }
            if(bindings.empty())throw std::runtime_error("No LCS model bindings");
            equivalent_bindings.clear();
            if(setting(L"EquivalentEntities")==L"1") {
                std::ifstream equivalents(std::filesystem::path(directory)/L"native-equivalents.tsv");
                if(!equivalents)throw std::runtime_error("Native equivalent entity profile is missing");
                equivalent_bindings=lcs::read_equivalents(equivalents);
                for(const auto& e:equivalent_bindings) {
                    uint32_t info=0,key=0;
                    const auto kind=lcs::equivalent_model_class(e);
                    const auto& expected_name=lcs::equivalent_runtime_name(e);
                    if(!read_game_word(0xA9B0C8+4*e.native,&info) || !info ||
                       !read_game_word(info+4,&key) || key!=model_key(expected_name) ||
                       lcs::sa_model_class(e.native)!=kind)throw std::runtime_error("Equivalent native model/class mismatch: id="+std::to_string(e.native)+" expected="+expected_name+" observedKey="+std::to_string(key));
                }
                log("Mission-first native equivalent entities verified="+std::to_string(equivalent_bindings.size()));
            }
            log("Native world bootstrap observed; matching models and collision pointers="+std::to_string(bindings.size()));
            if(setting(L"PlayerSkins")==L"1") {
                player_skins=std::make_unique<lcs::SaPlayerSkins>(std::filesystem::path(directory));
                const auto configured=setting(L"PlayerOutfit");
                std::string initial="plr3";
                if(campaign_load_requires_restore && pending_campaign_load)initial=pending_campaign_load->outfit;
                else if(!configured.empty()) {
                    initial.clear();
                    for(const auto c:configured) {
                        if(c>127)throw std::runtime_error("PlayerOutfit must be an original ASCII label");
                        initial.push_back(static_cast<char>(c));
                    }
                }
                if(!player_skins->initialize(initial))throw std::runtime_error("Cannot initialize original Toni skin");
                log("Original Toni skin active: "+initial+"; 16 outfits, reserved normal ped slots 297/298; player.img unchanged");
            }
            if(setting(L"GameplaySkins")==L"1") {
                gameplay_skins=std::make_unique<lcs::SaGameplaySkins>(std::filesystem::path(directory));
                log("Gameplay skins registered in existing native slots="+std::to_string(gameplay_skins->count())+"; converted ped clumps use scoped CULLNONE");
            }
            if(setting(L"OriginalMapRenderRules")==L"1") {
                world_preview=std::make_unique<lcs::SaWorldPreview>(std::filesystem::path(directory));
                log("Original island/atomic LOD ranges active; source physical buildings use normal collision sectors; CULLNONE at converted atomic draw callbacks (including road pass)");
            }
            if(setting(L"Mode")==L"Campaign" || setting(L"Mode")==L"FirstMission") {
                const auto data=setting(L"DataDirectory");if(data.empty())throw std::runtime_error("DataDirectory is not configured");
                campaign=std::make_unique<Campaign>(std::filesystem::path(data),setting(L"Mode")==L"FirstMission",campaign_load_requires_restore);
                if(campaign_load_requires_restore) {
                    uint32_t now=0;if(!read_game_word(0xB7CB84,&now))throw std::runtime_error("Cannot read loaded game timer");
                    campaign->restore_save(*pending_campaign_load,now);pending_campaign_load.reset();campaign_load_requires_restore=false;
                }
                log("Original LCS runtime started on the SA game thread; unsupported commands will block explicitly.");
            } else log("Mode=WorldPreview; native map bootstrap is active. Campaign is still under development.");
            read_game_word(0xB7CB84,&last_game_time);
        }catch(const std::exception& e){world_failure=true;campaign.reset();car_reward_cleanup_pending=!lcs::sa_release_car_rewards();log(std::string("Native world initialization failed: ")+e.what());}
    }
    if(campaign && !lcs::sa_lcs_bridge_tick())throw std::runtime_error("Cannot update owned LCS lift bridge entities/path links");
    if(world_preview) {
        if(!world_preview->service() || !world_preview->culling_healthy())throw std::runtime_error("Native map render policy/pool/culling mismatch");
        static bool draw_reported=false;
        if(!draw_reported && world_preview->double_sided_draws()) {
            log("Converted map double-sided atomic callbacks observed: "+std::to_string(world_preview->double_sided_draws()));
            draw_reported=true;
        }
    }
    if(player_skins && (!campaign || !campaign->adapter.has_active_cutscene())) {
        static std::string previous_skin_wait;
        if(!player_skins->service()) {
            const auto& reason=player_skins->recovery_diagnostic();
            if(reason!=previous_skin_wait){log("Player rebuild pending: "+reason);previous_skin_wait=reason;}
            // Native load/restart builds the player and streams its clump over
            // several phases. Do not abort the VM or tick it against that gap.
            return;
        }
        if(!previous_skin_wait.empty()){log("Committed Toni outfit restored after native player rebuild");previous_skin_wait.clear();}
    }
    if(gameplay_skins && !gameplay_skins->service())throw std::runtime_error("Gameplay skin pool/culling mismatch");
    if(world_failure || !campaign)return;
    if(!campaign->finish_restore())return;
    if(campaign->cutscenes)campaign->cutscenes->service();
    uint32_t now=0;if(!read_game_word(0xB7CB84,&now) || now==last_game_time)return;
    const auto elapsed=now-last_game_time;last_game_time=now;
    for(const auto& event:campaign->tick(now,elapsed)) {
        if(event.result!=lcs::StepResult::unsupported && event.result!=lcs::StepResult::failed)continue;
        std::ostringstream message;message<<"LCS thread="<<event.id<<" ip="<<event.ip<<" command="<<lcs::opcode_name(event.opcode)<<" diagnostic="<<event.diagnostic;log(message.str());
    }
}
void observe_bootstrap_stage() {
    uint32_t name0=0,name1=0,stage=0,ready=0;
    if(!read_game_word(0xA49960+616,&name0) || name0!=0x4C0903A4 ||
       !read_game_word(0xA49960+620,&name1) || name1!=0x41535343) {
        last_bootstrap_stage=UINT32_MAX;return;
    }
    if(!read_game_word(0xA49960+20,&stage) || !read_game_word(0xA49960+16,&ready))return;
    if(ready==0x4C435357)stage=4;
    if(stage>4 || stage==last_bootstrap_stage)return;
    static const char* descriptions[]={"bootstrap entry", "before native CREATE_PLAYER", "before native LOAD_SCENE", "before ground placement", "world bootstrap ready"};
    last_bootstrap_stage=stage;
    log(std::string("Bootstrap stage=")+std::to_string(stage)+" "+descriptions[stage]);
}
void sync_native_player_exports() {
    static int32_t previous=-1;
    const auto actor=lcs::sa_sync_native_player_exports();
    if(!actor){previous=-1;return;}
    if(previous!=*actor) {
        previous=*actor;
        log("Native mod player exports: $PLAYER_CHAR=0; $PLAYER_ACTOR="+std::to_string(*actor)+
            "; actual live player pool reference, refreshed before native/CLEO scripts");
    }
}
// The only installation point of the task observer. CGame::Initialise runs this first script pass (call site 0x53BCC9)
// on the thread that later runs every script and task routine; no ped, task, event or script command exists yet, and
// the game loop that could enter the hooked task routines has not started. That is the concrete exclusion the byte
// exchange needs (the write window is this call, never DllMain, never a frame callback, no thread suspension). One
// attempt per process: a refused or partial installation leaves the walking objectives unavailable, never guessed.
void install_task_observer_at_startup() {
    if(task_observer_attempted)return;
    task_observer_attempted=true;task_observer_thread=GetCurrentThreadId();
    std::optional<lcs::TaskHookSite> release;
    if(!retail_task_release_entry()) {
        const auto entry=lcs::sa_task_ola_release_entry();
        if(!entry || entry>UINT32_MAX){log("Task observer refused: the live task release entry is neither retail nor the verified adjuster");return;}
        release=lcs::TaskHookSite{uint32_t(entry),6,{{0x55,0x8B,0xEC,0xFF,0x75,0x08}}};
    }
    if(!task_observer)task_observer=new lcs::TaskObserverHost(task_hook_memory,&task_observer_profile,&task_observer_on_thread,&task_observer_excluded,&task_observer_task_live);
    task_observer_window=true;
    const bool installed=task_observer->install(release);
    if(installed)lcs::task_memory_original_prefix=&own_task_original_prefix;
    task_observer_window=false;
    log(installed?"Task observer installed on the retail/adjuster task entries before startup scripts":"Task observer refused or failed; walking objective completion stays unavailable");
    if(installed)log(lcs::sa_task_allocator_profile_matches() && lcs::sa_task_release_profile_matches()
        ?"Observed task allocator/release profiles verified through owned continuations"
        :"Task observer installed but task allocator/release compatibility verification failed");
}
void __cdecl on_startup_scripts() {
    try {
        install_task_observer_at_startup();
    } catch (...) { OutputDebugStringA("LcsSaBridge: task observer startup failure\n"); }
    try {
        prepare_initial_car_reward_hooks();
        log("ASI entered initial script processing. Hook=0x53BCC9 -> CTheScripts::Process; camera update follows this call.");
        observe_bootstrap_stage();
    } catch (...) { OutputDebugStringA("LcsSaBridge: startup diagnostic failure\n"); }
    original_startup_process();
    try {
        sync_native_player_exports();
        observe_bootstrap_stage();
        log("Initial script processing returned to CGame::Initialise.");
    } catch (...) { OutputDebugStringA("LcsSaBridge: startup diagnostic failure\n"); }
}
void __cdecl on_game_process() {
    // Record the pending operation before entering native scripts. This message
    // survives a synchronous streaming freeze and needs no watchdog or UI control.
    try {
        if(first_frame)log("ASI entered native script loop. Hook=0x53BFC7 -> CTheScripts::Process.");
        if(first_frame)prepare_initial_car_reward_hooks();
        // A restart can replace the player and its handle generation. Mods
        // consume these globals inside original_process, before observe_world.
        sync_native_player_exports();
        observe_bootstrap_stage();
    } catch (...) { OutputDebugStringA("LcsSaBridge: early diagnostic failure\n"); }
    original_process();
    ++frames;
    // Every operation that reads files or creates C++ objects runs on the game thread,
    // after the original process call, outside the Windows loader lock.
    try {
        if (first_frame) {
            first_frame=false;
            preflight();
        }
        if (frames==120) log("Native SA loop reached 120 frames with the bridge loaded.");
        observe_world();
        observe_bootstrap_stage();
        const bool down=(GetAsyncKeyState(VK_F9)&0x8000)!=0;
        if (down && !f9_down) preflight();
        f9_down=down;
    } catch (const std::exception& e) {
        log(std::string("LCS runtime fatal error: ")+e.what());
        if(campaign)campaign->abort();
        world_failure=true;
    } catch (...) {
        log("LCS runtime fatal error: unknown exception");
        if(campaign)campaign->abort();
        world_failure=true;
    }
}
void __cdecl on_phone_phase() {
    original_phone_phase(); // Preserve the previous shadows/plugin call first.
    // Unconfirmed SA FX stops are retried every rendered frame, also after abort/halt or once the
    // campaign object is gone: this hook outlives both.
    lcs::sa_fx_service_pending();
    if(!campaign || campaign->halted || world_failure)return;
    try {
        campaign->adapter.tick_phones();
        // Owned LCS particle emitters advance once per rendered game frame, never while game time is frozen.
        static uint32_t fx_frame=0;
        const auto clock=lcs::sa_frame_clock();
        if(clock && clock->frame!=fx_frame) {
            fx_frame=clock->frame;
            if(clock->now!=clock->previous)campaign->adapter.fx.tick(clock->now);
        }
    }
    catch(const std::exception& e){log(std::string("Original phone phase fatal: ")+e.what());campaign->abort();world_failure=true;}
    catch(...){log("Original phone phase fatal: unknown exception");campaign->abort();world_failure=true;}
}
// Retail audio frame producer for 402 objects: AudioManager constants TIME_SPENT=30 and
// SPEED_OF_SOUND/TIME_SPENT, SA's live pause/time-scale/camera-switch/effects gain, the camera as
// listener and the shared source mix (phone ducking recovery runs once per audio service).
void service_continuous_sound() {
    if(!campaign || campaign->halted || world_failure)return;
    uint32_t now=0;if(!read_game_word(0xB7CB84,&now))return;
    campaign->frame_updates.run("continuous-sound",lcs::UpdateScope::auxiliary,now,
        "Continuous script sound service unavailable",[](){
            if(campaign->continuous_sounds.quarantined() && !campaign->continuous_sounds.recover())return false;
            if(!campaign->continuous_runtime.recover())return false;
            const auto settings=continuous_native_settings();
            const auto listener=lcs::sa_audio_listener();
            if(!settings || !listener)return false;
            lcs::SaContinuousSoundFrame frame{*listener,30,343.0f/30.0f,settings->time_scale,
                settings->camera_switched,settings->paused,campaign->audio.service_source_mix(settings->effects_scale)};
            return campaign->continuous_runtime.service_frame(frame);
        });
}
void service_original_finale() {
    ++original_audio_frame; // Source AudioManager increments once per audio service, including pause.
    lcs::sa_fx_service_pending(); // also runs in the paused frontend and after abort/halt
    if(!campaign || campaign->halted || world_failure)return;
    service_continuous_sound();
    campaign->frame_updates.run("city-music",lcs::UpdateScope::auxiliary,original_audio_frame,
        "Original CITY music service unavailable",[](){
            const auto settings=ending_tune_settings();
            return settings && campaign->service_city_music(settings->paused,settings->music_scale);
        });
    campaign->frame_updates.run("ending-tune",lcs::UpdateScope::auxiliary,original_audio_frame,
        "Original finale audio service unavailable",[](){
            const auto settings=ending_tune_settings();if(!settings)return false;
            const auto result=campaign->finale.service(original_audio_frame,settings->paused,settings->music_scale);
            return result==lcs::EndingTuneResult::accepted;
        });
}
void __fastcall on_audio_main(void* self,void*) {
    reinterpret_cast<void(__thiscall*)(void*)>(original_audio_main)(self);
    service_original_finale();
}
void __fastcall on_audio_menu(void* self,void*) {
    reinterpret_cast<void(__thiscall*)(void*)>(original_audio_menu)(self);
    service_original_finale();
}
bool install_call(uintptr_t address,ScriptProcess callback,ScriptProcess& original,
                  bool(*prepare)(uintptr_t,uintptr_t)=nullptr) {
    if(original)return true;
    auto code=reinterpret_cast<uint8_t*>(address);
    if(code[0]!=0xE8)return false;
    int32_t previous_relative=0;std::memcpy(&previous_relative,code+1,4);
    const auto previous_target=uintptr_t(int64_t(address+5)+previous_relative);
    if(previous_target==reinterpret_cast<uintptr_t>(callback))return false;
    MEMORY_BASIC_INFORMATION target_info{};
    if(!VirtualQuery(reinterpret_cast<void*>(previous_target),&target_info,sizeof(target_info)) ||
       target_info.State!=MEM_COMMIT || !(target_info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)) ||
       (target_info.Protect&PAGE_GUARD))return false;
    // The door adapters must know the chained thiscall target before the CALL
    // can reach their fastcall boundary. Other existing hooks need no prepare.
    if(prepare && !prepare(address,previous_target))return false;
    DWORD old_protection;
    if (!VirtualProtect(code,5,PAGE_EXECUTE_READWRITE,&old_protection)) return false;
    // Preserve a previously installed plugin's CALL target. Calling the fixed
    // original function would otherwise bypass that plugin's game-loop work.
    original=reinterpret_cast<ScriptProcess>(previous_target);
    const int32_t relative=int32_t(reinterpret_cast<uintptr_t>(callback)-(address+5));
    std::memcpy(code+1,&relative,4);
    FlushInstructionCache(GetCurrentProcess(),code,5);
    DWORD ignored; VirtualProtect(code,5,old_protection,&ignored);
    return true;
}
bool install_remote_door_policy() {
    static std::array<ScriptProcess,53> originals{};
    size_t count=0;
    const auto sites=lcs::sa_remote_door_policy_call_sites(&count);
    if(!sites || count!=originals.size())return false;
    HMODULE resident=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&lcs::sa_remote_door_policy_callback_for_site),&resident))return false;
    for(size_t i=0;i<count;++i) {
        const auto callback=reinterpret_cast<ScriptProcess>(lcs::sa_remote_door_policy_callback_for_site(sites[i].address));
        if(!callback || !install_call(sites[i].address,callback,originals[i],lcs::sa_remote_door_policy_bind_callsite))return false;
    }
    return lcs::sa_remote_door_policy_calls_healthy();
}
bool install_sniper_query() {
    static std::array<ScriptProcess,2> originals{};
    size_t count=0;const auto sites=lcs::sa_sniper_query_call_sites(&count);
    if(!sites || count!=originals.size() || !lcs::sa_sniper_query_profile_matches())return false;
    HMODULE resident=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&lcs::sa_sniper_query_callback_for_site),&resident))return false;
    for(size_t i=0;i<count;++i) {
        const auto callback=reinterpret_cast<ScriptProcess>(lcs::sa_sniper_query_callback_for_site(sites[i]));
        if(!callback || !install_call(sites[i],callback,originals[i],lcs::sa_sniper_query_bind_callsite))return false;
    }
    return lcs::sa_sniper_query_calls_healthy();
}
void __cdecl on_mission_hud(){lcs::sa_hud_draw_owned(original_mission_hud);}
void __cdecl on_credits_hud(){
    original_credits_hud();
    if(!campaign || campaign->halted || campaign->credits.finished())return;
    uint32_t now=0;if(!read_game_word(0xB7CB84,&now))return;
    campaign->frame_updates.run("credits",lcs::UpdateScope::dependency,now,"Original credits renderer/input unavailable",[&](){
        uint32_t frame=0,width=0,height=0,menu=0;
        const auto* text=campaign->credits_language();
        if(!text || !lcs::sa_hud_render_ready() || !read_game_word(0xB7CB4C,&frame) ||
           !read_game_word(0xC17044,&width) || !read_game_word(0xC17048,&height) || !read_game_word(0xBA67A4,&menu))return false;
        bool skip=false,accelerated=false;
        if(!(menu&0xFF) && !credits_inputs(&skip,&accelerated))return false;
        return campaign->credits.draw(*text,frame,int(width),int(height),(menu&0xFF)!=0,skip,accelerated);
    });
}
void __cdecl on_marker_phase(){
    original_marker_phase();
    if(!campaign || campaign->halted)return;
    bool live=false;for(const auto& entry:campaign->marker_pool.entries())live|=entry.live;
    if(!live)return;
    const auto now=*reinterpret_cast<const uint32_t*>(0xB7CB84);
    campaign->frame_updates.run("script-markers",lcs::UpdateScope::dependency,now,
        "Original private marker renderer failed",[&](){return lcs::sa_script_marker_frame(campaign->marker_pool);});
}
void __cdecl on_marker_shutdown(){
    if(campaign)campaign->marker_pool.clear();
    if(!lcs::sa_script_marker_resources_release())log("Private marker shutdown refused; uncertain native resources quarantined");
    original_marker_shutdown();
}
void __cdecl on_world_shutdown(){
    // Native pool tags can repeat after LOAD/new game. Retire receipts at the
    // actual world boundary, before native code destroys any sector members.
    retire_campaign_world();
    lcs::sa_world_sector_world_retiring();
    // Exit crash evidence: report any sector member that ShutDown would
    // dispatch after its destruction. Read-only; native teardown is unchanged.
    try{for(const auto& line:lcs::sa_world_membership_audit())log(line);}catch(...){}
    original_world_shutdown();
}
void __cdecl on_world_restart(){
    // CMenuManager::m_bLoadingData (sdk m_bWantToLoad), offset 0x60.
    // It is still set here; InitialiseWhenRestarting clears it before GenericLoad.
    const bool loading=*reinterpret_cast<const uint8_t*>(0xBA6748+0x60)!=0;
    retire_campaign_world();
    campaign_load_requires_restore=loading;
    pending_campaign_load.reset();campaign_load_diagnostic.clear();pending_campaign_save.reset();
    lcs::sa_world_sector_world_retiring();
    original_world_restart();
}
bool __cdecl campaign_before_save(uint8_t slot,const char* native_path,void*) {
    pending_campaign_save.reset();pending_native_save.clear();
    if(setting(L"Mode")!=L"Campaign")return !campaign;
    try {
        if(!campaign || world_failure)throw std::runtime_error("Campaign is not ready to save");
        uint32_t now=0;if(!read_game_word(0xB7CB84,&now))throw std::runtime_error("Cannot read save timer");
        pending_native_save=std::filesystem::path(native_path);if(!pending_native_save.is_absolute())throw std::runtime_error("Native save path is not absolute");
        pending_campaign_save=campaign->capture_save(now);
        log("Campaign SAVE captured slot="+std::to_string(unsigned(slot)+1));return true;
    }catch(const std::exception& e){log(std::string("Campaign SAVE refused before native write: ")+e.what()+"; nativePath="+pending_native_save.u8string());return false;}
}
bool __cdecl campaign_saved(uint8_t slot,const char* native_path,bool native_saved,void*) {
    if(!native_saved){log(std::string("Campaign SAVE native writer failed: ")+(native_path?native_path:"<missing path>"));pending_campaign_save.reset();return false;}
    if(!pending_campaign_save)return setting(L"Mode")!=L"Campaign";
    try {
        const auto path=std::filesystem::path(native_path);if(path!=pending_native_save)throw std::runtime_error("Native save path changed during capture");
        const auto data=pending_campaign_save->encode(lcs::native_save_digest(path));
        const auto companion=lcs::campaign_checkpoint_path(path);auto temporary=companion;temporary+=L".tmp";
        {std::ofstream file(temporary,std::ios::binary|std::ios::trunc);if(!file || !file.write(reinterpret_cast<const char*>(data.data()),data.size()) || !file.flush())throw std::runtime_error("Cannot write LCS save companion");}
        if(!MoveFileExW(temporary.c_str(),companion.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot commit LCS save companion");
        pending_campaign_save.reset();log("Campaign SAVE committed slot="+std::to_string(unsigned(slot)+1));return true;
    }catch(const std::exception& e){pending_campaign_save.reset();log(std::string("Campaign SAVE companion failed: ")+e.what());return false;}
}
bool __cdecl campaign_before_load(uint8_t slot,const char* native_path,void*) {
    if(setting(L"Mode")!=L"Campaign")return true;
    campaign_load_requires_restore=true;pending_campaign_load.reset();campaign_load_diagnostic.clear();
    try {pending_campaign_load=lcs::read_campaign_checkpoint(std::filesystem::path(native_path));log("Campaign LOAD companion verified slot="+std::to_string(unsigned(slot)+1));return true;}
    catch(const std::exception& e){campaign_load_diagnostic=e.what();log("Campaign LOAD companion unavailable: "+campaign_load_diagnostic);return false;}
}
void __cdecl campaign_loaded(uint8_t,const char*,bool sidecar_ready,bool native_loaded,bool,void*) {
    if(setting(L"Mode")!=L"Campaign")return;
    campaign_load_requires_restore=true;
    if(!sidecar_ready){pending_campaign_load.reset();if(campaign_load_diagnostic.empty())campaign_load_diagnostic="Native save path/slot could not be verified";}
    if(!native_loaded){pending_campaign_load.reset();campaign_load_diagnostic="Native SA save load did not complete";}
}
ScriptProcess original_campaign_save=nullptr,original_campaign_load=nullptr;
bool bind_campaign_target(uintptr_t site,uintptr_t target) {
    using Profile=lcs::SACampaignSaveProfile;
    lcs::sa_campaign_save_bind_native_targets(
        reinterpret_cast<lcs::SACampaignNativeSaveWriter>(site==Profile::save_writer_call?target:reinterpret_cast<uintptr_t>(original_campaign_save)),
        reinterpret_cast<lcs::SACampaignNativeGenericLoad>(site==Profile::generic_load_call?target:reinterpret_cast<uintptr_t>(original_campaign_load)));
    return true;
}
bool install_campaign_storage() {
    if(!lcs::SACampaignSaveProfile::matches())return false;
    lcs::sa_campaign_save_bind_callbacks({&campaign_before_save,&campaign_saved,&campaign_before_load,&campaign_loaded,nullptr});
    const bool save=install_call(lcs::SACampaignSaveProfile::save_writer_call,reinterpret_cast<ScriptProcess>(&lcs::sa_campaign_save_writer_call),original_campaign_save,&bind_campaign_target);
    const bool load=install_call(lcs::SACampaignSaveProfile::generic_load_call,reinterpret_cast<ScriptProcess>(&lcs::sa_campaign_generic_load_call),original_campaign_load,&bind_campaign_target);
    return save && load && lcs::sa_campaign_save_hooks_ready();
}
bool install_hook() {
    if(original_process && original_startup_process && original_phone_phase && original_audio_main && original_audio_menu && original_mission_hud && original_marker_phase && original_marker_shutdown && original_world_shutdown && original_world_restart && lcs::sa_remote_door_policy_calls_healthy() && lcs::sa_sniper_query_calls_healthy() && lcs::sa_campaign_save_hooks_ready())return true;
    const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if(base!=0x400000)return false;
    const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<=0 || dos->e_lfanew>4096)return false;
    const auto pe=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    if(pe->Signature!=IMAGE_NT_SIGNATURE || pe->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 ||
       pe->OptionalHeader.SizeOfImage<=process_call-base+5 ||
       pe->OptionalHeader.SizeOfImage<=startup_process_call-base+5 ||
       pe->OptionalHeader.SizeOfImage<=0x53C158-base ||
       !lcs::sa_executor_profile_matches()) {
        OutputDebugStringA("LcsSaBridge: unsupported native script/profile instruction; hooks skipped\n");
        return false;
    }
    if(setting(L"VehicleColours")==L"1") {
        try {lcs::sa_prepare_vehicle_colours(resource_directory()/L"data/lcs/vehicle-colours.dat");}
        catch(const std::exception& error) {
            log(std::string("Vehicle colour profile refused before native startup: ")+error.what());
            return false;
        }
    }
    const bool storage=install_campaign_storage();
    const bool startup=install_call(startup_process_call,&on_startup_scripts,original_startup_process);
    const bool loop=install_call(process_call,&on_game_process,original_process);
    const bool world_shutdown=install_call(0x53C9BC,&on_world_shutdown,original_world_shutdown);
    const bool world_restart=install_call(0x53C5CB,&on_world_restart,original_world_restart);
    // CALL after world/camera/permanent shadows: source phone update phase.
    // install_call chains an existing plugin target rather than bypassing it.
    const bool phase=lcs::original_phone_phase_profile([](uintptr_t address,std::initializer_list<uint8_t> bytes){
        return std::memcmp(reinterpret_cast<const void*>(address),bytes.begin(),bytes.size())==0;
    });
    const bool phones=phase && install_call(0x53C140,&on_phone_phase,original_phone_phase);
    const bool audio_phase=lcs::original_ending_tune_phase_profile([](uintptr_t address,std::initializer_list<uint8_t> bytes){
        return std::memcmp(reinterpret_cast<const void*>(address),bytes.begin(),bytes.size())==0;
    });
    const bool audio_main=audio_phase && install_call(0x53E98B,reinterpret_cast<ScriptProcess>(&on_audio_main),original_audio_main);
    const bool audio_menu=audio_phase && install_call(0x53E7BC,reinterpret_cast<ScriptProcess>(&on_audio_menu),original_audio_menu);
    const bool mission_hud=lcs::sa_hud_render_profile() && install_call(0x58FBEE,&on_mission_hud,original_mission_hud);
    lcs::sa_hud_render_bound(mission_hud);
    // Wrap the parent HUD call: DrawMissionTimers is skipped in widescreen,
    // while the source credits roll must still reach the shared font flush.
    if(lcs::sa_credits_render_profile())install_call(0x53E4FF,&on_credits_hud,original_credits_hud);
    const bool marker_profile=lcs::sa_script_marker_phase_profile();
    const bool marker_frame=marker_profile && install_call(0x725067,&on_marker_phase,original_marker_phase);
    const bool marker_shutdown=marker_profile && install_call(0x723391,&on_marker_shutdown,original_marker_shutdown);
    lcs::sa_script_marker_render_bound(marker_frame?reinterpret_cast<uintptr_t>(&on_marker_phase):0,
        marker_shutdown?reinterpret_cast<uintptr_t>(&on_marker_shutdown):0);
    // Optional until a ground vehicle receives the source policy. Install only
    // here at startup, preserving every pre-existing target and its thiscall ABI.
    // A partial installation merely delegates; the command requires all sites.
    install_remote_door_policy();
    install_sniper_query();
    // These optional bindings do not gate unrelated campaign/HUD commands.
    // Marker availability still checks actual CALL ownership, native resources
    // and queries; optional hook failure cannot admit a fake rendered marker.
    return storage && startup && loop && phones && audio_main && audio_menu && mission_hud && world_shutdown && world_restart;
}
}
// Supported by Ultimate ASI Loader. Also installed during ordinary ASI LoadLibrary.
extern "C" __declspec(dllexport) void InitializeASI() { install_hook(); }
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if (reason==DLL_PROCESS_ATTACH) {
        module=instance;
        DisableThreadLibraryCalls(instance);
        // Checked CALL/colour-table references are installed here. No workers or game
        // initialization are started while the loader holds its lock.
        install_hook();
    }
    return TRUE;
}
