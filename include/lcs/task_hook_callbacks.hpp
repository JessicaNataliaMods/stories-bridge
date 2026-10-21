#pragma once
#include "task_hook_transaction.hpp"
namespace lcs {
// Observations only: no ownership, completion, SCM execution or return override.
struct TaskHookSink {
    virtual ~TaskHookSink()=default;
    virtual uint64_t begin(size_t site,uint32_t self,uint32_t argument)=0;
    virtual void end(uint64_t token,bool returned)=0;
    virtual void inline_flush(uint32_t record)=0;
    virtual void observation_lost()noexcept{}
};
// Configure only under host exclusion. Resident failures retain this module.
bool configure_task_hook_callbacks(const std::array<uint32_t,11>& trampolines,TaskHookSink* sink);
std::array<uint32_t,11> task_hook_callback_addresses();
// Every controller must gate ownership on this health signal. A notification
// failure invalidates observation for ALL actors until explicit reattachment.
bool task_hook_callbacks_observing();
bool task_hook_callbacks_attached_to(const TaskHookSink* sink);
void suspend_task_hook_observation()noexcept;
class TaskHookCallbackPreparation final:public TaskHookPreparation {
    TaskHookSink* sink_;
public:
    explicit TaskHookCallbackPreparation(TaskHookSink* sink):sink_(sink){}
    bool attach(const std::array<uint32_t,11>& trampolines)noexcept override {
        return configure_task_hook_callbacks(trampolines,sink_);
    }
    void detached()noexcept override{if(task_hook_callbacks_attached_to(sink_))suspend_task_hook_observation();}
    void suspended()noexcept override{if(task_hook_callbacks_attached_to(sink_))suspend_task_hook_observation();}
};
}
