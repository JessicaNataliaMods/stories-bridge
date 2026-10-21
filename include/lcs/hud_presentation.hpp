#pragma once
#include "hud.hpp"
#include <algorithm>
namespace lcs {
struct HudPresentation {
    HudRequest request;
    bool dollar=false;
    std::string number(int32_t value)const{
        // Retail2E3F8C uses unsigned total<99, then2E3F98 signed value<99.
        // Both fields are clamped when a total exists, unlike the reverse.
        if(request.total!=-1)value=std::min(99,value);
        auto result=(dollar?std::string("$"):std::string{})+std::to_string(value);
        if(request.total!=-1)result+="/"+std::to_string(std::min(99u,uint32_t(request.total)));
        return result;
    }
};
// Keeps presentation state only after the native entry is accepted. Rendering
// additionally verifies the actual native owner and display flag each frame.
class HudPresentations {
    std::array<std::optional<HudPresentation>,4> entries_;
public:
    bool apply(const HudRequest& r){
        if(r.action==HudRequest::Action::freeze)return true;
        if(r.native_offset<32 || r.native_offset>44 || r.native_offset%4)return false;
        auto& entry=entries_[(r.native_offset-32)/4];
        if(r.action==HudRequest::Action::counter_prefix){
            if(!entry || r.native_offset==32)return false;
            entry->dollar=r.type!=0;entry->request.custom=true;return true;
        }
        if(r.action==HudRequest::Action::clear_clock || r.action==HudRequest::Action::clear_counter){entry.reset();return true;}
        entry=HudPresentation{r,false};return true;
    }
    const auto& entries()const{return entries_;}
    bool custom()const{for(const auto& e:entries_)if(e && e->request.custom)return true;return false;}
};
}
