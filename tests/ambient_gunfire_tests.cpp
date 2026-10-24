#include "sa_ambient_gunfire.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
    int ambient=0;const auto native=[&]{++ambient;};
    lcs::dispatch_ambient_gunfire(false,native);check(ambient==1,"Native ambient generator remains active outside campaign");
    for(int i=0;i<100;++i)lcs::dispatch_ambient_gunfire(true,native);
    check(ambient==1,"Active campaign never dispatches the distant ambient gunfire generator");
    lcs::dispatch_ambient_gunfire(false,native);check(ambient==2,"Leaving campaign restores native generator routing");
    check(argc==2,"Expected GTA executable as read-only profile data");
    std::ifstream f(argv[1],std::ios::binary|std::ios::ate);check(bool(f),"Profile file readable");
    std::vector<unsigned char> bytes(size_t(f.tellg()));f.seekg(0);f.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()));check(bool(f),"Profile data complete");
    const auto u32=[&](size_t off){check(off+4<=bytes.size(),"PE field in bounds");uint32_t value;std::memcpy(&value,bytes.data()+off,4);return value;};
    const auto u16=[&](size_t off){check(off+2<=bytes.size(),"PE field in bounds");uint16_t value;std::memcpy(&value,bytes.data()+off,2);return value;};
    const size_t pe=u32(0x3c);check(u32(pe)==0x4550 && u16(pe+4)==0x14c,"Native PE32 x86 profile");
    const auto offset=[&](uintptr_t va){const auto rva=va-u32(pe+52);for(size_t i=0;i<u16(pe+6);++i){const auto s=pe+24+u16(pe+20)+i*40;const auto begin=u32(s+12),size=u32(s+16);if(rva>=begin && rva-begin<size)return size_t(u32(s+20)+rva-begin);}throw std::runtime_error("Code address not backed by profile file");};
    using P=lcs::SaAmbientGunfireProfile;const auto call=offset(P::call);check(bytes.at(call)==0xE8,"Ambient service remains a CALL");
    int32_t relative;std::memcpy(&relative,bytes.data()+call+1,4);check(uintptr_t(int64_t(P::call+5)+relative)==P::target,"Call targets only dedicated ambient gunfire generator");
    check(P::target!=0x504F80 && P::target!=0x503690,"Weapon fire/reload consumers are separate from the filtered generator");
    std::cout<<"PASS: campaign-only ambient suppression and native CALL profile; executable read as data only, no game/audio opened\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
