// Reuse the existing conditional-world fixture, not another world mock implementation.
#define main existing_world_sector_regression_main
#include "world_sector_tests.cpp"
#undef main
#include "../src/campaign_services_save.hpp"
#include "ScriptCommands.h"
int main(int argc,char** argv){try{
    check(argc==2,"Pass the retained mounted world fixture");
    const auto bindings=read_bindings(argv[1]);World world;auto engine=world.callbacks();
    lcs::SaWorldSectors before(argv[1],bindings,engine),after(argv[1],bindings,engine);
    lcs::HudBridge hud,loaded_hud;lcs::Collectives collectives,loaded_collectives;lcs::RouteScripts routes,loaded_routes;
    lcs::ScriptCoronas coronas,loaded_coronas;lcs::ContinuousSoundAdapter sounds,loaded_sounds;lcs::FxParticles fx,loaded_fx;
    std::vector<uint8_t> memory(128);auto word=[&](uint16_t v){memory.push_back(uint8_t(v));memory.push_back(uint8_t(v>>8));};
    word(COMMAND_DISPLAY_ONSCREEN_COUNTER);memory.push_back(204);memory.push_back(2);memory.push_back(6);for(int i=0;i<4;++i)memory.push_back(0);
    lcs::Vm vm(memory,128,uint32_t(memory.size()));int32_t counter=37;std::memcpy(vm.memory.data()+8,&counter,4);
    hud.update=[](const lcs::HudRequest&){return true;};hud.read_cell=[](uint32_t)->std::optional<int32_t>{return 37;};hud.write_cell=[](uint32_t,int32_t){return true;};
    lcs::Thread thread;thread.ip=130;check(hud.dispatch(vm,thread,COMMAND_DISPLAY_ONSCREEN_COUNTER)==lcs::StepResult::continued,"counter setup");
    std::vector<lcs::HudRequest> requests;loaded_hud.update=[&](const lcs::HudRequest& q){requests.push_back(q);return true;};loaded_hud.read_cell=hud.read_cell;loaded_hud.write_cell=hud.write_cell;
    fx.callbacks.emit=[](const lcs::FxParticleRequest&){return lcs::FxStatus{true,false,{}};};fx.callbacks.camera=[]()->std::optional<lcs::FxVec3>{return lcs::FxVec3{};};
    fx.callbacks.fire_start=[](const lcs::FxVec3&,float,lcs::FxStatus& status){status.ok=true;return 12u;};
    fx.callbacks.fire_stop=[](uint32_t){return true;};bool added=false;
    check(fx.add_moving(lcs::FxParticles::object_small_fire,{1,2,3},{0,0,0},1,{255,0,0},1000,100,added).ok && added,"source fire setup");
    size_t fires=0;loaded_fx.callbacks.fire_start=[&](const lcs::FxVec3& position,float,lcs::FxStatus& status){check(position==lcs::FxVec3{1,2,3},"saved fire position");++fires;status.ok=true;return 98u;};loaded_fx.callbacks.fire_stop=[](uint32_t){return true;};
    const auto bytes=lcs::CampaignServicesSave::save(hud,collectives,routes,coronas,sounds,fx,before,100);
    lcs::CampaignServicesSave::restore(bytes,loaded_hud,loaded_collectives,loaded_routes,loaded_coronas,loaded_sounds,loaded_fx,after,vm,5000);
    check(fires==1 && loaded_fx.live()==1,"fire must receive a new owned native system");
    check(requests.size()==1 && requests[0].action==lcs::HudRequest::Action::add_counter,"HUD widget restored through renderer");
    check(lcs::CampaignServicesSave::save(loaded_hud,loaded_collectives,loaded_routes,loaded_coronas,loaded_sounds,loaded_fx,after,5000)==bytes,"service state/timers roundtrip");

    // v1 carried committed sector states only. The v2 writer adds separate
    // staged intent and a request/plan receipt; dropping those final words
    // creates the exact old empty-state layout for compatibility coverage.
    lcs::HudBridge empty_hud,empty_loaded_hud;lcs::Collectives empty_collectives,empty_loaded_collectives;
    lcs::RouteScripts empty_routes,empty_loaded_routes;lcs::ScriptCoronas empty_coronas,empty_loaded_coronas;
    lcs::ContinuousSoundAdapter empty_sounds,empty_loaded_sounds;lcs::FxParticles empty_fx,empty_loaded_fx;
    World legacy_world;auto legacy_engine=legacy_world.callbacks();
    lcs::SaWorldSectors legacy_sectors(argv[1],bindings,legacy_engine);
    auto legacy_v1=lcs::CampaignServicesSave::save(empty_hud,empty_collectives,empty_routes,empty_coronas,empty_sounds,empty_fx,legacy_sectors,50);
    check(legacy_v1.size()>=8,"v2 service image has its committed/staged world tail");
    legacy_v1.resize(legacy_v1.size()-8);legacy_v1[0]=1;legacy_v1[1]=legacy_v1[2]=legacy_v1[3]=0;
    World legacy_loaded_world;auto legacy_loaded_engine=legacy_loaded_world.callbacks();
    lcs::SaWorldSectors legacy_loaded_sectors(argv[1],bindings,legacy_loaded_engine);
    const auto old_pending=lcs::CampaignServicesSave::restore(legacy_v1,empty_loaded_hud,empty_loaded_collectives,empty_loaded_routes,
        empty_loaded_coronas,empty_loaded_sounds,empty_loaded_fx,legacy_loaded_sectors,vm,5050);
    check(old_pending.states.empty()&&!old_pending.requested,"v1 service images restore with no staged-plan receipt");

    // Capture a settled state plus a new staged state after request() created
    // a still-pending plan. The snapshot is pure: it cannot commit the source
    // world or collapse the source's committed/staged boundary.
    World staged_world;auto staged_engine=staged_world.callbacks();
    lcs::SaWorldSectors staged_source(argv[1],bindings,staged_engine);
    check(staged_source.set(12,0).executed,"Initial group state stages");
    const auto base_request=staged_source.request();check(base_request.executed&&base_request.condition,"Initial committed-world request is ready");
    const auto base_complete=staged_source.complete();check(base_complete&&*base_complete,"Initial committed-world plan completes");
    check(staged_source.commit().executed,"Initial committed-world state commits");
    check(staged_source.set(12,1).executed,"Changed sector state remains staged");
    const auto pending_request=staged_source.request();check(pending_request.executed&&!pending_request.condition,"Changed sector request remains pending on model residency");
    const auto alternate_model=models_by_asset(bindings).at("whouse_int_5bd38e");
    check(staged_world.requested.count(alternate_model),"Pending plan requests its alternate world model");
    const auto pending_bytes=lcs::CampaignServicesSave::save(empty_hud,empty_collectives,empty_routes,empty_coronas,empty_sounds,empty_fx,staged_source,200);
    check(pending_bytes[0]==2&&pending_bytes[1]==0,"World transition state uses service format v2");
    check(staged_world.building_changes==0&&staged_world.object_changes==0,"Save capture must not commit or mutate the staged live world");
    check(lcs::CampaignServicesSave::save(empty_hud,empty_collectives,empty_routes,empty_coronas,empty_sounds,empty_fx,staged_source,200)==pending_bytes,
          "Repeated pending capture preserves the original committed/staged plan state");

    World restored_world;auto restored_engine=restored_world.callbacks();
    lcs::SaWorldSectors restored_sectors(argv[1],bindings,restored_engine);
    check(restored_sectors.set(12,2).executed&&restored_sectors.set(26,1).executed,"Adapter restore may stage a conflicting world intent first");
    const auto pending_world=lcs::CampaignServicesSave::restore(pending_bytes,empty_loaded_hud,empty_loaded_collectives,empty_loaded_routes,
        empty_loaded_coronas,empty_loaded_sounds,empty_loaded_fx,restored_sectors,vm,6000);
    check(pending_world.states==std::map<int32_t,int32_t>{{12,1}}&&pending_world.requested,
          "Load returns the staged target and preserves whether the source plan had already been requested");
    const auto committed_request=restored_sectors.request();check(committed_request.executed&&committed_request.condition,"Load first requests only the archived committed sector ledger");
    const auto committed_complete=restored_sectors.complete();check(committed_complete&&*committed_complete,"Archived committed sector plan completes before pending intent");
    check(restored_sectors.commit().executed,"Archived committed sector ledger commits through the real world transaction");
    check(restored_world.requested.empty()&&restored_world.object_changes==0,
          "Adapter's conflicting staged groups must not leak into the first committed-world transaction");
    lcs::CampaignServicesSave::apply_pending(pending_world,restored_sectors);
    check(restored_world.requested.count(alternate_model)&&restored_world.object_changes==0,
          "Saved staged intent is reapplied and requested without an early commit");
    restored_world.ready=restored_world.requested;
    const auto resumed_request=restored_sectors.request();check(resumed_request.executed&&resumed_request.condition,"Pending request resumes after model residency");
    const auto resumed_complete=restored_sectors.complete();check(resumed_complete&&*resumed_complete,"Pending world entities prepare after residency");
    check(restored_sectors.commit().executed,"Resumed script transition commits through the real world transaction");
    check(std::any_of(restored_world.objects.begin(),restored_world.objects.end(),[&](const auto& item){return item.second.ref.model==alternate_model&&item.second.active;}),
          "The staged world target becomes active only at the resumed commit boundary");

    // An uncertain sound mutation is a distinct save refusal; it must not be
    // conflated with retired-fire cleanup or ordinary world plans.
    auto append_parameter=[](std::vector<uint8_t>& code,int32_t value){code.push_back(6);const auto bits=uint32_t(value);for(size_t i=0;i<4;++i)code.push_back(uint8_t(bits>>(8*i)));};
    auto float_bits=[](float value){int32_t bits=0;std::memcpy(&bits,&value,sizeof(bits));return bits;};
    std::vector<uint8_t> sound_code(64);append_parameter(sound_code,float_bits(1));append_parameter(sound_code,float_bits(2));append_parameter(sound_code,float_bits(3));append_parameter(sound_code,4);sound_code.push_back(12);
    lcs::Vm sound_vm(sound_code,64,uint32_t(sound_code.size()));lcs::Thread sound_thread;sound_thread.ip=64;
    lcs::ContinuousSoundAdapter quarantined_sounds;quarantined_sounds.available=[](){return true;};quarantined_sounds.music_available=[](){return true;};
    quarantined_sounds.create=[](const lcs::ContinuousSoundObjects::Snapshot&){return lcs::ContinuousEntityReceipt{true,false,-5};};
    quarantined_sounds.release=[](const lcs::ContinuousSoundObjects::Snapshot&){return lcs::ContinuousEntityReceipt{true,false,-5};};
    check(quarantined_sounds.dispatch(sound_vm,sound_thread,402)==lcs::StepResult::unsupported&&quarantined_sounds.quarantined(),"Entered continuous-sound failure retains its quarantine receipt");
    std::string sound_error;try{(void)lcs::CampaignServicesSave::save(empty_hud,empty_collectives,empty_routes,empty_coronas,quarantined_sounds,empty_fx,legacy_sectors,75);}catch(const std::exception& e){sound_error=e.what();}
    check(sound_error.find("continuous sound")!=std::string::npos&&sound_error.find("quarantined")!=std::string::npos,
          "Sound uncertainty has a specific save diagnostic");
    std::string sound_restore_error;try{(void)lcs::CampaignServicesSave::restore(pending_bytes,empty_hud,empty_collectives,empty_routes,empty_coronas,
        quarantined_sounds,empty_fx,legacy_sectors,vm,75);}catch(const std::exception& e){sound_restore_error=e.what();}
    check(sound_restore_error.find("quarantined")!=std::string::npos&&quarantined_sounds.quarantined(),
          "Restore cannot replace or erase an unresolved live sound ownership receipt");

    lcs::FxParticles retired_fx;retired_fx.callbacks.emit=fx.callbacks.emit;retired_fx.callbacks.camera=fx.callbacks.camera;retired_fx.callbacks.fire_start=[](const lcs::FxVec3&,float,lcs::FxStatus& status){status.ok=true;return 44u;};
    retired_fx.callbacks.fire_stop=[](uint32_t){return false;};bool retired_added=false;
    check(retired_fx.add_moving(lcs::FxParticles::object_small_fire,{4,5,6},{0,0,0},1,{255,0,0},1000,100,retired_added).ok&&retired_added,"Retired-fire cleanup fixture starts a native fire");
    check(retired_fx.remove_in_area({3,4,5,5,6,7})==1&&retired_fx.pending_fire_stops()==1,"Refused fire stop remains session-owned cleanup state");
    const auto retired_bytes=lcs::CampaignServicesSave::save(empty_hud,empty_collectives,empty_routes,empty_coronas,empty_sounds,retired_fx,legacy_sectors,80);
    check(retired_fx.pending_fire_stops()==1,"Saving must not clear a live retired-fire cleanup receipt");
    lcs::CampaignServicesSave::restore(retired_bytes,empty_hud,empty_collectives,empty_routes,empty_coronas,empty_sounds,retired_fx,legacy_sectors,vm,5080);
    check(retired_fx.pending_fire_stops()==1,"Restore must retain native fire cleanup ownership from the current session");

    World broken_world;auto broken_engine=broken_world.callbacks();
    broken_engine.request_model=[](uint16_t){lcs::NativeResult failure;failure.executed=false;failure.native_entered=true;failure.diagnostic="test world-sector request fault";return failure;};
    lcs::SaWorldSectors broken_sectors(argv[1],bindings,broken_engine);
    check(broken_sectors.set(12,1).executed&&!broken_sectors.request().executed&&broken_sectors.faulted(),"World-sector fault fixture retains its failed request diagnostic");
    std::string sector_error;try{(void)lcs::CampaignServicesSave::save(empty_hud,empty_collectives,empty_routes,empty_coronas,empty_sounds,empty_fx,broken_sectors,100);}catch(const std::exception& e){sector_error=e.what();}
    check(sector_error.find("world sectors")!=std::string::npos&&sector_error.find("test world-sector request fault")!=std::string::npos,
          "Faulted world sector service refuses with its specific underlying diagnostic");

    fx.reset();loaded_fx.reset();std::cout<<"PASS: integrated service checkpoint, HUD rebuild, fire ownership and timer rebasing\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
