#pragma once
#include <limits>
namespace lcs {
// CTaskComplexSeekEntity chooses node routes outside this radius and direct
// movement inside it. Its standard/offset variants share the prefix at 0x20.
// Zero allows node routes; FLT_MAX keeps the original no-node seek policy.
inline float native_seek_radius(bool use_nodes) {
    return use_nodes?0.0f:std::numeric_limits<float>::max();
}
}
