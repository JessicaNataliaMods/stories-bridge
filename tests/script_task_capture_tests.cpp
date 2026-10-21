#include "lcs/script_task_capture.hpp"
#include <iostream>
#include <stdexcept>
using namespace lcs;
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(){try{
    ScriptTaskSnapshot before{},after{};
    check(!capture_accepted_script_task(before,after,0x1000,0x5D3),"Empty record bank cannot fabricate task ownership");
    after[127]={0x5D3,0x2000,0,0x1000,0};
    auto captured=capture_accepted_script_task(before,after,0x1000,0x5D3);
    check(captured && captured->slot==127 && captured->record==after[127],"Last actual record captures original event without off-by-one loss");
    check(!capture_accepted_script_task(before,after,0x1001,0x5D3) && !capture_accepted_script_task(before,after,0x1000,0x5D4),"Ped and translated native opcode must match accepted command");
    check(!capture_accepted_script_task(after,after,0x1000,0x5D3),"Existing matching native event cannot be claimed by new source objective");
    before=after;after[127]={0x5D3,0,0x3000,0x1000,1};
    captured=capture_accepted_script_task(before,after,0x1000,0x5D3);
    check(!captured,"Association of a previously existing event cannot claim new task ownership without its association receipt");
    before={};captured=capture_accepted_script_task(before,after,0x1000,0x5D3);
    check(captured && captured->record.task==0x3000,"New direct task binding in a previously empty record may be captured");
    before=after;after[127].status=2;
    check(!capture_accepted_script_task(before,after,0x1000,0x5D3),"Movement of pre-existing task into PRIMARY is not a new accepted task");
    before={};after={};after[3]={0x5D3,0,0x3000,0x1000,1};after[4]={0x5D3,0,0x3001,0x1000,1};
    check(!capture_accepted_script_task(before,after,0x1000,0x5D3),"Multiple new matches stay ambiguous instead of borrowing another mod's task");
    after[4].ped=0x4000;captured=capture_accepted_script_task(before,after,0x1000,0x5D3);
    check(captured && captured->slot==3,"Unrelated native actor records do not interfere with owned capture");
    for(int status:{-1,3,7,99}) {
        after[3].status=status;check(!capture_accepted_script_task(before,after,0x1000,0x5D3),"Absent/flushed/unknown status cannot create ownership");
    }
    for(int status:{1,2,4,5,6}) {
        after[3].status=status;check(bool(capture_accepted_script_task(before,after,0x1000,0x5D3)),"Valid direct native task status is captured without interpreting it as completion");
    }
    after[3].task=0;check(!capture_accepted_script_task(before,after,0x1000,0x5D3),"Task status with missing task pointer is malformed");
    after[3].status=0;after[3].event=0x2000;after[3].task=0x3000;
    check(!capture_accepted_script_task(before,after,0x1000,0x5D3),"New event binding with inconsistent old task refuses capture");
    after[3].task=0;captured=capture_accepted_script_task(before,after,0x1000,0x5D3);
    check(captured && captured->slot==3,"Single new event binds without a native script-pointer assumption");
    check(!capture_accepted_script_task(before,after,0,-1),"Null ped/wildcard opcode cannot claim every scripted record");
    const auto owned=*captured;auto associated=owned.record;associated.event=0;associated.task=0x8888;associated.status=1;
    auto task=associate_captured_event(owned,3,owned.record,associated,0x8888);
    check(task && task->record==associated,"Observed native association preserves the original owned event's task identity");
    check(!associate_captured_event(owned,4,owned.record,associated,0x8888),"Association callback in another slot cannot bind owned event");
    check(!associate_captured_event(owned,3,owned.record,associated,0x9999),"Actual task argument must equal native field written by association");
    for(int field=0;field<4;++field) {
        auto altered=associated;
        if(field==0)altered.ped^=1;if(field==1)altered.opcode^=1;if(field==2)altered.event=0x7777;if(field==3)altered.status=2;
        check(!associate_captured_event(owned,3,owned.record,altered,0x8888),"Foreign/ambiguous association cannot retain source ownership");
    }
    auto replaced_event=owned.record;replaced_event.event^=1;
    check(!associate_captured_event(owned,3,replaced_event,associated,0x8888),"Reused record's new event does not inherit old accepted receipt");
    auto primary=task->record;primary.status=2;ScriptTaskRecord flushed{};
    check(flushes_captured_binding(*task,3,primary,flushed),"Flush retires owned binding even after native status changed to primary");
    auto replaced_task=primary;replaced_task.task^=1;
    check(!flushes_captured_binding(*task,3,replaced_task,flushed) && !flushes_captured_binding(*task,4,primary,flushed),"Foreign task/slot cleanup cannot retire another binding");
    auto incomplete_flush=flushed;incomplete_flush.task=0x8888;
    check(!flushes_captured_binding(*task,3,primary,incomplete_flush),"Partially changed record is not a verified native flush");
    std::cout<<"PASS: accepted task record diff, native layout/bounds, identity, ambiguity and non-completion semantics; engine snapshot provider pending\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
