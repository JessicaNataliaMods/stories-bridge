#include <cstdint>
static std::uintptr_t fixture_pool=0;
static std::uintptr_t fixture_resolve(std::int32_t handle);
#define LCS_GAMEPLAY_SKIN_POOL_FIXTURE fixture_pool
#define LCS_GAMEPLAY_SKIN_PED_RESOLVER fixture_resolve
#include "../src/sa_gameplay_skins.cpp"
#include <cstring>
#include <iostream>
namespace {
alignas(4) unsigned char ped_a[0x30]{},ped_b[0x30]{},ped_c[0x30]{};
unsigned char tags[4]={1,2,0x80,4};
int draws=0;
uintptr_t last_clump=0;
void check(bool ok){if(!ok)throw std::runtime_error("Gameplay skin native pool regression");}
void fill(unsigned char* ped,uint16_t model,uintptr_t clump) {
    std::memcpy(ped+0x22,&model,2);std::memcpy(ped+0x18,&clump,4);
}
}
static std::uintptr_t fixture_resolve(std::int32_t handle) {
    const auto index=handle>>8;
    check(index>=0 && index<4 && uint8_t(handle)==tags[index]);
    return index==0 ? uintptr_t(ped_a) : index==1 ? uintptr_t(ped_b) : index==3 ? uintptr_t(ped_c) : 0;
}
namespace lcs {
std::optional<uint8_t> sa_model_class(int32_t){return uint8_t(7);}
bool bind_sa_skin_double_sided(uintptr_t clump) {++draws;last_clump=clump;return true;}
}
int main() {
    static_assert(sizeof(void*)==4,"Native pool profile is Win32");
    try {
        alignas(4) unsigned char pool[16]{};
        fixture_pool=uintptr_t(pool);
        *reinterpret_cast<uintptr_t*>(pool+4)=uintptr_t(tags);
        *reinterpret_cast<int32_t*>(pool+8)=4;
        bool models[300]{};models[7]=true;models[13]=true;
        fill(ped_a,7,0x1000);fill(ped_b,20,0x2000);fill(ped_c,13,0);
        check(lcs::service_skin_pool(models) && draws==1 && last_clump==0x1000);
        // Same slot, new generation tag and clump: rebind the live object.
        tags[0]=5;fill(ped_a,7,0x3000);
        check(lcs::service_skin_pool(models) && draws==2 && last_clump==0x3000);
        tags[0]=0x80;fill(ped_c,13,0x4000);
        check(lcs::service_skin_pool(models) && draws==3 && last_clump==0x4000);
        // Changed to another mod's/unconverted model: leave its renderer alone.
        fill(ped_c,299,0x5000);
        check(lcs::service_skin_pool(models) && draws==3);
        *reinterpret_cast<int32_t*>(pool+8)=100001;
        check(!lcs::service_skin_pool(models));
        fixture_pool=0;check(!lcs::service_skin_pool(models));
        std::cout<<"PASS: converted ped filtering, native handle resolution, deleted tags, clump rebuild and pool bounds\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
