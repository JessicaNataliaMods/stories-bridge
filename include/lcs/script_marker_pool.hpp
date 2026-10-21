#pragma once
#include "script_markers.hpp"

namespace lcs {
// Private source pool: no pointers into SA's shared C3dMarkers array.
struct ScriptMarkerInstance {
    ScriptMarkerPlacement source;
    std::array<float,12> matrix{}; // right,forward,up,position; padding excluded
    float standard_size=1,current_size=1,vertical_size=1,camera_range=0;
    uint32_t start_time=0;
    bool live=false,used=false,find_z=false;
};
struct ScriptMarkerEnvironment {
    uint32_t now=0;
    float time_step=1;
    std::array<float,3> player{};
    struct Floor {
        // false: original source area was not queryable; retry on placement.
        bool queryable=false;
        // A queryable zero/no-floor result clears the retry flag in retail.
        std::optional<float> height;
    };
    std::function<std::optional<Floor>(const std::array<float,3>&)> floor;
};
class ScriptMarkerPool {
public:
    using Draw=std::function<bool(size_t,const ScriptMarkerInstance&)>;
    bool submit(const ScriptMarkerCommand&,const ScriptMarkerEnvironment&);
    // Called in the real marker render phase, never once per VM instruction.
    // Expiry/fade/dedup are source operations, not inferred from native slots.
    bool frame(const Draw&);
    void clear(){entries_={};}
    const std::array<ScriptMarkerInstance,32>& entries()const{return entries_;}
private:
    std::array<ScriptMarkerInstance,32> entries_{};
    bool place(const ScriptMarkerPlacement&,const ScriptMarkerEnvironment&);
};
}
