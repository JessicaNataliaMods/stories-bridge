#pragma once
#include "vm.hpp"
#include <optional>

namespace lcs {
// Exact inputs to retail PlaceMarker249FF0, not native SA marker type aliases.
struct ScriptMarkerPlacement {
    uintptr_t identity=0;
    uint16_t type=0,pulse_period=0;
    int16_t rotate_rate=0;
    std::array<float,3> position{};
    std::optional<std::array<float,3>> target;
    // Retail Render scales X/Y by current size and Z by vertical_size.
    // PlaceMarker resolves a nonpositive vertical_size to current size.
    float size=0,pulse_fraction=0,vertical_size=0;
    std::array<uint8_t,4> colour{};
    bool force_position=false,persistent_fade=false;
};
struct ScriptMarkerCommand {
    ScriptMarkerPlacement placement;
    // CopyMarker searches the first source identity, re-places its CURRENT
    // size under new_identity with vertical_size=100 and no target. It is not
    // a memcpy or a duplicate of the new placement.
    struct Copy {uintptr_t source_identity,new_identity;bool force_position,persistent_fade;};
    std::optional<Copy> copy_before;
};
// This adapter is deliberately NOT registered in Campaign until an actual
// renderer implements the whole submit contract, including copy and lifetime.
class ScriptMarkers {
public:
    struct ProcessState {std::array<float,3> previous_point{};};
    explicit ScriptMarkers(ProcessState& process):process_(process){}
    std::function<bool()> available;
    std::function<std::optional<float>(float,float)> ground;
    // True means the whole original operation was accepted, including valid
    // no-slot/no-source results. False is a backend failure, not pool exhaustion.
    std::function<bool(const ScriptMarkerCommand&)> submit;
    static bool handles(uint16_t);
    StepResult dispatch(Vm&,Thread&,uint16_t);
private:
    ProcessState& process_;
};
}
