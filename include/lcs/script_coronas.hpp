#pragma once
#include "vm.hpp"
#include <list>
#include <optional>
namespace lcs {
struct ScriptCorona {
    uint32_t id=0;
    std::array<float,3> position{};
    float size=0;
    uint8_t type=0,flare=0,r=0,g=0,b=0;
    bool active=true;
};
class ScriptCoronas {
    inline static uint32_t next_id_=0; // original process-global counter, not reset by script Init
    // Retired nodes retain unique renderer IDs until this campaign is destroyed.
    // Never let an allocator recycle an ID while the native corona fades out.
    std::list<ScriptCorona> objects_;
public:
    std::function<bool()> available;
    std::function<std::optional<float>(float,float)> ground;
    std::function<bool(uintptr_t,const ScriptCorona&)> render;
    static bool handles(uint16_t);
    StepResult dispatch(Vm&,Thread&,uint16_t);
    bool tick()const;
    void clear(){for(auto& object:objects_)object.active=false;}
};
}
