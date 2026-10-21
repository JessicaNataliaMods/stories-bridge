#include "lcs/runtime.hpp"
#include "lcs/campaign_checkpoint.hpp"
#include "ScriptCommands.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool ok,const char* message) { if(!ok)throw std::runtime_error(message); }
template<class F> void rejects(F action,const char* message) {
    bool rejected=false;
    try { action(); } catch(const std::exception&) { rejected=true; }
    check(rejected,message);
}
struct Code {
    std::vector<uint8_t> bytes;
    uint32_t label() const { return uint32_t(64+bytes.size()); }
    void op(uint16_t value) { bytes.push_back(uint8_t(value));bytes.push_back(uint8_t(value>>8)); }
    void integer(int32_t value) {
        bytes.push_back(6);
        for(unsigned i=0;i<4;++i)bytes.push_back(uint8_t(uint32_t(value)>>(8*i)));
    }
    void global(uint8_t index) { bytes.push_back(204);bytes.push_back(index); }
    void local(uint8_t index) { bytes.push_back(uint8_t(12+index)); }
    void set_global(uint8_t index,int32_t value) { op(COMMAND_SET_VAR_INT);global(index);integer(value); }
    void wait(int32_t duration) { op(COMMAND_WAIT);integer(duration); }
    void name(const std::string& value) {
        op(COMMAND_SCRIPT_NAME);
        for(size_t i=0;i<8;++i)bytes.push_back(i<value.size()?uint8_t(value[i]):0);
    }
    void end() { op(COMMAND_TERMINATE_THIS_SCRIPT); }
};
lcs::ScmImage image(const Code& main,const std::vector<Code>& missions={}) {
    check(main.bytes.size()<448,"synthetic MAIN fits its allocation");
    lcs::ScmImage result;result.main_size=512;result.variable_space=64;result.entry_point=64;
    result.true_globals=4;result.most_globals=10;result.bytes.resize(result.main_size);
    result.bytes[0]=uint8_t(COMMAND_GOTO);result.bytes[2]=6;result.bytes[3]=64;
    std::copy(main.bytes.begin(),main.bytes.end(),result.bytes.begin()+64);
    for(const auto& mission:missions) {
        result.mission_offsets.push_back(uint32_t(result.bytes.size()));
        result.largest_mission=std::max(result.largest_mission,uint32_t(mission.bytes.size()));
        result.bytes.insert(result.bytes.end(),mission.bytes.begin(),mission.bytes.end());
    }
    return result;
}
uint32_t global(const lcs::Runtime& runtime,uint8_t index) {
    return lcs::read32(runtime.vm().memory,4u*index);
}

void roundtrip_globals_threads_mission_and_waits() {
    Code main;main.name("ROOT");main.set_global(2,1111);
    main.op(COMMAND_SET_LVAR_INT);main.local(0);main.integer(77);
    main.op(COMMAND_SET_VAR_INT_TO_LVAR_INT);main.global(3);main.bytes.push_back(10); // timer A
    main.wait(50);
    main.op(COMMAND_SET_VAR_INT_TO_LVAR_INT);main.global(4);main.local(0);
    main.op(COMMAND_SET_VAR_INT_TO_LVAR_INT);main.global(5);main.bytes.push_back(10);
    main.end();
    const auto worker_entry=main.label();
    main.name("WORKER");main.op(COMMAND_SET_LVAR_INT);main.local(1);main.integer(999);
    main.op(COMMAND_SET_VAR_INT_TO_LVAR_INT);main.global(6);main.local(1);main.wait(30);main.end();
    Code mission;mission.wait(25);mission.end();
    const auto source_image=image(main,{mission});

    lcs::Runtime source(source_image);
    const auto root=source.start_main();
    const auto worker=source.start_script(worker_entry);
    const auto mission_thread=source.load_mission(0);
    const auto first=source.tick(100,16);
    check(first.size()==3 && first[0].id==mission_thread && first[1].id==worker && first[2].id==root,
          "source scheduler starts mission, worker and MAIN in order");
    check(global(source,2)==1111 && global(source,3)==16 && source.script(root).thread.locals[0]==77 &&
          source.script(worker).thread.locals[1]==999,"source state includes globals and independent locals");
    check(source.script(root).thread.waiting && source.script(worker).thread.waiting &&
          source.script(mission_thread).thread.waiting,"all source threads are suspended on WAIT");
    const auto saved=source.save_state();

    lcs::Runtime restored(source_image);restored.restore_state(saved,1000);
    check(restored.mission_index()==0 && restored.vm().mission_size()==mission.bytes.size() &&
          std::equal(mission.bytes.begin(),mission.bytes.end(),restored.vm().memory.begin()+restored.vm().main_size),
          "loaded mission identity and code survive restore");
    check(restored.active_count()==3 && restored.script(root).thread.locals[0]==77 &&
          restored.script(worker).thread.locals[1]==999 && restored.script(root).thread.locals[104]==16,
          "thread slots, locals, timers and activity survive restore");
    check(restored.script(mission_thread).thread.wake_time==1025 &&
          restored.script(worker).thread.wake_time==1030 && restored.script(root).thread.wake_time==1050,
          "WAIT deadlines are rebased to the loaded game timer");
    check(global(restored,2)==1111 && global(restored,3)==16 && global(restored,6)==999,
          "global values survive restore without replaying MAIN");

    const auto waiting=restored.tick(1024,24);
    check(waiting.size()==3 && waiting[0].id==mission_thread && waiting[1].id==worker && waiting[2].id==root &&
          global(restored,4)==0,"restored scheduler order is retained while WAITs remain pending");
    restored.tick(1025,1);
    check(!restored.script(mission_thread).thread.active && restored.active_count()==2,
          "restored mission thread resumes at its saved instruction");
    restored.tick(1029,4);restored.tick(1030,1);
    check(!restored.script(worker).thread.active && restored.script(root).thread.active,
          "restored worker resumes at its own saved deadline");
    restored.tick(1049,19);
    check(global(restored,4)==0,"MAIN remains asleep until its rebased deadline");
    restored.tick(1050,1);
    check(!restored.script(root).thread.active && global(restored,4)==77 && global(restored,5)==66,
          "MAIN continues after WAIT with its saved local and timer state");
}

void resident_auxiliary_initialization_chunk_is_not_a_story_mission() {
    Code main;main.end();
    Code init_sav;init_sav.name("INITSAV");init_sav.set_global(2,456);init_sav.wait(50);init_sav.end();
    const auto source_image=image(main,{init_sav});

    lcs::Runtime source(source_image);
    const auto loader=source.load_mission(0);
    source.terminate(loader); // Keep its code chunk mounted, but not its story thread.
    const auto initializer=source.start_script(source.vm().main_size);
    const auto started=source.tick(100,16);
    check(started.size()==1 && started[0].id==initializer && source.script(initializer).thread.waiting,
          "INITSAV auxiliary starts and suspends inside the loaded mission chunk");
    check(source.script(initializer).resident && !source.script(initializer).mission &&
          !source.on_mission() && source.mission_buffer_in_use(),
          "resident INITSAV leases mission code without marking a story mission active");

    const auto saved=source.save_state();
    lcs::Runtime restored(source_image);restored.restore_state(saved,1000);
    check(restored.mission_index()==0 && restored.mission_buffer_in_use() && !restored.on_mission() &&
          restored.script(initializer).resident && !restored.script(initializer).mission &&
          restored.script(initializer).thread.wake_time==1050 && global(restored,2)==456,
          "resident INITSAV lease, globals and rebased WAIT survive snapshot without becoming a story mission");
    restored.tick(1049,49);
    check(restored.script(initializer).thread.active,"restored INITSAV remains asleep before its rebased deadline");
    restored.tick(1050,1);
    check(!restored.script(initializer).thread.active && global(restored,2)==456,
          "restored INITSAV finishes at its saved instruction without replaying MAIN");
}

Code suspended_main(int32_t value) {
    Code main;main.set_global(2,value);main.wait(100);main.end();return main;
}
void rejects_other_scm_atomically() {
    const auto image_a=image(suspended_main(11));
    const auto image_b=image(suspended_main(22));
    lcs::Runtime source(image_a);source.start_main();source.tick(10,16);
    const auto saved=source.save_state();

    lcs::Runtime target(image_b);const auto target_id=target.start_main();target.tick(200,16);
    const auto before=target.save_state();
    rejects([&]{target.restore_state(saved,900);},"save from a different SCM image is rejected");
    check(target.save_state()==before && global(target,2)==22 && target.script(target_id).thread.active,
          "SCM identity failure leaves the destination runtime unchanged");
}

void rejects_corruption_and_truncation_atomically() {
    const auto source_image=image(suspended_main(31));
    lcs::Runtime source(source_image);source.start_main();source.tick(30,16);
    const auto saved=source.save_state();

    lcs::Runtime target(source_image);const auto id=target.start_main();target.tick(400,16);
    const auto before=target.save_state();
    auto corrupted=saved;corrupted[corrupted.size()/2]^=0x80;
    rejects([&]{target.restore_state(corrupted,800);},"modified save bytes fail checksum validation");
    check(target.save_state()==before && target.script(id).thread.active,
          "checksum failure leaves the destination runtime unchanged");
    auto truncated=saved;truncated.pop_back();
    rejects([&]{target.restore_state(truncated,800);},"truncated save bytes are rejected");
    check(target.save_state()==before && global(target,2)==31,
          "truncation failure leaves the destination runtime unchanged");
}
}

int main(int argc,char** argv) {
    try {
        if(argc!=1&&argc!=3)throw std::runtime_error("Usage: runtime-save-tests [native-save original-MAIN.SCM]");
        if(argc==3){
            const auto checkpoint=lcs::read_campaign_checkpoint(argv[1]);
            lcs::Runtime actual(lcs::ScmImage::load(argv[2]));
            actual.restore_state(checkpoint.runtime,checkpoint.time);
            check(!actual.save_state().empty(),"Actual user VM checkpoint did not restore");
            std::cout<<"PASS: actual paired save VM restored against original SCM; no scripts executed\n";
        }
        roundtrip_globals_threads_mission_and_waits();
        resident_auxiliary_initialization_chunk_is_not_a_story_mission();
        rejects_other_scm_atomically();
        rejects_corruption_and_truncation_atomically();
        std::cout<<"PASS: LCS runtime save-state roundtrip, identity, corruption, truncation and atomic restore\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;
    }
}
