#pragma once
#include <cstdint>
namespace lcs {
struct SaAmbientGunfireProfile {
    static constexpr uintptr_t call=0x50781E,target=0x4DF210;
};
// Only the ambient generator is filtered. Real weapon events use other calls.
template<class Native> void dispatch_ambient_gunfire(bool campaign_active,Native native) {
    if(!campaign_active)native();
}
}
