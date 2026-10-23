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
inline constexpr uintptr_t sa_map_traffic_lights_model_slot = 0x8CD4F4;
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
        signal_passes_=seen_=hidden_=no_info_=no_matrix_=bad_effect_=fault_=0;effect_count_=bad_type_=six_count_=0;calls_=with_2dfx_=0;
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
    // One-line summary of why the signal pass did or did not run, for the log.
    std::string diagnostic() const {
        std::string six;for(unsigned i=0;i<six_count_;++i)six+=(i?",":"")+std::to_string(six_models_[i]);
        return "calls="+std::to_string(calls_)+" with2dfx="+std::to_string(with_2dfx_)+" six=["+six+"] seen="+std::to_string(seen_)+" hidden="+std::to_string(hidden_)+
               " handled="+std::to_string(signal_passes_)+" noInfo="+std::to_string(no_info_)+
               " effectCount="+std::to_string(effect_count_)+" noMatrix="+std::to_string(no_matrix_)+
               " badEffect="+std::to_string(bad_effect_)+" badEffectType="+std::to_string(bad_type_)+
               " fault="+std::to_string(fault_);
    }
    // CEntity::PreRender compares the model index with SA's traffic-light model
    // words and then runs CTrafficLights::DisplayActualLight, which reads the
    // model's own light 2DFX and draws the active phase. Register the converted
    // model in the first word (SA's own trafficlight1 is not part of this map).
    bool bind_native_slot() noexcept {
        if (model_id_ == 0 || slot_bound_) return slot_bound_;
        __try {
            auto* slot = reinterpret_cast<volatile uint16_t*>(sa_map_traffic_lights_model_slot);
            if (*slot == 0 || *slot >= 20000) return false;
            slot_original_ = *slot;
            *slot = model_id_;
            slot_bound_ = true;
        } __except (1) {
            return false;
        }
        return true;
    }
    void release_native_slot() noexcept {
        if (!slot_bound_) return;
        __try {
            *reinterpret_cast<volatile uint16_t*>(sa_map_traffic_lights_model_slot) = slot_original_;
        } __except (1) {}
        slot_bound_ = false;
    }
    uint64_t signal_seen() const noexcept { return calls_; }

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
        if (calls_!=UINT64_MAX) ++calls_;
        __try {
            const auto probe=*reinterpret_cast<const uintptr_t*>(0xA9B0C8+4*model_id);
            if(probe && *reinterpret_cast<const uint8_t*>(probe+0x0D)) {
                ++with_2dfx_;
                if(*reinterpret_cast<const uint8_t*>(probe+0x0D)==6) {
                    bool known=false;for(unsigned i=0;i<six_count_;++i)known|=six_models_[i]==model_id;
                    if(!known && six_count_<12)six_models_[six_count_++]=model_id;
                }
            }
        } __except (1) {}
        if (handles_model(model_id) && seen_!=UINT64_MAX) ++seen_;
        if (!visible) {if(handles_model(model_id))++hidden_;return false;}
        // Native bridge flashiness checks SA's own bridge object flags. Gate
        // the ordinary native blink instead using the converted lift's state.
        if(bridge_models_.count(model_id))return !sa_lcs_bridge_lights_flashing();
        if (!handles_model(model_id)) return false;

        // The signal itself is drawn by SA's own PreRender branch once the
        // model is registered in its traffic-light model table (bind_native_slot);
        // light effects remain the unchanged converted LCS records.
        return false;
    }

private:
    uint16_t model_id_ = 0;
    uint16_t slot_original_ = 0;
    bool slot_bound_ = false;
    std::set<uint16_t> bridge_models_;
    mutable uint64_t signal_passes_=0,seen_=0,hidden_=0,no_info_=0,no_matrix_=0,bad_effect_=0,fault_=0;
    mutable uint32_t effect_count_=0,bad_type_=0,six_count_=0;
    mutable uint64_t calls_=0,with_2dfx_=0;
    mutable uint16_t six_models_[12]{};
};

} // namespace lcs
