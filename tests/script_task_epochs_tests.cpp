#include "lcs/script_task_epochs.hpp"
#include <iostream>
#include <stdexcept>
using namespace lcs;
static void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
int main(){try{
    ScriptTaskEpochs epochs;const ScriptTaskRecord event{0x05D3,0x1111,0,0x2222,0};
    CapturedScriptTask binding{127,event};
    auto store=[&](size_t slot,const ScriptTaskRecord& record){const auto mutation=epochs.replacing(slot);return mutation && epochs.replaced(*mutation,record);};
    check(!epochs.replaced({127,0},event),"An after-only poll is not a synchronous observed replacement");
    check(!epochs.capture(binding,event),"A polled record without observed mutation cannot manufacture an epoch");
    check(store(127,event),"Last original record observes an actual native replacement");
    const auto owned=epochs.capture(binding,event);check(bool(owned),"Accepted command plus observed record yields a reuse token");
    check(!epochs.replaced({127,owned->epoch},event),"Repeated after-notification cannot bypass the invalidating entry notification");
    ScriptTaskRecord task{0x05D3,0,0x3333,0x2222,1};
    check(!epochs.associated(*owned,127,event,task,0x4444),"Association needs its genuine task argument, not inferred disappearance");
    auto associated=epochs.associated(*owned,127,event,task,0x3333);
    check(associated && epochs.still_owned(*associated,task) && !epochs.still_owned(*owned,task),"Exact event-to-task notification preserves epoch and replaces only its binding");
    auto active=task;active.status=2;
    check(epochs.still_owned(*associated,active),"Known status-only movement retains identity without declaring completion");
    for(int status:{-1,3,99}) {
        active.status=status;check(!epochs.still_owned(*associated,active),"Unknown/flushed status with contradictory live pointers cannot retain ownership");
    }
    const auto replacement=epochs.replacing(127);
    check(replacement && !epochs.still_owned(*associated,task),"Inline Set invalidates before it writes any replacement bytes");
    epochs.replaced(*replacement,task);
    check(!epochs.still_owned(*associated,task),"Identical task/ped/opcode/pointer bytes after reuse cannot resurrect an old job");
    auto fresh=epochs.capture({127,task},task);
    check(fresh && fresh->epoch!=associated->epoch && epochs.still_owned(*fresh,task),"New accepted receipt must capture its new epoch explicitly");
    auto foreign=task;foreign.task^=1;
    check(!epochs.still_owned(*fresh,foreign),"Unobserved foreign binding mismatch cannot count as owned movement");
    store(0,event);
    check(epochs.still_owned(*fresh,task),"Unrelated actor slot does not invalidate a real owned job");
    epochs.reset();check(!epochs.still_owned(*fresh,task) && !epochs.capture({127,task},task),"Native bank initialization invalidates old epochs without treating empty storage as completed");
    check(!epochs.replacing(128) && !epochs.replaced({128,1},event) && !epochs.capture({128,event},event),"Notification slot bounds are enforced");
    const auto outer=epochs.replacing(127),inner=epochs.replacing(127);
    check(outer && inner && epochs.replaced(*inner,task) && !epochs.capture({127,task},task),"Nested callbacks cannot publish ownership while an outer mutation is still writing");
    check(epochs.replaced(*outer,task),"Matching outer scope can commit after the real nested mutation");
    const auto nested_done=epochs.capture({127,task},task);
    check(nested_done && nested_done->epoch>inner->epoch && !epochs.replaced(*outer,task),"Outer writes retire the inner epoch and a finished scope cannot be replayed");
    for(int i=0;i<32;++i)check(bool(epochs.replacing(0)),"Bounded synchronous nesting has 32 mutation slots");
    check(!epochs.replacing(0) && !epochs.capture({0,event},event),"Excessive nesting disables ownership instead of recycling epoch identities");
    ScriptTaskEpochs lifetime;
    auto publish=[&](size_t slot,const ScriptTaskRecord& record){auto m=lifetime.replacing(slot);check(m && lifetime.replaced(*m,record),"Observed replacement");auto token=lifetime.capture({slot,record},record);check(bool(token),"Accepted receipt captures observed replacement");return *token;};
    auto queued=publish(4,event),unrelated=publish(5,task);
    check(!lifetime.observe_event_task(queued,event,0x9999,0xABCD) &&
        !lifetime.observe_event_task(queued,event,event.event,0),"Queued task dependency requires the genuine live event and non-null owned task");
    check(lifetime.observe_event_task(queued,event,event.event,0xABCD),"Reviewed event snapshot records its exact owned task");
    lifetime.retiring_task(0);lifetime.retiring_event(0);lifetime.retiring_task(0x1234);
    check(lifetime.still_owned(queued,event) && lifetime.still_owned(unrelated,task),"Null or unrelated release does not invalidate another actor's job");
    lifetime.retiring_task(0xABCD);
    check(!lifetime.still_owned(queued,event) && !lifetime.capture({4,event},event) &&
        lifetime.still_owned(unrelated,task),"Queued task release invalidates its own binding before reuse, not every queued objective");
    queued=publish(4,event);lifetime.retiring_event(event.event);
    check(!lifetime.still_owned(queued,event) && !lifetime.capture({4,event},event),"Event destruction cannot resurrect ownership through same-pointer reuse");
    lifetime.retiring_task(task.task);
    check(!lifetime.still_owned(unrelated,task),"Associated task release invalidates its binding even before the bank notices the missing task");
    queued=publish(4,event);
    check(lifetime.observe_event_task(queued,event,event.event,0xABCD) &&
        !lifetime.observe_event_task(queued,event,event.event,0xABCE) && !lifetime.still_owned(queued,event),"Unobserved event task replacement retires ownership rather than adopting foreign task");
    queued=publish(4,event);lifetime.inline_flush(4);lifetime.inline_flush(128);
    check(!lifetime.still_owned(queued,event) && !lifetime.capture({4,event},event),"Process's inlined Flush retires ownership without manufacturing a replacement record");
    queued=publish(4,event);
    check(lifetime.observe_event_task(queued,event,event.event,task.task),"Queue dependency recorded before native association");
    auto linked=lifetime.associated(queued,4,event,task,task.task);
    check(linked && lifetime.still_owned(*linked,task),"Actual association transfers identity to its native task");
    lifetime.retiring_event(event.event);
    check(lifetime.still_owned(*linked,task),"Deleting a consumed event after association cannot cancel the task now owned by the actor");
    lifetime.retiring_task(task.task);
    check(!lifetime.still_owned(*linked,task),"The transferred task's actual deletion still retires its binding");
    auto reused=publish(4,task);
    check(reused.epoch!=linked->epoch && lifetime.still_owned(reused,task) && !lifetime.still_owned(*linked,task),"Same-address allocation needs a newly observed assignment; old task token stays retired");
    std::cout<<"PASS: observed task mutation epochs, association, same-byte reuse and bank reset; no completion/runtime support inferred\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
