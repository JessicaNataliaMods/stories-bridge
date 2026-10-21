#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <map>
namespace lcs {
struct AnimationSequence {std::string name;int32_t bone_tag=-1;uint32_t keyframes=0;float duration=0;};
struct Animation {std::string name;std::vector<AnimationSequence> sequences;};
struct AnimationArchive {
    std::string name;
    std::vector<Animation> animations;
    static AnimationArchive parse_anpk(const std::vector<uint8_t>& bytes);
    static AnimationArchive load_anpk(const std::filesystem::path& path);
};
struct AnimationAdaptation {
    std::vector<uint8_t> bytes;
    size_t reordered_sequences=0,renamed_sequences=0,tagged_sequences=0;
};
// Keep complete keyframe records and stable ordering at equal times. Native
// loading subtracts successive absolute times, so decreasing export times
// must be ordered before they become interpolation durations.
using AnimationBoneMap=std::map<std::pair<std::string,std::string>,int32_t>;
AnimationAdaptation adapt_anpk(const std::vector<uint8_t>& bytes,const std::map<std::string,std::string>& frame_names={},const AnimationBoneMap& bones={},const std::string& block_name={});
}
