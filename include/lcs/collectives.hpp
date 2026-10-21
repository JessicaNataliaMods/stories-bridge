#pragma once
#include "native.hpp"
namespace lcs {
struct CollectiveCandidate {ObjectiveActor actor;int32_t type=0;uint8_t created_by=0;};
// Original global table: 32 entries shared by all script threads, not 32 per group.
class Collectives {
    friend struct CampaignServicesSave;
    struct Entry {int32_t group=-1;int32_t ped=0;};
    std::array<Entry,32> entries_{};
    int32_t next_=0;
    bool poisoned_=false;
public:
    std::function<bool()> available;
    std::function<std::optional<std::vector<CollectiveCandidate>>(float,float,float)> candidates;
    std::function<std::optional<ObjectiveActor>(int32_t)> live;
    std::function<NativeResult(const ObjectiveActor&,const ObjectiveActor&)> flee;
    // 614: finite SA flee from a character (not the ALWAYS packet).
    std::function<NativeResult(const ObjectiveActor&,const ObjectiveActor&)> flee_till_safe;
    // 606: finite SA flee from the ped's own current position; no target operand.
    std::function<NativeResult(const ObjectiveActor&)> flee_point_till_safe;
    static bool handles(uint16_t);
    StepResult dispatch(Vm&,Thread&,uint16_t);
};
}
