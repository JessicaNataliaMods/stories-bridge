#define LCS_ISOLATED_CORONA_TEST
#include "../src/sa_script_coronas.cpp"
#include <iostream>
#include <stdexcept>
namespace {
size_t calls=0;bool cull=false,raise=false;
void check(bool c,const char* m){if(!c)throw std::runtime_error(m);}
void set32(uintptr_t address,uint32_t v){std::memcpy(reinterpret_cast<void*>(address),&v,4);}
}
namespace lcs {
bool sa_executor_profile_matches(){return true;}
namespace {
float __cdecl mock_corona_ground(float x,float y){check(x==10 && y==20,"Real two-dimensional ground ABI receives original XY");return 88;}
void __cdecl mock_register_corona(uint32_t id,void* attach,uint8_t r,uint8_t g,uint8_t b,uint8_t alpha,
    const CoronaVector& position,float radius,float far_clip,uint8_t type,uint8_t flare,bool reflect,bool obstacles,
    int unused,float angle,bool long_distance,float near_clip,uint8_t fade,float speed,bool below,bool delay){
    ++calls;
    check(!attach && alpha==255 && far_clip==450 && type==2 && flare==0 && reflect && !obstacles && !unused &&
        angle==0 && !long_distance && near_clip==1.5f && fade==0 && speed==15 && !below && !delay,
        "Exact reviewed native21-operand corona ABI/defaults");
    if(cull)return;
    const uintptr_t entry=corona_pool;
    std::memcpy(reinterpret_cast<void*>(entry),&position,12);set32(entry+0xC,id);
    std::memcpy(reinterpret_cast<void*>(entry+0x14),&radius,4);
    *reinterpret_cast<uint8_t*>(entry+0x2C)=r;*reinterpret_cast<uint8_t*>(entry+0x2D)=g;*reinterpret_cast<uint8_t*>(entry+0x2E)=b;
    if(raise)RaiseException(0xE0434343,0,0,nullptr);
}
}
}
int main(){try{
    uint8_t entry[]={0x83,0xEC,0x1C,0x55,0x56,0x8B,0x74,0x24,0x2C,0x85,0xF6,0x57};
    uint8_t lookup[]={0x0F,0xB6,0x54,0x24,0x28,0x8B,0x04,0x95,0,0xE0,0xC3,0};
    std::array<uint8_t,64*0x3C> data{};
    lcs::corona_register_entry=reinterpret_cast<uintptr_t>(entry);
    lcs::corona_lookup_entry=reinterpret_cast<uintptr_t>(lookup);
    lcs::corona_pool=reinterpret_cast<uintptr_t>(data.data());
    uint8_t ground_entry[]={0x83,0xEC,0x38,0x8B,0x44,0x24,0x3C,0x8B,0x4C,0x24,0x40};
    lcs::mock_corona_ground_entry=ground_entry;
    check(lcs::sa_script_corona_ground(10,20)==88,"Original ground request uses actual XY native helper, no invented Z plane");
    ground_entry[0]=0xE9;
    check(!lcs::sa_script_corona_ground(10,20),"Modified ground ABI refuses before native entry");
    lcs::ScriptCorona source;source.position={10,20,30};source.size=2.5;source.type=2;source.r=10;source.g=20;source.b=30;
    check(lcs::sa_script_coronas_available() && lcs::sa_register_script_corona(0x123456,source) && calls==1,"Real backend accepts isolated renderer receipt");
    check(lcs::sa_register_script_corona(0x123456,source) && calls==2,"Matching live emitter refreshes its owned ID");
    set32(lcs::corona_pool+0xC,0x223456);
    check(!lcs::sa_register_script_corona(0x223456,source) && calls==2,"Existing foreign ID never enters renderer");
    set32(lcs::corona_pool+0xC,0x123456);set32(lcs::corona_pool,0);
    check(!lcs::sa_register_script_corona(0x123456,source) && calls==2,"Changed foreign pose refuses ownership before effects");
    entry[0]=0xE9;
    check(!lcs::sa_register_script_corona(0x323456,source) && calls==2,"Later native hook is preserved and never called through stale ABI");
    entry[0]=0x83;
    data.fill(0);cull=true;
    check(lcs::sa_register_script_corona(0x423456,source) && calls==3,"Native culling/full pool is processed without fabricating rendered visibility");
    cull=false;raise=true;
    check(!lcs::sa_register_script_corona(0x523456,source) && calls==4,"Entered native fault is observable");raise=false;
    check(!lcs::sa_register_script_corona(0x523456,source) && calls==4,"Partially entered renderer is quarantined without replay");

    std::cout<<"PASS: production corona backend ABI, foreign ownership, native culling and entered-failure quarantine; no game/render device executed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
