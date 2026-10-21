#include <cstdint>
#include <string>
#include <array>
static bool fixture_slot(int32_t,uint32_t&,uint16_t&);
static bool fixture_request(int32_t,const char*);
static bool fixture_replace(int32_t,int32_t);
static bool fixture_bind(int32_t);
#define LCS_PLAYER_SKIN_FIXTURE
#include "../src/sa_player_skins.cpp"
#include <iostream>
namespace {
int32_t actor=100,model=0;
bool loaded=true,clump=true;
std::array<uint32_t,2> names{};
std::array<uint16_t,2> refs{};
int renamed=0;
void check(bool value){if(!value)throw std::runtime_error("Player outfit recovery regression");}
}
static bool fixture_slot(int32_t slot,uint32_t& name,uint16_t& references) {
    if(slot<297 || slot>298)return false;
    name=names[slot-297];references=refs[slot-297];return true;
}
static bool fixture_request(int32_t slot,const char* name){names[slot-297]=lcs::key(name);++renamed;return true;}
static bool fixture_replace(int32_t ped,int32_t slot){if(ped!=actor || !clump)return false;model=slot;return true;}
static bool fixture_bind(int32_t ped){return ped==actor && clump;}
namespace lcs {
NativeResult execute_sa(const NativeCall& call) {
    NativeResult r;r.executed=true;
    if(call.opcode==0x01f5)r.values[0]=actor;
    else if(call.opcode==0x0665)r.values[0]=model;
    else if(call.opcode==0x0248)r.condition=loaded;
    return r;
}
}
int main() {
    // Use a tiny on-disk wardrobe with the real constructor's archive checks.
    const auto base=std::filesystem::temp_directory_path()/L"lcs-player-recovery-fixture";
    try {
        const auto world=base/L"data"/L"world";
        std::filesystem::create_directories(world/L"player");
        std::filesystem::create_directories(base/L"models"/L"lcs");
        std::ofstream index(world/L"player"/L"index.tsv");
        std::ofstream img(base/L"models"/L"lcs"/L"world.img",std::ios::binary);
        const uint32_t count=32;img.write("VER2",4);img.write(reinterpret_cast<const char*>(&count),4);
        for(int i=1;i<=16;++i) {
            const auto label=std::string("plr")+(i==1?"":std::to_string(i));
            index<<label<<" lcs_"<<label<<'\n';
            for(const auto* ext:{".dff",".txd"}) {
                std::array<char,32> entry{};const auto name="lcs_"+label+ext;
                std::memcpy(entry.data()+8,name.data(),name.size());img.write(entry.data(),32);
            }
        }
        index.close();img.close();
        names={lcs::key("special08"),lcs::key("special09")};
        lcs::SaPlayerSkins skins(world);check(skins.initialize("plr3"));
        check(model==297 && renamed==1);
        // LOAD rebuilds CJ; the other slot is legitimately occupied. The
        // committed asset must be reused without renaming either native slot.
        refs={1,1};actor=356;model=0;loaded=false;
        check(!skins.service() && model==0 && renamed==1);
        check(!skins.service() && model==0); // pending finish must be retried
        loaded=true;check(skins.service() && model==297 && renamed==1);
        check(skins.wearing(actor)==std::optional<std::string>("plr3"));
        // Another rebuild during an outstanding recovery invalidates only that
        // old request; the new player still receives the committed outfit.
        model=0;loaded=false;check(!skins.service());actor=612;loaded=true;
        check(skins.service() && skins.wearing(actor).has_value());
        clump=false;check(!skins.service());clump=true;check(skins.service());
        model=124;check(skins.service() && model==124); // another mod's skin
        // New Game constructs a fresh owner while native streaming retains
        // the old outfit. Referenced exact assets are reused, never renamed.
        actor=868;model=0;
        names[1]=lcs::key("lcs_plr2");
        lcs::SaPlayerSkins next(world);check(next.initialize("plr3"));
        check(model==297 && renamed==1);
        names[1]=lcs::key("foreign_skin");
        bool refused=false;try{lcs::SaPlayerSkins foreign(world);}catch(const std::runtime_error&){refused=true;}
        check(refused);
        std::cout<<"PASS: player rebuild, occupied alternate slot, streaming retry, actor replacement and foreign skin\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
