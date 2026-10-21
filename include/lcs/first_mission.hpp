#pragma once
#include "runtime.hpp"
#include "native.hpp"

namespace lcs {
// Explicit first-mission test profile. Objectives, dialogue, destinations and
// pass/fail logic execute from the original VIC1 bytecode. Mounted opening
// scenes run through the original shared movie function, before VIC1.
class FirstMission {
    Runtime& runtime_;
    NativeAdapter& adapter_;
    int32_t radio_=13;
    bool radio_enabled_=true;
    ScriptId root_=0;
    uint32_t retry_at_=0;
    bool pending_retry_=false;
    bool arrival_playing_=false,arrival_completed_=false;
    bool safehouse_group_=false;
    std::array<float,3> scene_light_{};
    bool rubbish_enabled_=false;
    uint32_t outfit_changes_=0;
    NativeResult native(NativeCall call);
    void put(uint32_t offset,int32_t value);
    bool before(Vm&,Thread&);
    ScriptId start_vic1();
public:
    bool passed=false,finished=false;
    bool opening_movie=false;
    bool arrival_movie=false;
    ScriptId root() const {return root_;}
    NativeAdapter::Executor execute;
    std::function<void(const std::string&)> log;
    FirstMission(const ScmImage&,Runtime&,NativeAdapter&);
    ScriptId start();
    StepResult dispatch(Vm&,Thread&,uint16_t);
    void tick(uint32_t now);
    static uint32_t fingerprint(const ScmImage&);
    // First-mission-only compatibility for the original pre-boarding dead-car
    // guard. The full campaign may install this same VM hook when this exact
    // source image is active.
    static bool supports_verified_image(const ScmImage&);
    static bool redirect_destroyed_car_failure(Vm&,Thread&);
};
}
