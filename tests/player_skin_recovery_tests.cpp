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
        // A native LOAD can restore the original model name while the pool's
        // reference count still reflects the pre-load session. Keep that slot
        // untouched and stream the saved outfit into the other free slot.
        names={lcs::key("special08"),lcs::key("special09")};refs={1,0};
        actor=924;model=0;loaded=true;clump=true;
        const int renamed_before_load=renamed;
        lcs::SaPlayerSkins after_load(world);check(after_load.initialize("plr3"));
        check(model==298 && names[0]==lcs::key("special08") && renamed==renamed_before_load+1);
        // If both original slots are still referenced, initialization must
        // refuse without renaming either slot or changing the rebuilt player.
        names={lcs::key("special08"),lcs::key("special09")};refs={1,1};
        actor=936;model=0;loaded=true;
        const auto names_before_refusal=names;const int renamed_before_refusal=renamed;
        lcs::SaPlayerSkins fully_occupied(world);check(!fully_occupied.initialize("plr3"));
        check(model==0 && names==names_before_refusal && renamed==renamed_before_refusal);
        // Exact retail CStreaming::ReInit state: it hashes the empty string
        // into model-info keys 290..299, instead of restoring special08/09.
        names={lcs::key(""),lcs::key("")};refs={0,0};actor=948;model=0;
        lcs::SaPlayerSkins empty_after_load(world);check(empty_after_load.initialize("plr3"));
        check(model==297 && names[0]==lcs::key("lcs_plr3"));
        names={lcs::key(""),lcs::key("")};refs={1,0};actor=960;model=0;
        lcs::SaPlayerSkins busy_empty_after_load(world);check(busy_empty_after_load.initialize("plr3"));
        check(model==298 && names[0]==lcs::key(""));
        names={lcs::key(""),lcs::key("")};refs={1,1};actor=972;model=0;
        const auto empty_names=names;const auto empty_renamed=renamed;
        lcs::SaPlayerSkins busy_reset_slots(world);check(!busy_reset_slots.initialize("plr3"));
        check(model==0 && names==empty_names && renamed==empty_renamed);
        std::cout<<"PASS: retail empty-key LOAD reset, occupied slots, streaming retry and foreign-skin protection\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
