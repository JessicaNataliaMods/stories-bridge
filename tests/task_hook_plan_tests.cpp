#include "lcs/task_hook_plan.hpp"
#include <iostream>
#include <stdexcept>
using namespace lcs;
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static uint32_t target(const uint8_t* jump,uint32_t address){uint32_t delta=0;std::memcpy(&delta,jump+1,4);return address+5u+delta;}
int main(){try{
    for(size_t i=0;i<task_hook_sites.size();++i) {
        const auto& site=task_hook_sites[i];const auto original=site.prefix;
        for(uint32_t callback:{uint32_t(0x12340000),uint32_t(0xF2340000)}) {
            const auto plan=plan_task_hook(i,callback,0x71230000,site.prefix.data(),site.length);
            check(plan && plan->entry_size==site.length && plan->trampoline_size==site.length+5 &&
                target(plan->entry.data(),site.address)==callback &&
                target(plan->trampoline.data()+site.length,0x71230000+site.length)==site.address+site.length,
                "Every reviewed whole-instruction prologue reaches its callback and original continuation with unsigned rel32 arithmetic");
            check(std::memcmp(plan->trampoline.data(),original.data(),site.length)==0 && site.prefix==original,"Planning preserves all native instructions and never writes source memory");
        }
        for(size_t byte=0;byte<site.length;++byte) {
            auto changed=original;changed[byte]^=1;
            check(!plan_task_hook(i,0x12340000,0x71230000,changed.data(),site.length),"Every foreign/modified prologue byte refuses planning");
        }
        check(!plan_task_hook(i,0x12340000,0x71230000,original.data(),site.length-1),"Truncated profile cannot split an instruction");
        check(!plan_task_hook(i,site.address+1,0x71230000,original.data(),site.length),"Callback cannot fall into overwritten entry bytes");
        check(!plan_task_hook(i,0x12340000,site.address-2,original.data(),site.length),"Trampoline beginning before the native entry still cannot overlap its copied bytes");
        check(!plan_task_hook(i,0x71230002,0x71230000,original.data(),site.length),"Callback and generated trampoline cannot overlap");
        check(!plan_task_hook(i,0x12340000,UINT32_MAX,original.data(),site.length),"Trampoline address cannot wrap its allocation range");
    }
    check(!plan_task_hook(task_hook_sites.size(),1,2,nullptr,0),"Only reviewed native mutation/free sites are accepted");
    std::cout<<"PASS: eleven bounded task hook plans, exact native prefixes, whole instructions and rel32 wrap; no hook installed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
