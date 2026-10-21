#pragma once
#include "vm.hpp"
#include <optional>

namespace lcs {
// Native HUD entries point into SA's script space, never into the LCS image.
// These cells are reserved in the verified native bootstrap's zero-filled bank.
struct HudRequest {
    enum class Action { add_clock, clear_clock, add_counter, clear_counter, freeze, counter_prefix };
    Action action;
    uint32_t native_offset=0;
    int32_t type=0;
    size_t line=0;
    std::string label;
    int32_t total=-1;
    std::string heading;
    uint16_t total_type=0;
    std::array<uint8_t,4> foreground{{112,132,157,255}},background{{42,58,81,255}};
    bool custom=false;
};
class HudBridge {
    friend struct CampaignServicesSave;
    struct Entry {uint32_t original=0;bool active=false;};
    std::array<Entry,4> entries_{}; // one original clock and three counters
    bool frozen_=false;
    std::array<std::optional<HudRequest>,4> saved_requests_{};
    std::array<std::optional<bool>,3> saved_prefixes_{};
    std::array<uint8_t,4> pending_foreground_{{255,255,255,255}},pending_background_{{255,255,255,255}};
public:
    static bool handles(uint16_t);
    static constexpr uint32_t native_cell(size_t slot) {return 32+uint32_t(slot)*4;}
    std::function<bool(const HudRequest&)> update;
    std::function<std::optional<int32_t>(uint32_t)> read_cell;
    std::function<bool(uint32_t,int32_t)> write_cell;
    StepResult dispatch(Vm&,Thread&,uint16_t);
    // The native clock advances once in SA's frame loop. Pull before the LCS
    // scheduler and push after it, including assignments from any worker.
    bool pull(Vm&);
    bool push(const Vm&);
    bool clear();
};
}
