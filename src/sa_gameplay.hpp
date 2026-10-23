#pragma once
#include "lcs/animation.hpp"
#include "lcs/riot_wait.hpp"
#include <filesystem>
#include <map>
#include <set>
#include <optional>
#include <random>
#include <string>
#include <vector>
namespace lcs {
std::string gameplay_animation_load_diagnostic();
// Diagnostic lines about mission gesture playback; the caller drains them into its log.
std::vector<std::string> take_gameplay_animation_trace();
class NativeGameplayAnimations {
    struct Gesture {std::string block,animation;uint16_t flags;std::string logical_block;};
    struct Block {std::filesystem::path path;AnimationArchive archive;uintptr_t native=0;int32_t index=-1;bool registered=false;bool reference_held=false;};
    std::map<int32_t,Gesture> gestures_;
    std::map<std::string,Block> blocks_;
    std::map<std::string,std::string> named_blocks_;
    struct Pose {uintptr_t hierarchy;bool timed;uint32_t deadline;int32_t state=0;uint32_t next_trace=0;};
    std::map<int32_t,Pose> poses_;
    struct RiotPose {uintptr_t hierarchy=0;RiotWaitCycle cycle;};
    std::map<int32_t,RiotPose> riots_;
    std::array<std::string,7> riot_names_;
    std::mt19937 random_{std::random_device{}()};
    std::set<std::string> requested_;
    uint32_t now_=0;
    uint32_t next_census_=0;
    uint32_t next_sunk_check_=0;
public:
    explicit NativeGameplayAnimations(const std::filesystem::path& directory);
    ~NativeGameplayAnimations();
    bool request(int32_t state,bool load);
    std::optional<bool> named(const std::string& name,uint16_t action);
    bool play(int32_t actor,int32_t state,int32_t duration,bool repeat);
    bool clear(int32_t actor);
    // Script-owned wait only; ambient SA AI is deliberately left native.
    // Query live pose lifetime so a finished SA association is not a stale wait.
    std::optional<bool> waiting(int32_t actor) const;
    bool tick(uint32_t now);
};
}
