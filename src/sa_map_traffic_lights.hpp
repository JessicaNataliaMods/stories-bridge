#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <set>
#include "sa_lcs_bridge.hpp"

namespace lcs {

// SA 1.0 US CEntity::PreRender calls ProcessLightsForEntity here. The context
// bytes are from the user's installed PE; keep the CALL displacement separate
// so the installer can preserve an already-chained CALL target.
inline constexpr uintptr_t sa_map_traffic_lights_process_lights_call = 0x535FCD;
inline constexpr uintptr_t sa_map_traffic_lights_process_lights_target = 0x6FC7A0;
inline constexpr uintptr_t sa_map_traffic_lights_display_actual_light = 0x49DAB0;
inline constexpr std::array<uint8_t, 13> sa_map_traffic_lights_call_context{{
    0x8A, 0x47, 0x0D, 0x84, 0xC0, 0x89, 0x5C, 0x24, 0x20, 0x74, 0x07, 0x8B, 0xCE
}};

inline bool sa_map_traffic_lights_callsite_matches() noexcept {
    __try {
        const auto* site = reinterpret_cast<const uint8_t*>(sa_map_traffic_lights_process_lights_call);
        return std::memcmp(site - sa_map_traffic_lights_call_context.size(),
                           sa_map_traffic_lights_call_context.data(),
                           sa_map_traffic_lights_call_context.size()) == 0 &&
               site[0] == 0xE8;
    } __except (1) {
        return false;
    }
}

// The converted map assigns a private runtime model ID to trafficlight1. Read
// that identity from the same generated table used by the world converter;
// neither its source ID nor its runtime ID belongs in the public source tree.
class SaMapTrafficLights final {
public:
    bool configure(const std::filesystem::path& model_bindings) {
        model_id_ = 0;
        signal_passes_=0;
        bridge_models_.clear();
        std::ifstream input(model_bindings);
        if (!input) return false;

        uint16_t candidate = 0;
        std::string line;
        while (std::getline(input, line)) {
            if (line.empty()) continue;
            std::istringstream row(line);
            int source_id = -1;
            int runtime_id = -1;
            std::string source_name;
            std::string runtime_name;
            std::string extra;
            if (!(row >> source_id >> runtime_id >> source_name >> runtime_name) || row >> extra)
                return false;

            if (source_name != "trafficlight1") continue;
            if (source_id < 0 || source_id > 65535 || runtime_id < 10000 || runtime_id >= 20000 ||
                candidate != 0)
                return false;
            candidate = static_cast<uint16_t>(runtime_id);
        }
        if (input.bad() || candidate == 0) return false;
        const auto bridge_path=model_bindings.parent_path()/L"map-light-bindings.tsv";
        std::ifstream bridge_input(bridge_path);
        if(bridge_input) {
            int id=0;std::string kind;
            while(bridge_input>>id>>kind) {
                if(id<10000 || id>=20000 || kind!="bridge" || id==candidate ||
                   !bridge_models_.insert(static_cast<uint16_t>(id)).second)return false;
            }
            if(!bridge_input.eof())return false;
        }
        model_id_ = candidate;
        return true;
    }

    uint16_t model_id() const noexcept { return model_id_; }
    uint64_t signal_passes() const noexcept { return signal_passes_; }

    bool handles_model(uint16_t model_id) const noexcept {
        return model_id_ != 0 && model_id == model_id_;
    }

    // Call from the per-entity CEntity::ProcessLightsForEntity dispatch. If
    // this returns true, the native pass was already called with the active
    // signal phase. Geometry and light parameters remain the original LCS
    // records; phase/orientation and light rendering remain native SA.
    bool process_entity(uintptr_t entity, uintptr_t native_process_lights) const noexcept {
        if (!entity || model_id_ == 0) return false;

        uint16_t model_id = 0;
        bool visible=false;
        __try {
            model_id = *reinterpret_cast<const uint16_t*>(entity + 0x22);
            visible=(*reinterpret_cast<const uint8_t*>(entity+0x1C)&0x80)!=0;
        } __except (1) {
            return false;
        }
        if (!visible) return false;
        // Native bridge flashiness checks SA's own bridge object flags. Gate
        // the ordinary native blink instead using the converted lift's state.
        if(bridge_models_.count(model_id))return !sa_lcs_bridge_lights_flashing();
        if (!handles_model(model_id)) return false;

        if(!native_process_lights)return false;
        // DisplayActualLight hardcodes the SA lamp's corona size/brightness.
        // Keep the LCS light records' sizes, textures, ranges and colours, and
        // select their enabled phase with SA's own orientation/timer functions.
        // Shared model effects are restored before processing another entity.
        uintptr_t effects[6]{};
        uint16_t saved[6]{};
        unsigned changed=0;
        bool handled=false;
        __try {
            const auto info=*reinterpret_cast<const uintptr_t*>(0xA9B0C8+4*model_id);
            if(!info || *reinterpret_cast<const uint8_t*>(info+0x0D)!=6)return false;
            using Direction=int(__cdecl*)(uintptr_t);
            using Phase=uint8_t(__cdecl*)();
            using Effect=uintptr_t(__thiscall*)(uintptr_t,int);
            const auto direction=reinterpret_cast<Direction>(0x49D580)(entity);
            const auto state=reinterpret_cast<Phase>(direction==1?0x49D350:0x49D3A0)();
            const auto matrix=*reinterpret_cast<const uintptr_t*>(entity+0x14);
            if(!matrix)return false; // FindTrafficLightType allocates it natively.
            const auto* forward=reinterpret_cast<const float*>(matrix+0x10);
            const auto* camera_forward=reinterpret_cast<const float*>(0xB6F9AC);
            const bool same_direction=forward[0]*camera_forward[0]+forward[1]*camera_forward[1]+
                forward[2]*camera_forward[2]>0.0f;
            for(unsigned i=0;i<6;++i) {
                effects[i]=reinterpret_cast<Effect>(0x4C4C70)(info,static_cast<int>(i));
                if(!effects[i] || *reinterpret_cast<const uint8_t*>(effects[i]+0x0C)!=0)return false;
                saved[i]=*reinterpret_cast<const uint16_t*>(effects[i]+0x24);
            }
            __try {
                for(unsigned i=0;i<6;++i) {
                    const auto* rgb=reinterpret_cast<const uint8_t*>(effects[i]+0x10);
                    const unsigned colour=rgb[0]>200?(rgb[1]>100?1:2):0;
                    const bool front=*reinterpret_cast<const float*>(effects[i]+4)>0.0f;
                    *reinterpret_cast<uint16_t*>(effects[i]+0x24)=
                        (saved[i]&~uint16_t(0x60))|(colour==state && front!=same_direction?0x60:0);
                    ++changed;
                }
                reinterpret_cast<void(__thiscall*)(uintptr_t)>(native_process_lights)(entity);
                if(signal_passes_!=UINT64_MAX)++signal_passes_;
                handled=true;
            } __finally {
                for(unsigned i=0;i<changed;++i)
                    *reinterpret_cast<uint16_t*>(effects[i]+0x24)=saved[i];
            }
        } __except(1) {return false;}
        return handled;
    }

private:
    uint16_t model_id_ = 0;
    std::set<uint16_t> bridge_models_;
    mutable uint64_t signal_passes_=0;
};

} // namespace lcs
