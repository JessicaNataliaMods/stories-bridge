#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include "lcs/native.hpp"
#include "lcs/sa_script.hpp"
#include "lcs/native_player_exports.hpp"
#include <fstream>
#include <array>
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <iomanip>
#include <sstream>

// Run verified parameter routines and the self-contained garage initializer in
// this test process with private memory pages. Relocate their absolute data and
// jump-table references to those pages. Never load or start the game.
static void check(bool value,const char* text) {if(!value)throw std::runtime_error(text);}
static std::vector<uint8_t> parameter_code(const char* path,std::vector<uint8_t>& garage_code) {
    std::ifstream file(path,std::ios::binary);
    check(bool(file),"Native reference executable cannot be read");
    const std::vector<uint8_t> image((std::istreambuf_iterator<char>(file)),{});
    const auto u16=[&](size_t at){check(at+2<=image.size(),"Truncated PE");uint16_t v;std::memcpy(&v,&image[at],2);return v;};
    const auto u32=[&](size_t at){check(at+4<=image.size(),"Truncated PE");uint32_t v;std::memcpy(&v,&image[at],4);return v;};
    check(u16(0)==0x5a4d,"Not a PE image");
    const auto pe=u32(0x3c);
    check(u32(pe)==0x4550 && u16(pe+4)==0x14c && u16(pe+24)==0x10b && u32(pe+52)==0x400000,"Unverified native PE profile");
    const auto sections=pe+24+u16(pe+20);
    for(unsigned i=0;i<u16(pe+6);++i) {
        const auto at=sections+i*40,rva=u32(at+12),size=u32(at+16);
        if(0x64080<rva || 0x64500ull>uint64_t(rva)+size)continue;
        const auto offset=uint64_t(u32(at+20))+0x64080-rva;
        check(offset+0x480<=image.size(),"Truncated native parameter routines");
        std::vector<uint8_t> code(image.begin()+size_t(offset),image.begin()+size_t(offset)+0x480);
        check(0x47600>=rva && 0x47679ull<=uint64_t(rva)+size,"Garage initializer is outside verified code section");
        const auto garage_offset=uint64_t(u32(at+20))+0x47600-rva;
        check(garage_offset+0x79<=image.size(),"Truncated garage initializer");
        garage_code.assign(image.begin()+size_t(garage_offset),image.begin()+size_t(garage_offset)+0x79);
        uint8_t digest[32]{};
        BCRYPT_ALG_HANDLE algorithm=nullptr;
        check(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)==0,"Cannot open SHA256");
        const auto status=BCryptHash(algorithm,nullptr,0,code.data(),ULONG(code.size()),digest,32);
        uint8_t garage_digest[32]{};
        const auto garage_status=BCryptHash(algorithm,nullptr,0,garage_code.data(),ULONG(garage_code.size()),garage_digest,32);
        BCryptCloseAlgorithmProvider(algorithm,0);
        check(status==0,"Cannot hash native instructions");
        std::ostringstream hash;hash<<std::hex<<std::setfill('0');for(auto b:digest)hash<<std::setw(2)<<unsigned(b);
        check(hash.str()=="0b12501e888356aa633bf98567338f4653fa6ea5f2240abc923d7c6bd028bde4","Parameter routines differ from verified native profile");
        check(garage_status==0,"Cannot hash garage initializer");
        std::ostringstream garage_hash;garage_hash<<std::hex<<std::setfill('0');for(auto b:garage_digest)garage_hash<<std::setw(2)<<unsigned(b);
        check(garage_hash.str()=="0c559d196404c4941a619d66cc0645e6ba6436bdebf40fab046e6b3f56a4ac03","Garage initializer differs from verified native profile");
        return code;
    }
    throw std::runtime_error("Native parameter code not found");
}
struct Page {
    void* address;
    Page():address(VirtualAlloc(nullptr,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)) {check(address!=nullptr,"Cannot allocate isolated native test page");}
    ~Page(){if(address)VirtualFree(address,0,MEM_RELEASE);}
};
using Parameters=void(__thiscall*)(lcs::SaScript*,int16_t);
int main(int argc,char** argv) {
    try {
        check(argc==2,"Pass the native reference executable as data");
        Page instructions,variables;
        std::vector<uint8_t> garage_code;
        const auto code=parameter_code(argv[1],garage_code);
        auto* relocated=static_cast<uint8_t*>(instructions.address);
        std::memcpy(relocated,code.data(),code.size());
        for(size_t i=0;i+4<=code.size();++i) {
            uint32_t value;std::memcpy(&value,&code[i],4);
            if(value>=0x464080 && value<0x464500) value=uint32_t(reinterpret_cast<uintptr_t>(instructions.address))+value-0x464080;
            else if(value>=0xa40000 && value<0xa50000) value=uint32_t(reinterpret_cast<uintptr_t>(variables.address))+value-0xa40000;
            else continue;
            std::memcpy(relocated+i,&value,4);i+=3;
        }
        auto* garage_routine=relocated+0x1000;
        std::memcpy(garage_routine,garage_code.data(),garage_code.size());
        for(size_t offset:{0x1fu,0x26u,0x40u,0x44u,0x48u}) {
            uint32_t address=0;std::memcpy(&address,garage_routine+offset,4);
            check(address>=0x447600 && address<0x447679,"Garage routine relocation escapes its isolated block");
            address=uint32_t(reinterpret_cast<uintptr_t>(garage_routine))+address-0x447600;
            std::memcpy(garage_routine+offset,&address,4);
        }
        DWORD old=0;check(VirtualProtect(instructions.address,0x10000,PAGE_EXECUTE_READ,&old)!=0,"Cannot protect native test instructions");
        check(FlushInstructionCache(GetCurrentProcess(),instructions.address,0x10000)!=0,"Cannot flush instruction cache");
        for(uint8_t type:{5,19,13}) {
            std::array<uint8_t,0xD8> garage;garage.fill(0xA5);
            garage[0x4C]=type;garage[0x4E]=0x2D;
            const float initial_progress=.25f;std::memcpy(garage.data()+0x38,&initial_progress,4);
            const auto before=garage;
            reinterpret_cast<void(__thiscall*)(void*)>(garage_routine)(garage.data());
            float progress=0;uint32_t timer=1;std::memcpy(&progress,garage.data()+0x38,4);std::memcpy(&timer,garage.data()+0x3C,4);
            check(progress==(type==5?1.f:type==13?.25f:0.f) && timer==0 && garage[0x4D]==(type==5?1:0) && garage[0x4E]==0x69,
                "Actual SA garage initializer must set progress/state/timer/flags for respray and closed garages");
            for(size_t i=0;i<garage.size();++i)if((i<0x38 || i>=0x40) && i!=0x4D && i!=0x4E)
                check(garage[i]==before[i],"Garage initializer changed an unrelated field");
        }
        std::cout<<"PASS: actual SA garage initialization, including PNS open progress, closed garages and preserved foreign fields\n";
        const auto collect=reinterpret_cast<Parameters>(relocated),store=reinterpret_cast<Parameters>(relocated+0x2f0);
        auto* params=reinterpret_cast<int32_t*>(static_cast<uint8_t*>(variables.address)+0x3c78);
        auto* shared=reinterpret_cast<int32_t*>(static_cast<uint8_t*>(variables.address)+0x8960);
        lcs::NativeCall player{0x01f5,{0},{},0,1};player.mission_context=true;
        auto packet=player.encode();
        lcs::SaScript broken(packet.data(),true);broken.mission=true;broken.ip+=2;
        collect(&broken,1);check(params[0]==0,"Native player index collector mismatch");
        params[0]=258;store(&broken,1);
        check(broken.locals[0]==0 && shared[0]==258,"Previous mission flag bug did not reproduce");
        std::cout<<"Reproduced previous error: native player handle 258 in shared mission locals, packet local still zero\n";

        for(bool cleanup:{false,true}) {
            lcs::NativeCall call;call.opcode=0x01f5;call.outputs=32;call.mission_context=cleanup;
            for(int i=0;i<32;++i)call.inputs.push_back(i%2 ? int32_t(0xc1234567u+i) : 258+i*256);
            packet=call.encode();lcs::SaScript script(packet.data(),call.mission_context);script.ip+=2;
            for(int i=0;i<32;++i)shared[i]=0x12340000+i;
            collect(&script,32);
            for(int i=0;i<32;++i)check(params[i]==call.inputs[i],"Native input bits changed");
            store(&script,32);
            for(int i=0;i<32;++i) {
                check(script.locals[i]==call.inputs[i],"Native output missing from private packet locals");
                check(shared[i]==0x12340000+i,"Packet overwrote SA shared mission locals");
            }
            check(script.cleanup==cleanup && !script.mission,"Cleanup and local ownership flags conflated");
            check(script.ip==packet.data()+packet.size(),"Native parameter packet consumption mismatch");
        }
        auto* globals=static_cast<uint8_t*>(variables.address)+0x9960;
        constexpr uint8_t mod_actor[]={0x72,0x01,0x02,0x0C,0,0x03,0,0};
        // Exact global operand from the installed teleport script. The noclip
        // 0E49/0E4A commands read the same byte-offset-12 actor variable.
        std::vector<uint8_t> mod_packet(std::begin(mod_actor),std::end(mod_actor));
        lcs::SaScript mod(mod_packet.data(),false);mod.ip+=2;collect(&mod,1);
        check(params[0]==0,"Old null mod actor binding did not reproduce");
        const std::vector<uint8_t> before(globals,globals+627);
        check(!lcs::publish_native_player_exports(globals,627,258),"Foreign SCM globals were overwritten");
        check(!std::memcmp(globals,before.data(),before.size()),"Rejected foreign SCM write changed bytes");
        constexpr uint8_t bootstrap[]={0xA4,0x03,0x09,'L','C','S','S','A','B','T',0};
        std::memcpy(globals+616,bootstrap,sizeof(bootstrap));
        for(int32_t actor:{258,514,1026}) {
            const std::vector<uint8_t> snapshot(globals,globals+627);
            check(lcs::publish_native_player_exports(globals,627,actor),"Live mod actor export failed");
            mod.ip=mod_packet.data()+2;collect(&mod,1);
            check(params[0]==actor,"Actual native/CLEO global parameter decoder read a stale actor");
            for(size_t i=0;i<snapshot.size();++i)if(i<8 || i>=16)
                check(globals[i]==snapshot[i],"Player exports overwrote bootstrap/HUD/mission data");
            int32_t player=-1;std::memcpy(&player,globals+8,4);check(player==0,"Standard player index export differs");
        }
        check(!lcs::publish_native_player_exports(globals,627,-1),"Invalid actor was published to mods");
        check(!lcs::publish_native_player_exports(globals,626,258),"Truncated bootstrap was accepted");
        std::cout<<"PASS: actual native parameter ABI preserves private/shared locals; reproduced null mod global and verified live/generation-refreshed player exports, foreign SCM protection and unchanged HUD/mission storage\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
