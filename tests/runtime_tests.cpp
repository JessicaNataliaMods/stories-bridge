#include "lcs/runtime.hpp"
#include "lcs/update_supervisor.hpp"
#include "lcs/native.hpp"
#include "ScriptCommands.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok,const char* name) { if (!ok) throw std::runtime_error(name); }
template<class F> void rejects(F f,const char* name) {
    bool rejected=false;
    try { f(); } catch (const std::exception&) { rejected=true; }
    check(rejected,name);
}
struct Code {
    std::vector<uint8_t> bytes;
    uint32_t label() const { return uint32_t(64+bytes.size()); }
    void op(uint16_t v) { bytes.push_back(uint8_t(v)); bytes.push_back(uint8_t(v>>8)); }
    void integer(int32_t v) {
        bytes.push_back(6); for (int i=0;i<4;++i) bytes.push_back(uint8_t(uint32_t(v)>>(8*i)));
    }
    size_t target() { bytes.push_back(6); const auto p=bytes.size(); bytes.resize(p+4); return p; }
    void patch(size_t p,int32_t v) { for (int i=0;i<4;++i) bytes.at(p+i)=uint8_t(uint32_t(v)>>(8*i)); }
    void global(uint8_t index) { bytes.push_back(204); bytes.push_back(index); }
    void local(uint8_t index) { bytes.push_back(uint8_t(12+index)); }
    void set_global(uint8_t index,int32_t v) { op(COMMAND_SET_VAR_INT); global(index); integer(v); }
    void wait(int32_t duration) { op(COMMAND_WAIT); integer(duration); }
    void name(const std::string& name) {
        op(COMMAND_SCRIPT_NAME);
        for (size_t i=0;i<8;++i) bytes.push_back(i<name.size() ? uint8_t(name[i]) : 0);
    }
    void end() { op(COMMAND_TERMINATE_THIS_SCRIPT); }
};
lcs::ScmImage image(const Code& main,const std::vector<Code>& missions = {}) {
    check(main.bytes.size()<448,"test main fits its allocation");
    lcs::ScmImage result;
    result.main_size=512; result.variable_space=64; result.entry_point=64;
    result.true_globals=4; result.most_globals=10;
    result.bytes.resize(512);
    result.bytes[0]=COMMAND_GOTO; result.bytes[2]=6; result.bytes[3]=64;
    std::copy(main.bytes.begin(),main.bytes.end(),result.bytes.begin()+64);
    for (const auto& mission:missions) {
        result.mission_offsets.push_back(uint32_t(result.bytes.size()));
        result.largest_mission=std::max(result.largest_mission,uint32_t(mission.bytes.size()));
        result.bytes.insert(result.bytes.end(),mission.bytes.begin(),mission.bytes.end());
    }
    return result;
}
uint32_t global(const lcs::Runtime& runtime,uint8_t index) { return lcs::read32(runtime.vm().memory,4u*index); }

void thread_arguments_and_order() {
    Code code; code.name("MAIN");
    code.op(COMMAND_SET_LVAR_INT); code.local(0); code.integer(7);
    code.op(COMMAND_START_NEW_SCRIPT); const auto child=code.target();
    code.local(0); code.integer(-3); code.bytes.insert(code.bytes.end(),{4,0x80,0x3f,0});
    code.set_global(2,111); code.wait(0); code.end();
    code.patch(child,int32_t(code.label())); code.name("WORK");
    code.op(COMMAND_SET_VAR_INT_TO_LVAR_INT); code.global(2); code.local(0);
    code.wait(10);
    code.op(COMMAND_SET_VAR_INT_TO_LVAR_INT); code.global(3); code.local(1);
    code.op(COMMAND_SET_VAR_FLOAT_TO_LVAR_FLOAT); code.global(4); code.local(2);
    code.end();
    lcs::Runtime runtime(image(code)); const auto root=runtime.start_main();
    auto events=runtime.tick(0,0);
    check(events.size()==1 && events[0].id==root && runtime.active_count()==2,"new child waits until next tick");
    check(global(runtime,2)==111 && runtime.script(root).thread.name=="main","parent finishes frame and folds its name");
    events=runtime.tick(0,1); const auto worker=events[0].id;
    check(events.size()==2 && worker!=root && global(runtime,2)==7,"newest ordinary thread runs first next frame");
    check(runtime.script(worker).thread.name=="work" && runtime.script(worker).thread.locals[2]==0x3f800000,
          "thread receives integer, local and compact float arguments");
    runtime.tick(9,9); check(runtime.active_count()==1 && global(runtime,3)==0,"waiting child does not execute early");
    runtime.tick(10,1);
    check(runtime.active_count()==0 && global(runtime,3)==uint32_t(-3) && global(runtime,4)==0x3f800000,
          "waiting child resumes and preserves its locals");
}

void mission_loading_and_bounds() {
    Code main; main.set_global(2,123); main.set_global(8,456); main.end();
    Code first; first.name("MISSION");
    first.op(COMMAND_GOTO); first.integer(-19);
    first.op(COMMAND_CREATE_CHAR); first.set_global(8,99); first.wait(5); first.end();
    Code second; second.end();
    auto original=image(main,{first,second});
    lcs::Runtime runtime(original); runtime.start_main(); runtime.tick(0,0);
    rejects([&] { runtime.vm().resolve_label(-1); },"allocated but unloaded mission bytes cannot be jumped to");
    const auto id=runtime.load_mission(0);
    check(runtime.vm().mission_size()==first.bytes.size() && runtime.script(id).mission,"mission loads into reserved slot");
    check(global(runtime,2)==123 && global(runtime,8)==0,"only mission temporary globals are cleared");
    check(std::equal(first.bytes.begin(),first.bytes.end(),runtime.vm().memory.begin()+512),"mission bytecode remains unchanged");
    runtime.tick(0,0);
    check(global(runtime,8)==99 && runtime.script(id).thread.waiting,"negative mission branch uses the relocated main-size base");
    rejects([&] { runtime.load_mission(1); },"active mission cannot be overwritten");
    check(runtime.mission_index()==0 && runtime.vm().mission_size()==first.bytes.size(),"failed replacement preserves loaded mission");
    runtime.tick(5,5); check(runtime.active_count()==0,"mission script terminates");
    const auto second_id=runtime.load_mission(1);
    check(runtime.vm().mission_size()==2 && runtime.vm().memory[514]==0,"shorter mission removes prior code tail");
    rejects([&] { runtime.vm().resolve_label(-2); },"jump past loaded mission cannot execute zero padding");
    runtime.tick(5,0); check(!runtime.script(second_id).thread.active,"replacement mission executes");
    rejects([&] { runtime.load_mission(2); },"invalid mission index is rejected");

    Code truncated; truncated.op(COMMAND_SET_VAR_INT); truncated.global(8); truncated.bytes.push_back(6);
    lcs::Runtime broken(image(main,{first,truncated})); const auto broken_id=broken.load_mission(1);
    const auto events=broken.tick(0,0);
    check(events.size()==1 && events[0].result==lcs::StepResult::failed && broken.script(broken_id).thread.active && broken.script(broken_id).blocked && broken.mission_buffer_in_use(),
          "truncated mission arguments quarantine their slot and retain the code lease instead of reading padding or disappearing");
}

void mission_launch_timing_and_helpers() {
    Code main; main.op(COMMAND_LOAD_AND_LAUNCH_MISSION_INTERNAL); main.integer(0); main.set_global(2,77); main.end();
    Code mission; mission.set_global(2,22); mission.wait(10); mission.end();
    lcs::Runtime runtime(image(main,{mission})); const auto root=runtime.start_main();
    const auto events=runtime.tick(0,16);
    check(events.size()==2 && events[0].id!=root && events[1].id==root && global(runtime,2)==77,
          "loaded mission processes immediately before parent continuation");
    check(runtime.script(events[0].id).thread.locals[104]==0,"immediate mission start does not advance timers twice");
    runtime.tick(10,10); check(runtime.active_count()==0,"loaded mission resumes through scheduler");

    Code helper; helper.wait(10); helper.end();
    Code owner; owner.op(COMMAND_START_NEW_SCRIPT); owner.integer(64); owner.bytes.push_back(0); owner.end();
    Code replacement; replacement.end();
    lcs::Runtime leases(image(helper,{owner,replacement})); leases.load_mission(0); leases.tick(0,0);
    check(leases.active_count()==1,"mission helper survives its parent");
    leases.load_mission(1);
    const auto helper_events=leases.tick(0,0);
    check(helper_events.size()==2 && !leases.script(helper_events[1].id).resident,"main worker does not inherit mission residency");
    leases.tick(10,10);
    check(leases.active_count()==0,"main worker survives mission buffer replacement");
    Code disk;disk.op(COMMAND_START_NEW_SCRIPT);disk.integer(522);disk.bytes.push_back(0);disk.end();disk.wait(10);disk.end();
    lcs::Runtime disk_leases(image(helper,{disk,replacement}));disk_leases.load_mission(0);disk_leases.tick(0,0);
    rejects([&] { disk_leases.load_mission(1); },"worker inside mission buffer keeps code leased");
    disk_leases.tick(0,0);disk_leases.tick(10,10);disk_leases.load_mission(1);
    check(disk_leases.mission_index()==1,"mission buffer becomes reusable when its worker ends");
}

void launch_local_mission_and_terminate_names() {
    Code code; code.op(COMMAND_LAUNCH_MISSION); const auto target=code.target(); code.end();
    code.patch(target,int32_t(code.label())); code.name("LOCALMIS"); code.wait(100); code.end();
    const auto killer=code.label(); code.name("KILLER");
    code.op(COMMAND_TERMINATE_ALL_SCRIPTS_WITH_THIS_NAME);
    for (const auto c:std::string("LocalMis")) code.bytes.push_back(uint8_t(c));
    code.end();
    lcs::Runtime runtime(image(code)); runtime.start_main(); runtime.tick(0,0);
    const auto events=runtime.tick(0,0); const auto mission=events[0].id;
    check(runtime.script(mission).mission && !runtime.script(mission).resident,"local mission is flagged without owning disk mission buffer");
    runtime.start_script(killer); runtime.tick(0,0);
    check(runtime.active_count()==0 && !runtime.script(mission).thread.active,"name termination handles case and removes waiting mission");

    Code self; self.name("SELF"); self.op(COMMAND_TERMINATE_ALL_SCRIPTS_WITH_THIS_NAME);
    self.bytes.insert(self.bytes.end(),{'s','e','l','f',0,0,0,0}); self.op(COMMAND_CREATE_CHAR);
    lcs::Runtime self_runtime(image(self)); self_runtime.start_main(); const auto self_events=self_runtime.tick(0,0);
    check(self_events[0].result==lcs::StepResult::terminated && self_runtime.active_count()==0,
          "script killed by its own name cannot execute a following opcode");
}

void bounded_initializers_keep_original_order() {
    Code main;
    main.op(COMMAND_LOAD_AND_LAUNCH_MISSION_INTERNAL);main.integer(0);
    main.op(COMMAND_LOAD_AND_LAUNCH_MISSION_INTERNAL);main.integer(1);
    main.set_global(2,99);main.end();
    Code first;first.name("INITZERO");
    for(int n=1;n<=20;++n)first.set_global(3,n);
    first.end();
    Code second;second.name("INITONE");second.set_global(4,77);second.end();
    lcs::Runtime runtime(image(main,{first,second}));const auto root=runtime.start_main();
    check(!runtime.is_mission_script(runtime.script(root).thread),"MAIN does not own native mission cleanup");
    for(uint32_t frame=0;frame<50 && runtime.script(root).thread.active;++frame) {
        const auto events=runtime.tick(frame*16,16,2);
        if(runtime.script(root).awaiting_initial_slice)
            check(runtime.is_mission_script(runtime.script(runtime.script(root).awaiting_initial_slice).thread),"loaded original initialization scripts carry mission cleanup ownership");
        for(const auto& event:events)check(event.result!=lcs::StepResult::failed && event.result!=lcs::StepResult::unsupported,
            "long initialization slices must not race the next mission-buffer load");
        if(runtime.mission_index()==0)check(global(runtime,4)==0 && global(runtime,2)==0,
            "MAIN continuation and second initializer wait for the first actual termination");
        if(runtime.mission_index()==1)check(global(runtime,3)==20,
            "all original first-initializer assignments complete before replacement");
    }
    check(!runtime.script(root).thread.active && global(runtime,2)==99 && global(runtime,4)==77,
        "sequential original initializers complete across bounded frames");

    Code blocked;blocked.op(COMMAND_CREATE_CHAR);blocked.end();
    lcs::Runtime missing(image(main,{blocked,second}));const auto parent=missing.start_main();
    const auto events=missing.tick(0,0,2);
    check(events[0].result==lcs::StepResult::unsupported && missing.script(parent).awaiting_initial_slice,
        "an unsupported child keeps MAIN suspended at its launch");
    missing.tick(16,16,2);
    check(missing.mission_index()==0 && global(missing,2)==0 && global(missing,4)==0,
        "blocked child cannot be overwritten or followed by guessed initialization");
    missing.engine=[](lcs::Vm&,lcs::Thread&,uint16_t op){return op==COMMAND_CREATE_CHAR?lcs::StepResult::continued:lcs::StepResult::unsupported;};
    missing.resume(missing.script(parent).awaiting_initial_slice);
    for(uint32_t frame=2;frame<12 && missing.script(parent).thread.active;++frame)missing.tick(frame*16,16,2);
    check(!missing.script(parent).thread.active && global(missing,4)==77,"real child adapter completion releases the parent continuation");
    Code malformed;for(int n=0;n<8;++n)malformed.set_global(3,n);
    malformed.op(COMMAND_SET_VAR_INT);malformed.bytes.push_back(0xFF);
    lcs::Runtime failed(image(main,{malformed,second}));const auto failed_parent=failed.start_main();
    for(uint32_t frame=0;frame<20 && failed.script(failed_parent).thread.active;++frame)failed.tick(frame*16,16,2);
    const auto dependency=failed.script(failed_parent).awaiting_initial_slice;
    check(dependency && failed.script(dependency).result==lcs::StepResult::failed && failed.script(dependency).blocked &&
          failed.script(failed_parent).thread.active && failed.mission_index()==0 && global(failed,4)==0 &&
          failed.failure(dependency)->scope==lcs::FailureScope::initialization,
        "failed initializer remains quarantined; MAIN retains its exact launch dependency and cannot advance incomplete initialization");
}

void blocked_adapters() {
    Code code; code.op(COMMAND_SET_TOTAL_NUMBER_OF_MISSIONS); code.integer(7); code.end();
    lcs::Runtime runtime(image(code)); const auto id=runtime.start_script(64);
    const auto events=runtime.tick(0,0);
    check(events[0].result==lcs::StepResult::unsupported && runtime.script(id).blocked && runtime.script(id).thread.ip==64,
          "missing engine operation blocks thread without consuming opcode");
    check(runtime.tick(1,1).empty(),"blocked engine operation is not repeatedly executed");
    int total=0;
    runtime.engine=[&total](lcs::Vm& vm,lcs::Thread& thread,uint16_t opcode) {
        if (opcode!=COMMAND_SET_TOTAL_NUMBER_OF_MISSIONS) return lcs::StepResult::unsupported;
        total=vm.parameter(thread); return lcs::StepResult::continued;
    };
    runtime.resume(id); runtime.tick(1,0);
    check(total==7 && runtime.active_count()==0,"explicit resume retries the same original command through an adapter");
}

void time_and_fairness() {
    Code timer; timer.wait(10); timer.op(COMMAND_SET_VAR_INT_TO_LVAR_INT); timer.global(2); timer.bytes.push_back(10); timer.end();
    lcs::Runtime time(image(timer)); const auto id=time.start_main();
    time.tick(UINT32_MAX-4,4); time.tick(1,6);
    check(time.script(id).thread.active && time.script(id).thread.locals[104]==10,"LCS timer advances while wrapped WAIT holds");
    time.tick(5,4);
    check(time.active_count()==0 && global(time,2)==14,"WAIT resumes across 32-bit clock wrap");

    Code loop; loop.op(COMMAND_GOTO); loop.integer(64);
    const auto finite=loop.label(); loop.set_global(2,55); loop.end();
    lcs::Runtime fair(image(loop)); const auto infinite=fair.start_script(64); fair.start_script(finite);
    const auto events=fair.tick(0,0,20);
    check(events.size()==2 && global(fair,2)==55 && fair.script(infinite).thread.active && fair.active_count()==1,
          "instruction budget prevents one script from starving other scripts");
}
void isolate_export_service_failure() {
    Code main;main.name("main");main.op(COMMAND_DECLARE_MISSION_FLAG);main.global(4);main.set_global(4,1);main.wait(0);main.set_global(2,123);main.end();
    const auto worker_entry=main.label();main.name("import");main.op(COMMAND_SET_TOTAL_NUMBER_OF_MISSIONS);main.integer(7);main.end();
    Code mission;mission.name("vic2");mission.wait(0);mission.set_global(3,456);mission.end();
    lcs::Runtime runtime(image(main,{mission}));
    runtime.start_main();const auto worker=runtime.start_script(worker_entry);const auto story=runtime.load_mission(0);
    runtime.tick(0,0);
    check(runtime.can_isolate_failure(worker) && runtime.script(worker).blocked && runtime.script(worker).thread.active && runtime.on_mission(),"Independent export monitor remains blocked and resumable without clearing the story mission flag");
    const auto stopped_ip=runtime.script(worker).thread.ip;
    runtime.tick(1,1);
    check(global(runtime,2)==123 && global(runtime,3)==456 && !runtime.script(story).thread.active,"Story and main progression continue while optional export command is suspended");
    check(runtime.script(worker).thread.ip==stopped_ip && runtime.script(worker).thread.locals[104]==0,"Suspended worker cannot skip an instruction or repeatedly execute a failed command");
    const auto next_story=runtime.load_mission(0);runtime.tick(2,1);
    check(runtime.script(next_story).thread.waiting && runtime.can_isolate_failure(worker) && runtime.on_mission(),"Blocked main export monitor cannot prevent loading the next original mission buffer");
    runtime.engine=[](lcs::Vm& vm,lcs::Thread& t,uint16_t){vm.parameter(t);return lcs::StepResult::continued;};
    runtime.resume(worker);runtime.tick(3,1);
    check(!runtime.script(worker).thread.active && !runtime.can_isolate_failure(worker),"Installing an adapter resumes exactly the retained export instruction");
    Code fatal;fatal.name("import");fatal.op(COMMAND_SET_VAR_INT);fatal.global(2);fatal.bytes.push_back(255);
    lcs::Runtime corrupt(image(fatal));const auto broken=corrupt.start_script(64);corrupt.tick(0,0);
    check(corrupt.can_isolate_failure(broken) && !corrupt.failure(broken)->resumable,"Malformed worker is isolated and retained, but cannot be automatically replayed as a missing adapter");
    rejects([&](){corrupt.resume(broken);},"Malformed bytecode requires repair instead of unsafe replay");
    Code owned;owned.name("import");owned.op(COMMAND_SET_TOTAL_NUMBER_OF_MISSIONS);owned.integer(7);owned.end();
    lcs::Runtime resident(image(main,{owned}));const auto critical=resident.load_mission(0);resident.tick(0,0);
    check(!resident.can_isolate_failure(critical),"Same name inside mission buffer never permits story code to be bypassed");
    Code other;other.name("vic2");other.op(COMMAND_SET_TOTAL_NUMBER_OF_MISSIONS);other.integer(7);other.end();
    lcs::Runtime unreviewed(image(other));const auto unknown=unreviewed.start_script(64);unreviewed.tick(0,0);
    check(unreviewed.can_isolate_failure(unknown) && unreviewed.script(unknown).thread.ip==74,"Ordinary worker suspension is structural, independent of the chosen script name; its instruction is retained");
}

void structural_fault_domains() {
    Code main;main.name("main");main.op(COMMAND_DECLARE_MISSION_FLAG);main.global(4);main.set_global(4,1);
    main.wait(0);main.set_global(2,123);main.wait(0);main.set_global(2,234);main.end();
    std::vector<uint32_t> entries;
    for(const char* name:{"ambtaxi","import","future","vic2","unnamed"}) {
        entries.push_back(main.label());main.name(name);main.op(COMMAND_SET_TOTAL_NUMBER_OF_MISSIONS);main.integer(7);main.end();
    }
    const auto malformed=main.label();main.name("badcode");main.op(COMMAND_SET_VAR_INT);main.global(3);main.bytes.push_back(255);
    Code story;story.name("vic3");story.wait(0);story.set_global(3,456);story.end();
    lcs::Runtime runtime(image(main,{story}));const auto root=runtime.start_main();std::vector<lcs::ScriptId> workers;
    for(auto entry:entries)workers.push_back(runtime.start_script(entry));workers.push_back(runtime.start_script(malformed));
    const auto mission=runtime.load_mission(0);const auto events=runtime.tick(0,0);
    check(runtime.failures().size()==workers.size() && runtime.on_mission(),"Multiple ambient/decoder failures retain mission flag and every fault record");
    for(const auto id:workers)check(runtime.failure(id)->scope==lcs::FailureScope::thread && runtime.script(id).blocked,"Name-independent fault supervisor scopes every background worker locally");
    runtime.tick(1,1);runtime.tick(2,1);
    check(global(runtime,2)==234 && global(runtime,3)==456 && !runtime.script(mission).thread.active && !runtime.script(root).thread.active,
          "Root and story complete despite simultaneous optional failures; no global shutdown or synthetic mission completion");
    const auto next=runtime.load_mission(0);runtime.tick(3,1);
    check(runtime.script(next).thread.waiting && runtime.failures().size()==workers.size(),"Quarantined main workers cannot steal the next mission buffer");
    // A real story instruction remains paused, keeping its lease. Only its
    // explicit dependency chain waits; an unrelated world worker still executes.
    Code driver;driver.op(COMMAND_LOAD_AND_LAUNCH_MISSION_INTERNAL);driver.integer(0);driver.set_global(2,999);driver.end();
    const auto healthy=driver.label();driver.name("healthy");driver.set_global(3,333);driver.end();
    Code unsupported;unsupported.name("import");unsupported.op(COMMAND_SET_TOTAL_NUMBER_OF_MISSIONS);unsupported.integer(7);unsupported.set_global(4,1);unsupported.end();
    lcs::Runtime chain(image(driver,{unsupported}));const auto parent=chain.start_main();chain.start_script(healthy);chain.tick(0,0);
    const auto child=chain.script(parent).awaiting_initial_slice;const auto fault=chain.failure(child);
    check(fault && fault->scope==lcs::FailureScope::initialization && fault->mission_code_leased && fault->dependents==std::vector<lcs::ScriptId>{parent},
          "Immediate launch dependency determines scope without relying on script names");
    chain.tick(1,1);check(global(chain,2)==0 && global(chain,3)==333 && chain.mission_buffer_in_use(),"Healthy worker runs; initializer parent cannot skip its suspended child");
    chain.engine=[](lcs::Vm& vm,lcs::Thread& t,uint16_t){vm.parameter(t);return lcs::StepResult::continued;};chain.resume(child);chain.tick(2,1);
    check(chain.failures().empty() && global(chain,2)==999 && global(chain,4)==1,"Explicit adapter recovery releases original dependency and continues retained instruction");
    lcs::Runtime resident(image(main,{unsupported}));resident.declare_mission_flag(16);resident.vm().memory[16]=1;
    const auto owned=resident.load_mission(0);resident.tick(0,0);
    check(resident.failure(owned)->scope==lcs::FailureScope::mission && resident.on_mission(),"Resident mission suspension preserves real mission flag and cannot masquerade as an ambient failure");
    rejects([&](){resident.load_mission(0);},"Suspended mission cannot be overwritten by another mission");
    // An adapter throwing after a side effect is held rather than replayed.
    size_t effects=0;lcs::Runtime partial(image(unsupported));partial.start_main();
    partial.engine=[&](lcs::Vm&,lcs::Thread&,uint16_t)->lcs::StepResult{++effects;throw std::runtime_error("Partial adapter failure");};
    const auto partial_events=partial.tick(0,0);const auto partial_id=partial_events.front().id;
    partial.tick(1,1);check(effects==1 && partial.failure(partial_id) && !partial.failure(partial_id)->resumable,"Failed side effect is quarantined once and never automatically repeated");
    rejects([&](){partial.resume(partial_id);},"Partial side effect cannot be replayed through ordinary missing-adapter resume");
    // Original cleanup can retire a failed worker and reuse its slot before
    // the ASI consumes this tick's events. Its diagnostic must remain readable.
    Code cleanup;cleanup.name("cleanup");cleanup.op(COMMAND_TERMINATE_ALL_SCRIPTS_WITH_THIS_NAME);
    cleanup.bytes.insert(cleanup.bytes.end(),{'w','o','r','k','e','r',0,0});
    cleanup.op(COMMAND_START_NEW_SCRIPT);const auto entry=cleanup.target();cleanup.bytes.push_back(0);cleanup.end();
    const auto broken_entry=cleanup.label();cleanup.name("worker");cleanup.op(COMMAND_SET_TOTAL_NUMBER_OF_MISSIONS);cleanup.integer(7);cleanup.end();
    cleanup.patch(entry,int32_t(cleanup.label()));cleanup.name("healthy");cleanup.set_global(2,888);cleanup.end();
    lcs::Runtime retired(image(cleanup));retired.start_script(64);const auto retired_id=retired.start_script(broken_entry);
    const auto retired_events=retired.tick(0,0);
    check(retired_events.front().id==retired_id && retired_events.front().result==lcs::StepResult::unsupported &&
          retired_events.front().name=="worker" && !retired.failure(retired_id) && retired.failures().empty(),
          "Same-frame original cleanup/slot reuse preserves fault snapshot without expired-ID lookup or campaign abort");
    retired.tick(1,1);check(global(retired,2)==888,"Replacement after original cleanup still receives its original slice");
}

void supervised_frame_updates() {
    lcs::UpdateSupervisor supervisor;size_t failed_calls=0,healthy_calls=0;std::vector<std::string> log;
    supervisor.diagnostic_event=[&](const std::string& event){log.push_back(event);};
    bool recovered=false;
    auto update=[&](){++failed_calls;if(!recovered)throw std::runtime_error("Unavailable actor");return true;};
    check(!supervisor.run("peds",lcs::UpdateScope::auxiliary,0,"Ped maintenance",update) && log.size()==1,"Advisory update exception is retained and logged without escaping into campaign abort");
    check(supervisor.run("healthy",lcs::UpdateScope::auxiliary,0,"Healthy service",[&](){++healthy_calls;return true;}) && healthy_calls==1,"Independent service still executes while another fails");
    check(!supervisor.run("peds",lcs::UpdateScope::auxiliary,249,"Ped maintenance",update) && failed_calls==1,"Advisory retry is bounded");
    check(!supervisor.run("peds",lcs::UpdateScope::auxiliary,250,"Ped maintenance",update) && failed_calls==2 && log.size()==1,"Repeated same failure cannot spam log each frame");
    check(!supervisor.run("peds",lcs::UpdateScope::auxiliary,5000,"Ped maintenance",update) && log.size()==2,"Persistent advisory failure gets bounded diagnostics");
    recovered=true;check(supervisor.run("peds",lcs::UpdateScope::auxiliary,5250,"Ped maintenance",update) && supervisor.failures().empty() && log.back().find("recovered")!=std::string::npos,"Real recovery releases advisory suspension");
    Code code;code.set_global(2,777);code.end();lcs::Runtime runtime(image(code));const auto root=runtime.start_main();
    bool hud_ready=false;
    auto guarded_tick=[&](uint32_t now){if(supervisor.run("hud",lcs::UpdateScope::dependency,now,"HUD binding",[&](){return hud_ready;}))runtime.tick(now,16);};
    guarded_tick(UINT32_MAX-100);
    check(global(runtime,2)==0 && runtime.script(root).thread.ip==0 && supervisor.failures().at("hud").scope==lcs::UpdateScope::dependency,"Unavailable shared input holds original state instead of running with fake data or aborting");
    hud_ready=true;guarded_tick(148);check(global(runtime,2)==0,"Dependency retry survives timer wrap");
    guarded_tick(149);check(global(runtime,2)==777 && !runtime.script(root).thread.active && supervisor.failures().empty(),"Validated dependency recovery resumes exact original VM instruction");
    rejects([&](){supervisor.run("allocation",lcs::UpdateScope::auxiliary,0,"Allocation",[]()->bool{throw std::bad_alloc();});},"Shared allocation failure cannot be relabeled a recoverable actor update");
}

void pool_and_reuse() {
    Code code; code.wait(0); code.end();
    const auto controller=code.label(); code.op(COMMAND_SET_TOTAL_NUMBER_OF_MISSIONS); code.integer(1); code.end();
    lcs::Runtime pool(image(code));
    std::vector<lcs::ScriptId> ids;
    for (int i=0;i<128;++i) ids.push_back(pool.start_script(64));
    rejects([&] { pool.start_script(64); },"129th active thread cannot overrun the LCS pool");
    pool.terminate(ids[0]); const auto replacement=pool.start_script(64);
    check(replacement!=ids[0] && pool.active_count()==128,"reused slot gets a distinct script ID");
    rejects([&] { pool.script(ids[0]); },"expired ID cannot alias replacement thread");

    lcs::Runtime reused(image(code)); const auto victim=reused.start_script(64);
    const auto actor=reused.start_script(controller); lcs::ScriptId child=0;
    reused.engine=[&](lcs::Vm& vm,lcs::Thread& t,uint16_t opcode) {
        if (opcode!=COMMAND_SET_TOTAL_NUMBER_OF_MISSIONS) return lcs::StepResult::unsupported;
        vm.parameter(t); reused.terminate(victim); child=reused.start_script(64); return lcs::StepResult::continued;
    };
    const auto events=reused.tick(0,0);
    check(events.size()==1 && events[0].id==actor && child && reused.script(child).thread.ip==64,
          "replacement created during tick is not processed under an expired ready ID");

    lcs::Runtime self(image(code)); const auto self_id=self.start_script(controller); lcs::ScriptId self_child=0;
    self.engine=[&](lcs::Vm& vm,lcs::Thread& t,uint16_t opcode) {
        if (opcode!=COMMAND_SET_TOTAL_NUMBER_OF_MISSIONS) return lcs::StepResult::unsupported;
        vm.parameter(t); self.terminate(self_id); self_child=self.start_script(64); return lcs::StepResult::terminated;
    };
    const auto self_events=self.tick(0,0);
    check(self_events[0].id==self_id && self_child!=self_id && !self.script(self_id).thread.active &&
          self.script(self_child).thread.ip==64,"currently executing slot cannot be reused inside its backend callback");
}

void original_missions(const std::filesystem::path& path) {
    const auto original=lcs::ScmImage::load(path);
    for (size_t i=0;i<original.mission_offsets.size();++i) {
        lcs::Runtime runtime(original); const auto id=runtime.load_mission(i); const auto bytes=original.mission(i);
        check(runtime.script(id).thread.ip==original.main_size && runtime.vm().mission_size()==bytes.size(),"original mission relocates to common execution address");
        check(std::equal(bytes.begin(),bytes.end(),runtime.vm().memory.begin()+original.main_size),"original mission bytes survive loading unchanged");
    }
    std::cout<<"PASS: "<<original.mission_offsets.size()<<" original PS2 mission script blocks loaded unchanged (playability not tested)\n";
}
void mission_state_and_recovery() {
    Code code;code.op(COMMAND_DECLARE_MISSION_FLAG);code.global(2);code.set_global(2,1);
    code.op(COMMAND_LAUNCH_MISSION);const auto entry=code.target();code.end();
    code.patch(entry,int32_t(code.label()));code.op(COMMAND_GOSUB);const auto body=code.target();
    code.op(COMMAND_HAS_DEATHARREST_BEEN_EXECUTED);code.set_global(3,99);code.end();
    code.patch(body,int32_t(code.label()));code.wait(1000);code.op(COMMAND_RETURN);
    lcs::Runtime runtime(image(code));runtime.start_main();runtime.tick(0,0);
    const auto events=runtime.tick(0,0);const auto mission=events[0].id;
    check(runtime.on_mission() && runtime.script(mission).thread.waiting,"LCS global mission flag is separate from thread residency");
    const auto memory_address=runtime.vm().memory.data();
    check(runtime.notify_player_restart() && !runtime.on_mission(),"death/arrest clears the original on-mission flag");
    check(runtime.vm().memory.data()==memory_address,"recovery preserves VM variable identities borrowed by live services");
    check(!runtime.script(mission).thread.waiting && runtime.script(mission).thread.stack_size==0,"death/arrest unwinds recovery frame and cancels WAIT");
    runtime.tick(1,1);
    check(runtime.script(mission).thread.condition && global(runtime,3)==99 && !runtime.script(mission).thread.active,"original recovery code observes death/arrest execution");
    check(!runtime.notify_player_restart(),"recovery notification is not repeated after mission state clears");
    Code bad;bad.op(COMMAND_DECLARE_MISSION_FLAG);bad.local(0);bad.end();
    lcs::Runtime invalid(image(bad));invalid.start_main();
    check(invalid.tick(0,0)[0].result==lcs::StepResult::failed && !invalid.on_mission(),"local mission flag reference is rejected");
}
void recovery_rejects_unsafe_state_without_partial_cleanup() {
    auto launcher=[](Code& code,size_t& target) {
        code.op(COMMAND_DECLARE_MISSION_FLAG);code.global(2);code.set_global(2,1);
        code.op(COMMAND_LAUNCH_MISSION);target=code.target();code.end();
    };
    auto enter_mission=[](lcs::Runtime& runtime) {
        runtime.start_main();runtime.tick(0,0);const auto events=runtime.tick(0,0);
        check(events.size()==1,"local mission gets its own scheduler slice");
        return events[0].id;
    };

    {
        Code code;size_t target=0;launcher(code,target);const auto entry=code.label();
        code.wait(500);code.end();code.patch(target,int32_t(entry));
        lcs::Runtime runtime(image(code));const auto id=enter_mission(runtime);const auto before=runtime.script(id).thread;
        check(runtime.on_mission() && !before.stack_size && before.waiting,"mission without a return frame is active and waiting");
        check(!runtime.notify_player_restart() && runtime.on_mission() && global(runtime,2)==1,
              "missing recovery frame leaves the mission flag unchanged");
        const auto& after=runtime.script(id).thread;
        check(after.ip==before.ip && after.stack_size==before.stack_size && after.waiting,
              "missing recovery frame cannot partially mutate the mission thread");
    }
    {
        Code code;size_t target=0;launcher(code,target);const auto entry=code.label();
        code.op(COMMAND_SET_DEATHARREST_STATE);code.integer(0);code.wait(500);code.end();code.patch(target,int32_t(entry));
        lcs::Runtime runtime(image(code));const auto id=enter_mission(runtime);const auto before=runtime.script(id).thread;
        check(!before.death_arrest_enabled && before.waiting,"mission explicitly disabled death/arrest recovery");
        check(!runtime.notify_player_restart() && runtime.on_mission() && global(runtime,2)==1 &&
              runtime.script(id).thread.ip==before.ip && runtime.script(id).thread.waiting,
              "disabled death/arrest recovery preserves mission state");
    }
    {
        Code code;size_t target=0;launcher(code,target);const auto entry=code.label();
        code.op(COMMAND_GOSUB);const auto body=code.target();code.end();
        code.patch(body,int32_t(code.label()));code.wait(500);code.op(COMMAND_RETURN);code.patch(target,int32_t(entry));
        lcs::Runtime runtime(image(code));const auto id=enter_mission(runtime);
        auto& thread=const_cast<lcs::Thread&>(runtime.script(id).thread);
        check(thread.stack_size==1 && thread.waiting,"mission entered a valid plain recovery call");
        thread.stack[0]=UINT32_MAX;const auto before=thread;
        check(!runtime.notify_player_restart() && runtime.on_mission() && global(runtime,2)==1,
              "out-of-range recovery frame cannot clear the mission flag");
        check(thread.ip==before.ip && thread.stack_size==before.stack_size && thread.waiting &&
              thread.stack[0]==before.stack[0],"invalid recovery frame leaves the thread untouched");
    }
    {
        Code code;size_t target=0;launcher(code,target);const auto entry=code.label();
        code.op(COMMAND_CREATE_CAR);code.end();code.patch(target,int32_t(entry));
        lcs::Runtime runtime(image(code));const auto id=enter_mission(runtime);const auto before=runtime.script(id).thread;
        check(runtime.script(id).blocked && runtime.script(id).result==lcs::StepResult::unsupported,
              "unsupported mission command is quarantined");
        check(!runtime.notify_player_restart() && runtime.on_mission() && global(runtime,2)==1 &&
              runtime.script(id).blocked && runtime.script(id).thread.ip==before.ip,
              "death/arrest cannot replay a blocked mission command or discard its state");
    }
    for(const auto fault:{lcs::StepResult::unsupported,lcs::StepResult::failed}) {
        // A mission quarantined on a failed command still owns a valid original
        // recovery frame. Death/arrest unwinds to that code; it never replays the
        // failed instruction and never leaves the shared flag stuck at 1.
        Code code;size_t target=0;launcher(code,target);const auto entry=code.label();
        code.op(COMMAND_GOSUB);const auto body=code.target();
        const auto continuation=code.label();code.op(COMMAND_HAS_DEATHARREST_BEEN_EXECUTED);code.set_global(3,99);code.end();
        code.patch(body,int32_t(code.label()));code.op(COMMAND_CREATE_CAR);code.op(COMMAND_RETURN);code.patch(target,int32_t(entry));
        lcs::Runtime runtime(image(code));unsigned attempts=0;
        runtime.engine=[&](lcs::Vm&,lcs::Thread&,uint16_t) {
            ++attempts;
            if(fault==lcs::StepResult::failed)throw std::runtime_error("Injected entered backend fault");
            return fault;
        };
        const auto id=enter_mission(runtime);
        check(runtime.script(id).blocked && runtime.script(id).result==fault &&
              runtime.script(id).thread.stack_size==1,"quarantined mission still has its original recovery frame");
        check(runtime.notify_player_restart() && !runtime.on_mission() && global(runtime,2)==0 &&
              !runtime.script(id).blocked && runtime.script(id).thread.ip==continuation &&
              runtime.script(id).thread.death_arrest_executed,
              "death/arrest recovers a quarantined mission through its original recovery code");
        runtime.tick(1,1);
        check(global(runtime,3)==99 && !runtime.script(id).blocked && !runtime.script(id).thread.active && attempts==1,
              "quarantined command is abandoned, not replayed, after recovery");
    }
}
void function_frame_recovery_validation() {
    auto run=[&](bool corrupt) {
        Code code;size_t launch=0;
        code.op(COMMAND_DECLARE_MISSION_FLAG);code.global(2);code.set_global(2,1);
        code.op(COMMAND_LAUNCH_MISSION);launch=code.target();code.end();const auto entry=code.label();
        code.op(COMMAND_CALL);code.bytes.insert(code.bytes.end(),{0,0,0});const auto target=code.target();
        const auto continuation=code.label();code.wait(500);code.end();
        code.patch(target,int32_t(code.label()));code.wait(500);code.op(COMMAND_RETURN);
        code.patch(launch,int32_t(entry));
        lcs::Runtime runtime(image(code));runtime.start_main();runtime.tick(0,0);const auto events=runtime.tick(0,0);
        check(events.size()==1,"function mission gets its own scheduler slice");const auto id=events[0].id;
        auto& thread=const_cast<lcs::Thread&>(runtime.script(id).thread);
        check(thread.waiting && thread.stack_size==1 && (thread.stack[0]&(1u<<23)),"mission is waiting under a packed function frame");
        if(corrupt)thread.stack[0]^=(1u<<22);
        const auto before=thread;const bool resumed=runtime.notify_player_restart();
        if(corrupt) {
            check(!resumed && runtime.on_mission() && global(runtime,2)==1 && thread.stack[0]==before.stack[0] &&
                  thread.ip==before.ip && thread.stack_size==before.stack_size && thread.waiting,
                  "malformed packed function frame cannot partially recover");
        } else {
            check(resumed && !runtime.on_mission() && global(runtime,2)==0 && thread.ip==continuation &&
                  !thread.stack_size && !thread.waiting && thread.death_arrest_executed,
                  "valid packed function frame retains its original death/arrest return contract");
        }
    };
    run(false);run(true);
}
void actual_frame_time_step() {
    Code code;code.op(COMMAND_ADD_TIMED_VAL_TO_FLOAT_VAR);code.global(2);code.integer(0x40800000);code.end(); // delta 4
    lcs::Runtime runtime(image(code));runtime.start_main();unsigned samples=0;
    runtime.frame_time_step=[&](){++samples;return std::optional<float>(0.125f);};
    runtime.tick(100,17);
    check(global(runtime,2)==0x3f000000 && samples==1,"timed arithmetic uses actual fractional native timestep rather than integer milliseconds");
    lcs::Runtime paused(image(code));paused.start_main();paused.frame_time_step=[](){return std::optional<float>(0);};paused.tick(200,20);
    check(global(paused,2)==0,"native pause timestep cannot advance cinematic arithmetic");
    lcs::Runtime invalid(image(code));const auto id=invalid.start_main();invalid.frame_time_step=[](){return std::optional<float>();};
    rejects([&](){invalid.tick(100,20);},"missing native timestep cannot silently become a simulated frame");
    check(global(invalid,2)==0 && invalid.script(id).thread.ip==0,"missing timestep cannot run or write any mission instruction");
}
void native_effects_cannot_replay_after_entry() {
    Code code;const auto loop=code.label();code.op(COMMAND_ADD_VAL_TO_INT_VAR);code.global(2);code.integer(1);code.wait(0);code.op(COMMAND_GOTO);code.integer(loop);
    const auto worker=code.label();code.name("FACTORY");code.op(COMMAND_SET_LVAR_INT);code.local(0);code.integer(42);code.op(COMMAND_CREATE_OBJECT);code.integer(7);for(int i=0;i<3;++i)code.integer(0);code.local(0);code.set_global(3,77);code.end();
    const auto source=image(code);
    for(bool after_entry:{false,true}){
        lcs::Runtime runtime(source);lcs::NativeAdapter adapter(source);adapter.world_ready=true;adapter.bind_model(7,10000,"fixture_object");
        bool fail=true;size_t attempts=0,created=0;
        adapter.execute=lcs::guard_native_executor([&](const lcs::NativeCall& call){lcs::NativeResult result;result.executed=true;result.condition=true;
            if(call.opcode==0x0107){++attempts;if(fail){result.executed=false;result.native_entered=after_entry;result.diagnostic="Injected native factory fault";if(after_entry)++created;}
                else {++created;result.native_entered=true;result.values[0]=901;}}
            return result;});
        runtime.engine=[&](lcs::Vm& vm,lcs::Thread& thread,uint16_t opcode){return adapter.dispatch(vm,thread,opcode);};
        const auto root=runtime.start_main(),factory=runtime.start_script(worker);
        runtime.tick(0,16);const auto failure=runtime.failure(factory);
        check(failure && failure->scope==lcs::FailureScope::thread && failure->resumable==!after_entry,"Native entry receipt controls fault resumability without promoting an ordinary worker to campaign failure");
        check(runtime.script(factory).result==(after_entry?lcs::StepResult::failed:lcs::StepResult::unsupported) && runtime.script(factory).thread.locals[0]==42 && global(runtime,3)==0,
              "Rejected native factory keeps original output/instruction and cannot run the worker's following state change");
        const auto old_counter=global(runtime,2);for(uint32_t frame=1;frame<5;++frame)runtime.tick(frame*16,16);
        check(global(runtime,2)>old_counter && runtime.script(root).thread.active && attempts==1,"Scheduler continues independent work without replaying a quarantined native factory");
        fail=false;
        if(after_entry){rejects([&](){runtime.resume(factory);},"Native partial execution cannot become a resumable missing-command fault");
            check(created==1 && attempts==1 && runtime.script(factory).blocked,"Native side effect remains single after repair-only failure");}
        else {runtime.resume(factory);runtime.tick(100,16);check(created==1 && attempts==2 && global(runtime,3)==77 && !runtime.script(factory).thread.active,
              "A backend rejection before native entry can be explicitly resumed after fixing the backend");}
    }
}
}
int main(int argc,char** argv) {
    try {
        if (argc==2) { original_missions(argv[1]); return 0; }
        thread_arguments_and_order(); mission_loading_and_bounds(); mission_launch_timing_and_helpers();
        launch_local_mission_and_terminate_names(); blocked_adapters(); time_and_fairness(); pool_and_reuse();
        mission_state_and_recovery();recovery_rejects_unsafe_state_without_partial_cleanup();
        function_frame_recovery_validation();bounded_initializers_keep_original_order();
        actual_frame_time_step();
        native_effects_cannot_replay_after_entry();
        isolate_export_service_failure();
        structural_fault_domains();
        supervised_frame_updates();
        std::cout<<"PASS: LCS scheduler, thread arguments/order, mission loading/leases, timers, blocked adapters and pool reuse\n";
        return 0;
    } catch (const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
