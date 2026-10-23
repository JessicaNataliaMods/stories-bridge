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

    bool handles_model(uint16_t model_id) const noexcept {
        return model_id_ != 0 && model_id == model_id_;
    }

    // Call from the per-entity CEntity::ProcessLightsForEntity dispatch. If
    // this returns true, the caller must skip that model's generic 2DFX pass:
    // DisplayActualLight selects the visible lamp using SA's orientation and
    // LightForCars1/2_Visual phase functions. Its 2DFX records come from the
    // converted DFF's native Geometry plugin, so this adapter stores no map
    // coordinates or extracted game data. Do not reject a null CPlaceable
    // matrix: the retail GetMatrix() used by DisplayActualLight lazily allocates
    // one from the entity placement, as it does for SA's own traffic lights.
    bool process_entity(uintptr_t entity) const noexcept {
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

        using DisplayActualLight = void(__cdecl*)(uintptr_t);
        reinterpret_cast<DisplayActualLight>(sa_map_traffic_lights_display_actual_light)(entity);
        return true;
    }

private:
    uint16_t model_id_ = 0;
    std::set<uint16_t> bridge_models_;
};

} // namespace lcs
