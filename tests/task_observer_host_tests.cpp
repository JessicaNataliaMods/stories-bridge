#include "lcs/task_observer_host.hpp"
#include <iostream>
#include <stdexcept>
using namespace lcs;
static bool abi=true,game_thread=true,excluded=false;
static bool profile(){return abi;}static bool engine_thread(){return game_thread;}static bool exclusion(){return excluded;}
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct Memory:TaskHookMemory {
    std::array<std::array<uint8_t,11>,11> entries{};
    int allocations=0,releases=0,writes=0,fail_at=-1;
    bool published=false,partial=false;
    Memory(){for(size_t i=0;i<11;++i)entries[i]=task_hook_sites[i].prefix;}
    bool read(uint32_t address,uint8_t* out,size_t length)override {
        for(size_t i=0;i<11;++i)if(address==task_hook_sites[i].address && length==task_hook_sites[i].length){std::memcpy(out,entries[i].data(),length);return true;}
        return false;
    }
    uint32_t allocate(size_t length)override{check(excluded && length==352,"Trampoline allocation only within actual host exclusion scope");++allocations;return 0x71230000;}
    bool publish(uint32_t,const uint8_t*,size_t)override{check(excluded,"Publish while excluded");published=true;return true;}
    Write exchange(uint32_t address,const uint8_t* expected,const uint8_t* desired,size_t length)override {
        check(excluded && published,"Native patch never outside host exclusion");
        for(size_t i=0;i<11;++i)if(address==task_hook_sites[i].address) {
            if(std::memcmp(entries[i].data(),expected,length))return Write::unchanged;
            if(desired[0]==0xE9) {
                check(task_hook_callbacks_observing(),"All real callbacks attached before first native entry change");
                if(writes++==fail_at){if(partial){std::memcpy(entries[i].data(),desired,2);return Write::uncertain;}return Write::unchanged;}
            }
            std::memcpy(entries[i].data(),desired,length);return Write::written;
        }
        return Write::unchanged;
    }
    void release(uint32_t)override{check(excluded && !task_hook_callbacks_observing(),"Observer disabled before detached allocation release");++releases;}
};
int main(){try{
    Memory m;TaskObserverHost host(m,profile,engine_thread,exclusion);
    check(!host.install() && !m.allocations && !host.active(),"No installation without host exclusion proof");
    excluded=true;game_thread=false;check(!host.install() && !m.allocations,"Wrong thread cannot install native observer");game_thread=true;
    abi=false;check(!host.install() && !m.allocations,"ABI mismatch refuses installation");abi=true;
    check(host.install() && host.active() && m.writes==11,"Complete host installs all prepared callbacks and owns every native entry");
    {TaskObserverHost unused(m,profile,engine_thread,exclusion);check(!unused.install(),"Second host refuses already-owned entries before callback reconfiguration");}
    check(host.active(),"Destroying an unattached host cannot detach another host's observer");
    game_thread=false;check(!host.active() && !host.remove(),"Foreign thread cannot inspect observer containers or tear down native entries");game_thread=true;
    check(host.active(),"A foreign read-only health query does not invent a native mutation");
    game_thread=false;check(!host.begin(2,0xC0B1E8,0x1234),"Actual foreign mutation callback cannot access observer containers");
    host.end(123,false);host.inline_flush(0xC0B1E8);host.observation_lost();game_thread=true;
    check(!host.active(),"Foreign-thread observation loss remains quarantined after returning to the engine thread, even with identical native hooks");
    check(host.remove() && host.install() && host.active(),"Explicit excluded detach/reattach starts a new observation lifetime after foreign mutation");
    excluded=false;check(!host.remove() && host.active(),"Removal without exclusion cannot partly tear down observer");excluded=true;
    auto installed=m.entries;m.entries[9][0]^=1;
    check(!host.active(),"Any foreign entry replacement invalidates the complete observer");m.entries=installed;
    check(!host.active(),"Restoring same-byte hooks cannot revive receipts after missed callbacks");
    check(host.remove() && host.state()==TaskHookTransaction::State::empty && m.releases==2 && !task_hook_callbacks_observing(),"Explicit teardown detaches native entries and observer before freeing code");
    m.writes=0;check(host.install() && host.active(),"Fresh complete installation can start a new observation lifetime");
    check(host.remove(),"Second clean teardown");
    {Memory refused;refused.fail_at=5;TaskObserverHost failed(refused,profile,engine_thread,exclusion);
     check(!failed.install() && !failed.active() && !task_hook_callbacks_observing() && refused.releases==1,"Definite install failure rolls back and detaches complete host");}
    {Memory resident;resident.fail_at=5;resident.partial=true;TaskObserverHost failed(resident,profile,engine_thread,exclusion);
     check(!failed.install() && failed.state()==TaskHookTransaction::State::resident_failure && !failed.active() && !task_hook_callbacks_observing() && !resident.releases,"Partial failure keeps native code resident but cannot claim observation");}
    check(!task_hook_callbacks_observing(),"Resident failure destruction does not publish a new observer");
    std::cout<<"PASS: concrete host joins callbacks/preparation/transaction/reader/sink, exact ownership, engine-thread/exclusion gates and latched health loss; model memory only, no native entries executed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
