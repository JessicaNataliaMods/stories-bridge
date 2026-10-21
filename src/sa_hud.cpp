#include <windows.h>
#include "sa_hud.hpp"
#include "lcs/sa_executor.hpp"
#include "lcs/native_memory.hpp"
#include <cstring>
namespace lcs {
#ifdef LCS_ISOLATED_HUD_RENDER_TEST
void native_hud_add_clock(uint32_t,const char*,uint32_t);
void native_hud_add_counter(uint32_t,uint16_t,const char*,uint16_t);
void native_hud_clear(uint32_t,bool);
#else
static void native_hud_add_clock(uint32_t offset,const char* label,uint32_t type){
    reinterpret_cast<void(__thiscall*)(void*,uint32_t,const char*,uint32_t)>(0x44CD50)(reinterpret_cast<void*>(0xBA1788),offset,label,type);
}
static void native_hud_add_counter(uint32_t offset,uint16_t type,const char* label,uint16_t slot){
    reinterpret_cast<void(__thiscall*)(void*,uint32_t,uint16_t,const char*,uint16_t)>(0x44CDA0)(reinterpret_cast<void*>(0xBA1788),offset,type,label,slot);
}
static void native_hud_clear(uint32_t offset,bool clock){
    reinterpret_cast<void(__thiscall*)(void*,uint32_t)>(clock?0x44CE60:0x44CE80)(reinterpret_cast<void*>(0xBA1788),offset);
}
#endif
namespace {
constexpr uintptr_t hud=0xBA1788,globals=0xA49960;
bool cell(uint32_t offset) {return offset>=32 && offset<=44 && offset%4==0;}
uintptr_t owner(size_t slot) {return native_entity_address(slot?hud+0x40+(slot-1)*0x44:hud);}
// COnscreenTimer's verified SA layouts and entry points come from the primary
// UserDisplay / OnscreenTimer / Onscreen*Entry declarations in vendor/sa-reference.
bool update_protected(const HudRequest* request) {
    const auto& r=*request;
    __try {
        if(r.action==HudRequest::Action::freeze) {
            *reinterpret_cast<bool*>(native_entity_address(hud+0x151))=r.type!=0;return true;
        }
        const bool clock=r.action==HudRequest::Action::add_clock || r.action==HudRequest::Action::clear_clock;
        const size_t slot=(r.native_offset-32)/4;
        if(!cell(r.native_offset) || (clock && slot!=0) || (!clock && (slot==0 || r.line>=3)))return false;
        const auto id=reinterpret_cast<const uint32_t*>(owner(slot));
        const bool adding=r.action==HudRequest::Action::add_clock || r.action==HudRequest::Action::add_counter;
        if(adding && *id)return false; // never displace a native mod's HUD
        if(!adding && *id!=r.native_offset){
            if(*id)return false;
            if(r.action!=HudRequest::Action::counter_prefix)sa_hud_present(r);
            return true;
        }
        if(r.action==HudRequest::Action::counter_prefix)return sa_hud_present(r);
        char label[9]{};std::memcpy(label,r.label.data(),r.label.size());
        if(r.action==HudRequest::Action::add_clock) {
            // Primary eTimerDirection: INCREASE=0, DECREASE=1, matching LCS.
            native_hud_add_clock(r.native_offset,r.label.empty()?nullptr:label,uint32_t(r.type));
        }else if(r.action==HudRequest::Action::add_counter) {
            native_hud_add_counter(r.native_offset,uint16_t(r.type),r.label.empty()?nullptr:label,uint16_t(slot-1));
        }else {
            native_hud_clear(r.native_offset,clock);
        }
        const bool accepted=adding?*id==r.native_offset:*id==0;
        return accepted && sa_hud_present(r);
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
}
bool sa_hud_update(const HudRequest& r) {
    if(!sa_executor_profile_matches() || r.label.size()>8 || r.label.find('\0')!=std::string::npos ||
       r.heading.size()>8 || r.heading.find('\0')!=std::string::npos)return false;
    if((r.custom || r.action==HudRequest::Action::counter_prefix) && !sa_hud_render_ready())return false;
    if((r.action==HudRequest::Action::add_counter && (r.type<0 || r.type>1)) ||
       (r.action==HudRequest::Action::add_clock && (r.type<0 || r.type>1)))return false;
    return update_protected(&r);
}
std::optional<int32_t> sa_hud_read(uint32_t offset) {
    if(!sa_executor_profile_matches() || !cell(offset))return {};
    __try {
        if(*reinterpret_cast<const uint32_t*>(owner((offset-32)/4))!=offset)return {};
        return *reinterpret_cast<const int32_t*>(native_entity_address(globals+offset));
    } __except(EXCEPTION_EXECUTE_HANDLER) {return {};}
}
bool sa_hud_write(uint32_t offset,int32_t value) {
    if(!sa_executor_profile_matches() || !cell(offset))return false;
    __try {
        const auto id=*reinterpret_cast<const uint32_t*>(owner((offset-32)/4));
        if(id && id!=offset)return false;
        *reinterpret_cast<int32_t*>(native_entity_address(globals+offset))=value;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool sa_display_message(const MissionMessage& request) {
    if(!sa_executor_profile_matches() || request.key.empty() || request.key.size()>8 ||
       request.key.find('\0')!=std::string::npos)return false;
    __try {
        // Plugin-SDK CText::Get / TheText and CMessages::AddToPreviousBriefArray.
        // Native GXT owns the pointer; native text unloading clears this queue.
        const auto text=reinterpret_cast<const char*(__thiscall*)(void*,const char*)>(0x6A0050)(reinterpret_cast<void*>(0xC1B340),request.key.c_str());
        if(!text)return false;
        if(request.subtitle && !*reinterpret_cast<const bool*>(0xBA6748+0x44)) {
            // Script10 also stores NO_BRIEF subtitles in the previous brief
            // when subtitles are disabled. Reproduce that original branch.
            reinterpret_cast<void(__cdecl*)(const char*,int32_t,int32_t,int32_t,int32_t,int32_t,int32_t,const char*)>(0x69DD50)(text,-1,-1,-1,-1,-1,-1,nullptr);
            return true;
        }
        NativeMessage message;
        message.text=uint32_t(reinterpret_cast<uintptr_t>(text));message.flags=request.flags;
        message.duration=request.duration;message.created_at=*reinterpret_cast<const uint32_t*>(0xB7CB84);
        message.numbers=request.numbers;
        message.previous_brief=request.previous_brief?1:0;
        const auto slot=insert_mission_message(reinterpret_cast<NativeMessage*>(0xC1A7F0),message,request.soon,request.queued);
        // A pending Soon message enters previous briefs when SA promotes it;
        // only a message displayed immediately enters that list now.
        if(slot==0 && request.previous_brief)reinterpret_cast<void(__cdecl*)(const char*,int32_t,int32_t,int32_t,int32_t,int32_t,int32_t,const char*)>(0x69DD50)(text,
            message.numbers[0],message.numbers[1],message.numbers[2],message.numbers[3],message.numbers[4],message.numbers[5],nullptr);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool sa_show_campaign_error(const std::string& message) {
    if(!sa_executor_profile_matches() || message.empty() || message.size()>350 || message.find('\0')!=std::string::npos)return false;
    constexpr uint8_t signature[]={0xA0,0xC0,0xAE,0xBA,0x00,0x84,0xC0,0x0F,0x85,0x4F,0x01,0x00,0x00,0xA0,0x14,0xC0};
    __try {
        if(std::memcmp(reinterpret_cast<const void*>(0x588BE0),signature,sizeof(signature)))return false;
        // CHud copies into its own buffer; no borrowed temporary GXT pointer.
        reinterpret_cast<void(__cdecl*)(const char*,bool,bool,bool)>(0x588BE0)(message.c_str(),false,true,false);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
}
