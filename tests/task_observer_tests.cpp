#include "lcs/task_observer.hpp"
#include <iostream>
#include <stdexcept>
using namespace lcs;
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct Reader:TaskObserverReader {
    ScriptTaskSnapshot bank{};bool live=true,readable=true,event_readable=true;
    uint32_t event=0x1234,task=0x5678;
    bool healthy()const override{return live;}
    bool record(size_t slot,ScriptTaskRecord& out)override{if(!readable || slot>=128)return false;out=bank[slot];return true;}
    bool event_task(uint32_t value,uint32_t& out)override{if(!event_readable || value!=event)return false;out=task;return true;}
};
static uint32_t address(size_t slot){return 0xC0B1E8+uint32_t(slot*0x14);}
int main(){try{
    Reader reader;ScriptTaskObserver observer(reader);
    const ScriptTaskRecord event{0x05D3,reader.event,0,0xABC0,0};
    const ScriptTaskRecord task{0x05D3,0,reader.task,0xABC0,1};
    auto write=[&](size_t slot,const ScriptTaskRecord& value){auto token=observer.begin(2,address(slot),value.event);check(bool(token),"Observed Set entry");reader.bank[slot]=value;observer.end(token,true);};
    reader.bank[127]=event;
    check(!observer.accept({127,event}),"Poll alone cannot own a native bank entry");
    write(127,event);
    auto owned=observer.accept({127,event});check(owned && observer.refresh(*owned),"Single accepted executor capture owns an observed last bank slot");
    auto association=observer.begin(7,address(127),reader.task);
    check(association && !observer.refresh(*owned),"Ownership is unavailable inside the association's native writes");
    reader.bank[127]=task;observer.end(association,true);
    auto linked=observer.refresh(*owned);check(linked && linked->binding.record.task==reader.task && linked->epoch==owned->epoch,"Observed exact event association refreshes original receipt without replacing epoch");
    observer.begin(10,reader.event,0);check(observer.refresh(*linked).has_value(),"Consumed event destructor does not retire the transferred actor task");
    observer.begin(8,reader.task,0);check(!observer.refresh(*linked) && !observer.accept({127,task}),"Task pool release retires ownership before same-address reuse");
    write(127,task);auto reused=observer.accept({127,task});check(reused && reused->epoch!=linked->epoch && !observer.refresh(*linked),"New native assignment can accept a reused address without resurrecting old receipt");
    observer.inline_flush(address(127));check(!observer.refresh(*reused),"Actual inline Process Flush retires exact slot");
    write(1,event);auto queued=observer.accept({1,event});check(bool(queued),"Event dependency accepted");
    write(2,task);auto other=observer.accept({2,task});check(bool(other),"Unrelated task accepted");
    observer.begin(10,reader.event,0);
    check(!observer.refresh(*queued) && observer.refresh(*other),"Event release invalidates only its queued job");
    write(1,event);queued=observer.accept({1,event});observer.begin(8,reader.task,0);
    check(!observer.refresh(*queued) && !observer.refresh(*other),"Same task release invalidates both genuine associated and queued dependencies");
    write(1,event);queued=observer.accept({1,event});
    association=observer.begin(7,address(1),reader.task+1);reader.bank[1]=task;observer.end(association,true);
    check(!observer.refresh(*queued),"Unmatched association argument never adopts a foreign task");
    write(1,event);queued=observer.accept({1,event});
    association=observer.begin(7,address(1),reader.task);write(1,event);reader.bank[1]=task;observer.end(association,true);
    check(!observer.refresh(*queued) && !observer.accept({1,task}),"Nested same-byte replacement invalidates old event association proof");
    auto failed=observer.begin(3,address(2),reader.task);reader.bank[2]=task;observer.end(failed,false);
    check(!observer.accept({2,task}),"Native abnormal return does not publish an accepted replacement");
    write(2,task);other=observer.accept({2,task});reader.live=false;
    check(!observer.refresh(*other) && !observer.accept({2,task}),"Callback health loss gates all existing/new actor receipts");reader.live=true;
    reader.event_readable=false;write(1,event);check(!observer.accept({1,event}),"Unreadable or wrong-vtable event cannot infer queued task ownership");reader.event_readable=true;
    check(!observer.begin(2,address(128),0) && !observer.begin(2,address(0)+1,0),"Foreign or unaligned records are never native bank slots");
    write(2,task);other=observer.accept({2,task});observer.reset();check(!observer.refresh(*other) && !observer.accept({2,task}),"Reset retires ownership even with unchanged live record bytes");
    auto obsolete=observer.begin(2,address(2),0);reader.bank[2]=task;observer.end(obsolete,true);observer.end(obsolete,true);
    check(!observer.accept({2,task}) && !observer.begin(2,address(2),0),"Repeated after-notification disables ambiguous observation rather than manufacturing a task");
    {   // release evidence: the owner's verdict at the native task-release notification
        Reader r;ScriptTaskObserver o(r);int verdict=ScriptTaskRelease::arrived;bool probe_runs=true;int calls=0;
        o.set_release_probe([&](const EpochScriptTask& t)->std::optional<int>{++calls;check(t.binding.record.status==1 && t.binding.record.task==r.task,"The probe sees the associated receipt");
            return probe_runs?std::optional<int>(verdict):std::nullopt;});
        const ScriptTaskRecord ev{0x05D9,r.event,0,0xABC0,0},tk{0x05D9,0,r.task,0xABC0,1};
        auto set=[&](size_t slot,const ScriptTaskRecord& v){auto t=o.begin(2,address(slot),v.event);r.bank[slot]=v;o.end(t,true);};
        auto assoc=[&](size_t slot){auto t=o.begin(7,address(slot),r.task);r.bank[slot]=tk;o.end(t,true);};
        set(5,ev);auto issued=o.accept({5,ev});check(bool(issued),"Receipt issued as the queued event");
        o.begin(8,r.task,0);
        check(calls==0 && !o.released(*issued),"A task freed before the event was associated leaves no release evidence and never runs the probe");
        set(5,ev);issued=o.accept({5,ev});assoc(5);
        o.begin(8,r.task,0);
        const auto rel=o.released(*issued);
        check(calls==1 && rel && rel->probed && rel->verdict==ScriptTaskRelease::arrived,"A release of the associated task records the probe verdict, found through the receipt as first issued");
        o.inline_flush(address(5));
        check(o.released(*issued).has_value() && !o.refresh(*issued),"The flush that follows the release keeps the evidence while ownership stays retired");
        set(5,ev);
        check(o.released(*issued).has_value(),"A later foreign writer does not erase evidence bound to the retired receipt");
        auto fresh=o.accept({5,ev});check(fresh && !o.released(*issued) && !o.released(*fresh),"A new receipt on the recycled slot starts without release evidence and the old one no longer matches");
        assoc(5);verdict=ScriptTaskRelease::not_arrived;o.begin(8,r.task,0);
        const auto far=o.released(*fresh);check(far && far->probed && far->verdict==ScriptTaskRelease::not_arrived,"The verdict is read again for every release");
        set(6,ev);auto second=o.accept({6,ev});assoc(6);probe_runs=false;o.begin(8,r.task,0);
        const auto blind=o.released(*second);check(blind && !blind->probed,"An unreadable probe still reports that SA released the task, without a verdict");
        probe_runs=true;
        set(7,ev);auto third=o.accept({7,ev});assoc(7);r.bank[7].ped=0xDEAD;o.begin(8,r.task,0);
        check(!o.released(*third),"A bank record that no longer matches the receipt produces no evidence");
        r.live=false;check(!o.released(*second),"Evidence is withheld while the callback health is lost");r.live=true;
        set(8,ev);auto fourth=o.accept({8,ev});assoc(8);o.begin(8,r.task+1,0);
        check(!o.released(*fourth) && o.refresh(*fourth).has_value(),"Releasing an unrelated task changes nothing");
        o.reset();check(!o.released(*issued) && !o.released(*second),"Reset clears every release");
    }
    Reader nested_reader;ScriptTaskObserver nested(nested_reader);
    for(int i=0;i<128;++i)check(nested.begin(2,address(size_t(i)),0)!=0,"Bounded callback frames available");
    check(!nested.begin(2,address(0),0),"Exhausted callback frames disable observation rather than overwrite active scopes");
    std::cout<<"PASS: callback sink to mutation epochs, accepted capture, exact association, releases and their probe evidence, inline Flush, nesting, bounds and health gate; no SCM support granted\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
