#pragma once
#include <cstdint>

namespace lcs {
// RenderWare's road pass bypasses CEntity::bBackfaceCulled.
// Apply the state at the atomic callback and restore the incoming value,
// including when the previous callback throws or renders another atomic.
template<class Get,class Set,class Draw>
auto draw_map_double_sided(Get get,Set set,Draw draw) -> decltype(draw()) {
    std::uint32_t previous=0;
    if(!get(previous))return draw();
    if(previous!=1 && !set(1))return draw(); // rwCULLMODECULLNONE
    struct Restore {
        Set& set;std::uint32_t previous;
        ~Restore(){set(previous);}
    } restore{set,previous};
    return draw();
}
}
