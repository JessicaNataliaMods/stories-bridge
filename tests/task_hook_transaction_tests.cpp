#include "lcs/task_hook_transaction.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
#include <type_traits>
using namespace lcs;
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct Memory:TaskHookMemory,TaskHookPreparation {
    std::array<std::array<uint8_t,11>,task_hook_sites.size()> entries{};
    std::vector<uint8_t> published;
    uint32_t base=0x71230000;
    int fail_at=-1,writes=0,releases=0,allocations=0;
    bool publication_fail=false,allocation_fail=false,change_on_publish=false,uncertain=false,partial=false,rollback_fail=false,no_bytes_on_uncertain=false;
    bool prepared=false,preparation_fail=false,tail_changed=false;int attachments=0,detachments=0,suspensions=0;
    std::array<uint32_t,11> prepared_targets{};
    bool attach(const std::array<uint32_t,11>& value)noexcept override {++attachments;prepared_targets=value;prepared=true;return !preparation_fail;}
    void detached()noexcept override{++detachments;prepared=false;}
    void suspended()noexcept override{++suspensions;}
    std::optional<TaskHookSite> alt; // live release entry owned by another reviewed module (index 8)
    TaskHookSite site(size_t i)const{return alt&&i==8?*alt:task_hook_sites[i];}
    Memory(){for(size_t i=0;i<entries.size();++i)entries[i]=site(i).prefix;}
    bool read(uint32_t address,uint8_t* bytes,size_t length)override {
        if(address>=base && uint64_t(address-base)+length<=published.size()) {
            std::memcpy(bytes,published.data()+address-base,length);return true;
        }
        for(size_t i=0;i<entries.size();++i)if(address==site(i).address && length==site(i).length) {
            std::memcpy(bytes,entries[i].data(),length);return true;
        }
        for(size_t i=0;i<entries.size();++i)if(address==site(i).address+site(i).length && length<=16) {
            std::memset(bytes,tail_changed?0xCD:0xCC,length);return true;
        }
        return false;
    }
    uint32_t allocate(size_t length)override{++allocations;check(length==entries.size()*32,"Bounded trampoline bank");return allocation_fail?0:base;}
    bool publish(uint32_t address,const uint8_t* bytes,size_t length)override {
        check(address==base,"Publish allocated bank");published.assign(bytes,bytes+length);
        if(change_on_publish)entries.back()[0]^=1;
        return !publication_fail;
    }
    Write exchange(uint32_t address,const uint8_t* expected,const uint8_t* desired,size_t length)override {
        check(!published.empty() && !publication_fail && prepared,"Native code is never redirected before executable trampolines and ALL callback targets are published");
        for(size_t i=0;i<entries.size();++i)if(address==site(i).address) {
            check(length==site(i).length,"Whole reviewed prefix");
            if(std::memcmp(entries[i].data(),expected,length))return Write::unchanged;
            bool installing=desired[0]==0xE9;
            if(!installing && rollback_fail)return Write::unchanged;
            if(installing && writes++==fail_at) {
                if(uncertain) {if(!no_bytes_on_uncertain)std::memcpy(entries[i].data(),desired,partial?2:length);return Write::uncertain;}
                return Write::unchanged;
            }
            std::memcpy(entries[i].data(),desired,length);return Write::written;
        }
        return Write::unchanged;
    }
    void release(uint32_t address)override{check(address==base,"Release own allocation");++releases;}
    bool original()const{for(size_t i=0;i<entries.size();++i)if(entries[i]!=site(i).prefix)return false;return true;}
};
int main(){try{
    static_assert(!std::is_copy_constructible_v<TaskHookTransaction> && !std::is_move_constructible_v<TaskHookTransaction>,"Resident hook ownership must have a single owner");
    std::array<uint32_t,task_hook_sites.size()> callbacks{};for(size_t i=0;i<callbacks.size();++i)callbacks[i]=0x12340000+uint32_t(i*64);
    {Memory m;TaskHookTransaction tx;check(tx.install(m,callbacks,m) && tx.state()==TaskHookTransaction::State::installed,"All eleven observer entries install together");
     const auto site=task_hook_sites[8];
     check(tx.owns_original_prefix(m,site.address,site.prefix.data(),site.length),"An installed release hook authenticates its reviewed original prefix");
     auto wrong=site.prefix;wrong[0]^=1;
     check(!tx.owns_original_prefix(m,site.address,wrong.data(),site.length),"A redirect does not authorize a different original ABI");
     m.published[8*32]^=1;
     check(!tx.owns_original_prefix(m,site.address,site.prefix.data(),site.length),"Changed trampoline bytes invalidate original-prefix authorization");m.published[8*32]^=1;
     m.entries[8][1]^=1;
     check(!tx.owns_original_prefix(m,site.address,site.prefix.data(),site.length),"A foreign replacement of our entry is rejected");m.entries[8][1]^=1;
     std::array<uint8_t,10> extended{};std::memcpy(extended.data(),site.prefix.data(),site.length);std::memset(extended.data()+site.length,0xCC,4);
     check(tx.owns_original_prefix(m,site.address,extended.data(),extended.size()),"Unmodified bytes beyond the replaced prefix are still verified");
     m.tail_changed=true;check(!tx.owns_original_prefix(m,site.address,extended.data(),extended.size()),"A changed release body beyond our hook is rejected");m.tail_changed=false;
     check(!tx.install(m,callbacks,m) && m.allocations==1,"No repeated installation or concurrent second allocation");
     check(tx.remove(m) && m.original() && m.releases==1 && !tx.trampoline(0),"Complete removal restores original bytes before freeing code");
     check(!tx.owns_original_prefix(m,site.address,site.prefix.data(),site.length),"A removed transaction cannot authorize an entry");}
    {   // The Open Limit Adjuster owns the live free path: the retail entry is a jump, so only an explicit, reviewed
        // replacement of the release entry may install; everything else stays retail.
        const TaskHookSite ola{0x100E7060u,6,{{0x55,0x8B,0xEC,0xFF,0x75,0x08}}};
        {Memory m;m.alt=ola;m.entries[8]=ola.prefix;TaskHookTransaction tx;
         check(!tx.install(m,callbacks,m) && !m.allocations && !m.writes,"A foreign release entry without a host replacement refuses the whole observer");}
        {Memory m;m.alt=ola;m.entries[8]=ola.prefix;TaskHookTransaction tx;
         TaskHookSite bad=ola;bad.length=7;
         check(!tx.install(m,callbacks,m,bad) && !m.allocations,"Only a six-byte straight-line release prologue can replace the entry");}
        {Memory m;m.alt=ola;m.entries[8]=ola.prefix;TaskHookTransaction tx;
         check(tx.install(m,callbacks,m,ola),"The replaced release entry installs");check(m.entries[8][0]==0xE9,"The live release prologue is redirected");check(m.writes==11,"Eleven entries written");
         check(tx.owns_entries(m) && tx.remove(m) && m.original(),"Removal restores the live module's own prologue");}
        {Memory m;m.alt=ola;m.entries[8]=ola.prefix;m.entries[8][0]^=1;TaskHookTransaction tx;
         check(!tx.install(m,callbacks,m,ola) && !m.allocations,"A modified live release prologue still refuses");}
    }
    for(size_t i=0;i<callbacks.size();++i){Memory m;TaskHookTransaction tx;m.entries[i][0]^=1;
        check(!tx.install(m,callbacks,m) && !m.allocations && !m.writes,"Any foreign native entry refuses the whole observer before allocation/writes");}
    for(int i=0;i<int(callbacks.size());++i){Memory m;TaskHookTransaction tx;m.fail_at=i;
        check(!tx.install(m,callbacks,m) && tx.state()==TaskHookTransaction::State::empty && m.original() && m.releases==1,"Every definite patch failure rolls back previous entries and frees only unreachable code");}
    for(bool partial:{false,true}){Memory m;TaskHookTransaction tx;m.fail_at=5;m.uncertain=true;m.partial=partial;
        check(!tx.install(m,callbacks,m) && tx.state()==TaskHookTransaction::State::resident_failure && !m.releases && tx.trampoline(5),"Partial write or uncertain cache/protection keeps callback code resident without overwriting unknown bytes");
        check(m.prepared && m.suspensions==1 && !m.detachments,"Resident partial detour retains all native targets but disables observer ownership");
        for(size_t i=0;i<11;++i)check(m.prepared_targets[i]==m.base+i*32,"Every continuation already configured before any partial native entry redirect");
    }
    {Memory m;TaskHookTransaction tx;m.fail_at=5;m.uncertain=true;m.no_bytes_on_uncertain=true;
     check(!tx.install(m,callbacks,m) && m.original() && tx.state()==TaskHookTransaction::State::resident_failure && !m.releases,"Original bytes alone do not prove safe cache/protection after an uncertain host operation");}
    {Memory m;TaskHookTransaction tx;m.fail_at=5;m.rollback_fail=true;
     check(!tx.install(m,callbacks,m) && tx.state()==TaskHookTransaction::State::resident_failure && !m.releases,"Rollback failure cannot free a still reachable trampoline");}
    {Memory m;TaskHookTransaction tx;check(tx.install(m,callbacks,m),"Setup foreign removal case");auto before=m.entries;m.entries[8][0]^=1;before=m.entries;
     check(!tx.remove(m) && m.entries==before && !m.releases,"Foreign replacement refuses removal without touching unrelated installed entries");}
    for(int mode=0;mode<3;++mode){Memory m;TaskHookTransaction tx;m.allocation_fail=mode==0;m.publication_fail=mode==1;m.change_on_publish=mode==2;
        check(!tx.install(m,callbacks,m) && !m.writes && tx.state()==TaskHookTransaction::State::empty,"Allocation/publication failure or changed preflight never patches native entries");}
    {Memory m;TaskHookTransaction tx;m.preparation_fail=true;
     check(!tx.install(m,callbacks,m) && !m.writes && m.original() && m.attachments==1 && m.detachments==1 && m.releases==1,"Callback preparation failure clears observation and refuses every native entry before patching");}
    for(uint32_t base:{uint32_t(0x60832F),uint32_t(0xFFFFFFF0),callbacks[5]-10}){Memory m;TaskHookTransaction tx;m.base=base;
        check(!tx.install(m,callbacks,m) && !m.writes && m.releases==1,"Whole bank overlap with native entry/callback or address wrap refuses installation");}
    {Memory m;TaskHookTransaction tx;auto bad=callbacks;bad[0]=task_hook_sites[7].address+1;
     check(!tx.install(m,bad,m) && !m.writes,"Callback cannot overlap another native entry, not just its own");}
    std::cout<<"PASS: complete observer transaction, all failure positions, foreign detours, executable publication and resident rollback failure; simulated memory only\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
