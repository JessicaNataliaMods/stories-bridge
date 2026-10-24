#pragma once
#include <cstdint>
namespace lcs {
// The native routine owns its existing sound handles even after LCS takes over.
// It must receive the masked inputs every frame so it can stop fallback voices.
template<class Native> void dispatch_vehicle_horn_siren(int32_t horn,int32_t siren,int32_t fast,
                                                       bool taken_horn,bool taken_siren,Native native) {
    native(taken_horn?0:(horn&0xFF),taken_siren?0:(siren&0xFF),taken_siren?0:(fast&0xFF));
}
}
