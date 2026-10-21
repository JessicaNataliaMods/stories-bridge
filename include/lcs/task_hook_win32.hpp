#pragma once
#include "task_hook_transaction.hpp"

namespace lcs {
// Explicit host gate: construction does not assert that the game is quiescent.
// The runtime integration must supply its own verified exclusion scope. No
// default gate exists and this backend never suspends arbitrary mod threads.
class TaskHookWin32Memory final:public TaskHookMemory {
    bool (*quiescent_)()=nullptr;
    uint32_t allocation_=0;
    size_t allocation_size_=0;
public:
    explicit TaskHookWin32Memory(bool (*quiescent)()):quiescent_(quiescent){}
    TaskHookWin32Memory(const TaskHookWin32Memory&)=delete;
    TaskHookWin32Memory& operator=(const TaskHookWin32Memory&)=delete;
    bool read(uint32_t address,uint8_t* bytes,size_t length)override;
    uint32_t allocate(size_t length)override;
    bool publish(uint32_t address,const uint8_t* bytes,size_t length)override;
    Write exchange(uint32_t address,const uint8_t* expected,const uint8_t* desired,size_t length)override;
    void release(uint32_t address)override;
    // No destructor releases storage which a live callback might still reach.
};
}
