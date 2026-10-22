#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace lcs {
// PS2 scripts count WAIT 0 cycles at 30 Hz. Rendering and native AI may run
// faster; retain elapsed game time without replaying several AI observations
// against the same world snapshot after a slow frame.
class ScriptCadence {
    // Game timestamps are integral milliseconds: allow their sub-ms rounding
    // at a 33/34 ms boundary without dropping a cycle at native 30 FPS.
    uint64_t phase_=30;
    uint32_t elapsed_=0;
    float step_=0;
public:
    struct Slice {uint32_t elapsed;float step;};
    std::optional<Slice> advance(uint32_t elapsed,float step) {
        if(!elapsed || !std::isfinite(step) || step<0)return {};
        phase_+=uint64_t(elapsed)*30;
        elapsed_+=elapsed;step_+=step;
        if(phase_<1000)return {};
        phase_%=1000;
        const Slice result{elapsed_,std::min(step_,3.0f)};
        elapsed_=0;step_=0;
        return result;
    }
};
}
