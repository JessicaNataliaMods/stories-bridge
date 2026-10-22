// Exercise the production callback binding/forwarding code with private fake
// RenderWare state. Never load or execute GTA or access a graphics device.
#include <cstdint>
static std::uintptr_t test_engine=0;
static unsigned shutdown_nodes_freed=0;
static void test_shutdown_free(std::uintptr_t,bool){++shutdown_nodes_freed;}
#define LCS_WORLD_SHUTDOWN_NODE_FREE test_shutdown_free
static void* __cdecl test_each_atomic(void*,void*(__cdecl*)(void*,void*),void*);
#define LCS_MAP_CULLING_FIXTURE test_engine
#define LCS_CLUMP_ATOMIC_ITERATOR test_each_atomic
#include "../src/sa_world_preview.cpp"
#include <cstring>
#include <iostream>

namespace {
unsigned state=2,draws=0,secondary_draws=0;
void* expected=nullptr;
void* nested=nullptr;
void check(bool ok){if(!ok)throw std::runtime_error("Native map atomic culling regression");}
int __cdecl get_state(int key,void* value) {
    check(key==20);*static_cast<unsigned*>(value)=state;return 1;
}
int __cdecl set_state(int key,void* value) {
    check(key==20);state=unsigned(reinterpret_cast<uintptr_t>(value));return 1;
}
void* __cdecl draw_first(void* atomic) {
    check(atomic==expected && state==1);++draws;
    if(nested) {
        const auto callback=*reinterpret_cast<lcs::AtomicRender*>(static_cast<unsigned char*>(nested)+0x48);
        check(callback(nested)==nested && state==1);
    }
    state=3;return atomic;
}
void* __cdecl draw_second(void* atomic) {
    check(state==1);++secondary_draws;state=2;return atomic;
}
struct FakeAtomic {
    alignas(4) unsigned char bytes[0x70]{};
    explicit FakeAtomic(lcs::AtomicRender callback) {bytes[0]=1;set(callback);}
    void set(lcs::AtomicRender callback){std::memcpy(bytes+0x48,&callback,sizeof(callback));}
    lcs::AtomicRender callback() const {lcs::AtomicRender fn;std::memcpy(&fn,bytes+0x48,sizeof(fn));return fn;}
};
struct FakeSkinClump {
    unsigned char type=2;
    void* atomics[2]{};
};
}
static void* __cdecl test_each_atomic(void* clump,void*(__cdecl* callback)(void*,void*),void* data) {
    for(const auto atomic:static_cast<FakeSkinClump*>(clump)->atomics)if(atomic && !callback(atomic,data))break;
    return clump;
}
int main() {
    // Exercise the production pointer-list unlinker with authored nodes, not
    // game memory. Removing duplicate owned nodes must preserve foreign nodes
    // and repair both forward and backward links before native deallocation.
    {
        uintptr_t a[3]{1,0,0},b[3]{9,0,0},c[3]{1,0,0};
        a[1]=reinterpret_cast<uintptr_t>(b);b[1]=reinterpret_cast<uintptr_t>(c);
        b[2]=reinterpret_cast<uintptr_t>(a);c[2]=reinterpret_cast<uintptr_t>(b);
        uintptr_t head=reinterpret_cast<uintptr_t>(a);
        check(lcs::repair_shutdown_link(reinterpret_cast<uintptr_t>(&head),false,1));
        check(head==reinterpret_cast<uintptr_t>(b) && b[1]==0 && b[2]==0 && shutdown_nodes_freed==2);
        uintptr_t single[2]{1,0};head=reinterpret_cast<uintptr_t>(single);
        check(lcs::repair_shutdown_link(reinterpret_cast<uintptr_t>(&head),true,1));
        check(head==0 && shutdown_nodes_freed==3);
    }
    static_assert(sizeof(void*)==4,"Native RW profile is Win32");
    alignas(4) unsigned char device[0x30]{};
    try {
        const auto engine=reinterpret_cast<uintptr_t>(device);test_engine=engine;
        *reinterpret_cast<lcs::StateFunction*>(engine+0x20)=set_state;
        *reinterpret_cast<lcs::StateFunction*>(engine+0x24)=get_state;
        FakeAtomic atomic(draw_first),second(draw_second),stock(draw_first);
        expected=atomic.bytes;
        check(lcs::bind_atomic_culling(reinterpret_cast<uintptr_t>(atomic.bytes)));
        const auto installed=atomic.callback();check(installed!=draw_first);
        check(lcs::bind_atomic_culling(reinterpret_cast<uintptr_t>(atomic.bytes)) && atomic.callback()==installed);
        check(stock.callback()==draw_first);
        for(unsigned initial:{1u,2u,3u}) {
            state=initial;check(atomic.callback()(atomic.bytes)==atomic.bytes && state==initial);
        }
        check(draws==3);
        check(lcs::bind_atomic_culling(reinterpret_cast<uintptr_t>(second.bytes)));
        nested=second.bytes;state=2;
        check(atomic.callback()(atomic.bytes)==atomic.bytes && state==2);
        check(draws==4 && secondary_draws==1);
        // Streaming/new-game reuse must bind the current callback instead of
        // relying on an old association with the same atomic address.
        nested=nullptr;atomic.set(draw_second);
        check(lcs::bind_atomic_culling(reinterpret_cast<uintptr_t>(atomic.bytes)));
        state=3;check(atomic.callback()(atomic.bytes)==atomic.bytes && state==3);
        check(secondary_draws==2 && lcs::no_cull_draws==6 && !lcs::culling_failed);
        check(lcs::bind_atomic_culling(0));
        FakeAtomic clump(draw_first);clump.bytes[0]=2;
        check(lcs::bind_atomic_culling(reinterpret_cast<uintptr_t>(clump.bytes)) && clump.callback()==draw_first);
        FakeAtomic skin(draw_second),skin_second(draw_second),unrelated(draw_second);
        FakeSkinClump ped;ped.atomics[0]=skin.bytes;ped.atomics[1]=skin_second.bytes;
        check(lcs::bind_sa_skin_double_sided(reinterpret_cast<uintptr_t>(&ped)));
        check(skin.callback()!=draw_second && skin_second.callback()!=draw_second && unrelated.callback()==draw_second);
        state=2;check(skin.callback()(skin.bytes)==skin.bytes && state==2);
        state=3;check(skin_second.callback()(skin_second.bytes)==skin_second.bytes && state==3);
        const auto skin_callback=skin.callback();
        check(lcs::bind_sa_skin_double_sided(reinterpret_cast<uintptr_t>(&ped)) && skin.callback()==skin_callback);
        skin.set(draw_first);expected=skin.bytes;
        check(lcs::bind_sa_skin_double_sided(reinterpret_cast<uintptr_t>(&ped)));
        state=2;check(skin.callback()(skin.bytes)==skin.bytes && state==2);
        check(!lcs::bind_sa_skin_double_sided(0) && !lcs::bind_sa_skin_double_sided(reinterpret_cast<uintptr_t>(unrelated.bytes)));
        FakeSkinClump empty;check(!lcs::bind_sa_skin_double_sided(reinterpret_cast<uintptr_t>(&empty)));
        check(!lcs::culling_failed);
        std::cout<<"PASS: native atomic callback offset/ABI, forwarding, road cull override, state restoration, nested drawing and streamed address reuse\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
