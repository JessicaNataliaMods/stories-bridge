#include "lcs/first_mission.hpp"
#include "ScriptCommands.h"
#include <stdexcept>
#include <cstring>
#include <cmath>

namespace lcs {
uint32_t FirstMission::fingerprint(const ScmImage& image) {
    uint32_t crc=0xffffffff;
    for(auto b:image.bytes){crc^=b;for(int i=0;i<8;++i)crc=(crc>>1)^((crc&1)?0xedb88320u:0u);}
    return crc;
}
bool FirstMission::supports_verified_image(const ScmImage& image) {
    return image.main_size==272710 && image.mission_offsets.size()==118 && fingerprint(image)==0xf39f994a;
}
static bool has_destroyed_car_failure_signature(Vm& vm) {
    // The source range is a mission overlay in ScriptSpace. Checking the full
    // image fingerprint at installation is complemented by these exact branch
    // and failure-body checks when the overlay is actually active.
    if(vm.main_size!=272710 || vm.mission_size()!=8157 || vm.memory.size()<280867) return false;
    if(read16(vm.memory,275035)!=uint16_t(COMMAND_IS_CAR_DEAD|0x8000) ||
       read16(vm.memory,275039)!=COMMAND_GOTO_IF_FALSE ||
       read16(vm.memory,280477)!=COMMAND_PRINT_BIG ||
       read16(vm.memory,280492)!=COMMAND_GOTO ||
       read16(vm.memory,280499)!=COMMAND_CLEAR_SMALL_PRINTS ||
       read16(vm.memory,280501)!=COMMAND_PRINT_BIG ||
       read16(vm.memory,280516)!=COMMAND_GOSUB ||
       read16(vm.memory,280523)!=COMMAND_RESTART_CRITICAL_MISSION ||
       read16(vm.memory,280541)!=COMMAND_RETURN ||
       read16(vm.memory,280863)!=COMMAND_MISSION_HAS_FINISHED ||
       read16(vm.memory,280865)!=COMMAND_RETURN) return false;
    try {
        Thread dead_car;dead_car.ip=275037;
        if(vm.global_reference(dead_car)!=15752 || dead_car.ip!=275039) return false;
        Thread branch;branch.ip=275041;
        if(vm.resolve_label(vm.parameter(branch))!=275118) return false;
        Thread failure;failure.ip=280479;
        if(vm.text_label(failure)!="vic1_f" || vm.parameter(failure)!=5000 || vm.parameter(failure)!=8) return false;
        if(failure.ip!=280492 || read16(vm.memory,failure.ip)!=COMMAND_GOTO) return false;
        failure.ip+=2;
        if(vm.resolve_label(vm.parameter(failure))!=280499) return false;
        failure.ip=280503;
        if(vm.text_label(failure)!="m_fail" || vm.parameter(failure)!=1000 || vm.parameter(failure)!=1) return false;
        failure.ip=280518;
        if(vm.resolve_label(vm.parameter(failure))!=280648) return false;
    } catch(const std::exception&) {
        return false;
    }
    return true;
}
bool FirstMission::redirect_destroyed_car_failure(Vm& vm,Thread& t) {
    // This instruction is the GOTO_IF_FALSE immediately following the inverted
    // IS_CAR_DEAD at 275035. At this hook point the previous opcode and its
    // condition are still intact, so false means the native dead predicate was
    // true. Healthy cars and the separate player-not-in-car jump are untouched.
    if(t.ip!=275039 || t.last_ip!=275039 || t.last_opcode!=COMMAND_IS_CAR_DEAD ||
       !t.inverted || t.condition || t.and_or) return false;
    if(!has_destroyed_car_failure_signature(vm)) return false;
    if(t.stack_size>=t.stack.size()) throw std::runtime_error("VIC1 destroyed-car failure needs one original GOSUB frame");
    // This duplicates the original failure helper's caller continuation. The
    // label itself runs the source M_FAIL, native cleanup, and critical retry.
    t.stack[t.stack_size++]=275203;
    t.ip=280477;
    return true;
}
FirstMission::FirstMission(const ScmImage& image,Runtime& runtime,NativeAdapter& adapter):runtime_(runtime),adapter_(adapter) {
    if(!supports_verified_image(image))
        throw std::runtime_error("First-mission profile requires the verified original PS2 script");
    adapter_.bind_model(79,125,"gang01",ModelKind::ped);
    adapter_.bind_model(109,124,"vinc_01",ModelKind::ped);
    adapter_.bind_model(152,547,"kuruma",ModelKind::vehicle);
    adapter_.bind_ped_type(7,12); // LCS Mafia -> SA GANG6, the native test actors' faction
    adapter_.bind_garage(0,"LCSVIC1"); // This profile's single verified native garage.
    runtime_.declare_mission_flag(2240); // Original MAIN clears it; VIC1 sets it at 273459.
    runtime_.vm().before_step=[this](Vm& vm,Thread& t){return before(vm,t);};
}
NativeResult FirstMission::native(NativeCall call) {
    if(!execute)throw std::runtime_error("First mission has no native executor");
    call.mission_context=!arrival_playing_;
    const auto result=execute(call);
    if(!result.executed)throw std::runtime_error("First-mission native command failed: "+result.diagnostic);
    return result;
}
void FirstMission::put(uint32_t offset,int32_t value) {
    auto& vm=runtime_.vm();
    if(offset<8 || offset%4 || offset+4ull>vm.variable_space)throw std::runtime_error("Invalid first-mission global");
    for(int i=0;i<4;++i)vm.memory[offset+i]=uint8_t(uint32_t(value)>>(8*i));
}
bool FirstMission::before(Vm& vm,Thread& t) {
    if(redirect_destroyed_car_failure(vm,t)) {
        if(log)log("VIC1 destroyed mission car entered original M_FAIL cleanup and critical retry path");
        return true;
    }
    if(arrival_playing_ && t.ip==268810 && !t.stack_size) {
        // Return from the original shared movie CALL (selector 22 = INTRO).
        // The rest of this MAIN worker is the debug movie selector, outside
        // this profile. VIC1 starts next tick after native CLEAR_CUTSCENE.
        if(adapter_.has_active_cutscene())throw std::runtime_error("Arrival returned without clearing its native cutscene");
        arrival_playing_=false;arrival_completed_=true;t.active=false;
        if(log)log("Original INTRO completed or skipped; VIC1 will start after movie cleanup");
        return true;
    }
    if(finished) {
        // A single-mission session ends after its original success/failure and
        // cleanup. Persistent campaign/save workers started by that success
        // are outside this profile, rather than silently emulating their work.
        if(log)log("FirstMission test ends after original cleanup; post-mission campaign/save work deferred: "+t.name);
        t.active=false;return true;
    }
    uint32_t target=0;
    switch(t.ip) {
    case 273958:
        if(opening_movie)return false;
        target=89381;break; // Only excluded when the native package is absent.
    case 273970:case 273985:case 274000:target=83577;break; // full-campaign island population reset
    case 274038:target=74314;break; // temporary native player costume is retained
    default: {
        if(t.ip<272710 || t.ip>=280866 || read16(vm.memory,t.ip)!=COMMAND_CALL)return false;
        auto probe=t;probe.ip+=5;
        if(vm.parameter(probe)!=74314)return false;
        target=74314;break; // remaining original tutorial costume changes, same test-skin policy
    }
    }
    auto cursor=t;
    if(read16(vm.memory,cursor.ip)!=COMMAND_CALL)throw std::runtime_error("First-mission CALL guard mismatch");
    cursor.ip+=2;
    const auto inputs=vm.memory.at(cursor.ip++),outputs=vm.memory.at(cursor.ip++);
    ++cursor.ip;
    if(outputs || vm.parameter(cursor)!=int32_t(target))throw std::runtime_error("First-mission prelude signature mismatch");
    for(unsigned i=0;i<inputs;++i)(void)vm.parameter(cursor);
    if(log)log("FirstMission excluded prelude CALL ip="+std::to_string(t.ip)+" target="+std::to_string(target));
    t.ip=cursor.ip;return true;
}
ScriptId FirstMission::start() {
    if(opening_movie && !adapter_.has_cutscene("homswet"))throw std::runtime_error("Opening movie requires the mounted original homswet cutscene");
    if(arrival_movie && (!opening_movie || !adapter_.has_cutscene("intro")))throw std::runtime_error("Arrival requires the mounted original intro and homswet cutscenes");
    const auto player=native({0x01F5,{0},{},0,1});
    if(player.values[0]<0)throw std::runtime_error("First mission requires the existing native player");
    if(!native({0x056D,{player.values[0]},{},0,0,true}).condition)
        throw std::runtime_error("First mission received an invalid native player handle: "+std::to_string(player.values[0]));
    if(log)log("FirstMission verified native player handle="+std::to_string(player.values[0]));
    passed=false;finished=false;pending_retry_=false;
    // The saved safehouse coordinates belong to the original main-script
    // initializer, not VIC1's buffer. Execute its actual constant assignments
    // before the first mission uses them; stop before any worker/engine code.
    Thread initializer;initializer.ip=179731;
    unsigned assignments=0;
    auto& vm=runtime_.vm();
    while(read16(vm.memory,initializer.ip)==COMMAND_SET_VAR_FLOAT || read16(vm.memory,initializer.ip)==COMMAND_SET_VAR_INT) {
        if(++assignments>200 || vm.step(initializer,0)!=StepResult::continued)throw std::runtime_error("Safehouse initializer failed");
    }
    if(assignments<50)throw std::runtime_error("Incomplete original safehouse coordinate initialization");
    if(log)log("FirstMission original safehouse constant assignments="+std::to_string(assignments)+" end="+std::to_string(initializer.ip));
    put(2144,0);put(2160,player.values[0]); // values originally established by initialization block 5
    const auto radio=native({0x051E,{}, {},0,1});radio_=radio.values[0];radio_enabled_=true;
    // The profile starts outside the office. Original VIC1 places the player and
    // its car immediately afterward using its own coordinates and ground sentinel.
    native({0x04BB,{0}});
    // Real native respawn inside the LCS world, also used by the critical retry.
    native({0x016E,{0x44b5a19a,int32_t(0xc3422b85),0x425e47ae,0}});
    if(arrival_movie && !arrival_completed_) {
        // Reuse a real original CALL so compact locals and the return frame
        // are exactly those of LCS. The source fingerprint guards this site;
        // the shared movie selector compares 22 before loading INTRO.
        if(read16(vm.memory,268799)!=COMMAND_CALL || read32(vm.memory,268805)!=89381)
            throw std::runtime_error("Original movie CALL signature changed");
        root_=runtime_.start_script(268799,{22});arrival_playing_=true;
        if(log)log("Original arrival INTRO queued through shared movie selector 22");
        return root_;
    }
    return start_vic1();
}
ScriptId FirstMission::start_vic1() {
    root_=runtime_.load_mission(9);
    if(log)log(std::string("FirstMission VIC1 started: original objectives; native test skins and Primo; opening movie ")+(opening_movie?"enabled":"excluded"));
    return root_;
}
StepResult FirstMission::dispatch(Vm& vm,Thread& t,uint16_t op) {
    switch(op) {
    case COMMAND_PLAY_MISSION_PASSED_TUNE:
        // This profile uses SA's completion track. Original LCS stream indices
        // beyond VIC1's standard success cue have no registered resource here.
        if(vm.parameter(t)!=1)throw std::runtime_error("Unmapped LCS completion track");
        native({0x0394,{1}});return StepResult::continued;
    case COMMAND_REGISTER_OUTFIT_CHANGE:
        ++outfit_changes_;return StepResult::continued;
    case COMMAND_SWITCH_RUBBISH:
        rubbish_enabled_=vm.parameter(t)!=0; // LCS paper/debris renderer is not mounted in this test.
        return StepResult::continued;
    case COMMAND_SET_EXTRA_COLOUR_LIGHT_DIRECTION:
        // The native SA lighting profile owns the direction in this test.
        // Preserve the LC request for the later renderer/timecycle integration;
        // this cosmetic operation does not replace any objective predicate.
        for(auto& f:scene_light_){const auto b=vm.parameter(t);std::memcpy(&f,&b,4);if(!std::isfinite(f))throw std::runtime_error("Invalid scene light direction");}
        return StepResult::continued;
    case COMMAND_SET_EXTRA_COLOURS: {
        const auto colour=vm.parameter(t),transition=vm.parameter(t);
        if(colour<1 || colour>32 || (transition!=0 && transition!=1))throw std::runtime_error("Invalid first-mission colour request");
        // LC timecycle banks are not yet mounted. A bounded native extra colour
        // supplies the scene lighting in this explicit appearance test profile.
        native({0x04F9,{1,transition}});return StepResult::continued;
    }
    case COMMAND_CLEAR_EXTRA_COLOURS:
        native({0x04FA,{vm.parameter(t)}});return StepResult::continued;
    case COMMAND_SET_AREA_VISIBLE: {
        const auto area=vm.parameter(t);
        if(area<0 || area>2)throw std::runtime_error("Unmapped first-mission area");
        // All converted IPL instances in this test installation are in native
        // area zero, including the safehouse. Keep that real area selected.
        native({0x04BB,{0}});return StepResult::continued;
    }
    case COMMAND_SWAP_BUILDINGS: {
        const auto group=vm.parameter(t),state=vm.parameter(t);
        if(group==21 && opening_movie && (state==0 || state==1)) {
            // The original office helper replaces franksclb02 with
            // izzy_salOffice at the same IPL origin, then hides LODnksclbase.
            // Both models are already in WORLD.IMG; load them before the
            // original 03B6 swap rather than adding an overlapping office IPL.
            for(int32_t object:{926,3885,696}) {
                const auto model=adapter_.model(object);
                if(!model)throw std::runtime_error("Missing original Salvatore office binding");
                native({0x0247,{*model}});
            }
            native({0x038B,{}});
            if(log)log("Original office models resident; original building/LOD swaps follow");
            return StepResult::continued;
        }
        if(group!=6 || (state!=0 && state!=1))throw std::runtime_error("Building group outside first-mission safehouse profile: "+std::to_string(group));
        // Group 6's geometry is prebuilt in this loader. Mount both door states
        // before the ORIGINAL two SWAP_NEAREST_BUILDING_MODEL operations, which
        // perform the actual native entity replacement in the following helper.
        for(int32_t object:{-33,-34,-35,-36}) {
            const auto model=adapter_.model(object);
            if(!model)throw std::runtime_error("Missing safehouse door state binding");
            native({0x0247,{*model}});
        }
        native({0x038B,{}});safehouse_group_=state!=0;
        if(log)log("FirstMission prebuilt safehouse group 6 state="+std::to_string(state)+"; original native door swaps follow");
        return StepResult::continued;
    }
    case COMMAND_DISABLE_PAUSE_MENU:
        // VIC1 explicitly enables pause (0) after the dialogue. This isolated
        // SA profile never disables the native pause menu; reject any attempt
        // to request the unsupported console-only disabled policy.
        if(vm.parameter(t)!=0)throw std::runtime_error("Disabled console pause policy is outside first-mission profile");
        if(log)log("FirstMission retains enabled native pause menu");
        return StepResult::continued;
    case COMMAND_LOAD_SPECIAL_CHARACTER:
    case COMMAND_HAS_SPECIAL_CHARACTER_LOADED:
    case COMMAND_UNLOAD_SPECIAL_CHARACTER: {
        const auto slot=vm.parameter(t);
        if(slot!=1)throw std::runtime_error("Unbound special character in first-mission profile");
        if(op==COMMAND_LOAD_SPECIAL_CHARACTER && vm.text_label(t)!="vinc_01")throw std::runtime_error("Unexpected first-mission character resource");
        if(op==COMMAND_HAS_SPECIAL_CHARACTER_LOADED)vm.compare(t,native({0x0248,{124},{},0,0,true}).condition);
        else native({uint16_t(op==COMMAND_LOAD_SPECIAL_CHARACTER?0x0247:0x0249),{124}});
        return StepResult::continued;
    }
    case COMMAND_SET_RADIO_CHANNEL: {
        (void)vm.parameter(t);const auto position=vm.parameter(t);
        if(position!=-1)throw std::runtime_error("LCS radio seek is not mounted in this test profile");
        // LCS radio streams are not mounted. Keep native music off so it cannot
        // overlap the actual original mission speech backend.
        radio_=13;native({0x041E,{13}});return StepResult::continued;
    }
    case COMMAND_SWITCH_CAR_RADIO: {
        const auto mode=vm.parameter(t);
        if(mode<0 || mode>2)throw std::runtime_error("Unknown LCS radio mode");
        radio_enabled_=mode==0;
        native({0x041E,{radio_enabled_?radio_:13}});return StepResult::continued;
    }
    case COMMAND_REGISTER_MISSION_PASSED: {
        const auto result=adapter_.dispatch(vm,t,op);
        if(result==StepResult::continued){passed=true;if(log)log("FirstMission ORIGINAL VIC1 passed");}
        return result;
    }
    case COMMAND_MISSION_HAS_FINISHED:
        native({0x00D8,{}});finished=true;
        put(2240,0);
        // The full main-script controller would restart a critical mission.
        // This isolated session retries after actual native player recovery.
        if(!passed){pending_retry_=true;retry_at_=0;}
        if(log)log(std::string("FirstMission native cleanup complete; passed=")+(passed?"true":"false"));
        return StepResult::continued;
    case COMMAND_RESTART_CRITICAL_MISSION: {
        const auto result=adapter_.dispatch(vm,t,op);
        if(result!=StepResult::continued)return result;
        pending_retry_=true;retry_at_=0;
        if(log)log("FirstMission original critical failure requested retry");
        return StepResult::continued;
    }
    default:return StepResult::unsupported;
    }
}
void FirstMission::tick(uint32_t now) {
    if(arrival_completed_ && !runtime_.mission_index()) {start_vic1();return;}
    if(!pending_retry_ || runtime_.script(root_).thread.active)return;
    if(!retry_at_){retry_at_=now+5000;return;}
    if(int32_t(now-retry_at_)<0)return;
    // Wait for native death/arrest recovery rather than altering player-state bytes.
    if(!native({0x0256,{0},{},0,0,true}).condition)return;
    start();
}
}
