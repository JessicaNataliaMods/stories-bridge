#include <windows.h>
#include "sa_player_skins.hpp"
#include "sa_world_preview.hpp"
#include "lcs/sa_executor.hpp"
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <array>
#include <set>
#include <cctype>
namespace lcs {
namespace {
constexpr int slots[]={297,298}; // Native special08/09, both use the man anim group.
uint32_t key(const std::string& name) {
    uint32_t value=0xffffffff;
    for(unsigned char c:name) {
        if(c>='a' && c<='z')c-=32;
        value^=c;for(int i=0;i<8;++i)value=(value>>1)^((value&1)?0xedb88320u:0u);
    }
    return value;
}
bool slot_info(int32_t slot,uint32_t& name,uint16_t& references) {
#ifdef LCS_PLAYER_SKIN_FIXTURE
    return fixture_slot(slot,name,references);
#else
    if(sa_model_class(slot)!=7)return false;
    __try {
        const auto model=*reinterpret_cast<const uintptr_t*>(0xa9b0c8+4*slot);
        if(!model)return false;
        name=*reinterpret_cast<const uint32_t*>(model+4);
        references=*reinterpret_cast<const uint16_t*>(model+8);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
#endif
}
bool request(int32_t slot,const char* name) {
#ifdef LCS_PLAYER_SKIN_FIXTURE
    return fixture_request(slot,name);
#else
    __try {
        // Request only the unused alternate slot, never a live player's model.
        reinterpret_cast<void(__cdecl*)(int32_t,const char*,int32_t)>(0x409d10)(slot,name,0x1a);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
#endif
}
bool replace(int32_t actor,int32_t slot) {
#ifdef LCS_PLAYER_SKIN_FIXTURE
    return fixture_replace(actor,slot);
#else
    __try {
        const auto ped=reinterpret_cast<uintptr_t(__cdecl*)(int32_t)>(0x54ff90)(actor);
        if(!ped || !*reinterpret_cast<const uintptr_t*>(ped+0x18))return false;
        // CPed::DeleteRwObject/SetModelIndex. Keep CPlayerPed, pool handle,
        // position, player data, inventory, health and all entity references.
        reinterpret_cast<void(__thiscall*)(uintptr_t)>(0x5debf0)(ped);
        reinterpret_cast<void(__thiscall*)(uintptr_t,uint32_t)>(0x5e4880)(ped,uint32_t(slot));
        return *reinterpret_cast<const uint16_t*>(ped+0x22)==slot && *reinterpret_cast<const uintptr_t*>(ped+0x18)!=0;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
#endif
}
bool bind_render(int32_t actor) {
#ifdef LCS_PLAYER_SKIN_FIXTURE
    return fixture_bind(actor);
#else
    __try {
        const auto ped=reinterpret_cast<uintptr_t(__cdecl*)(int32_t)>(0x54ff90)(actor);
        return ped && bind_sa_skin_double_sided(*reinterpret_cast<const uintptr_t*>(ped+0x18));
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
#endif
}
std::string lower(std::string value) {
    for(auto& c:value)c=char(std::tolower(static_cast<unsigned char>(c)));return value;
}
int32_t player_actor() {
    auto r=execute_sa({0x01f5,{0},{},0,1});return r.executed?r.values[0]:-1;
}
int32_t actor_model(int32_t actor) {
    auto r=execute_sa({0x0665,{actor},{},0,1});return r.executed?r.values[0]:-1;
}
}
SaPlayerSkins::SaPlayerSkins(const std::filesystem::path& world) {
    std::ifstream file(world/L"player"/L"index.tsv");std::string label,asset;
    if(!file)throw std::runtime_error("Original Toni outfit index missing");
    while(file>>label>>asset) {
        label=lower(label);
        if(asset!="lcs_"+label || !assets_.emplace(label,asset).second)throw std::runtime_error("Invalid original Toni outfit binding");
    }
    if(!file.eof() || assets_.size()!=16 || !assets_.count("plr"))throw std::runtime_error("Incomplete Toni wardrobe");
    for(int i=2;i<=16;++i)if(!assets_.count("plr"+std::to_string(i)))throw std::runtime_error("Missing original Toni outfit");
    // Streaming looks up unregistered DFF basenames in its extra directory.
    // Check every required entry before RequestSpecialModel can be called.
    std::ifstream img(world.parent_path().parent_path()/L"models"/L"lcs"/L"world.img",std::ios::binary);
    std::array<char,4> magic{};uint32_t count=0;img.read(magic.data(),4);img.read(reinterpret_cast<char*>(&count),4);
    if(!img || magic!=std::array<char,4>{'V','E','R','2'} || count<32 || count>10000)throw std::runtime_error("Toni skin archive missing/invalid");
    std::set<std::string> names;
    for(uint32_t i=0;i<count;++i) {
        std::array<char,32> entry{};img.read(entry.data(),32);entry[31]=0;
        if(!img || !names.emplace(entry.data()+8).second)throw std::runtime_error("Invalid Toni IMG directory");
    }
    for(const auto& row:assets_)for(const auto* ext:{".dff",".txd"})
        if(!names.count(row.second+ext))throw std::runtime_error("Toni outfit media absent");
    for(int i=0;i<2;++i) {
        uint32_t name=0;uint16_t refs=0;
        if(!slot_info(slots[i],name,refs))throw std::runtime_error("Toni slots are not native ped models");
        const bool original=name==key(i==0?"special08":"special09") || name==key("");
        bool converted=false;
        for(const auto& row:assets_)converted=converted || name==key(row.second);
        // CStreaming::ReInit resets special-model keys 290..299 to the
        // empty-name key, not special08/09. A referenced reset slot remains
        // unavailable for renaming; an empty unreferenced slot is reusable.
        if(!original && !converted)throw std::runtime_error("Toni skin slot is used by another mod/actor");
    }
}
bool SaPlayerSkins::begin(int32_t actor,const std::string& input) {
    const auto label=lower(input);const auto row=assets_.find(label);
    if(row==assets_.end() || actor<0 || actor!=player_actor() || (pending_>=0 && actor!=actor_))return false;
    if(pending_>=0)return actor==actor_ && label==requested_;
    if(worn_==label && actor_model(actor)==current_) {actor_=actor;requested_=label;pending_=current_;return true;}
    // A new Campaign has no committed slot yet, but streaming can still own
    // the previous session's exact outfit. Acquire it without RequestSpecialModel
    // (which would rename/reload a live model, including other actors' model).
    if(current_<0)for(const auto slot:slots) {
        uint32_t name=0;uint16_t refs=0;
        if(slot_info(slot,name,refs) && name==key(row->second) && execute_sa({0x0247,{slot}}).executed) {
            actor_=actor;requested_=label;pending_=slot;return true;
        }
    }
    // Native load rebuilds CJ but can keep the committed special model. Reuse
    // its exact asset instead of requiring the alternate wardrobe slot to be
    // empty. REQUEST_MODEL does not rename/replace a slot held by another ped.
    uint32_t committed_name=0;uint16_t committed_refs=0;
    if(worn_==label && current_>=0 && slot_info(current_,committed_name,committed_refs) &&
       committed_name==key(row->second) && execute_sa({0x0247,{current_}}).executed) {
        actor_=actor;requested_=label;pending_=current_;return true;
    }
    // Prefer alternating away from the currently worn slot, then fall back to
    // the other native Toni slot. A reference means the slot is still in use;
    // never request a rename for it, even when its name is a known outfit.
    const int preferred=current_==slots[0]?slots[1]:current_==slots[1]?slots[0]:slots[0];
    const int candidates[]={preferred,preferred==slots[0]?slots[1]:slots[0]};
    for(const auto target:candidates) {
        uint32_t name=0;uint16_t refs=0;
        if(!slot_info(target,name,refs) || refs)continue;
        bool owned=name==key(target==slots[0]?"special08":"special09") || name==key("");
        for(const auto& item:assets_)owned=owned || name==key(item.second);
        if(!owned)continue;
        if(!request(target,row->second.c_str()))return false;
        actor_=actor;requested_=label;pending_=target;return true;
    }
    return false;
}
bool SaPlayerSkins::finish(int32_t actor) {
    if(actor!=actor_ || actor!=player_actor() || pending_<0)return false;
    if(actor_model(actor)!=pending_) {
        if(!execute_sa({0x038b,{}}).executed)return false;
        const auto loaded=execute_sa({0x0248,{pending_},{},0,0,true});
        if(!loaded.executed || !loaded.condition || !execute_sa({0x0687,{actor}}).executed)return false;
        if(!replace(actor,pending_))return false;
    }
    if(actor_model(actor)!=pending_ || !bind_render(actor))return false;
    current_=pending_;pending_=-1;worn_=requested_;requested_.clear();return true;
}
std::optional<std::string> SaPlayerSkins::wearing(int32_t actor)const {
    if(actor!=actor_ || actor!=player_actor() || actor_model(actor)!=current_ || worn_.empty())return {};
    return worn_;
}
bool SaPlayerSkins::initialize(const std::string& label) {
    const auto actor=player_actor();return begin(actor,label) && finish(actor);
}
bool SaPlayerSkins::service() {
    // Native restart/clothes services can rebuild CJ. Restore the actual last
    // committed outfit only after such a reset; never override another skin.
    if(worn_.empty())return true;
    const auto actor=player_actor();
    if(actor<0){recovery_diagnostic_="native player is not available";return false;}
    // An explicit outfit command owns its pending request. Recovery owns only
    // requests issued here, and must retry their finish after streaming yields.
    if(pending_>=0) {
        if(!recovering_)return true;
        if(actor!=actor_){pending_=-1;requested_.clear();recovering_=false;}
        else if(finish(actor)){recovering_=false;recovery_diagnostic_.clear();return true;}
        else {recovery_diagnostic_="waiting for committed outfit streaming/clump";return false;}
    }
    const auto model=actor_model(actor);
    if(model==current_) {
        // A load/restart can replace the ped without changing its outfit model.
        actor_=actor;
        const bool ready=bind_render(actor);
        recovery_diagnostic_=ready?"":"waiting for rebuilt player clump";
        return ready;
    }
    if(model!=0)return true;
    const auto label=worn_;
    if(!begin(actor,label)){recovery_diagnostic_="committed outfit request refused (native slot/ownership)";return false;}
    recovering_=true;
    if(!finish(actor)){recovery_diagnostic_="waiting for committed outfit streaming/clump";return false;}
    recovering_=false;recovery_diagnostic_.clear();return true;
}
}
