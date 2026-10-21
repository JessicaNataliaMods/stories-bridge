#include "lcs/native.hpp"
#include "lcs/reward_vehicle_receipts.hpp"
#include "lcs/phone_ring_policy.hpp"
#include "lcs/phone_frame_coordinator.hpp"
#include "ScriptCommands.h"
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <limits>

static void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
static int32_t bits(float f){int32_t v=0;std::memcpy(&v,&f,4);return v;}
static void argument(std::vector<uint8_t>& c,int32_t v){c.push_back(6);for(int i=0;i<4;++i)c.push_back(uint8_t(uint32_t(v)>>(8*i)));}
static lcs::Vm command(uint16_t op,std::initializer_list<int32_t> args,bool output=false){
    std::vector<uint8_t> c(64);c.push_back(uint8_t(op));c.push_back(uint8_t(op>>8));for(auto v:args)argument(c,v);if(output)c.push_back(12);
    return lcs::Vm(std::move(c),64,64);
}
int main(){try{
    {
        lcs::PhoneFrameCoordinator coordinator;lcs::PhoneRingFrame frame;std::string order;
        lcs::PhoneFrameServices services;
        services.preflight=[&](bool visual,bool audio){check(visual && audio,"Ring preflight includes all planned services");order+='P';return true;};
        services.random=[&](){order+='R';return lcs::PhoneFrameRandomReceipt{{true,true},1023};};
        services.write_up=[&](float up){check(up>1,"CPU jitter uses actual random receipt");order+='U';return lcs::PhoneEffectReceipt{true,true};};
        services.audio=[&](){order+='A';return lcs::PhoneEffectReceipt{true,true};};
        services.refresh=[&](){order+='F';return lcs::PhoneEffectReceipt{true,true};};
        check(coordinator.update(frame,services)==lcs::PhoneFrameResult::accepted && coordinator.visible() && order.empty(),"Visibility activation precedes effects by one frame");
        frame.frame=1;frame.now=1880;frame.previous=1879;
        check(coordinator.update(frame,services)==lcs::PhoneFrameResult::accepted && order=="PRUAF","Original frame order is preflight, actual RNG, CPU pose, audio queue, renderer");
        check(coordinator.update(frame,services)==lcs::PhoneFrameResult::accepted && order=="PRUAF","Completed frame cannot consume RNG or replay effects twice");
        frame.frame=2;frame.now=5640;frame.previous=5639;
        services.audio=[&](){order+='X';return lcs::PhoneEffectReceipt{true,false};};
        check(coordinator.update(frame,services)==lcs::PhoneFrameResult::quarantined && order=="PRUAFPRUX","Partial audio failure stops renderer and quarantines prior effects");
        ++frame.frame;const auto frozen=order;
        check(coordinator.update(frame,services)==lcs::PhoneFrameResult::quarantined && order==frozen,"Quarantined phone never retries side effects on later frame");
        lcs::PhoneFrameCoordinator deferred;frame={};check(deferred.update(frame,services)==lcs::PhoneFrameResult::accepted,"Second fixture activates without effects");
        frame.frame=1;frame.now=1880;frame.previous=1879;
        services.preflight=[](bool,bool){return false;};
        check(deferred.update(frame,services)==lcs::PhoneFrameResult::deferred && order==frozen && !deferred.quarantined(),"Unavailable preflight performs no random or mutation and can remain deferred");
        services.preflight=[](bool,bool){return true;};services.random=[]()->lcs::PhoneFrameRandomReceipt{throw std::runtime_error("entered RNG fault");};
        check(deferred.update(frame,services)==lcs::PhoneFrameResult::quarantined,"Unknown side-effect exception cannot permit replay");
    }
    {
        lcs::PhoneRingFrame f;f.visible=true;f.now=1880;f.previous=1879;
        auto effect=lcs::original_phone_ring_frame(f);
        check(effect && effect->ring && effect->needs_random && !effect->up_z,"Ring half-cycle requests genuine random sample, never a made-up jitter");
        f.random=1023;effect=lcs::original_phone_ring_frame(f);
        check(effect && effect->up_z==float(1023)/16000.f+1.f && !effect->needs_random,"Original modulo/divisor produce exact booth jitter");
        f.previous=1880;effect=lcs::original_phone_ring_frame(f);check(effect && !effect->ring,"Ring sound emits only on half-cycle transition");
        f.previous=1879;f.player_z=-5;effect=lcs::original_phone_ring_frame(f);check(effect && !effect->ring && effect->up_z,"Retail suppresses sound at player Z<=-5 without suppressing booth jitter");
        f.player_z=-4.99f;effect=lcs::original_phone_ring_frame(f);check(effect && effect->ring,"Retail sound height threshold is strict");
        f.player_z=0;
        f.now=3760;effect=lcs::original_phone_ring_frame(f);check(effect && effect->up_z==1.f && !effect->ring,"Silent half-cycle resets original Up.z");
        f.camera_x=100;effect=lcs::original_phone_ring_frame(f);check(effect && effect->visible,"Outer visibility boundary includes exactly100m");
        f.camera_x=101;effect=lcs::original_phone_ring_frame(f);check(effect && !effect->visible && effect->up_z==1.f,"Original visible frame applies effect before crossing outer boundary");
        f.visible=false;f.camera_x=80;effect=lcs::original_phone_ring_frame(f);check(effect && !effect->visible,"Retail activation requires strict inner80m boundary rather than reconstructed60m");
        f.camera_x=79;f.frame=1;effect=lcs::original_phone_ring_frame(f);check(effect && !effect->visible,"Staggered entity seed gate avoids checking every hidden booth each frame");
        f.entity_seed=15;effect=lcs::original_phone_ring_frame(f);check(effect && effect->visible && !effect->up_z && !effect->ring,"Activation waits until following frame for ring/visual effects");
        f.camera_x=std::numeric_limits<float>::quiet_NaN();check(!lcs::original_phone_ring_frame(f),"Unknown camera never invents phone visibility");
    }
    lcs::ScmImage image;lcs::NativeAdapter adapter(image);adapter.world_ready=true;
    auto attach=[&](lcs::Vm& vm){vm.backend=[&](lcs::Vm& v,lcs::Thread& t,uint16_t op){return adapter.dispatch(v,t,op);};};
    {
        lcs::ScmImage isolated_image;lcs::NativeAdapter crusher(isolated_image);crusher.world_ready=true;
        auto test=command(COMMAND_IS_CAR_CRUSHED,{0x2307});size_t queries=0;
        crusher.execute=[&](const lcs::NativeCall&){++queries;lcs::NativeResult out;out.executed=true;out.condition=true;return out;};
        test.backend=[&](lcs::Vm& v,lcs::Thread& th,uint16_t op){return crusher.dispatch(v,th,op);};
        lcs::Thread candidate;candidate.ip=64;candidate.condition=true;
        check(test.step(candidate,0)==lcs::StepResult::unsupported && candidate.ip==64 && candidate.condition && queries==0,
              "0334 remains unregistered until a genuine native crusher event producer is proved");
    }
    {
        auto play=command(COMMAND_PLAY_END_OF_GAME_TUNE,{});attach(play);lcs::Thread tune_thread;tune_thread.ip=64;
        check(play.step(tune_thread,0)==lcs::StepResult::unsupported,"Unbound finale refuses without fabricated success");
        std::vector<bool> requests;bool accepted=true,entered=false;
        adapter.ending_tune=[&](bool start){requests.push_back(start);lcs::NativeResult r;r.executed=accepted;r.native_entered=entered;return r;};
        tune_thread={};tune_thread.ip=64;check(play.step(tune_thread,0)==lcs::StepResult::continued && adapter.music_mode()==0,"Accepted actual finale request commits FRONTEND0");
        auto stop=command(COMMAND_STOP_END_OF_GAME_TUNE,{});attach(stop);tune_thread={};tune_thread.ip=64;
        accepted=false;check(stop.step(tune_thread,0)==lcs::StepResult::unsupported && adapter.music_mode()==0,"Rejected stop retains original mode");
        accepted=true;tune_thread={};tune_thread.ip=64;check(stop.step(tune_thread,0)==lcs::StepResult::continued && adapter.music_mode()==1,"Accepted actual stop request commits GAME1");
        accepted=false;entered=true;tune_thread={};tune_thread.ip=64;
        check(play.step(tune_thread,0)==lcs::StepResult::failed && adapter.music_mode()==1,"Entered finale failure is nonresumable and retains mode");
        adapter.ending_tune={};
    }
    std::istringstream placements("inst\n2000, booth, 0, 0, 0, 2, 0, 0, 0, 1, -1\n2000, booth, 0, 20, 0, 2, 0, 0, 0, 1, -1\nend\n");
    adapter.load_phone_placements(placements,2000);
    auto claim=command(COMMAND_GRAB_PHONE,{bits(1),bits(0)},true);attach(claim);lcs::Thread t;t.ip=64;
    check(claim.step(t,0)==lcs::StepResult::continued && t.locals[0]==0,"Nearest actual phone is claimed, yielding original handle zero");
    t={};t.ip=64;check(claim.step(t,0)==lcs::StepResult::continued && t.locals[0]==1,"Already claimed booth is excluded from the next search");
    auto phone_on=command(COMMAND_TURN_PHONE_ON,{0});attach(phone_on);t={};t.ip=64;
    check(phone_on.step(t,0)==lcs::StepResult::unsupported,"Phone ON cannot succeed without the real late phase provider");
    bool phone_phase=true;size_t phone_frames=0;
    adapter.phone_phase_available=[&](){return phone_phase;};
    adapter.update_phone_frame=[&](const std::array<float,3>& position,lcs::PhoneFrameCoordinator&){
        check(position==std::array<float,3>{0,0,2},"Enabled source handle retains actual claimed canonical placement");
        ++phone_frames;return lcs::PhoneFrameResult::accepted;};
    t={};t.ip=64;check(phone_on.step(t,0)==lcs::StepResult::continued && phone_frames==0,"Phone ON commits source state9 without executing late frame early");
    adapter.tick_phones();check(phone_frames==1,"Late phase updates only enabled claimed source phone");
    phone_phase=false;adapter.tick_phones();check(phone_frames==1,"Absent late phase cannot trigger an early replacement update");phone_phase=true;
    auto phone_off=command(COMMAND_TURN_PHONE_OFF,{0});attach(phone_off);t={};t.ip=64;
    check(phone_off.step(t,0)==lcs::StepResult::continued,"Original phone OFF removes message/state without replaying effects");
    adapter.tick_phones();check(phone_frames==1,"Phone OFF stops future ringing updates");
    t={};t.ip=64;t.locals[0]=91;check(claim.step(t,0)==lcs::StepResult::unsupported && t.ip==64 && t.locals[0]==91 && adapter.claimed_phone_count()==2,"Exhausted real phone pool cannot manufacture a handle");
    lcs::NativeAdapter pool(image);pool.world_ready=true;std::istringstream one("inst\n2000, booth, 0, 0, 0, 2\nend\n");pool.load_phone_placements(one,2000);
    auto invalid=command(COMMAND_GRAB_PHONE,{0,0});invalid.memory.push_back(1);invalid.backend=[&](lcs::Vm& v,lcs::Thread& thread,uint16_t op){return pool.dispatch(v,thread,op);};
    t={};t.ip=64;check(invalid.step(t,0)==lcs::StepResult::failed && !pool.claimed_phone_count(),"Malformed phone destination does not claim the booth");
    std::vector<lcs::NativeCall> calls;bool loaded=false;
    adapter.bind_model(10,10100,"closed");adapter.bind_model(11,10101,"open");
    adapter.execute=[&](const lcs::NativeCall& c){calls.push_back(c);lcs::NativeResult r;r.executed=true;r.condition=loaded;return r;};
    auto swap=command(COMMAND_SWAP_NEAREST_BUILDING_MODEL,{0,0,0,bits(10),10,11});attach(swap);t={};t.ip=64;
    check(swap.step(t,0)==lcs::StepResult::yielded && t.ip==64 && calls.size()==4,"Building swap waits until both real models are resident");
    for(const auto& c:calls)check(c.opcode!=0x03B6,"No native swap before residency");loaded=true;calls.clear();
    check(swap.step(t,16)==lcs::StepResult::continued && calls.back().opcode==0x03B6 && calls.back().inputs[4]==10100 && calls.back().inputs[5]==10101,"Retry performs original model replacement once readiness is real");
    loaded=false;auto create=command(COMMAND_CREATE_OBJECT_NO_OFFSET,{10,0,0,0},true);attach(create);t={};t.ip=64;t.locals[0]=92;calls.clear();
    check(create.step(t,0)==lcs::StepResult::yielded && calls.size()==2 && t.locals[0]==92,"Unloaded mission entity does not enter the native factory or produce a handle");
    int garages=0;adapter.create_garage=[&](const std::array<float,8>& b,int32_t type,int32_t id)->std::optional<std::string>{check(b[3]==2 && b[6]==3 && type==19 && id==0,"Garage preserves original geometry and reviewed type");++garages;return "LCSG00";};
    auto garage=command(COMMAND_SET_GARAGE,{0,0,0,bits(2),0,0,bits(3),bits(4),19},true);attach(garage);t={};t.ip=64;
    check(garage.step(t,0)==lcs::StepResult::continued && t.locals[0]==0 && garages==1,"Native garage registration returns original stable handle");
    auto exclusive=command(COMMAND_SET_GARAGE,{0,0,0,bits(2),0,0,bits(3),bits(4),13},true);attach(exclusive);t={};t.ip=64;
    check(exclusive.step(t,0)==lcs::StepResult::continued && t.locals[0]==1 && garages==1,"Exclusive crusher declares only LCS geometry, with no substitute native garage");
    auto open=command(COMMAND_OPEN_GARAGE,{1});attach(open);t={};t.ip=64;calls.clear();
    check(open.step(t,0)==lcs::StepResult::unsupported && calls.empty(),"Exclusive service remains blocked when an operation requires a native binding");
    auto camera=command(COMMAND_NO_SPECIAL_CAMERA_FOR_THIS_GARAGE,{1});attach(camera);t={};t.ip=64;
    check(camera.step(t,0)==lcs::StepResult::continued && adapter.declared_garage_player_camera(1)==true && calls.empty(),"Exclusive service retains original camera setting without creating a native garage");
    auto capacity=command(COMMAND_SET_MAXIMUM_NUMBER_OF_CARS_IN_GARAGE,{1,257});attach(capacity);t={};t.ip=64;
    check(capacity.step(t,0)==lcs::StepResult::continued && adapter.declared_garage_capacity(1)==1,"Original uint8 garage capacity is retained, including original narrowing semantics");
    capacity=command(COMMAND_SET_MAXIMUM_NUMBER_OF_CARS_IN_GARAGE,{0,1});attach(capacity);t={};t.ip=64;
    check(capacity.step(t,0)==lcs::StepResult::unsupported && adapter.declared_garage_capacity(0)==4,"Remembering a value cannot falsely enforce an unadapted SA hideout limit");
    camera=command(COMMAND_NO_SPECIAL_CAMERA_FOR_THIS_GARAGE,{99});attach(camera);t={};t.ip=64;
    check(camera.step(t,0)==lcs::StepResult::unsupported,"Invalid original garage cannot receive a camera policy");
    auto lock=command(COMMAND_LOCK_GARAGE,{0,1});attach(lock);t={};t.ip=64;calls.clear();
    check(lock.step(t,0)==lcs::StepResult::continued && calls.size()==2 && calls[0].opcode==0x0361 && calls[1].opcode==0x02B9 && calls[0].text=="LCSG00","Lock closes and deactivates the actual registered native garage");
    auto change=command(COMMAND_CHANGE_GARAGE_TYPE,{0,5});attach(change);t={};t.ip=64;calls.clear();
    check(change.step(t,0)==lcs::StepResult::continued && calls.back().opcode==0x02FA && calls.back().inputs==std::vector<int32_t>{5} && calls.back().text=="LCSG00","Original garage handle is replaced by its real native name before type change");
    change=command(COMMAND_CHANGE_GARAGE_TYPE,{0,13});attach(change);t={};t.ip=64;calls.clear();
    check(change.step(t,0)==lcs::StepResult::unsupported && calls.empty(),"Unsupported crusher policy cannot corrupt a live native garage");
    adapter.execute=[&](const lcs::NativeCall& c){calls.push_back(c);lcs::NativeResult r;r.executed=true;r.condition=false;r.values[0]=c.opcode==0x01F5?2345:22;return r;};
    auto area=command(COMMAND_IS_PLAYER_IN_AREA_3D|0x8000,{0,0,0,0,bits(1),bits(1),bits(1),0});attach(area);t={};t.ip=64;calls.clear();
    check(area.step(t,0)==lcs::StepResult::continued && t.condition && calls.size()==2 && calls.back().opcode==0x00A4 && calls.back().inputs[0]==2345,"Location uses real player ped and original NOT exactly once");
    auto weapon=command(COMMAND_GET_CURRENT_PLAYER_WEAPON,{0},true);attach(weapon);t={};t.ip=64;calls.clear();
    check(weapon.step(t,0)==lcs::StepResult::continued && t.locals[0]==17 && calls.back().opcode==0x0470,"Equipped SA Colt45 translates back to original LCS type rather than leaking native 22");
    adapter.execute=[](const lcs::NativeCall&){lcs::NativeResult r;r.executed=true;r.values[0]=46;return r;};t={};t.ip=64;t.locals[0]=93;
    check(weapon.step(t,0)==lcs::StepResult::unsupported && t.locals[0]==93,"Unmapped equipped weapon cannot silently corrupt mission weapon checks");
    calls.clear();adapter.execute=[&](const lcs::NativeCall& c){calls.push_back(c);lcs::NativeResult r;r.executed=true;r.condition=false;r.values[0]=2345;return r;};
    auto near=command(COMMAND_LOCATE_PLAYER_ANY_MEANS_CHAR_3D|0x8000,{0,42,bits(2),bits(3),bits(4),0});attach(near);t={};t.ip=64;
    check(near.step(t,0)==lcs::StepResult::continued && t.condition && calls.size()==2 && calls.back().opcode==0x0104 && calls.back().inputs==std::vector<int32_t>({2345,42,bits(2),bits(3),bits(4),0}),"Story proximity checks query the real player and original target, applying NOT once");
    auto warp=command(COMMAND_WARP_PLAYER_INTO_CAR,{0,73});attach(warp);t={};t.ip=64;calls.clear();
    check(warp.step(t,0)==lcs::StepResult::continued && calls.size()==2 && calls.back().opcode==0x036A && calls.back().inputs==std::vector<int32_t>({2345,73}),"Original player enters the actual specified car through native ped resolution");
    auto inventory=command(COMMAND_HAS_PLAYER_GOT_WEAPON,{0,17});attach(inventory);t={};t.ip=64;calls.clear();
    check(inventory.step(t,0)==lcs::StepResult::continued && !t.condition && calls.back().opcode==0x0491 && calls.back().inputs==std::vector<int32_t>({2345,22}),"Actual native inventory query translates original weapon type and retains its false answer");
    inventory=command(COMMAND_HAS_CHAR_GOT_WEAPON,{42,2});attach(inventory);t={};t.ip=64;calls.clear();
    check(inventory.step(t,0)==lcs::StepResult::unsupported && calls.empty(),"Exclusive weapon cannot report an unrelated native inventory result");
    auto alive=command(COMMAND_IS_CAR_STILL_ALIVE|0x8000,{73});attach(alive);t={};t.ip=64;calls.clear();
    check(alive.step(t,0)==lcs::StepResult::continued && !t.condition && calls.size()==1 && calls[0].opcode==0x0119,"Vehicle life combines actual death state and original NOT exactly once");
    adapter.execute=[&](const lcs::NativeCall& c){calls.push_back(c);lcs::NativeResult r;r.executed=true;r.values[0]=bits(5);return r;};
    auto sprint=command(COMMAND_SET_CHAR_OBJ_SPRINT_TO_COORD,{42,bits(2),bits(3)});attach(sprint);t={};t.ip=64;calls.clear();
    check(sprint.step(t,0)==lcs::StepResult::continued && calls.size()==2 && calls[0].opcode==0x02CE && calls.back().inputs==std::vector<int32_t>({42,bits(2),bits(3),bits(5),7,-2}),"Original sprint starts an untimed native task using actual ground height");
    lcs::NativeAdapter crane_adapter(image);crane_adapter.world_ready=true;
    auto crane=command(COMMAND_ACTIVATE_CRUSHER_CRANE,{bits(0),bits(0),bits(5),bits(6),bits(1),bits(2),bits(3),bits(4),bits(5),bits(90)});
    crane.backend=[&](lcs::Vm& v,lcs::Thread& thread,uint16_t op){return crane_adapter.dispatch(v,thread,op);};t={};t.ip=64;
    check(crane.step(t,0)==lcs::StepResult::unsupported && !crane_adapter.active_crane_declarations(),"A missing original world crane cannot be fabricated by its configuration command");
    std::istringstream crane_site("inst\n2100, crusher_crane, 0, 1, 0, 10\nend\n");crane_adapter.load_crane_placements(crane_site,2100);
    check(crane.step(t,0)==lcs::StepResult::continued && crane_adapter.active_crane_declarations()==1,"Original crane configuration resolves a site that exists in mounted placements");
    auto capture=command(COMMAND_IS_CRANE_LIFTING_CAR,{bits(0),bits(0),73});capture.backend=crane.backend;t={};t.ip=64;t.condition=true;
    check(capture.step(t,0)==lcs::StepResult::unsupported && t.condition,"Crane configuration does not invent vehicle pickup or objective completion");
    auto retail=command(COMMAND_IS_FINAL_GAME,{});attach(retail);t={};t.ip=64;
    check(retail.step(t,0)==lcs::StepResult::continued && t.condition,"Retail MAIN uses the actual release selector");
    auto developer=command(COMMAND_IS_DEVELOPER|0x8000,{});attach(developer);t={};t.ip=64;
    check(developer.step(t,0)==lcs::StepResult::continued && t.condition,"Retail campaign does not enter developer mission selection");
    auto set_developer=command(COMMAND_SET_DEVELOPER_FLAG,{1});attach(set_developer);t={};t.ip=64;
    check(set_developer.step(t,0)==lcs::StepResult::continued,"Original developer flag assignment is retained");t={};t.ip=64;
    check(developer.step(t,0)==lcs::StepResult::continued && !t.condition,"Build predicate reads its original mutable flag and respects NOT");
    auto packages=command(COMMAND_SET_HIDDEN_PACKAGES_COLLECTED,{5});attach(packages);t={};t.ip=64;
    check(packages.step(t,0)==lcs::StepResult::unsupported,"Missing native count backend cannot falsely acknowledge a reset");
    std::array<int32_t,2> counters{99,100};adapter.collectables=[&](bool total,std::optional<int32_t> setting)->std::optional<int32_t>{auto& value=counters[total?1:0];if(setting)value=*setting;return value;};
    check(packages.step(t,0)==lcs::StepResult::continued && counters[0]==5 && counters[1]==100,"Original count assignment reaches the live player backend");
    counters[0]=7;auto count=command(COMMAND_GET_COLLECTABLE1S_COLLECTED,{},true);attach(count);t={};t.ip=64;
    check(count.step(t,0)==lcs::StepResult::continued && t.locals[0]==7,"Getter reads a changed live count rather than a cached declaration");
    auto found=command(COMMAND_SET_UNIQUE_JUMPS_FOUND,{3});attach(found);t={};t.ip=64;calls.clear();
    check(found.step(t,0)==lcs::StepResult::continued && calls.size()==1 && calls[0].opcode==0x0629 && calls[0].inputs==std::vector<int32_t>({144,3}),"Original unique-jump count uses the real native statistic setter");
    auto exported=command(COMMAND_ADD_EXPORTED_CARS,{2});attach(exported);t={};t.ip=64;calls.clear();
    check(exported.step(t,0)==lcs::StepResult::continued && calls.back().opcode==0x0623 && calls.back().inputs==std::vector<int32_t>({213,2}),"Original export registration increments the actual exported-vehicle statistic");
    float weapon_budget=10.25f;bool budget_readable=true,budget_writable=true;int budget_writes=0;
    adapter.execute=[&](const lcs::NativeCall& c){calls.push_back(c);lcs::NativeResult r;
        check(c.inputs[0]==13,"Weapon expenditure uses native STAT_WEAPON_BUDGET, never player cash");
        if(c.opcode==0x0653){r.executed=budget_readable;r.values[0]=bits(weapon_budget);}
        else {check(c.opcode==0x062A && c.outputs==0,"Weapon expenditure commits via SET_FLOAT_STAT");r.executed=budget_writable;
            if(r.executed){std::memcpy(&weapon_budget,&c.inputs[1],4);++budget_writes;}}
        return r;
    };
    auto purchase=command(COMMAND_ADD_MONEY_SPENT_ON_WEAPONS,{240});attach(purchase);t={};t.ip=64;calls.clear();
    check(purchase.step(t,0)==lcs::StepResult::continued && weapon_budget==250.25f && budget_writes==1 && calls.size()==2,
          "Paid purchase adds the integer cost to the live float budget exactly once");
    purchase=command(COMMAND_ADD_MONEY_SPENT_ON_WEAPONS,{-40});attach(purchase);t={};t.ip=64;
    check(purchase.step(t,0)==lcs::StepResult::continued && weapon_budget==210.25f,"Signed source correction is not silently dropped by SA IncrementStat");
    budget_readable=false;calls.clear();t={};t.ip=64;
    check(purchase.step(t,0)==lcs::StepResult::unsupported && t.ip==64 && budget_writes==2 && calls.size()==1,
          "A missing native read cannot update the budget or consume the original instruction");
    budget_readable=true;budget_writable=false;t={};t.ip=64;
    check(purchase.step(t,0)==lcs::StepResult::unsupported && t.ip==64 && weapon_budget==210.25f && budget_writes==2,
          "A rejected write preserves the live expenditure and original instruction");
    budget_writable=true;weapon_budget=std::numeric_limits<float>::quiet_NaN();calls.clear();t={};t.ip=64;
    check(purchase.step(t,0)==lcs::StepResult::unsupported && calls.size()==1 && budget_writes==2,
          "Corrupt native statistic cannot be copied back into the engine");
    auto declare=command(COMMAND_DECLARE_FORT_STAUNTON_DESTROYED_FLAG,{0});declare.memory[66]=204;declare.memory[67]=2;attach(declare);t={};t.ip=64;
    check(declare.step(t,0)==lcs::StepResult::continued && adapter.fort_staunton_flag()==8,"World destruction declaration retains the original global reference rather than its current boolean value");
    auto invalid_declare=command(COMMAND_DECLARE_FORT_STAUNTON_DESTROYED_FLAG,{},true);attach(invalid_declare);t={};t.ip=64;
    check(invalid_declare.step(t,0)==lcs::StepResult::failed && adapter.fort_staunton_flag()==8,"Local variable cannot overwrite the declared original world flag binding");
    // R08: source 1655 requires a native flag consumer. This gate cannot
    // publish reward state from a handle/pointer alias or a rejected write.
    lcs::RewardVehicleReceipts rewards;
    const lcs::RewardVehicleIdentity car{0x2708,0x10004000}, recycled{0x2808,0x10004000};
    const auto first_reward=rewards.begin(car,true);
    check(first_reward && !rewards.confirmed(car),"Reward preflight alone is not native acceptance");
    check(!rewards.confirm(*first_reward,car,false) && !rewards.confirmed(car),"Rejected native reward write publishes nothing");
    const auto second_try=rewards.begin(car,true);
    check(second_try && !rewards.confirm(*second_try,recycled,true) && !rewards.confirmed(recycled),"Car generation change before commit cannot be accepted");
    const auto third_try=rewards.begin(car,true);
    check(third_try && rewards.confirm(*third_try,car,true) && rewards.confirmed(car)==true,"Only verified original native car can commit a reward flag");
    const auto second_reward=rewards.begin(car,false);
    check(second_reward && rewards.confirm(*second_reward,car,true) && rewards.confirmed(car)==false,"Clearing the reward flag is an actual opposite state");
    check(!rewards.confirm(*first_reward,car,true),"Stale callback cannot roll back a newer native reward acknowledgement");
    rewards.retired(recycled);check(rewards.confirmed(car)==false,"Retirement of a different generation cannot erase live state");
    rewards.retired(car);check(!rewards.confirmed(car),"Vehicle deletion retires reward ownership");
    const auto retired_inflight=rewards.begin(car,true);
    rewards.retired(car);
    check(retired_inflight && !rewards.confirm(*retired_inflight,car,true),"Retired vehicle cannot accept a late native acknowledgement");
    check(!rewards.begin({0x2708,0},true) && !rewards.begin({-1,0x10004000},true),"Unverified vehicle identity cannot prepare a flag write");
    const auto new_owner=rewards.begin(recycled,true);
    check(new_owner && rewards.confirm(*new_owner,recycled,true) && rewards.confirmed(recycled)==true,"Reused pointer with a genuinely new handle can be independently accepted");
    const auto before_clear=rewards.begin(recycled,false);
    rewards.clear();check(!rewards.confirmed(recycled),"World teardown clears source reward ownership");
    check(before_clear && !rewards.confirm(*before_clear,recycled,true),"World reset cancels in-flight reward acknowledgements");
    std::cout<<"PASS: original crane/garage configuration, retail selectors, live collectable/stat counters, native mission proximity/inventory/sprint and streaming barriers; gameExecuted=false\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
